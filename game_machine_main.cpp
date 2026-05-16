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

// ゲーム選択画面
int showGameMenu() {
    if (!loader || !lcd) return -1;
    
    clearScreen(Color::BLACK);
    
    // ゲーム一覧取得
    const int MAX_GAMES = 10;
    GameInfo games[MAX_GAMES];
    int game_count = loader->getGameList(games, MAX_GAMES);
    
    if (game_count == 0) {
        drawText(10, 100, "No games found", Color::WHITE);
        drawText(10, 120, "Insert SD card", Color::YELLOW);
        return -1;
    }
    
    int selected = 0;
    bool menu_active = true;
    
    while (menu_active) {
        clearScreen(Color::BLACK);
        
        // タイトル
        drawText(10, 10, "Game Select", Color::CYAN);
        
        // ゲーム一覧表示
        for (int i = 0; i < game_count; i++) {
            uint16_t text_color = (i == selected) ? Color::YELLOW : Color::WHITE;
            char line[80];
            snprintf(line, sizeof(line), "%d. %s", i + 1, games[i].name);
            drawText(10, 40 + i * 20, line, text_color);
        }
        
        // 操作説明
        drawText(10, 280, "UP/DOWN: Select", Color::GRAY);
        drawText(10, 300, "OP_RIGHT: Start", Color::GRAY);
        
        // LCD更新
        lcd->drawRawImageDMA(0, 0, GameConfig::SCREEN_WIDTH, GameConfig::SCREEN_HEIGHT,
                           framebuffer, dma_channel, dma_buffer, sizeof(dma_buffer));
        
        // ボタン入力処理
        if (buttons) {
            buttons->update();
            
            if (buttons->wasPressed(Button::UP)) {
                selected = (selected > 0) ? selected - 1 : game_count - 1;
            }
            if (buttons->wasPressed(Button::DOWN)) {
                selected = (selected < game_count - 1) ? selected + 1 : 0;
            }
            if (buttons->wasPressed(Button::OP_RIGHT)) {
                menu_active = false;
            }
        }
        
        sleep_ms(100);
    }
    
    return selected;
}

// ゲーム実行（簡易版）
void runGame(const GameInfo& game) {
    if (!loader || !lcd) return;
    
    clearScreen(Color::BLACK);
    drawText(10, 100, "Loading game...", Color::WHITE);
    lcd->drawRawImageDMA(0, 0, GameConfig::SCREEN_WIDTH, GameConfig::SCREEN_HEIGHT,
                       framebuffer, dma_channel, dma_buffer, sizeof(dma_buffer));
    
    // ゲームアイコン読み込み
    ImageData icon = loader->loadImage(game.image_path);
    if (icon.valid) {
        // アイコン表示（簡易実装：全画面表示）
        clearScreen(Color::BLACK);
        lcd->drawRawImageDMA(0, 0, icon.width, icon.height,
                           icon.pixels, dma_channel, dma_buffer, sizeof(dma_buffer));
        sleep_ms(2000);
    }
    
    // プログラム読み込み
    uint8_t* program_data = nullptr;
    size_t program_size = 0;
    if (loader->loadProgram(game.program_path, &program_data, &program_size)) {
        // LuaスクリプトならLuaで実行する
        const char* prog_path = game.program_path ? game.program_path : "";
        size_t len = strlen(prog_path);
        bool is_lua = (len > 4 && strcmp(prog_path + len - 4, ".lua") == 0);

        if (is_lua) {
            // Lua実行ヘルパ
            lua_State* L = luaL_newstate();
            if (L) {
                luaL_openlibs(L);
                int load_status = luaL_loadbuffer(L, (const char*)program_data, program_size, prog_path);
                if (load_status == 0) {
                    int pcall_status = lua_pcall(L, 0, LUA_MULTRET, 0);
                    if (pcall_status != 0) {
                        const char* err = lua_tostring(L, -1);
                        clearScreen(Color::BLACK);
                        drawText(10, 100, "Lua runtime error:", Color::RED);
                        if (err) drawText(10, 120, err, Color::YELLOW);
                        lcd->drawRawImageDMA(0, 0, GameConfig::SCREEN_WIDTH, GameConfig::SCREEN_HEIGHT,
                                           framebuffer, dma_channel, dma_buffer, sizeof(dma_buffer));
                    }
                } else {
                    const char* err = lua_tostring(L, -1);
                    clearScreen(Color::BLACK);
                    drawText(10, 100, "Lua load error:", Color::RED);
                    if (err) drawText(10, 120, err, Color::YELLOW);
                    lcd->drawRawImageDMA(0, 0, GameConfig::SCREEN_WIDTH, GameConfig::SCREEN_HEIGHT,
                                       framebuffer, dma_channel, dma_buffer, sizeof(dma_buffer));
                }
                lua_close(L);
            }
            free(program_data);
        } else {
            // プログラム実行（簡易実装：バイナリデータを表示）
            clearScreen(Color::BLACK);
            char msg[64];
            snprintf(msg, sizeof(msg), "Program: %zu bytes", program_size);
            drawText(10, 100, msg, Color::GREEN);
            drawText(10, 120, "Press OP_LEFT to exit", Color::YELLOW);
            lcd->drawRawImageDMA(0, 0, GameConfig::SCREEN_WIDTH, GameConfig::SCREEN_HEIGHT,
                               framebuffer, dma_channel, dma_buffer, sizeof(dma_buffer));

            // ゲームループ（簡易版）
            bool game_running = true;
            while (game_running && buttons) {
                buttons->update();

                if (buttons->wasPressed(Button::OP_LEFT)) {
                    game_running = false;
                }

                // ここで実際のゲームロジックを実行
                // 現在は簡易実装のため、ボタン入力のみ処理

                sleep_ms(16);  // 約60FPS
            }

            free(program_data);
        }
    } else {
        clearScreen(Color::BLACK);
        drawText(10, 100, "Failed to load", Color::RED);
        drawText(10, 120, "Press any button", Color::YELLOW);
        lcd->drawRawImageDMA(0, 0, GameConfig::SCREEN_WIDTH, GameConfig::SCREEN_HEIGHT,
                           framebuffer, dma_channel, dma_buffer, sizeof(dma_buffer));
        
        // ボタン待ち
        if (buttons) {
            while (true) {
                buttons->update();
                if (buttons->getAllButtons() != 0xFF) break;  // 何かボタンが押された
                sleep_ms(100);
            }
        }
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
    clearScreen(Color::WHITE);
    // メインループ
    while (true) {
    
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
        } else {
            // SDカード未挿入またはエラー
            clearScreen(Color::BLACK);
            drawText(10, 100, "SD Card Error", Color::RED);
            drawText(10, 120, "Press any button", Color::YELLOW);
            lcd->drawRawImageDMA(0, 0, GameConfig::SCREEN_WIDTH, GameConfig::SCREEN_HEIGHT,
                               framebuffer, dma_channel, dma_buffer, sizeof(dma_buffer));
            
            if (buttons) {
                while (true) {
                    buttons->update();
                    if (buttons->getAllButtons() != 0xFF) break;
                    sleep_ms(100);
                }
            }
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
