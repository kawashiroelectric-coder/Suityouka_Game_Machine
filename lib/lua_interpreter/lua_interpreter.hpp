// ============================================
// ファイル: lua_interpreter.hpp
// SDカード上の Lua スクリプト読み込み・実行
// ============================================

#ifndef LUA_INTERPRETER_HPP
#define LUA_INTERPRETER_HPP

#include <cstddef>
#include <cstdint>

class GameDisplay;

/** LCD / ボタン / フレームバッファへのコールバック */
/** Lua から呼ばれるホスト側描画・入力コールバック */
struct LuaHostHooks {
    void* user_data = nullptr;
    void (*draw_text_bg)(void* user_data, int x, int y, const char* text, uint16_t color,
                         uint16_t bg_color) = nullptr;
    bool (*is_button_pressed)(void* user_data, int button_index) = nullptr;
    GameDisplay* display = nullptr;
};

struct lua_State;

/** Lua から参照する RGB565 画像スロット */
struct ImageSlot {
    uint16_t* pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    bool used = false;
};

/** SD 上の Lua スクリプト読み込みと machine.* API 提供 */
class LuaInterpreter {
public:
    static constexpr size_t kDefaultMaxScriptBytes = 48 * 1024;
    static constexpr int kMaxImageSlots = 16;
    static constexpr size_t kMaxImageBytes = 200 * 1024;

    LuaInterpreter();
    ~LuaInterpreter();

    void setHostHooks(const LuaHostHooks& hooks);
    /** FatFS 利用前に SD マウント済みかを設定 */
    void setSdMounted(bool mounted);
    void setMaxScriptBytes(size_t max_bytes);

    /** SD ルートの main.lua / game.lua / boot.lua、無ければ最初の .lua（1回実行） */
    bool executeOnSdRoot();

    /** game_update / game_draw ループ付きゲーム実行 */
    bool runGameLoopFromSd(const char* path);

    /** 指定パスの .lua を読み込んで1回実行 */
    bool runScriptFromSd(const char* path);

    /** SD 上の通常ファイルが存在するか */
    bool sdFileExists(const char* path) const;

    const LuaHostHooks& hostHooks() const { return hooks_; }

    /** 画像スロット操作（Lua バインディングから呼ばれる） */
    int loadImage(const char* path, uint16_t w, uint16_t h);
    const ImageSlot* getImage(int id) const;
    void freeImage(int id);
    void freeAllImages();

private:
    LuaHostHooks hooks_;
    bool sd_mounted_;
    size_t max_script_bytes_;
    char lcd_line_[64];
    lua_State* game_lua_;
    ImageSlot images_[kMaxImageSlots];

    bool readSdFileToBuffer(const char* path, char** out_buf, size_t* out_len) const;
    bool endsWithLuaExt(const char* name) const;
    void showStatus(const char* line1, const char* line2, uint16_t color, uint16_t bg);
    bool loadScriptIntoState(lua_State* L, const char* path, const char* source, size_t len);
    /** print / sleep_ms / machine.* を Lua に登録 */
    void registerLuaHostApi(lua_State* L);
    void closeGameState();
};

#endif // LUA_INTERPRETER_HPP
