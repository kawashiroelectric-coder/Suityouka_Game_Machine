// ============================================
// ファイル: encoder_input.cpp
// ロータリーエンコーダ（A/B + SW）入力
// ============================================

#include "encoder_input.hpp"

#include "config.hpp"
#include "hardware/gpio.h"
#include "pico/sync.h"

namespace {

/** 2bit グレイコード遷移 → -1 / 0 / +1 */
constexpr int8_t kQuadTable[16] = {
    0, -1, 1, 0,  //
    1, 0, 0, -1,  //
    -1, 0, 0, 1,  //
    0, 1, -1, 0,  //
};

constexpr uint32_t kEncoderIrqEvents = GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL;

EncoderInput* g_irq_encoder = nullptr;

void encoder_gpio_irq(uint gpio, uint32_t events) {
    (void)events;
    if (g_irq_encoder != nullptr) {
        g_irq_encoder->serviceIrq(gpio);
    }
}

}  // namespace

void EncoderInput::initPins() {
    gpio_init(EncoderConfig::PIN_A);
    gpio_set_dir(EncoderConfig::PIN_A, GPIO_IN);
    gpio_pull_up(EncoderConfig::PIN_A);

    gpio_init(EncoderConfig::PIN_B);
    gpio_set_dir(EncoderConfig::PIN_B, GPIO_IN);
    gpio_pull_up(EncoderConfig::PIN_B);

    gpio_init(EncoderConfig::PIN_SW);
    gpio_set_dir(EncoderConfig::PIN_SW, GPIO_IN);
    gpio_pull_up(EncoderConfig::PIN_SW);

    const uint8_t a = gpio_get(EncoderConfig::PIN_A) ? 1u : 0u;
    const uint8_t b = gpio_get(EncoderConfig::PIN_B) ? 1u : 0u;
    last_ab_state_ = static_cast<uint8_t>((a << 1) | b);
    switch_last_ = gpio_get(EncoderConfig::PIN_SW);
    switch_pressed_edge_ = false;
    position_ = 0;
    pending_delta_ = 0;
}

bool EncoderInput::init() {
    if (irq_enabled_) {
        disableIrq();
    }
    initPins();
    return true;
}

bool EncoderInput::initIrq() {
    if (irq_enabled_) {
        disableIrq();
    }
    initPins();

    g_irq_encoder = this;
    gpio_set_irq_enabled_with_callback(EncoderConfig::PIN_A, kEncoderIrqEvents, true,
                                       encoder_gpio_irq);
    gpio_set_irq_enabled(EncoderConfig::PIN_B, kEncoderIrqEvents, true);
    irq_enabled_ = true;
    return true;
}

void EncoderInput::disableIrq() {
    if (!irq_enabled_) {
        return;
    }
    gpio_set_irq_enabled(EncoderConfig::PIN_A, kEncoderIrqEvents, false);
    gpio_set_irq_enabled(EncoderConfig::PIN_B, kEncoderIrqEvents, false);
    if (g_irq_encoder == this) {
        g_irq_encoder = nullptr;
    }
    irq_enabled_ = false;
}

void EncoderInput::decodeQuadratureStep() {
    const uint8_t a = gpio_get(EncoderConfig::PIN_A) ? 1u : 0u;
    const uint8_t b = gpio_get(EncoderConfig::PIN_B) ? 1u : 0u;
    const uint8_t ab = static_cast<uint8_t>((a << 1) | b);
    const int8_t delta =
        kQuadTable[(static_cast<uint8_t>(last_ab_state_ << 2) | ab) & 0x0F];
    if (delta != 0) {
        position_ += delta;
        pending_delta_ += delta;
    }
    last_ab_state_ = ab;
}

void EncoderInput::serviceIrq(unsigned int gpio) {
    if (gpio != EncoderConfig::PIN_A && gpio != EncoderConfig::PIN_B) {
        return;
    }
    decodeQuadratureStep();
}

void EncoderInput::update() {
    if (!irq_enabled_) {
        decodeQuadratureStep();
    }

    const bool sw = gpio_get(EncoderConfig::PIN_SW);
    if (switch_last_ && !sw) {
        switch_pressed_edge_ = true;
        ++switch_press_count_;
    }
    switch_last_ = sw;
}

int32_t EncoderInput::position() const {
    uint32_t irq_state = save_and_disable_interrupts();
    const int32_t pos = position_;
    restore_interrupts(irq_state);
    return pos;
}

int32_t EncoderInput::consumeDelta() {
    uint32_t irq_state = save_and_disable_interrupts();
    const int32_t delta = pending_delta_;
    pending_delta_ = 0;
    restore_interrupts(irq_state);
    return delta;
}

bool EncoderInput::isSwitchPressed() const { return !gpio_get(EncoderConfig::PIN_SW); }

bool EncoderInput::wasSwitchPressed() {
    if (!switch_pressed_edge_) {
        return false;
    }
    switch_pressed_edge_ = false;
    return true;
}

bool EncoderInput::pinA() const { return gpio_get(EncoderConfig::PIN_A); }

bool EncoderInput::pinB() const { return gpio_get(EncoderConfig::PIN_B); }
