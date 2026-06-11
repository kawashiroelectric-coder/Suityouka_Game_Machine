// ============================================
// ファイル: lua_audio.hpp
// Lua 向け BGM ストリーム / SE 8ch 加算 / トーン
// Core0: SD 読み込み / Core1: ミキシング → I2S
// ============================================

#ifndef LUA_AUDIO_HPP
#define LUA_AUDIO_HPP

#include <cstddef>
#include <cstdint>

#include "config.hpp"

class AudioOutput;

/** BGM ストリーム + SE 8 系統加算 + トーンを I2S へ供給する */
class LuaAudio {
public:
    static constexpr size_t kStreamFrames = AudioConfig::STREAM_BUFFER_FRAMES;
    static constexpr size_t kSeChannels = AudioConfig::SE_CHANNEL_COUNT;
    static constexpr size_t kMaxSeBytes = AudioConfig::SE_MAX_BYTES;

    LuaAudio();
    ~LuaAudio();

    void attach(AudioOutput* output, uint32_t sample_rate);
    void setSdMounted(bool mounted) { sd_mounted_ = mounted; }

    /** AudioOutput::setCallback に渡す静的コールバック（Core1 から呼ばれる） */
    static void audioCallback(int16_t* left, int16_t* right, size_t samples);

    bool playTone(float frequency_hz, float duration_ms);
    /** BGM: 16bit PCM WAV を SD からストリーミング（SE は止めない） */
    bool playWav(const char* path, char* errbuf, size_t errbuf_len);
    /** SE: 16bit PCM WAV を RAM 載せで加算再生（8 系統、超過時は最古を上書き） */
    bool playSe(const char* path, char* errbuf, size_t errbuf_len);
    void stop();
    void setVolume(float volume);

    /** Core0: BGM 用ストリームバッファへ SD から読み込む */
    void pumpStream();
    /** BGM ストリーミング中か */
    bool isBgmStreaming() const { return bgm_active_; }
    /** BGM / SE / トーンのいずれかが鳴っているか */
    bool isAudioActive() const;

private:
    enum StreamSlotState : uint8_t {
        kSlotEmpty = 0,
        kSlotReady = 1,
    };

    struct StreamSlot {
        int16_t left[kStreamFrames];
        int16_t right[kStreamFrames];
        size_t frames;
        volatile uint8_t state;
    };

    struct SeChannel {
        int16_t* data;
        size_t byte_size;
        size_t frame_count;
        uint16_t channels;
        uint32_t source_rate;
        volatile double position;
        volatile bool active;
        uint32_t load_serial;
    };

    void fill(int16_t* left, int16_t* right, size_t samples);
    void mixSeChannels(int16_t* left, int16_t* right, size_t samples);
    void stopBgmLocked();
    void stopSeLocked();
    void stopToneLocked();
    void closeBgmFileLocked();
    void resetStreamSlotsLocked();
    void freeSeChannelLocked(int index);
    bool openBgmForStream(const char* path, char* errbuf, size_t errbuf_len);
    int allocateSeSlotLocked();
    bool fillStreamSlot(StreamSlot& slot);
    bool readNextBgmFrame(int16_t* out_l, int16_t* out_r);
    void primeStreamBuffers();
    int countReadySlots() const;
    bool consumeStreamSlot(int16_t* left, int16_t* right, size_t frames);
    void ensureOutputPlaying();

    static LuaAudio* instance_;

    AudioOutput* output_;
    uint32_t sample_rate_;
    bool sd_mounted_;
    float volume_;

    volatile bool tone_active_;
    volatile float tone_freq_;
    volatile float tone_phase_;
    volatile uint32_t tone_samples_left_;

    StreamSlot stream_slots_[2];
    volatile bool bgm_active_;
    volatile bool bgm_eof_;

    bool bgm_file_open_;
    uint16_t bgm_channels_;
    uint32_t bgm_source_rate_;
    uint32_t bgm_data_remaining_;
    double bgm_src_pos_;
    size_t bgm_file_frame_next_;
    int16_t bgm_cur_l_;
    int16_t bgm_cur_r_;

    SeChannel se_channels_[kSeChannels];
    uint32_t se_load_counter_;
};

#endif  // LUA_AUDIO_HPP
