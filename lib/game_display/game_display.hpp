// ============================================
// ファイル: game_display.hpp
// RGB565 フレームバッファ描画
// ============================================

#ifndef GAME_DISPLAY_HPP
#define GAME_DISPLAY_HPP

#include <cstddef>
#include <cstdint>

class ST7789_LCD;

class GameDisplay {
public:
    GameDisplay();

    void bind(uint16_t* framebuffer, uint16_t width, uint16_t height, ST7789_LCD* lcd,
              int dma_channel, uint8_t* dma_buffer, size_t dma_buffer_size);

    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }
    uint16_t* framebuffer() { return framebuffer_; }

    void clear(uint16_t color);
    void fillRect(int x, int y, int w, int h, uint16_t color);
    void drawTextBg(int x, int y, const char* text, uint16_t color, uint16_t bg_color);
    void present();

    static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);

private:
    uint16_t* framebuffer_;
    uint16_t width_;
    uint16_t height_;
    ST7789_LCD* lcd_;
    int dma_channel_;
    uint8_t* dma_buffer_;
    size_t dma_buffer_size_;
};

#endif // GAME_DISPLAY_HPP
