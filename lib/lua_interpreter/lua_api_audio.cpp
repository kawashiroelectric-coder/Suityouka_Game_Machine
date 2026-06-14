// ============================================
// ファイル: lua_api_audio.cpp
// machine.* 音声 API バインディング
// ============================================

#include "lua_api_internal.hpp"

#include "lua_interpreter.hpp"

extern "C" {
#include "ff.h"
#include "lua.h"
#include "lauxlib.h"
}

int luaHostPlayTone(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const float freq = static_cast<float>(luaL_checknumber(L, 1));
    const float ms = static_cast<float>(luaL_checknumber(L, 2));
    const bool ok = interp->audioEngine().playTone(freq, ms);
    lua_pushboolean(L, ok);
    return 1;
}

int luaHostPlayWav(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* path = luaL_checkstring(L, 1);
    char norm[FF_LFN_BUF + 4];
    interp->resolveGamePath(path, norm, sizeof(norm));
    char err[80];
    const bool ok = interp->audioEngine().playWav(norm, err, sizeof(err));
    if (!ok) {
        lua_pushnil(L);
        lua_pushstring(L, err[0] ? err : "play_wav failed");
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int luaHostPlaySe(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* path = luaL_checkstring(L, 1);
    char norm[FF_LFN_BUF + 4];
    interp->resolveGamePath(path, norm, sizeof(norm));
    char err[80];
    const bool ok = interp->audioEngine().playSe(norm, err, sizeof(err));
    if (!ok) {
        lua_pushnil(L);
        lua_pushstring(L, err[0] ? err : "play_se failed");
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int luaHostStopSound(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    interp->audioEngine().stop();
    return 0;
}

int luaHostSetVolume(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const float vol = static_cast<float>(luaL_checknumber(L, 1));
    interp->audioEngine().setVolume(vol);
    return 0;
}
