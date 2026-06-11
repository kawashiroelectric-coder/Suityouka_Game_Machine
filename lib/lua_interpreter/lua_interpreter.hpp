// ============================================
// ファイル: lua_interpreter.hpp
// SDカード上の Lua スクリプト読み込み・実行
// ============================================

#ifndef LUA_INTERPRETER_HPP
#define LUA_INTERPRETER_HPP

#include <cstddef>
#include <cstdint>

#include "lua_audio.hpp"
#include "tile_layers.hpp"

class AudioOutput;
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

/** Lua から選択する描画方式 */
enum class LuaDrawMode {
    /** 従来: game_draw 内で clear / draw_image 等を直接描く */
    Direct,
    /** GBA 風: ホストがタイルレイヤーを合成してから game_draw（スプライト等） */
    Layers,
};

/** Lua から参照する RGB565 画像スロット */
struct ImageSlot {
    uint16_t* pixels = nullptr;
    size_t byte_size = 0;
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
    /** PCM5102 出力と Lua 音声 API を接続しストリーミング開始 */
    void setAudioOutput(AudioOutput* audio);
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
    LuaAudio& audioEngine() { return audio_engine_; }
    LuaDrawMode drawMode() const { return draw_mode_; }
    void setDrawMode(LuaDrawMode mode) { draw_mode_ = mode; }
    TileLayerSystem& tileLayers() { return tile_layers_; }
    void setLayerBackdrop(uint16_t color) {
        layer_backdrop_color_ = color;
        tile_layers_.setBackdropColor(color);
    }
    uint16_t layerBackdropColor() const { return layer_backdrop_color_; }

    /** 画像スロット操作（Lua バインディングから呼ばれる） */
    int loadImage(const char* path, uint16_t w, uint16_t h);
    const ImageSlot* getImage(int id) const;
    void freeImage(int id);
    void freeAllImages();

private:
    LuaHostHooks hooks_;
    LuaAudio audio_engine_;
    LuaDrawMode draw_mode_;
    TileLayerSystem tile_layers_;
    uint16_t layer_backdrop_color_;
    bool sd_mounted_;
    size_t max_script_bytes_;
    char lcd_line_[64];
    lua_State* game_lua_;
    ImageSlot images_[kMaxImageSlots];

    bool readSdFileToBuffer(const char* path, char** out_buf, size_t* out_len);
    lua_State* newLuaState();
    bool endsWithLuaExt(const char* name) const;
    void showStatus(const char* line1, const char* line2, uint16_t color, uint16_t bg);
    bool loadScriptIntoState(lua_State* L, const char* path, const char* source, size_t len);
    /** print / sleep_ms / machine.* を Lua に登録 */
    void registerLuaHostApi(lua_State* L);
    void closeGameState();
    static const TileLayerImageView* tileLayerLookupImage(int id, void* ctx);
};

#endif // LUA_INTERPRETER_HPP
