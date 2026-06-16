// ============================================
// ファイル: lua_api_internal.cpp
// Lua バインディング共通状態
// ============================================

#include "lua_api_internal.hpp"

#include "game_display.hpp"
#include "lua_interpreter.hpp"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

LuaInterpreter* g_active_interpreter = nullptr;

}  // namespace

LuaInterpreter* luaApiActiveInterpreter() { return g_active_interpreter; }

void luaApiSetActiveInterpreter(LuaInterpreter* interp) { g_active_interpreter = interp; }

GameDisplay* luaApiActiveDisplay() {
    if (!g_active_interpreter) {
        return nullptr;
    }
    return g_active_interpreter->hostHooks().display;
}

uint16_t luaApiParseColor(lua_State* L, int idx) {
    const int n = lua_gettop(L);
    if (n >= idx + 2 && lua_isnumber(L, idx) && lua_isnumber(L, idx + 1) &&
        lua_isnumber(L, idx + 2)) {
        int r = static_cast<int>(luaL_checkinteger(L, idx));
        int g = static_cast<int>(luaL_checkinteger(L, idx + 1));
        int b = static_cast<int>(luaL_checkinteger(L, idx + 2));
        if (r < 0) r = 0;
        if (r > 255) r = 255;
        if (g < 0) g = 0;
        if (g > 255) g = 255;
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        return GameDisplay::rgb(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                static_cast<uint8_t>(b));
    }
    return static_cast<uint16_t>(luaL_checkinteger(L, idx));
}
