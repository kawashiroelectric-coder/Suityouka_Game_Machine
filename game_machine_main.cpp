// ============================================
// ファイル: game_machine_main.cpp
// ゲーム機メインループ
// SDカードからゲームを読み込んで実行
// ============================================

#include <stdio.h>
#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"
#include "st7789_lcd.hpp"
#include "config.hpp"
#include "button_input.hpp"
#include "audio_output.hpp"
#include "battery_monitor.hpp"
#include "lua_interpreter.hpp"
#include "game_display.hpp"
#include "input_test_mode.hpp"
#include "file_explorer.hpp"
#include "encoder_volume.hpp"
#include "sd_service.hpp"

#include <cstring>

extern "C" {
#include "hw_config.h"
#include "sd_debug.h"
}

static uint16_t framebuffer_a[GameConfig::BUFFER_WIDTH * GameConfig::BUFFER_HEIGHT];
static uint16_t framebuffer_b[GameConfig::BUFFER_WIDTH * GameConfig::BUFFER_HEIGHT];

static ST7789_LCD* lcd = nullptr;
static ButtonInput* buttons = nullptr;
static AudioOutput* audio = nullptr;
static int dma_channel = -1;
static uint8_t dma_buffer[16384];

static LuaInterpreter g_luaInterpreter;
static GameDisplay g_gameDisplay;

struct MainLuaHostContext {
    ButtonInput* buttons = nullptr;
};

static MainLuaHostContext g_luaHostCtx;

void drawTextBg(int x, int y, const char* text, uint16_t color, uint16_t bgColor);
void clearScreen(uint16_t color);
void waitForAnyButton();

static void inputTestModeFrameService(void* user_data);
static void runInputTestMode();
static void runFileExplorer();
static void runUntilSdReady();
static void syncLuaSdMountState();

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
    g_gameDisplay.bind(framebuffer_a, framebuffer_b, GameConfig::SCREEN_WIDTH,
                       GameConfig::SCREEN_HEIGHT, GameConfig::BUFFER_HEIGHT, lcd, dma_channel,
                       dma_buffer, sizeof(dma_buffer));
    LuaHostHooks hooks = {};
    hooks.user_data = &g_luaHostCtx;
    hooks.draw_text_bg = luaHostDrawText;
    hooks.is_button_pressed = luaHostButtonPressed;
    hooks.display = &g_gameDisplay;
    g_luaInterpreter.setHostHooks(hooks);
    g_luaInterpreter.setSdMounted(SdService::isMounted());
}

static void fileExplorerRunLua(const char* path, void* user_data) {
    (void)user_data;
    if (!path || path[0] == '\0') {
        return;
    }
    printf("FileExplorer: run %s\n", path);
    if (g_luaInterpreter.runGameLoopFromSd(path)) {
        return;
    }

    char line[64];
    clearScreen(Color::BLACK);
    drawTextBg(10, 80, "Game start failed", Color::RED, Color::BLACK);
    snprintf(line, sizeof(line), "%s", path);
    drawTextBg(10, 92, line, Color::WHITE, Color::BLACK);
    const char* err = g_luaInterpreter.lastError();
    if (err && err[0] != '\0') {
        drawTextBg(10, 116, err, Color::ORANGE, Color::BLACK);
    }
    drawTextBg(10, 140, "Press any button", Color::YELLOW, Color::BLACK);
    waitForAnyButton();
}

static bool fileExplorerSdPresent(void* user_data) {
    (void)user_data;
    return SdService::isMounted() && SdService::isCardPresent();
}

static void runFileExplorer() {
    if (!lcd || !buttons || !SdService::isMounted()) {
        return;
    }
    FileExplorer::Config config = {};
    config.lcd = lcd;
    config.buttons = buttons;
    config.on_frame = inputTestModeFrameService;
    config.on_run_lua = fileExplorerRunLua;
    config.is_sd_present = fileExplorerSdPresent;
    config.frame_interval_ms = 50;
    FileExplorer::run(config);
}

static void runUntilSdReady() {
    while (!SdService::isMounted()) {
        runInputTestMode();
        if (SdService::isMounted()) {
            break;
        }
    }
}

static void syncLuaSdMountState() {
    g_luaInterpreter.setSdMounted(SdService::isMounted());
}

static bool tryMountSdFromTestMode(void* user_data) {
    (void)user_data;
    if (!SdService::isCardPresent()) {
        return false;
    }
    if (!SdService::mount()) {
        return false;
    }
    syncLuaSdMountState();
    return true;
}

static void inputTestModeFrameService(void* user_data) {
    (void)user_data;
    g_luaInterpreter.audioEngine().pumpStream();
    EncoderVolumeControl::service();
}

static void runInputTestMode() {
    if (!lcd || !buttons) {
        return;
    }
    InputTestMode::Config config = {};
    config.lcd = lcd;
    config.buttons = buttons;
    config.encoder = &EncoderVolumeControl::encoder();
    config.try_mount = tryMountSdFromTestMode;
    config.on_frame = inputTestModeFrameService;
    config.mount_retry_ms = 2000;
    config.frame_interval_ms = 50;
    InputTestMode::run(config);
}

static void handleSdCardConnected() {
    syncLuaSdMountState();
    printf("SD Card connected.\n");
    SdService::listRoot();
}

void clearScreen(uint16_t color) {
    g_gameDisplay.fillScreen(color);
}

void drawTextBg(int x, int y, const char* text, uint16_t color, uint16_t bgColor) {
    if (!lcd) {
        return;
    }
    lcd->drawTextBg(x, y, text, color, bgColor);
}

bool isAnyButtonPressed() {
    if (!buttons) {
        return false;
    }
    for (int i = 0; i < 8; i++) {
        if (buttons->isPressed(static_cast<Button>(i))) {
            return true;
        }
    }
    return false;
}

void waitForButtonRelease() {
    if (!buttons) {
        return;
    }
    while (true) {
        buttons->update();
        if (!isAnyButtonPressed()) {
            break;
        }
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

void initDMA(ST7789_LCD& lcd_obj) {
    dma_channel = dma_claim_unused_channel(true);
    dma_channel_config config = dma_channel_get_default_config(dma_channel);

    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_dreq(&config, spi_get_dreq(lcd_obj.getSPI(), true));
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);

    dma_channel_configure(dma_channel, &config, &spi_get_hw(lcd_obj.getSPI())->dr, NULL, 0, false);

    printf("DMA初期化完了\n");
}

int main() {
    stdio_init_all();
    printf("=== ゲーム機初期化開始 ===\n");

    gpio_init(I2CConfig::RST);
    gpio_set_dir(I2CConfig::RST, GPIO_OUT);
    gpio_put(I2CConfig::RST, 1);

    printf("I2C初期化中...\n");
    i2c_init(I2CConfig::PORT, I2CConfig::BAUD_RATE);
    gpio_set_function(I2CConfig::SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2CConfig::SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2CConfig::SDA);
    gpio_pull_up(I2CConfig::SCL);

    printf("SDカード電源ON\n");
    gpio_init(SDConfig::PIN_SD_POWER);
    gpio_set_dir(SDConfig::PIN_SD_POWER, GPIO_OUT);
    gpio_put(SDConfig::PIN_SD_POWER, 0);
    sleep_ms(200);

    printf("SDカード初期化中...\n");
    sd_debug_run_diagnostics();
    if (SdService::mount()) {
        printf("SDカードマウント成功\n");
        SdService::listRoot();
        syncLuaSdMountState();
    } else {
        printf("SDカードマウント失敗（上の [SD DBG] ログを確認）\n");
    }

    printf("ボタン入力初期化中...\n");
    buttons = new ButtonInput(I2CConfig::PORT, I2CConfig::PCA9539_ADDR);
    if (!buttons->init()) {
        printf("ボタン入力初期化失敗\n");
        return -1;
    }

    printf("LCD初期化中...\n");
    lcd = new ST7789_LCD();
    lcd->init();
    initDMA(*lcd);
    setupLuaInterpreter();
    clearScreen(Color::GRAY);
    drawTextBg(10, 100, "LCD init", Color::WHITE, Color::BLACK);

    printf("音声出力初期化中 (PCM5102 I2S)...\n");
    BatteryMonitor::attach(buttons);
    audio = new AudioOutput();
    if (!audio->init()) {
        drawTextBg(10, 120, "Audio fail", Color::RED, Color::GRAY);
    } else {
        g_luaInterpreter.setAudioOutput(audio);
        drawTextBg(10, 120, "Audio I2S OK", Color::WHITE, Color::GRAY);
    }

    EncoderVolumeControl::init(audio, &g_luaInterpreter.audioEngine());
    EncoderVolumeControl::setDisplay(lcd);
    if (!EncoderVolumeControl::initEncoder()) {
        drawTextBg(10, 132, "Enc IRQ fail", Color::ORANGE, Color::GRAY);
    }

    printf("=== 初期化完了 ===\n");
    drawTextBg(10, 140, "File explorer", Color::WHITE, Color::BLACK);

    runUntilSdReady();

    while (true) {
        if (SdService::isMounted()) {
            runFileExplorer();
            SdService::unmount();
            syncLuaSdMountState();
        }

        runUntilSdReady();
        handleSdCardConnected();
    }
}
