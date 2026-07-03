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

/** 1 ビットフレームバッファ（ヒープ確保。未確保時は nullptr） */
uint8_t* bwFrameBuf();
bool bwFrameBufEnsure(uint16_t w, uint16_t h);
void bwFrameBufRelease(bool use_free);
bool bwFrameIsValid();

/** 1 ビットバッファの src_y0 行から rows 行分を RGB565 に展開 */
void expandBwBufferChunk(const uint8_t* frame, uint16_t w, int src_y0, int rows, uint16_t* dst,
                       uint16_t fg, uint16_t bg);

/** 1 ビットバッファを全画面 RGB565 に展開（draw_bw_pack 用） */
void expandBwBufferFull(const uint8_t* frame, uint16_t w, uint16_t h, uint16_t* dst, uint16_t fg,
                        uint16_t bg);

/** BWPK 用 RGB565 フルフレームバッファ（draw_bw_pack 専用・HeapBudget 優先の単一バッファ） */
bool bwPackRgbBufEnsure(uint16_t w, uint16_t h);
void bwPackRgbBufRelease(bool use_free);
void bwPackRgbBufResetPipeline();
bool bwPackRgbIsReady();

bool bwPackRgbHasDisplayFrame(int frame_index_1based);
bool bwPackRgbTrySwapToFrame(int frame_index_1based);
bool bwPackRgbLoadDisplayFrame(FIL* file, int frame_index_1based, uint32_t frame_count,
                               uint32_t data_base, uint16_t w, uint16_t h, uint16_t fg,
                               uint16_t bg, int* inout_bit_buffer_frame);
bool bwPackRgbPrefetchNextFrame(FIL* file, int current_frame_1based, uint32_t frame_count,
                                uint32_t data_base, uint16_t w, uint16_t h, uint16_t fg,
                                uint16_t bg, int* inout_bit_buffer_frame);
const uint16_t* bwPackRgbDisplayPixels(uint16_t w, uint16_t h);

/** SD 上の 1 ビットフレーム（フル／差分／スキップ）を frame へ適用する */
bool loadBwFrameFromSd(FIL* file, FSIZE_t file_size, uint8_t* frame, uint16_t w, uint16_t h,
                       bool seek_start = true);

/** BWPK パックのヘッダを検証し frame_count / data_base を返す */
bool bwPackReadHeader(FIL* file, uint32_t* out_frame_count, uint32_t* out_data_base);

/** BWPK 内の 1 フレーム（1 始まり）を frame へ適用する */
bool loadBwPackFrameFromSd(FIL* file, uint32_t frame_index_1based, uint32_t frame_count,
                           uint32_t data_base, uint8_t* frame, uint16_t w, uint16_t h);

/** 差分連鎖を保ったまま target フレームまで適用（フレーム飛ばし時は中間を順に適用） */
bool syncBwPackToFrame(FIL* file, uint32_t target_frame, uint32_t frame_count,
                       uint32_t data_base, uint8_t* frame, uint16_t w, uint16_t h,
                       int* inout_buffer_frame);

/** バンド index から画面上端 y（論理座標）を返す */
int bgBandTopY(int band_index);
/** バンド index から画面下端 y（論理座標、排他的）を返す */
int bgBandBottomY(int band_index);

/** 画像矩形とバンドの交差部分を求める。交差なしなら false */
bool bgStreamBandRegion(int band_index, int dx, int dy, uint16_t w, uint16_t h, int* draw_top,
                        int* rows, int* src_y0);

/** SD 上 RGB565 画像の src_y0 行から rows 行分を dst 先頭へ読み込む（f_lseek + f_read） */
bool readBgStreamChunk(FIL* file, uint16_t w, int src_y0, int rows, uint16_t* dst);

/** 1 ビット／行（MSB=左端）フレームのバイト数 */
size_t bwFrameByteSize(uint16_t w, uint16_t h);

/** SD 上 1 ビット画像の src_y0 行から rows 行分を読み、RGB565 に展開して dst へ書く */
bool readBwStreamChunk(FIL* file, uint16_t w, int src_y0, int rows, uint16_t* dst, uint16_t fg,
                       uint16_t bg);

#endif  // BG_STREAM_UTIL_HPP
