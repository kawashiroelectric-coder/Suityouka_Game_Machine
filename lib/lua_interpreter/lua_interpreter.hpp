// ============================================
// ファイル: lua_interpreter.hpp
// SDカード上の Lua スクリプト読み込み・実行
// ============================================

#ifndef LUA_INTERPRETER_HPP
#define LUA_INTERPRETER_HPP

#include <cstddef>
#include <cstdint>

/** LCD / ボタンなどホスト機能へのコールバック */
struct LuaHostHooks {
    void* user_data = nullptr;
    void (*draw_text_bg)(void* user_data, int x, int y, const char* text,
                         uint16_t color, uint16_t bg_color) = nullptr;
    bool (*is_button_pressed)(void* user_data, int button_index) = nullptr;
};

class LuaInterpreter {
public:
    static constexpr size_t kDefaultMaxScriptBytes = 48 * 1024;

    LuaInterpreter();

    void setHostHooks(const LuaHostHooks& hooks);
    void setSdMounted(bool mounted);
    void setMaxScriptBytes(size_t max_bytes);

    /** SD ルートの main.lua / game.lua / boot.lua、無ければ最初の .lua */
    bool executeOnSdRoot();

    /** 指定パスの .lua を読み込んで実行 */
    bool runScriptFromSd(const char* path);

    /** FatFS 上にファイルが存在するか */
    bool sdFileExists(const char* path) const;

    const LuaHostHooks& hostHooks() const { return hooks_; }

private:
    LuaHostHooks hooks_;
    bool sd_mounted_;
    size_t max_script_bytes_;
    char lcd_line_[64];

    bool readSdFileToBuffer(const char* path, char** out_buf, size_t* out_len) const;
    bool endsWithLuaExt(const char* name) const;
    void showStatus(const char* line1, const char* line2, uint16_t color, uint16_t bg);
};

#endif // LUA_INTERPRETER_HPP
