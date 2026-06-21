// ============================================
// ファイル: battery_monitor.cpp
// GP28 (ADC2) を Core1 で 100ms 周期サンプル → 残量 LED
// ============================================

#include "battery_monitor.hpp"

#include <cstdio>

#include "button_input.hpp"
#include "config.hpp"
#include "device_settings.hpp"
#include "hardware/adc.h"
#include "pico/stdlib.h"

static ButtonInput* s_buttons = nullptr;
static volatile float s_last_voltage = 0.0f;
static volatile uint8_t s_last_mask = BatteryLedConfig::MASK_OFF;
static volatile bool s_led_update_paused = false;
static uint8_t s_stable_mask = BatteryLedConfig::MASK_OFF;
static uint32_t s_pulse_until_ms = 0;
static bool s_boot_pulse_pending = true;

/** 現在の LED 出力ポリシーに従いマスクを I2C へ反映する */
static void applyLedOutput(uint8_t mask) {
    if (!s_led_update_paused && s_buttons) {
        s_buttons->setBatteryLedMask(mask);
    }
}

/** Pulse モード: 指定マスクを LED_PULSE_MS だけ点灯する */
static void startPulse(uint8_t mask, uint32_t now_ms) {
    s_stable_mask = mask;
    s_pulse_until_ms = now_ms + BatteryConfig::LED_PULSE_MS;
    applyLedOutput(mask);
}

/** 設定モードに応じて LED 表示を更新する */
static void applyDisplayPolicy(uint32_t now_ms, uint8_t mask, bool force_pulse) {
    if (s_led_update_paused) {
        return;
    }

    if (DeviceSettings::batteryLedMode() == DeviceSettings::BatteryLedMode::AlwaysOn) {
        s_pulse_until_ms = 0;
        s_boot_pulse_pending = false;
        s_stable_mask = mask;
        applyLedOutput(mask);
        return;
    }

    if (s_boot_pulse_pending || force_pulse) {
        s_boot_pulse_pending = false;
        startPulse(mask, now_ms);
        return;
    }

    if (mask != s_stable_mask) {
        startPulse(mask, now_ms);
        return;
    }

    if (s_pulse_until_ms != 0) {
        if (now_ms < s_pulse_until_ms) {
            return;
        }
        s_pulse_until_ms = 0;
        applyLedOutput(BatteryLedConfig::MASK_OFF);
    }
}

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

void BatteryMonitor::setLedUpdatePaused(bool paused) {
    s_led_update_paused = paused;
}

bool BatteryMonitor::isLedUpdatePaused() {
    return s_led_update_paused;
}

void BatteryMonitor::resumeLedAutoUpdate() {
    s_led_update_paused = false;
    onDisplayModeChanged();
}

void BatteryMonitor::onDisplayModeChanged() {
    if (s_led_update_paused) {
        return;
    }

    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (DeviceSettings::batteryLedMode() == DeviceSettings::BatteryLedMode::AlwaysOn) {
        s_boot_pulse_pending = false;
        s_pulse_until_ms = 0;
        applyDisplayPolicy(now_ms, s_last_mask, false);
        return;
    }

    s_boot_pulse_pending = true;
    s_pulse_until_ms = 0;
    applyDisplayPolicy(now_ms, s_last_mask, true);
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

    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    applyDisplayPolicy(now_ms, mask, false);
}
