// ============================================
// ファイル: bg_stream_util.cpp
// ============================================

#include "bg_stream_util.hpp"

#include <cstddef>
#include <cstring>

uint16_t g_bg_stream_buf[2][GameConfig::BUFFER_WIDTH * GameConfig::BUFFER_HEIGHT];

int bgBandTopY(int band_index) {
    return band_index * static_cast<int>(GameConfig::BUFFER_HEIGHT);
}

int bgBandBottomY(int band_index) {
    const int top = bgBandTopY(band_index);
    const int remaining = static_cast<int>(GameConfig::SCREEN_HEIGHT) - top;
    if (remaining <= 0) {
        return top;
    }
    if (remaining > static_cast<int>(GameConfig::BUFFER_HEIGHT)) {
        return top + static_cast<int>(GameConfig::BUFFER_HEIGHT);
    }
    return top + remaining;
}

bool bgStreamBandRegion(int band_index, int dx, int dy, uint16_t w, uint16_t h, int* draw_top,
                        int* rows, int* src_y0) {
    (void)dx;
    const int band_top = bgBandTopY(band_index);
    const int band_bottom = bgBandBottomY(band_index);
    const int img_top = dy;
    const int img_bottom = dy + static_cast<int>(h);
    const int top = img_top > band_top ? img_top : band_top;
    const int bottom = img_bottom < band_bottom ? img_bottom : band_bottom;
    if (top >= bottom) {
        return false;
    }
    *draw_top = top;
    *rows = bottom - top;
    *src_y0 = top - dy;
    return true;
}

bool readBgStreamChunk(FIL* file, uint16_t w, int src_y0, int rows, uint16_t* dst) {
    if (!file || !dst || w == 0 || rows <= 0) {
        return false;
    }
    const size_t row_bytes = static_cast<size_t>(w) * 2u;
    const size_t chunk = row_bytes * static_cast<size_t>(rows);
    if (chunk > sizeof(g_bg_stream_buf[0])) {
        return false;
    }
    const FSIZE_t offset = static_cast<FSIZE_t>(src_y0) * static_cast<FSIZE_t>(row_bytes);
    if (f_lseek(file, offset) != FR_OK) {
        return false;
    }
    UINT br = 0;
    if (f_read(file, dst, static_cast<UINT>(chunk), &br) != FR_OK ||
        br != static_cast<UINT>(chunk)) {
        return false;
    }
    return true;
}
