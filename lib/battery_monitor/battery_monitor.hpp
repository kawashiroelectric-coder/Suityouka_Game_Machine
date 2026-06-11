// ============================================
// ファイル: battery_monitor.hpp
// Core1: GP28 ADC でバッテリー電圧監視 → PCA9539 LED
// ============================================

#ifndef BATTERY_MONITOR_HPP
#define BATTERY_MONITOR_HPP

#include <cstdint>

class ButtonInput;

/** Core1 上で 100ms ごとに ADC 読み取りと LED 更新 */
class BatteryMonitor {
public:
    static void attach(ButtonInput* buttons);
    /** Core1 起動時に ADC を初期化 */
    static void initOnCore1();
    /** 100ms 周期で呼ぶ */
    static void tick();

    static float lastVoltage();
    static uint8_t lastLedMask();

private:
    static uint8_t voltageToLedMask(float voltage_v);
};

#endif  // BATTERY_MONITOR_HPP
