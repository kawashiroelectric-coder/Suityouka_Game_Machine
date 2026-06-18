// ============================================
// ファイル: game_display.hpp
// RGB565 バンド（ラインバッファ）描画
// ============================================
//
// 画面 (width x height, 例 320x240) を高さ buffer_height (例 20) の
// 横帯（バンド）に分割し、バンドごとに描画 → LCD へ DMA 転送する。
// フルフレーム分の RAM を確保できない環境向けのタイル描画方式。
//
// ホストのゲームループは 1 フレームを次の手順で描く:
//   for (int b = 0; b < bandCount(); ++b) {
//       beginBand(b);   // 描画先バッファと y 原点を決める
//       game_draw();    // 論理座標 (0..width, 0..height) で全フレームを描く
//       endBand();      // 当該バンド領域の DMA 転送をキック
//   }
//   waitForTransferComplete(); // 最後のバンド転送完了待ち
// 描画 API は論理画面座標を受け取り、内部で現在のバンドへクリップする。

#ifndef GAME_DISPLAY_HPP
#define GAME_DISPLAY_HPP

#include <cstddef>
#include <cstdint>

class ST7789_LCD;
class FontRenderer;

/** フレームバッファへの描画と LCD への DMA 転送 */
class GameDisplay {
public:
    /** fillRects 用の矩形＋色 */
    struct FillRect {
        int x;
        int y;
        int w;
        int h;
        uint16_t color;
    };

    /** 描画状態を初期化する。インスタンス生成時に使う */
    GameDisplay();

    /** バンドバッファと LCD・DMA を関連付ける。ゲームループ開始前に一度呼ぶ */
    void bind(uint16_t* buffer_a, uint16_t* buffer_b, uint16_t width, uint16_t height,
              uint16_t buffer_height, ST7789_LCD* lcd, int dma_channel, uint8_t* dma_buffer,
              size_t dma_buffer_size);

    /** 論理画面の幅（px）を返す。Lua API や描画座標計算時に使う */
    uint16_t width() const { return width_; }
    /** 論理画面の高さ（px）を返す。Lua API や描画座標計算時に使う */
    uint16_t height() const { return height_; }
    /** 現在バンドの描画先フレームバッファを返す。低レベル描画時に使う */
    uint16_t* framebuffer() { return work_buffer_; }
    /** 1 バンドの行数を返す。バンドループ設計時に使う */
    uint16_t bufferHeight() const { return buffer_height_; }

    /** 画面を覆うのに必要なバンド数を返す。フレーム描画ループの回数決定時に使う */
    int bandCount() const;
    /** 現在描画中のバンド番号を返す。game_draw 内のデバッグや最適化時に使う */
    int bandIndex() const { return band_index_; }
    /** 現在バンドの画面上端 y（含む）を返す。クリッピング計算時に使う */
    int bandTopY() const { return band_y0_; }
    /** 現在バンドの画面下端 y（含まない）を返す。クリッピング計算時に使う */
    int bandBottomY() const { return band_y0_ + band_rows_; }
    /** 論理矩形が現在バンドと交差するか判定する。描画 API の内部クリップ時に使う */
    bool rectIntersectsBand(int y, int h) const;

    /** バンド描画を開始する。各バンドの game_draw 直前に呼ぶ */
    void beginBand(int band);
    /** 現在バンドを LCD へ DMA 転送する。各バンドの game_draw 直後に呼ぶ */
    void endBand();
    /** 進行中 DMA の完了を待つ。1 フレーム末尾で呼ぶ */
    void waitForTransferComplete();

    /** DMA を解放し ST7789 直描画へ戻す。ゲーム終了後メニュー復帰前に呼ぶ */
    void releaseForDirectDraw();

    /** 全画面を単色で塗る。起動画面など game_draw を使わない描画時に呼ぶ */
    void fillScreen(uint16_t color);

    /** 現在バンドを単色で塗る。タイルレイヤー合成の下地クリア時に呼ぶ */
    void clear(uint16_t color);
    /** クリップ付き矩形を塗りつぶす。game_draw 内の図形描画時に呼ぶ */
    void fillRect(int x, int y, int w, int h, uint16_t color);
    /** 複数矩形をまとめて塗る。バッチ矩形描画時に呼ぶ */
    void fillRects(const FillRect* rects, size_t count);
    /** クリップ付き直線を描く。game_draw 内の線描画時に呼ぶ */
    void drawLine(int x0, int y0, int x1, int y1, uint16_t color);
    /** クリップ付き円の輪郭を描く。game_draw 内の円描画時に呼ぶ */
    void drawCircle(int cx, int cy, int radius, uint16_t color);
    /** クリップ付き塗りつぶし円を描く。game_draw 内の円塗り時に呼ぶ */
    void fillCircle(int cx, int cy, int radius, uint16_t color);
    /** タイルセットから 1 タイルを転写する。タイルマップ描画時に呼ぶ */
    void drawTile(int dx, int dy, int tile_w, int tile_h, int sheet_cols, const uint16_t* tileset,
                  int sheet_w, int sheet_h, int tile_index);
    /** RGB565 画像をクリップ付きで転写する。スプライト描画時に呼ぶ */
    void drawImage(int dx, int dy, int img_w, int img_h, const uint16_t* pixels);
    /** RGB565 画像の一部領域を転写する。部分スプライト描画時に呼ぶ */
    void drawImageSub(int dx, int dy, int img_w, int img_h, const uint16_t* pixels,
                      int sx, int sy, int sw, int sh);
    /** 透過色をスキップして部分矩形を転写する。キー付きスプライト描画時に呼ぶ */
    void drawImageSubKeyed(int dx, int dy, int img_w, int img_h, const uint16_t* pixels,
                           int sx, int sy, int sw, int sh, uint16_t key_color, bool key_enabled);
    /** 透過色付きでタイル 1 枚を転写する。タイルレイヤー合成時に呼ぶ */
    void drawTileKeyed(int dx, int dy, int tile_w, int tile_h, int sheet_cols, const uint16_t* tileset,
                       int sheet_w, int sheet_h, int tile_index, uint16_t key_color, bool key_enabled);
    /** 背景付きテキストを描く。UI やデバッグ文字列表示時に呼ぶ */
    void drawTextBg(int x, int y, const char* text, uint16_t color, uint16_t bg_color);

    /** 関連付けた ST7789_LCD を返す。低レベル操作が必要なときに使う */
    ST7789_LCD* lcd() const { return lcd_; }

    /** グローバル FontRenderer を登録する。ゲーム起動前の初期化時に呼ぶ */
    static void setFontRenderer(FontRenderer* font) { font_renderer_ = font; }

    /** RGB888 を RGB565 に変換する。色指定 API から呼ぶ */
    static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);

private:
    /** 現在バンドの画面上端 y（内部用） */
    int bandTop() const { return band_y0_; }
    /** 現在バンドの画面下端 y（内部用） */
    int bandBottom() const { return band_y0_ + band_rows_; }
    /** 現在バンドのフレームバッファに 1 画素を書く。基本図形描画の内部処理で使う */
    void plotPixel(int x, int y, uint16_t color);

    uint16_t* buffers_[2];
    uint16_t* work_buffer_;
    uint16_t width_;
    uint16_t height_;
    uint16_t buffer_height_;
    int band_index_;
    int current_buffer_index_;
    int inflight_buffer_index_;
    bool transfer_active_;
    int band_y0_;
    int band_rows_;
    ST7789_LCD* lcd_;
    int dma_channel_;
    uint8_t* dma_buffer_;
    size_t dma_buffer_size_;
    static FontRenderer* font_renderer_;
};

#endif // GAME_DISPLAY_HPP
