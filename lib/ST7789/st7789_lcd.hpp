#ifndef ST7789_LCD_HPP
#define ST7789_LCD_HPP

#include <stdio.h>
#include "math.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"

// 色定義（RGB565形式）
namespace Color {
    constexpr uint16_t BLACK   = 0x0000;
    constexpr uint16_t WHITE   = 0xFFFF;
    constexpr uint16_t RED     = 0xF800;
    constexpr uint16_t GREEN   = 0x07E0;
    constexpr uint16_t BLUE    = 0x001F;
    constexpr uint16_t YELLOW  = 0xFFE0;
    constexpr uint16_t CYAN    = 0x07FF;
    constexpr uint16_t MAGENTA = 0xF81F;
    constexpr uint16_t ORANGE  = 0xFD20;
    constexpr uint16_t PURPLE  = 0x780F;
    constexpr uint16_t GRAY    = 0x8410;

    
    // RGB888からRGB565への変換
    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
}

// フォントサイズ列挙型
enum class FontSize {
    SMALL  = 1,  // 8x8
    MEDIUM = 2,  // 16x16
    LARGE  = 3   // 24x24
};


// ST7789 LCDクラス
class ST7789_LCD {
private:
    // ピン定義
    static constexpr uint8_t PIN_CS   = 1;
    static constexpr uint8_t PIN_SCK  = 2;
    static constexpr uint8_t PIN_MOSI = 3;
    static constexpr uint8_t PIN_RST  = 8;
    static constexpr uint8_t PIN_DC   = 9;
    static constexpr uint8_t PIN_BLK  = 14;
    
    // ディスプレイサイズ
    static constexpr uint16_t PHYSICAL_WIDTH  = 240;
    static constexpr uint16_t PHYSICAL_HEIGHT = 320;
    
    
    // 現在の論理サイズ（回転により変化）
    uint16_t _width;
    uint16_t _height;
    uint8_t _rotation;
    uint16_t _textColor;
    uint16_t _textBgColor;
    FontSize _fontSize;
    bool _textBg;  // 背景色を描画するかどうか
    
    // ST7789コマンド
    enum Command : uint8_t {
        NOP     = 0x00,
        SWRESET = 0x01,
        SLPIN   = 0x10,
        SLPOUT  = 0x11,
        PTLON   = 0x12,
        NORON   = 0x13,
        INVOFF  = 0x20,
        INVON   = 0x21,
        DISPOFF = 0x28,
        DISPON  = 0x29,
        CASET   = 0x2A,
        RASET   = 0x2B,
        RAMWR   = 0x2C,
        RAMRD   = 0x2E,
        PTLAR   = 0x30,
        TEOFF   = 0x34,
        TEON    = 0x35,
        MADCTL  = 0x36,
        COLMOD  = 0x3A,

        MADCTL_MY = 0x80,
        MADCTL_MX = 0x40,
        MADCTL_MV = 0x20,
        MADCTL_ML = 0x10,
        MADCTL_RGB = 0x00,

        RDID1 = 0xDA,
        RDID2 = 0xDB,
        RDID3 = 0xDC,
        RDID4 = 0xDD
        
    };
    
    spi_inst_t* spi_port;
    
    // コマンド送信
    void writeCommand(uint8_t cmd);
    
    // データ送信（1バイト）
    void writeData(uint8_t data);

    // データ送信を連続で（1バイト）
    void writeData_continue(const uint8_t* data, size_t len);

    
    // データ送信（複数バイト）
    void writeDataBuffer(const uint8_t* buf, size_t len);

    // ハードウェアリセット
    void hardwareReset();
    
    // 描画範囲設定
    void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

public:
    ST7789_LCD();
    
    // 初期化
    void init();
    
    // バックライト制御
    void setBacklight(bool on);

    //反転表示
    void invertDisplay(bool i);

    // SPI/GPIO取得（DMA用）
    spi_inst_t* getSPI() const { return spi_port; }
    uint8_t getPinCS() const { return PIN_CS; }
    uint8_t getPinDC() const { return PIN_DC; }
    
    // 描画範囲設定（外部公開）
    void setWindowPublic(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
        setWindow(x0, y0, x1, y1);
    }
    
    // 画面全体を塗りつぶし
    void fill(uint16_t color);
    
    // 矩形描画
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    
    // DMA対応矩形描画（外部から使用）
    void fillRectDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, 
                     int dma_channel, uint8_t* dma_buffer, size_t buffer_size);

    //円塗りつぶし
    void fillCircle(uint16_t x0,uint16_t y0,uint16_t r,uint16_t color);
    
    // ピクセル描画
    void drawPixel(uint16_t x, uint16_t y, uint16_t color);
    
    // ピクセル描画(高速)
    void writePixel(uint16_t x, uint16_t y, uint16_t color);
    
    // 線描画（水平）
    void drawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
    
    // 線描画（垂直）
    void drawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);

    // 線描画（任意角度・Bresenhamアルゴリズム）
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

    // 矩形枠描画
    void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

    //円描写
    void drawCircle(uint16_t x0,uint16_t y0,uint16_t r,uint16_t color);

    //画面回転（0, 1, 2, 3 = 0°, 90°, 180°, 270°）
    void setRotation(uint8_t r);

    
    // テキスト描画関連
    void setTextColor(uint16_t color);
    void setTextColor(uint16_t color, uint16_t bgColor);
    void setFontSize(FontSize size);
    void drawChar(uint16_t x, uint16_t y, char c, uint16_t color);
    void drawText(uint16_t x, uint16_t y, const char* text);
    void drawTextBg(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bgColor);
    
    // 画像描画関連
    void drawBMP(uint16_t x, uint16_t y, const uint16_t* bmpData);
    void drawRawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data);
    
    // DMA対応画像描画（外部から使用）
    void drawRawImageDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, 
                         const uint16_t* data, int dma_channel, 
                         uint8_t* dma_buffer, size_t buffer_size);

    // ディスプレイサイズ取得
    uint16_t width() const { return _width; }
    uint16_t height() const { return _height; }
    uint8_t rotation() const { return _rotation; }
    
};

#endif // ST7789_LCD_HPP