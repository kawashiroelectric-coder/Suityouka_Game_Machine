// ============================================
// ファイル: audio_output.hpp
// 音声出力管理クラス（PWM使用）
// ============================================

#ifndef AUDIO_OUTPUT_HPP
#define AUDIO_OUTPUT_HPP

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "config.hpp"
#include <cstdint>

// 音声出力管理クラス
class AudioOutput {
private:
    uint8_t pin_l;
    uint8_t pin_r;
    uint8_t pin_sd;
    uint8_t pin_abd;
    
    uint slice_l;
    uint slice_r;
    uint channel_l;
    uint channel_r;
    
    int dma_channel_l;
    int dma_channel_r;
    
    uint32_t sample_rate;
    bool initialized;
    bool playing;
    
    // DMAバッファ（ダブルバッファリング）
    static constexpr size_t BUFFER_SIZE = 512;
    int16_t buffer_l[2][BUFFER_SIZE];
    int16_t buffer_r[2][BUFFER_SIZE];
    uint8_t current_buffer;
    
    // コールバック関数型
    typedef void (*AudioCallback)(int16_t* left, int16_t* right, size_t samples);
    AudioCallback callback;
    
    // DMA割り込みハンドラ
    static void dma_handler();
    static AudioOutput* instance;
    
public:
    AudioOutput(uint8_t pin_l, uint8_t pin_r, uint8_t pin_sd, uint8_t pin_abd);
    ~AudioOutput();
    
    // 初期化
    bool init(uint32_t sample_rate = AudioConfig::SAMPLE_RATE);
    
    // 開始/停止
    void start();
    void stop();
    
    // 再生中かどうか
    bool isPlaying() const { return playing; }
    
    // コールバック関数を設定（音声データ生成用）
    void setCallback(AudioCallback cb) { callback = cb; }
    
    // 音量設定（0.0～1.0）
    void setVolume(float volume);
    
    // テストトーン生成
    void playTone(float frequency, float duration_ms);
    
private:
    // DMA転送完了時の処理
    void onDMATransferComplete();
    
    // PWM初期化
    void initPWM();
    
    // DMA初期化
    void initDMA();
};

#endif // AUDIO_OUTPUT_HPP
