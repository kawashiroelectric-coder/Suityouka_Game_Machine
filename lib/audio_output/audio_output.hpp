// ============================================
// ファイル: audio_output.hpp
// PCM5102 向け 32bit I2S 音声出力（PIO + DMA、Core 1 で駆動）
// Core0 が SD から PCM 二重バッファへ供給、Core1 がコールバック経由で I2S へ変換
// ============================================

#ifndef AUDIO_OUTPUT_HPP
#define AUDIO_OUTPUT_HPP

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "config.hpp"
#include <cstdint>

/** PCM5102 I2S（PIO + DMA）。再生処理は Core 1 で実行する */
class AudioOutput {
public:
    /** 各 DMA ブロック分の L/R サンプルを生成するコールバック（16bit PCM、Core1 から呼ばれる） */
    typedef void (*AudioCallback)(int16_t* left, int16_t* right, size_t samples);

    /** 1 DMA チャンクあたりのフレーム数（LuaAudio ストリームバッファと一致） */
    static constexpr size_t BUFFER_FRAMES = AudioConfig::STREAM_BUFFER_FRAMES;

    /** コンストラクタ。内部バッファとシングルトン参照を初期化する */
    AudioOutput();
    /** デストラクタ。再生を停止しシングルトン参照を解除する */
    ~AudioOutput();

    /** Core 1 を起動し I2S PIO / DMA を初期化する */
    bool init(uint32_t sample_rate = AudioConfig::SAMPLE_RATE);

    /** DMA 転送とコールバック駆動の再生を開始（Core 1 へ要求） */
    void start();
    /** DMA を停止し無音を出力 */
    void stop();

    /** 現在 I2S 再生中かどうかを返す */
    bool isPlaying() const { return playing_; }

    /** PCM 供給用コールバックを登録する（Core 1 から呼ばれる） */
    void setCallback(AudioCallback cb) { callback_ = cb; }
    /** 再生音量を 0.0〜1.0 で設定する */
    void setVolume(float volume);

    /** 指定周波数・時間のトーン再生（未実装） */
    void playTone(float frequency, float duration_ms);

    /** Core 1 エントリ（multicore_launch_core1 から呼ぶ） */
    static void core1Entry();

private:
    /** ステレオ 32bit ワード数（L,R 交互） */
    static constexpr size_t BUFFER_WORDS = BUFFER_FRAMES * 2;

    uint32_t sample_rate_;
    bool initialized_;
    volatile bool core1_ready_;
    volatile bool playing_;
    volatile bool start_requested_;
    volatile bool stop_requested_;

    PIO pio_;
    uint sm_;
    uint pio_offset_;
    int dma_channel_;

    int32_t buffer_a_[BUFFER_WORDS];
    int32_t buffer_b_[BUFFER_WORDS];
    uint8_t current_buffer_;

    AudioCallback callback_;
    float volume_;

    static AudioOutput* instance_;

    /** DMA 完了割り込みハンドラ（IRQ から呼ばれる） */
    static void dmaIrqHandler();
    /** DMA 完了時に次バッファを充填して再キックする */
    void onDmaComplete();
    /** コールバック結果を 32bit ステレオ DMA バッファへ変換する */
    void fillBuffer(int32_t* dst, int16_t* scratch_l, int16_t* scratch_r);
    /** 指定バッファから PIO TX FIFO へ DMA 転送を開始する */
    void kickDma(const int32_t* src);
    /** Core 1 上で PIO・DMA・GPIO を初期化する */
    void initHardwareOnCore1();
    /** Core 1 上で I2S 再生を開始する */
    void startPlaybackOnCore1();
    /** Core 1 上で I2S 再生を停止する */
    void stopPlaybackOnCore1();
    /** Core 1 のメインループ（再生制御・バッテリー監視） */
    void core1Loop();
};

#endif  // AUDIO_OUTPUT_HPP
