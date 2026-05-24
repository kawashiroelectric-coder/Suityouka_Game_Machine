// ============================================
// ファイル: audio_output.hpp
// 音声出力管理クラス（PWM + DMA）
// ============================================

#ifndef AUDIO_OUTPUT_HPP
#define AUDIO_OUTPUT_HPP

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "config.hpp"
#include <cstdint>

/** PWM + DMA によるステレオ音声出力 */
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
    
    static constexpr size_t BUFFER_SIZE = 512;
    int16_t buffer_l[2][BUFFER_SIZE];
    int16_t buffer_r[2][BUFFER_SIZE];
    uint8_t current_buffer;
    
    /** 各 DMA ブロック分の L/R サンプルを生成するコールバック */
    typedef void (*AudioCallback)(int16_t* left, int16_t* right, size_t samples);
    AudioCallback callback;
    
    static void dma_handler();
    static AudioOutput* instance;
    
public:
    /** @param pin_l 左チャンネル PWM ピン @param pin_r 右チャンネル PWM ピン
     *  @param pin_sd アンプ SD（シャットダウン） @param pin_abd アンプ ABD */
    AudioOutput(uint8_t pin_l, uint8_t pin_r, uint8_t pin_sd, uint8_t pin_abd);
    ~AudioOutput();
    
    /** GPIO / PWM / DMA を初期化する */
    bool init(uint32_t sample_rate = AudioConfig::SAMPLE_RATE);
    
    /** DMA 転送とコールバック駆動の再生を開始する */
    void start();
    /** DMA を停止し無音を出力する */
    void stop();
    
    /** 再生中かどうか */
    bool isPlaying() const { return playing; }
    
    /** サンプル生成コールバックを登録する（start 前に設定） */
    void setCallback(AudioCallback cb) { callback = cb; }
    
    /** 音量 0.0～1.0（現状はコールバック側で処理想定） */
    void setVolume(float volume);
    
    /** テスト用トーン再生（未実装） */
    void playTone(float frequency, float duration_ms);
    
private:
    /** DMA 完了時に次バッファを生成して再転送する */
    void onDMATransferComplete();
    
    /** 左右 PWM スライスを設定する */
    void initPWM();
    
    /** 左右 DMA チャンネルと IRQ を設定する */
    void initDMA();
};

#endif // AUDIO_OUTPUT_HPP
