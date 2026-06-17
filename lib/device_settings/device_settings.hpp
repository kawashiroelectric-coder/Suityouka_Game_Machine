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

    static void load();
    /** 遅延フラッシュ書き込み（メインループから定期的に呼ぶ） */
    static void service();
    static int volumeStep();
    static int brightnessPercent();
    static void setVolumeStep(int step);
    static void setBrightnessPercent(int percent);
};

#endif  // DEVICE_SETTINGS_HPP
