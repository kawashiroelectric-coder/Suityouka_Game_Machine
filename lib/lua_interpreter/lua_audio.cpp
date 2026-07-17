// ============================================
// ファイル: lua_audio.cpp
// Core0: BGM SD ストリーム + SE RAM 読み込み
// Core1: BGM + SE 8ch + トーンをミキシング → I2S
// ============================================

#include "lua_audio.hpp"

#include <cmath>
#include <cstddef>
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

/** サンプル値を int16_t の範囲にクランプする */
int32_t clampSample(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

/** 2 点間を線形補間して int16_t サンプルを得る（リサンプル用） */
int16_t lerpSample16(int16_t a, int16_t b, double frac) {
    if (frac <= 0.0) {
        return a;
    }
    if (frac >= 1.0) {
        return b;
    }
    const double v =
        static_cast<double>(a) + (static_cast<double>(b) - static_cast<double>(a)) * frac;
    return static_cast<int16_t>(clampSample(static_cast<int32_t>(std::lround(v))));
}

/** リトルエンディアン 32bit 整数をバイト列から読み取る */
uint32_t readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/** リトルエンディアン 16bit 整数をバイト列から読み取る */
uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

/** エラーメッセージバッファに文字列をコピーする */
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

/** コンストラクタ: シングルトン instance と各チャンネル状態を初期化する */
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
      bgm_embed_active_(false),
      bgm_embed_pcm_(nullptr),
      bgm_embed_frame_count_(0),
      bgm_embed_frames_remaining_(0),
      bgm_channels_(0),
      bgm_source_rate_(0),
      bgm_data_remaining_(0),
      bgm_src_pos_(0.0),
      bgm_file_frame_next_(0),
      bgm_cur_l_(0),
      bgm_cur_r_(0),
      bgm_next_l_(0),
      bgm_next_r_(0),
      bgm_resample_base_(0),
      bgm_resample_primed_(false),
      se_load_counter_(0) {
    instance_ = this;
    resetStreamSlotsLocked();
    for (size_t i = 0; i < kSeChannels; i++) {
        se_channels_[i] = SeChannel{};
    }
}

/** デストラクタ: 再生を停止しシングルトン参照を解除する */
LuaAudio::~LuaAudio() {
    stop();
    instance_ = nullptr;
}

/** AudioOutput とサンプルレートを接続する */
void LuaAudio::attach(AudioOutput* output, uint32_t sample_rate) {
    output_ = output;
    sample_rate_ = sample_rate;
}

/** I2S コールバック（Core1）: ミックス済み PCM を出力バッファへ書き込む */
void LuaAudio::audioCallback(int16_t* left, int16_t* right, size_t samples) {
    if (instance_) {
        instance_->fill(left, right, samples);
    } else {
        memset(left, 0, samples * sizeof(int16_t));
        memset(right, 0, samples * sizeof(int16_t));
    }
}

/** マスター音量を 0.0〜1.0 の範囲で設定する */
void LuaAudio::setVolume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    volume_ = volume;
    if (output_) {
        output_->setVolume(volume);
    }
}

/** BGM / SE / トーンのいずれかが再生中か返す */
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

/** 出力が停止中なら I2S 再生を開始する */
void LuaAudio::ensureOutputPlaying() {
    if (output_ && !output_->isPlaying()) {
        output_->start();
    }
}

/** BGM ストリーム用ダブルバッファスロットを空状態にリセットする */
void LuaAudio::resetStreamSlotsLocked() {
    for (StreamSlot& slot : stream_slots_) {
        memset(slot.left, 0, sizeof(slot.left));
        memset(slot.right, 0, sizeof(slot.right));
        slot.frames = 0;
        slot.state = kSlotEmpty;
    }
}

/** BGM ストリーミング状態を停止して内部変数を初期化する */
void LuaAudio::stopBgmLocked() {
    bgm_active_ = false;
    bgm_eof_ = false;
    bgm_src_pos_ = 0.0;
    bgm_file_frame_next_ = 0;
    bgm_cur_l_ = 0;
    bgm_cur_r_ = 0;
    bgm_next_l_ = 0;
    bgm_next_r_ = 0;
    bgm_resample_base_ = 0;
    bgm_resample_primed_ = false;
}

/** トーン生成状態を停止する */
void LuaAudio::stopToneLocked() {
    tone_active_ = false;
    tone_samples_left_ = 0;
}

/** 指定 SE チャンネルの PCM バッファを解放して無効化する */
void LuaAudio::freeSeChannelLocked(int index) {
    if (index < 0 || index >= static_cast<int>(kSeChannels)) {
        return;
    }
    SeChannel& ch = se_channels_[index];
    ch.active = false;
    if (ch.pcm_on_heap && ch.pcm) {
        HeapBudget::release(const_cast<int16_t*>(ch.pcm), ch.byte_size);
    }
    ch.pcm = nullptr;
    ch.pcm_on_heap = false;
    ch.byte_size = 0;
    ch.frame_count = 0;
    ch.channels = 0;
    ch.source_rate = 0;
    ch.position = 0.0;
    ch.load_serial = 0;
}

/** 全 SE チャンネルを停止・解放する */
void LuaAudio::stopSeLocked() {
    for (size_t i = 0; i < kSeChannels; i++) {
        freeSeChannelLocked(static_cast<int>(i));
    }
}

/** 開いている BGM 用 WAV ファイルを閉じ、埋め込み BGM 参照も解除する */
void LuaAudio::closeBgmFileLocked() {
    if (bgm_file_open_) {
        f_close(&s_bgm_file);
        bgm_file_open_ = false;
    }
    bgm_embed_active_ = false;
    bgm_embed_pcm_ = nullptr;
    bgm_embed_frame_count_ = 0;
    bgm_embed_frames_remaining_ = 0;
    bgm_channels_ = 0;
    bgm_source_rate_ = 0;
    bgm_data_remaining_ = 0;
}

/** BGM / SE / トーンをすべて停止し、ファイルとバッファを解放する */
void LuaAudio::stop() {
    stopBgm();
    uint32_t irq = save_and_disable_interrupts();
    stopSeLocked();
    stopToneLocked();
    restore_interrupts(irq);

    if (output_) {
        output_->stop();
    }
}

/** BGM のみ停止（I2S DMA は止めない） */
void LuaAudio::stopBgm() {
    uint32_t irq = save_and_disable_interrupts();
    stopBgmLocked();
    resetStreamSlotsLocked();
    restore_interrupts(irq);
    closeBgmFileLocked();
}

/** 指定周波数・時間のサイン波トーンを再生する */
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

/** SD 上の WAV を開き、BGM ストリーミング用に fmt/data を解析する */
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
    bgm_next_l_ = 0;
    bgm_next_r_ = 0;
    bgm_resample_base_ = 0;
    bgm_resample_primed_ = false;
    bgm_eof_ = false;
    return true;
}

/** BGM ファイルまたは埋め込み PCM から次の1フレーム（L/R）を読み込む */
bool LuaAudio::readNextBgmFrame(int16_t* out_l, int16_t* out_r) {
    if (bgm_embed_active_) {
        if (!bgm_embed_pcm_ || bgm_embed_frames_remaining_ == 0) {
            return false;
        }
        const size_t idx = bgm_embed_frame_count_ - bgm_embed_frames_remaining_;
        if (bgm_channels_ == 1) {
            const int16_t sample = bgm_embed_pcm_[idx];
            *out_l = sample;
            *out_r = sample;
        } else {
            *out_l = bgm_embed_pcm_[idx * 2];
            *out_r = bgm_embed_pcm_[idx * 2 + 1];
        }
        bgm_embed_frames_remaining_--;
        bgm_file_frame_next_++;
        bgm_cur_l_ = *out_l;
        bgm_cur_r_ = *out_r;
        return true;
    }

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

/** ファイル BGM のリサンプル用: target_idx の L/R と次フレームを 2 点分用意する */
bool LuaAudio::advanceFileResampleHold(size_t target_idx, int16_t* s0l, int16_t* s0r, int16_t* s1l,
                                        int16_t* s1r) {
    if (!bgm_file_open_) {
        return false;
    }

    if (!bgm_resample_primed_) {
        if (!readNextBgmFrame(&bgm_cur_l_, &bgm_cur_r_)) {
            return false;
        }
        bgm_resample_base_ = 0;
        bgm_resample_primed_ = true;
        if (bgm_data_remaining_ > 0) {
            if (!readNextBgmFrame(&bgm_next_l_, &bgm_next_r_)) {
                bgm_next_l_ = bgm_cur_l_;
                bgm_next_r_ = bgm_cur_r_;
            }
        } else {
            bgm_next_l_ = bgm_cur_l_;
            bgm_next_r_ = bgm_cur_r_;
        }
    }

    while (bgm_resample_base_ < target_idx) {
        bgm_cur_l_ = bgm_next_l_;
        bgm_cur_r_ = bgm_next_r_;
        bgm_resample_base_++;
        if (bgm_data_remaining_ > 0) {
            if (!readNextBgmFrame(&bgm_next_l_, &bgm_next_r_)) {
                bgm_next_l_ = bgm_cur_l_;
                bgm_next_r_ = bgm_cur_r_;
            }
        } else {
            bgm_next_l_ = bgm_cur_l_;
            bgm_next_r_ = bgm_cur_r_;
        }
    }

    if (bgm_resample_base_ != target_idx) {
        return false;
    }

    *s0l = bgm_cur_l_;
    *s0r = bgm_cur_r_;
    *s1l = bgm_next_l_;
    *s1r = bgm_next_r_;
    return true;
}

/** BGM データをリサンプルしながら1ストリームスロット分を埋める */
bool LuaAudio::fillStreamSlot(StreamSlot& slot) {
    if (!bgm_file_open_ && !bgm_embed_active_) {
        return false;
    }

    const double rate_step =
        (bgm_source_rate_ > 0 && sample_rate_ > 0)
            ? static_cast<double>(bgm_source_rate_) / static_cast<double>(sample_rate_)
            : 1.0;
    const bool resample = (rate_step != 1.0);

    size_t out = 0;

    while (out < kStreamFrames) {
        const double src_pos = bgm_src_pos_;

        if (bgm_embed_active_) {
            const size_t idx = static_cast<size_t>(src_pos);
            if (idx >= bgm_embed_frame_count_) {
                break;
            }
            const double frac = src_pos - static_cast<double>(idx);
            const size_t idx1 =
                (idx + 1 < bgm_embed_frame_count_) ? idx + 1 : idx;
            if (bgm_channels_ == 1) {
                const int16_t s =
                    lerpSample16(bgm_embed_pcm_[idx], bgm_embed_pcm_[idx1], frac);
                slot.left[out] = s;
                slot.right[out] = s;
            } else {
                slot.left[out] = lerpSample16(bgm_embed_pcm_[idx * 2], bgm_embed_pcm_[idx1 * 2],
                                             frac);
                slot.right[out] =
                    lerpSample16(bgm_embed_pcm_[idx * 2 + 1], bgm_embed_pcm_[idx1 * 2 + 1], frac);
            }
        } else if (resample) {
            const size_t src_idx = static_cast<size_t>(src_pos);
            const double frac = src_pos - static_cast<double>(src_idx);
            int16_t s0l = 0;
            int16_t s0r = 0;
            int16_t s1l = 0;
            int16_t s1r = 0;
            if (!advanceFileResampleHold(src_idx, &s0l, &s0r, &s1l, &s1r)) {
                break;
            }
            slot.left[out] = lerpSample16(s0l, s1l, frac);
            slot.right[out] = lerpSample16(s0r, s1r, frac);
        } else {
            const size_t src_idx = static_cast<size_t>(src_pos);
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
        }

        out++;
        bgm_src_pos_ += rate_step;
    }

    if (out == 0) {
        if (bgm_embed_active_) {
            if (bgm_src_pos_ >= static_cast<double>(bgm_embed_frame_count_)) {
                bgm_embed_frames_remaining_ = 0;
                bgm_eof_ = true;
            }
        } else if (bgm_data_remaining_ == 0) {
            bgm_eof_ = true;
        }
        return false;
    }

    if (out < kStreamFrames) {
        memset(slot.left + out, 0, (kStreamFrames - out) * sizeof(int16_t));
        memset(slot.right + out, 0, (kStreamFrames - out) * sizeof(int16_t));
    }

    if (bgm_embed_active_) {
        if (bgm_src_pos_ >= static_cast<double>(bgm_embed_frame_count_)) {
            bgm_embed_frames_remaining_ = 0;
            bgm_eof_ = true;
        } else {
            const size_t remaining =
                bgm_embed_frame_count_ - static_cast<size_t>(bgm_src_pos_);
            bgm_embed_frames_remaining_ = remaining;
        }
    } else if (bgm_data_remaining_ == 0 &&
               bgm_src_pos_ >= static_cast<double>(bgm_file_frame_next_)) {
        bgm_eof_ = true;
    }

    slot.frames = kStreamFrames;
    return true;
}

/** 埋め込み PCM パラメータの妥当性を検証する */
bool LuaAudio::validateEmbeddedPcm(const int16_t* pcm, size_t frame_count, uint16_t channels,
                                   uint32_t sample_rate, size_t max_bytes) {
    if (!pcm || frame_count == 0 || sample_rate == 0) {
        return false;
    }
    if (channels != 1 && channels != 2) {
        return false;
    }
    const size_t byte_size = frame_count * static_cast<size_t>(channels) * sizeof(int16_t);
    return byte_size > 0 && byte_size <= max_bytes;
}

/** SE チャンネルに PCM を割り当てて再生開始する */
void LuaAudio::activateSeChannelLocked(int slot, const int16_t* pcm, bool pcm_on_heap,
                                       size_t byte_size, size_t frame_count, uint16_t channels,
                                       uint32_t sample_rate) {
    SeChannel& ch = se_channels_[slot];
    // 先に無効化してから差し替え。Core1 が古い position を書き戻さないよう serial を先に進める
    ch.active = false;
    ch.load_serial = ++se_load_counter_;
    __dmb();
    if (ch.pcm_on_heap && ch.pcm) {
        HeapBudget::release(const_cast<int16_t*>(ch.pcm), ch.byte_size);
    }
    ch.pcm = pcm;
    ch.pcm_on_heap = pcm_on_heap;
    ch.byte_size = byte_size;
    ch.frame_count = frame_count;
    ch.channels = channels;
    ch.source_rate = sample_rate;
    ch.position = 0.0;
    __dmb();
    ch.active = true;
}

/** READY 状態のストリームスロット数を返す */
int LuaAudio::countReadySlots() const {
    int count = 0;
    for (const StreamSlot& slot : stream_slots_) {
        if (slot.state == kSlotReady) {
            count++;
        }
    }
    return count;
}

/** Core0: 空きストリームスロットへ SD から BGM データを読み込む */
void LuaAudio::pumpStream() {
    if (!bgm_active_) {
        if (bgm_file_open_) {
            closeBgmFileLocked();
        }
        return;
    }
    if (!bgm_file_open_ && !bgm_embed_active_) {
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

/** BGM 再生開始前にストリームバッファを先読みしてプリロードする */
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

/** SD 上の WAV を BGM としてストリーミング再生開始する */
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

/** 空き SE スロットを探す。満杯なら最古のスロットを再利用する */
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

/** 再生中のうち最も古い SE スロットを返す */
int LuaAudio::findOldestActiveSeSlotLocked() const {
    int oldest = -1;
    uint32_t min_serial = 0;
    for (size_t i = 0; i < kSeChannels; i++) {
        if (!se_channels_[i].active) {
            continue;
        }
        if (oldest < 0 || se_channels_[i].load_serial < min_serial) {
            min_serial = se_channels_[i].load_serial;
            oldest = static_cast<int>(i);
        }
    }
    return oldest;
}

/** 新規 SE の RAM 確保前に、必要なら最古の SE を捨ててヒープを空ける */
void LuaAudio::evictOldestSeForHeap(size_t need_bytes) {
    if (need_bytes == 0) {
        return;
    }
    // 最悪でもチャンネル数回だけ解放（無限ループ防止）
    for (size_t n = 0; n < kSeChannels; n++) {
        if (HeapBudget::available() >= need_bytes) {
            return;
        }
        uint32_t irq = save_and_disable_interrupts();
        const int oldest = findOldestActiveSeSlotLocked();
        if (oldest < 0) {
            restore_interrupts(irq);
            return;
        }
        freeSeChannelLocked(oldest);
        restore_interrupts(irq);
    }
}

/** SD 上の WAV を RAM に読み込み、SE チャンネルで加算再生する */
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

        // 8ch 満杯でヒープが足りないときは最古 SE を止めてから確保する
        evictOldestSeForHeap(data_size);

        void* alloc_ptr = nullptr;
        if (!HeapBudget::tryAlloc(data_size, &alloc_ptr)) {
            // 念のためもう一段: 最古を1つ捨てて再試行
            evictOldestSeForHeap(data_size);
            if (!HeapBudget::tryAlloc(data_size, &alloc_ptr)) {
                f_close(&file);
                setError(errbuf, errbuf_len, "heap budget exceeded");
                return false;
            }
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
    activateSeChannelLocked(slot, new_pcm, true, new_pcm_bytes, new_frames, new_ch, new_rate);
    restore_interrupts(irq);

    ensureOutputPlaying();
    return true;
}

/** flash 埋め込み PCM を SE として加算再生する */
bool LuaAudio::playSeFromEmbedded(const int16_t* pcm, size_t frame_count, uint16_t channels,
                                  uint32_t sample_rate) {
    if (!output_) {
        return false;
    }
    if (!validateEmbeddedPcm(pcm, frame_count, channels, sample_rate, kMaxSeBytes)) {
        return false;
    }

    const size_t byte_size = frame_count * static_cast<size_t>(channels) * sizeof(int16_t);
    uint32_t irq = save_and_disable_interrupts();

    // 同一埋め込み PCM が再生中ならスロットを再利用して頭から再トリガーする
    // （長いカーソル SE を高速連打すると 8ch が埋まり、上書き時の dual-core 競合で無音化するため）
    for (size_t i = 0; i < kSeChannels; i++) {
        SeChannel& ch = se_channels_[i];
        if (ch.active && ch.pcm == pcm) {
            ch.load_serial = ++se_load_counter_;
            __dmb();
            ch.position = 0.0;
            restore_interrupts(irq);
            ensureOutputPlaying();
            return true;
        }
    }

    const int slot = allocateSeSlotLocked();
    activateSeChannelLocked(slot, pcm, false, byte_size, frame_count, channels, sample_rate);
    restore_interrupts(irq);

    ensureOutputPlaying();
    return true;
}

/** flash 埋め込み PCM を BGM としてストリーム再生する */
bool LuaAudio::playBgmFromEmbedded(const int16_t* pcm, size_t frame_count, uint16_t channels,
                                   uint32_t sample_rate) {
    if (!output_) {
        return false;
    }
    if (!validateEmbeddedPcm(pcm, frame_count, channels, sample_rate, SIZE_MAX)) {
        return false;
    }

    uint32_t irq = save_and_disable_interrupts();
    stopBgmLocked();
    resetStreamSlotsLocked();
    closeBgmFileLocked();

    bgm_embed_active_ = true;
    bgm_embed_pcm_ = pcm;
    bgm_embed_frame_count_ = frame_count;
    bgm_embed_frames_remaining_ = frame_count;
    bgm_channels_ = channels;
    bgm_source_rate_ = sample_rate;
    bgm_src_pos_ = 0.0;
    bgm_file_frame_next_ = 0;
    bgm_cur_l_ = 0;
    bgm_cur_r_ = 0;
    bgm_next_l_ = 0;
    bgm_next_r_ = 0;
    bgm_resample_base_ = 0;
    bgm_resample_primed_ = false;
    bgm_eof_ = false;
    bgm_active_ = true;
    restore_interrupts(irq);

    primeStreamBuffers();

    if (countReadySlots() == 0) {
        uint32_t irq2 = save_and_disable_interrupts();
        stopBgmLocked();
        resetStreamSlotsLocked();
        closeBgmFileLocked();
        restore_interrupts(irq2);
        return false;
    }

    ensureOutputPlaying();
    return true;
}

/** READY なストリームスロットを1つ取り出し、出力バッファへコピーする */
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

/** 全アクティブ SE チャンネルを出力バッファへ加算ミックスする */
void LuaAudio::mixSeChannels(int16_t* left, int16_t* right, size_t samples) {
    for (size_t c = 0; c < kSeChannels; c++) {
        SeChannel& ch = se_channels_[c];
        // Core0 の再トリガー／差し替えと並行し得るので世代番号で書き戻しを保護する
        const uint32_t serial = ch.load_serial;
        __dmb();
        if (!ch.active || !ch.pcm) {
            continue;
        }

        const int16_t* pcm = ch.pcm;
        const size_t frame_count = ch.frame_count;
        const uint16_t channels = ch.channels;
        const uint32_t source_rate = ch.source_rate;
        const double rate_step =
            (source_rate > 0 && sample_rate_ > 0)
                ? static_cast<double>(source_rate) / static_cast<double>(sample_rate_)
                : 1.0;

        double pos = ch.position;
        bool still_active = true;

        for (size_t i = 0; i < samples; i++) {
            const size_t idx = static_cast<size_t>(pos);
            if (idx >= frame_count) {
                still_active = false;
                break;
            }

            const double frac = pos - static_cast<double>(idx);
            const size_t idx1 = (idx + 1 < frame_count) ? idx + 1 : idx;

            int32_t sl;
            int32_t sr;
            if (channels == 1) {
                sl = sr = lerpSample16(pcm[idx], pcm[idx1], frac);
            } else {
                sl = lerpSample16(pcm[idx * 2], pcm[idx1 * 2], frac);
                sr = lerpSample16(pcm[idx * 2 + 1], pcm[idx1 * 2 + 1], frac);
            }

            left[i] = static_cast<int16_t>(clampSample(static_cast<int32_t>(left[i]) + sl));
            right[i] = static_cast<int16_t>(clampSample(static_cast<int32_t>(right[i]) + sr));
            pos += rate_step;
        }

        __dmb();
        // Core0 が途中で load_serial を進めていたら、このバッファの結果は捨てる
        if (ch.load_serial != serial) {
            continue;
        }
        ch.position = pos;
        if (!still_active || static_cast<size_t>(pos) >= frame_count) {
            ch.active = false;
        }
    }
}

/** I2S 1 バッファ分: BGM + SE + トーンをミックスして出力する */
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
