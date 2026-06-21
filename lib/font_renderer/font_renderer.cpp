// ============================================
// ファイル: font_renderer.cpp
// MISF サブセットフォント（8x8 / 12x12 等）UTF-8 描画
// ============================================

#include "font_renderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "ff.h"
#include "f_util.h"
}

#include "heap_budget.hpp"
#include "sd_path_util.hpp"

FontRenderer* FontRenderer::s_active_ = nullptr;

// コンストラクタ（メンバはデフォルト初期化）
FontRenderer::FontRenderer() = default;

// デストラクタ。アクティブ参照を解除し、読み込み済みフォントを解放する
FontRenderer::~FontRenderer() {
    if (s_active_ == this) {
        s_active_ = nullptr;
    }
    unload();
}

// HeapBudget 経由で確保したフォントデータを解放し、状態をリセットする
void FontRenderer::unload() {
    if (index_) {
        auto* base = reinterpret_cast<uint8_t*>(index_) - 16;
        HeapBudget::release(base, alloc_bytes_);
    }
    glyph_data_ = nullptr;
    index_ = nullptr;
    glyph_count_ = 0;
    alloc_bytes_ = 0;
    scale_num_ = 1;
    scale_den_ = 1;
}

// malloc 起点のフォントデータを raw free し、状態をリセットする（HeapBudget 迂回）
void FontRenderer::unloadRaw() {
    if (index_) {
        auto* base = reinterpret_cast<uint8_t*>(index_) - 16;
        free(base);
    }
    glyph_data_ = nullptr;
    index_ = nullptr;
    glyph_count_ = 0;
    alloc_bytes_ = 0;
    scale_num_ = 1;
    scale_den_ = 1;
}

// 描画倍率を整数比 num/den で設定する（0 指定時は 1:1 に戻す）
void FontRenderer::setScale(uint8_t num, uint8_t den) {
    if (num == 0 || den == 0) {
        scale_num_ = 1;
        scale_den_ = 1;
        return;
    }
    scale_num_ = num;
    scale_den_ = den;
}

// フォントメトリクス値に描画倍率を適用して返す
uint8_t FontRenderer::scaleValue(uint8_t value) const {
    if (scale_num_ == scale_den_) {
        return value;
    }
    return static_cast<uint8_t>((static_cast<unsigned>(value) * scale_num_) / scale_den_);
}

// グリフ 1 行あたりのバイト数を返す
uint8_t FontRenderer::bytesPerRow() const {
    if (glyph_h_ == 0) {
        return 0;
    }
    return static_cast<uint8_t>(bytes_per_glyph_ / glyph_h_);
}

// グリフビットマップの指定座標がオン（描画対象）かどうかを返す
bool FontRenderer::glyphPixel(const uint8_t* glyph, int row, int col) const {
    if (!glyph || row < 0 || col < 0 || row >= glyph_h_ || col >= glyph_w_) {
        return false;
    }
    const uint8_t bytes_per_row = bytesPerRow();
    if (bytes_per_row == 0) {
        return false;
    }
    const uint8_t bits = glyph[static_cast<size_t>(row) * bytes_per_row + static_cast<size_t>(col / 8)];
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (col % 8));
    return (bits & mask) != 0;
}

// SD カード上の MISF v1 フォントファイルを読み込み、インデックスとグリフをメモリに展開する
bool FontRenderer::loadFromSd(const char* path) {
    unload();
    if (!path || path[0] == '\0') {
        return false;
    }

    char norm[FF_LFN_BUF + 4];
    normalizeSdPath(path, norm, sizeof(norm));

    FIL file;
    FRESULT fr = f_open(&file, norm, FA_READ);
    if (fr != FR_OK) {
        printf("FontRenderer: open failed %s (%s)\n", norm, FRESULT_str(fr));
        return false;
    }

    const FSIZE_t fsize = f_size(&file);
    if (fsize < 16) {
        printf("FontRenderer: file too small %s\n", path);
        f_close(&file);
        return false;
    }

    void* buf_ptr = nullptr;
    if (!HeapBudget::tryAlloc(static_cast<size_t>(fsize), &buf_ptr)) {
        printf("FontRenderer: heap budget exceeded (%lu bytes)\n", (unsigned long)fsize);
        f_close(&file);
        return false;
    }

    auto* file_buf = static_cast<uint8_t*>(buf_ptr);
    UINT br = 0;
    fr = f_read(&file, file_buf, static_cast<UINT>(fsize), &br);
    f_close(&file);
    if (fr != FR_OK || br != static_cast<UINT>(fsize)) {
        printf("FontRenderer: read failed %s\n", path);
        HeapBudget::release(file_buf, static_cast<size_t>(fsize));
        return false;
    }

    if (std::memcmp(file_buf, kMagic, 4) != 0) {
        printf("FontRenderer: bad magic %s\n", path);
        HeapBudget::release(file_buf, static_cast<size_t>(fsize));
        return false;
    }

    const uint8_t version = file_buf[4];
    if (version != 1) {
        printf("FontRenderer: unsupported version %u\n", version);
        HeapBudget::release(file_buf, static_cast<size_t>(fsize));
        return false;
    }

    glyph_w_ = file_buf[5];
    glyph_h_ = file_buf[6];
    default_advance_ = file_buf[7];
    glyph_count_ = static_cast<uint16_t>(file_buf[8] | (static_cast<uint16_t>(file_buf[9]) << 8));
    bytes_per_glyph_ = static_cast<uint16_t>(file_buf[10] | (static_cast<uint16_t>(file_buf[11]) << 8));

    if (glyph_w_ == 0 || glyph_h_ == 0 || bytes_per_glyph_ == 0 || glyph_count_ == 0) {
        printf("FontRenderer: invalid header %s\n", path);
        HeapBudget::release(file_buf, static_cast<size_t>(fsize));
        return false;
    }

    const size_t index_bytes = static_cast<size_t>(glyph_count_) * sizeof(IndexEntry);
    const size_t glyph_bytes = static_cast<size_t>(glyph_count_) * bytes_per_glyph_;
    const size_t expected = 16 + index_bytes + glyph_bytes;
    if (fsize < expected) {
        printf("FontRenderer: truncated file %s (%lu < %u)\n", path, (unsigned long)fsize,
               (unsigned)expected);
        HeapBudget::release(file_buf, static_cast<size_t>(fsize));
        return false;
    }

    index_ = reinterpret_cast<IndexEntry*>(file_buf + 16);
    glyph_data_ = file_buf + 16 + index_bytes;
    alloc_bytes_ = static_cast<size_t>(fsize);

    printf("FontRenderer: loaded %s (%u glyphs, %ux%u)\n", path, glyph_count_, glyph_w_, glyph_h_);
    return true;
}

// コードポイントからグリフインデックスエントリを二分探索で検索する
const FontRenderer::IndexEntry* FontRenderer::findGlyph(uint32_t codepoint) const {
    if (!index_ || glyph_count_ == 0) {
        return nullptr;
    }
    int lo = 0;
    int hi = static_cast<int>(glyph_count_) - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const uint32_t cp = index_[mid].codepoint;
        if (cp == codepoint) {
            return &index_[mid];
        }
        if (cp < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return nullptr;
}

// 1 グリフをバンドフレームバッファへ描画する（倍率対応・背景色あり）
void FontRenderer::drawGlyph(uint16_t* fb, uint16_t fb_w, uint16_t band_rows, int band_y0,
                             int x, int y, const uint8_t* glyph, uint16_t fg,
                             uint16_t bg, bool use_bg) const {
    if (!fb || !glyph) {
        return;
    }

    const int out_w = scaleValue(glyph_w_);
    const int out_h = scaleValue(glyph_h_);
    if (out_w <= 0 || out_h <= 0) {
        return;
    }

    if (scale_num_ == scale_den_) {
        for (int row = 0; row < glyph_h_; ++row) {
            const int py = y + row;
            const int local_y = py - band_y0;
            if (local_y < 0 || local_y >= static_cast<int>(band_rows)) {
                continue;
            }
            for (int col = 0; col < glyph_w_; ++col) {
                const int px = x + col;
                if (px < 0 || px >= static_cast<int>(fb_w)) {
                    continue;
                }
                const bool on = glyphPixel(glyph, row, col);
                if (on) {
                    fb[static_cast<uint32_t>(local_y) * fb_w + static_cast<uint32_t>(px)] = fg;
                } else if (use_bg) {
                    fb[static_cast<uint32_t>(local_y) * fb_w + static_cast<uint32_t>(px)] = bg;
                }
            }
        }
        return;
    }

    for (int oy = 0; oy < out_h; ++oy) {
        const int py = y + oy;
        const int local_y = py - band_y0;
        if (local_y < 0 || local_y >= static_cast<int>(band_rows)) {
            continue;
        }
        const int src_row = (oy * static_cast<int>(glyph_h_)) / out_h;
        for (int ox = 0; ox < out_w; ++ox) {
            const int px = x + ox;
            if (px < 0 || px >= static_cast<int>(fb_w)) {
                continue;
            }
            const int src_col = (ox * static_cast<int>(glyph_w_)) / out_w;
            const bool on = glyphPixel(glyph, src_row, src_col);
            if (on) {
                fb[static_cast<uint32_t>(local_y) * fb_w + static_cast<uint32_t>(px)] = fg;
            } else if (use_bg) {
                fb[static_cast<uint32_t>(local_y) * fb_w + static_cast<uint32_t>(px)] = bg;
            }
        }
    }
}

namespace {

// UTF-8 文字列から次のコードポイントを 1 つ取り出し、ポインタを進める
bool utf8Next(const char*& p, uint32_t& codepoint) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c == '\0') {
        return false;
    }
    if (c < 0x80) {
        codepoint = c;
        ++p;
        return true;
    }
    if ((c & 0xE0) == 0xC0) {
        const unsigned char c1 = static_cast<unsigned char>(p[1]);
        if ((c1 & 0xC0) != 0x80) {
            codepoint = '?';
            ++p;
            return true;
        }
        codepoint = ((c & 0x1F) << 6) | (c1 & 0x3F);
        p += 2;
        return true;
    }
    if ((c & 0xF0) == 0xE0) {
        const unsigned char c1 = static_cast<unsigned char>(p[1]);
        const unsigned char c2 = static_cast<unsigned char>(p[2]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
            codepoint = '?';
            ++p;
            return true;
        }
        codepoint = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        p += 3;
        return true;
    }
    if ((c & 0xF8) == 0xF0) {
        const unsigned char c1 = static_cast<unsigned char>(p[1]);
        const unsigned char c2 = static_cast<unsigned char>(p[2]);
        const unsigned char c3 = static_cast<unsigned char>(p[3]);
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
            codepoint = '?';
            ++p;
            return true;
        }
        codepoint = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        p += 4;
        return true;
    }
    codepoint = '?';
    ++p;
    return true;
}

}  // namespace

// UTF-8 テキストをバンドフレームバッファへ描画する（改行・欠損グリフのフォールバック対応）
void FontRenderer::drawTextBg(uint16_t* fb, uint16_t fb_w, uint16_t band_rows, int band_y0, int x,
                              int y, const char* utf8, uint16_t fg, uint16_t bg,
                              bool use_bg) const {
    if (!fb || !utf8 || !glyph_data_) {
        return;
    }

    int cx = x;
    int cy = y;
    const char* p = utf8;

    while (*p) {
        if (*p == '\n') {
            cx = x;
            cy += scaleValue(glyph_h_);
            ++p;
            continue;
        }

        uint32_t codepoint = 0;
        if (!utf8Next(p, codepoint)) {
            break;
        }

        const IndexEntry* entry = findGlyph(codepoint);
        uint8_t advance = default_advance_;
        const uint8_t* glyph = glyph_data_;

        if (entry) {
            advance = entry->advance ? entry->advance : default_advance_;
            glyph = glyph_data_ + static_cast<size_t>(entry->glyph_index) * bytes_per_glyph_;
        } else if (codepoint == ' ') {
            cx += scaleValue(advance);
            continue;
        } else {
            const IndexEntry* fallback = findGlyph(static_cast<uint32_t>('?'));
            if (!fallback) {
                cx += scaleValue(advance);
                continue;
            }
            advance = fallback->advance ? fallback->advance : default_advance_;
            glyph = glyph_data_ + static_cast<size_t>(fallback->glyph_index) * bytes_per_glyph_;
        }

        drawGlyph(fb, fb_w, band_rows, band_y0, cx, cy, glyph, fg, bg, use_bg);
        cx += scaleValue(advance);
    }
}
