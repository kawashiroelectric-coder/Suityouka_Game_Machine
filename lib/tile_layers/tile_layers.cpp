// ============================================
// ファイル: tile_layers.cpp
// タイル背景レイヤーのバンド合成
// ============================================

#include "tile_layers.hpp"

#include <cstdlib>
#include <cstring>

#include "config.hpp"
#include "game_display.hpp"

namespace {

int floorDiv(int a, int b) {
    if (b == 0) return 0;
    int q = a / b;
    int r = a % b;
    if (r != 0 && ((r > 0) != (b > 0))) {
        q--;
    }
    return q;
}

}  // namespace

TileLayerSystem::TileLayerSystem() : backdrop_color_(0) {
    reset();
}

TileLayerSystem::~TileLayerSystem() {
    reset();
}

void TileLayerSystem::freeLayerTiles(Layer& layer) {
    free(layer.tiles);
    layer.tiles = nullptr;
    layer.tile_count = 0;
}

void TileLayerSystem::reset() {
    for (Layer& layer : layers_) {
        freeLayerTiles(layer);
        layer = Layer{};
    }
    backdrop_color_ = 0;
}

bool TileLayerSystem::setLayerConfig(size_t layer_index, int tileset_id, int tile_w, int tile_h,
                                     int sheet_cols, int map_cols, int map_rows, int map_x,
                                     int map_y, int scroll_x, int scroll_y, bool enabled,
                                     bool key_enabled, uint16_t key_color) {
    if (layer_index >= kLayerCount) {
        return false;
    }
    if (tile_w <= 0 || tile_h <= 0 || sheet_cols <= 0 || map_cols <= 0 || map_rows <= 0) {
        return false;
    }
    const size_t cells = static_cast<size_t>(map_cols) * static_cast<size_t>(map_rows);
    if (cells == 0 || cells > kMaxCells) {
        return false;
    }

    Layer& layer = layers_[layer_index];
    layer.configured = true;
    layer.enabled = enabled;
    layer.tileset_id = tileset_id;
    layer.tile_w = tile_w;
    layer.tile_h = tile_h;
    layer.sheet_cols = sheet_cols;
    layer.map_cols = map_cols;
    layer.map_rows = map_rows;
    layer.map_x = map_x;
    layer.map_y = map_y;
    layer.scroll_x = scroll_x;
    layer.scroll_y = scroll_y;
    layer.key_enabled = key_enabled;
    layer.key_color = key_color;

    if (layer.tile_count != cells) {
        int16_t* next = static_cast<int16_t*>(malloc(cells * sizeof(int16_t)));
        if (!next) {
            return false;
        }
        for (size_t i = 0; i < cells; i++) {
            next[i] = 0;
        }
        freeLayerTiles(layer);
        layer.tiles = next;
        layer.tile_count = cells;
    }
    return true;
}

bool TileLayerSystem::setLayerScroll(size_t layer_index, int scroll_x, int scroll_y) {
    if (layer_index >= kLayerCount) {
        return false;
    }
    layers_[layer_index].scroll_x = scroll_x;
    layers_[layer_index].scroll_y = scroll_y;
    return true;
}

bool TileLayerSystem::setLayerEnabled(size_t layer_index, bool enabled) {
    if (layer_index >= kLayerCount) {
        return false;
    }
    layers_[layer_index].enabled = enabled;
    return true;
}

void TileLayerSystem::clearLayer(size_t layer_index) {
    if (layer_index >= kLayerCount) {
        return;
    }
    freeLayerTiles(layers_[layer_index]);
    layers_[layer_index] = Layer{};
}

bool TileLayerSystem::setLayerTiles(size_t layer_index, const int* data, size_t count) {
    if (layer_index >= kLayerCount || !data) {
        return false;
    }
    Layer& layer = layers_[layer_index];
    if (!layer.configured || layer.tile_count == 0 || count != layer.tile_count) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        layer.tiles[i] = static_cast<int16_t>(data[i]);
    }
    return true;
}

void TileLayerSystem::composeBand(GameDisplay* disp,
                                  const TileLayerImageView* (*get_image)(int id, void* ctx),
                                  void* ctx) {
    if (!disp || !get_image) {
        return;
    }

    disp->clear(backdrop_color_);

    const int band_top = disp->bandTopY();
    const int band_bottom = disp->bandBottomY();
    const int screen_w = static_cast<int>(disp->width());

    for (size_t li = 0; li < kLayerCount; li++) {
        const Layer& layer = layers_[li];
        if (!layer.configured || !layer.enabled || !layer.tiles || layer.tile_count == 0) {
            continue;
        }
        if (layer.tileset_id < 0) {
            continue;
        }

        const TileLayerImageView* slot = get_image(layer.tileset_id, ctx);
        if (!slot || !slot->pixels) {
            continue;
        }

        const int tile_w = layer.tile_w;
        const int tile_h = layer.tile_h;

        const int start_tile_row = floorDiv(band_top - layer.map_y + layer.scroll_y, tile_h);
        const int end_tile_row =
            floorDiv(band_bottom - 1 - layer.map_y + layer.scroll_y, tile_h);

        for (int tile_row = start_tile_row; tile_row <= end_tile_row; tile_row++) {
            const int ty = layer.map_y + tile_row * tile_h - layer.scroll_y;
            if (!disp->rectIntersectsBand(ty, tile_h)) {
                continue;
            }

            const int start_tile_col = floorDiv(0 - layer.map_x + layer.scroll_x, tile_w);
            const int end_tile_col =
                floorDiv(screen_w - 1 - layer.map_x + layer.scroll_x, tile_w);

            for (int tile_col = start_tile_col; tile_col <= end_tile_col; tile_col++) {
                if (tile_col < 0 || tile_col >= layer.map_cols || tile_row < 0 ||
                    tile_row >= layer.map_rows) {
                    continue;
                }

                const size_t map_idx =
                    static_cast<size_t>(tile_row) * static_cast<size_t>(layer.map_cols) +
                    static_cast<size_t>(tile_col);
                if (map_idx >= layer.tile_count) {
                    continue;
                }

                const int16_t raw = layer.tiles[map_idx];
                if (raw <= 0) {
                    continue;
                }
                const int tile_index = static_cast<int>(raw - 1);

                const int tx = layer.map_x + tile_col * tile_w - layer.scroll_x;
                disp->drawTileKeyed(tx, ty, tile_w, tile_h, layer.sheet_cols, slot->pixels,
                                    slot->width, slot->height, tile_index, layer.key_color,
                                    layer.key_enabled);
            }
        }
    }
}
