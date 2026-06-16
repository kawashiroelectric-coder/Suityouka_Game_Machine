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

static void* luaHeapAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud;
    return HeapBudget::reallocBlock(ptr, osize, nsize);
}

}  // namespace

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

LuaInterpreter::~LuaInterpreter() {
    closeGameState();
    freeAllImages();
}

void LuaInterpreter::closeGameState() {
    audio_engine_.stop();
    draw_mode_ = LuaDrawMode::Direct;
    layer_backdrop_color_ = 0;
    tile_layers_.reset();
    clearGameScriptDir();
    if (game_lua_) {
        luaApiSetActiveInterpreter(nullptr);
        lua_close(game_lua_);
        game_lua_ = nullptr;
    }
    freeAllImages();
    unloadFont();
    closeBgStream();
    closeVnStreamCompose();
}

void LuaInterpreter::clearGameScriptDir() { game_script_dir_[0] = '\0'; }

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

void LuaInterpreter::resolveGamePath(const char* path, char* out, size_t out_len) const {
    const char* dir = (game_script_dir_[0] != '\0') ? game_script_dir_ : nullptr;
    resolveSdPath(dir, path, out, out_len);
}

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

void LuaInterpreter::unloadFont() {
    font_renderer_.unload();
    if (FontRenderer::active() == &font_renderer_) {
        FontRenderer::setActive(nullptr);
    }
    GameDisplay::setFontRenderer(nullptr);
}

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

void LuaInterpreter::closeBgStream() {
    if (bg_stream_.open) {
        f_close(&bg_stream_.file);
        bg_stream_.open = false;
    }
    bg_stream_.path[0] = '\0';
    bg_stream_.fail_path[0] = '\0';
    bg_stream_.width = 0;
    bg_stream_.height = 0;
    bg_stream_.dx = 0;
    bg_stream_.dy = 0;
    bg_stream_.prefetch.valid = false;
    bg_stream_.prefetch.display_band = -1;
}

void LuaInterpreter::prefetchBgStreamBand(int display_band) {
    bg_stream_.prefetch.valid = false;
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
    if (!readBgStreamChunk(&bg_stream_.file, bg_stream_.width, src_y0, rows,
                           g_bg_stream_buf[slot])) {
        return;
    }

    bg_stream_.prefetch.valid = true;
    bg_stream_.prefetch.display_band = display_band;
    bg_stream_.prefetch.draw_top = draw_top;
    bg_stream_.prefetch.rows = rows;
    bg_stream_.prefetch.src_y0 = src_y0;
    bg_stream_.prefetch.buf_slot = slot;
}

bool LuaInterpreter::drawBgStreamFromSd(const char* path, int dx, int dy, uint16_t w, uint16_t h) {
    if (!path || path[0] == '\0' || w == 0 || h == 0) {
        return false;
    }
    if (!sd_mounted_) {
        return false;
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
        bg_stream_.height != h;
    if (path_changed) {
        closeBgStream();
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

const ImageSlot* LuaInterpreter::getImage(int id) const {
    if (id < 0 || id >= kMaxImageSlots) {
        return nullptr;
    }
    if (!images_[id].used) {
        return nullptr;
    }
    return &images_[id];
}

void LuaInterpreter::freeImage(int id) {
    if (id < 0 || id >= kMaxImageSlots) {
        return;
    }
    if (images_[id].used) {
        HeapBudget::release(images_[id].pixels, images_[id].byte_size);
        images_[id] = ImageSlot();
    }
}

void LuaInterpreter::freeAllImages() {
    for (int i = 0; i < kMaxImageSlots; i++) {
        freeImage(i);
    }
}

void LuaInterpreter::setHostHooks(const LuaHostHooks& hooks) { hooks_ = hooks; }

void LuaInterpreter::setAudioOutput(AudioOutput* audio) {
    audio_engine_.attach(audio, AudioConfig::SAMPLE_RATE);
    if (audio) {
        audio->setCallback(LuaAudio::audioCallback);
        audio->setVolume(1.0f);
        audio->start();
    }
}

void LuaInterpreter::setSdMounted(bool mounted) {
    sd_mounted_ = mounted;
    audio_engine_.setSdMounted(mounted);
}

void LuaInterpreter::setMaxScriptBytes(size_t max_bytes) { max_script_bytes_ = max_bytes; }

bool LuaInterpreter::sdFileExists(const char* path) const {
    char norm[FF_LFN_BUF + 4];
    resolveGamePath(path, norm, sizeof(norm));
    FILINFO fno;
    if (f_stat(norm, &fno) != FR_OK) {
        return false;
    }
    return !(fno.fattrib & AM_DIR);
}

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

void LuaInterpreter::setLastError(const char* msg) {
    if (!msg) {
        last_error_[0] = '\0';
        return;
    }
    strncpy(last_error_, msg, sizeof(last_error_) - 1);
    last_error_[sizeof(last_error_) - 1] = '\0';
}

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

lua_State* LuaInterpreter::newLuaState() { return lua_newstate(luaHeapAlloc, nullptr); }

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

    char* source = nullptr;
    size_t len = 0;
    closeGameState();
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
            lua_pop(game_lua_, 1);
            break;
        }
        lua_pop(game_lua_, 1);

        const int bands = hooks_.display->bandCount();
        for (int band = 0; band < bands; band++) {
            hooks_.display->beginBand(band);

            if (draw_mode_ == LuaDrawMode::Layers) {
                tile_layers_.composeBand(hooks_.display, tileLayerLookupImage, this);
            }

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

            hooks_.display->endBand();
            prefetchBgStreamBand(band + 1);
        }
        if (failed) {
            break;
        }
        hooks_.display->waitForTransferComplete();
        closeBgStream();
        closeVnStreamCompose();
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
    closeGameState();
    return !failed;
}

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
