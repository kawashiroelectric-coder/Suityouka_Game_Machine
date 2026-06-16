// ============================================
// ファイル: lua_interpreter.hpp
// SD 上 Lua 5.4 の読み込み・実行と machine.* API の中核。
//
// 依存: GameDisplay（描画）, TileLayerSystem（layers モード）,
//       LuaAudio→AudioOutput（音声）, FontRenderer, HeapBudget, FatFS
//
// 実装分割:
//   lua_api_machine.cpp … machine 登録・パス
//   lua_api_draw.cpp    … 描画・バンド・画像
//   lua_api_audio.cpp   … 音声
//   vn_stream_compose   … draw_vn_stream
//   bg_stream_util      … SD バンド行読み込み（BG/VN 共用）
// ============================================

#ifndef LUA_INTERPRETER_HPP
#define LUA_INTERPRETER_HPP

#include <cstddef>
#include <cstdint>

#include "lua_audio.hpp"
#include "tile_layers.hpp"
#include "font_renderer.hpp"
#include "vn_stream_compose.hpp"

extern "C" {
#include "ff.h"
}

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

    /** game_update / game_draw ループ付きゲーム実行。
     *  FileExplorer::on_run_lua から呼ばれるメインエントリ。
     *  各フレーム: update → 全バンドで draw（layers 時は composeBand 先行）
     *  → DMA 完了待ち → SD ストリーム FD クローズ。 */
    bool runGameLoopFromSd(const char* path);

    /** 指定パスの .lua を読み込んで1回実行 */
    bool runScriptFromSd(const char* path);

    /** SD 上の通常ファイルが存在するか */
    bool sdFileExists(const char* path) const;
    /** SD 上のファイルサイズ（バイト）。存在しない/ディレクトリなら 0 */
    size_t sdFileSize(const char* path) const;

    /** 直近の runGameLoopFromSd / load 失敗メッセージ（LCD 表示用） */
    const char* lastError() const { return last_error_; }

    /** SD 上の .lua を実行し return 値 1 個を L に push */
    bool pushLoadReturnFromSd(lua_State* L, const char* path);

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

    /** 背景 1 枚: SD から現在バンド行だけ読み drawImageSub（RAM に全枚載せない）。
     *  フレーム中 FIL を保持し、次バンドを prefetchBgStreamBand で先読み可能。 */
    bool drawBgStreamFromSd(const char* path, int dx, int dy, uint16_t w, uint16_t h);
    /** フレーム末・ゲーム終了時に bg_stream_ の FIL を閉じる */
    void closeBgStream();

    /** VN: 背景 + 立ち絵最大 2 枚を SD バンド合成（vn_stream_compose.cpp）。
     *  machine.draw_vn_stream から呼ばれる。 */
    bool drawVnStreamCompose(lua_State* L, int table_index);
    /** vn_stream_ の全 FIL を閉じる（フレーム末に runGameLoopFromSd が呼ぶ） */
    void closeVnStreamCompose();

    /** MISF サブセットフォント（美咲）を SD から読み込む */
    bool loadFont(const char* path);
    void unloadFont();
    FontRenderer* fontRenderer() { return &font_renderer_; }
    const FontRenderer* fontRenderer() const { return &font_renderer_; }

    /** 実行中 .lua のディレクトリ（例: "/visual_novel"）。resolveGamePath の基準。 */
    const char* gameScriptDir() const { return game_script_dir_; }

    /** 相対パス → SD 絶対パス（sd_path_util::resolveSdPath 経由） */
    void resolveGamePath(const char* path, char* out, size_t out_len) const;

    /** テーブルを SD に保存（Lua リテラル形式、最大 16KB） */
    bool saveDataToSd(lua_State* L, int table_index, const char* path);
    /** SD からテーブルを読み込み L に push（失敗時 stack 変更なし） */
    bool loadDataFromSd(lua_State* L, const char* path);

    friend bool vnStreamComposeDraw(LuaInterpreter* interp, lua_State* L, int table_index);
    friend void vnStreamComposeClose(LuaInterpreter* interp);

private:
    LuaHostHooks hooks_;
    LuaAudio audio_engine_;
    LuaDrawMode draw_mode_;
    TileLayerSystem tile_layers_;
    uint16_t layer_backdrop_color_;
    bool sd_mounted_;
    size_t max_script_bytes_;
    char lcd_line_[64];
    char last_error_[96];
    lua_State* game_lua_;
    ImageSlot images_[kMaxImageSlots];
    FontRenderer font_renderer_;
    char game_script_dir_[FF_LFN_BUF + 4];

    struct BgStreamState {
        FIL file{};
        char path[FF_LFN_BUF + 4]{};
        char fail_path[FF_LFN_BUF + 4]{};
        bool open = false;
        uint16_t width = 0;
        uint16_t height = 0;
        int dx = 0;
        int dy = 0;
        struct {
            bool valid = false;
            int display_band = -1;
            int draw_top = 0;
            int rows = 0;
            int src_y0 = 0;
            uint8_t buf_slot = 0;
        } prefetch;
    } bg_stream_;

    VnStreamComposeState vn_stream_;

    void prefetchBgStreamBand(int display_band);

    void clearGameScriptDir();
    void setGameScriptFromPath(const char* script_path);

    bool readSdFileToBuffer(const char* path, char** out_buf, size_t* out_len);
    lua_State* newLuaState();
    bool endsWithLuaExt(const char* name) const;
    void showStatus(const char* line1, const char* line2, uint16_t color, uint16_t bg);
    void setLastError(const char* msg);
    bool loadScriptIntoState(lua_State* L, const char* path, char* source, size_t len);
    /** print / sleep_ms / machine.* を Lua に登録 */
    void registerLuaHostApi(lua_State* L);
    void closeGameState();
    static const TileLayerImageView* tileLayerLookupImage(int id, void* ctx);
};

#endif // LUA_INTERPRETER_HPP
