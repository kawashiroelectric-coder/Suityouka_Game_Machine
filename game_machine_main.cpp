// ============================================
// ファイル: game_machine_main.cpp
// ゲーム機メインループ
// SDカードからゲームを読み込んで実行
// ============================================

#include <stdio.h>
#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "st7789_lcd.hpp"
#include "config.hpp"
#include "button_input.hpp"
#include "audio_output.hpp"
#include "game_loader.hpp"
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"

#include <string>
#include <cstring>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "assets/GameLogo.h"
#include "assets/nitorih1.h"
#include "assets/nitorih2.h"
#include "assets/nitorih3.h"
#include "assets/nitorih4.h"

// フレームバッファ
static uint16_t framebuffer[GameConfig::SCREEN_WIDTH * GameConfig::SCREEN_HEIGHT];

// グローバルオブジェクト
static ST7789_LCD* lcd = nullptr;
static ButtonInput* buttons = nullptr;
static AudioOutput* audio = nullptr;
static GameLoader* loader = nullptr;
static int dma_channel = -1;
static uint8_t dma_buffer[16384];


// 画面クリア
void clearScreen(uint16_t color) {
    for (uint32_t i = 0; i < GameConfig::SCREEN_WIDTH * GameConfig::SCREEN_HEIGHT; i++) {
        framebuffer[i] = color;
    }
    if (lcd) {
        lcd->drawRawImageDMA(0, 0, GameConfig::SCREEN_WIDTH, GameConfig::SCREEN_HEIGHT,
                           framebuffer, dma_channel, dma_buffer, sizeof(dma_buffer));
    }
}

// テキスト表示（簡易版）
void drawText(int x, int y, const char* text, uint16_t color) {
    if (!lcd) return;
    lcd->setTextColor(color);
    lcd->drawText(x, y, text);
}

void drawTextBg(int x, int y, const char* text, uint16_t color, uint16_t bgColor) {
    if (!lcd) return;
    lcd->drawTextBg(x, y, text, color, bgColor);
}

bool isAnyButtonPressed() {
    if (!buttons) return false;
    for (int i = 0; i < 8; i++) {
        if (buttons->isPressed(static_cast<Button>(i))) {
            return true;
        }
    }
    return false;
}

void waitForButtonRelease() {
    if (!buttons) return;
    while (true) {
        buttons->update();
        if (!isAnyButtonPressed()) break;
        sleep_ms(50);
    }
}

void waitForAnyButton() {
    if (!buttons) {
        sleep_ms(1000);
        return;
    }
    while (true) {
        buttons->update();
        if (isAnyButtonPressed()) {
            waitForButtonRelease();
            break;
        }
        sleep_ms(50);
    }
}


// SDカードマウントパス（FatFS）
static const char* getSdMountPath() {
    return (GameConfig::SD_ROOT[0] != '\0') ? GameConfig::SD_ROOT : "0:";
}

// SDカード挿入検出（no-OS-FatFS の GPIO カード検出）
static bool isSdCardInserted() {
    sd_card_t* sd = sd_get_by_num(0);
    if (!sd) return false;
    return sd_card_detect(sd);
}

// SDカード状態とルート直下のフォルダ名を液晶に表示
static void updateSdCardDisplay() {
    if (!lcd) return;

    clearScreen(Color::BLACK);

    if (!isSdCardInserted()) {
        drawTextBg(10, 100, "SD: Not inserted", Color::RED, Color::BLACK);
        printf("SD card: not inserted\n");
        return;
    }

    drawTextBg(10, 10, "SD: Inserted", Color::GREEN, Color::BLACK);

    if (!loader) {
        drawTextBg(10, 40, "Loader not ready", Color::RED, Color::BLACK);
        return;
    }

    if (!loader->isMounted() && !loader->init()) {
        drawTextBg(10, 40, "Mount failed", Color::RED, Color::BLACK);
        printf("SD card: mount failed\n");
        return;
    }

    DIR dir;
    FILINFO fno;
    const char* root = getSdMountPath();
    FRESULT fr = f_opendir(&dir, root);
    if (fr != FR_OK) {
        char msg[48];
        snprintf(msg, sizeof(msg), "Open root err: %d", fr);
        drawTextBg(10, 40, msg, Color::RED, Color::BLACK);
        printf("SD card: f_opendir(%s) failed (%d)\n", root, fr);
        return;
    }

    drawTextBg(10, 30, "Root folders:", Color::CYAN, Color::BLACK);

    int y = 50;
    int count = 0;
    constexpr int kMaxFolderLines = 8;

    while (count < kMaxFolderLines) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;
        if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;
        if (!(fno.fattrib & AM_DIR)) continue;

        drawText(10, y, fno.fname, Color::WHITE);
        printf("SD root folder: %s\n", fno.fname);
        y += 18;
        count++;
    }

    f_closedir(&dir);

    if (count == 0) {
        drawTextBg(10, 50, "(no folders)", Color::YELLOW, Color::BLACK);
    }
}

// DMA初期化
void initDMA(ST7789_LCD& lcd_obj) {
    dma_channel = dma_claim_unused_channel(true);
    dma_channel_config config = dma_channel_get_default_config(dma_channel);
    
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_dreq(&config, spi_get_dreq(lcd_obj.getSPI(), true));
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    
    dma_channel_configure(dma_channel, &config,
                         &spi_get_hw(lcd_obj.getSPI())->dr, NULL, 0, false);
    
    printf("DMA初期化完了\n");
}

int main() {

    
    stdio_init_all();
    
    printf("=== ゲーム機初期化開始 ===\n");
    
    // GPIO初期化
    gpio_init(I2CConfig::RST);
    gpio_set_dir(I2CConfig::RST, GPIO_OUT);
    gpio_put(I2CConfig::RST, 1);
    
    gpio_init(AudioConfig::PIN_AUDIO_SD);
    gpio_set_dir(AudioConfig::PIN_AUDIO_SD, GPIO_OUT);
    gpio_put(AudioConfig::PIN_AUDIO_SD, 0);  // シャットダウン
    
    gpio_init(AudioConfig::PIN_ABD);
    gpio_set_dir(AudioConfig::PIN_ABD, GPIO_OUT);
    gpio_put(AudioConfig::PIN_ABD, 0);
    
    // ADC初期化（バッテリーモニター）
    adc_init();
    adc_gpio_init(BatteryConfig::PIN_ADC);
    adc_select_input(BatteryConfig::ADC_CHANNEL);
    
    // I2C初期化
    printf("I2C初期化中...\n");
    i2c_init(I2CConfig::PORT, I2CConfig::BAUD_RATE);
    gpio_set_function(I2CConfig::SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2CConfig::SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2CConfig::SDA);
    gpio_pull_up(I2CConfig::SCL);

    // SDカード電源ON
    printf("SDカード電源ON\n");
    gpio_init(SDConfig::PIN_SD_POWER);
    gpio_set_dir(SDConfig::PIN_SD_POWER, GPIO_OUT);
    gpio_put(SDConfig::PIN_SD_POWER, 1);
    sleep_ms(100);

    // SDドライバ初期化（カード検出GPIO・SPI）
    printf("SDドライバ初期化中...\n");
    if (!sd_init_driver()) {
        printf("SDドライバ初期化失敗\n");
    }

    // ボタン入力初期化
    printf("ボタン入力初期化中...\n");
    buttons = new ButtonInput(I2CConfig::PORT, I2CConfig::PCA9539_ADDR);
    if (!buttons->init()) {
        printf("ボタン入力初期化失敗\n");
        return -1;
    }
    
    // LCD初期化
    printf("LCD初期化中...\n");
    lcd = new ST7789_LCD();
    lcd->init();
    initDMA(*lcd);
    
    // 音声出力初期化
    printf("音声出力初期化中...\n");
    audio = new AudioOutput(AudioConfig::PIN_L_OUT, AudioConfig::PIN_R_OUT,
                           AudioConfig::PIN_AUDIO_SD, AudioConfig::PIN_ABD);
    audio->init();
    
    // ゲームローダー初期化
    printf("ゲームローダー初期化中...\n");
    loader = new GameLoader();
    if (!loader->init()) {
        printf("ゲームローダー初期化失敗（SDカード未挿入の可能性）\n");
    }
    
    // 起動画面
    // Display embedded GameLogo centered
    /*
    clearScreen(Color::BLACK);
    int logo_x = (GameConfig::SCREEN_WIDTH - GameLogo_width) / 2;
    int logo_y = (GameConfig::SCREEN_HEIGHT - GameLogo_height) / 2;
    lcd->drawRawImageDMA(logo_x, logo_y, GameLogo_width, GameLogo_height,
                       (uint16_t*)GameLogo_pixels, dma_channel, dma_buffer, sizeof(dma_buffer));
    sleep_ms(2000);
    */
    printf("=== 初期化完了 ===\n");

    // 起動時: SD挿入状態とルートフォルダを液晶表示
    updateSdCardDisplay();
    sleep_ms(2000);

    // メインループ
    while (true) {
        // SD挿入確認 → 挿入時はルート直下フォルダ名を液晶表示
        updateSdCardDisplay();
        sleep_ms(1500);
    /*
    int logo_x = 0;
    int logo_y = 0;
    lcd->drawRawImageDMA(logo_x, logo_y, 320, 240,
                       (uint16_t*)nitorih1_pixels, dma_channel, dma_buffer, sizeof(dma_buffer));
    sleep_ms(2000);
    lcd->drawRawImageDMA(logo_x, logo_y, 320, 240,
                       (uint16_t*)nitorih2_pixels, dma_channel, dma_buffer, sizeof(dma_buffer));
    sleep_ms(2000);
    lcd->drawRawImageDMA(logo_x, logo_y, 320, 240,
                       (uint16_t*)nitorih3_pixels, dma_channel, dma_buffer, sizeof(dma_buffer));
    sleep_ms(2000);
    lcd->drawRawImageDMA(logo_x, logo_y, 320, 240,
                       (uint16_t*)nitorih4_pixels, dma_channel, dma_buffer, sizeof(dma_buffer));
    sleep_ms(2000);
    */
        /*
        // ゲーム選択
        int selected = showGameMenu();
        
        if (selected >= 0 && loader->isMounted()) {
            // ゲーム情報取得
            GameInfo games[10];
            int count = loader->getGameList(games, 10);
            if (selected < count) {
                runGame(games[selected]);
            }
        } else if (!loader->isMounted()) {
            // SDカード未挿入またはマウントエラー
            clearScreen(Color::BLACK);
            drawTextBg(10, 100, "SD Card Error", Color::RED, Color::BLACK);
            drawTextBg(10, 120, "Press any button", Color::YELLOW, Color::BLACK);
            waitForAnyButton();
        }
        */
        
    }
    
    // クリーンアップ（通常は到達しない）
    //delete loader;
    //delete audio;
    //delete buttons;
    //delete lcd;
    
    //return 0;
    
}
