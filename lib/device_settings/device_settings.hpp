// ============================================
// ファイル: device_settings.hpp
// 本体フラッシュ ROM への音量・輝度の永続化
// ============================================

#ifndef DEVICE_SETTINGS_HPP
#define DEVICE_SETTINGS_HPP

#include <cstdint>

/** フラッシュ末尾 1 セクタに保存するシステム設定 */
class DeviceSettings {
public:
    static constexpr int kDefaultBrightnessPercent = 80;

    /** バッテリー残量 LED の表示モード */
    enum class BatteryLedMode : uint8_t {
        /** 常に現在の残量 LED を表示 */
        AlwaysOn = 0,
        /** 起動時・残量変化時のみ LED_PULSE_MS だけ表示 */
        PulseOnChange = 1,
    };

    /** 起動時にフラッシュから音量・輝度を読み込む */
    static void load();
    /** 遅延フラッシュ書き込み（メインループから定期的に呼ぶ） */
    static void service();
    /** 現在の音量ステップ（0 始まり）を返す */
    static int volumeStep();
    /** 現在の LCD 輝度（%）を返す */
    static int brightnessPercent();
    /** 音量ステップを設定し遅延フラッシュ保存を予約する */
    static void setVolumeStep(int step);
    /** LCD 輝度（%）を設定し遅延フラッシュ保存を予約する */
    static void setBrightnessPercent(int percent);
    /** バッテリー LED 表示モードを返す */
    static BatteryLedMode batteryLedMode();
    /** バッテリー LED 表示モードを設定し遅延フラッシュ保存を予約する */
    static void setBatteryLedMode(BatteryLedMode mode);
    /** 未保存なら即時フラッシュ（メニュー退場時など） */
    static void flushPending();
};

#endif  // DEVICE_SETTINGS_HPP
