// ============================================
// ファイル: audio_output.cpp
// PCM5102 32bit I2S（pcm5102_i2s.pio）+ DMA、Core 1 駆動
// ============================================

#include "audio_output.hpp"
#include "battery_monitor.hpp"
#include "pcm5102_i2s.hpp"
#include "pico/flash.h"

#include <cstdio>
#include <cmath>
#include <cstring>

#include "hardware/clocks.h"
#include "pico/multicore.h"

AudioOutput* AudioOutput::instance_ = nullptr;

// コンストラクタ。バッファをゼロ初期化し、シングルトン instance_ を自身に設定する
AudioOutput::AudioOutput()
    : sample_rate_(AudioConfig::SAMPLE_RATE),
      initialized_(false),
      core1_ready_(false),
      playing_(false),
      start_requested_(false),
      stop_requested_(false),
      dma_in_flight_(false),
      pio_(pio1),
      sm_(0),
      pio_offset_(0),
      dma_channel_(-1),
      current_buffer_(0),
      callback_(nullptr),
      volume_(1.0f) {
    instance_ = this;
    memset(buffer_a_, 0, sizeof(buffer_a_));
    memset(buffer_b_, 0, sizeof(buffer_b_));
}

// デストラクタ。再生を停止し、シングルトン参照を解除する
AudioOutput::~AudioOutput() {
    stop();
    instance_ = nullptr;
}

// Core 1 のエントリポイント。multicore_launch_core1 から呼ばれ core1Loop へ委譲する
void AudioOutput::core1Entry() {
    if (instance_) {
        instance_->core1Loop();
    }
}

// Core 1 を起動し I2S ハードウェアの初期化完了を待つ。成功時 true
bool AudioOutput::init(uint32_t sample_rate) {
    if (initialized_) {
        return true;
    }
    sample_rate_ = sample_rate;
    core1_ready_ = false;
    multicore_launch_core1(core1Entry);

    const uint32_t deadline = to_ms_since_boot(get_absolute_time()) + 3000;
    while (!core1_ready_) {
        if (to_ms_since_boot(get_absolute_time()) > deadline) {
            printf("AudioOutput: Core1 init timeout\n");
            return false;
        }
        tight_loop_contents();
    }
    initialized_ = true;
    printf("AudioOutput: PCM5102 I2S init OK (%lu Hz, Core1)\n",
           (unsigned long)sample_rate_);
    return true;
}

// Core 1 上で PIO・DMA・XSMT GPIO を初期化し、I2S 出力を有効化する
void AudioOutput::initHardwareOnCore1() {
    using namespace AudioConfig::I2S;

    auto setXsmtLevel = [](int8_t pin, bool level) {
        if (pin < 0) {
            return;
        }
        gpio_put(static_cast<uint>(pin), level ? 1 : 0);
    };
    auto initMutePin = [&](int8_t pin, bool level) {
        if (pin < 0) {
            return;
        }
        gpio_init(static_cast<uint>(pin));
        gpio_set_dir(static_cast<uint>(pin), GPIO_OUT);
        setXsmtLevel(pin, level);
    };
    // 起動直後はミュート。I2S クロック開始後に startPlaybackOnCore1 で解除する
    initMutePin(PIN_XSMT, !XSMT_UNMUTE_LEVEL);
    initMutePin(PIN_SPMUTE, !XSMT_UNMUTE_LEVEL);

    pio_offset_ = pio_add_program(pio_, &pcm5102_i2s_program);
    pcm5102_i2s_init_sm(pio_, sm_, pio_offset_, sample_rate_);

    dma_channel_ = dma_claim_unused_channel(true);

    pcm5102_i2s_start(pio_, sm_);
    core1_ready_ = true;

    const uint32_t sys_hz = clock_get_hz(clk_sys);
    const float div = static_cast<float>(sys_hz) /
                      (static_cast<float>(sample_rate_) * 128.0f);
    const uint32_t lrck_hz = sample_rate_;
    const uint32_t bck_hz = sample_rate_ * 64u;
    printf("AudioOutput: sys=%lu Hz pio_clkdiv=%.2f dma_ch=%d\n",
           static_cast<unsigned long>(sys_hz), static_cast<double>(div), dma_channel_);
    printf("AudioOutput: expect LRCK(GP%u)=%lu Hz  BCK(GP%u)=%lu Hz (64fs)  DIN=GP%u  XSMT(GP%d)=mute until play\n",
           static_cast<unsigned>(PIN_LRCK), static_cast<unsigned long>(lrck_hz),
           static_cast<unsigned>(PIN_BCK), static_cast<unsigned long>(bck_hz),
           static_cast<unsigned>(PIN_DATA), static_cast<int>(PIN_XSMT));
}

// コールバックで 16bit L/R を取得し、32bit I2S スロット（上位16bit）へ変換する
void AudioOutput::fillBuffer(int32_t* dst, int16_t* scratch_l, int16_t* scratch_r) {
    if (callback_) {
        callback_(scratch_l, scratch_r, BUFFER_FRAMES);
    } else {
        memset(scratch_l, 0, BUFFER_FRAMES * sizeof(int16_t));
        memset(scratch_r, 0, BUFFER_FRAMES * sizeof(int16_t));
    }

    for (size_t i = 0; i < BUFFER_FRAMES; i++) {
        const float effective = volume_ * AudioConfig::CODE_VOLUME_GAIN;
        int32_t l = static_cast<int32_t>(scratch_l[i] * effective);
        int32_t r = static_cast<int32_t>(scratch_r[i] * effective);
        if (l > 32767) {
            l = 32767;
        } else if (l < -32768) {
            l = -32768;
        }
        if (r > 32767) {
            r = 32767;
        } else if (r < -32768) {
            r = -32768;
        }
        dst[i * 2] = l << 16;
        dst[i * 2 + 1] = r << 16;
    }
}

// 指定バッファから PIO TX FIFO へ DMA 転送を開始する
void AudioOutput::kickDma(const int32_t* src) {
    if (dma_channel_ < 0) {
        return;
    }
    dma_channel_config cfg = dma_channel_get_default_config(dma_channel_);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_dreq(&cfg, pio_get_dreq(pio_, sm_, true));
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);

    dma_channel_configure(dma_channel_, &cfg, &pio_->txf[sm_], src, BUFFER_WORDS, true);
    dma_in_flight_ = true;
}

// DMA 転送完了時に非アクティブ側バッファを充填し、次の DMA をキックする（ダブルバッファ）
void AudioOutput::onDmaComplete() {
    if (!playing_) {
        return;
    }

    static int16_t scratch_l[BUFFER_FRAMES];
    static int16_t scratch_r[BUFFER_FRAMES];

    const uint8_t next = static_cast<uint8_t>(1 - current_buffer_);
    int32_t* next_buf = (next == 0) ? buffer_a_ : buffer_b_;
    fillBuffer(next_buf, scratch_l, scratch_r);
    kickDma(next_buf);
    current_buffer_ = next;
}

// Core 1 上で両バッファを充填し、I2S 再生を開始する
void AudioOutput::startPlaybackOnCore1() {
    if (playing_ || dma_channel_ < 0) {
        return;
    }

    static int16_t scratch_l[BUFFER_FRAMES];
    static int16_t scratch_r[BUFFER_FRAMES];

    playing_ = true;
    fillBuffer(buffer_a_, scratch_l, scratch_r);
    fillBuffer(buffer_b_, scratch_l, scratch_r);
    current_buffer_ = 0;

    using namespace AudioConfig::I2S;
    if (PIN_XSMT >= 0) {
        gpio_put(static_cast<uint>(PIN_XSMT), XSMT_UNMUTE_LEVEL ? 1 : 0);
    }
    if (PIN_SPMUTE >= 0) {
        gpio_put(static_cast<uint>(PIN_SPMUTE), XSMT_UNMUTE_LEVEL ? 1 : 0);
    }

    kickDma(buffer_a_);
    printf("AudioOutput: I2S playback start (Core1), XSMT GP%d=%d\n",
           static_cast<int>(PIN_XSMT), XSMT_UNMUTE_LEVEL ? 1 : 0);
}

// Core 1 上で DMA を中止し、バッファを無音化して再生を停止する
void AudioOutput::stopPlaybackOnCore1() {
    if (!playing_) {
        return;
    }
    playing_ = false;
    dma_in_flight_ = false;
    using namespace AudioConfig::I2S;
    if (PIN_XSMT >= 0) {
        gpio_put(static_cast<uint>(PIN_XSMT), XSMT_UNMUTE_LEVEL ? 0 : 1);
    }
    if (PIN_SPMUTE >= 0) {
        gpio_put(static_cast<uint>(PIN_SPMUTE), XSMT_UNMUTE_LEVEL ? 0 : 1);
    }
    if (dma_channel_ >= 0) {
        dma_channel_abort(dma_channel_);
    }
    memset(buffer_a_, 0, sizeof(buffer_a_));
    memset(buffer_b_, 0, sizeof(buffer_b_));
    printf("AudioOutput: I2S playback stop\n");
}

// Core 1 のメインループ。再生開始/停止要求の処理とバッテリー監視を行う
void AudioOutput::core1Loop() {
    if (!flash_safe_execute_core_init()) {
        printf("AudioOutput: flash_safe_execute_core_init failed\n");
    }
    initHardwareOnCore1();
    BatteryMonitor::initOnCore1();

    uint32_t last_battery_ms = to_ms_since_boot(get_absolute_time());

    while (true) {
        if (start_requested_ && !playing_) {
            start_requested_ = false;
            startPlaybackOnCore1();
        }
        if (stop_requested_ && playing_) {
            stop_requested_ = false;
            stopPlaybackOnCore1();
        }

        if (playing_ && dma_in_flight_ && dma_channel_ >= 0 && !dma_channel_is_busy(dma_channel_)) {
            onDmaComplete();
        }

        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_battery_ms >= BatteryConfig::SAMPLE_INTERVAL_MS) {
            BatteryMonitor::tick();
            last_battery_ms = now_ms;
        }

        tight_loop_contents();
    }
}

// Core 0 から再生開始を Core 1 へ要求する
void AudioOutput::start() {
    if (!initialized_) {
        return;
    }
    start_requested_ = true;
}

// Core 0 から再生停止を要求し、Core 1 が停止するまで最大 500ms 待機する
void AudioOutput::stop() {
    stop_requested_ = true;
    const uint32_t deadline = to_ms_since_boot(get_absolute_time()) + 500;
    while (playing_ && to_ms_since_boot(get_absolute_time()) < deadline) {
        tight_loop_contents();
    }
}

// 再生音量を 0.0〜1.0 の範囲にクランプして設定する
void AudioOutput::setVolume(float volume) {
    if (volume < 0.0f) {
        volume = 0.0f;
    }
    if (volume > 1.0f) {
        volume = 1.0f;
    }
    volume_ = volume;
}

// 指定周波数・時間のトーン再生（未実装・スタブ）
void AudioOutput::playTone(float frequency, float duration_ms) {
    (void)frequency;
    (void)duration_ms;
}
