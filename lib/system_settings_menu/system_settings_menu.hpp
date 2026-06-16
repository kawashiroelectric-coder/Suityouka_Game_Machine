// ============================================
// ファイル: system_settings_menu.hpp
// システム設定メニュー（拡張用）
// ============================================

#ifndef SYSTEM_SETTINGS_MENU_HPP
#define SYSTEM_SETTINGS_MENU_HPP

#include <cstdint>

class ST7789_LCD;
class ButtonInput;

/** WiFi / 音量等のシステム設定画面 */
class SystemSettingsMenu {
public:
    using FrameCallback = void (*)(void* user_data);
    using RunInputTestCallback = void (*)(void* user_data);

    struct Config {
        ST7789_LCD* lcd = nullptr;
        ButtonInput* buttons = nullptr;
        FrameCallback on_frame = nullptr;
        RunInputTestCallback on_run_input_test = nullptr;
        void* user_data = nullptr;
        uint32_t frame_interval_ms = 50;
    };

    /** LEFT / Back で抜けるまでブロック */
    static void run(const Config& config);
};

#endif  // SYSTEM_SETTINGS_MENU_HPP
