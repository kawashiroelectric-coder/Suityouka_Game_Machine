// ============================================
// ファイル: battery_monitor.cpp
// GP28 (ADC2) を Core1 で 100ms 周期サンプル → 残量 LED
// ============================================

#include "battery_monitor.hpp"

#include <cstdio>

#include "button_input.hpp"
#include "config.hpp"
#include "hardware/adc.h"
#include "pico/stdlib.h"

static ButtonInput* s_buttons = nullptr;
static volatile float s_last_voltage = 0.0f;
static volatile uint8_t s_last_mask = BatteryLedConfig::MASK_OFF;

void BatteryMonitor::attach(ButtonInput* buttons) {
    s_buttons = buttons;
}

float BatteryMonitor::lastVoltage() {
    return s_last_voltage;
}

uint8_t BatteryMonitor::lastLedMask() {
    return s_last_mask;
}

void BatteryMonitor::initOnCore1() {
    adc_init();
    adc_gpio_init(BatteryConfig::PIN_ADC);
    adc_select_input(BatteryConfig::ADC_CHANNEL);
    printf("BatteryMonitor: Core1 ADC init (GP%u, ch%u)\n",
           (unsigned)BatteryConfig::PIN_ADC, (unsigned)BatteryConfig::ADC_CHANNEL);
}

uint8_t BatteryMonitor::voltageToLedMask(float voltage_v) {
    if (voltage_v >= BatteryConfig::THRESHOLD_FULL_V) {
        return BatteryLedConfig::MASK_FULL;
    }
    if (voltage_v >= BatteryConfig::THRESHOLD_MID_V) {
        return BatteryLedConfig::MASK_MID;
    }
    return BatteryLedConfig::MASK_LOW;
}

void BatteryMonitor::tick() {
    if (!s_buttons) {
        return;
    }

    const uint16_t raw = adc_read();
    const float voltage =
        static_cast<float>(raw) * BatteryConfig::ADC_VREF / static_cast<float>(BatteryConfig::ADC_MAX);
    const uint8_t mask = voltageToLedMask(voltage);

    s_last_voltage = voltage;
    s_last_mask = mask;
    s_buttons->setBatteryLedMask(mask);
}
