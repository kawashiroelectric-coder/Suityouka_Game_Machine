// ============================================
// ファイル: input_test_mode.cpp
// 設定画面から起動するエンコーダ・ボタン入力テスト画面
// ============================================

#include "input_test_mode.hpp"

#include <cstdio>
#include <cstring>

#include "button_input.hpp"
#include "battery_monitor.hpp"
#include "config.hpp"
#include "encoder_input.hpp"
#include "pico/stdlib.h"
#include "st7789_lcd.hpp"

namespace {

struct ButtonLayout {
    Button id;
    int x;
    int y;
    int w;
    int h;
    const char* label;
};

constexpr ButtonLayout kButtons[] = {
    {Button::UP, 140, 44, 40, 22, "UP"},
    {Button::DOWN, 140, 92, 40, 22, "DN"},
    {Button::LEFT, 92, 68, 40, 22, "LT"},
    {Button::RIGHT, 188, 68, 40, 22, "RT"},
    {Button::OP_LEFT, 24, 118, 44, 22, "A-L"},
    {Button::OP_RIGHT, 76, 118, 44, 22, "A-R"},
    {Button::NEAR, 200, 118, 44, 22, "NEAR"},
    {Button::FAR, 252, 118, 44, 22, "FAR"},
};

constexpr int kBarX = 24;
constexpr int kBarY = 196;
constexpr int kBarW = 272;
constexpr int kBarH = 12;
constexpr uint16_t kBarFill = Color::rgb(0, 0, 0);

struct UiState {
    bool button_pressed[8] = {};
    bool button_had_edge[8] = {};
    int marker_x = -1;
    char pos_line[32] = {};
    char delta_line[16] = {};
    char pin_line[32] = {};
    char raw_line[32] = {};
    char battery_line[16] = {};
};

/** テキストが前回と異なるときだけ LCD に描画する。部分更新のちらつき防止に使う */
bool drawTextIfChanged(ST7789_LCD* lcd, int x, int y, const char* text, uint16_t fg,
                       uint16_t bg, char* cache, size_t cache_size) {
    if (strcmp(cache, text) == 0) {
        return false;
    }
    strncpy(cache, text, cache_size - 1);
    cache[cache_size - 1] = '\0';
    lcd->drawTextBg(static_cast<uint16_t>(x), static_cast<uint16_t>(y), text, fg, bg);
    return true;
}

/** バッテリー電圧を表示用文字列に整形する。右上表示更新時に使う */
void formatBatteryLine(char* line, size_t line_size) {
    const float voltage = BatteryMonitor::lastVoltage();
    snprintf(line, line_size, "%6.3f V", static_cast<double>(voltage));
}

/** バッテリー電圧表示を必要時のみ更新する。各フレームの動的更新時に呼ぶ */
bool updateBatteryLine(ST7789_LCD* lcd, UiState& state) {
    char line[16];
    formatBatteryLine(line, sizeof(line));
    return drawTextIfChanged(lcd, 240, 6, line, Color::WHITE, Color::BLACK, state.battery_line,
                             sizeof(state.battery_line));
}

/** ボタン 1 個分の枠とラベルを描画する。押下状態・エッジ変化時に呼ぶ */
void drawButtonBox(ST7789_LCD* lcd, const ButtonLayout& layout, bool pressed, bool edge) {
    const uint16_t fill = pressed ? Color::GREEN : Color::rgb(40, 40, 40);
    const uint16_t border = edge ? Color::YELLOW : (pressed ? Color::WHITE : Color::GRAY);
    lcd->fillRect(static_cast<uint16_t>(layout.x), static_cast<uint16_t>(layout.y),
                  static_cast<uint16_t>(layout.w), static_cast<uint16_t>(layout.h), fill);
    lcd->drawRect(static_cast<uint16_t>(layout.x), static_cast<uint16_t>(layout.y),
                  static_cast<uint16_t>(layout.w), static_cast<uint16_t>(layout.h), border);
    const uint16_t text = pressed ? Color::BLACK : Color::WHITE;
    const int tx = layout.x + (layout.w - static_cast<int>(strlen(layout.label)) * 8) / 2;
    const int ty = layout.y + (layout.h - 8) / 2;
    lcd->drawTextBg(static_cast<uint16_t>(tx), static_cast<uint16_t>(ty), layout.label, text,
                    fill);
}

/** 入力テスト画面の固定背景と説明を描く。画面入場時に一度呼ぶ */
void drawStaticBackground(ST7789_LCD* lcd) {
    lcd->fill(Color::BLACK);
    lcd->drawTextBg(8, 6, "INPUT TEST MODE", Color::WHITE, Color::BLACK);
    lcd->drawTextBg(8, 18, "[LEFT] & [FAR] Back", Color::GREEN, Color::BLACK);

    lcd->fillRect(kBarX, kBarY, kBarW, kBarH, kBarFill);

    char line[48];
    snprintf(line, sizeof(line), "GP%d/A GP%d/B GP%d/SW", EncoderConfig::PIN_A,
             EncoderConfig::PIN_B, EncoderConfig::PIN_SW);
    lcd->drawTextBg(24, 172, line, Color::GRAY, Color::BLACK);
}

/** エンコーダ位置バーの枠線を描く。バー更新のたびに呼ぶ */
void drawEncoderBarFrame(ST7789_LCD* lcd) {
    lcd->drawRect(kBarX, kBarY, kBarW, kBarH, Color::GRAY);
}

/** エンコーダ位置に応じてバー上のマーカーを移動描画する。各フレームの動的更新時に呼ぶ */
void refreshEncoderBar(ST7789_LCD* lcd, int32_t position, UiState& state) {
    const int marker = kBarX + static_cast<int>((position % kBarW + kBarW) % kBarW);

    if (state.marker_x >= 0 && state.marker_x != marker) {
        lcd->fillRect(static_cast<uint16_t>(state.marker_x), static_cast<uint16_t>(kBarY - 2), 4,
                      static_cast<uint16_t>(kBarH + 4), kBarFill);
    }

    drawEncoderBarFrame(lcd);

    lcd->fillRect(static_cast<uint16_t>(marker), static_cast<uint16_t>(kBarY - 2), 4,
                  static_cast<uint16_t>(kBarH + 4), Color::CYAN);
    state.marker_x = marker;
}

/** 全ボタンの押下状態表示を更新する。各フレームの動的更新時に呼ぶ */
bool updateButtons(ST7789_LCD* lcd, ButtonInput* buttons, UiState& state) {
    bool changed = false;
    for (size_t i = 0; i < sizeof(kButtons) / sizeof(kButtons[0]); ++i) {
        const ButtonLayout& layout = kButtons[i];
        const int idx = static_cast<int>(layout.id);
        const bool pressed = buttons->isPressed(layout.id);
        const bool edge = buttons->wasPressed(layout.id);
        if (pressed == state.button_pressed[idx] && !edge && !state.button_had_edge[idx]) {
            continue;
        }
        drawButtonBox(lcd, layout, pressed, edge);
        state.button_pressed[idx] = pressed;
        state.button_had_edge[idx] = edge;
        changed = true;
    }
    return changed;
}

/** エンコーダ・ボタン・バッテリー等の動的表示をまとめて更新する。メインループ各フレームで呼ぶ */
bool updateDynamicFields(ST7789_LCD* lcd, ButtonInput* buttons, EncoderInput& encoder,
                         int32_t frame_delta, UiState& state) {
    bool changed = updateButtons(lcd, buttons, state);

    char line[48];

    snprintf(line, sizeof(line), "Encoder pos: %8ld", static_cast<long>(encoder.position()));
    changed |= drawTextIfChanged(lcd, 24, 148, line, Color::WHITE, Color::BLACK, state.pos_line,
                                 sizeof(state.pos_line));

    if (frame_delta != 0) {
        snprintf(line, sizeof(line), "Delta: %+4ld", static_cast<long>(frame_delta));
        changed |= drawTextIfChanged(lcd, 200, 148, line, Color::CYAN, Color::BLACK,
                                     state.delta_line, sizeof(state.delta_line));
    } else {
        changed |= drawTextIfChanged(lcd, 200, 148, "Delta:   +0", Color::GRAY, Color::BLACK,
                                     state.delta_line, sizeof(state.delta_line));
    }

    snprintf(line, sizeof(line), "A:%d B:%d SW:%3s (%4lu)", encoder.pinA() ? 1 : 0,
             encoder.pinB() ? 1 : 0, encoder.isSwitchPressed() ? "ON" : "OFF",
             static_cast<unsigned long>(encoder.switchPressCount()));
    changed |= drawTextIfChanged(lcd, 24, 160, line, Color::WHITE, Color::BLACK, state.pin_line,
                                 sizeof(state.pin_line));

    refreshEncoderBar(lcd, encoder.position(), state);

    const uint8_t raw = buttons->getAllButtons();
    snprintf(line, sizeof(line), "Port0 raw: 0x%02X (1=open)", raw);
    changed |= drawTextIfChanged(lcd, 24, 214, line, Color::GRAY, Color::BLACK, state.raw_line,
                                 sizeof(state.raw_line));

    changed |= updateBatteryLine(lcd, state);

    return changed;
}

/** 入力テスト画面を初期描画し UI 状態をリセットする。run 入場時に一度呼ぶ */
void initUiState(ST7789_LCD* lcd, ButtonInput* buttons, EncoderInput& encoder, UiState& state) {
    drawStaticBackground(lcd);
    memset(&state, 0, sizeof(state));
    state.marker_x = -1;

    char line[48];
    snprintf(line, sizeof(line), "Encoder pos: %8ld", static_cast<long>(encoder.position()));
    lcd->drawTextBg(24, 148, line, Color::WHITE, Color::BLACK);
    strncpy(state.pos_line, line, sizeof(state.pos_line));

    lcd->drawTextBg(200, 148, "Delta:   +0", Color::GRAY, Color::BLACK);
    strncpy(state.delta_line, "Delta:   +0", sizeof(state.delta_line));

    snprintf(line, sizeof(line), "A:%d B:%d SW:%3s (%4lu)", encoder.pinA() ? 1 : 0,
             encoder.pinB() ? 1 : 0, encoder.isSwitchPressed() ? "ON" : "OFF",
             static_cast<unsigned long>(encoder.switchPressCount()));
    lcd->drawTextBg(24, 160, line, Color::WHITE, Color::BLACK);
    strncpy(state.pin_line, line, sizeof(state.pin_line));

    const uint8_t raw = buttons->getAllButtons();
    snprintf(line, sizeof(line), "Port0 raw: 0x%02X (1=open)", raw);
    lcd->drawTextBg(24, 214, line, Color::GRAY, Color::BLACK);
    strncpy(state.raw_line, line, sizeof(state.raw_line));

    formatBatteryLine(line, sizeof(line));
    lcd->drawTextBg(240, 6, line, Color::WHITE, Color::BLACK);
    strncpy(state.battery_line, line, sizeof(state.battery_line));

    for (const ButtonLayout& layout : kButtons) {
        const bool pressed = buttons->isPressed(layout.id);
        drawButtonBox(lcd, layout, pressed, false);
        state.button_pressed[static_cast<int>(layout.id)] = pressed;
    }

    refreshEncoderBar(lcd, encoder.position(), state);
}

}  // namespace

/** 入力テスト画面のメインループ。設定メニューから起動し LEFT+FAR で戻る */
void InputTestMode::run(const Config& config) {
    if (!config.lcd || !config.buttons) {
        return;
    }

    EncoderInput local_encoder;
    EncoderInput* encoder = config.encoder;
    const bool owns_encoder = (encoder == nullptr);
    if (owns_encoder) {
        encoder = &local_encoder;
        if (!encoder->initIrq()) {
            config.lcd->drawTextBg(8, 110, "Encoder IRQ fail", Color::RED, Color::BLACK);
            return;
        }
    }

    UiState ui_state;
    config.buttons->update();
    encoder->update();
    (void)encoder->consumeDelta();
    initUiState(config.lcd, config.buttons, *encoder, ui_state);

    BatteryMonitor::setLedUpdatePaused(true);
    config.buttons->setBatteryLedMask(BatteryLedConfig::PORT1_MASK);

    uint32_t last_mount_attempt_ms = 0;
    absolute_time_t last_frame = get_absolute_time();

    while (true) {
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        if (config.try_mount && (now_ms - last_mount_attempt_ms >= config.mount_retry_ms)) {
            last_mount_attempt_ms = now_ms;
            (void)config.try_mount(config.user_data);
        }

        config.buttons->update();
        if (config.buttons->wasPressed(Button::LEFT) &&
            config.buttons->wasPressed(Button::FAR)) {
            break;
        }
        if (config.on_frame) {
            config.on_frame(config.user_data);
        }

        encoder->update();
        const int32_t frame_delta = owns_encoder ? encoder->consumeDelta() : 0;
        (void)encoder->wasSwitchPressed();

        (void)updateDynamicFields(config.lcd, config.buttons, *encoder, frame_delta, ui_state);

        const absolute_time_t target =
            delayed_by_ms(last_frame, config.frame_interval_ms);
        sleep_until(target);
        last_frame = target;
    }

    BatteryMonitor::resumeLedAutoUpdate();

    if (owns_encoder) {
        encoder->disableIrq();
    }
}
