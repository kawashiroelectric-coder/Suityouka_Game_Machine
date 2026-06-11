// ============================================
// ファイル: lua_audio.cpp
// Core0: BGM SD ストリーム + SE RAM 読み込み
// Core1: BGM + SE 8ch + トーンをミキシング → I2S
// ============================================

#include "lua_audio.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "audio_output.hpp"
#include "heap_budget.hpp"
#include "hardware/sync.h"

extern "C" {
#include "f_util.h"
#include "ff.h"
}

namespace {

constexpr float kPi = 3.14159265f;
constexpr int16_t kToneAmplitude = 12000;

/** Core0 のみ: BGM ストリーム用 FatFS ハンドル */
FIL s_bgm_file;

int32_t clampSample(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

uint32_t readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

void setError(char* errbuf, size_t errbuf_len, const char* msg) {
    if (!errbuf || errbuf_len == 0) return;
    snprintf(errbuf, errbuf_len, "%s", msg);
}

/** 開済み WAV ファイルから fmt/data を探し data 先頭へシーク。成功時 data_size を返す */
bool parseWavPcm16(FIL& file, uint16_t* num_channels, uint32_t* sample_rate,
                   uint32_t* data_size, char* errbuf, size_t errbuf_len) {
    FSIZE_t fsize = f_size(&file);
    if (fsize < 44) {
        setError(errbuf, errbuf_len, "invalid file size");
        return false;
    }

    uint8_t header[12];
    UINT br = 0;
    FRESULT fr = f_read(&file, header, sizeof(header), &br);
    if (fr != FR_OK || br != sizeof(header)) {
        setError(errbuf, errbuf_len, "read header failed");
        return false;
    }
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        setError(errbuf, errbuf_len, "not RIFF WAVE");
        return false;
    }

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t rate = 0;
    uint16_t bits_per_sample = 0;
    uint32_t data_bytes = 0;
    bool got_fmt = false;
    bool got_data = false;

    uint8_t chunk_hdr[8];
    while (true) {
        fr = f_read(&file, chunk_hdr, 8, &br);
        if (fr != FR_OK || br != 8) break;

        const uint32_t chunk_size = readLe32(chunk_hdr + 4);
        if (memcmp(chunk_hdr, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                setError(errbuf, errbuf_len, "bad fmt chunk");
                return false;
            }
            uint8_t fmt[16];
            fr = f_read(&file, fmt, 16, &br);
            if (fr != FR_OK || br != 16) {
                setError(errbuf, errbuf_len, "read fmt failed");
                return false;
            }
            audio_format = readLe16(fmt);
            channels = readLe16(fmt + 2);
            rate = readLe32(fmt + 4);
            bits_per_sample = readLe16(fmt + 14);
            if (chunk_size > 16) {
                f_lseek(&file, f_tell(&file) + static_cast<FSIZE_t>(chunk_size - 16));
            }
            got_fmt = true;
        } else if (memcmp(chunk_hdr, "data", 4) == 0) {
            data_bytes = chunk_size;
            got_data = true;
            break;
        } else {
            f_lseek(&file, f_tell(&file) + chunk_size);
        }
    }

    if (!got_fmt || !got_data || data_bytes == 0) {
        setError(errbuf, errbuf_len, "missing fmt/data");
        return false;
    }
    if (audio_format != 1 || bits_per_sample != 16 || (channels != 1 && channels != 2)) {
        setError(errbuf, errbuf_len, "need 16bit PCM mono/stereo");
        return false;
    }

    *num_channels = channels;
    *sample_rate = rate;
    *data_size = data_bytes;
    return true;
}

}  // namespace

static_assert(LuaAudio::kStreamFrames == AudioConfig::STREAM_BUFFER_FRAMES,
              "stream and I2S buffer frame counts must match");

LuaAudio* LuaAudio::instance_ = nullptr;

LuaAudio::LuaAudio()
    : output_(nullptr),
      sample_rate_(AudioConfig::SAMPLE_RATE),
      sd_mounted_(false),
      volume_(1.0f),
      tone_active_(false),
      tone_freq_(0.0f),
      tone_phase_(0.0f),
      tone_samples_left_(0),
      bgm_active_(false),
      bgm_eof_(false),
      bgm_file_open_(false),
      bgm_channels_(0),
      bgm_source_rate_(0),
      bgm_data_remaining_(0),
      bgm_src_pos_(0.0),
      bgm_file_frame_next_(0),
      bgm_cur_l_(0),
      bgm_cur_r_(0),
      se_load_counter_(0) {
    instance_ = this;
    resetStreamSlotsLocked();
    for (size_t i = 0; i < kSeChannels; i++) {
        se_channels_[i] = SeChannel{};
    }
}

LuaAudio::~LuaAudio() {
    stop();
    instance_ = nullptr;
}

void LuaAudio::attach(AudioOutput* output, uint32_t sample_rate) {
    output_ = output;
    sample_rate_ = sample_rate;
}

void LuaAudio::audioCallback(int16_t* left, int16_t* right, size_t samples) {
    if (instance_) {
        instance_->fill(left, right, samples);
    } else {
        memset(left, 0, samples * sizeof(int16_t));
        memset(right, 0, samples * sizeof(int16_t));
    }
}

void LuaAudio::setVolume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    volume_ = volume;
    if (output_) {
        output_->setVolume(volume);
    }
}

bool LuaAudio::isAudioActive() const {
    if (bgm_active_ || tone_active_) {
        return true;
    }
    for (size_t i = 0; i < kSeChannels; i++) {
        if (se_channels_[i].active) {
            return true;
        }
    }
    return false;
}

void LuaAudio::ensureOutputPlaying() {
    if (output_ && !output_->isPlaying()) {
        output_->start();
    }
}

void LuaAudio::resetStreamSlotsLocked() {
    for (StreamSlot& slot : stream_slots_) {
        memset(slot.left, 0, sizeof(slot.left));
        memset(slot.right, 0, sizeof(slot.right));
        slot.frames = 0;
        slot.state = kSlotEmpty;
    }
}

void LuaAudio::stopBgmLocked() {
    bgm_active_ = false;
    bgm_eof_ = false;
    bgm_src_pos_ = 0.0;
    bgm_file_frame_next_ = 0;
    bgm_cur_l_ = 0;
    bgm_cur_r_ = 0;
}

void LuaAudio::stopToneLocked() {
    tone_active_ = false;
    tone_samples_left_ = 0;
}

void LuaAudio::freeSeChannelLocked(int index) {
    if (index < 0 || index >= static_cast<int>(kSeChannels)) {
        return;
    }
    SeChannel& ch = se_channels_[index];
    ch.active = false;
    if (ch.data) {
        HeapBudget::release(ch.data, ch.byte_size);
    }
    ch.data = nullptr;
    ch.byte_size = 0;
    ch.frame_count = 0;
    ch.channels = 0;
    ch.source_rate = 0;
    ch.position = 0.0;
    ch.load_serial = 0;
}

void LuaAudio::stopSeLocked() {
    for (size_t i = 0; i < kSeChannels; i++) {
        freeSeChannelLocked(static_cast<int>(i));
    }
}

void LuaAudio::closeBgmFileLocked() {
    if (bgm_file_open_) {
        f_close(&s_bgm_file);
        bgm_file_open_ = false;
    }
    bgm_channels_ = 0;
    bgm_source_rate_ = 0;
    bgm_data_remaining_ = 0;
}

void LuaAudio::stop() {
    uint32_t irq = save_and_disable_interrupts();
    stopBgmLocked();
    stopSeLocked();
    stopToneLocked();
    resetStreamSlotsLocked();
    restore_interrupts(irq);

    closeBgmFileLocked();

    if (output_) {
        output_->stop();
    }
}

bool LuaAudio::playTone(float frequency_hz, float duration_ms) {
    if (!output_ || frequency_hz <= 0.0f || duration_ms <= 0.0f) {
        return false;
    }
    const uint32_t samples =
        static_cast<uint32_t>(duration_ms * static_cast<float>(sample_rate_) / 1000.0f);
    if (samples == 0) {
        return false;
    }

    uint32_t irq = save_and_disable_interrupts();
    stopToneLocked();
    tone_freq_ = frequency_hz;
    tone_phase_ = 0.0f;
    tone_samples_left_ = samples;
    tone_active_ = true;
    restore_interrupts(irq);

    ensureOutputPlaying();
    return true;
}

bool LuaAudio::openBgmForStream(const char* path, char* errbuf, size_t errbuf_len) {
    closeBgmFileLocked();

    FRESULT fr = f_open(&s_bgm_file, path, FA_READ);
    if (fr != FR_OK) {
        setError(errbuf, errbuf_len, "open failed");
        return false;
    }
    bgm_file_open_ = true;

    uint16_t channels = 0;
    uint32_t rate = 0;
    uint32_t data_size = 0;
    if (!parseWavPcm16(s_bgm_file, &channels, &rate, &data_size, errbuf, errbuf_len)) {
        closeBgmFileLocked();
        return false;
    }

    bgm_channels_ = channels;
    bgm_source_rate_ = rate;
    bgm_data_remaining_ = data_size;
    bgm_src_pos_ = 0.0;
    bgm_file_frame_next_ = 0;
    bgm_cur_l_ = 0;
    bgm_cur_r_ = 0;
    bgm_eof_ = false;
    return true;
}

bool LuaAudio::readNextBgmFrame(int16_t* out_l, int16_t* out_r) {
    if (!bgm_file_open_ || bgm_data_remaining_ == 0) {
        return false;
    }

    const size_t frame_bytes = static_cast<size_t>(bgm_channels_) * sizeof(int16_t);
    int16_t raw[2];
    UINT br = 0;
    const FRESULT fr = f_read(&s_bgm_file, raw, static_cast<UINT>(frame_bytes), &br);
    if (fr != FR_OK || br != frame_bytes) {
        bgm_data_remaining_ = 0;
        return false;
    }
    bgm_data_remaining_ -= static_cast<uint32_t>(frame_bytes);

    if (bgm_channels_ == 1) {
        *out_l = raw[0];
        *out_r = raw[0];
    } else {
        *out_l = raw[0];
        *out_r = raw[1];
    }
    bgm_file_frame_next_++;
    bgm_cur_l_ = *out_l;
    bgm_cur_r_ = *out_r;
    return true;
}

bool LuaAudio::fillStreamSlot(StreamSlot& slot) {
    if (!bgm_file_open_) {
        return false;
    }

    const double rate_step =
        (bgm_source_rate_ > 0 && sample_rate_ > 0)
            ? static_cast<double>(bgm_source_rate_) / static_cast<double>(sample_rate_)
            : 1.0;

    size_t out = 0;

    while (out < kStreamFrames) {
        const size_t src_idx = static_cast<size_t>(bgm_src_pos_);
        while (bgm_file_frame_next_ <= src_idx && bgm_data_remaining_ > 0) {
            if (!readNextBgmFrame(&bgm_cur_l_, &bgm_cur_r_)) {
                break;
            }
        }

        if (src_idx >= bgm_file_frame_next_ && bgm_data_remaining_ == 0) {
            break;
        }
        if (bgm_file_frame_next_ == 0) {
            break;
        }

        slot.left[out] = bgm_cur_l_;
        slot.right[out] = bgm_cur_r_;
        out++;
        bgm_src_pos_ += rate_step;
    }

    if (out == 0) {
        if (bgm_data_remaining_ == 0) {
            bgm_eof_ = true;
        }
        return false;
    }

    if (out < kStreamFrames) {
        memset(slot.left + out, 0, (kStreamFrames - out) * sizeof(int16_t));
        memset(slot.right + out, 0, (kStreamFrames - out) * sizeof(int16_t));
        if (bgm_data_remaining_ == 0) {
            bgm_eof_ = true;
        }
    }

    slot.frames = kStreamFrames;
    return true;
}

int LuaAudio::countReadySlots() const {
    int count = 0;
    for (const StreamSlot& slot : stream_slots_) {
        if (slot.state == kSlotReady) {
            count++;
        }
    }
    return count;
}

void LuaAudio::pumpStream() {
    if (!bgm_active_) {
        if (bgm_file_open_) {
            closeBgmFileLocked();
        }
        return;
    }
    if (!bgm_file_open_) {
        return;
    }

    for (StreamSlot& slot : stream_slots_) {
        if (slot.state != kSlotEmpty) {
            continue;
        }
        if (!fillStreamSlot(slot)) {
            break;
        }
        __dmb();
        slot.state = kSlotReady;
    }

    if (bgm_eof_ && countReadySlots() == 0) {
        bool any_empty = false;
        for (const StreamSlot& slot : stream_slots_) {
            if (slot.state == kSlotEmpty) {
                any_empty = true;
                break;
            }
        }
        if (!any_empty) {
            bgm_active_ = false;
        }
    }
}

void LuaAudio::primeStreamBuffers() {
    for (int i = 0; i < 4; i++) {
        pumpStream();
        const int ready = countReadySlots();
        if (ready >= 2) {
            break;
        }
        if (bgm_eof_ && ready > 0) {
            break;
        }
        if (bgm_eof_ && ready == 0) {
            break;
        }
    }
}

bool LuaAudio::playWav(const char* path, char* errbuf, size_t errbuf_len) {
    if (errbuf && errbuf_len > 0) {
        errbuf[0] = '\0';
    }
    if (!output_ || !sd_mounted_ || !path || path[0] == '\0') {
        setError(errbuf, errbuf_len, "audio not ready");
        return false;
    }

    if (!openBgmForStream(path, errbuf, errbuf_len)) {
        return false;
    }

    uint32_t irq = save_and_disable_interrupts();
    stopBgmLocked();
    resetStreamSlotsLocked();
    bgm_active_ = true;
    bgm_eof_ = false;
    restore_interrupts(irq);

    primeStreamBuffers();

    if (countReadySlots() == 0) {
        uint32_t irq2 = save_and_disable_interrupts();
        stopBgmLocked();
        resetStreamSlotsLocked();
        restore_interrupts(irq2);
        closeBgmFileLocked();
        setError(errbuf, errbuf_len, "empty or unreadable wav");
        return false;
    }

    ensureOutputPlaying();
    return true;
}

int LuaAudio::allocateSeSlotLocked() {
    for (size_t i = 0; i < kSeChannels; i++) {
        if (!se_channels_[i].active) {
            return static_cast<int>(i);
        }
    }

    int oldest = 0;
    uint32_t min_serial = se_channels_[0].load_serial;
    for (size_t i = 1; i < kSeChannels; i++) {
        if (se_channels_[i].load_serial < min_serial) {
            min_serial = se_channels_[i].load_serial;
            oldest = static_cast<int>(i);
        }
    }
    return oldest;
}

bool LuaAudio::playSe(const char* path, char* errbuf, size_t errbuf_len) {
    if (errbuf && errbuf_len > 0) {
        errbuf[0] = '\0';
    }
    if (!output_ || !sd_mounted_ || !path || path[0] == '\0') {
        setError(errbuf, errbuf_len, "audio not ready");
        return false;
    }

    int16_t* new_pcm = nullptr;
    size_t new_pcm_bytes = 0;
    size_t new_frames = 0;
    uint16_t new_ch = 0;
    uint32_t new_rate = 0;

    {
        FIL file;
        FRESULT fr = f_open(&file, path, FA_READ);
        if (fr != FR_OK) {
            setError(errbuf, errbuf_len, "open failed");
            return false;
        }

        uint16_t channels = 0;
        uint32_t rate = 0;
        uint32_t data_size = 0;
        if (!parseWavPcm16(file, &channels, &rate, &data_size, errbuf, errbuf_len)) {
            f_close(&file);
            return false;
        }
        if (data_size > kMaxSeBytes) {
            f_close(&file);
            setError(errbuf, errbuf_len, "se too large");
            return false;
        }

        void* alloc_ptr = nullptr;
        if (!HeapBudget::tryAlloc(data_size, &alloc_ptr)) {
            f_close(&file);
            setError(errbuf, errbuf_len, "heap budget exceeded");
            return false;
        }
        new_pcm = static_cast<int16_t*>(alloc_ptr);
        new_pcm_bytes = data_size;

        UINT br = 0;
        fr = f_read(&file, new_pcm, data_size, &br);
        f_close(&file);
        if (fr != FR_OK || br != data_size) {
            HeapBudget::release(new_pcm, data_size);
            setError(errbuf, errbuf_len, "read data failed");
            return false;
        }

        new_frames = data_size / (static_cast<size_t>(channels) * sizeof(int16_t));
        if (new_frames == 0) {
            HeapBudget::release(new_pcm, data_size);
            setError(errbuf, errbuf_len, "empty data");
            return false;
        }
        new_ch = channels;
        new_rate = rate;
    }

    uint32_t irq = save_and_disable_interrupts();
    const int slot = allocateSeSlotLocked();
    SeChannel& ch = se_channels_[slot];
    ch.active = false;
    __dmb();
    if (ch.data) {
        HeapBudget::release(ch.data, ch.byte_size);
    }
    ch.data = new_pcm;
    ch.byte_size = new_pcm_bytes;
    ch.frame_count = new_frames;
    ch.channels = new_ch;
    ch.source_rate = new_rate;
    ch.position = 0.0;
    ch.load_serial = ++se_load_counter_;
    __dmb();
    ch.active = true;
    restore_interrupts(irq);

    ensureOutputPlaying();
    return true;
}

bool LuaAudio::consumeStreamSlot(int16_t* left, int16_t* right, size_t frames) {
    for (StreamSlot& slot : stream_slots_) {
        if (slot.state != kSlotReady) {
            continue;
        }

        const size_t n = (slot.frames < frames) ? slot.frames : frames;
        memcpy(left, slot.left, n * sizeof(int16_t));
        memcpy(right, slot.right, n * sizeof(int16_t));
        if (n < frames) {
            memset(left + n, 0, (frames - n) * sizeof(int16_t));
            memset(right + n, 0, (frames - n) * sizeof(int16_t));
        }

        __dmb();
        slot.state = kSlotEmpty;
        return true;
    }
    return false;
}

void LuaAudio::mixSeChannels(int16_t* left, int16_t* right, size_t samples) {
    for (size_t c = 0; c < kSeChannels; c++) {
        SeChannel& ch = se_channels_[c];
        if (!ch.active || !ch.data) {
            continue;
        }

        const double rate_step =
            (ch.source_rate > 0 && sample_rate_ > 0)
                ? static_cast<double>(ch.source_rate) / static_cast<double>(sample_rate_)
                : 1.0;

        double pos = ch.position;
        bool still_active = true;

        for (size_t i = 0; i < samples; i++) {
            const size_t idx = static_cast<size_t>(pos);
            if (idx >= ch.frame_count) {
                still_active = false;
                break;
            }

            int32_t sl;
            int32_t sr;
            if (ch.channels == 1) {
                sl = sr = ch.data[idx];
            } else {
                sl = ch.data[idx * 2];
                sr = ch.data[idx * 2 + 1];
            }

            left[i] = static_cast<int16_t>(clampSample(static_cast<int32_t>(left[i]) + sl));
            right[i] = static_cast<int16_t>(clampSample(static_cast<int32_t>(right[i]) + sr));
            pos += rate_step;
        }

        ch.position = pos;
        if (!still_active || static_cast<size_t>(pos) >= ch.frame_count) {
            ch.active = false;
        }
    }
}

void LuaAudio::fill(int16_t* left, int16_t* right, size_t samples) {
    const float vol = volume_;
    const bool tone_on = tone_active_;
    const float tone_freq = tone_freq_;
    float tone_phase = tone_phase_;
    uint32_t tone_left = tone_samples_left_;

    const bool bgm_on = bgm_active_;
    const bool bgm_eof = bgm_eof_;

    if (bgm_on) {
        if (!consumeStreamSlot(left, right, samples)) {
            memset(left, 0, samples * sizeof(int16_t));
            memset(right, 0, samples * sizeof(int16_t));
            if (bgm_eof) {
                bgm_active_ = false;
            }
        }
    } else {
        memset(left, 0, samples * sizeof(int16_t));
        memset(right, 0, samples * sizeof(int16_t));
    }

    mixSeChannels(left, right, samples);

    if (!tone_on || tone_left == 0) {
        tone_phase_ = tone_phase;
        tone_samples_left_ = tone_left;
        if (tone_left == 0) {
            tone_active_ = false;
        }
        return;
    }

    for (size_t i = 0; i < samples; i++) {
        if (tone_left == 0) {
            break;
        }
        const int16_t t =
            static_cast<int16_t>(sinf(tone_phase) * static_cast<float>(kToneAmplitude) * vol);
        left[i] = static_cast<int16_t>(clampSample(static_cast<int32_t>(left[i]) + t));
        right[i] = static_cast<int16_t>(clampSample(static_cast<int32_t>(right[i]) + t));
        tone_phase += 2.0f * kPi * tone_freq / static_cast<float>(sample_rate_);
        if (tone_phase >= 2.0f * kPi) {
            tone_phase -= 2.0f * kPi;
        }
        tone_left--;
    }

    tone_phase_ = tone_phase;
    tone_samples_left_ = tone_left;
    if (tone_left == 0) {
        tone_active_ = false;
    }
}
