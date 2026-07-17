// ============================================
// ファイル: lua_api_machine.cpp
// print / sleep_ms / 入力・ヒープ・パス / machine テーブル登録
// ============================================

#include "lua_api_internal.hpp"

#include <cstdio>

#include "font_renderer.hpp"
#include "game_display.hpp"
#include "heap_budget.hpp"
#include "lua_interpreter.hpp"
#include "pico/stdlib.h"
#include "st7789_lcd.hpp"

extern "C" {
#include "ff.h"
#include "lua.h"
#include "lauxlib.h"
}

/** Lua バインディング: print — 引数をタブ区切りでシリアル出力する */
int luaHostPrint(lua_State* L) {
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        if (i > 1) {
            printf("\t");
        }
        const char* s = luaL_tolstring(L, i, nullptr);
        if (s) {
            printf("%s", s);
        }
        lua_pop(L, 1);
    }
    printf("\n");
    fflush(stdout);
    return 0;
}

/** Lua バインディング: sleep_ms — 指定ミリ秒だけブロッキング待機する */
int luaHostSleepMs(lua_State* L) {
    lua_Integer ms = luaL_checkinteger(L, 1);
    if (ms < 0) {
        ms = 0;
    }
    sleep_ms(static_cast<uint32_t>(ms));
    return 0;
}

/** Lua バインディング: machine.text — 画面上にテキストを描画する */
int luaHostLcdText(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return 0;
    }
    const LuaHostHooks& hooks = interp->hostHooks();

    const lua_Integer x = luaL_checkinteger(L, 1);
    const lua_Integer y = luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    const uint16_t fg = (lua_gettop(L) >= 4) ? luaApiParseColor(L, 4) : Color::WHITE;
    const bool use_bg = (lua_gettop(L) >= 5);
    const uint16_t bg = use_bg ? luaApiParseColor(L, 5) : 0;

    if (interp->drawCommands().isRecording()) {
        interp->drawCommands().recText(static_cast<int>(x), static_cast<int>(y), text, fg, bg,
                                       use_bg);
        return 0;
    }
    if (hooks.display) {
        hooks.display->drawTextBg(static_cast<int>(x), static_cast<int>(y), text, fg, bg, use_bg);
        return 0;
    }
    if (hooks.draw_text_bg) {
        hooks.draw_text_bg(hooks.user_data, static_cast<int>(x), static_cast<int>(y), text, fg, bg);
    }
    return 0;
}

/** Lua バインディング: machine.pressed — ボタン index が押されているか返す */
int luaHostButtonPressed(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const LuaHostHooks& hooks = interp->hostHooks();
    const lua_Integer idx = luaL_checkinteger(L, 1);
    bool pressed = false;
    if (hooks.is_button_pressed) {
        pressed = hooks.is_button_pressed(hooks.user_data, static_cast<int>(idx));
    }
    lua_pushboolean(L, pressed);
    return 1;
}

/** Lua バインディング: machine.jump_pressed — ジャンプ用ボタンのいずれかが押されているか返す */
int luaHostJumpPressed(lua_State* L) {
    (void)L;
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const LuaHostHooks& hooks = interp->hostHooks();
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

/** Lua バインディング: machine.set_present_mode — 互換用ノオペ（現行実装では未使用） */
int luaHostSetPresentMode(lua_State* L) {
    (void)L;
    return 0;
}

/** Lua バインディング: machine.present — 互換用ノオペ（現行実装では未使用） */
int luaHostPresent(lua_State* L) {
    (void)L;
    return 0;
}

/** Lua バインディング: machine.width — 画面幅（ピクセル）を返す */
int luaHostWidth(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    lua_pushinteger(L, disp ? disp->width() : 0);
    return 1;
}

/** Lua バインディング: machine.height — 画面高さ（ピクセル）を返す */
int luaHostHeight(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    lua_pushinteger(L, disp ? disp->height() : 0);
    return 1;
}

/** Lua バインディング: machine.time_ms — 起動からの経過ミリ秒を返す */
int luaHostTimeMs(lua_State* L) {
    (void)L;
    lua_pushinteger(L, static_cast<lua_Integer>(to_ms_since_boot(get_absolute_time())));
    return 1;
}

/** Lua バインディング: machine.rgb — R,G,B から RGB565 色値を返す */
int luaHostRgb(lua_State* L) {
    const int r = static_cast<int>(luaL_checkinteger(L, 1));
    const int g = static_cast<int>(luaL_checkinteger(L, 2));
    const int b = static_cast<int>(luaL_checkinteger(L, 3));
    lua_pushinteger(L, GameDisplay::rgb(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                        static_cast<uint8_t>(b)));
    return 1;
}

/** Lua バインディング: machine.heap_used — ヒープ使用量（バイト）を返す */
int luaHostHeapUsed(lua_State* L) {
    (void)L;
    lua_pushinteger(L, static_cast<lua_Integer>(HeapBudget::used()));
    return 1;
}

/** Lua バインディング: machine.heap_available — ヒープ残量（バイト）を返す */
int luaHostHeapAvailable(lua_State* L) {
    (void)L;
    lua_pushinteger(L, static_cast<lua_Integer>(HeapBudget::available()));
    return 1;
}

/** Lua バインディング: machine.load_font — SD からゲーム用フォントを読み込む */
int luaHostLoadFont(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* path = luaL_checkstring(L, 1);
    if (interp->loadFont(path)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, "load_font failed (see serial log)");
    return 2;
}

/** Lua バインディング: machine.font_loaded — フォントが読み込み済みか返す */
int luaHostFontLoaded(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const FontRenderer* font = interp->fontRenderer();
    lua_pushboolean(L, font && font->isLoaded());
    return 1;
}

/** Lua バインディング: machine.font_height — スケール適用後のフォント高さを返す */
int luaHostFontHeight(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        lua_pushinteger(L, 8);
        return 1;
    }
    const FontRenderer* font = interp->fontRenderer();
    lua_pushinteger(L, font && font->isLoaded() ? font->scaledGlyphHeight() : 8);
    return 1;
}

/** Lua バインディング: machine.font_advance — スケール適用後のデフォルト字送りを返す */
int luaHostFontAdvance(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        lua_pushinteger(L, 8);
        return 1;
    }
    const FontRenderer* font = interp->fontRenderer();
    lua_pushinteger(L, font && font->isLoaded() ? font->scaledDefaultAdvance() : 8);
    return 1;
}

/** Lua バインディング: machine.set_font_scale — フォントの拡大率（分子/分母）を設定する */
int luaHostSetFontScale(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const int num = static_cast<int>(luaL_checkinteger(L, 1));
    const int den = lua_isnoneornil(L, 2) ? 1 : static_cast<int>(luaL_checkinteger(L, 2));
    if (num <= 0 || den <= 0 || num > 255 || den > 255) {
        return luaL_error(L, "font scale must be 1..255 (num, den)");
    }
    FontRenderer* font = interp->fontRenderer();
    if (!font) {
        return luaL_error(L, "no font renderer");
    }
    font->setScale(static_cast<uint8_t>(num), static_cast<uint8_t>(den));
    return 0;
}

/** Lua バインディング: machine.load_return — SD の .lua を実行し return 値を返す */
int luaHostLoadReturn(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* path = luaL_checkstring(L, 1);
    if (interp->pushLoadReturnFromSd(L, path)) {
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, interp->lastError());
    return 2;
}

/** Lua バインディング: machine.script_dir — 実行中ゲームのスクリプトディレクトリを返す */
int luaHostScriptDir(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        lua_pushstring(L, "/");
        return 1;
    }
    const char* dir = interp->gameScriptDir();
    lua_pushstring(L, (dir && dir[0] != '\0') ? dir : "/");
    return 1;
}

/** Lua バインディング: machine.resolve_path — 相対パスを SD 絶対パスに解決して返す */
int luaHostResolvePath(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* path = luaL_checkstring(L, 1);
    char norm[FF_LFN_BUF + 4];
    interp->resolveGamePath(path, norm, sizeof(norm));
    lua_pushstring(L, norm);
    return 1;
}

/** Lua バインディング: machine.file_exists — SD 上にファイルが存在するか返す */
int luaHostFileExists(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* path = luaL_checkstring(L, 1);
    lua_pushboolean(L, interp->sdFileExists(path));
    return 1;
}

/** Lua バインディング: machine.save_data — Lua テーブルを SD に保存する */
int luaHostSaveData(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* path = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    if (interp->saveDataToSd(L, 2, path)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, interp->lastError());
    return 2;
}

/** Lua バインディング: machine.load_data — SD から Lua テーブルを読み込む */
int luaHostLoadData(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* path = luaL_checkstring(L, 1);
    if (interp->loadDataFromSd(L, path)) {
        return 1;
    }
    lua_pushnil(L);
    lua_pushstring(L, interp->lastError());
    return 2;
}

/** print / sleep_ms / machine.* を Lua グローバルに登録する */
void luaRegisterHostApi(lua_State* L) {
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
    lua_pushcfunction(L, luaHostFillRectAlpha);
    lua_setfield(L, -2, "fill_rect_alpha");
    lua_pushcfunction(L, luaHostFillRects);
    lua_setfield(L, -2, "fill_rects");
    lua_pushcfunction(L, luaHostDrawLine);
    lua_setfield(L, -2, "draw_line");
    lua_pushcfunction(L, luaHostDrawCircle);
    lua_setfield(L, -2, "draw_circle");
    lua_pushcfunction(L, luaHostFillCircle);
    lua_setfield(L, -2, "fill_circle");
    lua_pushcfunction(L, luaHostBandIndex);
    lua_setfield(L, -2, "band_index");
    lua_pushcfunction(L, luaHostBandCount);
    lua_setfield(L, -2, "band_count");
    lua_pushcfunction(L, luaHostBandTop);
    lua_setfield(L, -2, "band_top");
    lua_pushcfunction(L, luaHostBandBottom);
    lua_setfield(L, -2, "band_bottom");
    lua_pushcfunction(L, luaHostBandHeight);
    lua_setfield(L, -2, "band_height");
    lua_pushcfunction(L, luaHostRectInBand);
    lua_setfield(L, -2, "rect_in_band");
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
    lua_pushcfunction(L, luaHostDrawImageKeyed);
    lua_setfield(L, -2, "draw_image_keyed");
    lua_pushcfunction(L, luaHostDrawImageAffine);
    lua_setfield(L, -2, "draw_image_affine");
    lua_pushcfunction(L, luaHostDrawImageXform);
    lua_setfield(L, -2, "draw_image_xform");
    lua_pushcfunction(L, luaHostDrawBgStream);
    lua_setfield(L, -2, "draw_bg_stream");
    lua_pushcfunction(L, luaHostDrawBwStream);
    lua_setfield(L, -2, "draw_bw_stream");
    lua_pushcfunction(L, luaHostDrawBwPack);
    lua_setfield(L, -2, "draw_bw_pack");
    lua_pushcfunction(L, luaHostDrawVnStream);
    lua_setfield(L, -2, "draw_vn_stream");
    lua_pushcfunction(L, luaHostFreeImage);
    lua_setfield(L, -2, "free_image");
    lua_pushcfunction(L, luaHostImageSize);
    lua_setfield(L, -2, "image_size");
    lua_pushcfunction(L, luaHostLoadSprite);
    lua_setfield(L, -2, "load_sprite");
    lua_pushcfunction(L, luaHostDrawSprite);
    lua_setfield(L, -2, "draw_sprite");
    lua_pushcfunction(L, luaHostDrawSpriteKeyed);
    lua_setfield(L, -2, "draw_sprite_keyed");
    lua_pushcfunction(L, luaHostDrawImageAffine);
    lua_setfield(L, -2, "draw_sprite_affine");
    lua_pushcfunction(L, luaHostDrawImageXform);
    lua_setfield(L, -2, "draw_sprite_xform");
    lua_pushcfunction(L, luaHostFreeSprite);
    lua_setfield(L, -2, "free_sprite");
    lua_pushcfunction(L, luaHostDrawTilemap);
    lua_setfield(L, -2, "draw_tilemap");
    lua_pushcfunction(L, luaHostSetDrawMode);
    lua_setfield(L, -2, "set_draw_mode");
    lua_pushcfunction(L, luaHostDrawMode);
    lua_setfield(L, -2, "draw_mode");
    lua_pushcfunction(L, luaHostLayerCount);
    lua_setfield(L, -2, "layer_count");
    lua_pushcfunction(L, luaHostSetLayerBackdrop);
    lua_setfield(L, -2, "set_layer_backdrop");
    lua_pushcfunction(L, luaHostSetLayer);
    lua_setfield(L, -2, "set_layer");
    lua_pushcfunction(L, luaHostSetLayerScroll);
    lua_setfield(L, -2, "set_layer_scroll");
    lua_pushcfunction(L, luaHostSetLayerTiles);
    lua_setfield(L, -2, "set_layer_tiles");
    lua_pushcfunction(L, luaHostClearLayer);
    lua_setfield(L, -2, "clear_layer");
    lua_pushcfunction(L, luaHostClearAllLayers);
    lua_setfield(L, -2, "clear_all_layers");
    lua_pushcfunction(L, luaHostPlayTone);
    lua_setfield(L, -2, "play_tone");
    lua_pushcfunction(L, luaHostPlayWav);
    lua_setfield(L, -2, "play_wav");
    lua_pushcfunction(L, luaHostPlaySe);
    lua_setfield(L, -2, "play_se");
    lua_pushcfunction(L, luaHostStopSound);
    lua_setfield(L, -2, "stop_sound");
    lua_pushcfunction(L, luaHostSetVolume);
    lua_setfield(L, -2, "set_volume");
    lua_pushcfunction(L, luaHostHeapUsed);
    lua_setfield(L, -2, "heap_used");
    lua_pushcfunction(L, luaHostHeapAvailable);
    lua_setfield(L, -2, "heap_available");
    lua_pushcfunction(L, luaHostLoadFont);
    lua_setfield(L, -2, "load_font");
    lua_pushcfunction(L, luaHostFontLoaded);
    lua_setfield(L, -2, "font_loaded");
    lua_pushcfunction(L, luaHostFontHeight);
    lua_setfield(L, -2, "font_height");
    lua_pushcfunction(L, luaHostFontAdvance);
    lua_setfield(L, -2, "font_advance");
    lua_pushcfunction(L, luaHostSetFontScale);
    lua_setfield(L, -2, "set_font_scale");
    lua_pushcfunction(L, luaHostLoadReturn);
    lua_setfield(L, -2, "load_return");
    lua_pushcfunction(L, luaHostScriptDir);
    lua_setfield(L, -2, "script_dir");
    lua_pushcfunction(L, luaHostResolvePath);
    lua_setfield(L, -2, "resolve_path");
    lua_pushcfunction(L, luaHostFileExists);
    lua_setfield(L, -2, "file_exists");
    lua_pushcfunction(L, luaHostSaveData);
    lua_setfield(L, -2, "save_data");
    lua_pushcfunction(L, luaHostLoadData);
    lua_setfield(L, -2, "load_data");
    lua_setglobal(L, "machine");
}

/** luaRegisterHostApi のラッパー（LuaInterpreter から呼ばれる） */
void LuaInterpreter::registerLuaHostApi(lua_State* L) { luaRegisterHostApi(L); }
