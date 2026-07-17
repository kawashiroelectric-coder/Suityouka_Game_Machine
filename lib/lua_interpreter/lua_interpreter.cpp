// ============================================
// ファイル: lua_interpreter.cpp
// SDカード上の Lua スクリプト読み込み・実行
// ============================================

#include "lua_interpreter.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "audio_output.hpp"
#include "config.hpp"
#include "debug_overlay.hpp"
#include "encoder_volume.hpp"
#include "font_renderer.hpp"
#include "game_display.hpp"
#include "heap_budget.hpp"
#include "bg_stream_util.hpp"
#include "lua_api_internal.hpp"
#include "pico/stdlib.h"
#include "sd_path_util.hpp"
#include "st7789_lcd.hpp"

extern "C" {
#include "f_util.h"
#include "ff.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace {

/** ゲームスクリプトに game_init / game_update / game_draw が定義されているか検証する */
static bool requireGameCallbacks(lua_State* L) {
    static const char* kRequired[] = {"game_init", "game_update", "game_draw"};
    for (const char* name : kRequired) {
        lua_getglobal(L, name);
        const bool ok = lua_isfunction(L, -1);
        lua_pop(L, 1);
        if (!ok) {
            printf("Lua game: missing %s()\n", name);
            return false;
        }
    }
    return true;
}

static bool g_lua_teardown = false;
static bool g_session_teardown = false;

/** ゲーム終了時に大きな Lua グローバル変数を nil で解放してメモリを節約する */
static void trimLargeLuaGlobals(lua_State* L) {
    static const char* kKeys[] = {"SCENES", "ASSETS", "state", "SCENARIO_PATHS", "ASSET_PATHS"};
    for (const char* key : kKeys) {
        lua_pushnil(L);
        lua_setglobal(L, key);
    }
}

/** Lua VM 用カスタムアロケータ。通常時は HeapBudget、teardown 中は素の realloc/free */
static void* luaHeapAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud;
    if (g_lua_teardown) {
        if (nsize == 0) {
            free(ptr);
            return nullptr;
        }
        return realloc(ptr, nsize);
    }
    return HeapBudget::reallocBlock(ptr, osize, nsize);
}

}  // namespace

/** コンストラクタ: 初期状態を設定する */
LuaInterpreter::LuaInterpreter()
    : hooks_(),
      draw_mode_(LuaDrawMode::Direct),
      layer_backdrop_color_(0),
      sd_mounted_(false),
      max_script_bytes_(kDefaultMaxScriptBytes),
      game_lua_(nullptr),
      images_() {
    lcd_line_[0] = '\0';
    last_error_[0] = '\0';
    game_script_dir_[0] = '\0';
}

/** デストラクタ: ゲーム状態と画像スロットを解放する */
LuaInterpreter::~LuaInterpreter() {
    closeGameState();
    freeAllImages();
}

/** 画像・フォント・SD ストリーム等（Lua VM 以外）のゲームアセットを解放する */
void LuaInterpreter::releaseGameAssets() {
    printf("[MENU-DBG] releaseGameAssets begin\n");
    fflush(stdout);
    resetBgStream();
    printf("[MENU-DBG] releaseGameAssets: after resetBgStream\n");
    bwFrameBufRelease(g_session_teardown);
    bwPackRgbBufRelease(g_session_teardown);
    fflush(stdout);
    closeVnStreamCompose(true);
    printf("[MENU-DBG] releaseGameAssets: after closeVnStreamCompose\n");
    fflush(stdout);
    freeAllImages();
    printf("[MENU-DBG] releaseGameAssets: after freeAllImages\n");
    fflush(stdout);
    unloadFont();
    printf("[MENU-DBG] releaseGameAssets: after unloadFont\n");
    fflush(stdout);
    draw_mode_ = LuaDrawMode::Direct;
    layer_backdrop_color_ = 0;
    tile_layers_.reset();
    draw_cmds_.reset();
    printf("[MENU-DBG] releaseGameAssets done\n");
    fflush(stdout);
}

/** trim 済み Lua VM を lua_close して完全解放する */
void LuaInterpreter::releaseGameLuaVm() {
    if (!game_lua_) {
        return;
    }
    luaApiSetActiveInterpreter(nullptr);
    clearGameScriptDir();
    trimLargeLuaGlobals(game_lua_);
    g_lua_teardown = true;
    printf("[MENU-DBG] releaseGameLuaVm: before lua_close\n");
    fflush(stdout);
    lua_close(game_lua_);
    g_lua_teardown = false;
    game_lua_ = nullptr;
    printf("[MENU-DBG] releaseGameLuaVm: after lua_close\n");
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// ゲームセッション終了（game_machine_main::teardownLuaSessionAfterGame から呼ばれる）
//   1. finishGameSession      … 軽量終了（メニュー復帰を阻害しない）
//   2. closePendingGameSession … lua_close / 画像 / フォントの完全解放
// ---------------------------------------------------------------------------

/** ゲームループ終了直後の軽量終了処理（音声停止・グローバル trim、lua_close は行わない） */
void LuaInterpreter::finishGameSession() {
    audio_engine_.stop();
    resetBgStream();
    if (game_lua_) {
        printf("[MENU-DBG] finishGameSession: trimming lua globals\n");
        fflush(stdout);
        trimLargeLuaGlobals(game_lua_);
    }
    luaApiSetActiveInterpreter(nullptr);
    vn_stream_.bg.open = false;
    vn_stream_.bg.active = false;
    for (int i = 0; i < kVnStreamCharLayers; ++i) {
        vn_stream_.chars[i].open = false;
        vn_stream_.chars[i].active = false;
    }
    vn_stream_.char_count = 0;
    printf("[MENU-DBG] finishGameSession done (lua deferred)\n");
    fflush(stdout);
}

/** 遅延解放が必要なゲームセッション（Lua VM やフォント）が残っているか */
bool LuaInterpreter::hasPendingGameSession() const {
    if (game_lua_ != nullptr) {
        return true;
    }
    return FontRenderer::active() == &font_renderer_;
}

/** 遅延していた Lua VM とアセットを段階的に解放する */
void LuaInterpreter::closeDeferredSession() {
    printf("[MENU-DBG] closeDeferredSession begin\n");
    fflush(stdout);
    g_session_teardown = true;
    if (game_lua_) {
        releaseGameLuaVm();
    }
    releaseGameAssets();
    g_session_teardown = false;
    HeapBudget::resetAccounting();
    vn_stream_.bg.open = false;
    vn_stream_.bg.active = false;
    vn_stream_.bg.path[0] = '\0';
    vn_stream_.bg.fail_path[0] = '\0';
    for (int i = 0; i < kVnStreamCharLayers; ++i) {
        vn_stream_.chars[i].open = false;
        vn_stream_.chars[i].active = false;
        vn_stream_.chars[i].path[0] = '\0';
        vn_stream_.chars[i].fail_path[0] = '\0';
    }
    vn_stream_.char_count = 0;
    printf("[MENU-DBG] closeDeferredSession done\n");
    fflush(stdout);
}

/** 保留中のゲームセッションを完全に解放する（次ゲーム起動前にも使用） */
void LuaInterpreter::closePendingGameSession() {
    printf("[MENU-DBG] closePendingGameSession begin\n");
    fflush(stdout);
    g_session_teardown = true;
    if (game_lua_) {
        releaseGameLuaVm();
    }
    releaseGameAssets();
    g_session_teardown = false;
    HeapBudget::resetAccounting();
    vn_stream_.bg.open = false;
    vn_stream_.bg.active = false;
    vn_stream_.bg.path[0] = '\0';
    vn_stream_.bg.fail_path[0] = '\0';
    for (int i = 0; i < kVnStreamCharLayers; ++i) {
        vn_stream_.chars[i].open = false;
        vn_stream_.chars[i].active = false;
        vn_stream_.chars[i].path[0] = '\0';
        vn_stream_.chars[i].fail_path[0] = '\0';
    }
    vn_stream_.char_count = 0;
    printf("[MENU-DBG] closePendingGameSession done\n");
    fflush(stdout);
}

/** ゲーム状態をすべて終了する（音声停止＋遅延セッション解放） */
void LuaInterpreter::closeGameState() {
    audio_engine_.stop();
    bad_apple_player_ = false;
    bad_apple_fast_active_ = false;
    bad_apple_pack_path_[0] = '\0';
    bad_apple_ready_ = false;
    bad_apple_missing_ = false;
    bad_apple_frame_idx_ = 1;
    bad_apple_frame_acc_ = 0;
    bad_apple_frame_ms_ = 33;
    bad_apple_frame_count_ = 6572;
    bad_apple_last_drawn_frame_ = 0;
    resetBgStream();
    closeDeferredSession();
}

/** 実行中ゲームのスクリプトディレクトリをクリアする */
void LuaInterpreter::clearGameScriptDir() { game_script_dir_[0] = '\0'; }

/** スクリプトパスからゲーム用の基準ディレクトリを抽出して保持する */
void LuaInterpreter::setGameScriptFromPath(const char* script_path) {
    if (!script_path || script_path[0] == '\0') {
        clearGameScriptDir();
        return;
    }
    char norm[FF_LFN_BUF + 4];
    normalizeSdPath(script_path, norm, sizeof(norm));
    extractScriptDir(norm, game_script_dir_, sizeof(game_script_dir_));
    printf("Lua script dir: %s (from %s)\n", game_script_dir_, norm);
}

/** 相対パスをゲームスクリプト基準の SD 絶対パスに解決する */
void LuaInterpreter::resolveGamePath(const char* path, char* out, size_t out_len) const {
    const char* dir = (game_script_dir_[0] != '\0') ? game_script_dir_ : nullptr;
    resolveSdPath(dir, path, out, out_len);
}

/** SD から MISF サブセットフォントを読み込み、GameDisplay に適用する */
bool LuaInterpreter::loadFont(const char* path) {
    if (!sd_mounted_) {
        printf("loadFont: SD not mounted\n");
        return false;
    }
    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));
    if (!font_renderer_.loadFromSd(norm)) {
        return false;
    }
    FontRenderer::setActive(&font_renderer_);
    if (hooks_.display) {
        GameDisplay::setFontRenderer(&font_renderer_);
    }
    return true;
}

/** 読み込み済みフォントをアンロードし、グローバル参照を解除する */
void LuaInterpreter::unloadFont() {
    if (g_session_teardown) {
        font_renderer_.unloadRaw();
    } else {
        font_renderer_.unload();
    }
    if (FontRenderer::active() == &font_renderer_) {
        FontRenderer::setActive(nullptr);
    }
    GameDisplay::setFontRenderer(nullptr);
}

/** タイルレイヤー合成時の画像 ID から ImageSlot を TileLayerImageView に変換する */
const TileLayerImageView* LuaInterpreter::tileLayerLookupImage(int id, void* ctx) {
    auto* self = static_cast<LuaInterpreter*>(ctx);
    if (!self) {
        return nullptr;
    }
    const ImageSlot* slot = self->getImage(id);
    if (!slot || !slot->pixels) {
        return nullptr;
    }
    static TileLayerImageView view;
    view.pixels = slot->pixels;
    view.width = slot->width;
    view.height = slot->height;
    return &view;
}

/** SD から RGB565 画像を読み込み、空きスロットに格納して ID を返す */
int LuaInterpreter::loadImage(const char* path, uint16_t w, uint16_t h) {
    if (!sd_mounted_) {
        printf("loadImage: SD not mounted\n");
        return -1;
    }
    const size_t byte_size = static_cast<size_t>(w) * h * 2;
    if (byte_size == 0 || byte_size > kMaxImageBytes) {
        printf("loadImage: size out of range (%u x %u = %u bytes, max %u)\n", w, h,
               static_cast<unsigned>(byte_size), static_cast<unsigned>(kMaxImageBytes));
        return -1;
    }

    int slot_id = -1;
    for (int i = 0; i < kMaxImageSlots; i++) {
        if (!images_[i].used) {
            slot_id = i;
            break;
        }
    }
    if (slot_id < 0) {
        printf("loadImage: no free slot (max %d)\n", kMaxImageSlots);
        return -1;
    }

    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));

    FIL file;
    FRESULT fr = f_open(&file, norm, FA_READ);
    if (fr != FR_OK) {
        printf("loadImage: open failed %s (%s)\n", norm, FRESULT_str(fr));
        return -1;
    }

    const FSIZE_t fsize = f_size(&file);
    if (fsize < byte_size) {
        printf("loadImage: file too small (%lu < %u)\n", static_cast<unsigned long>(fsize),
               static_cast<unsigned>(byte_size));
        f_close(&file);
        return -1;
    }

    void* alloc_ptr = nullptr;
    if (!HeapBudget::tryAlloc(byte_size, &alloc_ptr)) {
        printf("loadImage: heap budget exceeded (%u bytes)\n", static_cast<unsigned>(byte_size));
        f_close(&file);
        return -1;
    }
    auto* pixels = static_cast<uint16_t*>(alloc_ptr);

    UINT br = 0;
    fr = f_read(&file, pixels, static_cast<UINT>(byte_size), &br);
    f_close(&file);
    if (fr != FR_OK || br != static_cast<UINT>(byte_size)) {
        printf("loadImage: read failed %s\n", path);
        HeapBudget::release(pixels, byte_size);
        return -1;
    }

    images_[slot_id].pixels = pixels;
    images_[slot_id].byte_size = byte_size;
    images_[slot_id].width = w;
    images_[slot_id].height = h;
    images_[slot_id].used = true;
    printf("loadImage: [%d] %s %ux%u OK\n", slot_id, path, w, h);
    return slot_id;
}

/** フレーム末: FIL は跨ぎ維持。prefetch だけ破棄。BW パックは次フレーム先読み */
void LuaInterpreter::closeBgStream() {
    const bool bw_pack_active = bg_stream_.bw_mode && bg_stream_.bw_pack_count > 0;
    bg_stream_.prefetch.valid = false;
    bg_stream_.prefetch.display_band = -1;
    if (bg_stream_.bw_mode) {
        if (bw_pack_active && bg_stream_.open && bg_stream_.bw_pack_frame > 0) {
            if (bwPackBitHasDisplayFrame(bg_stream_.bw_pack_frame) ||
                bwPackBitDisplayPixels() != nullptr) {
                bwPackBitPrefetchNextFrame(
                    &bg_stream_.file, bg_stream_.bw_pack_frame, bg_stream_.bw_pack_count,
                    bg_stream_.bw_pack_data_base, bg_stream_.width, bg_stream_.height,
                    &bg_stream_.bw_buffer_frame);
            } else if (bwPackRgbIsReady()) {
                bwPackRgbPrefetchNextFrame(
                    &bg_stream_.file, bg_stream_.bw_pack_frame, bg_stream_.bw_pack_count,
                    bg_stream_.bw_pack_data_base, bg_stream_.width, bg_stream_.height,
                    bg_stream_.bw_fg, bg_stream_.bw_bg, &bg_stream_.bw_buffer_frame);
            }
        }
        return;
    }
    // 通常 RGB ストリーム: FIL / path は次フレームのために維持する
}

/** 背景ストリーム状態を完全リセット（ゲーム終了・アセット解放時） */
void LuaInterpreter::resetBgStream() {
    if (bg_stream_.open) {
        f_close(&bg_stream_.file);
        bg_stream_.open = false;
    }
    bwPackRgbBufRelease(g_session_teardown);
    bwPackBitRelease(g_session_teardown);
    bg_stream_.path[0] = '\0';
    bg_stream_.fail_path[0] = '\0';
    bg_stream_.width = 0;
    bg_stream_.height = 0;
    bg_stream_.bw_mode = false;
    bg_stream_.bw_fg = 0xFFFF;
    bg_stream_.bw_bg = 0x0000;
    bg_stream_.bw_pack_frame = 0;
    bg_stream_.bw_pack_count = 0;
    bg_stream_.bw_pack_data_base = 0;
    bg_stream_.bw_buffer_frame = 0;
    bg_stream_.dx = 0;
    bg_stream_.dy = 0;
    bg_stream_.prefetch.valid = false;
    bg_stream_.prefetch.display_band = -1;
    bg_stream_.bw_rgb_fast_bands = false;
}

/** draw_bw_pack RGB 全画面: 現在バンドへキャッシュ済み RGB565 を転写する */
bool LuaInterpreter::drawBwPackBlitCurrentBand() {
    if (!bg_stream_.bw_rgb_fast_bands || !bwPackRgbIsReady() || bg_stream_.bw_pack_frame <= 0) {
        return false;
    }
    GameDisplay* disp = hooks_.display;
    if (!disp) {
        return false;
    }

    const int band_index = disp->bandIndex();
    int draw_top = 0;
    int rows = 0;
    int src_y0 = 0;
    if (!bgStreamBandRegion(band_index, bg_stream_.dx, bg_stream_.dy, bg_stream_.width,
                            bg_stream_.height, &draw_top, &rows, &src_y0)) {
        return true;
    }

    const uint16_t* rgb = bwPackRgbDisplayPixels(bg_stream_.width, bg_stream_.height);
    if (!rgb) {
        return false;
    }
    disp->drawImageSub(bg_stream_.dx, draw_top, static_cast<int>(bg_stream_.width),
                       static_cast<int>(bg_stream_.height), rgb, 0, src_y0,
                       static_cast<int>(bg_stream_.width), rows);
    return true;
}

/** 次バンド用に背景ストリームの行データを先読みしてバッファに載せる */
void LuaInterpreter::prefetchBgStreamBand(int display_band) {
    bg_stream_.prefetch.valid = false;
    if (bg_stream_.bw_mode && bg_stream_.bw_pack_count > 0) {
        if (bwPackRgbIsReady()) {
            return;
        }
        if (display_band < 0 || !bwFrameIsValid()) {
            return;
        }

        int draw_top = 0;
        int rows = 0;
        int src_y0 = 0;
        if (!bgStreamBandRegion(display_band, bg_stream_.dx, bg_stream_.dy, bg_stream_.width,
                                bg_stream_.height, &draw_top, &rows, &src_y0)) {
            return;
        }

        uint8_t* frame = bwFrameBuf();
        if (!frame) {
            return;
        }

        const uint8_t slot = static_cast<uint8_t>(display_band & 1);
        expandBwBufferChunk(frame, bg_stream_.width, src_y0, rows, g_bg_stream_buf[slot],
                            bg_stream_.bw_fg, bg_stream_.bw_bg);
        bg_stream_.prefetch.valid = true;
        bg_stream_.prefetch.display_band = display_band;
        bg_stream_.prefetch.draw_top = draw_top;
        bg_stream_.prefetch.rows = rows;
        bg_stream_.prefetch.src_y0 = src_y0;
        bg_stream_.prefetch.buf_slot = slot;
        return;
    }
    if (!bg_stream_.open || display_band < 0) {
        return;
    }

    int draw_top = 0;
    int rows = 0;
    int src_y0 = 0;
    if (!bgStreamBandRegion(display_band, bg_stream_.dx, bg_stream_.dy, bg_stream_.width,
                            bg_stream_.height, &draw_top, &rows, &src_y0)) {
        return;
    }

    const uint8_t slot = static_cast<uint8_t>(display_band & 1);
    if (bg_stream_.bw_mode && bwFrameIsValid()) {
        uint8_t* frame = bwFrameBuf();
        if (frame) {
            expandBwBufferChunk(frame, bg_stream_.width, src_y0, rows, g_bg_stream_buf[slot],
                                bg_stream_.bw_fg, bg_stream_.bw_bg);
        }
    } else if (bg_stream_.bw_mode) {
        return;
    } else {
        const bool ok = readBgStreamChunk(&bg_stream_.file, bg_stream_.width, src_y0, rows,
                                          g_bg_stream_buf[slot]);
        if (!ok) {
            return;
        }
    }

    bg_stream_.prefetch.valid = true;
    bg_stream_.prefetch.display_band = display_band;
    bg_stream_.prefetch.draw_top = draw_top;
    bg_stream_.prefetch.rows = rows;
    bg_stream_.prefetch.src_y0 = src_y0;
    bg_stream_.prefetch.buf_slot = slot;
}

/** SD から現在バンド分だけ背景画像を読み込み、画面に描画する */
bool LuaInterpreter::drawBgStreamFromSd(const char* path, int dx, int dy, uint16_t w, uint16_t h) {
    if (!path || path[0] == '\0' || w == 0 || h == 0) {
        return false;
    }
    if (!sd_mounted_) {
        return false;
    }
    if (draw_cmds_.isRecording()) {
        draw_cmds_.recBgStream(path, dx, dy, w, h);
        return true;
    }
    GameDisplay* disp = hooks_.display;
    if (!disp) {
        return false;
    }

    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));

    if (bg_stream_.fail_path[0] != '\0' && std::strcmp(bg_stream_.fail_path, norm) == 0) {
        return false;
    }

    const int band_index = disp->bandIndex();
    int draw_top = 0;
    int rows = 0;
    int src_y0 = 0;
    if (!bgStreamBandRegion(band_index, dx, dy, w, h, &draw_top, &rows, &src_y0)) {
        return true;
    }

    const size_t chunk = static_cast<size_t>(w) * 2u * static_cast<size_t>(rows);
    if (chunk > sizeof(g_bg_stream_buf[0])) {
        printf("drawBgStream: band chunk too large (%u)\n", static_cast<unsigned>(chunk));
        return false;
    }

    const bool path_changed =
        !bg_stream_.open || std::strcmp(bg_stream_.path, norm) != 0 || bg_stream_.width != w ||
        bg_stream_.height != h || bg_stream_.bw_mode;
    if (path_changed) {
        resetBgStream();
        const size_t byte_size = static_cast<size_t>(w) * static_cast<size_t>(h) * 2u;
        const FRESULT fr = f_open(&bg_stream_.file, norm, FA_READ);
        if (fr != FR_OK) {
            printf("drawBgStream: open failed %s (%s)\n", norm, FRESULT_str(fr));
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        if (f_size(&bg_stream_.file) < byte_size) {
            printf("drawBgStream: file too small %s (%lu < %u)\n", norm,
                   static_cast<unsigned long>(f_size(&bg_stream_.file)),
                   static_cast<unsigned>(byte_size));
            f_close(&bg_stream_.file);
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        bg_stream_.open = true;
        bg_stream_.bw_mode = false;
        std::strncpy(bg_stream_.path, norm, sizeof(bg_stream_.path) - 1);
        bg_stream_.path[sizeof(bg_stream_.path) - 1] = '\0';
        bg_stream_.width = w;
        bg_stream_.height = h;
        printf("drawBgStream: open %s %ux%u\n", norm, w, h);
    }

    bg_stream_.dx = dx;
    bg_stream_.dy = dy;

    uint8_t use_slot = static_cast<uint8_t>(band_index & 1);
    const auto& pf = bg_stream_.prefetch;
    if (pf.valid && pf.display_band == band_index && pf.draw_top == draw_top && pf.rows == rows &&
        pf.src_y0 == src_y0) {
        use_slot = pf.buf_slot;
    } else if (!readBgStreamChunk(&bg_stream_.file, w, src_y0, rows, g_bg_stream_buf[use_slot])) {
        printf("drawBgStream: read failed %s\n", norm);
        return false;
    }

    disp->drawImageSub(dx, draw_top, static_cast<int>(w), rows, g_bg_stream_buf[use_slot], 0, 0,
                       static_cast<int>(w), rows);
    bg_stream_.prefetch.valid = false;
    return true;
}

/** SD から 1 ビット白黒フレームを読み込み、現在バンド分を画面に描画する */
bool LuaInterpreter::drawBwStreamFromSd(const char* path, int dx, int dy, uint16_t w, uint16_t h,
                                        uint16_t fg, uint16_t bg) {
    if (!path || path[0] == '\0' || w == 0 || h == 0) {
        return false;
    }
    if (!sd_mounted_) {
        return false;
    }
    if (draw_cmds_.isRecording()) {
        draw_cmds_.recBwStream(path, dx, dy, w, h, fg, bg);
        return true;
    }
    GameDisplay* disp = hooks_.display;
    if (!disp) {
        return false;
    }

    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));

    if (bg_stream_.fail_path[0] != '\0' && std::strcmp(bg_stream_.fail_path, norm) == 0) {
        return false;
    }

    const int band_index = disp->bandIndex();
    int draw_top = 0;
    int rows = 0;
    int src_y0 = 0;
    if (!bgStreamBandRegion(band_index, dx, dy, w, h, &draw_top, &rows, &src_y0)) {
        return true;
    }

    const size_t chunk = static_cast<size_t>(w) * static_cast<size_t>(rows);
    if (chunk > sizeof(g_bg_stream_buf[0]) / sizeof(g_bg_stream_buf[0][0])) {
        printf("drawBwStream: band chunk too large (%u)\n", static_cast<unsigned>(chunk));
        return false;
    }

    const bool frame_changed = std::strcmp(bg_stream_.path, norm) != 0 || bg_stream_.width != w ||
                               bg_stream_.height != h || !bg_stream_.bw_mode ||
                               bg_stream_.bw_fg != fg || bg_stream_.bw_bg != bg;
    if (frame_changed) {
        if (bg_stream_.open && bg_stream_.bw_pack_count > 0) {
            f_close(&bg_stream_.file);
            bg_stream_.open = false;
        }
        if (!bwFrameBufEnsure(w, h)) {
            printf("drawBwStream: frame buffer alloc failed %ux%u\n", w, h);
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        uint8_t* frame = bwFrameBuf();
        if (!frame) {
            return false;
        }
        FIL file{};
        const FRESULT fr = f_open(&file, norm, FA_READ);
        if (fr != FR_OK) {
            printf("drawBwStream: open failed %s (%s)\n", norm, FRESULT_str(fr));
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        const FSIZE_t file_size = f_size(&file);
        const size_t frame_bytes = bwFrameByteSize(w, h);
        if (file_size > frame_bytes) {
            printf("drawBwStream: file too large %s (%lu > %u)\n", norm,
                   static_cast<unsigned long>(file_size), static_cast<unsigned>(frame_bytes));
            f_close(&file);
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        if (!loadBwFrameFromSd(&file, file_size, frame, w, h)) {
            printf("drawBwStream: load failed %s (%lu bytes)\n", norm,
                   static_cast<unsigned long>(file_size));
            f_close(&file);
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        f_close(&file);
        bg_stream_.open = false;
        bg_stream_.bw_mode = true;
        bg_stream_.bw_fg = fg;
        bg_stream_.bw_bg = bg;
        bg_stream_.bw_pack_frame = 0;
        bg_stream_.bw_pack_count = 0;
        bg_stream_.bw_pack_data_base = 0;
        std::strncpy(bg_stream_.path, norm, sizeof(bg_stream_.path) - 1);
        bg_stream_.path[sizeof(bg_stream_.path) - 1] = '\0';
        bg_stream_.width = w;
        bg_stream_.height = h;
        bg_stream_.fail_path[0] = '\0';
        if (file_size == 0) {
            printf("drawBwStream: skip %s\n", norm);
        } else if (file_size == frame_bytes) {
            printf("drawBwStream: full %s (%u bytes)\n", norm, static_cast<unsigned>(file_size));
        } else {
            printf("drawBwStream: delta %s (%lu bytes)\n", norm,
                   static_cast<unsigned long>(file_size));
        }
    }

    if (!bwFrameIsValid()) {
        return false;
    }

    bg_stream_.dx = dx;
    bg_stream_.dy = dy;

    uint8_t* frame = bwFrameBuf();
    if (!frame) {
        return false;
    }

    uint8_t use_slot = static_cast<uint8_t>(band_index & 1);
    const auto& pf = bg_stream_.prefetch;
    if (pf.valid && pf.display_band == band_index && pf.draw_top == draw_top && pf.rows == rows &&
        pf.src_y0 == src_y0) {
        use_slot = pf.buf_slot;
    } else {
        expandBwBufferChunk(frame, w, src_y0, rows, g_bg_stream_buf[use_slot], fg, bg);
    }

    disp->drawImageSub(dx, draw_top, static_cast<int>(w), rows, g_bg_stream_buf[use_slot], 0, 0,
                       static_cast<int>(w), rows);
    bg_stream_.prefetch.valid = false;
    return true;
}

/** スクリプトパスが bad_apple 専用プレイヤー対象か判定する */
bool LuaInterpreter::isBadAppleScriptPath(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }
    if (!std::strstr(path, "bad_apple")) {
        return false;
    }
    const char* name = std::strrchr(path, '/');
    name = name ? name + 1 : path;
    return std::strcmp(name, "bad_apple.lua") == 0;
}

/** bad_apple: game_init 後に FRAMES_PACK パスを解決して保持する */
void LuaInterpreter::initBadApplePlayerFromLua() {
    bad_apple_player_ = true;
    bad_apple_fast_active_ = true;
    bad_apple_pack_path_[0] = '\0';

    if (!game_lua_) {
        return;
    }

    const char* rel = "frames.pack";
    lua_getglobal(game_lua_, "FRAMES_PACK");
    if (lua_isstring(game_lua_, -1)) {
        const char* pack = lua_tostring(game_lua_, -1);
        if (pack && pack[0] != '\0') {
            rel = pack;
        }
    }
    lua_pop(game_lua_, 1);

    resolveGamePath(rel, bad_apple_pack_path_, sizeof(bad_apple_pack_path_));

    bad_apple_frame_idx_ = 1;
    bad_apple_frame_acc_ = 0;
    bad_apple_frame_ms_ = 33;
    bad_apple_frame_count_ = 6572;
    bad_apple_last_drawn_frame_ = 0;
    bad_apple_ready_ = true;
    bad_apple_missing_ = false;

    lua_getglobal(game_lua_, "__bad_apple_ready");
    if (lua_isboolean(game_lua_, -1)) {
        bad_apple_ready_ = lua_toboolean(game_lua_, -1);
    }
    lua_pop(game_lua_, 1);

    lua_getglobal(game_lua_, "__bad_apple_missing");
    if (lua_isboolean(game_lua_, -1)) {
        bad_apple_missing_ = lua_toboolean(game_lua_, -1);
    }
    lua_pop(game_lua_, 1);

    lua_getglobal(game_lua_, "__bad_apple_frame_ms");
    if (lua_isinteger(game_lua_, -1)) {
        const lua_Integer ms = lua_tointeger(game_lua_, -1);
        if (ms > 0 && ms <= 1000) {
            bad_apple_frame_ms_ = static_cast<int>(ms);
        }
    }
    lua_pop(game_lua_, 1);

    lua_getglobal(game_lua_, "__bad_apple_frame_count");
    if (lua_isinteger(game_lua_, -1)) {
        const lua_Integer count = lua_tointeger(game_lua_, -1);
        if (count > 0) {
            bad_apple_frame_count_ = static_cast<uint32_t>(count);
        }
    }
    lua_pop(game_lua_, 1);

    printf("bad_apple: fast player enabled (%s, %dms, %u frames)\n", bad_apple_pack_path_,
           bad_apple_frame_ms_, static_cast<unsigned>(bad_apple_frame_count_));
}

namespace {

bool badAppleExitPressed(const LuaHostHooks& hooks) {
    if (!hooks.is_button_pressed) {
        return false;
    }
    static const int kJumpButtons[] = {1, 5, 0, 3, 7};
    for (int btn : kJumpButtons) {
        if (hooks.is_button_pressed(hooks.user_data, btn)) {
            return true;
        }
    }
    if (hooks.is_button_pressed(hooks.user_data, 4)) {
        return true;
    }
    if (hooks.is_button_pressed(hooks.user_data, 2)) {
        return true;
    }
    return false;
}

}  // namespace

/** bad_apple 専用: Lua game_update を C 側で処理（true=継続, false=終了） */
bool LuaInterpreter::runBadAppleUpdate(int dt_ms) {
    if (badAppleExitPressed(hooks_)) {
        return false;
    }
    if (!bad_apple_ready_) {
        return true;
    }

    bad_apple_frame_acc_ += dt_ms;
    if (bad_apple_frame_acc_ >= bad_apple_frame_ms_) {
        bad_apple_frame_acc_ -= bad_apple_frame_ms_;
        bad_apple_frame_idx_++;
        if (static_cast<uint32_t>(bad_apple_frame_idx_) > bad_apple_frame_count_) {
            bad_apple_frame_idx_ = 1;
        }
    }
    return true;
}

/** bad_apple: BWPK を RGB565 キャッシュまで準備（帯 blit は行わない） */
bool LuaInterpreter::ensureBwPackRgbFrameReady(const char* path, int frame_index, uint16_t w,
                                             uint16_t h, uint16_t fg, uint16_t bg,
                                             const uint16_t** out_pixels) {
    if (out_pixels) {
        *out_pixels = nullptr;
    }
    if (!path || path[0] == '\0' || frame_index <= 0 || w == 0 || h == 0 || !sd_mounted_) {
        return false;
    }

    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));

    if (bg_stream_.fail_path[0] != '\0' && std::strcmp(bg_stream_.fail_path, norm) == 0) {
        return false;
    }

    const bool pack_changed = std::strcmp(bg_stream_.path, norm) != 0 || bg_stream_.width != w ||
                              bg_stream_.height != h || !bg_stream_.bw_mode ||
                              bg_stream_.bw_fg != fg || bg_stream_.bw_bg != bg ||
                              bg_stream_.bw_pack_count == 0;

    if (pack_changed) {
        if (bg_stream_.open) {
            f_close(&bg_stream_.file);
            bg_stream_.open = false;
        }

        const FRESULT fr = f_open(&bg_stream_.file, norm, FA_READ);
        if (fr != FR_OK) {
            printf("drawBwPack: open failed %s (%s)\n", norm, FRESULT_str(fr));
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        bg_stream_.open = true;

        uint32_t frame_count = 0;
        uint32_t data_base = 0;
        if (!bwPackReadHeader(&bg_stream_.file, &frame_count, &data_base)) {
            printf("drawBwPack: invalid header %s\n", norm);
            f_close(&bg_stream_.file);
            bg_stream_.open = false;
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }

        bg_stream_.bw_mode = true;
        bg_stream_.bw_fg = fg;
        bg_stream_.bw_bg = bg;
        bg_stream_.bw_pack_count = frame_count;
        bg_stream_.bw_pack_data_base = data_base;
        bg_stream_.bw_buffer_frame = 0;
        bg_stream_.bw_pack_frame = 0;
        std::strncpy(bg_stream_.path, norm, sizeof(bg_stream_.path) - 1);
        bg_stream_.path[sizeof(bg_stream_.path) - 1] = '\0';
        bg_stream_.width = w;
        bg_stream_.height = h;
        bg_stream_.fail_path[0] = '\0';
        bg_stream_.bw_rgb_fast_bands = false;

        if (game_lua_) {
            trimLargeLuaGlobals(game_lua_);
            lua_gc(game_lua_, LUA_GCCOLLECT, 0);
        }
        bwPackRgbBufResetPipeline();
        if (!bwPackRgbBufEnsure(w, h)) {
            printf("drawBwPack: RGB dbuf unavailable, band fallback\n");
        }
    } else if (!bg_stream_.open) {
        const FRESULT fr = f_open(&bg_stream_.file, norm, FA_READ);
        if (fr != FR_OK) {
            printf("drawBwPack: reopen failed %s (%s)\n", norm, FRESULT_str(fr));
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        bg_stream_.open = true;
    }

    if (static_cast<uint32_t>(frame_index) > bg_stream_.bw_pack_count) {
        printf("drawBwPack: frame %d out of range (%u)\n", frame_index,
               static_cast<unsigned>(bg_stream_.bw_pack_count));
        std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
        bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
        return false;
    }

    if (!bwPackRgbIsReady()) {
        return false;
    }

    if (!bwPackRgbHasDisplayFrame(frame_index)) {
        if (!bwPackRgbTrySwapToFrame(frame_index)) {
            if (!bwPackRgbLoadDisplayFrame(&bg_stream_.file, frame_index, bg_stream_.bw_pack_count,
                                           bg_stream_.bw_pack_data_base, w, h, fg, bg,
                                           &bg_stream_.bw_buffer_frame)) {
                printf("drawBwPack: RGB load failed %s frame %d\n", norm, frame_index);
                std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
                bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
                return false;
            }
        }
    }

    bg_stream_.bw_pack_frame = frame_index;
    bg_stream_.fail_path[0] = '\0';
    bg_stream_.dx = 0;
    bg_stream_.dy = 0;

    const uint16_t* rgb = bwPackRgbDisplayPixels(w, h);
    if (!rgb) {
        return false;
    }
    if (out_pixels) {
        *out_pixels = rgb;
    }
    return true;
}

/** bad_apple: pack ファイルを開き、表示用 1bit フレームを同期する */
bool LuaInterpreter::ensureBadAppleBitFrameReady(int frame_index) {
    if (frame_index <= 0 || bad_apple_pack_path_[0] == '\0' || !sd_mounted_) {
        return false;
    }

    const uint16_t w = GameConfig::SCREEN_WIDTH;
    const uint16_t h = GameConfig::SCREEN_HEIGHT;
    const uint16_t fg = 0xFFFF;
    const uint16_t bg = 0x0000;
    const char* norm = bad_apple_pack_path_;

    if (bg_stream_.fail_path[0] != '\0' && std::strcmp(bg_stream_.fail_path, norm) == 0) {
        return false;
    }

    const bool pack_changed = std::strcmp(bg_stream_.path, norm) != 0 || bg_stream_.width != w ||
                              bg_stream_.height != h || !bg_stream_.bw_mode ||
                              bg_stream_.bw_pack_count == 0;

    if (pack_changed) {
        if (bg_stream_.open) {
            f_close(&bg_stream_.file);
            bg_stream_.open = false;
        }
        const FRESULT fr = f_open(&bg_stream_.file, norm, FA_READ);
        if (fr != FR_OK) {
            printf("bad_apple: open failed %s (%s)\n", norm, FRESULT_str(fr));
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        bg_stream_.open = true;

        uint32_t frame_count = 0;
        uint32_t data_base = 0;
        if (!bwPackReadHeader(&bg_stream_.file, &frame_count, &data_base)) {
            printf("bad_apple: invalid header %s\n", norm);
            f_close(&bg_stream_.file);
            bg_stream_.open = false;
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }

        bg_stream_.bw_mode = true;
        bg_stream_.bw_fg = fg;
        bg_stream_.bw_bg = bg;
        bg_stream_.bw_pack_count = frame_count;
        bg_stream_.bw_pack_data_base = data_base;
        bg_stream_.bw_buffer_frame = 0;
        bg_stream_.bw_pack_frame = 0;
        std::strncpy(bg_stream_.path, norm, sizeof(bg_stream_.path) - 1);
        bg_stream_.path[sizeof(bg_stream_.path) - 1] = '\0';
        bg_stream_.width = w;
        bg_stream_.height = h;
        bg_stream_.fail_path[0] = '\0';
        bg_stream_.bw_rgb_fast_bands = false;

        if (game_lua_) {
            trimLargeLuaGlobals(game_lua_);
            lua_gc(game_lua_, LUA_GCCOLLECT, 0);
        }
        // 全画面 RGB は使わず、1bit 二重バッファ + 帯展開で DMA と重ねる
        bwPackRgbBufRelease(false);
        if (!bwPackBitEnsure(w, h)) {
            printf("bad_apple: bit dual buffer unavailable\n");
            return false;
        }
    } else if (!bg_stream_.open) {
        const FRESULT fr = f_open(&bg_stream_.file, norm, FA_READ);
        if (fr != FR_OK) {
            printf("bad_apple: reopen failed %s (%s)\n", norm, FRESULT_str(fr));
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        bg_stream_.open = true;
    }

    if (static_cast<uint32_t>(frame_index) > bg_stream_.bw_pack_count) {
        return false;
    }

    if (!bwPackBitSyncDisplayFrame(&bg_stream_.file, frame_index, bg_stream_.bw_pack_count,
                                   bg_stream_.bw_pack_data_base, w, h,
                                   &bg_stream_.bw_buffer_frame)) {
        printf("bad_apple: bit sync failed frame %d\n", frame_index);
        return false;
    }

    bg_stream_.bw_pack_frame = frame_index;
    bg_stream_.fail_path[0] = '\0';
    return true;
}

namespace {

void badApplePumpDisplay(void* user) {
    auto* disp = static_cast<GameDisplay*>(user);
    if (disp) {
        disp->pumpDma();
    }
}

}  // namespace

/** bad_apple: 1bit→帯展開→DMA を重ね、裏バッファへ次フレームを SD 先読み */
bool LuaInterpreter::runBadAppleDrawFrame() {
    GameDisplay* disp = hooks_.display;
    if (!disp || bad_apple_pack_path_[0] == '\0') {
        bad_apple_skip_prefetch_ = false;
        return false;
    }

    bad_apple_skip_prefetch_ = false;

    if (bad_apple_missing_ || !bad_apple_ready_) {
        disp->fillScreen(Color::BLACK);
        if (bad_apple_missing_) {
            disp->beginBand(0);
            disp->drawTextBg(8, 100, "NO FRAMES", Color::rgb(255, 80, 80), Color::BLACK);
            disp->drawTextBg(8, 116, "Run convert_video.py", Color::rgb(200, 200, 200),
                             Color::BLACK);
            disp->endBand();
            disp->waitForTransferComplete();
        }
        bad_apple_last_drawn_frame_ = 0;
        return true;
    }

    const int frame_idx = bad_apple_frame_idx_;
    if (frame_idx == bad_apple_last_drawn_frame_ && bwPackBitHasDisplayFrame(frame_idx)) {
        bad_apple_skip_prefetch_ = true;
        return true;
    }

    if (!ensureBadAppleBitFrameReady(frame_idx)) {
        return false;
    }

    const uint8_t* bit = bwPackBitDisplayPixels();
    if (!bit) {
        return false;
    }

    const uint16_t w = GameConfig::SCREEN_WIDTH;
    const uint16_t fg = 0xFFFF;
    const uint16_t bg = 0x0000;
    const int bands = disp->bandCount();

    // 帯展開と前バンド DMA を重ねる（SPI0）。SD 先読みは全バンドキック後に SPI1 で重ねる
    for (int band = 0; band < bands; ++band) {
        disp->beginBand(band);
        const int y0 = disp->bandTopY();
        const int rows = disp->bandBottomY() - y0;
        expandBwBufferChunk(bit, w, y0, rows, disp->framebuffer(), fg, bg);
        disp->pumpDma();
        disp->endBand();
        disp->pumpDma();
    }

    // 最終バンド転送中に次 1bit フレームを SD から裏バッファへ
    bwPackBitPrefetchNextFramePumped(
        &bg_stream_.file, frame_idx, bg_stream_.bw_pack_count, bg_stream_.bw_pack_data_base, w,
        GameConfig::SCREEN_HEIGHT, &bg_stream_.bw_buffer_frame, badApplePumpDisplay, disp);

    disp->waitForTransferComplete();

    bad_apple_last_drawn_frame_ = frame_idx;
    return true;
}

/** SD 上の BWPK から指定フレームを読み込み、現在バンド分を画面に描画する */
bool LuaInterpreter::drawBwPackFromSd(const char* path, int frame_index, int dx, int dy,
                                      uint16_t w, uint16_t h, uint16_t fg, uint16_t bg) {
    if (!path || path[0] == '\0' || frame_index <= 0 || w == 0 || h == 0) {
        return false;
    }
    if (!sd_mounted_) {
        return false;
    }
    if (draw_cmds_.isRecording()) {
        draw_cmds_.recBwPack(path, frame_index, dx, dy, w, h, fg, bg);
        return true;
    }
    GameDisplay* disp = hooks_.display;
    if (!disp) {
        return false;
    }

    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));

    if (bg_stream_.fail_path[0] != '\0' && std::strcmp(bg_stream_.fail_path, norm) == 0) {
        return false;
    }

    const int band_index = disp->bandIndex();
    int draw_top = 0;
    int rows = 0;
    int src_y0 = 0;
    if (!bgStreamBandRegion(band_index, dx, dy, w, h, &draw_top, &rows, &src_y0)) {
        return true;
    }

    const size_t chunk = static_cast<size_t>(w) * static_cast<size_t>(rows);
    if (chunk > sizeof(g_bg_stream_buf[0]) / sizeof(g_bg_stream_buf[0][0])) {
        printf("drawBwPack: band chunk too large (%u)\n", static_cast<unsigned>(chunk));
        return false;
    }

    const uint16_t* rgb = nullptr;
    if (ensureBwPackRgbFrameReady(path, frame_index, w, h, fg, bg, &rgb)) {
        disp->drawImageSub(dx, draw_top, static_cast<int>(w), static_cast<int>(h), rgb, 0, src_y0,
                           static_cast<int>(w), rows);
        if (dx == 0 && dy == 0 && w == GameConfig::SCREEN_WIDTH && h == GameConfig::SCREEN_HEIGHT) {
            bg_stream_.bw_rgb_fast_bands = true;
        }
        return true;
    }

    if (!bg_stream_.open || std::strcmp(bg_stream_.path, norm) != 0 || !bg_stream_.bw_mode ||
        bg_stream_.width != w || bg_stream_.height != h || bg_stream_.bw_pack_count == 0) {
        if (bg_stream_.open) {
            f_close(&bg_stream_.file);
            bg_stream_.open = false;
        }

        const FRESULT fr = f_open(&bg_stream_.file, norm, FA_READ);
        if (fr != FR_OK) {
            printf("drawBwPack: open failed %s (%s)\n", norm, FRESULT_str(fr));
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        bg_stream_.open = true;

        uint32_t frame_count = 0;
        uint32_t data_base = 0;
        if (!bwPackReadHeader(&bg_stream_.file, &frame_count, &data_base)) {
            printf("drawBwPack: invalid header %s\n", norm);
            f_close(&bg_stream_.file);
            bg_stream_.open = false;
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }

        bg_stream_.bw_mode = true;
        bg_stream_.bw_fg = fg;
        bg_stream_.bw_bg = bg;
        bg_stream_.bw_pack_count = frame_count;
        bg_stream_.bw_pack_data_base = data_base;
        bg_stream_.bw_buffer_frame = 0;
        bg_stream_.bw_pack_frame = 0;
        std::strncpy(bg_stream_.path, norm, sizeof(bg_stream_.path) - 1);
        bg_stream_.path[sizeof(bg_stream_.path) - 1] = '\0';
        bg_stream_.width = w;
        bg_stream_.height = h;
        bg_stream_.fail_path[0] = '\0';
        bg_stream_.bw_rgb_fast_bands = false;
    } else if (!bg_stream_.open) {
        const FRESULT fr = f_open(&bg_stream_.file, norm, FA_READ);
        if (fr != FR_OK) {
            printf("drawBwPack: reopen failed %s (%s)\n", norm, FRESULT_str(fr));
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        bg_stream_.open = true;
    }

    if (static_cast<uint32_t>(frame_index) > bg_stream_.bw_pack_count) {
        printf("drawBwPack: frame %d out of range (%u)\n", frame_index,
               static_cast<unsigned>(bg_stream_.bw_pack_count));
        std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
        bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
        return false;
    }

    const bool frame_changed = bg_stream_.bw_pack_frame != frame_index;
    if (frame_changed) {
        if (!bwFrameBufEnsure(w, h)) {
            printf("drawBwPack: frame buffer alloc failed %ux%u\n", w, h);
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        uint8_t* frame = bwFrameBuf();
        if (!frame) {
            return false;
        }
        if (!syncBwPackToFrame(&bg_stream_.file, static_cast<uint32_t>(frame_index),
                               bg_stream_.bw_pack_count, bg_stream_.bw_pack_data_base, frame, w,
                               h, &bg_stream_.bw_buffer_frame)) {
            printf("drawBwPack: load failed %s frame %d\n", norm, frame_index);
            std::strncpy(bg_stream_.fail_path, norm, sizeof(bg_stream_.fail_path) - 1);
            bg_stream_.fail_path[sizeof(bg_stream_.fail_path) - 1] = '\0';
            return false;
        }
        bg_stream_.bw_pack_frame = frame_index;
        bg_stream_.fail_path[0] = '\0';
    }

    if (!bwFrameIsValid()) {
        return false;
    }

    bg_stream_.dx = dx;
    bg_stream_.dy = dy;

    uint8_t* frame = bwFrameBuf();
    if (!frame) {
        return false;
    }

    uint8_t use_slot = static_cast<uint8_t>(band_index & 1);
    const auto& pf = bg_stream_.prefetch;
    if (pf.valid && pf.display_band == band_index && pf.draw_top == draw_top && pf.rows == rows &&
        pf.src_y0 == src_y0) {
        use_slot = pf.buf_slot;
    } else {
        expandBwBufferChunk(frame, w, src_y0, rows, g_bg_stream_buf[use_slot], fg, bg);
    }

    disp->drawImageSub(dx, draw_top, static_cast<int>(w), rows, g_bg_stream_buf[use_slot], 0, 0,
                       static_cast<int>(w), rows);
    bg_stream_.prefetch.valid = false;
    return true;
}

/** 画像スロット ID に対応する ImageSlot を返す。無効なら nullptr */
const ImageSlot* LuaInterpreter::getImage(int id) const {
    if (id < 0 || id >= kMaxImageSlots) {
        return nullptr;
    }
    if (!images_[id].used) {
        return nullptr;
    }
    return &images_[id];
}

/** 指定 ID の画像スロットを解放する */
void LuaInterpreter::freeImage(int id) {
    if (id < 0 || id >= kMaxImageSlots) {
        return;
    }
    if (!images_[id].used) {
        return;
    }
    if (images_[id].pixels) {
        if (g_session_teardown) {
            free(images_[id].pixels);
        } else {
            HeapBudget::release(images_[id].pixels, images_[id].byte_size);
        }
    }
    images_[id] = ImageSlot();
}

/** すべての画像スロットを解放する */
void LuaInterpreter::freeAllImages() {
    for (int i = 0; i < kMaxImageSlots; i++) {
        if (images_[i].used) {
            printf("[MENU-DBG] freeImage slot=%d bytes=%u\n", i,
                   static_cast<unsigned>(images_[i].byte_size));
            fflush(stdout);
        }
        freeImage(i);
    }
}

/** LCD 描画・ボタン入力用のホストコールバックを登録する */
void LuaInterpreter::setHostHooks(const LuaHostHooks& hooks) { hooks_ = hooks; }

/** AudioOutput を接続し、Lua 音声 API のストリーミングを開始する */
void LuaInterpreter::setAudioOutput(AudioOutput* audio) {
    audio_engine_.attach(audio, AudioConfig::SAMPLE_RATE);
    if (audio) {
        audio->setCallback(LuaAudio::audioCallback);
        audio->setVolume(1.0f);
        audio->start();
    }
}

/** SD マウント状態を interpreter と音声エンジンに伝える */
void LuaInterpreter::setSdMounted(bool mounted) {
    sd_mounted_ = mounted;
    audio_engine_.setSdMounted(mounted);
}

/** 読み込み可能な Lua スクリプトの最大バイト数を設定する */
void LuaInterpreter::setMaxScriptBytes(size_t max_bytes) { max_script_bytes_ = max_bytes; }

/** SD 上に通常ファイルが存在するか（ディレクトリは除外） */
bool LuaInterpreter::sdFileExists(const char* path) const {
    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));
    FILINFO fno;
    if (f_stat(norm, &fno) != FR_OK) {
        return false;
    }
    return !(fno.fattrib & AM_DIR);
}

/** SD 上のファイルサイズ（バイト）を返す。存在しないかディレクトリなら 0 */
size_t LuaInterpreter::sdFileSize(const char* path) const {
    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));
    FILINFO fno;
    if (f_stat(norm, &fno) != FR_OK) {
        return 0;
    }
    if (fno.fattrib & AM_DIR) {
        return 0;
    }
    return static_cast<size_t>(fno.fsize);
}

/** 直近のエラーメッセージを内部バッファに保存する */
void LuaInterpreter::setLastError(const char* msg) {
    if (!msg) {
        last_error_[0] = '\0';
        return;
    }
    strncpy(last_error_, msg, sizeof(last_error_) - 1);
    last_error_[sizeof(last_error_) - 1] = '\0';
}

/** LCD にステータス行を表示し、last_error_ も更新する */
void LuaInterpreter::showStatus(const char* line1, const char* line2, uint16_t color, uint16_t bg) {
    if (line2) {
        setLastError(line2);
    } else if (line1) {
        setLastError(line1);
    }
    if (hooks_.draw_text_bg) {
        if (line1) {
            hooks_.draw_text_bg(hooks_.user_data, 10, 80, line1, color, bg);
        }
        if (line2) {
            hooks_.draw_text_bg(hooks_.user_data, 10, 90, line2, color, bg);
        }
    }
}

/** HeapBudget 連携のカスタムアロケータで新しい Lua VM を生成する */
lua_State* LuaInterpreter::newLuaState() { return lua_newstate(luaHeapAlloc, nullptr); }

/** SD ファイルをヒープに読み込み、バッファと長さを返す */
bool LuaInterpreter::readSdFileToBuffer(const char* path, char** out_buf, size_t* out_len) {
    *out_buf = nullptr;
    *out_len = 0;
    if (!sd_mounted_) {
        return false;
    }

    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));

    FIL file;
    FRESULT fr = f_open(&file, norm, FA_READ);
    if (fr != FR_OK) {
        printf("Lua: open failed %s (%s)\n", norm, FRESULT_str(fr));
        return false;
    }

    const FSIZE_t fsize = f_size(&file);
    if (fsize == 0 || fsize > max_script_bytes_) {
        printf("Lua: invalid size %s (%lu bytes, max %u)\n", norm, static_cast<unsigned long>(fsize),
               static_cast<unsigned>(max_script_bytes_));
        f_close(&file);
        return false;
    }

    const size_t alloc_size = static_cast<size_t>(fsize) + 1;
    void* alloc_ptr = nullptr;
    if (!HeapBudget::tryAlloc(alloc_size, &alloc_ptr)) {
        printf("Lua: heap budget exceeded for %s (%u bytes)\n", norm,
               static_cast<unsigned>(alloc_size));
        f_close(&file);
        return false;
    }
    char* buf = static_cast<char*>(alloc_ptr);

    UINT br = 0;
    fr = f_read(&file, buf, static_cast<UINT>(fsize), &br);
    f_close(&file);
    if (fr != FR_OK || br != static_cast<UINT>(fsize)) {
        printf("Lua: read failed %s (%s)\n", norm, FRESULT_str(fr));
        HeapBudget::release(buf, alloc_size);
        return false;
    }
    buf[fsize] = '\0';
    *out_buf = buf;
    *out_len = static_cast<size_t>(fsize);
    return true;
}

/** バッファ内の Lua ソースをロードして実行する */
bool LuaInterpreter::loadScriptIntoState(lua_State* L, const char* path, char* source, size_t len) {
    auto release_source = [&]() {
        if (source) {
            HeapBudget::release(source, len + 1);
            source = nullptr;
        }
    };

    const int load_stat = luaL_loadbuffer(L, source, len, path);
    release_source();
    if (load_stat != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        printf("Lua load error [%s]: %s\n", path, err ? err : "unknown");
        setLastError(err ? err : "load error");
        return false;
    }
    const int call_stat = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (call_stat != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        printf("Lua runtime error [%s]: %s\n", path, err ? err : "unknown");
        setLastError(err ? err : "runtime error");
        return false;
    }
    return true;
}

/** SD 上の .lua を実行し、return 値 1 個を Lua スタックに push する */
bool LuaInterpreter::pushLoadReturnFromSd(lua_State* L, const char* path) {
    if (!sd_mounted_) {
        setLastError("SD not mounted");
        return false;
    }

    char* source = nullptr;
    size_t len = 0;
    if (!readSdFileToBuffer(path, &source, &len)) {
        setLastError("read failed");
        return false;
    }

    const int load_stat = luaL_loadbuffer(L, source, len, path);
    HeapBudget::release(source, len + 1);
    if (load_stat != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        printf("Lua load error [%s]: %s\n", path, err ? err : "unknown");
        setLastError(err ? err : "load error");
        return false;
    }
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        printf("Lua runtime error [%s]: %s\n", path, err ? err : "unknown");
        setLastError(err ? err : "runtime error");
        return false;
    }
    return true;
}

/** ファイル名が .lua 拡張子で終わるか（大文字小文字無視） */
bool LuaInterpreter::endsWithLuaExt(const char* name) const {
    const size_t len = strlen(name);
    if (len < 4) {
        return false;
    }
    const char* ext = name + len - 4;
    return ext[0] == '.' && tolower(static_cast<unsigned char>(ext[1])) == 'l' &&
           tolower(static_cast<unsigned char>(ext[2])) == 'u' &&
           tolower(static_cast<unsigned char>(ext[3])) == 'a';
}

/** 指定パスの .lua を SD から読み込んで1回だけ実行する */
bool LuaInterpreter::runScriptFromSd(const char* path) {
    char* source = nullptr;
    size_t len = 0;
    clearGameScriptDir();
    if (!readSdFileToBuffer(path, &source, &len)) {
        return false;
    }

    printf("Lua: running %s (%u bytes)\n", path, static_cast<unsigned>(len));
    snprintf(lcd_line_, sizeof(lcd_line_), "%s", path);
    showStatus("Lua run:", lcd_line_, Color::YELLOW, Color::GRAY);

    lua_State* L = newLuaState();
    if (!L) {
        printf("Lua: lua_newstate failed\n");
        HeapBudget::release(source, len + 1);
        return false;
    }

    luaApiSetActiveInterpreter(this);
    luaL_openlibs(L);
    registerLuaHostApi(L);
    setGameScriptFromPath(path);

    const bool ok = loadScriptIntoState(L, path, source, len);
    if (!ok) {
        showStatus("Lua load err", last_error_, Color::RED, Color::GRAY);
    } else {
        printf("Lua: finished %s\n", path);
        showStatus("Lua OK", nullptr, Color::GREEN, Color::GRAY);
    }

    luaApiSetActiveInterpreter(nullptr);
    clearGameScriptDir();
    lua_close(L);
    return ok;
}

/** game_init / game_update / game_draw ループ付きで SD 上のゲームを実行する */
bool LuaInterpreter::runGameLoopFromSd(const char* path) {
    setLastError(nullptr);
    if (!sd_mounted_) {
        printf("Lua: SD not mounted\n");
        showStatus("SD not mounted", nullptr, Color::RED, Color::BLACK);
        return false;
    }
    if (!hooks_.display) {
        printf("Lua: GameDisplay not set\n");
        showStatus("No display", nullptr, Color::RED, Color::BLACK);
        return false;
    }

    if (hasPendingGameSession()) {
        printf("[MENU-DBG] runGameLoopFromSd: closing pending session\n");
        fflush(stdout);
        closePendingGameSession();
        printf("[MENU-DBG] runGameLoopFromSd: pending session closed\n");
        fflush(stdout);
    }

    char* source = nullptr;
    size_t len = 0;
    if (!readSdFileToBuffer(path, &source, &len)) {
        showStatus("Read failed", path, Color::RED, Color::BLACK);
        return false;
    }

    game_lua_ = newLuaState();
    if (!game_lua_) {
        printf("Lua: lua_newstate failed\n");
        showStatus("Lua OOM", nullptr, Color::RED, Color::BLACK);
        HeapBudget::release(source, len + 1);
        return false;
    }

    printf("Lua game: %s (%u bytes)\n", path, static_cast<unsigned>(len));
    luaApiSetActiveInterpreter(this);
    luaL_openlibs(game_lua_);
    registerLuaHostApi(game_lua_);
    setGameScriptFromPath(path);

    if (!loadScriptIntoState(game_lua_, path, source, len)) {
        showStatus("Game load err", last_error_, Color::RED, Color::BLACK);
        closeGameState();
        return false;
    }

    if (!requireGameCallbacks(game_lua_)) {
        setLastError("missing game_init/update/draw");
        showStatus("Not a game script", last_error_, Color::RED, Color::BLACK);
        closeGameState();
        return false;
    }

    lua_getglobal(game_lua_, "game_init");
    if (lua_pcall(game_lua_, 0, 0, 0) != LUA_OK) {
        const char* err = lua_tostring(game_lua_, -1);
        printf("game_init error: %s\n", err ? err : "?");
        showStatus("game_init err", err, Color::RED, Color::BLACK);
        lua_pop(game_lua_, 1);
        closeGameState();
        return false;
    }

    hooks_.display->fillScreen(Color::BLACK);

    const bool bad_apple_game = isBadAppleScriptPath(path);
    if (bad_apple_game) {
        initBadApplePlayerFromLua();
    }

    uint32_t last_ms = to_ms_since_boot(get_absolute_time());
    bool running = true;
    bool failed = false;
    debugOverlayReset();

    // --- メインゲームループ（1 イテレーション = 1 フレーム）---
    // update → [layers: composeBand] → game_draw×bandCount → DMA 待ち → SD ストリーム close
    while (running) {
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        debugOverlayTick(now_ms);
        lua_Integer dt = static_cast<lua_Integer>(now_ms - last_ms);
        last_ms = now_ms;
        if (dt < 0) {
            dt = 0;
        }
        if (dt > 100) {
            dt = 100;
        }

        audio_engine_.pumpStream();

        bool exit_requested = false;
        if (bad_apple_player_ && bad_apple_fast_active_) {
            if (!runBadAppleUpdate(static_cast<int>(dt))) {
                exit_requested = true;
            }
        } else {
            lua_getglobal(game_lua_, "game_update");
            if (!lua_isfunction(game_lua_, -1)) {
                lua_pop(game_lua_, 1);
                showStatus("game_update missing", nullptr, Color::RED, Color::BLACK);
                failed = true;
                break;
            }
            lua_pushinteger(game_lua_, dt);
            if (lua_pcall(game_lua_, 1, 1, 0) != LUA_OK) {
                const char* err = lua_tostring(game_lua_, -1);
                printf("game_update error: %s\n", err ? err : "?");
                showStatus("game_update err", err, Color::RED, Color::BLACK);
                lua_pop(game_lua_, 1);
                failed = true;
                break;
            }
            if (lua_toboolean(game_lua_, -1)) {
                exit_requested = true;
            }
            lua_pop(game_lua_, 1);
        }
        if (exit_requested) {
            printf("[MENU-DBG] runGameLoop: game_update returned true (exit request)\n");
            fflush(stdout);
            break;
        }

        bool drew_frame = false;
        if (bad_apple_player_ && bad_apple_fast_active_) {
            if (runBadAppleDrawFrame()) {
                drew_frame = true;
            } else {
                printf("bad_apple: fast path failed, falling back to band loop\n");
                bad_apple_fast_active_ = false;
            }
        }

        if (!drew_frame) {
            const int bands = hooks_.display->bandCount();
            bg_stream_.bw_rgb_fast_bands = false;

            // --- 高速パス: game_draw を 1 回だけ録画し、C 側でバンド再生 + dirty 帯スキップ ---
            // layers モードはタイルスクロールが command list 外なので従来パスを使う
            bool used_cmd_list = false;
            if (draw_mode_ != LuaDrawMode::Layers) {
                draw_cmds_.beginRecord(hooks_.display->width(), hooks_.display->height(),
                                       hooks_.display->bufferHeight());
                lua_getglobal(game_lua_, "game_draw");
                if (lua_isfunction(game_lua_, -1)) {
                    if (lua_pcall(game_lua_, 0, 0, 0) != LUA_OK) {
                        const char* err = lua_tostring(game_lua_, -1);
                        printf("game_draw error: %s\n", err ? err : "?");
                        showStatus("game_draw err", err, Color::RED, Color::BLACK);
                        lua_pop(game_lua_, 1);
                        draw_cmds_.markFailed();
                        failed = true;
                        running = false;
                    }
                } else {
                    lua_pop(game_lua_, 1);
                    draw_cmds_.markFailed();
                }

                const bool record_ok = draw_cmds_.endRecord();
                if (!failed && record_ok) {
                    used_cmd_list = true;
                    uint16_t dirty_mask = 0xFFFF;
                    const bool all_clean = draw_cmds_.computeDirtyBands(&dirty_mask);
                    if (!all_clean) {
                        for (int band = 0; band < bands; band++) {
                            if ((dirty_mask & static_cast<uint16_t>(1u << band)) == 0) {
                                continue;
                            }
                            hooks_.display->beginBand(band);
                            draw_cmds_.replayBand(this, hooks_.display, band);
                            hooks_.display->endBand();
                            prefetchBgStreamBand(band + 1);
                        }
                    }
                    draw_cmds_.commitBandHashes();
                }
            }

            // --- フォールバック: 従来どおりバンドごとに Lua game_draw ---
            if (!failed && !used_cmd_list) {
                draw_cmds_.reset();
                for (int band = 0; band < bands; band++) {
                    hooks_.display->beginBand(band);

                    if (draw_mode_ == LuaDrawMode::Layers) {
                        tile_layers_.composeBand(hooks_.display, tileLayerLookupImage, this);
                    }

                    const bool bw_rgb_blit_only =
                        band > 0 && drawBwPackBlitCurrentBand();
                    if (!bw_rgb_blit_only) {
                        lua_getglobal(game_lua_, "game_draw");
                        if (lua_isfunction(game_lua_, -1)) {
                            if (lua_pcall(game_lua_, 0, 0, 0) != LUA_OK) {
                                const char* err = lua_tostring(game_lua_, -1);
                                printf("game_draw error: %s\n", err ? err : "?");
                                showStatus("game_draw err", err, Color::RED, Color::BLACK);
                                lua_pop(game_lua_, 1);
                                failed = true;
                                running = false;
                                break;
                            }
                        } else {
                            lua_pop(game_lua_, 1);
                        }
                    }

                    hooks_.display->endBand();
                    prefetchBgStreamBand(band + 1);
                }
            }
        }
        if (failed) {
            break;
        }
        if (!drew_frame) {
            hooks_.display->waitForTransferComplete();
        }
        // FIL は跨ぎ維持（prefetch 破棄・BW 先読みのみ）。完全 close はゲーム終了時。
        if (!bad_apple_skip_prefetch_) {
            closeBgStream();
        }
        // closeVnStreamCompose() は呼ばない（フレーム末 close を廃止）
        debugOverlayDrawAfterFrame(hooks_.display->lcd(),
                                   static_cast<int>(hooks_.display->width()));

        EncoderVolumeControl::service();

        //sleep_ms(16);
    }

    if (failed) {
        printf("Lua game aborted: %s\n", path);
    } else {
        printf("Lua game ended: %s\n", path);
    }
    fflush(stdout);
    printf("[MENU-DBG] runGameLoop: loop finished failed=%d\n", failed ? 1 : 0);
    fflush(stdout);
    bad_apple_player_ = false;
    bad_apple_fast_active_ = false;
    bad_apple_pack_path_[0] = '\0';
    bad_apple_ready_ = false;
    bad_apple_missing_ = false;
    bad_apple_frame_idx_ = 1;
    bad_apple_frame_acc_ = 0;
    bad_apple_last_drawn_frame_ = 0;
    resetBgStream();
    closeVnStreamCompose(true);
    if (hooks_.display) {
        hooks_.display->releaseForDirectDraw();
    }
    printf("[MENU-DBG] runGameLoop: returning %d (defer finishGameSession)\n", failed ? 0 : 1);
    fflush(stdout);
    return !failed;
}

/** SD ルートの優先スクリプトまたは最初の .lua を1回実行する */
bool LuaInterpreter::executeOnSdRoot() {
    if (!sd_mounted_) {
        printf("Lua: SD not mounted\n");
        return false;
    }

    static const char* kPriorityScripts[] = {"main.lua", "game.lua", "boot.lua"};
    for (const char* script : kPriorityScripts) {
        if (!sdFileExists(script)) {
            continue;
        }
        if (runScriptFromSd(script)) {
            return true;
        }
    }

    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, "/") != FR_OK) {
        printf("Lua: f_opendir failed\n");
        return false;
    }

    char first_lua[FF_LFN_BUF + 1] = {};
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        if (fno.fattrib & AM_DIR) {
            continue;
        }
        if (!endsWithLuaExt(fno.fname)) {
            continue;
        }
        strncpy(first_lua, fno.fname, sizeof(first_lua) - 1);
        break;
    }
    f_closedir(&dir);

    if (first_lua[0] == 0) {
        printf("Lua: no .lua file on SD root\n");
        showStatus("No .lua file", nullptr, Color::YELLOW, Color::GRAY);
        return false;
    }

    return runScriptFromSd(first_lua);
}
