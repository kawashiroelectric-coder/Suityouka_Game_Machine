// ============================================
// ファイル: game_machine_main.cpp
// ゲーム機ファームウェアのエントリポイント
// ============================================
//
// 【起動〜メインループの流れ】
//   main()
//     → ハードウェア初期化（I2C / SD / LCD+DMA / 音声 / エンコーダ）
//     → bindLuaRuntimeToHardware() … LuaInterpreter に GameDisplay・入力を接続
//     → GameSelectMenu::run() … ゲーム選択メニュー（ここで永久ループ）
//
// 【ゲーム起動の流れ】（メニューから .lua を選んだとき）
//   menuLaunchGameCallback(path)
//     → runGameFromMenuAndTeardown(path)
//         1. LuaInterpreter::runGameLoopFromSd(path) … game_init/update/draw ループ
//         2. waitForAllButtonsReleased() … 終了ボタン離し待ち（誤再押下防止）
//         3. teardownLuaSessionAfterGame() … 軽量終了 + 遅延リソース解放
//         4. LCD DMA 後処理
//     → メニューへ戻る
//
// 【Lua セッション終了が二段階な理由】
//   VN など大きな Lua 状態では、ゲームループ直後に lua_close すると
//   スタックが深い／時間がかかりメニュー復帰できないことがある。
//   そのため finishGameSession（音声停止・大テーブル trim）と
//   closePendingGameSession（lua_close・画像・フォント解放）に分けている。
//   詳細は lib/lua_interpreter/lua_interpreter.hpp 参照。
//
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
#include "encoder_volume.hpp"
#include "device_settings.hpp"
#include "sd_service.hpp"
#include "game_select_menu.hpp"

#include <cstring>

extern "C" {
#include "hw_config.h"
#include "sd_debug.h"
}

// ---------------------------------------------------------------------------
// 静的リソース（起動時に一度だけ確保・main 終了まで存続）
// ---------------------------------------------------------------------------

static uint16_t framebuffer_a[GameConfig::BUFFER_WIDTH * GameConfig::BUFFER_HEIGHT];
static uint16_t framebuffer_b[GameConfig::BUFFER_WIDTH * GameConfig::BUFFER_HEIGHT];

static ST7789_LCD* lcd = nullptr;
static ButtonInput* buttons = nullptr;
static AudioOutput* audio = nullptr;
static int lcd_spi_dma_channel = -1;
static uint8_t lcd_spi_dma_buffer[16384];

static LuaInterpreter g_luaInterpreter;
static GameDisplay g_gameDisplay;

/** LuaHostHooks から参照するボタン入力（game_machine_main 専用） */
struct LuaHostButtonContext {
    ButtonInput* buttons = nullptr;
};

static LuaHostButtonContext g_luaHostButtonCtx;

// ---------------------------------------------------------------------------
// 起動画面・エラー表示用の簡易 LCD ヘルパ（メニュー外のブートメッセージ向け）
// ---------------------------------------------------------------------------

static void bootScreenClear(uint16_t color);
static void bootScreenDrawText(int x, int y, const char* text, uint16_t color, uint16_t bg_color);
static void bootScreenWaitForAnyButtonPress();
static void bootScreenWaitForAllButtonsReleased();

// ---------------------------------------------------------------------------
// ハードウェア初期化
// ---------------------------------------------------------------------------

static void initLcdSpiDma(ST7789_LCD& lcd_ref);
static void bindLuaRuntimeToHardware();

// ---------------------------------------------------------------------------
// ゲーム選択メニュー ↔ Lua 実行の橋渡し
// ---------------------------------------------------------------------------

static void menuIdleFrameTick(void* user_data);
static void menuLaunchGameCallback(const char* path, void* user_data);
static void menuLaunchInputTestCallback(void* user_data);
static void menuOnSdCardStateChanged(void* user_data);

static void syncLuaInterpreterSdMountFlag();
static bool runGameFromMenuAndTeardown(const char* script_path);
static void teardownLuaSessionAfterGame();
static void showGameStartFailureScreen(const char* script_path);
static void waitForAllButtonsReleased(ButtonInput* btn_input, const char* log_tag);

// ---------------------------------------------------------------------------
// 入力テスト画面（SD 未マウント時など）
// ---------------------------------------------------------------------------

static void inputTestIdleFrameTick(void* user_data);
static void runInputTestScreen();
static bool inputTestTryMountSdCard(void* user_data);

// ---------------------------------------------------------------------------
// LuaHostHooks コールバック（Lua → C++ ホスト）
// ---------------------------------------------------------------------------

static void luaHostDrawText(void* user_data, int x, int y, const char* text, uint16_t color,
                            uint16_t bg_color) {
    (void)user_data;
    bootScreenDrawText(x, y, text, color, bg_color);
}

static bool luaHostIsButtonPressed(void* user_data, int button_index) {
    auto* ctx = static_cast<LuaHostButtonContext*>(user_data);
    if (!ctx || !ctx->buttons || button_index < 0 || button_index >= 8) {
        return false;
    }
    ctx->buttons->update();
    return ctx->buttons->isPressed(static_cast<Button>(button_index));
}

static bool isAnyButtonHeld(ButtonInput* btn_input) {
    if (!btn_input) {
        return false;
    }
    for (int i = 0; i < 8; i++) {
        if (btn_input->isPressed(static_cast<Button>(i))) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Lua ランタイムと描画・入力の接続
// ---------------------------------------------------------------------------

static void bindLuaRuntimeToHardware() {
    g_luaHostButtonCtx.buttons = buttons;

    // GameDisplay: バンドバッファ ↔ ST7789 SPI DMA
    g_gameDisplay.bind(framebuffer_a, framebuffer_b, GameConfig::SCREEN_WIDTH,
                       GameConfig::SCREEN_HEIGHT, GameConfig::BUFFER_HEIGHT, lcd,
                       lcd_spi_dma_channel, lcd_spi_dma_buffer, sizeof(lcd_spi_dma_buffer));

    LuaHostHooks hooks = {};
    hooks.user_data = &g_luaHostButtonCtx;
    hooks.draw_text_bg = luaHostDrawText;
    hooks.is_button_pressed = luaHostIsButtonPressed;
    hooks.display = &g_gameDisplay;

    g_luaInterpreter.setHostHooks(hooks);
    syncLuaInterpreterSdMountFlag();
}

static void syncLuaInterpreterSdMountFlag() {
    g_luaInterpreter.setSdMounted(SdService::isMounted());
}

// ---------------------------------------------------------------------------
// ゲーム実行（メニュー → Lua → メニュー）
// ---------------------------------------------------------------------------

/**
 * メニューで選ばれた .lua を実行し、終了後に Lua セッションを片付けてメニューへ戻る。
 * @return true … ゲームループが正常終了（game_update が true を返した等）
 */
static bool runGameFromMenuAndTeardown(const char* script_path) {
    if (!script_path || script_path[0] == '\0') {
        return false;
    }

    printf("GameMenu: run %s\n", script_path);
    fflush(stdout);

    const bool game_loop_ok = g_luaInterpreter.runGameLoopFromSd(script_path);
    printf("[MENU-DBG] runGameFromMenu: loop finished ok=%d\n", game_loop_ok ? 1 : 0);
    fflush(stdout);

    // 終了ボタン（例: VN の FAR 長押し）が離れるまで待つ
    waitForAllButtonsReleased(buttons, "runGameFromMenu");

    teardownLuaSessionAfterGame();

    if (lcd) {
        lcd->finishDrawRawImageDMA();
    }

    if (game_loop_ok) {
        return true;
    }

    showGameStartFailureScreen(script_path);
    return false;
}

/** ゲームループ終了後の Lua / 音声 / 画像リソース解放（メニュー復帰前に完了させる） */
static void teardownLuaSessionAfterGame() {
    g_luaInterpreter.finishGameSession();

    if (g_luaInterpreter.hasPendingGameSession()) {
        g_luaInterpreter.closePendingGameSession();
    }

    syncLuaInterpreterSdMountFlag();
    printf("[MENU-DBG] runGameFromMenu: session teardown done\n");
    fflush(stdout);
}

static void showGameStartFailureScreen(const char* script_path) {
    bootScreenClear(Color::BLACK);
    bootScreenDrawText(10, 80, "Game start failed", Color::RED, Color::BLACK);
    bootScreenDrawText(10, 92, script_path, Color::WHITE, Color::BLACK);
    const char* err = g_luaInterpreter.lastError();
    if (err && err[0] != '\0') {
        bootScreenDrawText(10, 116, err, Color::ORANGE, Color::BLACK);
    }
    bootScreenDrawText(10, 140, "Press any button", Color::YELLOW, Color::BLACK);
    bootScreenWaitForAnyButtonPress();
}

/** デバッグ用: 全ボタン離しを待つ（シリアルに経過時間を出す） */
static void waitForAllButtonsReleased(ButtonInput* btn_input, const char* log_tag) {
    if (!btn_input) {
        return;
    }
    printf("[MENU-DBG] %s: wait button release\n", log_tag);
    fflush(stdout);

    uint32_t waited_ms = 0;
    while (true) {
        btn_input->update();
        if (!isAnyButtonHeld(btn_input)) {
            break;
        }
        if (waited_ms == 0 || (waited_ms % 500) == 0) {
            printf("[MENU-DBG] %s: still held (%lu ms)\n", log_tag,
                   static_cast<unsigned long>(waited_ms));
            fflush(stdout);
        }
        sleep_ms(50);
        waited_ms += 50;
    }

    printf("[MENU-DBG] %s: released (%lu ms)\n", log_tag, static_cast<unsigned long>(waited_ms));
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// GameSelectMenu コールバック
// ---------------------------------------------------------------------------

/** メニュー表示中の毎フレーム処理（音声ストリーム・エンコーダ音量） */
static void menuIdleFrameTick(void* user_data) {
    inputTestIdleFrameTick(user_data);
}

static void menuLaunchGameCallback(const char* path, void* user_data) {
    (void)user_data;
    runGameFromMenuAndTeardown(path);
}

static void menuLaunchInputTestCallback(void* user_data) {
    (void)user_data;
    runInputTestScreen();
}

static void menuOnSdCardStateChanged(void* user_data) {
    (void)user_data;
    syncLuaInterpreterSdMountFlag();
    if (SdService::isMounted()) {
        printf("SD Card connected.\n");
        SdService::listRoot();
    }
}

// ---------------------------------------------------------------------------
// 入力テスト画面
// ---------------------------------------------------------------------------

static void inputTestIdleFrameTick(void* user_data) {
    (void)user_data;
    g_luaInterpreter.audioEngine().pumpStream();
    EncoderVolumeControl::service();
}

static bool inputTestTryMountSdCard(void* user_data) {
    (void)user_data;
    if (!SdService::isCardPresent()) {
        return false;
    }
    if (!SdService::mount()) {
        return false;
    }
    menuOnSdCardStateChanged(nullptr);
    return true;
}

static void runInputTestScreen() {
    if (!lcd || !buttons) {
        return;
    }
    InputTestMode::Config config = {};
    config.lcd = lcd;
    config.buttons = buttons;
    config.encoder = &EncoderVolumeControl::encoder();
    config.try_mount = inputTestTryMountSdCard;
    config.on_frame = inputTestIdleFrameTick;
    config.mount_retry_ms = 2000;
    config.frame_interval_ms = 50;
    InputTestMode::run(config);
}

// ---------------------------------------------------------------------------
// ブート画面用 LCD ヘルパ
// ---------------------------------------------------------------------------

static void bootScreenClear(uint16_t color) {
    g_gameDisplay.fillScreen(color);
}

static void bootScreenDrawText(int x, int y, const char* text, uint16_t color, uint16_t bg_color) {
    if (!lcd) {
        return;
    }
    lcd->drawTextBg(x, y, text, color, bg_color);
}

static void bootScreenWaitForAllButtonsReleased() {
    if (!buttons) {
        return;
    }
    while (true) {
        buttons->update();
        if (!isAnyButtonHeld(buttons)) {
            break;
        }
        sleep_ms(50);
    }
}

static void bootScreenWaitForAnyButtonPress() {
    if (!buttons) {
        sleep_ms(1000);
        return;
    }
    while (true) {
        buttons->update();
        if (isAnyButtonHeld(buttons)) {
            bootScreenWaitForAllButtonsReleased();
            break;
        }
        sleep_ms(50);
    }
}

// ---------------------------------------------------------------------------
// LCD SPI DMA
// ---------------------------------------------------------------------------

static void initLcdSpiDma(ST7789_LCD& lcd_ref) {
    lcd_spi_dma_channel = dma_claim_unused_channel(true);
    dma_channel_config config = dma_channel_get_default_config(lcd_spi_dma_channel);

    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_dreq(&config, spi_get_dreq(lcd_ref.getSPI(), true));
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);

    dma_channel_configure(lcd_spi_dma_channel, &config, &spi_get_hw(lcd_ref.getSPI())->dr, NULL, 0,
                          false);

    printf("DMA初期化完了\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    stdio_init_all();
    // デバッグ時: USB シリアル接続を待つとログ取りやすい
    // while (!stdio_usb_connected()) { sleep_ms(10); }
    printf("=== ゲーム機初期化開始 ===\n");

    // --- I2C（ボタン・バッテリー LED）---
    gpio_init(I2CConfig::RST);
    gpio_set_dir(I2CConfig::RST, GPIO_OUT);
    gpio_put(I2CConfig::RST, 1);

    printf("I2C初期化中...\n");
    i2c_init(I2CConfig::PORT, I2CConfig::BAUD_RATE);
    gpio_set_function(I2CConfig::SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2CConfig::SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2CConfig::SDA);
    gpio_pull_up(I2CConfig::SCL);

    // --- SD カード ---
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
        syncLuaInterpreterSdMountFlag();
    } else {
        printf("SDカードマウント失敗（上の [SD DBG] ログを確認）\n");
    }

    // --- ボタン ---
    printf("ボタン入力初期化中...\n");
    buttons = new ButtonInput(I2CConfig::PORT, I2CConfig::PCA9539_ADDR);
    if (!buttons->init()) {
        printf("ボタン入力初期化失敗\n");
        return -1;
    }

    // --- LCD + Lua ランタイム接続 ---
    printf("LCD初期化中...\n");
    DeviceSettings::load();
    lcd = new ST7789_LCD();
    lcd->init();
    lcd->setBacklightPercent(DeviceSettings::brightnessPercent());
    initLcdSpiDma(*lcd);
    bindLuaRuntimeToHardware();

    bootScreenClear(Color::GRAY);
    bootScreenDrawText(10, 100, "LCD init", Color::WHITE, Color::BLACK);

    // --- 音声（Core1 I2S）---
    printf("音声出力初期化中 (PCM5102 I2S)...\n");
    BatteryMonitor::attach(buttons);
    audio = new AudioOutput();
    if (!audio->init()) {
        bootScreenDrawText(10, 120, "Audio fail", Color::RED, Color::GRAY);
    } else {
        g_luaInterpreter.setAudioOutput(audio);
        bootScreenDrawText(10, 120, "Audio I2S OK", Color::WHITE, Color::GRAY);
    }

    EncoderVolumeControl::init(audio, &g_luaInterpreter.audioEngine());
    EncoderVolumeControl::restoreVolumeStep(DeviceSettings::volumeStep());
    if (!EncoderVolumeControl::initEncoder()) {
        bootScreenDrawText(10, 132, "Enc IRQ fail", Color::ORANGE, Color::GRAY);
    }

    printf("=== 初期化完了 ===\n");
    bootScreenDrawText(10, 140, "Game select menu", Color::WHITE, Color::BLACK);

    g_gameDisplay.waitForTransferComplete();
    lcd->finishDrawRawImageDMA();

    // --- ゲーム選択メニュー（戻らない）---
    GameSelectMenu::Config menu = {};
    menu.lcd = lcd;
    menu.buttons = buttons;
    menu.games_dir = GameConfig::GAMES_DIR;
    menu.on_frame = menuIdleFrameTick;
    menu.on_run_game = menuLaunchGameCallback;
    menu.on_run_input_test = menuLaunchInputTestCallback;
    menu.on_sd_state_changed = menuOnSdCardStateChanged;
    GameSelectMenu::run(menu);
}
