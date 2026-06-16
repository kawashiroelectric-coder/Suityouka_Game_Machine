// ============================================
// ファイル: file_explorer.hpp
// SD カード上の簡易 CUI ファイルエクスプローラ
// ============================================

#ifndef FILE_EXPLORER_HPP
#define FILE_EXPLORER_HPP

#include <cstddef>
#include <cstdint>

class ST7789_LCD;
class ButtonInput;

/** SD 上のファイルを一覧・選択し .lua のみ実行する */
class FileExplorer {
public:
    using FrameCallback = void (*)(void* user_data);
    using RunLuaCallback = void (*)(const char* path, void* user_data);
    using SdPresentCallback = bool (*)(void* user_data);

    struct Config {
        ST7789_LCD* lcd = nullptr;
        ButtonInput* buttons = nullptr;
        FrameCallback on_frame = nullptr;
        RunLuaCallback on_run_lua = nullptr;
        SdPresentCallback is_sd_present = nullptr;
        void* user_data = nullptr;
        uint32_t frame_interval_ms = 50;
    };

    /** SD 上のファイルを一覧・選択し .lua のみ実行する。
     *  on_run_lua で LuaInterpreter::runGameLoopFromSd を呼ぶ想定（ゲーム終了までブロック）。 */
    static void run(const Config& config);
};

#endif  // FILE_EXPLORER_HPP
