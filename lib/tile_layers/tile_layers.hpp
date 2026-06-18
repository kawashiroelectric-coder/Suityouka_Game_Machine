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

    /** 全レイヤー状態を初期化する。インスタンス生成時に使う */
    TileLayerSystem();
    /** 全レイヤーのメモリを解放する。インスタンス破棄時に使う */
    ~TileLayerSystem();

    /** 全レイヤーと背景色を初期状態に戻す。ゲーム終了時に呼ぶ */
    void reset();

    /** レイヤーのタイルセット・マップ寸法・スクロール等を設定する。Lua layers API から呼ぶ */
    bool setLayerConfig(size_t layer_index, int tileset_id, int tile_w, int tile_h, int sheet_cols,
                        int map_cols, int map_rows, int map_x, int map_y, int scroll_x, int scroll_y,
                        bool enabled, bool key_enabled, uint16_t key_color);

    /** レイヤーのスクロールオフセットを更新する。カメラ移動時に呼ぶ */
    bool setLayerScroll(size_t layer_index, int scroll_x, int scroll_y);
    /** レイヤーの有効／無効を切り替える。表示オンオフ時に呼ぶ */
    bool setLayerEnabled(size_t layer_index, bool enabled);
    /** 指定レイヤーを解放して未設定状態に戻す。レイヤー削除時に呼ぶ */
    void clearLayer(size_t layer_index);

    /** タイルマップデータを一括設定する（1 始まり番号、0 以下は空）。マップ更新時に呼ぶ */
    bool setLayerTiles(size_t layer_index, const int* data, size_t count);

    /** 合成前の背景色を設定する。レイヤー初期化時に呼ぶ */
    void setBackdropColor(uint16_t color) { backdrop_color_ = color; }
    /** 現在の背景色を返す。デバッグや再描画時に使う */
    uint16_t backdropColor() const { return backdrop_color_; }

    /** 現在バンドへ下層→上層の順に合成する。各バンドの game_draw 前に呼ぶ */
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

    /** 1 レイヤーのタイル配列を解放する。レイヤー再構成時の内部処理で使う */
    void freeLayerTiles(Layer& layer);

    Layer layers_[kLayerCount];
    uint16_t backdrop_color_;
};

#endif  // TILE_LAYERS_HPP
