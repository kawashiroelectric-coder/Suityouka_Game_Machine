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

    AudioOutput();
    ~AudioOutput();

    /** Core 1 を起動し I2S PIO / DMA を初期化する */
    bool init(uint32_t sample_rate = AudioConfig::SAMPLE_RATE);

    /** DMA 転送とコールバック駆動の再生を開始（Core 1 へ要求） */
    void start();
    /** DMA を停止し無音を出力 */
    void stop();

    bool isPlaying() const { return playing_; }

    void setCallback(AudioCallback cb) { callback_ = cb; }
    void setVolume(float volume);

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

    static void dmaIrqHandler();
    void onDmaComplete();
    void fillBuffer(int32_t* dst, int16_t* scratch_l, int16_t* scratch_r);
    void kickDma(const int32_t* src);
    void initHardwareOnCore1();
    void startPlaybackOnCore1();
    void stopPlaybackOnCore1();
    void core1Loop();
};

#endif  // AUDIO_OUTPUT_HPP
