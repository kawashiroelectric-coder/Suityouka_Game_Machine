// ============================================
// ファイル: game_select_menu.hpp
// ゲーム選択メニュー（部分再描画・プレビュー付き）
// ============================================

#ifndef GAME_SELECT_MENU_HPP
#define GAME_SELECT_MENU_HPP

#include <cstddef>
#include <cstdint>

class ST7789_LCD;
class ButtonInput;

/** /games 配下のゲーム一覧から起動する GUI メニュー */
class GameSelectMenu {
public:
    using FrameCallback = void (*)(void* user_data);
    using RunGameCallback = void (*)(const char* script_path, void* user_data);
    using RunInputTestCallback = void (*)(void* user_data);
    using SdStateCallback = void (*)(void* user_data);

    struct Config {
        ST7789_LCD* lcd = nullptr;
        ButtonInput* buttons = nullptr;
        const char* games_dir = nullptr;
        FrameCallback on_frame = nullptr;
        RunGameCallback on_run_game = nullptr;
        /** 設定画面の Input Test 行で呼ぶ（nullptr なら無視） */
        RunInputTestCallback on_run_input_test = nullptr;
        /** SD マウント／アンマウント直後に呼ぶ（Lua 同期等） */
        SdStateCallback on_sd_state_changed = nullptr;
        void* user_data = nullptr;
        uint32_t frame_interval_ms = 50;
    };

    /** ゲーム選択メニューのメインループを開始する。game_machine_main から起動時に呼ぶ */
    static bool run(const Config& config);
};

#endif  // GAME_SELECT_MENU_HPP
