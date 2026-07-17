// ============================================
// ファイル: st7789_lcd.hpp
// ST7789 TFT LCD ドライバ（SPI）
// ============================================

#ifndef ST7789_LCD_HPP
#define ST7789_LCD_HPP

#include <stdio.h>
#include "math.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "config.hpp"

/** RGB565 定数と rgb() 変換 */
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

    /** 8bit RGB を RGB565 16bit カラー値に変換する */
    constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
}

/** 8x8 ビットマップフォントの拡大倍率 */
enum class FontSize {
    SMALL  = 1,
    MEDIUM = 2,
    LARGE  = 3
};

/** ST7789 240x320 液晶（SPI0、回転対応） */
class ST7789_LCD {
public:
    static constexpr int kBacklightMinPercent = 10;
    static constexpr int kBacklightMaxPercent = 100;

private:
    uint16_t _width;
    uint16_t _height;
    uint8_t _rotation;
    uint16_t _textColor;
    uint16_t _textBgColor;
    FontSize _fontSize;
    bool _textBg;
    
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

    /** 非ブロッキング DMA 転送の進行状態 */
    struct AsyncDmaState {
        bool active = false;
        uint16_t x = 0;
        uint16_t y = 0;
        uint16_t w = 0;
        uint16_t h = 0;
        const uint16_t* data = nullptr;
        uint32_t src_stride = 0;
        int dma_channel = -1;
        uint8_t* dma_buffer = nullptr;
        size_t dma_buffer_size = 0;
        uint32_t row = 0;
        uint32_t col_processed = 0;
        /** バウンスを 2 分割し、片側 DMA 中にもう片側をバイトスワップ準備 */
        uint8_t dma_half = 0;
        uint8_t prep_half = 0;
        uint32_t prep_pixels = 0;
        bool prep_valid = false;
        bool dma_started = false;
    };
    AsyncDmaState dma_async_;

    static constexpr uint16_t kBacklightPwmWrap = 255;
    int _backlight_percent = 80;
    bool _backlight_pwm_ready = false;

    /** バックライトピンを PWM 初期化する */
    void initBacklightPwm();
    /** バックライト輝度を PWM または GPIO で出力する */
    void applyBacklightPwm();
    /** 非同期 DMA 転送の次チャンクを開始する */
    void dmaAsyncStartChunk();
    /** 非同期 DMA 転送を完了し状態をリセットする */
    void dmaAsyncFinish();

    /** DC=0: コマンド 1 バイト */
    void writeCommand(uint8_t cmd);
    /** DC=1: データ 1 バイト */
    void writeData(uint8_t data);
    /** RAMWR 連続書き込み中のデータ送信 */
    void writeData_continue(const uint8_t* data, size_t len);
    /** DC=1: データバッファ送信 */
    void writeDataBuffer(const uint8_t* buf, size_t len);
    /** RST ピンリセット */
    void hardwareReset();
    /** 描画ウィンドウ (CASET/RASET) + RAMWR */
    void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

public:
    /** コンストラクタ。SPI ポートと描画状態を初期化する */
    ST7789_LCD();
    
    /** GPIO / SPI 初期化と ST7789 レジスタ設定 */
    void init();
    
    /** バックライト ON/OFF（OFF 時は最小輝度 10%） */
    void setBacklight(bool on);
    /** バックライト輝度 10〜100%（BLK ピン PWM） */
    void setBacklightPercent(int percent);
    /** 現在のバックライト輝度（%）を返す */
    int backlightPercent() const { return _backlight_percent; }
    /** 表示の色反転 ON/OFF */
    void invertDisplay(bool i);

    /** DMA 設定用 SPI インスタンスを返す */
    spi_inst_t* getSPI() const { return spi_port; }
    /** CS ピン番号を返す */
    uint8_t getPinCS() const { return LCDConfig::PIN_CS; }
    /** DC ピン番号を返す */
    uint8_t getPinDC() const { return LCDConfig::PIN_DC; }
    
    /** 描画ウィンドウ（CASET/RASET/RAMWR）を外部から設定 */
    void setWindowPublic(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
        setWindow(x0, y0, x1, y1);
    }
    
    /** 論理解像度の全面を単色で塗る */
    void fill(uint16_t color);
    /** 矩形を SPI ブロッキングで塗る */
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    /** 矩形を DMA で塗る */
    void fillRectDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, 
                     int dma_channel, uint8_t* dma_buffer, size_t buffer_size);
    /** 円塗りつぶし（未実装） */
    void fillCircle(uint16_t x0,uint16_t y0,uint16_t r,uint16_t color);
    /** 1 ピクセル描画（毎回ウィンドウ設定） */
    void drawPixel(uint16_t x, uint16_t y, uint16_t color);
    /** 連続 RAM 書き込み前提の高速ピクセル描画 */
    void writePixel(uint16_t x, uint16_t y, uint16_t color);
    /** 水平線を描画する */
    void drawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
    /** 垂直線を描画する */
    void drawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color);
    /** Bresenham による任意線 */
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
    /** 矩形の枠線を描画する */
    void drawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    /** 円の輪郭を描画する */
    void drawCircle(uint16_t x0,uint16_t y0,uint16_t r,uint16_t color);
    /** 画面回転 0～3（0°/90°/180°/270°） */
    void setRotation(uint8_t r);

    /** テキスト描画色を設定する（背景なし） */
    void setTextColor(uint16_t color);
    /** テキスト描画色と背景色を設定する */
    void setTextColor(uint16_t color, uint16_t bgColor);
    /** 内蔵 8x8 フォントの拡大倍率を設定する */
    void setFontSize(FontSize size);
    /** 1 文字を内蔵フォントで描画する */
    void drawChar(uint16_t x, uint16_t y, char c, uint16_t color);
    /** ASCII 文字列を内蔵フォントで描画する */
    void drawText(uint16_t x, uint16_t y, const char* text);
    /** 前景色・背景色を指定してテキストを描画する */
    void drawTextBg(uint16_t x, uint16_t y, const char* text, uint16_t color, uint16_t bgColor);
    /** 24bit BMP バイナリを描画（ヘッダは uint16_t 配列として渡す） */
    void drawBMP(uint16_t x, uint16_t y, const uint16_t* bmpData);
    /** RGB565 配列を SPI で転送 */
    void drawRawImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data);
    /** RGB565 配列を DMA で転送（行単位チャンク、完了までブロック） */
    void drawRawImageDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                         const uint16_t* data, int dma_channel, uint8_t* dma_buffer,
                         size_t buffer_size);
    /** 非ブロッキング DMA 転送を開始（src_stride=0 のとき w をストライドとする） */
    bool beginDrawRawImageDMA(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                              const uint16_t* data, uint32_t src_stride, int dma_channel,
                              uint8_t* dma_buffer, size_t buffer_size);
    /** 進行中の DMA を 1 ステップ進める（メインループから呼ぶ） */
    void pumpDrawRawImageDMA();
    /** 非ブロッキング DMA が動作中か */
    bool isDrawRawImageDMABusy() const { return dma_async_.active; }
    /** 進行中 DMA を完了させ SPI をアイドルにする（メニュー描画前など） */
    void finishDrawRawImageDMA();

    /** 現在の論理幅（回転後） */
    uint16_t width() const { return _width; }
    /** 現在の論理高さ（回転後） */
    uint16_t height() const { return _height; }
    /** 現在の画面回転値（0〜3） */
    uint8_t rotation() const { return _rotation; }
    
};

#endif // ST7789_LCD_HPP
