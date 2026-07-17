// ============================================
// ファイル: draw_command_list.hpp
// game_draw 1 回分を録画し、バンドごとに C 側で再生する
// ============================================
//
// 目的: Lua game_draw の 12 回呼び出しを 1 回に減らし、前フレームと
//       帯ごとの描画内容が同一なら LCD 転送をスキップする。
// バッファは静的 12KB（十数 KB 以内の増加を許容した設計）。

#ifndef DRAW_COMMAND_LIST_HPP
#define DRAW_COMMAND_LIST_HPP

#include <cstddef>
#include <cstdint>

class GameDisplay;
class LuaInterpreter;

/** game_draw 録画バッファと dirty 帯判定 */
class DrawCommandList {
public:
    static constexpr size_t kBufferBytes = 12 * 1024;
    static constexpr int kMaxBands = 16;

    DrawCommandList() = default;

    /** 録画開始。成功時 true（バッファクリア） */
    void beginRecord(uint16_t screen_w, uint16_t screen_h, uint16_t band_h);
    /** 録画終了。overflow / unsupported が無ければ ready */
    bool endRecord();

    bool isRecording() const { return recording_; }
    bool isReady() const { return ready_; }
    bool failed() const { return failed_; }
    /** 非対応 API や容量不足で録画失敗にした */
    void markFailed();

    /** 録画モード中の仮想バンド情報（全画面相当） */
    int recordBandIndex() const { return 0; }
    int recordBandTop() const { return 0; }
    int recordBandBottom() const { return static_cast<int>(screen_h_); }
    int recordBandHeight() const { return static_cast<int>(screen_h_); }
    bool recordRectInBand(int /*y*/, int /*h*/) const { return true; }

    // --- 録画 API（容量不足時は markFailed） ---
    void recClear(uint16_t color);
    void recFillRect(int x, int y, int w, int h, uint16_t color);
    void recFillRectAlpha(int x, int y, int w, int h, uint16_t color, uint8_t alpha);
    void recFillRects(const int* xywhc, int count);  // count 組の x,y,w,h,color
    void recDrawLine(int x0, int y0, int x1, int y1, uint16_t color);
    void recDrawCircle(int cx, int cy, int r, uint16_t color);
    void recFillCircle(int cx, int cy, int r, uint16_t color);
    void recText(int x, int y, const char* text, uint16_t fg, uint16_t bg, bool use_bg);
    void recImage(int id, int dx, int dy, int sx, int sy, int sw, int sh, bool keyed,
                  uint16_t key_color);
    /** 整数倍スケール画像（回転なし）。dest_h = 元画像高さ * scale */
    void recImageScaled(int id, int dx, int dy, int scale, int dest_h);
    void recBgStream(const char* path, int dx, int dy, uint16_t w, uint16_t h);
    void recBwStream(const char* path, int dx, int dy, uint16_t w, uint16_t h, uint16_t fg,
                     uint16_t bg);
    void recBwPack(const char* path, int frame_index, int dx, int dy, uint16_t w, uint16_t h,
                   uint16_t fg, uint16_t bg);
    /** VN: レイヤー同期済み前提のマーカー（パス指紋を録画して dirty 判定に使う） */
    void recVnStreamMarker(LuaInterpreter* interp);

    /**
     * 帯ごとにコマンドを再生する。prev と内容同一なら false（スキップ可）。
     * 再生したら true。
     */
    bool replayBand(LuaInterpreter* interp, GameDisplay* disp, int band_index);

    /** 全帯の dirty マスクを前回ハッシュと比較して更新し、全スキップなら true */
    bool computeDirtyBands(uint16_t* out_mask);

    /** このフレームの帯ハッシュを次回比較用に確定する */
    void commitBandHashes();

    /** ゲーム終了時に状態を捨てる */
    void reset();

private:
    enum class Op : uint8_t {
        Clear = 1,
        FillRect,
        FillRects,
        DrawLine,
        DrawCircle,
        FillCircle,
        Text,
        Image,
        ImageScaled,
        BgStream,
        BwStream,
        BwPack,
        VnStream,
        FillRectAlpha,
    };

    uint8_t buf_[kBufferBytes]{};
    size_t used_ = 0;
    bool recording_ = false;
    bool ready_ = false;
    bool failed_ = false;
    uint16_t screen_w_ = 320;
    uint16_t screen_h_ = 240;
    uint16_t band_h_ = 20;
    int band_count_ = 12;

    uint32_t band_hash_[kMaxBands]{};
    uint32_t prev_band_hash_[kMaxBands]{};
    bool have_prev_hashes_ = false;

    uint8_t* alloc(size_t n);
    bool writeU8(uint8_t v);
    bool writeU16(uint16_t v);
    bool writeI16(int16_t v);
    bool writeI32(int32_t v);
    bool writeBytes(const void* p, size_t n);
    bool writeString(const char* s);

    static uint32_t fnv1a(uint32_t h, const void* p, size_t n);
    uint32_t hashBandCommands(int band_index) const;
    static bool cmdIntersectsBand(int y, int h, int band_y0, int band_y1);

    void replayOne(LuaInterpreter* interp, GameDisplay* disp, const uint8_t*& p, const uint8_t* end);
};

#endif  // DRAW_COMMAND_LIST_HPP
