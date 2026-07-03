// ============================================
// ファイル: bg_stream_util.cpp
// ============================================

#include "bg_stream_util.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "heap_budget.hpp"

uint16_t g_bg_stream_buf[2][GameConfig::BUFFER_WIDTH * GameConfig::BUFFER_HEIGHT];

static uint8_t* s_bw_frame_buf = nullptr;
static size_t s_bw_frame_byte_size = 0;
static bool s_bw_frame_valid = false;

/** 1 ビット／行（MSB=左端）フレームのバイト数 */
size_t bwFrameByteSize(uint16_t w, uint16_t h) {
    if (w == 0 || h == 0) {
        return 0;
    }
    const size_t row_bytes = (static_cast<size_t>(w) + 7u) / 8u;
    return row_bytes * static_cast<size_t>(h);
}

uint8_t* bwFrameBuf() { return s_bw_frame_buf; }

bool bwFrameIsValid() { return s_bw_frame_valid && s_bw_frame_buf != nullptr; }

bool bwFrameBufEnsure(uint16_t w, uint16_t h) {
    const size_t need = bwFrameByteSize(w, h);
    if (need == 0) {
        return false;
    }
    if (s_bw_frame_buf && s_bw_frame_byte_size == need) {
        return true;
    }
    bwFrameBufRelease(false);
    void* alloc_ptr = nullptr;
    if (!HeapBudget::tryAlloc(need, &alloc_ptr)) {
        return false;
    }
    s_bw_frame_buf = static_cast<uint8_t*>(alloc_ptr);
    s_bw_frame_byte_size = need;
    std::memset(s_bw_frame_buf, 0, need);
    s_bw_frame_valid = false;
    return true;
}

void bwFrameBufRelease(bool use_free) {
    if (!s_bw_frame_buf) {
        s_bw_frame_byte_size = 0;
        s_bw_frame_valid = false;
        return;
    }
    if (use_free) {
        std::free(s_bw_frame_buf);
    } else {
        HeapBudget::release(s_bw_frame_buf, s_bw_frame_byte_size);
    }
    s_bw_frame_buf = nullptr;
    s_bw_frame_byte_size = 0;
    s_bw_frame_valid = false;
}

/** 1 packed バイト（MSB=左）を最大 8 ピクセルへ展開 */
static void expandBwByteToPixels(uint8_t byte, uint16_t* out, int max_pixels, uint16_t fg,
                                 uint16_t bg) {
    if (max_pixels <= 0) {
        return;
    }
    if (max_pixels >= 8 && byte == 0x00) {
        const uint32_t pair = (static_cast<uint32_t>(bg) << 16) | static_cast<uint32_t>(bg);
        uint32_t* out32 = reinterpret_cast<uint32_t*>(out);
        out32[0] = pair;
        out32[1] = pair;
        out32[2] = pair;
        out32[3] = pair;
        return;
    }
    if (max_pixels >= 8 && byte == 0xFF) {
        const uint32_t pair = (static_cast<uint32_t>(fg) << 16) | static_cast<uint32_t>(fg);
        uint32_t* out32 = reinterpret_cast<uint32_t*>(out);
        out32[0] = pair;
        out32[1] = pair;
        out32[2] = pair;
        out32[3] = pair;
        return;
    }
    for (int bit = 7; bit >= 0 && max_pixels > 0; --bit, --max_pixels) {
        *out++ = (byte & (1 << bit)) ? fg : bg;
    }
}

static void fillRgb565Row(uint16_t* out, int w, uint16_t color) {
    if (!out || w <= 0) {
        return;
    }
    const uint32_t pair = (static_cast<uint32_t>(color) << 16) | static_cast<uint32_t>(color);
    uint32_t* out32 = reinterpret_cast<uint32_t*>(out);
    const int pairs = w / 2;
    for (int i = 0; i < pairs; ++i) {
        out32[i] = pair;
    }
    if (w & 1) {
        out[w - 1] = color;
    }
}

static bool bwLineAllByte(const uint8_t* line, int row_bytes, uint8_t value) {
    for (int i = 0; i < row_bytes; ++i) {
        if (line[i] != value) {
            return false;
        }
    }
    return true;
}

/** 1 ビット packed 行を RGB565 バンドへ展開 */
void expandBwBufferChunk(const uint8_t* frame, uint16_t w, int src_y0, int rows, uint16_t* dst,
                         uint16_t fg, uint16_t bg) {
    if (!frame || !dst || w == 0 || rows <= 0) {
        return;
    }
    const int row_bytes = (static_cast<int>(w) + 7) / 8;
    for (int row = 0; row < rows; ++row) {
        const uint8_t* line =
            frame + static_cast<size_t>(src_y0 + row) * static_cast<size_t>(row_bytes);
        uint16_t* out = dst + static_cast<size_t>(row) * static_cast<size_t>(w);
        int x = 0;
        while (x < static_cast<int>(w)) {
            const int byte_idx = x / 8;
            const int rem = static_cast<int>(w) - x;
            const int in_byte = rem >= 8 ? 8 : rem;
            expandBwByteToPixels(line[byte_idx], out, in_byte, fg, bg);
            x += in_byte;
            out += in_byte;
        }
    }
}

/** 1 ビットバッファを全画面 RGB565 に展開 */
void expandBwBufferFull(const uint8_t* frame, uint16_t w, uint16_t h, uint16_t* dst, uint16_t fg,
                        uint16_t bg) {
    if (!frame || !dst || w == 0 || h == 0) {
        return;
    }
    const int row_bytes = (static_cast<int>(w) + 7) / 8;
    for (uint16_t y = 0; y < h; ++y) {
        const uint8_t* line = frame + static_cast<size_t>(y) * static_cast<size_t>(row_bytes);
        uint16_t* out = dst + static_cast<size_t>(y) * static_cast<size_t>(w);
        if (bwLineAllByte(line, row_bytes, 0x00)) {
            fillRgb565Row(out, static_cast<int>(w), bg);
            continue;
        }
        if (bwLineAllByte(line, row_bytes, 0xFF)) {
            fillRgb565Row(out, static_cast<int>(w), fg);
            continue;
        }
        int x = 0;
        while (x < static_cast<int>(w)) {
            const int byte_idx = x / 8;
            const int rem = static_cast<int>(w) - x;
            const int in_byte = rem >= 8 ? 8 : rem;
            expandBwByteToPixels(line[byte_idx], out + x, in_byte, fg, bg);
            x += in_byte;
        }
    }
}

// --- BWPK RGB565 フルフレームバッファ（draw_bw_pack 専用・HeapBudget 優先）---
// 320x240 x1 ≈ 150KB。300KB ダブルバッファは RAM 不足で失敗しやすいため単一バッファ + 1-bit prefetch。

static uint16_t* s_bw_rgb_buf = nullptr;
static size_t s_bw_rgb_alloc_bytes = 0;
static bool s_bw_rgb_from_heap_budget = false;
static size_t s_bw_rgb_pixel_count = 0;
static uint16_t s_bw_rgb_width = 0;
static uint16_t s_bw_rgb_height = 0;
static int s_bw_rgb_display_frame = 0;
/** 1-bit バッファが SD 同期済みのフレーム（RGB 未展開の prefetch 状態） */
static int s_bw_rgb_bit_ready_frame = 0;

static void bwPackRgbFreeBuffer() {
    if (s_bw_rgb_buf) {
        if (s_bw_rgb_from_heap_budget) {
            HeapBudget::release(s_bw_rgb_buf, s_bw_rgb_alloc_bytes);
        } else {
            std::free(s_bw_rgb_buf);
        }
        s_bw_rgb_buf = nullptr;
    }
    s_bw_rgb_alloc_bytes = 0;
    s_bw_rgb_from_heap_budget = false;
    s_bw_rgb_pixel_count = 0;
    s_bw_rgb_width = 0;
    s_bw_rgb_height = 0;
}

bool bwPackRgbIsReady() { return s_bw_rgb_buf != nullptr && s_bw_rgb_pixel_count > 0; }

bool bwPackRgbBufEnsure(uint16_t w, uint16_t h) {
    if (w == 0 || h == 0) {
        return false;
    }
    const size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
    if (bwPackRgbIsReady() && s_bw_rgb_pixel_count == pixels && s_bw_rgb_width == w &&
        s_bw_rgb_height == h) {
        return true;
    }
    bwPackRgbFreeBuffer();
    const size_t bytes = pixels * sizeof(uint16_t);
    void* alloc_ptr = nullptr;
    if (HeapBudget::tryAlloc(bytes, &alloc_ptr)) {
        s_bw_rgb_from_heap_budget = true;
    } else {
        alloc_ptr = std::malloc(bytes);
        s_bw_rgb_from_heap_budget = false;
    }
    if (!alloc_ptr) {
        printf("bwPackRgb: alloc failed %u bytes (single buffer)\n", static_cast<unsigned>(bytes));
        return false;
    }
    s_bw_rgb_buf = static_cast<uint16_t*>(alloc_ptr);
    s_bw_rgb_alloc_bytes = bytes;
    s_bw_rgb_pixel_count = pixels;
    s_bw_rgb_width = w;
    s_bw_rgb_height = h;
    s_bw_rgb_display_frame = 0;
    s_bw_rgb_bit_ready_frame = 0;
    printf("bwPackRgb: ready %ux%u (%u KB, HeapBudget=%d)\n", w, h, static_cast<unsigned>(bytes / 1024),
           s_bw_rgb_from_heap_budget ? 1 : 0);
    return true;
}

void bwPackRgbBufRelease(bool use_free) {
    (void)use_free;
    bwPackRgbFreeBuffer();
    s_bw_rgb_display_frame = 0;
    s_bw_rgb_bit_ready_frame = 0;
}

void bwPackRgbBufResetPipeline() {
    s_bw_rgb_display_frame = 0;
    s_bw_rgb_bit_ready_frame = 0;
}

bool bwPackRgbHasDisplayFrame(int frame_index_1based) {
    return frame_index_1based > 0 && s_bw_rgb_display_frame == frame_index_1based && s_bw_rgb_buf;
}

bool bwPackRgbTrySwapToFrame(int frame_index_1based) {
    (void)frame_index_1based;
    return false;
}

static bool bwPackRgbExpandCurrent(uint16_t w, uint16_t h, uint16_t fg, uint16_t bg) {
    if (!s_bw_rgb_buf || !bwFrameIsValid()) {
        return false;
    }
    uint8_t* frame = bwFrameBuf();
    if (!frame) {
        return false;
    }
    expandBwBufferFull(frame, w, h, s_bw_rgb_buf, fg, bg);
    return true;
}

bool bwPackRgbLoadDisplayFrame(FIL* file, int frame_index_1based, uint32_t frame_count,
                               uint32_t data_base, uint16_t w, uint16_t h, uint16_t fg,
                               uint16_t bg, int* inout_bit_buffer_frame) {
    if (!file || frame_index_1based <= 0 || !inout_bit_buffer_frame) {
        return false;
    }
    if (!bwPackRgbBufEnsure(w, h) || !bwFrameBufEnsure(w, h)) {
        return false;
    }
    uint8_t* frame = bwFrameBuf();
    if (!frame) {
        return false;
    }
    if (s_bw_rgb_bit_ready_frame != frame_index_1based) {
        if (!syncBwPackToFrame(file, static_cast<uint32_t>(frame_index_1based), frame_count,
                               data_base, frame, w, h, inout_bit_buffer_frame)) {
            return false;
        }
    }
    if (!bwPackRgbExpandCurrent(w, h, fg, bg)) {
        return false;
    }
    s_bw_rgb_display_frame = frame_index_1based;
    s_bw_rgb_bit_ready_frame = 0;
    return true;
}

bool bwPackRgbPrefetchNextFrame(FIL* file, int current_frame_1based, uint32_t frame_count,
                                uint32_t data_base, uint16_t w, uint16_t h, uint16_t fg,
                                uint16_t bg, int* inout_bit_buffer_frame) {
    (void)fg;
    (void)bg;
    if (!file || current_frame_1based <= 0 || frame_count == 0 || !inout_bit_buffer_frame) {
        return false;
    }
    if (!s_bw_rgb_buf) {
        return false;
    }

    int next_frame = current_frame_1based + 1;
    if (static_cast<uint32_t>(next_frame) > frame_count) {
        next_frame = 1;
    }
    if (s_bw_rgb_bit_ready_frame == next_frame) {
        return true;
    }
    if (!bwFrameBufEnsure(w, h)) {
        return false;
    }
    uint8_t* frame = bwFrameBuf();
    if (!frame) {
        return false;
    }

    const int last = *inout_bit_buffer_frame;
    bool synced = false;
    if (last > 0 && last == current_frame_1based && bwFrameIsValid() &&
        next_frame == current_frame_1based + 1) {
        synced = loadBwPackFrameFromSd(file, static_cast<uint32_t>(next_frame), frame_count,
                                       data_base, frame, w, h);
        if (synced) {
            *inout_bit_buffer_frame = next_frame;
        }
    } else if (last == static_cast<int>(frame_count) && next_frame == 1) {
        synced = loadBwPackFrameFromSd(file, 1, frame_count, data_base, frame, w, h);
        if (synced) {
            *inout_bit_buffer_frame = 1;
        }
    } else {
        synced = syncBwPackToFrame(file, static_cast<uint32_t>(next_frame), frame_count, data_base,
                                   frame, w, h, inout_bit_buffer_frame);
    }
    if (!synced) {
        return false;
    }
    s_bw_rgb_bit_ready_frame = next_frame;
    return true;
}

const uint16_t* bwPackRgbDisplayPixels(uint16_t w, uint16_t h) {
    if (s_bw_rgb_display_frame <= 0 || !s_bw_rgb_buf || w != s_bw_rgb_width ||
        h != s_bw_rgb_height) {
        return nullptr;
    }
    return s_bw_rgb_buf;
}

static uint32_t readLeU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/** SD 上の 1 ビットフレーム（フル／差分／スキップ）を frame へ適用する */
bool loadBwFrameFromSd(FIL* file, FSIZE_t file_size, uint8_t* frame, uint16_t w, uint16_t h,
                       bool seek_start) {
    if (!file || !frame || w == 0 || h == 0) {
        return false;
    }

    const size_t frame_bytes = bwFrameByteSize(w, h);
    const int row_bytes = (static_cast<int>(w) + 7) / 8;

    if (file_size == 0) {
        return bwFrameIsValid();
    }
    if (file_size > frame_bytes) {
        return false;
    }
    if (file_size == frame_bytes) {
        UINT br = 0;
        if (seek_start && f_lseek(file, 0) != FR_OK) {
            return false;
        }
        if (f_read(file, frame, static_cast<UINT>(frame_bytes), &br) != FR_OK ||
            br != static_cast<UINT>(frame_bytes)) {
            return false;
        }
        s_bw_frame_valid = true;
        return true;
    }

    UINT br = 0;
    uint8_t row_count = 0;
    if (seek_start && f_lseek(file, 0) != FR_OK) {
        return false;
    }
    if (f_read(file, &row_count, 1, &br) != FR_OK || br != 1) {
        return false;
    }
    if (row_count == 0 || row_count > h) {
        return false;
    }
    const size_t expected =
        1u + static_cast<size_t>(row_count) * (1u + static_cast<size_t>(row_bytes));
    if (file_size != expected) {
        return false;
    }
    if (!bwFrameIsValid()) {
        return false;
    }

    for (uint8_t i = 0; i < row_count; ++i) {
        uint8_t y = 0;
        if (f_read(file, &y, 1, &br) != FR_OK || br != 1) {
            return false;
        }
        if (y >= h) {
            return false;
        }
        uint8_t* dst_row = frame + static_cast<size_t>(y) * static_cast<size_t>(row_bytes);
        if (f_read(file, dst_row, static_cast<UINT>(row_bytes), &br) != FR_OK ||
            br != static_cast<UINT>(row_bytes)) {
            return false;
        }
    }
    s_bw_frame_valid = true;
    return true;
}

static constexpr uint32_t kBwPackHeaderSize = 12u;

/** BWPK パックのヘッダを検証し frame_count / data_base を返す */
bool bwPackReadHeader(FIL* file, uint32_t* out_frame_count, uint32_t* out_data_base) {
    if (!file || !out_frame_count || !out_data_base) {
        return false;
    }
    uint8_t header[kBwPackHeaderSize];
    UINT br = 0;
    if (f_lseek(file, 0) != FR_OK) {
        return false;
    }
    if (f_read(file, header, static_cast<UINT>(sizeof(header)), &br) != FR_OK ||
        br != static_cast<UINT>(sizeof(header))) {
        return false;
    }
    if (std::memcmp(header, "BWPK", 4) != 0 || header[4] != 1u) {
        return false;
    }
    const uint32_t frame_count = readLeU32(header + 8);
    if (frame_count == 0) {
        return false;
    }
    *out_frame_count = frame_count;
    *out_data_base = kBwPackHeaderSize + frame_count * 8u;
    return true;
}

/** BWPK 内の 1 フレーム（1 始まり）を frame へ適用する */
bool loadBwPackFrameFromSd(FIL* file, uint32_t frame_index_1based, uint32_t frame_count,
                           uint32_t data_base, uint8_t* frame, uint16_t w, uint16_t h) {
    if (!file || !frame || frame_index_1based == 0 || frame_index_1based > frame_count) {
        return false;
    }
    const uint32_t idx = frame_index_1based - 1u;
    const FSIZE_t index_off =
        static_cast<FSIZE_t>(kBwPackHeaderSize) + static_cast<FSIZE_t>(idx) * 8u;
    uint8_t ent[8];
    UINT br = 0;
    if (f_lseek(file, index_off) != FR_OK) {
        return false;
    }
    if (f_read(file, ent, static_cast<UINT>(sizeof(ent)), &br) != FR_OK ||
        br != static_cast<UINT>(sizeof(ent))) {
        return false;
    }
    const uint32_t offset = readLeU32(ent);
    const uint32_t size = readLeU32(ent + 4);
    const size_t frame_bytes = bwFrameByteSize(w, h);
    if (size > frame_bytes) {
        return false;
    }
    if (f_lseek(file, static_cast<FSIZE_t>(data_base) + static_cast<FSIZE_t>(offset)) !=
        FR_OK) {
        return false;
    }
    return loadBwFrameFromSd(file, static_cast<FSIZE_t>(size), frame, w, h, false);
}

/** 差分連鎖を保ったまま target フレームまで適用する */
bool syncBwPackToFrame(FIL* file, uint32_t target_frame, uint32_t frame_count,
                       uint32_t data_base, uint8_t* frame, uint16_t w, uint16_t h,
                       int* inout_buffer_frame) {
    if (!file || !frame || !inout_buffer_frame || target_frame == 0 ||
        target_frame > frame_count) {
        return false;
    }

    const int last = *inout_buffer_frame;
    if (last == static_cast<int>(target_frame) && bwFrameIsValid()) {
        return true;
    }

    if (last <= 0 || !bwFrameIsValid()) {
        for (uint32_t f = 1; f <= target_frame; ++f) {
            if (!loadBwPackFrameFromSd(file, f, frame_count, data_base, frame, w, h)) {
                return false;
            }
        }
        *inout_buffer_frame = static_cast<int>(target_frame);
        return true;
    }

    if (last == static_cast<int>(frame_count) && target_frame == 1u) {
        if (!loadBwPackFrameFromSd(file, 1, frame_count, data_base, frame, w, h)) {
            return false;
        }
        *inout_buffer_frame = 1;
        return true;
    }

    if (static_cast<uint32_t>(last) < target_frame) {
        for (uint32_t f = static_cast<uint32_t>(last) + 1u; f <= target_frame; ++f) {
            if (!loadBwPackFrameFromSd(file, f, frame_count, data_base, frame, w, h)) {
                return false;
            }
        }
        *inout_buffer_frame = static_cast<int>(target_frame);
        return true;
    }

    for (uint32_t f = 1; f <= target_frame; ++f) {
        if (!loadBwPackFrameFromSd(file, f, frame_count, data_base, frame, w, h)) {
            return false;
        }
    }
    *inout_buffer_frame = static_cast<int>(target_frame);
    return true;
}

/** バンド index から画面上端の Y 座標（論理座標）を返す */
int bgBandTopY(int band_index) {
    return band_index * static_cast<int>(GameConfig::BUFFER_HEIGHT);
}

/** バンド index から画面下端の Y 座標（論理座標、排他的）を返す */
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

/** 画像矩形と指定バンドの交差領域を求める。交差なしなら false */
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

/** SD 上 RGB565 画像の src_y0 行から rows 行分を dst 先頭へ読み込む */
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

/** SD 上 1 ビット画像の src_y0 行から rows 行分を読み、RGB565 に展開して dst へ書く */
bool readBwStreamChunk(FIL* file, uint16_t w, int src_y0, int rows, uint16_t* dst, uint16_t fg,
                       uint16_t bg) {
    if (!file || !dst || w == 0 || rows <= 0) {
        return false;
    }

    const int row_bytes = (static_cast<int>(w) + 7) / 8;
    const size_t packed_chunk = static_cast<size_t>(row_bytes) * static_cast<size_t>(rows);
    uint8_t packed[(GameConfig::BUFFER_WIDTH / 8 + 1) * GameConfig::BUFFER_HEIGHT];
    if (packed_chunk > sizeof(packed)) {
        return false;
    }

    const FSIZE_t offset =
        static_cast<FSIZE_t>(src_y0) * static_cast<FSIZE_t>(row_bytes);
    if (f_lseek(file, offset) != FR_OK) {
        return false;
    }
    UINT br = 0;
    if (f_read(file, packed, static_cast<UINT>(packed_chunk), &br) != FR_OK ||
        br != static_cast<UINT>(packed_chunk)) {
        return false;
    }

    for (int row = 0; row < rows; ++row) {
        const uint8_t* line = packed + static_cast<size_t>(row) * static_cast<size_t>(row_bytes);
        uint16_t* out = dst + static_cast<size_t>(row) * static_cast<size_t>(w);
        for (int x = 0; x < static_cast<int>(w); ++x) {
            const int byte_idx = x / 8;
            const int bit = 7 - (x % 8);
            const bool on = (line[byte_idx] >> bit) & 1;
            out[x] = on ? fg : bg;
        }
    }
    return true;
}
