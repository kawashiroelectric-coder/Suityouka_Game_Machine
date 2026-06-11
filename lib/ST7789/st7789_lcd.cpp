// ============================================
// ファイル: st7789_lcd.cpp
// ST7789 LCD ドライバ実装
// ============================================

#include "st7789_lcd.hpp"
#include <cstring>

// 8x8 ASCIIフォント (簡易版)
static const uint8_t font8x8[96][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 空白
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // !
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // "
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // #
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // $
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // %
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // &
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // '
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // (
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // )
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // *
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // +
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ,
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // .
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // /
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // 0
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // 1
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // 2
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // 3
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // 4
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // 5
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // 6
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // 7
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // 8
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // 9
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // :
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ;
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // <
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // =
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // >
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // ?
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // @
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // A
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // B
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // C
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // D
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // E
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // F
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // G
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // H
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // I
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // J
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // K
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // L
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // M
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // N
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // O
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // P
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // Q
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // R
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // S
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // T
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // U
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // V
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // W
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // X
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // Y
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // Z
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // [
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // backslash
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // ]
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // ^
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // _
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // `
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // a
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // b
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // c
    {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00}, // d
    {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00}, // e
    {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00}, // f
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // g
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // h
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // i
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // j
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // k
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // l
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // m
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // n
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // o
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // p
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // q
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // r
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // s
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // t
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // u
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // v
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // w
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // x
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // y
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // z
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // {
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // |
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // }
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ~
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // DEL
};


ST7789_LCD::ST7789_LCD()
    : spi_port(LCDConfig::spiHw()),
      _width(LCDConfig::PHYSICAL_WIDTH),
      _height(LCDConfig::PHYSICAL_HEIGHT),
                            _rotation(0), _textColor(Color::WHITE), _textBgColor(Color::BLACK),
                            _fontSize(FontSize::SMALL), _textBg(false) {}


using uint8_t = unsigned char;  // C++11以降の書き方
using uint16_t = unsigned short;

/** DC=0 で 1 バイトコマンド送信 */
void ST7789_LCD::writeCommand(uint8_t cmd) {
    gpio_put(LCDConfig::PIN_DC, 0);
    gpio_put(LCDConfig::PIN_CS, 0);
    spi_write_blocking(spi_port, &cmd, 1);
    gpio_put(LCDConfig::PIN_CS, 1);
}

/** DC=1 で 1 バイトデータ送信 */
void ST7789_LCD::writeData(uint8_t data) {
    gpio_put(LCDConfig::PIN_DC, 1);
    gpio_put(LCDConfig::PIN_CS, 0);
    spi_write_blocking(spi_port, &data, 1);
    gpio_put(LCDConfig::PIN_CS, 1);
}


/** DC=1 でバッファを一括送信 */
void ST7789_LCD::writeDataBuffer(const uint8_t* buf, size_t len) {
    gpio_put(LCDConfig::PIN_DC, 1);
    gpio_put(LCDConfig::PIN_CS, 0);
    spi_write_blocking(spi_port, buf, len);
    gpio_put(LCDConfig::PIN_CS, 1);
}

/** RST ピンでハードウェアリセットパルス */
void ST7789_LCD::hardwareReset() {
    gpio_put(LCDConfig::PIN_RST, 1);
    sleep_ms(5);
    gpio_put(LCDConfig::PIN_RST, 0);
    sleep_ms(20);
    gpio_put(LCDConfig::PIN_RST, 1);
    sleep_ms(150);
}

/** CASET / RASET / RAMWR で描画矩形を設定 */
void ST7789_LCD::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    writeCommand(Command::CASET);
    writeData(x0 >> 8);
    writeData(x0 & 0xFF);
    writeData(x1 >> 8);
    writeData(x1 & 0xFF);
    
    writeCommand(Command::RASET);
    writeData(y0 >> 8);
    writeData(y0 & 0xFF);
    writeData(y1 >> 8);
    writeData(y1 & 0xFF);
    
    writeCommand(Command::RAMWR);
}

/** GPIO/SPI 初期化と ST7789 レジスタシーケンス */
void ST7789_LCD::init() {
    // GPIO初期化
    gpio_init(LCDConfig::PIN_CS);
    gpio_init(LCDConfig::PIN_RST);
    gpio_init(LCDConfig::PIN_DC);
    gpio_init(LCDConfig::PIN_BLK);
    
    gpio_set_dir(LCDConfig::PIN_CS, GPIO_OUT);
    gpio_set_dir(LCDConfig::PIN_RST, GPIO_OUT);
    gpio_set_dir(LCDConfig::PIN_DC, GPIO_OUT);
    gpio_set_dir(LCDConfig::PIN_BLK, GPIO_OUT);
    
    gpio_put(LCDConfig::PIN_CS, 1);
    gpio_put(LCDConfig::PIN_BLK, 1);  // バックライトON
    
    // SPI初期化
    spi_init(spi_port, LCDConfig::SPI_BAUD_HZ);
    
    //spi_set_format(spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST); //現状不要 spimode3に変更時はコメントアウト解除
    gpio_set_function(LCDConfig::PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCDConfig::PIN_MOSI, GPIO_FUNC_SPI);
    
    // ディスプレイリセット
    hardwareReset();
    
    // 初期化シーケンス
    writeCommand(Command::SWRESET);
    sleep_ms(150);
    
    writeCommand(Command::SLPOUT);
    sleep_ms(255);
    
    writeCommand(Command::COLMOD);
    writeData(0x55);  // 16ビットカラー
    // Set rotation to landscape (match GameConfig: 320x240)
    setRotation(LCDConfig::DEFAULT_ROTATION);
    
    writeCommand(Command::INVON);
    
    writeCommand(Command::NORON);
    sleep_ms(10);
    
    writeCommand(Command::DISPON);
    sleep_ms(100);
}

void ST7789_LCD::setBacklight(bool on) {
    gpio_put(LCDConfig::PIN_BLK, on ? 1 : 0);
}


void ST7789_LCD::invertDisplay(bool i){
    if(i == 0){
    writeCommand(Command::INVOFF);
    }else{
    writeCommand(Command::INVON);
    }
}


void ST7789_LCD::fill(uint16_t color) {
        fillRect(0, 0, _width, _height, color);
}

void ST7789_LCD::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (x >= _width || y >= _height) return;
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;
    
    setWindow(x, y, x + w - 1, y + h - 1);
    
    uint8_t colorBuf[2] = {static_cast<uint8_t>(color >> 8), 
                            static_cast<uint8_t>(color & 0xFF)};
    
    gpio_put(LCDConfig::PIN_DC, 1);
    gpio_put(LCDConfig::PIN_CS, 0);
    
    for (uint32_t i = 0; i < w * h; i++) {
        spi_write_blocking(spi_port, colorBuf, 2);
    }
    
    gpio_put(LCDConfig::PIN_CS, 1);
}



void ST7789_LCD::fillRectDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color,
                              int dma_channel, uint8_t* dma_buffer, size_t buffer_size) {
    if (x >= _width || y >= _height) return;
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;
    
    setWindow(x, y, x + w - 1, y + h - 1);
    
    uint8_t colorBuf[2] = {static_cast<uint8_t>(color >> 8), 
                            static_cast<uint8_t>(color & 0xFF)};
    
    uint32_t pixelCount = w * h;
    uint32_t bufferPixels = buffer_size / 2;
    
    // バッファに色データを詰める
    for (uint32_t i = 0; i < bufferPixels * 2; i += 2) {
        dma_buffer[i] = colorBuf[0];
        dma_buffer[i + 1] = colorBuf[1];
    }
    
    gpio_put(LCDConfig::PIN_DC, 1);
    gpio_put(LCDConfig::PIN_CS, 0);
    
    // DMA転送（必要に応じて複数回）
    uint32_t remaining = pixelCount;
    while (remaining > 0) {
        uint32_t transferPixels = (remaining > bufferPixels) ? bufferPixels : remaining;
        
        dma_channel_wait_for_finish_blocking(dma_channel);
        
        
        dma_channel_config cfg = dma_channel_get_default_config(dma_channel);
        
        dma_channel_configure(
            dma_channel,
            &cfg,
            &spi_get_hw(spi_port)->dr,
            dma_buffer,
            transferPixels * 2,
            true
        );
        
        dma_channel_wait_for_finish_blocking(dma_channel);
        remaining -= transferPixels;
    }
    
    gpio_put(LCDConfig::PIN_CS, 1);
}


void ST7789_LCD::fillCircle(uint16_t x0,uint16_t y0,uint16_t r,uint16_t color){

}

void ST7789_LCD::drawPixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= _width || y >= _height) return;
    
    setWindow(x, y, x, y);
    
    uint8_t colorBuf[2] = {static_cast<uint8_t>(color >> 8), 
                            static_cast<uint8_t>(color & 0xFF)};
    writeDataBuffer(colorBuf, 2);
}

void ST7789_LCD::writePixel(uint16_t x, uint16_t y, uint16_t color){
    if (x >= _width || y >= _height) return;
    
    setWindow(x, y, x, y);
    
    uint8_t colorBuf_continue[2] = {static_cast<uint8_t>(color >> 8), 
                            static_cast<uint8_t>(color & 0xFF)};
    writeData_continue(colorBuf_continue,2);
}


void ST7789_LCD::drawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color) {
    fillRect(x, y, w, 1, color);
}

void ST7789_LCD::drawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color) {
    fillRect(x, y, 1, h, color);
}


void ST7789_LCD::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;
    
    while (true) {
        drawPixel(x0, y0, color);
        
        if (x0 == x1 && y0 == y1) break;
        
        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void ST7789_LCD::drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    drawHLine(x, y, w, color);
    drawHLine(x, y + h - 1, w, color);
    drawVLine(x, y, h, color);
    drawVLine(x + w - 1, y, h, color);
}

void ST7789_LCD::drawCircle(uint16_t x0,uint16_t y0,uint16_t r,uint16_t color){
    int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;

    gpio_put(LCDConfig::PIN_CS, 0);
     writePixel(x0, y0 + r, color);
     writePixel(x0, y0 - r, color);
     writePixel(x0 + r, y0, color);
     writePixel(x0 - r, y0, color);

  while (x < y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;

    writePixel(x0 + x, y0 + y, color);
    writePixel(x0 - x, y0 + y, color);
    writePixel(x0 + x, y0 - y, color);
    writePixel(x0 - x, y0 - y, color);
    writePixel(x0 + y, y0 + x, color);
    writePixel(x0 - y, y0 + x, color);
    writePixel(x0 + y, y0 - x, color);
    writePixel(x0 - y, y0 - x, color);
  }
    gpio_put(LCDConfig::PIN_CS, 1);
}

void ST7789_LCD::setRotation(uint8_t r) {
    _rotation = r % 4;  // 0-3の範囲に制限
    
    writeCommand(Command::MADCTL);
    
    switch (_rotation) {
        case 0:  // 0° (ポートレート)
            writeData(0x00);
            _width = LCDConfig::PHYSICAL_WIDTH;
            _height = LCDConfig::PHYSICAL_HEIGHT;
            break;
        case 1:  // 90° (ランドスケープ)
            writeData(0x60);
            _width = LCDConfig::PHYSICAL_HEIGHT;
            _height = LCDConfig::PHYSICAL_WIDTH;
            break;
        case 2:  // 180° (ポートレート反転)
            writeData(0xC0);
            _width = LCDConfig::PHYSICAL_WIDTH;
            _height = LCDConfig::PHYSICAL_HEIGHT;
            break;
        case 3:  // 270° (ランドスケープ反転)
            writeData(0xA0);
            _width = LCDConfig::PHYSICAL_HEIGHT;
            _height = LCDConfig::PHYSICAL_WIDTH;
            break;
    }
}

void ST7789_LCD::setTextColor(uint16_t color) {
    _textColor = color;
    _textBg = false;
}

void ST7789_LCD::setTextColor(uint16_t color, uint16_t bgColor) {
    _textColor = color;
    _textBgColor = bgColor;
    _textBg = true;
}

void ST7789_LCD::setFontSize(FontSize size) {
    _fontSize = size;
}

void ST7789_LCD::drawChar(uint16_t x, uint16_t y, char c, uint16_t color) {
    if (c < 32 || c > 127) c = 32; // 印字可能文字のみ
    
    const uint8_t* glyph = font8x8[c - 32];
    uint8_t scale = static_cast<uint8_t>(_fontSize);
    
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t line = glyph[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (line & (1 << j)) {
                if (scale == 1) {
                    drawPixel(x + j, y + i, color);
                } else {
                    fillRect(x + j * scale, y + i * scale, scale, scale, color);
                }
            } else if (_textBg) {
                if (scale == 1) {
                    drawPixel(x + j, y + i, _textBgColor);
                } else {
                    fillRect(x + j * scale, y + i * scale, scale, scale, _textBgColor);
                }
            }
        }
    }
}

void ST7789_LCD::drawText(uint16_t x, uint16_t y, const char* text) {
    uint8_t scale = static_cast<uint8_t>(_fontSize);
    uint16_t cursorX = x;
    
    while (*text) {
        if (*text == '\n') {
            cursorX = x;
            y += 8 * scale;
        } else {
            drawChar(cursorX, y, *text, _textColor);
            cursorX += 8 * scale;
        }
        text++;
    }
}

void ST7789_LCD::drawTextBg(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bgColor) {
    setTextColor(color, bgColor);
    drawText(x, y, text);
    _textBg = false;
}

void ST7789_LCD::drawRawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data) {
    if (x >= _width || y >= _height) return;
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;
    
    setWindow(x, y, x + w - 1, y + h - 1);
    
    gpio_put(LCDConfig::PIN_DC, 1);
    gpio_put(LCDConfig::PIN_CS, 0);
    
    for (uint32_t i = 0; i < w * h; i++) {
        uint16_t color = data[i];
        uint8_t colorBuf[2] = {static_cast<uint8_t>(color >> 8), 
                                static_cast<uint8_t>(color & 0xFF)};
        spi_write_blocking(spi_port, colorBuf, 2);
    }
    
    gpio_put(LCDConfig::PIN_CS, 1);
}
/*
void ST7789_LCD::drawRawImageDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, 
                                  const uint16_t* data, int dma_channel, 
                                  uint8_t* dma_buffer, size_t buffer_size) {
    if (x >= _width || y >= _height) return;
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;
    
    setWindow(x, y, x + w - 1, y + h - 1);
    
    gpio_put(LCDConfig::PIN_DC, 1);
    gpio_put(LCDConfig::PIN_CS, 0);
    
    uint32_t pixelCount = w * h;
    uint32_t bufferPixels = buffer_size / 2;
    uint32_t remaining = pixelCount;
    
    // dataポインタから直接順番に読む（行単位での計算は不要）
    for (uint32_t row = 0; row < h; row++) {
        uint32_t rowRemaining = w;
        uint32_t colOffset = 0;
        
        while (rowRemaining > 0) {
            uint32_t transferPixels = (rowRemaining > bufferPixels) ? bufferPixels : rowRemaining;
            
            // 現在の行の正しい位置からデータを取得
            const uint16_t* srcData = data + row * _width + colOffset;
            
            for (uint32_t i = 0; i < transferPixels; i++) {
                uint16_t color = srcData[i];
                dma_buffer[i * 2] = color >> 8;
                dma_buffer[i * 2 + 1] = color & 0xFF;
            }
            
            dma_channel_wait_for_finish_blocking(dma_channel);
            
            dma_channel_config c = dma_channel_get_default_config(dma_channel);
            channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
            channel_config_set_dreq(&c, spi_get_dreq(spi_port, true));
            channel_config_set_read_increment(&c, true);
            channel_config_set_write_increment(&c, false);
            
            dma_channel_configure(
                dma_channel,
                &c,
                &spi_get_hw(spi_port)->dr,
                dma_buffer,
                transferPixels * 2,
                true
            );
            
            dma_channel_wait_for_finish_blocking(dma_channel);
            
            rowRemaining -= transferPixels;
            colOffset += transferPixels;
        }
    }
    
    gpio_put(LCDConfig::PIN_CS, 1);
}
*/
/** SPI 送信完了まで待機 */
static void spiWaitIdle(spi_inst_t* spi_port) {
    while (!(spi_get_hw(spi_port)->sr & SPI_SSPSR_TFE_BITS)) {
        tight_loop_contents();
    }
    while (spi_get_hw(spi_port)->sr & SPI_SSPSR_BSY_BITS) {
        tight_loop_contents();
    }
}

void ST7789_LCD::dmaAsyncFinish() {
    gpio_put(LCDConfig::PIN_CS, 1);
    dma_async_.active = false;
    dma_async_.data = nullptr;
}

void ST7789_LCD::dmaAsyncStartChunk() {
    if (!dma_async_.active || !dma_async_.data || !dma_async_.dma_buffer ||
        dma_async_.dma_buffer_size < 2 || dma_async_.dma_channel < 0) {
        dmaAsyncFinish();
        return;
    }

    const uint32_t buffer_pixels = static_cast<uint32_t>(dma_async_.dma_buffer_size / 2);
    const uint32_t stride = dma_async_.src_stride ? dma_async_.src_stride : dma_async_.w;

    while (dma_async_.row < dma_async_.h) {
        const uint16_t* row_src =
            dma_async_.data + dma_async_.row * stride + dma_async_.col_processed;
        uint32_t transfer_pixels = dma_async_.w - dma_async_.col_processed;
        if (transfer_pixels > buffer_pixels) {
            transfer_pixels = buffer_pixels;
        }

        uint8_t* out = dma_async_.dma_buffer;
        for (uint32_t i = 0; i < transfer_pixels; i++) {
            const uint16_t color = row_src[i];
            out[i * 2] = static_cast<uint8_t>(color >> 8);
            out[i * 2 + 1] = static_cast<uint8_t>(color & 0xFF);
        }

        dma_channel_config c = dma_channel_get_default_config(dma_async_.dma_channel);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
        channel_config_set_dreq(&c, spi_get_dreq(spi_port, true));
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);

        dma_channel_configure(dma_async_.dma_channel, &c, &spi_get_hw(spi_port)->dr, out,
                              transfer_pixels * 2, true);

        dma_async_.col_processed += transfer_pixels;
        if (dma_async_.col_processed >= dma_async_.w) {
            dma_async_.col_processed = 0;
            dma_async_.row++;
        }
        return;
    }

    spiWaitIdle(spi_port);
    dmaAsyncFinish();
}

bool ST7789_LCD::beginDrawRawImageDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      const uint16_t* data, uint32_t src_stride, int dma_channel,
                                      uint8_t* dma_buffer, size_t buffer_size) {
    if (dma_async_.active) return false;
    if (x >= _width || y >= _height || !data || !dma_buffer || buffer_size < 2 ||
        dma_channel < 0) {
        return false;
    }
    if (x + w > _width) w = _width - x;
    if (y + h > _height) h = _height - y;
    if (w == 0 || h == 0) return false;

    dma_async_.active = true;
    dma_async_.x = x;
    dma_async_.y = y;
    dma_async_.w = w;
    dma_async_.h = h;
    dma_async_.data = data;
    dma_async_.src_stride = src_stride ? src_stride : w;
    dma_async_.dma_channel = dma_channel;
    dma_async_.dma_buffer = dma_buffer;
    dma_async_.dma_buffer_size = buffer_size;
    dma_async_.row = 0;
    dma_async_.col_processed = 0;

    setWindow(x, y, x + w - 1, y + h - 1);
    gpio_put(LCDConfig::PIN_DC, 1);
    gpio_put(LCDConfig::PIN_CS, 0);
    dmaAsyncStartChunk();
    return dma_async_.active;
}

void ST7789_LCD::pumpDrawRawImageDMA() {
    if (!dma_async_.active) return;
    if (dma_channel_is_busy(dma_async_.dma_channel)) return;
    dmaAsyncStartChunk();
}

void ST7789_LCD::drawRawImageDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                 const uint16_t* data, int dma_channel, uint8_t* dma_buffer,
                                 size_t buffer_size) {
    while (isDrawRawImageDMABusy()) {
        pumpDrawRawImageDMA();
    }
    if (!beginDrawRawImageDMA(x, y, w, h, data, w, dma_channel, dma_buffer, buffer_size)) {
        return;
    }
    while (isDrawRawImageDMABusy()) {
        pumpDrawRawImageDMA();
    }
}

void ST7789_LCD::drawBMP(uint16_t x, uint16_t y, const uint16_t* bmpData) {
    // BMPヘッダー解析 (簡易版 - 24bit BMPのみ対応)
    if (bmpData[0] != 'B' || bmpData[1] != 'M') return;
    
    uint32_t dataOffset = bmpData[10] | (bmpData[11] << 8) | (bmpData[12] << 16) | (bmpData[13] << 24);
    uint32_t width = bmpData[18] | (bmpData[19] << 8) | (bmpData[20] << 16) | (bmpData[21] << 24);
    uint32_t height = bmpData[22] | (bmpData[23] << 8) | (bmpData[24] << 16) | (bmpData[25] << 24);
    uint16_t bpp = bmpData[28] | (bmpData[29] << 8);
    
    if (bpp != 24) return; // 24bitのみ対応
    
    if (x >= _width || y >= _height) return;
    if (x + width > _width) width = _width - x;
    if (y + height > _height) height = _height - y;
    
    setWindow(x, y, x + width - 1, y + height - 1);
    
    gpio_put(LCDConfig::PIN_DC, 1);
    gpio_put(LCDConfig::PIN_CS, 0);
    
    // BMPは下から上に格納されているので反転して描画
    uint32_t rowSize = ((width * 3 + 3) / 4) * 4; // 4バイトアライメント
    
    for (int32_t row = height - 1; row >= 0; row--) {
        for (uint32_t col = 0; col < width; col++) {
            uint32_t offset = dataOffset + row * rowSize + col * 3;
            uint8_t b = bmpData[offset];
            uint8_t g = bmpData[offset + 1];
            uint8_t r = bmpData[offset + 2];
            
            uint16_t color = Color::rgb(r, g, b);
            uint8_t colorBuf[2] = {static_cast<uint8_t>(color >> 8), 
                                    static_cast<uint8_t>(color & 0xFF)};
            spi_write_blocking(spi_port, colorBuf, 2);
        }
    }
    
    gpio_put(LCDConfig::PIN_CS, 1);
}