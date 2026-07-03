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

    /** コンストラクタ */
    LuaInterpreter();
    /** デストラクタ */
    ~LuaInterpreter();

    /** LCD / ボタン / 描画コールバックを登録する */
    void setHostHooks(const LuaHostHooks& hooks);
    /** PCM5102 出力と Lua 音声 API を接続しストリーミング開始 */
    void setAudioOutput(AudioOutput* audio);
    /** FatFS 利用前に SD マウント済みかを設定 */
    /** FatFS 利用前に SD マウント済みかを設定 */
    void setSdMounted(bool mounted);
    /** 読み込み可能な Lua スクリプトの最大バイト数を設定 */
    void setMaxScriptBytes(size_t max_bytes);

    /** SD ルートの main.lua / game.lua / boot.lua、無ければ最初の .lua（1回実行） */
    bool executeOnSdRoot();

    /** game_update / game_draw ループ付きゲーム実行。
     *  game_machine_main の runGameFromMenuAndTeardown から呼ばれるメインエントリ。
     *  各フレーム: update → 全バンドで draw（layers 時は composeBand 先行）
     *  → DMA 完了待ち → SD ストリーム FD クローズ。
     *  戻り後は finishGameSession / closePendingGameSession で片付けること。 */
    bool runGameLoopFromSd(const char* path);

    /** ゲームループ直後の軽量終了（音声停止・BG ストリーム・Lua 大グローバル trim）。
     *  lua_close は行わない。メニューへ戻る前に必ず呼ぶ。 */
    void finishGameSession();

    /** finishGameSession 後も Lua VM やフォントが残っているか（遅延解放が必要か） */
    bool hasPendingGameSession() const;

    /** 遅延していた Lua VM・画像・フォント等を完全解放。
     *  前回ゲームの残骸がある状態で次の runGameLoopFromSd を呼ぶ前にも使用される。 */
    void closePendingGameSession();

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

    /** 登録済みホストコールバックを返す */
    const LuaHostHooks& hostHooks() const { return hooks_; }
    /** Lua 音声エンジンへの参照を返す */
    LuaAudio& audioEngine() { return audio_engine_; }
    /** 現在の描画モードを返す */
    LuaDrawMode drawMode() const { return draw_mode_; }
    /** 描画モード（Direct / Layers）を設定する */
    void setDrawMode(LuaDrawMode mode) { draw_mode_ = mode; }
    /** タイルレイヤーシステムへの参照を返す */
    TileLayerSystem& tileLayers() { return tile_layers_; }
    /** レイヤー合成の背景色を設定する */
    void setLayerBackdrop(uint16_t color) {
        layer_backdrop_color_ = color;
        tile_layers_.setBackdropColor(color);
    }
    /** レイヤー合成の背景色を返す */
    uint16_t layerBackdropColor() const { return layer_backdrop_color_; }

    /** SD から RGB565 画像を読み込みスロット ID を返す */
    int loadImage(const char* path, uint16_t w, uint16_t h);
    /** 画像スロット ID に対応する ImageSlot を返す */
    const ImageSlot* getImage(int id) const;
    /** 指定 ID の画像スロットを解放する */
    void freeImage(int id);
    /** すべての画像スロットを解放する */
    void freeAllImages();

    /** 背景 1 枚: SD から現在バンド行だけ読み drawImageSub（RAM に全枚載せない）。
     *  フレーム中 FIL を保持し、次バンドを prefetchBgStreamBand で先読み可能。 */
    bool drawBgStreamFromSd(const char* path, int dx, int dy, uint16_t w, uint16_t h);
    /** 1 ビット白黒フレーム: SD から現在バンド行だけ読み RGB565 に展開して描画 */
    bool drawBwStreamFromSd(const char* path, int dx, int dy, uint16_t w, uint16_t h,
                            uint16_t fg, uint16_t bg);
    /** 1 ビット白黒フレーム列（BWPK）: 指定フレームを読み込みバンド描画 */
    bool drawBwPackFromSd(const char* path, int frame_index, int dx, int dy, uint16_t w,
                          uint16_t h, uint16_t fg, uint16_t bg);
    /** フレーム末・ゲーム終了時に bg_stream_ の FIL を閉じる（BW パック状態は維持） */
    void closeBgStream();
    /** bg_stream_ を完全リセット（ゲーム終了・アセット解放時） */
    void resetBgStream();

    /** VN: 背景 + 立ち絵最大 2 枚を SD バンド合成（vn_stream_compose.cpp）。
     *  machine.draw_vn_stream から呼ばれる。 */
    bool drawVnStreamCompose(lua_State* L, int table_index);
    /** vn_stream_ の全 FIL を閉じる（フレーム末に runGameLoopFromSd が呼ぶ） */
    void closeVnStreamCompose(bool abandon_open_files = false);

    /** MISF サブセットフォント（美咲）を SD から読み込む */
    bool loadFont(const char* path);
    /** 読み込み済みフォントをアンロードする */
    void unloadFont();
    /** フォントレンダラーへの参照を返す */
    FontRenderer* fontRenderer() { return &font_renderer_; }
    /** フォントレンダラーへの const 参照を返す */
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
    friend void vnStreamComposeClose(LuaInterpreter* interp, bool abandon_open_files);

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
        bool bw_mode = false;
        uint16_t width = 0;
        uint16_t height = 0;
        uint16_t bw_fg = 0xFFFF;
        uint16_t bw_bg = 0x0000;
        int bw_pack_frame = 0;
        uint32_t bw_pack_count = 0;
        uint32_t bw_pack_data_base = 0;
        int bw_buffer_frame = 0;
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
        /** draw_bw_pack RGB 全画面: band0 以降は Lua game_draw を省略して C 側 blit */
        bool bw_rgb_fast_bands = false;
    } bg_stream_;

    VnStreamComposeState vn_stream_;

    /** bad_apple 実行時のみ true（他ゲーム API には影響なし） */
    bool bad_apple_player_ = false;
    bool bad_apple_fast_active_ = false;
    char bad_apple_pack_path_[FF_LFN_BUF + 4]{};
    bool bad_apple_ready_ = false;
    bool bad_apple_missing_ = false;
    int bad_apple_frame_idx_ = 1;
    int bad_apple_frame_acc_ = 0;
    int bad_apple_frame_ms_ = 33;
    uint32_t bad_apple_frame_count_ = 6572;
    int bad_apple_last_drawn_frame_ = 0;
    bool bad_apple_skip_prefetch_ = false;

    /** bad_apple 専用: Lua game_update を C 側で処理（true=継続, false=終了） */
    bool runBadAppleUpdate(int dt_ms);

    /** 次バンド用に背景ストリームの行データを先読みする */
    void prefetchBgStreamBand(int display_band);
    /** draw_bw_pack RGB 全画面時: 現在バンドへキャッシュ RGB を blit（Lua 省略用） */
    bool drawBwPackBlitCurrentBand();

    /** bad_apple 専用: BWPK を RGB565 キャッシュまで準備（帯 blit なし） */
    bool ensureBwPackRgbFrameReady(const char* path, int frame_index, uint16_t w, uint16_t h,
                                   uint16_t fg, uint16_t bg, const uint16_t** out_pixels);
    /** bad_apple 専用: Lua game_draw を使わず 1 回 DMA で全画面描画 */
    bool runBadAppleDrawFrame();
    /** bad_apple 専用: game_init 後に FRAMES_PACK 等を取り込む */
    void initBadApplePlayerFromLua();
    /** スクリプトパスが bad_apple か判定 */
    static bool isBadAppleScriptPath(const char* path);

    /** 実行中ゲームのスクリプトディレクトリをクリアする */
    void clearGameScriptDir();
    /** スクリプトパスからゲーム用基準ディレクトリを設定する */
    void setGameScriptFromPath(const char* script_path);

    /** SD ファイルをヒープバッファに読み込む */
    bool readSdFileToBuffer(const char* path, char** out_buf, size_t* out_len);
    /** カスタムアロケータで新しい Lua VM を生成する */
    lua_State* newLuaState();
    /** ファイル名が .lua 拡張子か判定する */
    bool endsWithLuaExt(const char* name) const;
    /** LCD にステータス行を表示する */
    void showStatus(const char* line1, const char* line2, uint16_t color, uint16_t bg);
    /** 直近エラーメッセージを保存する */
    void setLastError(const char* msg);
    /** バッファ内 Lua ソースをロードして実行する */
    bool loadScriptIntoState(lua_State* L, const char* path, char* source, size_t len);
    /** print / sleep_ms / machine.* を Lua に登録 */
    void registerLuaHostApi(lua_State* L);
    /** ゲーム状態をすべて終了する */
    void closeGameState();
    /** フォント / Lua VM / タイル等のみ解放（finishGameSession 後） */
    void closeDeferredSession();
    /** 画像・フォント・SD ストリーム等（Lua VM 以外）を解放 */
    void releaseGameAssets();
    /** trim 済み Lua VM を lua_close する */
    void releaseGameLuaVm();
    /** タイルレイヤー合成時の画像 ID ルックアップコールバック */
    static const TileLayerImageView* tileLayerLookupImage(int id, void* ctx);
};

#endif // LUA_INTERPRETER_HPP
