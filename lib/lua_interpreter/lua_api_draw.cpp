// ============================================
// ファイル: lua_api_draw.cpp
// machine.* 描画・画像・タイルレイヤー API バインディング
// ============================================

#include "lua_api_internal.hpp"

#include <cstring>

#include "game_display.hpp"
#include "lua_interpreter.hpp"
#include "tile_layers.hpp"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

int luaHostClear(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return 0;
    }
    disp->clear(luaApiParseColor(L, 1));
    return 0;
}

int luaHostFillRect(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return 0;
    }
    const int x = static_cast<int>(luaL_checknumber(L, 1));
    const int y = static_cast<int>(luaL_checknumber(L, 2));
    const int w = static_cast<int>(luaL_checknumber(L, 3));
    const int h = static_cast<int>(luaL_checknumber(L, 4));
    const uint16_t color = luaApiParseColor(L, 5);
    disp->fillRect(x, y, w, h, color);
    return 0;
}

int luaHostFillRects(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return 0;
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    const int n = static_cast<int>(lua_rawlen(L, 1));
    if (n <= 0) {
        return 0;
    }

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
                const int v = static_cast<int>(luaL_checkinteger(L, -1));
                lua_pop(L, 1);
                return v;
            };

            GameDisplay::FillRect r;
            r.x = read_i(1);
            r.y = read_i(2);
            r.w = read_i(3);
            r.h = read_i(4);
            lua_rawgeti(L, t, 5);
            r.color = static_cast<uint16_t>(luaL_checkinteger(L, -1));
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

int luaHostBandIndex(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return luaL_error(L, "no display");
    }
    lua_pushinteger(L, disp->bandIndex());
    return 1;
}

int luaHostBandCount(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return luaL_error(L, "no display");
    }
    lua_pushinteger(L, disp->bandCount());
    return 1;
}

int luaHostBandTop(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return luaL_error(L, "no display");
    }
    lua_pushinteger(L, disp->bandTopY());
    return 1;
}

int luaHostBandBottom(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return luaL_error(L, "no display");
    }
    lua_pushinteger(L, disp->bandBottomY());
    return 1;
}

int luaHostBandHeight(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return luaL_error(L, "no display");
    }
    lua_pushinteger(L, disp->bufferHeight());
    return 1;
}

int luaHostRectInBand(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return luaL_error(L, "no display");
    }
    const int y = static_cast<int>(luaL_checkinteger(L, 1));
    const int h = static_cast<int>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, disp->rectIntersectsBand(y, h));
    return 1;
}

int luaHostDrawLine(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return 0;
    }
    const int x0 = static_cast<int>(luaL_checkinteger(L, 1));
    const int y0 = static_cast<int>(luaL_checkinteger(L, 2));
    const int x1 = static_cast<int>(luaL_checkinteger(L, 3));
    const int y1 = static_cast<int>(luaL_checkinteger(L, 4));
    disp->drawLine(x0, y0, x1, y1, luaApiParseColor(L, 5));
    return 0;
}

int luaHostDrawCircle(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return 0;
    }
    const int cx = static_cast<int>(luaL_checkinteger(L, 1));
    const int cy = static_cast<int>(luaL_checkinteger(L, 2));
    const int r = static_cast<int>(luaL_checkinteger(L, 3));
    disp->drawCircle(cx, cy, r, luaApiParseColor(L, 4));
    return 0;
}

int luaHostFillCircle(lua_State* L) {
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return 0;
    }
    const int cx = static_cast<int>(luaL_checkinteger(L, 1));
    const int cy = static_cast<int>(luaL_checkinteger(L, 2));
    const int r = static_cast<int>(luaL_checkinteger(L, 3));
    disp->fillCircle(cx, cy, r, luaApiParseColor(L, 4));
    return 0;
}

int luaHostLoadImage(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* path = luaL_checkstring(L, 1);
    const int w = static_cast<int>(luaL_checkinteger(L, 2));
    const int h = static_cast<int>(luaL_checkinteger(L, 3));
    if (w <= 0 || h <= 0) {
        return luaL_error(L, "load_image: width/height must be > 0");
    }

    const int id = interp->loadImage(path, static_cast<uint16_t>(w), static_cast<uint16_t>(h));
    if (id < 0) {
        lua_pushnil(L);
        lua_pushstring(L, "load_image failed (see serial log)");
        return 2;
    }
    lua_pushinteger(L, id);
    return 1;
}

int luaHostDrawImage(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return 0;
    }
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return 0;
    }

    const int id = static_cast<int>(luaL_checkinteger(L, 1));
    const int dx = static_cast<int>(luaL_checkinteger(L, 2));
    const int dy = static_cast<int>(luaL_checkinteger(L, 3));

    const ImageSlot* slot = interp->getImage(id);
    if (!slot) {
        return luaL_error(L, "draw_image: invalid image id %d", id);
    }

    if (lua_gettop(L) >= 7) {
        const int sx = static_cast<int>(luaL_checkinteger(L, 4));
        const int sy = static_cast<int>(luaL_checkinteger(L, 5));
        const int sw = static_cast<int>(luaL_checkinteger(L, 6));
        const int sh = static_cast<int>(luaL_checkinteger(L, 7));
        disp->drawImageSub(dx, dy, slot->width, slot->height, slot->pixels, sx, sy, sw, sh);
    } else {
        disp->drawImage(dx, dy, slot->width, slot->height, slot->pixels);
    }
    return 0;
}

int luaHostDrawImageKeyed(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return 0;
    }
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return 0;
    }

    const int id = static_cast<int>(luaL_checkinteger(L, 1));
    const int dx = static_cast<int>(luaL_checkinteger(L, 2));
    const int dy = static_cast<int>(luaL_checkinteger(L, 3));

    const ImageSlot* slot = interp->getImage(id);
    if (!slot) {
        return luaL_error(L, "draw_image_keyed: invalid image id %d", id);
    }

    uint16_t key_color = 0xF81F;
    if (lua_gettop(L) >= 7) {
        const int sx = static_cast<int>(luaL_checkinteger(L, 4));
        const int sy = static_cast<int>(luaL_checkinteger(L, 5));
        const int sw = static_cast<int>(luaL_checkinteger(L, 6));
        const int sh = static_cast<int>(luaL_checkinteger(L, 7));
        if (lua_gettop(L) >= 8) {
            key_color = static_cast<uint16_t>(luaL_checkinteger(L, 8));
        }
        disp->drawImageSubKeyed(dx, dy, slot->width, slot->height, slot->pixels, sx, sy, sw, sh,
                                key_color, true);
    } else {
        if (lua_gettop(L) >= 4) {
            key_color = static_cast<uint16_t>(luaL_checkinteger(L, 4));
        }
        disp->drawImageSubKeyed(dx, dy, slot->width, slot->height, slot->pixels, 0, 0,
                                slot->width, slot->height, key_color, true);
    }
    return 0;
}

int luaHostFreeImage(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return 0;
    }
    const int id = static_cast<int>(luaL_checkinteger(L, 1));
    interp->freeImage(id);
    return 0;
}

int luaHostImageSize(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const int id = static_cast<int>(luaL_checkinteger(L, 1));
    const ImageSlot* slot = interp->getImage(id);
    if (!slot) {
        return luaL_error(L, "image_size: invalid image id %d", id);
    }
    lua_pushinteger(L, slot->width);
    lua_pushinteger(L, slot->height);
    return 2;
}

int luaHostDrawBgStream(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* path = luaL_checkstring(L, 1);
    const int dx = static_cast<int>(luaL_checkinteger(L, 2));
    const int dy = static_cast<int>(luaL_checkinteger(L, 3));
    const int w = static_cast<int>(luaL_checkinteger(L, 4));
    const int h = static_cast<int>(luaL_checkinteger(L, 5));
    if (w <= 0 || h <= 0) {
        return luaL_error(L, "draw_bg_stream: width/height must be > 0");
    }
    const bool ok =
        interp->drawBgStreamFromSd(path, dx, dy, static_cast<uint16_t>(w), static_cast<uint16_t>(h));
    lua_pushboolean(L, ok);
    return 1;
}

int luaHostLoadSprite(lua_State* L) { return luaHostLoadImage(L); }
int luaHostDrawSprite(lua_State* L) { return luaHostDrawImage(L); }
int luaHostDrawSpriteKeyed(lua_State* L) { return luaHostDrawImageKeyed(L); }
int luaHostFreeSprite(lua_State* L) { return luaHostFreeImage(L); }

int luaHostDrawTilemap(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    GameDisplay* disp = luaApiActiveDisplay();
    if (!disp) {
        return 0;
    }

    const int id = static_cast<int>(luaL_checkinteger(L, 1));
    const int map_x = static_cast<int>(luaL_checkinteger(L, 2));
    const int map_y = static_cast<int>(luaL_checkinteger(L, 3));
    const int cols = static_cast<int>(luaL_checkinteger(L, 4));
    const int rows = static_cast<int>(luaL_checkinteger(L, 5));
    const int tile_w = static_cast<int>(luaL_checkinteger(L, 6));
    const int tile_h = static_cast<int>(luaL_checkinteger(L, 7));
    const int sheet_cols = static_cast<int>(luaL_checkinteger(L, 8));
    luaL_checktype(L, 9, LUA_TTABLE);

    if (cols <= 0 || rows <= 0 || tile_w <= 0 || tile_h <= 0 || sheet_cols <= 0) {
        return luaL_error(L, "draw_tilemap: invalid size");
    }
    static constexpr int kMaxCells = 2048;
    if (cols * rows > kMaxCells) {
        return luaL_error(L, "draw_tilemap: map too large (max %d cells)", kMaxCells);
    }

    const ImageSlot* slot = interp->getImage(id);
    if (!slot) {
        return luaL_error(L, "draw_tilemap: invalid sprite id %d", id);
    }

    const int expected = cols * rows;
    const int n = static_cast<int>(lua_rawlen(L, 9));
    if (n < expected) {
        return luaL_error(L, "draw_tilemap: data needs %d entries", expected);
    }

    for (int row = 0; row < rows; row++) {
        const int ty = map_y + row * tile_h;
        if (!disp->rectIntersectsBand(ty, tile_h)) {
            continue;
        }
        for (int col = 0; col < cols; col++) {
            const int idx = row * cols + col + 1;
            lua_rawgeti(L, 9, idx);
            if (!lua_isnumber(L, -1)) {
                lua_pop(L, 1);
                continue;
            }
            const int tile = static_cast<int>(lua_tointeger(L, -1));
            lua_pop(L, 1);
            if (tile < 0) {
                continue;
            }
            disp->drawTile(map_x + col * tile_w, ty, tile_w, tile_h, sheet_cols, slot->pixels,
                           slot->width, slot->height, tile);
        }
    }
    return 0;
}

int luaHostSetDrawMode(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* mode = luaL_checkstring(L, 1);
    if (strcmp(mode, "direct") == 0) {
        interp->setDrawMode(LuaDrawMode::Direct);
    } else if (strcmp(mode, "layers") == 0) {
        interp->setDrawMode(LuaDrawMode::Layers);
    } else {
        return luaL_error(L, "set_draw_mode: use \"direct\" or \"layers\"");
    }
    return 0;
}

int luaHostDrawMode(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const char* mode =
        (interp->drawMode() == LuaDrawMode::Layers) ? "layers" : "direct";
    lua_pushstring(L, mode);
    return 1;
}

int luaHostLayerCount(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(TileLayerSystem::kLayerCount));
    return 1;
}

int luaHostSetLayerBackdrop(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    interp->setLayerBackdrop(luaApiParseColor(L, 1));
    return 0;
}

namespace {

bool tableFieldInteger(lua_State* L, int table_idx, const char* key, int* out, bool required) {
    lua_getfield(L, table_idx, key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        if (required) {
            return false;
        }
        return true;
    }
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    *out = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return true;
}

bool tableFieldBool(lua_State* L, int table_idx, const char* key, bool* out) {
    lua_getfield(L, table_idx, key);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    *out = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return true;
}

}  // namespace

int luaHostSetLayer(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const int layer = static_cast<int>(luaL_checkinteger(L, 1));
    if (layer < 0 || layer >= static_cast<int>(TileLayerSystem::kLayerCount)) {
        return luaL_error(L, "set_layer: layer index out of range");
    }
    luaL_checktype(L, 2, LUA_TTABLE);

    int tileset = -1;
    int tile_w = 0;
    int tile_h = 0;
    int sheet_cols = 0;
    int map_cols = 0;
    int map_rows = 0;
    int map_x = 0;
    int map_y = 0;
    int scroll_x = 0;
    int scroll_y = 0;
    bool enabled = true;
    bool key_enabled = false;
    uint16_t key_color = 0;

    if (!tableFieldInteger(L, 2, "tileset", &tileset, true) ||
        !tableFieldInteger(L, 2, "tile_w", &tile_w, true) ||
        !tableFieldInteger(L, 2, "tile_h", &tile_h, true) ||
        !tableFieldInteger(L, 2, "sheet_cols", &sheet_cols, true) ||
        !tableFieldInteger(L, 2, "map_cols", &map_cols, true) ||
        !tableFieldInteger(L, 2, "map_rows", &map_rows, true)) {
        return luaL_error(L, "set_layer: need tileset,tile_w,tile_h,sheet_cols,map_cols,map_rows");
    }

    tableFieldInteger(L, 2, "map_x", &map_x, false);
    tableFieldInteger(L, 2, "map_y", &map_y, false);
    tableFieldInteger(L, 2, "scroll_x", &scroll_x, false);
    tableFieldInteger(L, 2, "scroll_y", &scroll_y, false);
    tableFieldBool(L, 2, "enabled", &enabled);

    lua_getfield(L, 2, "transparent");
    if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) {
        key_enabled = true;
        key_color = 0xF81F;
    } else if (lua_isnumber(L, -1)) {
        key_enabled = true;
        key_color = static_cast<uint16_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);

    TileLayerSystem& layers = interp->tileLayers();
    if (!layers.setLayerConfig(static_cast<size_t>(layer), tileset, tile_w, tile_h, sheet_cols,
                                 map_cols, map_rows, map_x, map_y, scroll_x, scroll_y, enabled,
                                 key_enabled, key_color)) {
        return luaL_error(L, "set_layer: invalid config");
    }
    return 0;
}

int luaHostSetLayerScroll(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const int layer = static_cast<int>(luaL_checkinteger(L, 1));
    const int sx = static_cast<int>(luaL_checkinteger(L, 2));
    const int sy = static_cast<int>(luaL_checkinteger(L, 3));
    if (!interp->tileLayers().setLayerScroll(static_cast<size_t>(layer), sx, sy)) {
        return luaL_error(L, "set_layer_scroll: invalid layer");
    }
    return 0;
}

int luaHostSetLayerTiles(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const int layer = static_cast<int>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);

    TileLayerSystem& layers = interp->tileLayers();
    if (layer < 0 || layer >= static_cast<int>(TileLayerSystem::kLayerCount)) {
        return luaL_error(L, "set_layer_tiles: invalid layer");
    }

    const int n = static_cast<int>(lua_rawlen(L, 2));
    if (n <= 0 || n > static_cast<int>(TileLayerSystem::kMaxCells)) {
        return luaL_error(L, "set_layer_tiles: invalid tile count");
    }

    int tiles[TileLayerSystem::kMaxCells];
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 2, i);
        if (!lua_isnumber(L, -1)) {
            lua_pop(L, 1);
            return luaL_error(L, "set_layer_tiles: entry %d not a number", i);
        }
        tiles[i - 1] = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }

    if (!layers.setLayerTiles(static_cast<size_t>(layer), tiles, static_cast<size_t>(n))) {
        return luaL_error(L, "set_layer_tiles: size mismatch (call set_layer first)");
    }
    return 0;
}

int luaHostClearLayer(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const int layer = static_cast<int>(luaL_checkinteger(L, 1));
    if (layer < 0 || layer >= static_cast<int>(TileLayerSystem::kLayerCount)) {
        return luaL_error(L, "clear_layer: invalid layer");
    }
    interp->tileLayers().clearLayer(static_cast<size_t>(layer));
    return 0;
}

int luaHostClearAllLayers(lua_State* L) {
    LuaInterpreter* interp = luaApiActiveInterpreter();
    if (!interp) {
        return luaL_error(L, "no active interpreter");
    }
    const uint16_t backdrop = interp->layerBackdropColor();
    interp->tileLayers().reset();
    interp->setLayerBackdrop(backdrop);
    return 0;
}
