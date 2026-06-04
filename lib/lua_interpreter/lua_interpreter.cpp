// ============================================
// ファイル: lua_interpreter.cpp
// SDカード上の Lua スクリプト読み込み・実行
// ============================================

#include "lua_interpreter.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pico/stdlib.h"
#include "st7789_lcd.hpp"
#include "game_display.hpp"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "f_util.h"
#include "ff.h"
}

namespace {

LuaInterpreter* g_active_interpreter = nullptr;

#ifdef GAME_MACHINE_DEBUG
class FpsOverlay {
public:
    void reset() {
        last_ms_ = 0;
        accum_ms_ = 0;
        frames_ = 0;
        displayed_fps_ = 0;
    }

    void tick(uint32_t now_ms) {
        if (last_ms_ == 0) {
            last_ms_ = now_ms;
            return;
        }
        const uint32_t dt = now_ms - last_ms_;
        last_ms_ = now_ms;
        accum_ms_ += dt;
        frames_++;
        if (accum_ms_ >= 250) {
            displayed_fps_ = static_cast<uint16_t>((frames_ * 1000u + accum_ms_ / 2) / accum_ms_);
            accum_ms_ = 0;
            frames_ = 0;
        }
    }

    void draw(GameDisplay* disp) const {
        if (!disp) return;
        char buf[16];
        snprintf(buf, sizeof(buf), "FPS:%u", static_cast<unsigned>(displayed_fps_));
        const int text_w = static_cast<int>(strlen(buf)) * 8;
        const int x = static_cast<int>(disp->width()) - text_w;
        disp->drawTextBg(x, 0, buf, Color::WHITE, Color::BLACK);
    }

private:
    uint32_t last_ms_ = 0;
    uint32_t accum_ms_ = 0;
    uint32_t frames_ = 0;
    uint16_t displayed_fps_ = 0;
};

FpsOverlay g_fps_overlay;
#endif  // GAME_MACHINE_DEBUG

/** Lua 引数を RGB565 に変換（3 整数または 1 整数） */
uint16_t parseColor(lua_State* L, int idx) {
    int n = lua_gettop(L);
    if (n >= idx + 2 && lua_isnumber(L, idx) && lua_isnumber(L, idx + 1) &&
        lua_isnumber(L, idx + 2)) {
        int r = (int)luaL_checkinteger(L, idx);
        int g = (int)luaL_checkinteger(L, idx + 1);
        int b = (int)luaL_checkinteger(L, idx + 2);
        if (r < 0) r = 0;
        if (r > 255) r = 255;
        if (g < 0) g = 0;
        if (g > 255) g = 255;
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        return GameDisplay::rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    return (uint16_t)luaL_checkinteger(L, idx);
}

/** 実行中インタプリタの GameDisplay を取得 */
GameDisplay* activeDisplay() {
    if (!g_active_interpreter) return nullptr;
    return g_active_interpreter->hostHooks().display;
}

/** machine.print 相当: 引数をタブ区切りで stdout へ */
int luaHostPrint(lua_State* L) {
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) printf("\t");
        const char* s = luaL_tolstring(L, i, nullptr);
        if (s) printf("%s", s);
        lua_pop(L, 1);
    }
    printf("\n");
    fflush(stdout);
    return 0;
}

/** sleep_ms(ms) */
int luaHostSleepMs(lua_State* L) {
    lua_Integer ms = luaL_checkinteger(L, 1);
    if (ms < 0) ms = 0;
    sleep_ms((uint32_t)ms);
    return 0;
}

/** machine.text(x, y, str [, fg [, bg]]) */
int luaHostLcdText(lua_State* L) {
    if (!g_active_interpreter) return 0;
    const LuaHostHooks& hooks = g_active_interpreter->hostHooks();

    lua_Integer x = luaL_checkinteger(L, 1);
    lua_Integer y = luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    uint16_t fg = (lua_gettop(L) >= 4) ? parseColor(L, 4) : Color::WHITE;
    uint16_t bg = (lua_gettop(L) >= 5) ? parseColor(L, 5) : Color::BLACK;

    if (hooks.display) {
        hooks.display->drawTextBg((int)x, (int)y, text, fg, bg);
        return 0;
    }
    if (hooks.draw_text_bg) {
        hooks.draw_text_bg(hooks.user_data, (int)x, (int)y, text, fg, bg);
    }
    return 0;
}

/** machine.pressed(button_index) */
int luaHostButtonPressed(lua_State* L) {
    if (!g_active_interpreter) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const LuaHostHooks& hooks = g_active_interpreter->hostHooks();
    lua_Integer idx = luaL_checkinteger(L, 1);
    bool pressed = false;
    if (hooks.is_button_pressed) {
        pressed = hooks.is_button_pressed(hooks.user_data, (int)idx);
    }
    lua_pushboolean(L, pressed);
    return 1;
}

/** machine.jump_pressed(): ジャンプ用ボタンいずれか */
int luaHostJumpPressed(lua_State* L) {
    (void)L;
    if (!g_active_interpreter) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const LuaHostHooks& hooks = g_active_interpreter->hostHooks();
    bool pressed = false;
    if (hooks.is_button_pressed) {
        static const int kJumpButtons[] = {1, 5, 0, 3, 7};
        for (int btn : kJumpButtons) {
            if (hooks.is_button_pressed(hooks.user_data, btn)) {
                pressed = true;
                break;
            }
        }
    }
    lua_pushboolean(L, pressed);
    return 1;
}

/** machine.clear(color) */
int luaHostClear(lua_State* L) {
    GameDisplay* disp = activeDisplay();
    if (!disp) return 0;
    disp->clear(parseColor(L, 1));
    return 0;
}

/** machine.fill_rect(x, y, w, h, color) */
int luaHostFillRect(lua_State* L) {
    GameDisplay* disp = activeDisplay();
    if (!disp) return 0;
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    uint16_t color = parseColor(L, 5);
    disp->fillRect(x, y, w, h, color);
    return 0;
}

/** machine.fill_rects({{x,y,w,h,color}, ...}) */
int luaHostFillRects(lua_State* L) {
    GameDisplay* disp = activeDisplay();
    if (!disp) return 0;
    luaL_checktype(L, 1, LUA_TTABLE);
    const int n = (int)lua_rawlen(L, 1);
    if (n <= 0) return 0;

    static constexpr int kMaxBatch = 64;
    GameDisplay::FillRect rects[kMaxBatch];

    int processed = 0;
    while (processed < n) {
        int count = 0;
        const int chunk_end = (processed + kMaxBatch < n) ? (processed + kMaxBatch) : n;

        for (int i = processed + 1; i <= chunk_end; i++) {
            lua_rawgeti(L, 1, i);
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                continue;
            }

            const int t = lua_gettop(L);
            auto read_i = [&](int idx) -> int {
                lua_rawgeti(L, t, idx);
                int v = (int)luaL_checkinteger(L, -1);
                lua_pop(L, 1);
                return v;
            };

            GameDisplay::FillRect r;
            r.x = read_i(1);
            r.y = read_i(2);
            r.w = read_i(3);
            r.h = read_i(4);
            lua_rawgeti(L, t, 5);
            r.color = (uint16_t)luaL_checkinteger(L, -1);
            lua_pop(L, 1);

            rects[count++] = r;
            lua_pop(L, 1);
        }

        if (count > 0) {
            disp->fillRects(rects, static_cast<size_t>(count));
        }
        processed = chunk_end;
    }
    return 0;
}

/**
 * machine.set_present_mode("full"|"partial")
 * バンド（ラインバッファ）描画ではホストのゲームループが全バンドを順に転送するため、
 * 描画モードの切り替えは行わない（互換のため受け取って無視）。
 */
int luaHostSetPresentMode(lua_State* L) {
    (void)L;
    return 0;
}

/**
 * machine.present([mode])
 * バンド描画ではバンドごとに game_draw → 転送をホストが駆動するため、
 * スクリプト側からの present 要求は無視する（互換のための no-op）。
 */
int luaHostPresent(lua_State* L) {
    (void)L;
    return 0;
}

/** machine.width() */
int luaHostWidth(lua_State* L) {
    GameDisplay* disp = activeDisplay();
    lua_pushinteger(L, disp ? disp->width() : 0);
    return 1;
}

/** machine.height() */
int luaHostHeight(lua_State* L) {
    GameDisplay* disp = activeDisplay();
    lua_pushinteger(L, disp ? disp->height() : 0);
    return 1;
}

/** machine.time_ms(): 起動からのミリ秒 */
int luaHostTimeMs(lua_State* L) {
    (void)L;
    lua_pushinteger(L, (lua_Integer)to_ms_since_boot(get_absolute_time()));
    return 1;
}

/** machine.rgb(r, g, b) */
int luaHostRgb(lua_State* L) {
    int r = (int)luaL_checkinteger(L, 1);
    int g = (int)luaL_checkinteger(L, 2);
    int b = (int)luaL_checkinteger(L, 3);
    lua_pushinteger(L, GameDisplay::rgb((uint8_t)r, (uint8_t)g, (uint8_t)b));
    return 1;
}

/** machine.load_image(path, width, height) → id（失敗時は nil, errmsg） */
int luaHostLoadImage(lua_State* L) {
    if (!g_active_interpreter) return luaL_error(L, "no active interpreter");
    const char* path = luaL_checkstring(L, 1);
    int w = (int)luaL_checkinteger(L, 2);
    int h = (int)luaL_checkinteger(L, 3);
    if (w <= 0 || h <= 0) return luaL_error(L, "load_image: width/height must be > 0");

    int id = g_active_interpreter->loadImage(path, (uint16_t)w, (uint16_t)h);
    if (id < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "load_image failed (see serial log)");
        return 2;
    }
    lua_pushinteger(L, id);
    return 1;
}

/** machine.draw_image(id, x, y [, sx, sy, sw, sh]) */
int luaHostDrawImage(lua_State* L) {
    if (!g_active_interpreter) return 0;
    GameDisplay* disp = activeDisplay();
    if (!disp) return 0;

    int id = (int)luaL_checkinteger(L, 1);
    int dx = (int)luaL_checkinteger(L, 2);
    int dy = (int)luaL_checkinteger(L, 3);

    const ImageSlot* slot = g_active_interpreter->getImage(id);
    if (!slot) return luaL_error(L, "draw_image: invalid image id %d", id);

    if (lua_gettop(L) >= 7) {
        int sx = (int)luaL_checkinteger(L, 4);
        int sy = (int)luaL_checkinteger(L, 5);
        int sw = (int)luaL_checkinteger(L, 6);
        int sh = (int)luaL_checkinteger(L, 7);
        disp->drawImageSub(dx, dy, slot->width, slot->height, slot->pixels, sx, sy, sw, sh);
    } else {
        disp->drawImage(dx, dy, slot->width, slot->height, slot->pixels);
    }
    return 0;
}

/** machine.free_image(id) */
int luaHostFreeImage(lua_State* L) {
    if (!g_active_interpreter) return 0;
    int id = (int)luaL_checkinteger(L, 1);
    g_active_interpreter->freeImage(id);
    return 0;
}

/** machine.image_size(id) → width, height */
int luaHostImageSize(lua_State* L) {
    if (!g_active_interpreter) return luaL_error(L, "no active interpreter");
    int id = (int)luaL_checkinteger(L, 1);
    const ImageSlot* slot = g_active_interpreter->getImage(id);
    if (!slot) return luaL_error(L, "image_size: invalid image id %d", id);
    lua_pushinteger(L, slot->width);
    lua_pushinteger(L, slot->height);
    return 2;
}

}  // namespace

/** print / sleep_ms / machine テーブルをグローバル登録 */
void LuaInterpreter::registerLuaHostApi(lua_State* L) {
    lua_pushcfunction(L, luaHostPrint);
    lua_setglobal(L, "print");
    lua_pushcfunction(L, luaHostSleepMs);
    lua_setglobal(L, "sleep_ms");

    lua_newtable(L);
    lua_pushcfunction(L, luaHostLcdText);
    lua_setfield(L, -2, "text");
    lua_pushcfunction(L, luaHostButtonPressed);
    lua_setfield(L, -2, "pressed");
    lua_pushcfunction(L, luaHostJumpPressed);
    lua_setfield(L, -2, "jump_pressed");
    lua_pushcfunction(L, luaHostClear);
    lua_setfield(L, -2, "clear");
    lua_pushcfunction(L, luaHostFillRect);
    lua_setfield(L, -2, "fill_rect");
    lua_pushcfunction(L, luaHostFillRects);
    lua_setfield(L, -2, "fill_rects");
    lua_pushcfunction(L, luaHostSetPresentMode);
    lua_setfield(L, -2, "set_present_mode");
    lua_pushcfunction(L, luaHostPresent);
    lua_setfield(L, -2, "present");
    lua_pushcfunction(L, luaHostWidth);
    lua_setfield(L, -2, "width");
    lua_pushcfunction(L, luaHostHeight);
    lua_setfield(L, -2, "height");
    lua_pushcfunction(L, luaHostTimeMs);
    lua_setfield(L, -2, "time_ms");
    lua_pushcfunction(L, luaHostRgb);
    lua_setfield(L, -2, "rgb");
    lua_pushcfunction(L, luaHostLoadImage);
    lua_setfield(L, -2, "load_image");
    lua_pushcfunction(L, luaHostDrawImage);
    lua_setfield(L, -2, "draw_image");
    lua_pushcfunction(L, luaHostFreeImage);
    lua_setfield(L, -2, "free_image");
    lua_pushcfunction(L, luaHostImageSize);
    lua_setfield(L, -2, "image_size");
    lua_setglobal(L, "machine");
}

LuaInterpreter::LuaInterpreter()
    : hooks_(),
      sd_mounted_(false),
      max_script_bytes_(kDefaultMaxScriptBytes),
      game_lua_(nullptr),
      images_() {
    lcd_line_[0] = '\0';
}

LuaInterpreter::~LuaInterpreter() {
    closeGameState();
    freeAllImages();
}

/** game_lua_ を閉じ g_active_interpreter をクリアし画像も全解放 */
void LuaInterpreter::closeGameState() {
    if (game_lua_) {
        g_active_interpreter = nullptr;
        lua_close(game_lua_);
        game_lua_ = nullptr;
    }
    freeAllImages();
}

/** SD から RGB565 生データを読み込みスロットに格納。成功時スロット ID、失敗時 -1 */
int LuaInterpreter::loadImage(const char* path, uint16_t w, uint16_t h) {
    if (!sd_mounted_) {
        printf("loadImage: SD not mounted\n");
        return -1;
    }
    size_t byte_size = (size_t)w * h * 2;
    if (byte_size == 0 || byte_size > kMaxImageBytes) {
        printf("loadImage: size out of range (%u x %u = %u bytes, max %u)\n",
               w, h, (unsigned)byte_size, (unsigned)kMaxImageBytes);
        return -1;
    }

    int slot_id = -1;
    for (int i = 0; i < kMaxImageSlots; i++) {
        if (!images_[i].used) { slot_id = i; break; }
    }
    if (slot_id < 0) {
        printf("loadImage: no free slot (max %d)\n", kMaxImageSlots);
        return -1;
    }

    FIL file;
    FRESULT fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        printf("loadImage: open failed %s (%s)\n", path, FRESULT_str(fr));
        return -1;
    }

    FSIZE_t fsize = f_size(&file);
    if (fsize < byte_size) {
        printf("loadImage: file too small (%lu < %u)\n", (unsigned long)fsize, (unsigned)byte_size);
        f_close(&file);
        return -1;
    }

    uint16_t* pixels = static_cast<uint16_t*>(malloc(byte_size));
    if (!pixels) {
        printf("loadImage: malloc failed (%u bytes)\n", (unsigned)byte_size);
        f_close(&file);
        return -1;
    }

    UINT br = 0;
    fr = f_read(&file, pixels, (UINT)byte_size, &br);
    f_close(&file);
    if (fr != FR_OK || br != (UINT)byte_size) {
        printf("loadImage: read failed %s\n", path);
        free(pixels);
        return -1;
    }

    images_[slot_id].pixels = pixels;
    images_[slot_id].width = w;
    images_[slot_id].height = h;
    images_[slot_id].used = true;
    printf("loadImage: [%d] %s %ux%u OK\n", slot_id, path, w, h);
    return slot_id;
}

const ImageSlot* LuaInterpreter::getImage(int id) const {
    if (id < 0 || id >= kMaxImageSlots) return nullptr;
    if (!images_[id].used) return nullptr;
    return &images_[id];
}

void LuaInterpreter::freeImage(int id) {
    if (id < 0 || id >= kMaxImageSlots) return;
    if (images_[id].used) {
        free(images_[id].pixels);
        images_[id] = ImageSlot();
    }
}

void LuaInterpreter::freeAllImages() {
    for (int i = 0; i < kMaxImageSlots; i++) {
        freeImage(i);
    }
}

void LuaInterpreter::setHostHooks(const LuaHostHooks& hooks) { hooks_ = hooks; }

void LuaInterpreter::setSdMounted(bool mounted) { sd_mounted_ = mounted; }

void LuaInterpreter::setMaxScriptBytes(size_t max_bytes) { max_script_bytes_ = max_bytes; }

bool LuaInterpreter::sdFileExists(const char* path) const {
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) return false;
    return !(fno.fattrib & AM_DIR);
}

/** draw_text_bg コールバックで 2 行ステータス表示 */
void LuaInterpreter::showStatus(const char* line1, const char* line2, uint16_t color, uint16_t bg) {
    if (hooks_.draw_text_bg) {
        if (line1) hooks_.draw_text_bg(hooks_.user_data, 10, 80, line1, color, bg);
        if (line2) hooks_.draw_text_bg(hooks_.user_data, 10, 90, line2, color, bg);
    }
}

/** FatFS でファイルを読み NUL 終端バッファに格納（呼び出し側 free） */
bool LuaInterpreter::readSdFileToBuffer(const char* path, char** out_buf, size_t* out_len) const {
    *out_buf = nullptr;
    *out_len = 0;
    if (!sd_mounted_) return false;

    FIL file;
    FRESULT fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        printf("Lua: open failed %s (%s)\n", path, FRESULT_str(fr));
        return false;
    }

    FSIZE_t fsize = f_size(&file);
    if (fsize == 0 || fsize > max_script_bytes_) {
        printf("Lua: invalid size %s (%lu bytes, max %u)\n", path, (unsigned long)fsize,
               (unsigned)max_script_bytes_);
        f_close(&file);
        return false;
    }

    char* buf = static_cast<char*>(malloc((size_t)fsize + 1));
    if (!buf) {
        printf("Lua: malloc failed for %s\n", path);
        f_close(&file);
        return false;
    }

    UINT br = 0;
    fr = f_read(&file, buf, (UINT)fsize, &br);
    f_close(&file);
    if (fr != FR_OK || br != (UINT)fsize) {
        printf("Lua: read failed %s (%s)\n", path, FRESULT_str(fr));
        free(buf);
        return false;
    }
    buf[fsize] = '\0';
    *out_buf = buf;
    *out_len = (size_t)fsize;
    return true;
}

/** luaL_loadbuffer + lua_pcall でスクリプトを 1 回実行 */
bool LuaInterpreter::loadScriptIntoState(lua_State* L, const char* path, const char* source,
                                          size_t len) {
    int load_stat = luaL_loadbuffer(L, source, len, path);
    if (load_stat != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        printf("Lua load error [%s]: %s\n", path, err ? err : "unknown");
        return false;
    }
    int call_stat = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (call_stat != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        printf("Lua runtime error [%s]: %s\n", path, err ? err : "unknown");
        return false;
    }
    return true;
}

/** 拡張子が .lua（大文字小文字無視）か */
bool LuaInterpreter::endsWithLuaExt(const char* name) const {
    size_t len = strlen(name);
    if (len < 4) return false;
    const char* ext = name + len - 4;
    return ext[0] == '.' && tolower((unsigned char)ext[1]) == 'l' &&
           tolower((unsigned char)ext[2]) == 'u' &&            tolower((unsigned char)ext[3]) == 'a';
}

/** 一時 lua_State で SD 上の .lua を 1 回実行 */
bool LuaInterpreter::runScriptFromSd(const char* path) {
    char* source = nullptr;
    size_t len = 0;
    if (!readSdFileToBuffer(path, &source, &len)) {
        return false;
    }

    printf("Lua: running %s (%u bytes)\n", path, (unsigned)len);
    snprintf(lcd_line_, sizeof(lcd_line_), "%s", path);
    showStatus("Lua run:", lcd_line_, Color::YELLOW, Color::GRAY);

    lua_State* L = luaL_newstate();
    if (!L) {
        printf("Lua: luaL_newstate failed\n");
        free(source);
        return false;
    }

    g_active_interpreter = this;
    luaL_openlibs(L);
    registerLuaHostApi(L);

    bool ok = loadScriptIntoState(L, path, source, len);
    if (!ok) {
        showStatus("Lua load err", nullptr, Color::RED, Color::GRAY);
    } else {
        printf("Lua: finished %s\n", path);
        showStatus("Lua OK", nullptr, Color::GREEN, Color::GRAY);
    }

    g_active_interpreter = nullptr;
    lua_close(L);
    free(source);
    return ok;
}

/** game_init / game_update / game_draw ループでゲームを実行 */
bool LuaInterpreter::runGameLoopFromSd(const char* path) {
    if (!sd_mounted_) {
        printf("Lua: SD not mounted\n");
        return false;
    }
    if (!hooks_.display) {
        printf("Lua: GameDisplay not set\n");
        return false;
    }

    char* source = nullptr;
    size_t len = 0;
    if (!readSdFileToBuffer(path, &source, &len)) {
        return false;
    }

    closeGameState();
    game_lua_ = luaL_newstate();
    if (!game_lua_) {
        free(source);
        return false;
    }

    printf("Lua game: %s (%u bytes)\n", path, (unsigned)len);
    g_active_interpreter = this;
    luaL_openlibs(game_lua_);
    registerLuaHostApi(game_lua_);

    if (!loadScriptIntoState(game_lua_, path, source, len)) {
        showStatus("Game load err", nullptr, Color::RED, Color::BLACK);
        closeGameState();
        free(source);
        return false;
    }
    free(source);

    lua_getglobal(game_lua_, "game_init");
    if (lua_isfunction(game_lua_, -1)) {
        if (lua_pcall(game_lua_, 0, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(game_lua_, -1);
            printf("game_init error: %s\n", err ? err : "?");
            lua_pop(game_lua_, 1);
            closeGameState();
            return false;
        }
    } else {
        lua_pop(game_lua_, 1);
    }

    uint32_t last_ms = to_ms_since_boot(get_absolute_time());
    bool running = true;
#ifdef GAME_MACHINE_DEBUG
    g_fps_overlay.reset();
#endif

    while (running) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
#ifdef GAME_MACHINE_DEBUG
        g_fps_overlay.tick(now_ms);
#endif
        lua_Integer dt = (lua_Integer)(now_ms - last_ms);
        last_ms = now_ms;
        if (dt < 0) dt = 0;
        if (dt > 100) dt = 100;

        lua_getglobal(game_lua_, "game_update");
        if (!lua_isfunction(game_lua_, -1)) {
            lua_pop(game_lua_, 1);
            break;
        }
        lua_pushinteger(game_lua_, dt);
        if (lua_pcall(game_lua_, 1, 1, 0) != LUA_OK) {
            const char* err = lua_tostring(game_lua_, -1);
            printf("game_update error: %s\n", err ? err : "?");
            lua_pop(game_lua_, 1);
            break;
        }
        if (lua_toboolean(game_lua_, -1)) {
            lua_pop(game_lua_, 1);
            break;
        }
        lua_pop(game_lua_, 1);

        // バンド（ラインバッファ）描画: 1 フレームを横帯に分割し、
        // 各バンドで game_draw を呼んでそのバンド領域だけ LCD へ転送する。
        const int bands = hooks_.display->bandCount();
        bool draw_error = false;
        for (int band = 0; band < bands; band++) {
            hooks_.display->beginBand(band);

            lua_getglobal(game_lua_, "game_draw");
            if (lua_isfunction(game_lua_, -1)) {
                if (lua_pcall(game_lua_, 0, 0, 0) != LUA_OK) {
                    const char* err = lua_tostring(game_lua_, -1);
                    printf("game_draw error: %s\n", err ? err : "?");
                    lua_pop(game_lua_, 1);
                    draw_error = true;
                    break;
                }
            } else {
                lua_pop(game_lua_, 1);
            }

#ifdef GAME_MACHINE_DEBUG
            g_fps_overlay.draw(hooks_.display);
#endif
            hooks_.display->endBand();
        }
        if (draw_error) break;
        hooks_.display->waitForTransferComplete();

        sleep_ms(16);
    }

    printf("Lua game ended: %s\n", path);
    closeGameState();
    return true;
}

/** main.lua → game.lua → boot.lua → 最初の .lua の順で 1 本実行 */
bool LuaInterpreter::executeOnSdRoot() {
    if (!sd_mounted_) {
        printf("Lua: SD not mounted\n");
        return false;
    }

    static const char* kPriorityScripts[] = {"main.lua", "game.lua", "boot.lua"};
    for (const char* script : kPriorityScripts) {
        if (!sdFileExists(script)) continue;
        if (runScriptFromSd(script)) return true;
    }

    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, "/") != FR_OK) {
        printf("Lua: f_opendir failed\n");
        return false;
    }

    char first_lua[FF_LFN_BUF + 1] = {};
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        if (fno.fattrib & AM_DIR) continue;
        if (!endsWithLuaExt(fno.fname)) continue;
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
