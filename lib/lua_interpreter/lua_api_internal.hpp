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

/** 現在ゲーム実行中の LuaInterpreter ポインタを返す */
LuaInterpreter* luaApiActiveInterpreter();
/** ゲーム実行中に使う LuaInterpreter を登録する */
void luaApiSetActiveInterpreter(LuaInterpreter* interp);
/** アクティブ interpreter に紐づく GameDisplay を返す */
GameDisplay* luaApiActiveDisplay();
/** Lua スタック上の色引数を RGB565 に変換する */
uint16_t luaApiParseColor(lua_State* L, int idx);

// --- Lua C バインディング（lua_api_*.cpp で実装） ---
/** Lua バインディング: print */
int luaHostPrint(lua_State* L);
/** Lua バインディング: sleep_ms */
int luaHostSleepMs(lua_State* L);
/** Lua バインディング: machine.text */
int luaHostLcdText(lua_State* L);
/** Lua バインディング: machine.pressed */
int luaHostButtonPressed(lua_State* L);
/** Lua バインディング: machine.jump_pressed */
int luaHostJumpPressed(lua_State* L);
/** Lua バインディング: machine.clear */
int luaHostClear(lua_State* L);
/** Lua バインディング: machine.fill_rect */
int luaHostFillRect(lua_State* L);
/** Lua バインディング: machine.fill_rects */
int luaHostFillRects(lua_State* L);
/** Lua バインディング: machine.set_present_mode */
int luaHostSetPresentMode(lua_State* L);
/** Lua バインディング: machine.present */
int luaHostPresent(lua_State* L);
/** Lua バインディング: machine.width */
int luaHostWidth(lua_State* L);
/** Lua バインディング: machine.height */
int luaHostHeight(lua_State* L);
/** Lua バインディング: machine.time_ms */
int luaHostTimeMs(lua_State* L);
/** Lua バインディング: machine.rgb */
int luaHostRgb(lua_State* L);
/** Lua バインディング: machine.load_image */
int luaHostLoadImage(lua_State* L);
/** Lua バインディング: machine.draw_image */
int luaHostDrawImage(lua_State* L);
/** Lua バインディング: machine.draw_image_keyed */
int luaHostDrawImageKeyed(lua_State* L);
/** Lua バインディング: machine.free_image */
int luaHostFreeImage(lua_State* L);
/** Lua バインディング: machine.image_size */
int luaHostImageSize(lua_State* L);
/** Lua バインディング: machine.play_tone */
int luaHostPlayTone(lua_State* L);
/** Lua バインディング: machine.play_wav */
int luaHostPlayWav(lua_State* L);
/** Lua バインディング: machine.play_se */
int luaHostPlaySe(lua_State* L);
/** Lua バインディング: machine.stop_sound */
int luaHostStopSound(lua_State* L);
/** Lua バインディング: machine.set_volume */
int luaHostSetVolume(lua_State* L);
/** Lua バインディング: machine.heap_used */
int luaHostHeapUsed(lua_State* L);
/** Lua バインディング: machine.heap_available */
int luaHostHeapAvailable(lua_State* L);
/** Lua バインディング: machine.band_index */
int luaHostBandIndex(lua_State* L);
/** Lua バインディング: machine.band_count */
int luaHostBandCount(lua_State* L);
/** Lua バインディング: machine.band_top */
int luaHostBandTop(lua_State* L);
/** Lua バインディング: machine.band_bottom */
int luaHostBandBottom(lua_State* L);
/** Lua バインディング: machine.band_height */
int luaHostBandHeight(lua_State* L);
/** Lua バインディング: machine.rect_in_band */
int luaHostRectInBand(lua_State* L);
/** Lua バインディング: machine.draw_line */
int luaHostDrawLine(lua_State* L);
/** Lua バインディング: machine.draw_circle */
int luaHostDrawCircle(lua_State* L);
/** Lua バインディング: machine.fill_circle */
int luaHostFillCircle(lua_State* L);
/** Lua バインディング: machine.draw_bg_stream */
int luaHostDrawBgStream(lua_State* L);
/** Lua バインディング: machine.draw_bw_stream */
int luaHostDrawBwStream(lua_State* L);
/** Lua バインディング: machine.draw_bw_pack */
int luaHostDrawBwPack(lua_State* L);
/** Lua バインディング: machine.draw_vn_stream */
int luaHostDrawVnStream(lua_State* L);
/** Lua バインディング: machine.load_sprite */
int luaHostLoadSprite(lua_State* L);
/** Lua バインディング: machine.draw_sprite */
int luaHostDrawSprite(lua_State* L);
/** Lua バインディング: machine.draw_sprite_keyed */
int luaHostDrawSpriteKeyed(lua_State* L);
/** Lua バインディング: machine.free_sprite */
int luaHostFreeSprite(lua_State* L);
/** Lua バインディング: machine.draw_tilemap */
int luaHostDrawTilemap(lua_State* L);
/** Lua バインディング: machine.set_draw_mode */
int luaHostSetDrawMode(lua_State* L);
/** Lua バインディング: machine.draw_mode */
int luaHostDrawMode(lua_State* L);
/** Lua バインディング: machine.layer_count */
int luaHostLayerCount(lua_State* L);
/** Lua バインディング: machine.set_layer_backdrop */
int luaHostSetLayerBackdrop(lua_State* L);
/** Lua バインディング: machine.set_layer */
int luaHostSetLayer(lua_State* L);
/** Lua バインディング: machine.set_layer_scroll */
int luaHostSetLayerScroll(lua_State* L);
/** Lua バインディング: machine.set_layer_tiles */
int luaHostSetLayerTiles(lua_State* L);
/** Lua バインディング: machine.clear_layer */
int luaHostClearLayer(lua_State* L);
/** Lua バインディング: machine.clear_all_layers */
int luaHostClearAllLayers(lua_State* L);
/** Lua バインディング: machine.load_font */
int luaHostLoadFont(lua_State* L);
/** Lua バインディング: machine.font_loaded */
int luaHostFontLoaded(lua_State* L);
/** Lua バインディング: machine.font_height */
int luaHostFontHeight(lua_State* L);
/** Lua バインディング: machine.font_advance */
int luaHostFontAdvance(lua_State* L);
/** Lua バインディング: machine.set_font_scale */
int luaHostSetFontScale(lua_State* L);
/** Lua バインディング: machine.load_return */
int luaHostLoadReturn(lua_State* L);
/** Lua バインディング: machine.script_dir */
int luaHostScriptDir(lua_State* L);
/** Lua バインディング: machine.resolve_path */
int luaHostResolvePath(lua_State* L);

/** print / sleep_ms / machine.* を Lua に登録する */
void luaRegisterHostApi(lua_State* L);

#endif  // LUA_API_INTERNAL_HPP
