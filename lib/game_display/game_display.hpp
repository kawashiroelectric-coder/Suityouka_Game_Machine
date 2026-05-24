// ============================================
// ファイル: game_display.hpp
// RGB565 フレームバッファ描画
// ============================================

#ifndef GAME_DISPLAY_HPP
#define GAME_DISPLAY_HPP

#include <cstddef>
#include <cstdint>

class ST7789_LCD;

/** フレームバッファへの描画と LCD への DMA 転送 */
class GameDisplay {
public:
    /** 画面更新方式: 全面転送 / 変更矩形のみ */
    enum class PresentMode { Full, Partial };

    /** fillRects 用の矩形＋色 */
    struct FillRect {
        int x;
        int y;
        int w;
        int h;
        uint16_t color;
    };

    GameDisplay();

    /** フレームバッファ・LCD・DMA バッファを関連付ける */
    void bind(uint16_t* framebuffer, uint16_t width, uint16_t height, ST7789_LCD* lcd,
              int dma_channel, uint8_t* dma_buffer, size_t dma_buffer_size);

    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }
    uint16_t* framebuffer() { return framebuffer_; }

    void setPresentMode(PresentMode mode) { present_mode_ = mode; }
    PresentMode presentMode() const { return present_mode_; }

    /** 全面を単色で塗り、dirty 領域を全画面にする */
    void clear(uint16_t color);
    /** クリッピング付き矩形塗りつぶし */
    void fillRect(int x, int y, int w, int h, uint16_t color);
    /** 複数矩形をまとめて塗る */
    void fillRects(const FillRect* rects, size_t count);
    /** 8x8 フォントで背景付きテキスト描画 */
    void drawTextBg(int x, int y, const char* text, uint16_t color, uint16_t bg_color);
    /** 現在の present_mode_ で LCD に転送（Partial 時は dirty 領域のみ） */
    void present();
    /** フレームバッファ全体を DMA 転送 */
    void presentFull();
    /** dirty 矩形のみ DMA 転送 */
    void presentPartial();

    /** RGB888 を RGB565 に変換 */
    static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);

private:
    /** 描画範囲を dirty 矩形にマージする（Partial 用） */
    void markDirtyRect(int x0, int y0, int x1, int y1);

    uint16_t* framebuffer_;
    uint16_t width_;
    uint16_t height_;
    ST7789_LCD* lcd_;
    int dma_channel_;
    uint8_t* dma_buffer_;
    size_t dma_buffer_size_;
    PresentMode present_mode_;
    bool dirty_;
    int dirty_x0_;
    int dirty_y0_;
    int dirty_x1_;
    int dirty_y1_;
};

#endif // GAME_DISPLAY_HPP
