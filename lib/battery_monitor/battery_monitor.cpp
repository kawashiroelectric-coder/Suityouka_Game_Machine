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

/** LED 制御先の ButtonInput を登録する（main 起動時） */
void BatteryMonitor::attach(ButtonInput* buttons) {
    s_buttons = buttons;
}

/** 直近の ADC 測定電圧（V）を返す */
float BatteryMonitor::lastVoltage() {
    return s_last_voltage;
}

/** 直近の残量 LED マスク（下位 3bit）を返す */
uint8_t BatteryMonitor::lastLedMask() {
    return s_last_mask;
}

/** Core1 起動時に GP28 ADC を初期化する */
void BatteryMonitor::initOnCore1() {
    adc_init();
    adc_gpio_init(BatteryConfig::PIN_ADC);
    adc_select_input(BatteryConfig::ADC_CHANNEL);
    printf("BatteryMonitor: Core1 ADC init (GP%u, ch%u)\n",
           (unsigned)BatteryConfig::PIN_ADC, (unsigned)BatteryConfig::ADC_CHANNEL);
}

/** 電圧（V）から残量 LED マスク（LOW/MID/FULL）へ変換する */
uint8_t BatteryMonitor::voltageToLedMask(float voltage_v) {
    if (voltage_v >= BatteryConfig::THRESHOLD_FULL_V) {
        return BatteryLedConfig::MASK_FULL;
    }
    if (voltage_v >= BatteryConfig::THRESHOLD_MID_V) {
        return BatteryLedConfig::MASK_MID;
    }
    return BatteryLedConfig::MASK_LOW;
}

/** ADC を読み取り、残量 LED を更新する（Core1 で 100ms 周期） */
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
