// ============================================
// ファイル: tile_layers.hpp
// GBA 風タイル背景レイヤー（最大 4 層）の保持とバンド合成
// ============================================

#ifndef TILE_LAYERS_HPP
#define TILE_LAYERS_HPP

#include <cstddef>
#include <cstdint>

#include "config.hpp"

class GameDisplay;

/** 合成時に参照するタイルセット画像 */
struct TileLayerImageView {
    const uint16_t* pixels;
    uint16_t width;
    uint16_t height;
};

/** タイルセット参照付き背景レイヤー */
class TileLayerSystem {
public:
    static constexpr size_t kLayerCount = GameConfig::TILE_LAYER_COUNT;
    static constexpr size_t kMaxCells = GameConfig::TILEMAP_MAX_CELLS;

    TileLayerSystem();
    ~TileLayerSystem();

    void reset();

    bool setLayerConfig(size_t layer_index, int tileset_id, int tile_w, int tile_h, int sheet_cols,
                        int map_cols, int map_rows, int map_x, int map_y, int scroll_x, int scroll_y,
                        bool enabled, bool key_enabled, uint16_t key_color);

    bool setLayerScroll(size_t layer_index, int scroll_x, int scroll_y);
    bool setLayerEnabled(size_t layer_index, bool enabled);
    void clearLayer(size_t layer_index);

    /** data: 1 始まりタイル番号、0 以下は空タイル。expected = map_cols * map_rows */
    bool setLayerTiles(size_t layer_index, const int* data, size_t count);

    void setBackdropColor(uint16_t color) { backdrop_color_ = color; }
    uint16_t backdropColor() const { return backdrop_color_; }

    /** 現在バンドへ下層→上層の順に合成（layers 描画モード用）。
     *  runGameLoopFromSd が各バンドの game_draw 前に呼ぶ。
     *  get_image: load_image スロット id → ピクセル列（LuaInterpreter が提供） */
    void composeBand(GameDisplay* disp, const TileLayerImageView* (*get_image)(int id, void* ctx),
                     void* ctx);

private:
    struct Layer {
        bool configured;
        bool enabled;
        int tileset_id;
        int tile_w;
        int tile_h;
        int sheet_cols;
        int map_cols;
        int map_rows;
        int map_x;
        int map_y;
        int scroll_x;
        int scroll_y;
        bool key_enabled;
        uint16_t key_color;
        int16_t* tiles;
        size_t tile_count;
    };

    void freeLayerTiles(Layer& layer);

    Layer layers_[kLayerCount];
    uint16_t backdrop_color_;
};

#endif  // TILE_LAYERS_HPP
