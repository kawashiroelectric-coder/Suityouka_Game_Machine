// ============================================
// ファイル: game_display.cpp
// ============================================

#include "game_display.hpp"
#include "font_renderer.hpp"
#include "st7789_lcd.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

FontRenderer* GameDisplay::font_renderer_ = nullptr;

// 8x8 ASCII (st7789_lcd と同じ簡易フォント)
static const uint8_t kFont8x8[96][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
    {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00},
    {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00},
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

/** フレームバッファに 8x8 グリフ 1 文字を描画する。組み込みフォントのテキスト描画時に使う */
static void drawCharFb(uint16_t* fb, uint16_t w, uint16_t h, int x, int y, char c, uint16_t color,
                       uint16_t bg, bool use_bg) {
    if (!fb || c < 32 || c > 127) return;
    const uint8_t* glyph = kFont8x8[c - 32];
    for (int row = 0; row < 8; row++) {
        int py = y + row;
        if (py < 0 || py >= (int)h) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0 || px >= (int)w) continue;
            if (bits & (1 << col)) {
                fb[(uint32_t)py * w + px] = color;
            } else if (use_bg) {
                fb[(uint32_t)py * w + px] = bg;
            }
        }
    }
}

/** 描画状態を初期化する。bind 呼び出し前のデフォルト構築時に使う */
GameDisplay::GameDisplay()
    : buffers_{nullptr, nullptr},
      work_buffer_(nullptr),
      width_(0),
      height_(0),
      buffer_height_(0),
      band_index_(0),
      current_buffer_index_(0),
      inflight_buffer_index_(-1),
      transfer_active_(false),
      band_y0_(0),
      band_rows_(0),
      lcd_(nullptr),
      dma_channel_(-1),
      dma_buffer_(nullptr),
      dma_buffer_size_(0) {}

/** バンドバッファと LCD・DMA リソースを登録する。ゲームループ開始前に一度呼ぶ */
void GameDisplay::bind(uint16_t* buffer_a, uint16_t* buffer_b, uint16_t width, uint16_t height,
                       uint16_t buffer_height, ST7789_LCD* lcd, int dma_channel,
                       uint8_t* dma_buffer, size_t dma_buffer_size) {
    buffers_[0] = buffer_a;
    buffers_[1] = buffer_b ? buffer_b : buffer_a;
    work_buffer_ = buffers_[0];
    width_ = width;
    height_ = height;
    buffer_height_ = (buffer_height > 0) ? buffer_height : height;
    if (buffer_height_ > height_) buffer_height_ = height_;
    band_index_ = 0;
    current_buffer_index_ = 0;
    inflight_buffer_index_ = -1;
    transfer_active_ = false;
    band_y0_ = 0;
    band_rows_ = buffer_height_;
    lcd_ = lcd;
    dma_channel_ = dma_channel;
    dma_buffer_ = dma_buffer;
    dma_buffer_size_ = dma_buffer_size;
}

/** RGB888 を RGB565 に変換する。Lua やゲームから色指定するときに使う */
uint16_t GameDisplay::rgb(uint8_t r, uint8_t g, uint8_t b) {
    return Color::rgb(r, g, b);
}

/** 画面を覆うのに必要なバンド数を返す。フレーム描画ループの回数決定時に使う */
int GameDisplay::bandCount() const {
    if (buffer_height_ == 0) return 0;
    return (height_ + buffer_height_ - 1) / buffer_height_;
}

/** 論理矩形が現在バンドと交差するか判定する。描画 API のクリッピング判定時に使う */
bool GameDisplay::rectIntersectsBand(int y, int h) const {
    if (h <= 0) return false;
    return (y + h) > bandTop() && y < bandBottom();
}

/** 現在バンドのフレームバッファに 1 画素を書く。線・円などの基本描画時に使う */
void GameDisplay::plotPixel(int x, int y, uint16_t color) {
    if (!work_buffer_) return;
    if (x < 0 || x >= (int)width_) return;
    if (y < bandTop() || y >= bandBottom()) return;
    work_buffer_[(uint32_t)(y - band_y0_) * width_ + (uint32_t)x] = color;
}

/** バンド描画を開始し描画先バッファと y 範囲を設定する。各バンドの game_draw 直前に呼ぶ */
void GameDisplay::beginBand(int band) {
    band_index_ = band;
    current_buffer_index_ = (band & 1);
    band_y0_ = band * (int)buffer_height_;
    int remaining = (int)height_ - band_y0_;
    if (remaining < 0) remaining = 0;
    band_rows_ = (remaining < (int)buffer_height_) ? remaining : (int)buffer_height_;
    work_buffer_ = buffers_[current_buffer_index_] ? buffers_[current_buffer_index_] : buffers_[0];
    // 今回使うバッファがまだ DMA 送信中なら完了まで待つ（バッファ再利用保護）
    if (lcd_ && transfer_active_ && inflight_buffer_index_ == current_buffer_index_) {
        while (lcd_->isDrawRawImageDMABusy()) {
            lcd_->pumpDrawRawImageDMA();
        }
        transfer_active_ = false;
        inflight_buffer_index_ = -1;
    }
}

/** 現在バンドを LCD へ DMA 転送する。各バンドの game_draw 直後に呼ぶ */
void GameDisplay::endBand() {
    if (!lcd_ || !work_buffer_ || band_rows_ <= 0) return;
    // SPI には 1 本しか流せないため、前バンド送信中はここでポンプして完了待ち
    while (lcd_->isDrawRawImageDMABusy()) {
        lcd_->pumpDrawRawImageDMA();
    }
    if (lcd_->beginDrawRawImageDMA(0, (uint16_t)band_y0_, width_, (uint16_t)band_rows_,
                                   work_buffer_, width_, dma_channel_, dma_buffer_,
                                   dma_buffer_size_)) {
        transfer_active_ = true;
        inflight_buffer_index_ = current_buffer_index_;
    } else {
        // 念のため begin に失敗したらブロッキングでフォールバック
        lcd_->drawRawImageDMA(0, (uint16_t)band_y0_, width_, (uint16_t)band_rows_, work_buffer_,
                              dma_channel_, dma_buffer_, dma_buffer_size_);
        transfer_active_ = false;
        inflight_buffer_index_ = -1;
    }
}

/** 進行中の DMA 転送完了を待つ。1 フレーム末尾で呼ぶ */
void GameDisplay::waitForTransferComplete() {
    if (!lcd_) return;
    while (lcd_->isDrawRawImageDMABusy()) {
        lcd_->pumpDrawRawImageDMA();
    }
    transfer_active_ = false;
    inflight_buffer_index_ = -1;
}

/** 外部 RGB565 バッファから全画面へ 1 回 DMA（bad_apple 専用・memcpy なし） */
bool GameDisplay::submitFullFrameRgb565(const uint16_t* pixels, uint16_t w, uint16_t h) {
    if (!lcd_ || !pixels || w != width_ || h != height_) {
        return false;
    }
    waitForTransferComplete();
    lcd_->finishDrawRawImageDMA();
    lcd_->drawRawImageDMA(0, 0, w, h, pixels, dma_channel_, dma_buffer_, dma_buffer_size_);

    band_index_ = 0;
    band_y0_ = 0;
    band_rows_ = static_cast<int>(buffer_height_);
    transfer_active_ = false;
    inflight_buffer_index_ = -1;
    return true;
}

/** DMA を解放し ST7789 直描画モードへ戻す。ゲーム終了後メニュー復帰前に呼ぶ */
void GameDisplay::releaseForDirectDraw() {
    printf("[MENU-DBG] GameDisplay::releaseForDirectDraw enter (transfer_active=%d)\n",
           transfer_active_ ? 1 : 0);
    fflush(stdout);
    waitForTransferComplete();
    printf("[MENU-DBG] GameDisplay::waitForTransferComplete done\n");
    fflush(stdout);
    if (lcd_) {
        lcd_->finishDrawRawImageDMA();
    }
    printf("[MENU-DBG] GameDisplay::releaseForDirectDraw exit\n");
    fflush(stdout);
}

/** 全画面を単色で塗る。起動画面など game_draw を使わない描画時に呼ぶ */
void GameDisplay::fillScreen(uint16_t color) {
    waitForTransferComplete();
    uint16_t* buf = buffers_[0];
    if (!buf) return;
    const uint32_t n = (uint32_t)width_ * buffer_height_;
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = color;
    }
    if (!lcd_) return;
    const int bands = bandCount();
    for (int b = 0; b < bands; b++) {
        int y0 = b * (int)buffer_height_;
        int rows = (int)height_ - y0;
        if (rows > (int)buffer_height_) rows = (int)buffer_height_;
        if (rows <= 0) break;
        lcd_->drawRawImageDMA(0, (uint16_t)y0, width_, (uint16_t)rows, buf, dma_channel_,
                              dma_buffer_, dma_buffer_size_);
    }
}

/** 現在バンド領域を単色で塗る。タイルレイヤー合成の下地クリア時に呼ぶ */
void GameDisplay::clear(uint16_t color) {
    if (!work_buffer_ || band_rows_ <= 0) return;
    const uint32_t n = (uint32_t)width_ * band_rows_;
    for (uint32_t i = 0; i < n; i++) {
        work_buffer_[i] = color;
    }
}

/** クリップ付き矩形を塗りつぶす。game_draw 内の図形描画時に呼ぶ */
void GameDisplay::fillRect(int x, int y, int w, int h, uint16_t color) {
    if (!work_buffer_ || w <= 0 || h <= 0) return;

    int x0 = x;
    int x1 = x + w;
    int y0 = y;
    int y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (x1 > (int)width_) x1 = (int)width_;
    if (y0 < bandTop()) y0 = bandTop();
    if (y1 > bandBottom()) y1 = bandBottom();
    if (x0 >= x1 || y0 >= y1) return;

    for (int row = y0; row < y1; row++) {
        uint16_t* line = work_buffer_ + (uint32_t)(row - band_y0_) * width_;
        for (int col = x0; col < x1; col++) {
            line[col] = color;
        }
    }
}

/** RGB565 画像全体をクリップ付きで転写する。スプライト描画時に呼ぶ */
void GameDisplay::drawImage(int dx, int dy, int img_w, int img_h, const uint16_t* pixels) {
    if (!work_buffer_ || !pixels || img_w <= 0 || img_h <= 0) return;

    int sx = 0, sy = 0, sw = img_w, sh = img_h;
    if (dx < 0) { sx = -dx; sw += dx; dx = 0; }
    if (dy < 0) { sy = -dy; sh += dy; dy = 0; }
    if (dx + sw > (int)width_)  sw = (int)width_ - dx;
    if (dy + sh > (int)height_) sh = (int)height_ - dy;
    if (sw <= 0 || sh <= 0) return;

    for (int row = 0; row < sh; row++) {
        const int screen_y = dy + row;
        if (screen_y < bandTop() || screen_y >= bandBottom()) continue;
        const uint16_t* src = pixels + (sy + row) * img_w + sx;
        uint16_t* dst = work_buffer_ + (uint32_t)(screen_y - band_y0_) * width_ + dx;
        memcpy(dst, src, sw * sizeof(uint16_t));
    }
}

/** RGB565 画像の部分矩形をクリップ付きで転写する。タイル・部分描画時に呼ぶ */
void GameDisplay::drawImageSub(int dx, int dy, int img_w, int img_h, const uint16_t* pixels,
                               int sx, int sy, int sw, int sh) {
    if (!work_buffer_ || !pixels || img_w <= 0 || img_h <= 0) return;
    if (sx < 0) { dx -= sx; sw += sx; sx = 0; }
    if (sy < 0) { dy -= sy; sh += sy; sy = 0; }
    if (sx + sw > img_w) sw = img_w - sx;
    if (sy + sh > img_h) sh = img_h - sy;
    if (sw <= 0 || sh <= 0) return;

    if (dx < 0) { sx -= dx; sw += dx; dx = 0; }
    if (dy < 0) { sy -= dy; sh += dy; dy = 0; }
    if (dx + sw > (int)width_)  sw = (int)width_ - dx;
    if (dy + sh > (int)height_) sh = (int)height_ - dy;
    if (sw <= 0 || sh <= 0) return;

    for (int row = 0; row < sh; row++) {
        const int screen_y = dy + row;
        if (screen_y < bandTop() || screen_y >= bandBottom()) continue;
        const uint16_t* src = pixels + (sy + row) * img_w + sx;
        uint16_t* dst = work_buffer_ + (uint32_t)(screen_y - band_y0_) * width_ + dx;
        memcpy(dst, src, sw * sizeof(uint16_t));
    }
}

/** 透過色をスキップして部分矩形を転写する。キー付きスプライト描画時に呼ぶ */
void GameDisplay::drawImageSubKeyed(int dx, int dy, int img_w, int img_h, const uint16_t* pixels,
                                    int sx, int sy, int sw, int sh, uint16_t key_color,
                                    bool key_enabled) {
    if (!key_enabled) {
        drawImageSub(dx, dy, img_w, img_h, pixels, sx, sy, sw, sh);
        return;
    }
    if (!work_buffer_ || !pixels || img_w <= 0 || img_h <= 0) return;
    if (sx < 0) { dx -= sx; sw += sx; sx = 0; }
    if (sy < 0) { dy -= sy; sh += sy; sy = 0; }
    if (sx + sw > img_w) sw = img_w - sx;
    if (sy + sh > img_h) sh = img_h - sy;
    if (sw <= 0 || sh <= 0) return;

    if (dx < 0) { sx -= dx; sw += dx; dx = 0; }
    if (dy < 0) { sy -= dy; sh += dy; dy = 0; }
    if (dx + sw > (int)width_) sw = (int)width_ - dx;
    if (dy + sh > (int)height_) sh = (int)height_ - dy;
    if (sw <= 0 || sh <= 0) return;

    for (int row = 0; row < sh; row++) {
        const int screen_y = dy + row;
        if (screen_y < bandTop() || screen_y >= bandBottom()) continue;
        const uint16_t* src = pixels + (sy + row) * img_w + sx;
        uint16_t* dst = work_buffer_ + (uint32_t)(screen_y - band_y0_) * width_ + dx;
        for (int col = 0; col < sw; col++) {
            const uint16_t c = src[col];
            if (c != key_color) {
                dst[col] = c;
            }
        }
    }
}

/** Bresenham 法でクリップ付き直線を描く。game_draw 内の線描画時に呼ぶ */
void GameDisplay::drawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true) {
        plotPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/** クリップ付き円の輪郭を描く。game_draw 内の円描画時に呼ぶ */
void GameDisplay::drawCircle(int cx, int cy, int radius, uint16_t color) {
    if (radius <= 0) {
        plotPixel(cx, cy, color);
        return;
    }
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        plotPixel(cx + x, cy + y, color);
        plotPixel(cx + y, cy + x, color);
        plotPixel(cx - y, cy + x, color);
        plotPixel(cx - x, cy + y, color);
        plotPixel(cx - x, cy - y, color);
        plotPixel(cx - y, cy - x, color);
        plotPixel(cx + y, cy - x, color);
        plotPixel(cx + x, cy - y, color);
        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) {
            x--;
            err += 1 - 2 * x;
        }
    }
}

/** クリップ付き塗りつぶし円を描く。game_draw 内の円塗り時に呼ぶ */
void GameDisplay::fillCircle(int cx, int cy, int radius, uint16_t color) {
    if (radius <= 0) {
        plotPixel(cx, cy, color);
        return;
    }
    for (int dy = -radius; dy <= radius; dy++) {
        const int row_y = cy + dy;
        if (row_y < bandTop() || row_y >= bandBottom()) continue;
        const int dx = (int)(sqrtf((float)(radius * radius - dy * dy)) + 0.5f);
        fillRect(cx - dx, row_y, dx * 2 + 1, 1, color);
    }
}

/** タイルセットから 1 タイルを転写する。タイルマップ描画時に呼ぶ */
void GameDisplay::drawTile(int dx, int dy, int tile_w, int tile_h, int sheet_cols,
                           const uint16_t* tileset, int sheet_w, int sheet_h, int tile_index) {
    if (!tileset || tile_w <= 0 || tile_h <= 0 || sheet_cols <= 0 || tile_index < 0) {
        return;
    }
    const int sx = (tile_index % sheet_cols) * tile_w;
    const int sy = (tile_index / sheet_cols) * tile_h;
    if (sx + tile_w > sheet_w || sy + tile_h > sheet_h) {
        return;
    }
    drawImageSub(dx, dy, sheet_w, sheet_h, tileset, sx, sy, tile_w, tile_h);
}

/** 透過色付きでタイル 1 枚を転写する。タイルレイヤー合成時に呼ぶ */
void GameDisplay::drawTileKeyed(int dx, int dy, int tile_w, int tile_h, int sheet_cols,
                                const uint16_t* tileset, int sheet_w, int sheet_h, int tile_index,
                                uint16_t key_color, bool key_enabled) {
    if (!tileset || tile_w <= 0 || tile_h <= 0 || sheet_cols <= 0 || tile_index < 0) {
        return;
    }
    const int sx = (tile_index % sheet_cols) * tile_w;
    const int sy = (tile_index / sheet_cols) * tile_h;
    if (sx + tile_w > sheet_w || sy + tile_h > sheet_h) {
        return;
    }
    drawImageSubKeyed(dx, dy, sheet_w, sheet_h, tileset, sx, sy, tile_w, tile_h, key_color,
                      key_enabled);
}

/** 複数矩形を順に塗りつぶす。バッチ矩形描画時に呼ぶ */
void GameDisplay::fillRects(const FillRect* rects, size_t count) {
    if (!rects) return;
    for (size_t i = 0; i < count; i++) {
        const FillRect& r = rects[i];
        fillRect(r.x, r.y, r.w, r.h, r.color);
    }
}

/** テキストを描く。FontRenderer 未使用時は 8x8 ASCII にフォールバックする */
void GameDisplay::drawTextBg(int x, int y, const char* text, uint16_t color, uint16_t bg_color,
                             bool use_bg) {
    if (!work_buffer_ || !text) return;
    if (font_renderer_ && font_renderer_->isLoaded()) {
        font_renderer_->drawTextBg(work_buffer_, width_, band_rows_, band_y0_, x, y, text, color,
                                   bg_color, use_bg);
        return;
    }
    int cx = x;
    while (*text) {
        if (*text == '\n') {
            cx = x;
            y += 8;
        } else {
            // y はバンドローカル座標に変換（drawCharFb は [0, band_rows_) でクリップ）
            drawCharFb(work_buffer_, width_, (uint16_t)band_rows_, cx, y - band_y0_, *text, color,
                       bg_color, use_bg);
            cx += 8;
        }
        text++;
    }
}
