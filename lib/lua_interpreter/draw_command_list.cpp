// ============================================
// ファイル: draw_command_list.cpp
// game_draw 録画バッファの実装
// ============================================

#include "draw_command_list.hpp"

#include <cstring>

#include "config.hpp"
#include "game_display.hpp"
#include "lua_interpreter.hpp"
#include "vn_stream_compose.hpp"

// 1 にすると endRecord ごとに旧実装（帯ごと走査）と照合し、不一致と処理時間を
// USB シリアルに出力する（検証用。通常は 0）
#ifndef DRAW_CMD_HASH_SELFTEST
#define DRAW_CMD_HASH_SELFTEST 0
#endif

#if DRAW_CMD_HASH_SELFTEST
#include <cstdio>
#include "pico/time.h"
#endif

namespace {

inline int16_t clampI16(int v) {
    if (v < -32768) {
        return -32768;
    }
    if (v > 32767) {
        return 32767;
    }
    return static_cast<int16_t>(v);
}

}  // namespace

void DrawCommandList::beginRecord(uint16_t screen_w, uint16_t screen_h, uint16_t band_h) {
    used_ = 0;
    recording_ = true;
    ready_ = false;
    failed_ = false;
    screen_w_ = screen_w;
    screen_h_ = screen_h;
    band_h_ = band_h == 0 ? 20 : band_h;
    band_count_ = static_cast<int>((screen_h_ + band_h_ - 1) / band_h_);
    if (band_count_ > kMaxBands) {
        band_count_ = kMaxBands;
    }
    for (int i = 0; i < kMaxBands; ++i) {
        band_hash_[i] = 0;
    }
}

bool DrawCommandList::endRecord() {
    recording_ = false;
    if (failed_) {
        ready_ = false;
        return false;
    }
    ready_ = true;
#if DRAW_CMD_HASH_SELFTEST
    static uint32_t s_frames = 0;
    static uint32_t s_mismatch = 0;
    static uint64_t s_us_new = 0;
    static uint64_t s_us_old = 0;
    const uint64_t t0 = time_us_64();
    hashAllBands();
    const uint64_t t1 = time_us_64();
    for (int b = 0; b < band_count_; ++b) {
        if (hashBandCommands(b) != band_hash_[b]) {
            ++s_mismatch;
            printf("[HASH-SELFTEST] mismatch band=%d used=%u\n", b, static_cast<unsigned>(used_));
        }
    }
    const uint64_t t2 = time_us_64();
    s_us_new += t1 - t0;
    s_us_old += t2 - t1;
    if (++s_frames >= 300) {
        printf("[HASH-SELFTEST] 300 frames: mismatch=%lu  new=%lu us/f  old=%lu us/f\n",
               static_cast<unsigned long>(s_mismatch),
               static_cast<unsigned long>(s_us_new / s_frames),
               static_cast<unsigned long>(s_us_old / s_frames));
        s_frames = 0;
        s_us_new = 0;
        s_us_old = 0;
    }
#else
    hashAllBands();
#endif
    return true;
}

void DrawCommandList::markFailed() {
    failed_ = true;
    ready_ = false;
}

void DrawCommandList::reset() {
    used_ = 0;
    recording_ = false;
    ready_ = false;
    failed_ = false;
    have_prev_hashes_ = false;
    for (int i = 0; i < kMaxBands; ++i) {
        band_hash_[i] = 0;
        prev_band_hash_[i] = 0;
    }
}

uint8_t* DrawCommandList::alloc(size_t n) {
    if (failed_ || used_ + n > kBufferBytes) {
        markFailed();
        return nullptr;
    }
    uint8_t* p = buf_ + used_;
    used_ += n;
    return p;
}

bool DrawCommandList::writeU8(uint8_t v) {
    uint8_t* p = alloc(1);
    if (!p) {
        return false;
    }
    *p = v;
    return true;
}

bool DrawCommandList::writeU16(uint16_t v) {
    uint8_t* p = alloc(2);
    if (!p) {
        return false;
    }
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    return true;
}

bool DrawCommandList::writeI16(int16_t v) {
    return writeU16(static_cast<uint16_t>(v));
}

bool DrawCommandList::writeI32(int32_t v) {
    uint8_t* p = alloc(4);
    if (!p) {
        return false;
    }
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    return true;
}

bool DrawCommandList::writeBytes(const void* src, size_t n) {
    uint8_t* p = alloc(n);
    if (!p) {
        return false;
    }
    std::memcpy(p, src, n);
    return true;
}

bool DrawCommandList::writeString(const char* s) {
    if (!s) {
        s = "";
    }
    const size_t len = std::strlen(s);
    if (len > 255) {
        markFailed();
        return false;
    }
    if (!writeU8(static_cast<uint8_t>(len))) {
        return false;
    }
    return writeBytes(s, len);
}

void DrawCommandList::recClear(uint16_t color) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::Clear));
    writeU16(color);
}

void DrawCommandList::recFillRect(int x, int y, int w, int h, uint16_t color) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::FillRect));
    writeI16(clampI16(x));
    writeI16(clampI16(y));
    writeI16(clampI16(w));
    writeI16(clampI16(h));
    writeU16(color);
}

void DrawCommandList::recFillRectAlpha(int x, int y, int w, int h, uint16_t color, uint8_t alpha) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::FillRectAlpha));
    writeI16(clampI16(x));
    writeI16(clampI16(y));
    writeI16(clampI16(w));
    writeI16(clampI16(h));
    writeU16(color);
    writeU8(alpha);
}

void DrawCommandList::recFillRects(const int* xywhc, int count) {
    if (!recording_ || failed_ || count <= 0) {
        return;
    }
    if (count > 64) {
        count = 64;
    }
    writeU8(static_cast<uint8_t>(Op::FillRects));
    writeU8(static_cast<uint8_t>(count));
    for (int i = 0; i < count; ++i) {
        const int* r = xywhc + i * 5;
        writeI16(clampI16(r[0]));
        writeI16(clampI16(r[1]));
        writeI16(clampI16(r[2]));
        writeI16(clampI16(r[3]));
        writeU16(static_cast<uint16_t>(r[4]));
    }
}

void DrawCommandList::recDrawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::DrawLine));
    writeI16(clampI16(x0));
    writeI16(clampI16(y0));
    writeI16(clampI16(x1));
    writeI16(clampI16(y1));
    writeU16(color);
}

void DrawCommandList::recDrawCircle(int cx, int cy, int r, uint16_t color) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::DrawCircle));
    writeI16(clampI16(cx));
    writeI16(clampI16(cy));
    writeI16(clampI16(r));
    writeU16(color);
}

void DrawCommandList::recFillCircle(int cx, int cy, int r, uint16_t color) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::FillCircle));
    writeI16(clampI16(cx));
    writeI16(clampI16(cy));
    writeI16(clampI16(r));
    writeU16(color);
}

void DrawCommandList::recText(int x, int y, const char* text, uint16_t fg, uint16_t bg,
                             bool use_bg) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::Text));
    writeI16(clampI16(x));
    writeI16(clampI16(y));
    writeU16(fg);
    writeU16(bg);
    writeU8(use_bg ? 1 : 0);
    writeString(text);
}

void DrawCommandList::recImage(int id, int dx, int dy, int sx, int sy, int sw, int sh, bool keyed,
                              uint16_t key_color) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::Image));
    writeI16(clampI16(id));
    writeI16(clampI16(dx));
    writeI16(clampI16(dy));
    writeI16(clampI16(sx));
    writeI16(clampI16(sy));
    writeI16(clampI16(sw));
    writeI16(clampI16(sh));
    writeU8(keyed ? 1 : 0);
    writeU16(key_color);
}

void DrawCommandList::recImageScaled(int id, int dx, int dy, int scale, int dest_h) {
    if (!recording_ || failed_) {
        return;
    }
    if (scale < 1 || scale > 16) {
        markFailed();
        return;
    }
    writeU8(static_cast<uint8_t>(Op::ImageScaled));
    writeI16(clampI16(id));
    writeI16(clampI16(dx));
    writeI16(clampI16(dy));
    writeU8(static_cast<uint8_t>(scale));
    writeI16(clampI16(dest_h));
}

void DrawCommandList::recBgStream(const char* path, int dx, int dy, uint16_t w, uint16_t h) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::BgStream));
    writeString(path);
    writeI16(clampI16(dx));
    writeI16(clampI16(dy));
    writeU16(w);
    writeU16(h);
}

void DrawCommandList::recBwStream(const char* path, int dx, int dy, uint16_t w, uint16_t h,
                                 uint16_t fg, uint16_t bg) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::BwStream));
    writeString(path);
    writeI16(clampI16(dx));
    writeI16(clampI16(dy));
    writeU16(w);
    writeU16(h);
    writeU16(fg);
    writeU16(bg);
}

void DrawCommandList::recBwPack(const char* path, int frame_index, int dx, int dy, uint16_t w,
                               uint16_t h, uint16_t fg, uint16_t bg) {
    if (!recording_ || failed_) {
        return;
    }
    writeU8(static_cast<uint8_t>(Op::BwPack));
    writeString(path);
    writeI32(frame_index);
    writeI16(clampI16(dx));
    writeI16(clampI16(dy));
    writeU16(w);
    writeU16(h);
    writeU16(fg);
    writeU16(bg);
}

void DrawCommandList::recVnStreamMarker(LuaInterpreter* interp) {
    if (!recording_ || failed_ || !interp) {
        return;
    }
    // パス・配置を直列化して dirty 判定に使い、再生時は vn_stream_ 状態を描画する
    writeU8(static_cast<uint8_t>(Op::VnStream));
    const VnStreamComposeState& st = interp->vnStreamState();
    writeU8(st.bg.active ? 1 : 0);
    if (st.bg.active) {
        writeString(st.bg.path);
        writeI16(clampI16(st.bg.dx));
        writeI16(clampI16(st.bg.dy));
        writeU16(st.bg.width);
        writeU16(st.bg.height);
    }
    writeU8(static_cast<uint8_t>(st.char_count));
    for (int i = 0; i < st.char_count; ++i) {
        const VnStreamLayer& c = st.chars[i];
        writeU8(c.active ? 1 : 0);
        if (!c.active) {
            continue;
        }
        writeString(c.path);
        writeI16(clampI16(c.dx));
        writeI16(clampI16(c.dy));
        writeU16(c.width);
        writeU16(c.height);
        writeU16(c.key_color);
        writeU8(c.keyed ? 1 : 0);
    }
}

uint32_t DrawCommandList::fnv1a(uint32_t h, const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

bool DrawCommandList::cmdIntersectsBand(int y, int h, int band_y0, int band_y1) {
    if (h <= 0) {
        return false;
    }
    const int y1 = y + h;
    return y < band_y1 && y1 > band_y0;
}

uint32_t DrawCommandList::bandMaskFor(int y, int h) const {
    if (h <= 0) {
        return 0;
    }
    int y0 = y;
    int y1 = y + h;  // 排他
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > static_cast<int>(screen_h_)) {
        y1 = static_cast<int>(screen_h_);
    }
    if (y0 >= y1) {
        return 0;
    }
    const int bh = static_cast<int>(band_h_);
    int first = y0 / bh;
    int last = (y1 - 1) / bh;
    if (last >= band_count_) {
        last = band_count_ - 1;
    }
    if (first > last) {
        return 0;
    }
    const uint32_t upto = (last >= 31) ? 0xFFFFFFFFu : ((1u << (last + 1)) - 1u);
    const uint32_t below = (1u << first) - 1u;
    return upto & ~below;
}

/**
 * 全帯のハッシュを 1 パスで計算する（旧: 帯ごとに全コマンドを 12 回走査）。
 * 各コマンドを 1 回だけ解析し、交差する帯のハッシュにだけコマンドのバイト列を加える。
 * コマンドの解析規則・交差判定・ハッシュ対象バイトは hashBandCommands() と同一なので、
 * 結果（band_hash_）は旧実装とビット単位で一致する。
 */
void DrawCommandList::hashAllBands() {
    const uint32_t all_bands =
        (band_count_ >= 32) ? 0xFFFFFFFFu : ((1u << band_count_) - 1u);
    for (int b = 0; b < band_count_; ++b) {
        band_hash_[b] = 2166136261u;
    }

    const uint8_t* p = buf_;
    const uint8_t* end = buf_ + used_;

    auto rdU8 = [&]() -> uint8_t {
        return (p < end) ? *p++ : 0;
    };
    auto rdU16 = [&]() -> uint16_t {
        const uint16_t lo = rdU8();
        const uint16_t hi = rdU8();
        return static_cast<uint16_t>(lo | (hi << 8));
    };
    auto rdI16 = [&]() -> int {
        return static_cast<int>(static_cast<int16_t>(rdU16()));
    };
    auto rdI32 = [&]() -> int {
        const uint32_t b0 = rdU8();
        const uint32_t b1 = rdU8();
        const uint32_t b2 = rdU8();
        const uint32_t b3 = rdU8();
        return static_cast<int>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
    };
    auto skipStr = [&]() {
        const uint8_t n = rdU8();
        p += n;
        if (p > end) {
            p = end;
        }
    };

    while (p < end) {
        const uint8_t* op_start = p;
        const Op op = static_cast<Op>(rdU8());
        uint32_t mask = 0;
        switch (op) {
            case Op::Clear:
                rdU16();
                mask = all_bands;
                break;
            case Op::FillRect: {
                rdI16();
                const int y = rdI16();
                rdI16();
                const int hh = rdI16();
                rdU16();
                mask = bandMaskFor(y, hh);
                break;
            }
            case Op::FillRectAlpha: {
                rdI16();
                const int y = rdI16();
                rdI16();
                const int hh = rdI16();
                rdU16();
                rdU8();
                mask = bandMaskFor(y, hh);
                break;
            }
            case Op::FillRects: {
                const int n = rdU8();
                for (int i = 0; i < n; ++i) {
                    rdI16();
                    const int y = rdI16();
                    rdI16();
                    const int hh = rdI16();
                    rdU16();
                    mask |= bandMaskFor(y, hh);
                }
                break;
            }
            case Op::DrawLine: {
                rdI16();
                const int y0 = rdI16();
                rdI16();
                const int y1 = rdI16();
                rdU16();
                const int ymin = y0 < y1 ? y0 : y1;
                const int ymax = y0 > y1 ? y0 : y1;
                mask = bandMaskFor(ymin, ymax - ymin + 1);
                break;
            }
            case Op::DrawCircle:
            case Op::FillCircle: {
                rdI16();
                const int cy = rdI16();
                const int r = rdI16();
                rdU16();
                mask = bandMaskFor(cy - r, 2 * r + 1);
                break;
            }
            case Op::Text: {
                rdI16();
                const int y = rdI16();
                rdU16();
                rdU16();
                rdU8();
                const uint8_t n = rdU8();
                // テキスト高はざっくり 24px まで見ておく（旧実装と同じ）
                mask = bandMaskFor(y, 24);
                p += n;
                if (p > end) {
                    p = end;
                }
                break;
            }
            case Op::Image: {
                rdI16();
                rdI16();
                const int dy = rdI16();
                rdI16();
                rdI16();
                rdI16();
                const int sh = rdI16();
                rdU8();
                rdU16();
                mask = bandMaskFor(dy, sh > 0 ? sh : 1);
                break;
            }
            case Op::ImageScaled: {
                rdI16();
                rdI16();
                const int dy = rdI16();
                rdU8();
                const int dh = rdI16();
                mask = bandMaskFor(dy, dh > 0 ? dh : 1);
                break;
            }
            case Op::BgStream:
            case Op::BwStream: {
                skipStr();
                rdI16();
                const int dy = rdI16();
                rdU16();
                const int hh = rdU16();
                if (op == Op::BwStream) {
                    rdU16();
                    rdU16();
                }
                mask = bandMaskFor(dy, hh);
                break;
            }
            case Op::BwPack: {
                skipStr();
                rdI32();
                rdI16();
                const int dy = rdI16();
                rdU16();
                const int hh = rdU16();
                rdU16();
                rdU16();
                mask = bandMaskFor(dy, hh);
                break;
            }
            case Op::VnStream: {
                const uint8_t bg_on = rdU8();
                if (bg_on) {
                    skipStr();
                    rdI16();
                    rdI16();
                    rdU16();
                    rdU16();
                }
                const int n = rdU8();
                for (int i = 0; i < n; ++i) {
                    const uint8_t on = rdU8();
                    if (!on) {
                        continue;
                    }
                    skipStr();
                    rdI16();
                    rdI16();
                    rdU16();
                    rdU16();
                    rdU16();
                    rdU8();
                }
                // VN コマンドは指紋全体を全帯に含める（旧実装と同じ）
                mask = all_bands;
                break;
            }
            default:
                // 不明なコマンド以降は旧実装同様ハッシュ対象外
                return;
        }
        if (mask) {
            const size_t len = static_cast<size_t>(p - op_start);
            for (int b = 0; b < band_count_; ++b) {
                if (mask & (1u << b)) {
                    band_hash_[b] = fnv1a(band_hash_[b], op_start, len);
                }
            }
        }
    }
}

uint32_t DrawCommandList::hashBandCommands(int band_index) const {
    const int band_y0 = band_index * static_cast<int>(band_h_);
    int band_y1 = band_y0 + static_cast<int>(band_h_);
    if (band_y1 > static_cast<int>(screen_h_)) {
        band_y1 = static_cast<int>(screen_h_);
    }

    uint32_t h = 2166136261u;
    const uint8_t* p = buf_;
    const uint8_t* end = buf_ + used_;

    auto rdU8 = [&]() -> uint8_t {
        return (p < end) ? *p++ : 0;
    };
    auto rdU16 = [&]() -> uint16_t {
        const uint16_t lo = rdU8();
        const uint16_t hi = rdU8();
        return static_cast<uint16_t>(lo | (hi << 8));
    };
    auto rdI16 = [&]() -> int {
        return static_cast<int>(static_cast<int16_t>(rdU16()));
    };
    auto rdI32 = [&]() -> int {
        const uint32_t b0 = rdU8();
        const uint32_t b1 = rdU8();
        const uint32_t b2 = rdU8();
        const uint32_t b3 = rdU8();
        return static_cast<int>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
    };
    auto skipStr = [&]() {
        const uint8_t n = rdU8();
        p += n;
        if (p > end) {
            p = end;
        }
    };

    while (p < end) {
        const uint8_t* op_start = p;
        const Op op = static_cast<Op>(rdU8());
        bool hit = false;
        switch (op) {
            case Op::Clear:
                rdU16();
                hit = true;
                break;
            case Op::FillRect: {
                rdI16();
                const int y = rdI16();
                rdI16();
                const int hh = rdI16();
                rdU16();
                hit = cmdIntersectsBand(y, hh, band_y0, band_y1);
                break;
            }
            case Op::FillRectAlpha: {
                rdI16();
                const int y = rdI16();
                rdI16();
                const int hh = rdI16();
                rdU16();
                rdU8();
                hit = cmdIntersectsBand(y, hh, band_y0, band_y1);
                break;
            }
            case Op::FillRects: {
                const int n = rdU8();
                for (int i = 0; i < n; ++i) {
                    rdI16();
                    const int y = rdI16();
                    rdI16();
                    const int hh = rdI16();
                    rdU16();
                    if (cmdIntersectsBand(y, hh, band_y0, band_y1)) {
                        hit = true;
                    }
                }
                break;
            }
            case Op::DrawLine: {
                rdI16();
                const int y0 = rdI16();
                rdI16();
                const int y1 = rdI16();
                rdU16();
                const int ymin = y0 < y1 ? y0 : y1;
                const int ymax = y0 > y1 ? y0 : y1;
                hit = cmdIntersectsBand(ymin, ymax - ymin + 1, band_y0, band_y1);
                break;
            }
            case Op::DrawCircle:
            case Op::FillCircle: {
                rdI16();
                const int cy = rdI16();
                const int r = rdI16();
                rdU16();
                hit = cmdIntersectsBand(cy - r, 2 * r + 1, band_y0, band_y1);
                break;
            }
            case Op::Text: {
                rdI16();
                const int y = rdI16();
                rdU16();
                rdU16();
                rdU8();
                const uint8_t n = rdU8();
                // テキスト高はざっくり 24px まで見ておく
                hit = cmdIntersectsBand(y, 24, band_y0, band_y1);
                p += n;
                if (p > end) {
                    p = end;
                }
                break;
            }
            case Op::Image: {
                rdI16();
                rdI16();
                const int dy = rdI16();
                rdI16();
                rdI16();
                rdI16();
                const int sh = rdI16();
                rdU8();
                rdU16();
                hit = cmdIntersectsBand(dy, sh > 0 ? sh : 1, band_y0, band_y1);
                break;
            }
            case Op::ImageScaled: {
                rdI16();
                rdI16();
                const int dy = rdI16();
                rdU8();
                const int dh = rdI16();
                hit = cmdIntersectsBand(dy, dh > 0 ? dh : 1, band_y0, band_y1);
                break;
            }
            case Op::BgStream:
            case Op::BwStream: {
                skipStr();
                rdI16();
                const int dy = rdI16();
                rdU16();
                const int hh = rdU16();
                if (op == Op::BwStream) {
                    rdU16();
                    rdU16();
                }
                hit = cmdIntersectsBand(dy, hh, band_y0, band_y1);
                break;
            }
            case Op::BwPack: {
                skipStr();
                rdI32();
                rdI16();
                const int dy = rdI16();
                rdU16();
                const int hh = rdU16();
                rdU16();
                rdU16();
                hit = cmdIntersectsBand(dy, hh, band_y0, band_y1);
                break;
            }
            case Op::VnStream: {
                const uint8_t bg_on = rdU8();
                if (bg_on) {
                    skipStr();
                    rdI16();
                    const int dy = rdI16();
                    rdU16();
                    const int hh = rdU16();
                    if (cmdIntersectsBand(dy, hh, band_y0, band_y1)) {
                        hit = true;
                    }
                }
                const int n = rdU8();
                for (int i = 0; i < n; ++i) {
                    const uint8_t on = rdU8();
                    if (!on) {
                        continue;
                    }
                    skipStr();
                    rdI16();
                    const int dy = rdI16();
                    rdU16();
                    const int hh = rdU16();
                    rdU16();
                    rdU8();
                    if (cmdIntersectsBand(dy, hh, band_y0, band_y1)) {
                        hit = true;
                    }
                }
                // VN コマンドは指紋全体を帯に含める（配置が帯外でもシーン同一性に必要）
                hit = true;
                break;
            }
            default:
                return h;
        }
        if (hit) {
            h = fnv1a(h, op_start, static_cast<size_t>(p - op_start));
        }
    }
    return h;
}

bool DrawCommandList::computeDirtyBands(uint16_t* out_mask) {
    uint16_t mask = 0;
    if (!ready_ || !have_prev_hashes_) {
        mask = static_cast<uint16_t>((1u << band_count_) - 1u);
        if (out_mask) {
            *out_mask = mask;
        }
        return false;
    }
    for (int b = 0; b < band_count_; ++b) {
        if (band_hash_[b] != prev_band_hash_[b]) {
            mask = static_cast<uint16_t>(mask | (1u << b));
        }
    }
    if (out_mask) {
        *out_mask = mask;
    }
    return mask == 0;
}

void DrawCommandList::commitBandHashes() {
    for (int b = 0; b < band_count_; ++b) {
        prev_band_hash_[b] = band_hash_[b];
    }
    have_prev_hashes_ = ready_;
}

static uint16_t readU16at(const uint8_t*& p, const uint8_t* end) {
    if (p + 2 > end) {
        p = end;
        return 0;
    }
    const uint16_t v = static_cast<uint16_t>(p[0] | (p[1] << 8));
    p += 2;
    return v;
}

static int readI16at(const uint8_t*& p, const uint8_t* end) {
    return static_cast<int>(static_cast<int16_t>(readU16at(p, end)));
}

static int readI32at(const uint8_t*& p, const uint8_t* end) {
    if (p + 4 > end) {
        p = end;
        return 0;
    }
    const int v = static_cast<int>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
    p += 4;
    return v;
}

static const char* readStrAt(const uint8_t*& p, const uint8_t* end, char* tmp, size_t tmp_len) {
    if (p >= end) {
        tmp[0] = '\0';
        return tmp;
    }
    const uint8_t n = *p++;
    if (p + n > end) {
        tmp[0] = '\0';
        p = end;
        return tmp;
    }
    const size_t copy = n < tmp_len - 1 ? n : tmp_len - 1;
    std::memcpy(tmp, p, copy);
    tmp[copy] = '\0';
    p += n;
    return tmp;
}

void DrawCommandList::replayOne(LuaInterpreter* interp, GameDisplay* disp, const uint8_t*& p,
                                const uint8_t* end) {
    if (p >= end) {
        return;
    }
    const Op op = static_cast<Op>(*p++);
    char path_tmp[192];

    switch (op) {
        case Op::Clear:
            disp->clear(readU16at(p, end));
            break;
        case Op::FillRect: {
            const int x = readI16at(p, end);
            const int y = readI16at(p, end);
            const int w = readI16at(p, end);
            const int h = readI16at(p, end);
            const uint16_t c = readU16at(p, end);
            disp->fillRect(x, y, w, h, c);
            break;
        }
        case Op::FillRectAlpha: {
            const int x = readI16at(p, end);
            const int y = readI16at(p, end);
            const int w = readI16at(p, end);
            const int h = readI16at(p, end);
            const uint16_t c = readU16at(p, end);
            const uint8_t a = (p < end) ? *p++ : 0;
            disp->fillRectAlpha(x, y, w, h, c, a);
            break;
        }
        case Op::FillRects: {
            const int n = (p < end) ? *p++ : 0;
            GameDisplay::FillRect rects[64];
            int count = 0;
            for (int i = 0; i < n && count < 64; ++i) {
                rects[count].x = readI16at(p, end);
                rects[count].y = readI16at(p, end);
                rects[count].w = readI16at(p, end);
                rects[count].h = readI16at(p, end);
                rects[count].color = readU16at(p, end);
                ++count;
            }
            if (count > 0) {
                disp->fillRects(rects, static_cast<size_t>(count));
            }
            break;
        }
        case Op::DrawLine: {
            const int x0 = readI16at(p, end);
            const int y0 = readI16at(p, end);
            const int x1 = readI16at(p, end);
            const int y1 = readI16at(p, end);
            disp->drawLine(x0, y0, x1, y1, readU16at(p, end));
            break;
        }
        case Op::DrawCircle: {
            const int cx = readI16at(p, end);
            const int cy = readI16at(p, end);
            const int r = readI16at(p, end);
            disp->drawCircle(cx, cy, r, readU16at(p, end));
            break;
        }
        case Op::FillCircle: {
            const int cx = readI16at(p, end);
            const int cy = readI16at(p, end);
            const int r = readI16at(p, end);
            disp->fillCircle(cx, cy, r, readU16at(p, end));
            break;
        }
        case Op::Text: {
            const int x = readI16at(p, end);
            const int y = readI16at(p, end);
            const uint16_t fg = readU16at(p, end);
            const uint16_t bg = readU16at(p, end);
            const bool use_bg = (p < end) ? ((*p++) != 0) : true;
            char text_tmp[160];
            readStrAt(p, end, text_tmp, sizeof(text_tmp));
            disp->drawTextBg(x, y, text_tmp, fg, bg, use_bg);
            break;
        }
        case Op::Image: {
            const int id = readI16at(p, end);
            const int dx = readI16at(p, end);
            const int dy = readI16at(p, end);
            const int sx = readI16at(p, end);
            const int sy = readI16at(p, end);
            const int sw = readI16at(p, end);
            const int sh = readI16at(p, end);
            const bool keyed = (p < end) ? ((*p++) != 0) : false;
            const uint16_t key = readU16at(p, end);
            const ImageSlot* slot = interp->getImage(id);
            if (!slot) {
                break;
            }
            if (keyed) {
                disp->drawImageSubKeyed(dx, dy, slot->width, slot->height, slot->pixels, sx, sy, sw,
                                        sh, key, true);
            } else if (sx == 0 && sy == 0 && sw == slot->width && sh == slot->height) {
                disp->drawImage(dx, dy, slot->width, slot->height, slot->pixels);
            } else {
                disp->drawImageSub(dx, dy, slot->width, slot->height, slot->pixels, sx, sy, sw, sh);
            }
            break;
        }
        case Op::ImageScaled: {
            const int id = readI16at(p, end);
            const int dx = readI16at(p, end);
            const int dy = readI16at(p, end);
            const int scale = (p < end) ? *p++ : 1;
            readI16at(p, end);  // dest_h（dirty 用。再生では slot から計算）
            const ImageSlot* slot = interp->getImage(id);
            if (slot && slot->pixels) {
                disp->drawImageScaled(dx, dy, slot->width, slot->height, slot->pixels, scale);
            }
            break;
        }
        case Op::BgStream: {
            readStrAt(p, end, path_tmp, sizeof(path_tmp));
            const int dx = readI16at(p, end);
            const int dy = readI16at(p, end);
            const uint16_t w = readU16at(p, end);
            const uint16_t h = readU16at(p, end);
            interp->drawBgStreamFromSd(path_tmp, dx, dy, w, h);
            break;
        }
        case Op::BwStream: {
            readStrAt(p, end, path_tmp, sizeof(path_tmp));
            const int dx = readI16at(p, end);
            const int dy = readI16at(p, end);
            const uint16_t w = readU16at(p, end);
            const uint16_t h = readU16at(p, end);
            const uint16_t fg = readU16at(p, end);
            const uint16_t bg = readU16at(p, end);
            interp->drawBwStreamFromSd(path_tmp, dx, dy, w, h, fg, bg);
            break;
        }
        case Op::BwPack: {
            readStrAt(p, end, path_tmp, sizeof(path_tmp));
            const int frame = readI32at(p, end);
            const int dx = readI16at(p, end);
            const int dy = readI16at(p, end);
            const uint16_t w = readU16at(p, end);
            const uint16_t h = readU16at(p, end);
            const uint16_t fg = readU16at(p, end);
            const uint16_t bg = readU16at(p, end);
            interp->drawBwPackFromSd(path_tmp, frame, dx, dy, w, h, fg, bg);
            break;
        }
        case Op::VnStream: {
            // 録画時に既に sync 済み。再生はバンド描画のみ。
            // ペイロードは読み飛ばす（状態は interp->vn_stream_）。
            const uint8_t bg_on = (p < end) ? *p++ : 0;
            if (bg_on) {
                readStrAt(p, end, path_tmp, sizeof(path_tmp));
                readI16at(p, end);
                readI16at(p, end);
                readU16at(p, end);
                readU16at(p, end);
            }
            const int n = (p < end) ? *p++ : 0;
            for (int i = 0; i < n; ++i) {
                const uint8_t on = (p < end) ? *p++ : 0;
                if (!on) {
                    continue;
                }
                readStrAt(p, end, path_tmp, sizeof(path_tmp));
                readI16at(p, end);
                readI16at(p, end);
                readU16at(p, end);
                readU16at(p, end);
                readU16at(p, end);
                if (p < end) {
                    ++p;
                }
            }
            vnStreamComposeReplayBand(interp);
            break;
        }
        default:
            p = end;
            break;
    }
}

bool DrawCommandList::replayBand(LuaInterpreter* interp, GameDisplay* disp, int band_index) {
    if (!ready_ || !interp || !disp) {
        return false;
    }
    (void)band_index;
    const uint8_t* p = buf_;
    const uint8_t* end = buf_ + used_;
    while (p < end) {
        replayOne(interp, disp, p, end);
    }
    return true;
}
