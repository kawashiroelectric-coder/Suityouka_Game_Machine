// ============================================
// ファイル: game_select_menu.hpp
// ゲーム選択メニュー（部分再描画・プレビュー付き）
// ============================================

#ifndef GAME_SELECT_MENU_HPP
#define GAME_SELECT_MENU_HPP

#include <cstdint>

class ST7789_LCD;
class ButtonInput;

/** /games 配下のゲーム一覧から起動する GUI メニュー */
class GameSelectMenu {
public:
    using FrameCallback = void (*)(void* user_data);
    using RunGameCallback = void (*)(const char* script_path, void* user_data);
    using RunInputTestCallback = void (*)(void* user_data);

    struct Config {
        ST7789_LCD* lcd = nullptr;
        ButtonInput* buttons = nullptr;
        const char* games_dir = nullptr;
        FrameCallback on_frame = nullptr;
        RunGameCallback on_run_game = nullptr;
        /** 設定画面の Input Test 行で呼ぶ（nullptr なら無視） */
        RunInputTestCallback on_run_input_test = nullptr;
        void* user_data = nullptr;
        uint32_t frame_interval_ms = 50;
    };

    /** SD マウント済み・カード挿入中はループ。ゲーム起動は on_run_game へ委譲 */
    static bool run(const Config& config);
};

#endif  // GAME_SELECT_MENU_HPP
