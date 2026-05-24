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
#include "lua_interpreter.hpp"
#include "game_display.hpp"
//#include "game_loader.hpp"

#include <string>
#include <cstring>

extern "C" {
#include "hw_config.h"
#include "f_util.h"
#include "ff.h"
}

#include "assets/GameLogo.h"
#include "assets/nitorih1.h"
#include "assets/nitorih2.h"
#include "assets/nitorih3.h"
#include "assets/nitorih4.h"
#include "sd_debug.h"


// フレームバッファ
static uint16_t framebuffer[GameConfig::SCREEN_WIDTH * GameConfig::SCREEN_HEIGHT];

// グローバルオブジェクト
static ST7789_LCD* lcd = nullptr;
static ButtonInput* buttons = nullptr;
static AudioOutput* audio = nullptr;
//static GameLoader* loader = nullptr;
static int dma_channel = -1;
static uint8_t dma_buffer[16384];

static FATFS sd_fs;
static bool sd_mounted = false;
static LuaInterpreter g_luaInterpreter;
static GameDisplay g_gameDisplay;

struct MainLuaHostContext {
    ButtonInput* buttons = nullptr;
};

static MainLuaHostContext g_luaHostCtx;

void drawTextBg(int x, int y, const char* text, uint16_t color, uint16_t bgColor);

static void luaHostDrawText(void* user_data, int x, int y, const char* text, uint16_t color,
                            uint16_t bg_color) {
    (void)user_data;
    drawTextBg(x, y, text, color, bg_color);
}

static bool luaHostButtonPressed(void* user_data, int button_index) {
    auto* ctx = static_cast<MainLuaHostContext*>(user_data);
    if (!ctx || !ctx->buttons || button_index < 0 || button_index >= 8) {
        return false;
    }
    ctx->buttons->update();
    return ctx->buttons->isPressed(static_cast<Button>(button_index));
}

static void setupLuaInterpreter() {
    g_luaHostCtx.buttons = buttons;
    g_gameDisplay.bind(framebuffer, GameConfig::SCREEN_WIDTH, GameConfig::SCREEN_HEIGHT, lcd,
                       dma_channel, dma_buffer, sizeof(dma_buffer));
    LuaHostHooks hooks = {};
    hooks.user_data = &g_luaHostCtx;
    hooks.draw_text_bg = luaHostDrawText;
    hooks.is_button_pressed = luaHostButtonPressed;
    hooks.display = &g_gameDisplay;
    g_luaInterpreter.setHostHooks(hooks);
    g_luaInterpreter.setSdMounted(sd_mounted);
}

static bool tryStartLuaGame() {
    if (!sd_mounted) return false;
    static const char* kGameScripts[] = {"dino.lua", "stg.lua", "game.lua"};
    char line[64];
    for (const char* script : kGameScripts) {
        if (g_luaInterpreter.sdFileExists(script)) {
            printf("Starting Lua game: %s\n", script);
            drawTextBg(10, 100, "Lua game", Color::WHITE, Color::BLACK);
            snprintf(line, sizeof(line), "%s", script);
            drawTextBg(10, 112, line, Color::WHITE, Color::BLACK);
            sleep_ms(500);
            return g_luaInterpreter.runGameLoopFromSd(script);
        }
    }
    return false;
}

static void syncLuaSdMountState() { g_luaInterpreter.setSdMounted(sd_mounted); }

static bool mountSdCard() {
    FRESULT fr = f_mount(&sd_fs, "", 1);
    if (fr != FR_OK) {
        printf("f_mount failed: %s (%d)\n", FRESULT_str(fr), fr);
        sd_mounted = false;
        return false;
    }
    sd_mounted = true;
    return true;
}

static void unmountSdCard() {
    if (sd_mounted) {
        f_unmount("");
        sd_mounted = false;
    }
}

static bool isSdCardPresent() {
    sd_card_t* card = sd_get_by_num(0);
    if (!card || !card->sd_test_com) return false;
    return card->sd_test_com(card);
}

static void listSdRoot() {
    DIR dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, "/");
    if (fr != FR_OK) {
        printf("f_opendir failed: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    while (true) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;
        printf("  %s\t%lu\n", fno.fname, static_cast<unsigned long>(fno.fsize));
    }
    f_closedir(&dir);
}



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
    //while (!stdio_usb_connected()) {
    //    sleep_ms(10);
    //}
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
    gpio_put(SDConfig::PIN_SD_POWER, 0);
    sleep_ms(200);

    // SDカード初期化・段階別デバッグ (no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
    printf("SDカード初期化中...\n");
    sd_debug_run_diagnostics();
    if (mountSdCard()) {
        printf("SDカードマウント成功\n");
        listSdRoot();
        syncLuaSdMountState();
    } else {
        printf("SDカードマウント失敗（上の [SD DBG] ログを確認）\n");
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
    clearScreen(Color::GRAY);
    drawTextBg(10, 100, "LCD init", Color::WHITE, Color::GRAY);
    
    
    // 音声出力初期化
    printf("音声出力初期化中...\n");
    audio = new AudioOutput(AudioConfig::PIN_L_OUT, AudioConfig::PIN_R_OUT,
                           AudioConfig::PIN_AUDIO_SD, AudioConfig::PIN_ABD);
    audio->init();
    drawTextBg(10, 120, "Audio init", Color::WHITE, Color::GRAY);

    setupLuaInterpreter();
    
    /*
    // ゲームローダー初期化
    printf("ゲームローダー初期化中...\n");
    loader = new GameLoader();
    if (!loader->init()) {
        printf("ゲームローダー初期化失敗（SDカード未挿入の可能性）\n");
    }
    */
    
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
    drawTextBg(10, 140, "Start main loop", Color::WHITE, Color::GRAY);

    bool lua_executed_for_mount = false;
    if (sd_mounted) {
        if (!tryStartLuaGame()) {
            g_luaInterpreter.executeOnSdRoot();
        }
        lua_executed_for_mount = true;
    }

    bool connectedFlag = sd_mounted;
    char lcdBuffer[64];
    uint32_t lastMountAttemptMs = 0;

    // メインループ
    while (true) {
        if (connectedFlag) {
            if (!isSdCardPresent()) {
                drawTextBg(10, 100, "SD disconnected", Color::RED, Color::GRAY);
                unmountSdCard();
                syncLuaSdMountState();
                connectedFlag = false;
                lua_executed_for_mount = false;
            }
        } else if (isSdCardPresent()
                   && (to_ms_since_boot(get_absolute_time()) - lastMountAttemptMs >= 5000)
                   && mountSdCard()) {
            lastMountAttemptMs = to_ms_since_boot(get_absolute_time());
            syncLuaSdMountState();
            drawTextBg(10, 160, "sd connected", Color::WHITE, Color::GRAY);
            printf("SD Card connected.\n");
            connectedFlag = true;
            DIR dir;
            FILINFO fno;
             if (f_opendir(&dir, "/") == FR_OK) {
                const int baseY = 100;      // 1行目の Y
                const int lineStep = 10;    // 行間（8pxフォント + 少し余白）
                int line = 0;
                const int maxLines = 12;    // 画面に収まる行数（お好みで）
                while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
                    // 任意: カレント/親ディレクトリをスキップ
                    if (fno.fname[0] == '.' &&
                        (fno.fname[1] == '\0' ||
                        (fno.fname[1] == '.' && fno.fname[2] == '\0'))) {
                        continue;
                    }
                    if (line >= maxLines) break;
                    int y = baseY + line * lineStep;
                    // ファイル名だけ（サイズも出すなら下のコメント参照）
                    snprintf(lcdBuffer, sizeof(lcdBuffer), "%s", fno.fname);
                    // snprintf(lcdBuffer, sizeof(lcdBuffer), "%s %lu",
                    //          fno.fname, (unsigned long)fno.fsize);
                    drawTextBg(10, y, lcdBuffer, Color::BLUE, Color::GRAY);
                    printf("%-32s%lu\n", fno.fname,
                            (unsigned long)fno.fsize);
                    line++;
                }
             f_closedir(&dir);
            }
            if (!lua_executed_for_mount) {
                if (!tryStartLuaGame()) {
                    g_luaInterpreter.executeOnSdRoot();
                }
                lua_executed_for_mount = true;
            }
        }
       // drawTextBg(10, 180, "loop", Color::WHITE, Color::BLACK);
        sleep_ms(500);
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
