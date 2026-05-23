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

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "f_util.h"
#include "ff.h"
}

namespace {

LuaInterpreter* g_active_interpreter = nullptr;

int luaHostPrint(lua_State* L) {
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) putchar('\t');
        const char* s = luaL_tolstring(L, i, nullptr);
        if (s) printf("%s", s);
        lua_pop(L, 1);
    }
    putchar('\n');
    return 0;
}

int luaHostSleepMs(lua_State* L) {
    lua_Integer ms = luaL_checkinteger(L, 1);
    if (ms < 0) ms = 0;
    sleep_ms((uint32_t)ms);
    return 0;
}

int luaHostLcdText(lua_State* L) {
    if (!g_active_interpreter) return 0;
    const LuaHostHooks& hooks = g_active_interpreter->hostHooks();
    if (!hooks.draw_text_bg) return 0;

    lua_Integer x = luaL_checkinteger(L, 1);
    lua_Integer y = luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    hooks.draw_text_bg(hooks.user_data, (int)x, (int)y, text, Color::WHITE, Color::GRAY);
    return 0;
}

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

void registerLuaHostApi(lua_State* L) {
    lua_pushcfunction(L, luaHostPrint);
    lua_setglobal(L, "print");
    lua_pushcfunction(L, luaHostSleepMs);
    lua_setglobal(L, "sleep_ms");
    lua_newtable(L);
    lua_pushcfunction(L, luaHostLcdText);
    lua_setfield(L, -2, "text");
    lua_pushcfunction(L, luaHostButtonPressed);
    lua_setfield(L, -2, "pressed");
    lua_setglobal(L, "machine");
}

}  // namespace

LuaInterpreter::LuaInterpreter()
    : hooks_(), sd_mounted_(false), max_script_bytes_(kDefaultMaxScriptBytes) {
    lcd_line_[0] = '\0';
}

void LuaInterpreter::setHostHooks(const LuaHostHooks& hooks) { hooks_ = hooks; }

void LuaInterpreter::setSdMounted(bool mounted) { sd_mounted_ = mounted; }

void LuaInterpreter::setMaxScriptBytes(size_t max_bytes) { max_script_bytes_ = max_bytes; }

bool LuaInterpreter::sdFileExists(const char* path) const {
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) return false;
    return !(fno.fattrib & AM_DIR);
}

void LuaInterpreter::showStatus(const char* line1, const char* line2, uint16_t color,
                                uint16_t bg) {
    if (hooks_.draw_text_bg) {
        if (line1) hooks_.draw_text_bg(hooks_.user_data, 10, 80, line1, color, bg);
        if (line2) hooks_.draw_text_bg(hooks_.user_data, 10, 90, line2, color, bg);
    }
}

bool LuaInterpreter::readSdFileToBuffer(const char* path, char** out_buf,
                                        size_t* out_len) const {
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
        printf("Lua: invalid size %s (%lu bytes, max %u)\n", path,
               (unsigned long)fsize, (unsigned)max_script_bytes_);
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

bool LuaInterpreter::endsWithLuaExt(const char* name) const {
    size_t len = strlen(name);
    if (len < 4) return false;
    const char* ext = name + len - 4;
    return ext[0] == '.' && tolower((unsigned char)ext[1]) == 'l' &&
           tolower((unsigned char)ext[2]) == 'u' && tolower((unsigned char)ext[3]) == 'a';
}

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

    int load_stat = luaL_loadbuffer(L, source, len, path);
    if (load_stat != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        printf("Lua load error [%s]: %s\n", path, err ? err : "unknown");
        showStatus("Lua load err", nullptr, Color::RED, Color::GRAY);
        g_active_interpreter = nullptr;
        lua_close(L);
        free(source);
        return false;
    }

    int call_stat = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (call_stat != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        printf("Lua runtime error [%s]: %s\n", path, err ? err : "unknown");
        if (err) {
            snprintf(lcd_line_, sizeof(lcd_line_), "%.28s", err);
            showStatus("Lua run err", lcd_line_, Color::RED, Color::GRAY);
        } else {
            showStatus("Lua run err", nullptr, Color::RED, Color::GRAY);
        }
        g_active_interpreter = nullptr;
        lua_close(L);
        free(source);
        return false;
    }

    printf("Lua: finished %s\n", path);
    showStatus("Lua OK", nullptr, Color::GREEN, Color::GRAY);
    g_active_interpreter = nullptr;
    lua_close(L);
    free(source);
    return true;
}

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
