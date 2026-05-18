// ============================================
// ファイル: audio_output.cpp
// 音声出力管理クラスの実装
// ============================================

#include "audio_output.hpp"
#include <cstdio>
#include <cmath>
#include "hardware/irq.h"

AudioOutput* AudioOutput::instance = nullptr;

AudioOutput::AudioOutput(uint8_t pin_l, uint8_t pin_r, uint8_t pin_sd, uint8_t pin_abd)
    : pin_l(pin_l), pin_r(pin_r), pin_sd(pin_sd), pin_abd(pin_abd),
      sample_rate(AudioConfig::SAMPLE_RATE), initialized(false), playing(false),
      current_buffer(0), callback(nullptr) {
    instance = this;
}

AudioOutput::~AudioOutput() {
    stop();
    if (initialized) {
        pwm_set_enabled(slice_l, false);
        pwm_set_enabled(slice_r, false);
    }
}

bool AudioOutput::init(uint32_t sample_rate) {
    this->sample_rate = sample_rate;
    
    // GPIO初期化
    gpio_init(pin_sd);
    gpio_set_dir(pin_sd, GPIO_OUT);
    gpio_put(pin_sd, 1);  // シャットダウン解除
    
    gpio_init(pin_abd);
    gpio_set_dir(pin_abd, GPIO_OUT);
    gpio_put(pin_abd, 0);
    
    // PWM初期化
    initPWM();
    
    // DMA初期化
    initDMA();
    
    // バッファをゼロクリア
    for (size_t i = 0; i < BUFFER_SIZE; i++) {
        buffer_l[0][i] = 0;
        buffer_l[1][i] = 0;
        buffer_r[0][i] = 0;
        buffer_r[1][i] = 0;
    }
    
    initialized = true;
    printf("AudioOutput: 初期化完了 (サンプルレート: %lu Hz)\n", sample_rate);
    return true;
}

void AudioOutput::initPWM() {
    // 左チャンネル
    gpio_set_function(pin_l, GPIO_FUNC_PWM);
    slice_l = pwm_gpio_to_slice_num(pin_l);
    channel_l = pwm_gpio_to_channel(pin_l);
    
    pwm_config cfg_l = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg_l, 125.0f / (sample_rate / 1000.0f));  // 125MHz / (sample_rate / 1000)
    pwm_config_set_wrap(&cfg_l, AudioConfig::PWM_WRAP);
    pwm_config_set_phase_correct(&cfg_l, false);
    pwm_init(slice_l, &cfg_l, true);
    pwm_set_chan_level(slice_l, channel_l, 0);
    
    // 右チャンネル
    gpio_set_function(pin_r, GPIO_FUNC_PWM);
    slice_r = pwm_gpio_to_slice_num(pin_r);
    channel_r = pwm_gpio_to_channel(pin_r);
    
    pwm_config cfg_r = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg_r, 125.0f / (sample_rate / 1000.0f));
    pwm_config_set_wrap(&cfg_r, AudioConfig::PWM_WRAP);
    pwm_config_set_phase_correct(&cfg_r, false);
    pwm_init(slice_r, &cfg_r, true);
    pwm_set_chan_level(slice_r, channel_r, 0);
}

void AudioOutput::initDMA() {
    // 左チャンネル用DMA
    dma_channel_l = dma_claim_unused_channel(true);
    dma_channel_config cfg_l = dma_channel_get_default_config(dma_channel_l);
    channel_config_set_transfer_data_size(&cfg_l, DMA_SIZE_16);
    channel_config_set_dreq(&cfg_l, pwm_get_dreq(slice_l));
    channel_config_set_read_increment(&cfg_l, true);
    channel_config_set_write_increment(&cfg_l, false);
    
    // 右チャンネル用DMA
    dma_channel_r = dma_claim_unused_channel(true);
    dma_channel_config cfg_r = dma_channel_get_default_config(dma_channel_r);
    channel_config_set_transfer_data_size(&cfg_r, DMA_SIZE_16);
    channel_config_set_dreq(&cfg_r, pwm_get_dreq(slice_r));
    channel_config_set_read_increment(&cfg_r, true);
    channel_config_set_write_increment(&cfg_r, false);
    
    // DMA割り込み設定
    dma_channel_set_irq0_enabled(dma_channel_l, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

void AudioOutput::dma_handler() {
    if (instance) {
        if (dma_irqn_get_channel_status(0, instance->dma_channel_l)) {
            dma_irqn_acknowledge_channel(0, instance->dma_channel_l);
            instance->onDMATransferComplete();
        }
    }
}

void AudioOutput::onDMATransferComplete() {
    if (!playing || !callback) return;
    
    // 次のバッファを生成
    uint8_t next_buffer = 1 - current_buffer;
    
    // コールバックで音声データを生成
    callback(buffer_l[next_buffer], buffer_r[next_buffer], BUFFER_SIZE);
    
    // 次のDMA転送を開始
    dma_channel_config cfg_l = dma_channel_get_default_config(dma_channel_l);
    channel_config_set_transfer_data_size(&cfg_l, DMA_SIZE_16);
    channel_config_set_dreq(&cfg_l, pwm_get_dreq(slice_l));
    channel_config_set_read_increment(&cfg_l, true);
    channel_config_set_write_increment(&cfg_l, false);
    
    // PWM CCレジスタへのポインタ（チャンネルA/Bで異なる）
    volatile uint32_t* pwm_cc_l;
    if (channel_l == PWM_CHAN_A) {
        pwm_cc_l = &pwm_hw->slice[slice_l].cc;
    } else {
        pwm_cc_l = (volatile uint32_t*)((uintptr_t)&pwm_hw->slice[slice_l].cc + sizeof(uint32_t));
    }
    
    volatile uint32_t* pwm_cc_r;
    if (channel_r == PWM_CHAN_A) {
        pwm_cc_r = &pwm_hw->slice[slice_r].cc;
    } else {
        pwm_cc_r = (volatile uint32_t*)((uintptr_t)&pwm_hw->slice[slice_r].cc + sizeof(uint32_t));
    }
    
    dma_channel_configure(
        dma_channel_l,
        &cfg_l,
        pwm_cc_l,  // PWM比較レジスタ
        buffer_l[next_buffer],
        BUFFER_SIZE,
        true
    );
    
    dma_channel_config cfg_r = dma_channel_get_default_config(dma_channel_r);
    channel_config_set_transfer_data_size(&cfg_r, DMA_SIZE_16);
    channel_config_set_dreq(&cfg_r, pwm_get_dreq(slice_r));
    channel_config_set_read_increment(&cfg_r, true);
    channel_config_set_write_increment(&cfg_r, false);
    
    dma_channel_configure(
        dma_channel_r,
        &cfg_r,
        pwm_cc_r,
        buffer_r[next_buffer],
        BUFFER_SIZE,
        true
    );
    
    current_buffer = next_buffer;
}

void AudioOutput::start() {
    if (!initialized || playing) return;
    
    playing = true;
    
    // 最初のバッファを生成
    if (callback) {
        callback(buffer_l[0], buffer_r[0], BUFFER_SIZE);
        callback(buffer_l[1], buffer_r[1], BUFFER_SIZE);
    }
    
    // DMA転送開始
    dma_channel_config cfg_l = dma_channel_get_default_config(dma_channel_l);
    channel_config_set_transfer_data_size(&cfg_l, DMA_SIZE_16);
    channel_config_set_dreq(&cfg_l, pwm_get_dreq(slice_l));
    channel_config_set_read_increment(&cfg_l, true);
    channel_config_set_write_increment(&cfg_l, false);
    
    // PWM CCレジスタへのポインタ（チャンネルA/Bで異なる）
    volatile uint32_t* pwm_cc_l;
    if (channel_l == PWM_CHAN_A) {
        pwm_cc_l = &pwm_hw->slice[slice_l].cc;
    } else {
        pwm_cc_l = (volatile uint32_t*)((uintptr_t)&pwm_hw->slice[slice_l].cc + sizeof(uint32_t));
    }
    
    volatile uint32_t* pwm_cc_r;
    if (channel_r == PWM_CHAN_A) {
        pwm_cc_r = &pwm_hw->slice[slice_r].cc;
    } else {
        pwm_cc_r = (volatile uint32_t*)((uintptr_t)&pwm_hw->slice[slice_r].cc + sizeof(uint32_t));
    }
    
    dma_channel_configure(
        dma_channel_l,
        &cfg_l,
        pwm_cc_l,
        buffer_l[0],
        BUFFER_SIZE,
        true
    );
    
    dma_channel_config cfg_r = dma_channel_get_default_config(dma_channel_r);
    channel_config_set_transfer_data_size(&cfg_r, DMA_SIZE_16);
    channel_config_set_dreq(&cfg_r, pwm_get_dreq(slice_r));
    channel_config_set_read_increment(&cfg_r, true);
    channel_config_set_write_increment(&cfg_r, false);
    
    dma_channel_configure(
        dma_channel_r,
        &cfg_r,
        pwm_cc_r,
        buffer_r[0],
        BUFFER_SIZE,
        true
    );
    
    current_buffer = 0;
    printf("AudioOutput: 再生開始\n");
}

void AudioOutput::stop() {
    if (!playing) return;
    
    playing = false;
    dma_channel_abort(dma_channel_l);
    dma_channel_abort(dma_channel_r);
    
    // 無音を出力
    pwm_set_chan_level(slice_l, channel_l, 0);
    pwm_set_chan_level(slice_r, channel_r, 0);
    
    printf("AudioOutput: 再生停止\n");
}

void AudioOutput::setVolume(float volume) {
    // 音量制御はコールバック内で実装するか、PWMのレベルを調整
    // 簡易実装として、ここでは何もしない（コールバック側で処理）
    (void)volume;
}

void AudioOutput::playTone(float frequency, float duration_ms) {
    // テスト用のトーン生成（簡易実装）
    // 注意: この実装は簡易版です。実際の使用では、より高度な音声再生システムを実装してください。
    (void)frequency;
    (void)duration_ms;
    // TODO: トーン生成機能を実装
}
