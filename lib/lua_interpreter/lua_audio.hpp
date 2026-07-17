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

    /** コンストラクタ */
    LuaAudio();
    /** デストラクタ */
    ~LuaAudio();

    /** AudioOutput とサンプルレートを接続する */
    void attach(AudioOutput* output, uint32_t sample_rate);
    /** SD マウント状態を設定する */
    void setSdMounted(bool mounted) { sd_mounted_ = mounted; }

    /** AudioOutput::setCallback に渡す静的コールバック（Core1 から呼ばれる） */
    static void audioCallback(int16_t* left, int16_t* right, size_t samples);

    /** トーン（サイン波）を指定周波数・時間再生する */
    bool playTone(float frequency_hz, float duration_ms);
    /** BGM: 16bit PCM WAV を SD からストリーミング（SE は止めない） */
    bool playWav(const char* path, char* errbuf, size_t errbuf_len);
    /** SE: 16bit PCM WAV を RAM 載せで加算再生（8 系統、超過時は最古を上書き） */
    bool playSe(const char* path, char* errbuf, size_t errbuf_len);
    /**
     * SE: flash 埋め込み PCM を加算再生（ヒープコピーなし）。
     * pcm は再生完了まで有効な const 配列を指すこと。最大 kMaxSeBytes。
     */
    bool playSeFromEmbedded(const int16_t* pcm, size_t frame_count, uint16_t channels,
                            uint32_t sample_rate);
    /**
     * BGM: flash 埋め込み PCM をストリーム再生（SE は止めない）。
     * 長い曲向け。サイズ上限は flash 容量のみ。
     */
    bool playBgmFromEmbedded(const int16_t* pcm, size_t frame_count, uint16_t channels,
                             uint32_t sample_rate);
    /** BGM / SE / トーンをすべて停止する */
    void stop();
    /** BGM のみ停止（I2S 出力は維持。メニュー SE 用） */
    void stopBgm();
    /** マスター音量（0.0〜1.0）を設定する */
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
        const int16_t* pcm;
        bool pcm_on_heap;
        size_t byte_size;
        size_t frame_count;
        uint16_t channels;
        uint32_t source_rate;
        volatile double position;
        volatile bool active;
        /** Core0 再割当／再トリガー時に進め、Core1 の古い position 書き戻しを無効化する */
        volatile uint32_t load_serial;
    };

    /** I2S 1 バッファ分: BGM + SE + トーンをミックスして出力する */
    void fill(int16_t* left, int16_t* right, size_t samples);
    /** 全アクティブ SE チャンネルを出力バッファへ加算する */
    void mixSeChannels(int16_t* left, int16_t* right, size_t samples);
    /** BGM ストリーミング状態を停止する */
    void stopBgmLocked();
    /** 全 SE チャンネルを停止する */
    void stopSeLocked();
    /** トーン生成を停止する */
    void stopToneLocked();
    /** BGM 用 WAV ファイルを閉じる */
    void closeBgmFileLocked();
    /** ストリーム用ダブルバッファスロットをリセットする */
    void resetStreamSlotsLocked();
    /** 指定 SE チャンネルの PCM を解放する */
    void freeSeChannelLocked(int index);
    /** SD 上の WAV を BGM ストリーミング用に開く */
    bool openBgmForStream(const char* path, char* errbuf, size_t errbuf_len);
    /** 空きまたは最古の SE スロット index を返す */
    int allocateSeSlotLocked();
    /** 再生中のうち load_serial が最小のスロットを返す。無ければ -1 */
    int findOldestActiveSeSlotLocked() const;
    /** 新規 SE 用に need_bytes 分のヒープが空くまで最古 SE を停止・解放する */
    void evictOldestSeForHeap(size_t need_bytes);
    /** SE チャンネルに PCM を割り当てて再生開始する */
    void activateSeChannelLocked(int slot, const int16_t* pcm, bool pcm_on_heap, size_t byte_size,
                                 size_t frame_count, uint16_t channels, uint32_t sample_rate);
    /** 埋め込み PCM パラメータの妥当性を検証する */
    static bool validateEmbeddedPcm(const int16_t* pcm, size_t frame_count, uint16_t channels,
                                  uint32_t sample_rate, size_t max_bytes);
    /** 1 ストリームスロット分の BGM PCM を SD から読み込む */
    bool fillStreamSlot(StreamSlot& slot);
    /** BGM ファイルから次の1オーディオフレームを読み込む */
    bool readNextBgmFrame(int16_t* out_l, int16_t* out_r);
    /** ファイル BGM のリサンプル用: target_idx 位置の L/R と次フレームを用意する */
    bool advanceFileResampleHold(size_t target_idx, int16_t* s0l, int16_t* s0r, int16_t* s1l,
                                 int16_t* s1r);
    /** 再生開始前にストリームバッファをプリロードする */
    void primeStreamBuffers();
    /** READY 状態のストリームスロット数を返す */
    int countReadySlots() const;
    /** READY スロットを1つ取り出して出力バッファへコピーする */
    bool consumeStreamSlot(int16_t* left, int16_t* right, size_t frames);
    /** 出力が停止中なら I2S 再生を開始する */
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
    bool bgm_embed_active_;
    const int16_t* bgm_embed_pcm_;
    size_t bgm_embed_frame_count_;
    size_t bgm_embed_frames_remaining_;
    uint16_t bgm_channels_;
    uint32_t bgm_source_rate_;
    uint32_t bgm_data_remaining_;
    double bgm_src_pos_;
    size_t bgm_file_frame_next_;
    int16_t bgm_cur_l_;
    int16_t bgm_cur_r_;
    int16_t bgm_next_l_;
    int16_t bgm_next_r_;
    size_t bgm_resample_base_;
    bool bgm_resample_primed_;

    SeChannel se_channels_[kSeChannels];
    uint32_t se_load_counter_;
};

#endif  // LUA_AUDIO_HPP
