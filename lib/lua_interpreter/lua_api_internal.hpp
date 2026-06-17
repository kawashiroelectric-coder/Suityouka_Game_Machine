// ============================================
// ファイル: lua_api_internal.hpp
// Lua バインディング共通（アクティブ interpreter / 色パース等）
//
// machine.* の実装は lua_api_machine / draw / audio に分割。
// 各 luaHost* は luaApiActiveInterpreter() 経由で LuaInterpreter / GameDisplay に触れる。
// ============================================

#ifndef LUA_API_INTERNAL_HPP
#define LUA_API_INTERNAL_HPP

#include <cstdint>

struct lua_State;
class GameDisplay;
class LuaInterpreter;

LuaInterpreter* luaApiActiveInterpreter();
void luaApiSetActiveInterpreter(LuaInterpreter* interp);
GameDisplay* luaApiActiveDisplay();
uint16_t luaApiParseColor(lua_State* L, int idx);

// --- Lua C バインディング（lua_api_*.cpp で実装） ---
int luaHostPrint(lua_State* L);
int luaHostSleepMs(lua_State* L);
int luaHostLcdText(lua_State* L);
int luaHostButtonPressed(lua_State* L);
int luaHostJumpPressed(lua_State* L);
int luaHostClear(lua_State* L);
int luaHostFillRect(lua_State* L);
int luaHostFillRects(lua_State* L);
int luaHostSetPresentMode(lua_State* L);
int luaHostPresent(lua_State* L);
int luaHostWidth(lua_State* L);
int luaHostHeight(lua_State* L);
int luaHostTimeMs(lua_State* L);
int luaHostRgb(lua_State* L);
int luaHostLoadImage(lua_State* L);
int luaHostDrawImage(lua_State* L);
int luaHostDrawImageKeyed(lua_State* L);
int luaHostFreeImage(lua_State* L);
int luaHostImageSize(lua_State* L);
int luaHostPlayTone(lua_State* L);
int luaHostPlayWav(lua_State* L);
int luaHostPlaySe(lua_State* L);
int luaHostStopSound(lua_State* L);
int luaHostSetVolume(lua_State* L);
int luaHostHeapUsed(lua_State* L);
int luaHostHeapAvailable(lua_State* L);
int luaHostBandIndex(lua_State* L);
int luaHostBandCount(lua_State* L);
int luaHostBandTop(lua_State* L);
int luaHostBandBottom(lua_State* L);
int luaHostBandHeight(lua_State* L);
int luaHostRectInBand(lua_State* L);
int luaHostDrawLine(lua_State* L);
int luaHostDrawCircle(lua_State* L);
int luaHostFillCircle(lua_State* L);
int luaHostDrawBgStream(lua_State* L);
int luaHostDrawVnStream(lua_State* L);
int luaHostLoadSprite(lua_State* L);
int luaHostDrawSprite(lua_State* L);
int luaHostDrawSpriteKeyed(lua_State* L);
int luaHostFreeSprite(lua_State* L);
int luaHostDrawTilemap(lua_State* L);
int luaHostSetDrawMode(lua_State* L);
int luaHostDrawMode(lua_State* L);
int luaHostLayerCount(lua_State* L);
int luaHostSetLayerBackdrop(lua_State* L);
int luaHostSetLayer(lua_State* L);
int luaHostSetLayerScroll(lua_State* L);
int luaHostSetLayerTiles(lua_State* L);
int luaHostClearLayer(lua_State* L);
int luaHostClearAllLayers(lua_State* L);
int luaHostLoadFont(lua_State* L);
int luaHostFontLoaded(lua_State* L);
int luaHostFontHeight(lua_State* L);
int luaHostFontAdvance(lua_State* L);
int luaHostSetFontScale(lua_State* L);
int luaHostLoadReturn(lua_State* L);
int luaHostScriptDir(lua_State* L);
int luaHostResolvePath(lua_State* L);

void luaRegisterHostApi(lua_State* L);

#endif  // LUA_API_INTERNAL_HPP
