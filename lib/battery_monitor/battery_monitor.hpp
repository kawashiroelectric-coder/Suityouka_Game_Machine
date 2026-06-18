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
    /** LED 制御先の ButtonInput を登録する（main 起動時） */
    static void attach(ButtonInput* buttons);
    /** Core1 起動時に ADC を初期化する */
    static void initOnCore1();
    /** ADC 読み取りと LED 更新（Core1 で 100ms 周期） */
    static void tick();

    /** 直近の ADC 測定電圧（V）を返す */
    static float lastVoltage();
    /** 直近の残量 LED マスク（下位 3bit）を返す */
    static uint8_t lastLedMask();

private:
    /** 電圧（V）から残量 LED マスクへ変換する */
    static uint8_t voltageToLedMask(float voltage_v);
};

#endif  // BATTERY_MONITOR_HPP
