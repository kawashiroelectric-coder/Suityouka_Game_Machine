// ============================================
// ファイル: vn_stream_compose.cpp
// VN 用 SD ストリーム合成（背景 + 立ち絵最大2枚）
// ============================================

#include "vn_stream_compose.hpp"

#include <cstdio>
#include <cstring>

#include "bg_stream_util.hpp"
#include "config.hpp"
#include "game_display.hpp"
#include "lua_interpreter.hpp"

extern "C" {
#include "f_util.h"
#include "ff.h"
#include "lauxlib.h"
#include "lua.h"
}

namespace {

/** VN 合成レイヤー用に開いている SD ファイルを閉じる */
void closeVnLayerFile(VnStreamLayer* layer, bool abandon_open_files) {
    if (layer->open) {
        if (abandon_open_files) {
            printf("[MENU-DBG] vn abandon: %s\n", layer->path[0] != '\0' ? layer->path : "(no path)");
            fflush(stdout);
            layer->file = FIL{};
        } else {
            const FRESULT fr = f_close(&layer->file);
            if (fr != FR_OK) {
                printf("draw_vn_stream: close failed %s (%s)\n", layer->path, FRESULT_str(fr));
            }
        }
    }
    layer->open = false;
    layer->path[0] = '\0';
    layer->fail_path[0] = '\0';
    layer->width = 0;
    layer->height = 0;
}

/** VN レイヤーのファイルと描画パラメータを初期状態に戻す */
void resetVnLayer(VnStreamLayer* layer, bool abandon_open_files) {
    closeVnLayerFile(layer, abandon_open_files);
    layer->active = false;
    layer->dx = 0;
    layer->dy = 0;
    layer->key_color = 0xF81F;
    layer->keyed = true;
}

/** Lua テーブルから任意の整数フィールドを読み取る（無ければデフォルト値） */
bool readOptionalIntField(lua_State* L, int tbl, const char* key, int* out, int default_value) {
    lua_getfield(L, tbl, key);
    if (lua_isinteger(L, -1)) {
        *out = static_cast<int>(lua_tointeger(L, -1));
    } else {
        *out = default_value;
    }
    lua_pop(L, 1);
    return true;
}

/** Lua テーブルから任意の RGB565 色フィールドを読み取る */
bool readOptionalColorField(lua_State* L, int tbl, const char* key, uint16_t* out,
                            uint16_t default_value) {
    lua_getfield(L, tbl, key);
    if (lua_isinteger(L, -1)) {
        *out = static_cast<uint16_t>(lua_tointeger(L, -1));
    } else {
        *out = default_value;
    }
    lua_pop(L, 1);
    return true;
}

/** Lua レイヤー指定テーブル（path/x/y/w/h/key）をパースする */
bool parseLayerTable(lua_State* L, int tbl, char* path_out, size_t path_len, int* dx, int* dy,
                     uint16_t* w, uint16_t* h, uint16_t* key_color, bool* keyed) {
    lua_getfield(L, tbl, "path");
    if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    const char* rel = lua_tostring(L, -1);
    if (!rel || rel[0] == '\0') {
        lua_pop(L, 1);
        return false;
    }
    std::strncpy(path_out, rel, path_len - 1);
    path_out[path_len - 1] = '\0';
    lua_pop(L, 1);

    readOptionalIntField(L, tbl, "x", dx, 0);
    readOptionalIntField(L, tbl, "y", dy, 0);

    lua_getfield(L, tbl, "w");
    if (!lua_isinteger(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    *w = static_cast<uint16_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, tbl, "h");
    if (!lua_isinteger(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    *h = static_cast<uint16_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);

    if (*w == 0 || *h == 0) {
        return false;
    }

    readOptionalColorField(L, tbl, "key", key_color, 0xF81F);
    lua_getfield(L, tbl, "keyed");
    if (lua_isboolean(L, -1)) {
        *keyed = lua_toboolean(L, -1);
    } else {
        *keyed = true;
    }
    lua_pop(L, 1);
    return true;
}

/** レイヤー用 SD ファイルを開く。パスやサイズ変更時は再オープンする */
bool ensureLayerOpen(VnStreamLayer* layer, const char* norm, uint16_t w,
                     uint16_t h, const char* tag) {
    if (layer->fail_path[0] != '\0' && std::strcmp(layer->fail_path, norm) == 0) {
        return false;
    }

    const bool changed =
        !layer->open || std::strcmp(layer->path, norm) != 0 || layer->width != w ||
        layer->height != h;
    if (!changed) {
        return true;
    }

    closeVnLayerFile(layer, false);

    const size_t byte_size = static_cast<size_t>(w) * static_cast<size_t>(h) * 2u;
    const FRESULT fr = f_open(&layer->file, norm, FA_READ);
    if (fr != FR_OK) {
        printf("draw_vn_stream: %s open failed %s (%s)\n", tag, norm, FRESULT_str(fr));
        std::strncpy(layer->fail_path, norm, sizeof(layer->fail_path) - 1);
        layer->fail_path[sizeof(layer->fail_path) - 1] = '\0';
        return false;
    }
    if (f_size(&layer->file) < byte_size) {
        printf("draw_vn_stream: %s file too small %s\n", tag, norm);
        f_close(&layer->file);
        std::strncpy(layer->fail_path, norm, sizeof(layer->fail_path) - 1);
        layer->fail_path[sizeof(layer->fail_path) - 1] = '\0';
        return false;
    }

    layer->open = true;
    std::strncpy(layer->path, norm, sizeof(layer->path) - 1);
    layer->path[sizeof(layer->path) - 1] = '\0';
    layer->width = w;
    layer->height = h;
    layer->fail_path[0] = '\0';
    return true;
}

/** パース済みレイヤー仕様を状態に反映し、必要なら SD ファイルを開く */
bool applyLayerSpec(LuaInterpreter* interp, VnStreamLayer* layer,
                    const char* rel_path, int dx, int dy, uint16_t w, uint16_t h,
                    uint16_t key_color, bool keyed, const char* tag) {
    char norm[FF_LFN_BUF + 4];
    interp->resolveGamePath(rel_path, norm, sizeof(norm));

    layer->dx = dx;
    layer->dy = dy;
    layer->key_color = key_color;
    layer->keyed = keyed;

    if (!ensureLayerOpen(layer, norm, w, h, tag)) {
        layer->active = false;
        return false;
    }

    layer->active = true;
    return true;
}

/** 1 レイヤーについて現在バンド分を SD から読み込み描画する */
bool drawLayerBand(GameDisplay* disp, VnStreamLayer* layer, int band_index,
                   uint8_t buf_slot) {
    // readBgStreamChunk でバンド行を buf 先頭に載せ、drawImageSub( sy=0 ) で転送。
    // sy=src_y0 にするとバッファ範囲外参照になるため draw_bg_stream と同じ 0,0 を使う。
    if (!layer->active || !layer->open) {
        return true;
    }
    if (layer->fail_path[0] != '\0') {
        return false;
    }

    int draw_top = 0;
    int rows = 0;
    int src_y0 = 0;
    if (!bgStreamBandRegion(band_index, layer->dx, layer->dy, layer->width, layer->height,
                            &draw_top, &rows, &src_y0)) {
        return true;
    }

    const size_t chunk = static_cast<size_t>(layer->width) * 2u * static_cast<size_t>(rows);
    if (chunk > sizeof(g_bg_stream_buf[0])) {
        printf("draw_vn_stream: band chunk too large (%u)\n", static_cast<unsigned>(chunk));
        return false;
    }

    uint16_t* buf = g_bg_stream_buf[buf_slot & 1u];
    if (!readBgStreamChunk(&layer->file, layer->width, src_y0, rows, buf)) {
        printf("draw_vn_stream: read failed %s\n", layer->path);
        return false;
    }

    const int img_w = static_cast<int>(layer->width);
    if (layer->keyed) {
        disp->drawImageSubKeyed(layer->dx, draw_top, img_w, rows, buf, 0, 0, img_w, rows,
                                layer->key_color, true);
    } else {
        disp->drawImageSub(layer->dx, draw_top, img_w, rows, buf, 0, 0, img_w, rows);
    }
    return true;
}

/** 合成状態の全レイヤーを非アクティブにする */
void deactivateAllLayers(VnStreamComposeState* st) {
    st->bg.active = false;
    for (int i = 0; i < kVnStreamCharLayers; ++i) {
        st->chars[i].active = false;
    }
}

/** Lua テーブル（bg/chars）から VN 合成レイヤー状態を同期する */
bool syncLayersFromTable(LuaInterpreter* interp, VnStreamComposeState* st, lua_State* L,
                         int table_index) {
    deactivateAllLayers(st);
    st->char_count = 0;

    char path_buf[FF_LFN_BUF + 4];
    int dx = 0;
    int dy = 0;
    uint16_t w = 0;
    uint16_t h = 0;
    uint16_t key_color = 0xF81F;
    bool keyed = true;

    lua_getfield(L, table_index, "bg");
    if (lua_istable(L, -1)) {
        if (parseLayerTable(L, lua_gettop(L), path_buf, sizeof(path_buf), &dx, &dy, &w, &h,
                            &key_color, &keyed)) {
            applyLayerSpec(interp, &st->bg, path_buf, dx, dy, w, h, key_color, false, "bg");
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, table_index, "chars");
    if (lua_istable(L, -1)) {
        const int char_tbl = lua_gettop(L);
        const int n = static_cast<int>(lua_rawlen(L, char_tbl));
        const int count = n > kVnStreamCharLayers ? kVnStreamCharLayers : n;
        st->char_count = count;
        for (int i = 0; i < count; ++i) {
            lua_rawgeti(L, char_tbl, i + 1);
            if (lua_istable(L, -1)) {
                char tag[16];
                std::snprintf(tag, sizeof(tag), "char%d", i);
                if (parseLayerTable(L, lua_gettop(L), path_buf, sizeof(path_buf), &dx, &dy, &w, &h,
                                    &key_color, &keyed)) {
                    applyLayerSpec(interp, &st->chars[i], path_buf, dx, dy, w, h, key_color, keyed,
                                   tag);
                }
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    return true;
}

}  // namespace

/** VN 合成用に開いている全 SD ファイルを閉じ、状態をリセットする */
void vnStreamComposeClose(LuaInterpreter* interp, bool abandon_open_files) {
    if (!interp) {
        return;
    }
    VnStreamComposeState* st = &interp->vn_stream_;
    resetVnLayer(&st->bg, abandon_open_files);
    for (int i = 0; i < kVnStreamCharLayers; ++i) {
        resetVnLayer(&st->chars[i], abandon_open_files);
    }
    st->char_count = 0;
}

/** 現在バンド分を背景→立ち絵の順に SD ストリーム合成描画する */
bool vnStreamComposeDraw(LuaInterpreter* interp, lua_State* L, int table_index) {
    if (!interp || !L) {
        return false;
    }
    GameDisplay* disp = interp->hostHooks().display;
    if (!disp) {
        return false;
    }
    if (!lua_istable(L, table_index)) {
        return false;
    }

    VnStreamComposeState* st = &interp->vn_stream_;
    const int abs_idx = lua_absindex(L, table_index);
    syncLayersFromTable(interp, st, L, abs_idx);

    const int band_index = disp->bandIndex();
    const uint8_t buf_slot = static_cast<uint8_t>(band_index & 1);

    if (!drawLayerBand(disp, &st->bg, band_index, buf_slot)) {
        return false;
    }
    for (int i = 0; i < st->char_count; ++i) {
        if (!drawLayerBand(disp, &st->chars[i], band_index, buf_slot)) {
            return false;
        }
    }
    return true;
}

/** machine.draw_vn_stream から呼ばれる VN 合成描画のエントリ */
bool LuaInterpreter::drawVnStreamCompose(lua_State* L, int table_index) {
    if (!sd_mounted_) {
        return false;
    }
    return vnStreamComposeDraw(this, L, table_index);
}

/** vn_stream_ の全 FIL を閉じる（フレーム末・ゲーム終了時） */
void LuaInterpreter::closeVnStreamCompose(bool abandon_open_files) {
    vnStreamComposeClose(this, abandon_open_files);
}
