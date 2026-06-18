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

/** 現在ゲーム実行中の LuaInterpreter ポインタを返す */
LuaInterpreter* luaApiActiveInterpreter() { return g_active_interpreter; }

/** ゲーム実行中に使う LuaInterpreter を登録する */
void luaApiSetActiveInterpreter(LuaInterpreter* interp) { g_active_interpreter = interp; }

/** アクティブ interpreter に紐づく GameDisplay を返す。未登録なら nullptr */
GameDisplay* luaApiActiveDisplay() {
    if (!g_active_interpreter) {
        return nullptr;
    }
    return g_active_interpreter->hostHooks().display;
}

/** Lua スタック上の色引数を RGB565 に変換する（整数1個 or R,G,B 3個） */
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
