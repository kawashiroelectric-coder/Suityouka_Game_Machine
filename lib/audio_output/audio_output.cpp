// ============================================
// ファイル: audio_output.cpp
// PCM5102 32bit I2S（pcm5102_i2s.pio）+ DMA、Core 1 駆動
// ============================================

#include "audio_output.hpp"
#include "battery_monitor.hpp"
#include "pcm5102_i2s.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "pico/multicore.h"

AudioOutput* AudioOutput::instance_ = nullptr;

AudioOutput::AudioOutput()
    : sample_rate_(AudioConfig::SAMPLE_RATE),
      initialized_(false),
      core1_ready_(false),
      playing_(false),
      start_requested_(false),
      stop_requested_(false),
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

AudioOutput::~AudioOutput() {
    stop();
    instance_ = nullptr;
}

void AudioOutput::core1Entry() {
    if (instance_) {
        instance_->core1Loop();
    }
}

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

void AudioOutput::initHardwareOnCore1() {
    using namespace AudioConfig::I2S;

    gpio_init(PIN_SPMUTE);
    gpio_set_dir(PIN_SPMUTE, GPIO_OUT);
    gpio_put(PIN_SPMUTE, 1);

    gpio_init(PIN_MUTE);
    gpio_set_dir(PIN_MUTE, GPIO_OUT);
    gpio_put(PIN_MUTE, 1);

    pio_offset_ = pio_add_program(pio_, &pcm5102_i2s_32_program);
    pcm5102_i2s_init_sm(pio_, sm_, pio_offset_, sample_rate_);

    dma_channel_ = dma_claim_unused_channel(true);
    dma_channel_set_irq1_enabled(dma_channel_, true);
    irq_set_exclusive_handler(DMA_IRQ_1, dmaIrqHandler);
    irq_set_enabled(DMA_IRQ_1, true);

    pcm5102_i2s_32_start(pio_, sm_);
    core1_ready_ = true;
}

void AudioOutput::fillBuffer(int32_t* dst, int16_t* scratch_l, int16_t* scratch_r) {
    if (callback_) {
        callback_(scratch_l, scratch_r, BUFFER_FRAMES);
    } else {
        memset(scratch_l, 0, BUFFER_FRAMES * sizeof(int16_t));
        memset(scratch_r, 0, BUFFER_FRAMES * sizeof(int16_t));
    }

    for (size_t i = 0; i < BUFFER_FRAMES; i++) {
        const int32_t l = static_cast<int32_t>(scratch_l[i] * volume_) << 16;
        const int32_t r = static_cast<int32_t>(scratch_r[i] * volume_) << 16;
        dst[i * 2] = l;
        dst[i * 2 + 1] = r;
    }
}

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
}

void AudioOutput::dmaIrqHandler() {
    if (!instance_ || instance_->dma_channel_ < 0) {
        return;
    }
    if (dma_irqn_get_channel_status(1, instance_->dma_channel_)) {
        dma_irqn_acknowledge_channel(1, instance_->dma_channel_);
        instance_->onDmaComplete();
    }
}

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
    kickDma(buffer_a_);
    printf("AudioOutput: I2S playback start (Core1)\n");
}

void AudioOutput::stopPlaybackOnCore1() {
    if (!playing_) {
        return;
    }
    playing_ = false;
    if (dma_channel_ >= 0) {
        dma_channel_abort(dma_channel_);
    }
    memset(buffer_a_, 0, sizeof(buffer_a_));
    memset(buffer_b_, 0, sizeof(buffer_b_));
    printf("AudioOutput: I2S playback stop\n");
}

void AudioOutput::core1Loop() {
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

        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_battery_ms >= BatteryConfig::SAMPLE_INTERVAL_MS) {
            BatteryMonitor::tick();
            last_battery_ms = now_ms;
        }

        tight_loop_contents();
    }
}

void AudioOutput::start() {
    if (!initialized_) {
        return;
    }
    start_requested_ = true;
}

void AudioOutput::stop() {
    stop_requested_ = true;
    const uint32_t deadline = to_ms_since_boot(get_absolute_time()) + 500;
    while (playing_ && to_ms_since_boot(get_absolute_time()) < deadline) {
        tight_loop_contents();
    }
}

void AudioOutput::setVolume(float volume) {
    if (volume < 0.0f) {
        volume = 0.0f;
    }
    if (volume > 1.0f) {
        volume = 1.0f;
    }
    volume_ = volume;
}

void AudioOutput::playTone(float frequency, float duration_ms) {
    (void)frequency;
    (void)duration_ms;
}
