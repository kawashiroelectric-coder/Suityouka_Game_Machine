// ============================================
// ファイル: bg_stream_util.hpp
// SD ストリーム描画のバンド計算・行読み込み（draw_bg_stream / draw_vn_stream 共用）
//
// g_bg_stream_buf は 2 スロット（バンド index & 1）で draw_bg_stream の prefetch と
// VN 合成が交互に使う。1 スロット = BUFFER_WIDTH × BUFFER_HEIGHT 画素。
// ============================================

#ifndef BG_STREAM_UTIL_HPP
#define BG_STREAM_UTIL_HPP

#include <cstdint>

#include "config.hpp"

extern "C" {
#include "ff.h"
}

/** draw_bg_stream / draw_vn_stream 用のバンド行バッファ（2 スロット交互） */
extern uint16_t g_bg_stream_buf[2][GameConfig::BUFFER_WIDTH * GameConfig::BUFFER_HEIGHT];

/** バンド index から画面上端 y（論理座標）を返す */
int bgBandTopY(int band_index);
/** バンド index から画面下端 y（論理座標、排他的）を返す */
int bgBandBottomY(int band_index);

/** 画像矩形とバンドの交差部分を求める。交差なしなら false */
bool bgStreamBandRegion(int band_index, int dx, int dy, uint16_t w, uint16_t h, int* draw_top,
                        int* rows, int* src_y0);

/** SD 上 RGB565 画像の src_y0 行から rows 行分を dst 先頭へ読み込む（f_lseek + f_read） */
bool readBgStreamChunk(FIL* file, uint16_t w, int src_y0, int rows, uint16_t* dst);

#endif  // BG_STREAM_UTIL_HPP
