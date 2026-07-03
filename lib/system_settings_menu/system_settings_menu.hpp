// ============================================
// ファイル: system_settings_menu.hpp
// システム設定メニュー（拡張用）
// ============================================

#ifndef SYSTEM_SETTINGS_MENU_HPP
#define SYSTEM_SETTINGS_MENU_HPP

#include <cstdint>

class ST7789_LCD;
class ButtonInput;
class LuaAudio;

/** 音量・明るさ等のシステム設定画面。
 *  About（Code Ver）表示文字列は system_settings_menu.cpp の kAboutLines を編集。 */
class SystemSettingsMenu {
public:
    using FrameCallback = void (*)(void* user_data);
    using RunInputTestCallback = void (*)(void* user_data);

    struct Config {
        ST7789_LCD* lcd = nullptr;
        ButtonInput* buttons = nullptr;
        LuaAudio* audio = nullptr;
        FrameCallback on_frame = nullptr;
        RunInputTestCallback on_run_input_test = nullptr;
        void* user_data = nullptr;
        uint32_t frame_interval_ms = 50;
    };

    /** システム設定メニューを起動し LEFT/Back で戻るまでループする。ゲーム選択メニューから呼ぶ */
    static void run(const Config& config);
};

#endif  // SYSTEM_SETTINGS_MENU_HPP
