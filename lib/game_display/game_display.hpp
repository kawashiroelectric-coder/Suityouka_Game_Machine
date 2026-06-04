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

    GameDisplay();

    /**
     * バンドバッファと LCD・DMA リソースを関連付ける。
     * buffer_a / buffer_b: それぞれ width * buffer_height 画素の RGB565 バンドバッファ。
     *                      バンドごとに交互に使う（buffer_b は nullptr 可）。
     * width / height:      論理画面サイズ（machine.width()/height() が返す値）。
     * buffer_height:       1 バンドの行数（ラインバッファ高さ）。
     */
    void bind(uint16_t* buffer_a, uint16_t* buffer_b, uint16_t width, uint16_t height,
              uint16_t buffer_height, ST7789_LCD* lcd, int dma_channel, uint8_t* dma_buffer,
              size_t dma_buffer_size);

    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }
    /** 現在のバンドの描画先バッファ */
    uint16_t* framebuffer() { return work_buffer_; }
    /** 1 バンドの行数 */
    uint16_t bufferHeight() const { return buffer_height_; }

    /** 画面を覆うのに必要なバンド数 */
    int bandCount() const;
    /** バンド描画開始: 描画先バッファ・y 原点・行数を設定する */
    void beginBand(int band);
    /** 現在のバンドを LCD へ転送（非ブロッキング DMA をキック） */
    void endBand();
    /** 進行中 DMA の完了待ち（フレーム末尾で呼ぶ） */
    void waitForTransferComplete();

    /** 全画面を単色で塗る（起動画面など game_draw を介さない用途） */
    void fillScreen(uint16_t color);

    /** 現在のバンドを単色で塗る */
    void clear(uint16_t color);
    /** クリッピング付き矩形塗りつぶし */
    void fillRect(int x, int y, int w, int h, uint16_t color);
    /** 複数矩形をまとめて塗る */
    void fillRects(const FillRect* rects, size_t count);
    /** RGB565 画像をクリッピング付きで転写 */
    void drawImage(int dx, int dy, int img_w, int img_h, const uint16_t* pixels);
    /** RGB565 画像の一部領域を転写 */
    void drawImageSub(int dx, int dy, int img_w, int img_h, const uint16_t* pixels,
                      int sx, int sy, int sw, int sh);
    /** 8x8 フォントで背景付きテキスト描画 */
    void drawTextBg(int x, int y, const char* text, uint16_t color, uint16_t bg_color);

    /** RGB888 を RGB565 に変換 */
    static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);

private:
    /** 現在のバンドの画面上端 y */
    int bandTop() const { return band_y0_; }
    /** 現在のバンドの画面下端 y（exclusive） */
    int bandBottom() const { return band_y0_ + band_rows_; }

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
};

#endif // GAME_DISPLAY_HPP
