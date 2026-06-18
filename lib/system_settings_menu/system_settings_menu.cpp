// ============================================
// ファイル: system_settings_menu.cpp
// ============================================

#include "system_settings_menu.hpp"

#include <cstdio>
#include <cstring>

#include "button_input.hpp"
#include "config.hpp"
#include "device_settings.hpp"
#include "encoder_volume.hpp"
#include "pico/stdlib.h"
#include "st7789_lcd.hpp"

namespace {

constexpr uint16_t kSettingsBg = Color::rgb(15, 22, 34);
constexpr uint16_t kSettingsSelBg = Color::rgb(30, 70, 110);
constexpr int kSettingsPanelW = 260;
constexpr int kSettingsPanelH = 176;
constexpr int kSettingsPanelX = (GameConfig::SCREEN_WIDTH - kSettingsPanelW) / 2;
constexpr int kSettingsPanelY = 34;
constexpr int kSettingsRowFirstY = kSettingsPanelY + 16;
constexpr int kSettingsRowPitch = 24;
constexpr int kSettingsRowBgH = 8;

constexpr int kSettingsRowCount = 6;
constexpr int kVolumeRowIndex = 2;
constexpr int kBrightnessRowIndex = 3;
constexpr int kBacklightStepPercent = 10;
constexpr int kSettingsFooterTextY = 222;
constexpr int kSettingsFooterClearY = 216;
constexpr int kSettingsFooterClearH = GameConfig::SCREEN_HEIGHT - kSettingsFooterClearY;

struct SettingsState {
    int volume_step = EncoderVolumeControl::kVolumeStepMax;
    int brightness_percent = 80;
    bool editing_brightness = false;
    char row_labels[kSettingsRowCount][48] = {};
};

struct SettingsUiCache {
    bool ready = false;
    int prev_cursor = -1;
};

/** 8x8 フォント前提で文字列の描画幅（px）を返す。中央揃え計算時に使う */
int textWidthPx(const char* text) {
    if (!text) {
        return 0;
    }
    return static_cast<int>(std::strlen(text)) * 8;
}

/** 画面幅中央に背景付きテキストを描く。タイトル・フッター表示時に使う */
void drawTextCenteredBg(ST7789_LCD* lcd, int y, const char* text, uint16_t fg, uint16_t bg) {
    if (!lcd) {
        return;
    }
    const int x = (GameConfig::SCREEN_WIDTH - textWidthPx(text)) / 2;
    lcd->drawTextBg(x < 0 ? 0 : x, y, text, fg, bg);
}

/** 8 ボタンのいずれかが押下中か判定する。離し待ちループで毎フレーム使う */
bool isAnyButtonPressed(ButtonInput* buttons) {
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

/** 全ボタンが離されるまでブロックする。設定メニュー入退場時のチャタリング防止に使う */
void waitForButtonRelease(ButtonInput* buttons) {
    if (!buttons) {
        return;
    }
    while (true) {
        buttons->update();
        if (!isAnyButtonPressed(buttons)) {
            break;
        }
        sleep_ms(50);
    }
}

/** パーセント値を [====----] 形式のメーター行に整形する。明るさ行ラベル更新時に使う */
void buildMeterLine(char* out, size_t out_len, const char* prefix, int percent) {
    if (!out || out_len == 0) {
        return;
    }
    int filled = percent / 10;
    if (filled < 1 && percent >= 10) {
        filled = 1;
    }
    if (filled > 10) {
        filled = 10;
    }
    char bar[11];
    for (int i = 0; i < 10; i++) {
        bar[i] = (i < filled) ? '=' : '-';
    }
    bar[10] = '\0';
    std::snprintf(out, out_len, "%s [%s] %d%%", prefix, bar, percent);
}

/** 音量ステップをメーター付き行文字列に整形する。音量行ラベル更新時に使う */
void buildVolumeMeterLine(char* out, size_t out_len, int step) {
    if (!out || out_len == 0) {
        return;
    }
    int filled = 0;
    if (EncoderVolumeControl::kVolumeStepMax > 0) {
        filled = (step * 10 + EncoderVolumeControl::kVolumeStepMax / 2) /
                 EncoderVolumeControl::kVolumeStepMax;
    }
    if (filled > 10) {
        filled = 10;
    }
    char bar[11];
    for (int i = 0; i < 10; i++) {
        bar[i] = (i < filled) ? '=' : '-';
    }
    bar[10] = '\0';
    std::snprintf(out, out_len, "Volume: [%s] %d/%d", bar, step + 1,
                  EncoderVolumeControl::kVolumeSteps);
}

/** 全設定行の表示ラベルを最新状態で再構築する。音量・明るさ変更後に呼ぶ */
void refreshSettingsRowLabels(SettingsState& state) {
    std::snprintf(state.row_labels[0], sizeof(state.row_labels[0]), "WiFi: [CONNECTED]");
    std::snprintf(state.row_labels[1], sizeof(state.row_labels[1]), "SSID: HOME_NET");
    buildVolumeMeterLine(state.row_labels[2], sizeof(state.row_labels[2]), state.volume_step);
    buildMeterLine(state.row_labels[3], sizeof(state.row_labels[3]), "Brightness:",
                   state.brightness_percent);
    std::snprintf(state.row_labels[4], sizeof(state.row_labels[4]), "Input Test Mode");
    std::snprintf(state.row_labels[5], sizeof(state.row_labels[5]), "Back");
}

/** 指定行の表示ラベル文字列を返す。行描画時に使う */
const char* settingsRowLabel(const SettingsState& state, int index) {
    if (index < 0 || index >= kSettingsRowCount) {
        return "";
    }
    return state.row_labels[index];
}

/** LCD の現在輝度を状態へ同期しラベルを更新する。画面初期化時に呼ぶ */
void syncBrightnessFromLcd(ST7789_LCD* lcd, SettingsState& state) {
    if (!lcd) {
        return;
    }
    state.brightness_percent = lcd->backlightPercent();
    refreshSettingsRowLabels(state);
}

/** エンコーダ音量ステップを状態へ同期しラベルを更新する。画面初期化時に呼ぶ */
void syncVolumeFromEncoder(SettingsState& state) {
    state.volume_step = EncoderVolumeControl::volumeStep();
    refreshSettingsRowLabels(state);
}

/** バックライト輝度を LCD と永続設定へ反映する。明るさ調整確定時に呼ぶ */
void applyBrightness(ST7789_LCD* lcd, SettingsState& state, int percent) {
    if (!lcd) {
        return;
    }
    lcd->setBacklightPercent(percent);
    state.brightness_percent = lcd->backlightPercent();
    DeviceSettings::setBrightnessPercent(state.brightness_percent);
    refreshSettingsRowLabels(state);
}

/** 明るさを delta 分だけ増減する。編集モードで LEFT/RIGHT 入力時に呼ぶ */
void adjustBrightness(ST7789_LCD* lcd, SettingsState& state, int delta) {
    int next = state.brightness_percent + delta;
    if (next < ST7789_LCD::kBacklightMinPercent) {
        next = ST7789_LCD::kBacklightMinPercent;
    } else if (next > ST7789_LCD::kBacklightMaxPercent) {
        next = ST7789_LCD::kBacklightMaxPercent;
    }
    applyBrightness(lcd, state, next);
}

/** 画面下部の操作ヒントを描く。モード切替や初期表示時に呼ぶ */
void drawSettingsFooterHint(ST7789_LCD* lcd, bool editing_brightness) {
    if (!lcd) {
        return;
    }
    lcd->fillRect(0, kSettingsFooterClearY, GameConfig::SCREEN_WIDTH, kSettingsFooterClearH,
                  kSettingsBg);
    if (editing_brightness) {
        drawTextCenteredBg(lcd, kSettingsFooterTextY, "[L/R] Brightness  [FAR] Back", Color::GREEN,
                           kSettingsBg);
    } else {
        drawTextCenteredBg(lcd, kSettingsFooterTextY, "[U/D] Select  [NEAR] Enter  [L] Back",
                           Color::GREEN, kSettingsBg);
    }
}

/** 設定画面の固定枠（パネル・タイトル・フッター）を描く。初期化や全面再描画時に呼ぶ */
void drawSettingsStaticChrome(ST7789_LCD* lcd, bool editing_brightness) {
    if (!lcd) {
        return;
    }
    lcd->fill(kSettingsBg);
    lcd->drawRect(kSettingsPanelX, kSettingsPanelY, kSettingsPanelW, kSettingsPanelH, Color::CYAN);
    drawTextCenteredBg(lcd, kSettingsPanelY - 18, "== SYSTEM MENU ==", Color::CYAN, kSettingsBg);
    drawSettingsFooterHint(lcd, editing_brightness);
}

/** 設定メニューの 1 行を描画または更新する。カーソル移動・値変更時に呼ぶ */
void drawSettingsRow(ST7789_LCD* lcd, const SettingsState& state, int row_index, int cursor) {
    if (!lcd || row_index < 0 || row_index >= kSettingsRowCount) {
        return;
    }
    const int row_top = kSettingsRowFirstY + row_index * kSettingsRowPitch;
    const int bg_y = row_top + (kSettingsRowPitch - kSettingsRowBgH) / 2;
    const bool selected = (row_index == cursor);
    const bool editing_row = selected && state.editing_brightness && row_index == kBrightnessRowIndex;
    const uint16_t fg = editing_row ? Color::rgb(255, 220, 100)
                                    : (selected ? Color::WHITE : Color::rgb(180, 205, 230));
    const uint16_t bg = editing_row ? Color::rgb(50, 90, 50)
                                    : (selected ? kSettingsSelBg : kSettingsBg);
    lcd->fillRect(kSettingsPanelX + 10, bg_y, kSettingsPanelW - 20, kSettingsRowBgH, bg);
    const char* marker = editing_row ? "*" : (selected ? ">" : " ");
    lcd->drawTextBg(kSettingsPanelX + 12, bg_y, marker, fg, bg);
    lcd->drawTextBg(kSettingsPanelX + 26, bg_y, settingsRowLabel(state, row_index), fg, bg);
}

/** 設定画面を初回描画し状態を同期する。run 入場時や入力テスト復帰後に呼ぶ */
void initSettingsScreen(ST7789_LCD* lcd, SettingsUiCache& cache, SettingsState& state, int cursor) {
    state.editing_brightness = false;
    syncVolumeFromEncoder(state);
    syncBrightnessFromLcd(lcd, state);
    cache.ready = false;
    cache.prev_cursor = -1;
    drawSettingsStaticChrome(lcd, state.editing_brightness);
    for (int i = 0; i < kSettingsRowCount; i++) {
        drawSettingsRow(lcd, state, i, cursor);
    }
    cache.ready = true;
    cache.prev_cursor = cursor;
}

/** カーソル移動時に旧行と新行のみ部分更新する。UP/DOWN 入力時に呼ぶ */
void updateSettingsCursor(ST7789_LCD* lcd, SettingsUiCache& cache, SettingsState& state,
                          int old_cursor, int new_cursor) {
    if (old_cursor >= 0 && old_cursor < kSettingsRowCount) {
        drawSettingsRow(lcd, state, old_cursor, new_cursor);
    }
    drawSettingsRow(lcd, state, new_cursor, new_cursor);
    cache.prev_cursor = new_cursor;
}

}  // namespace

/** システム設定メニューのメインループ。ゲーム選択メニューから起動し LEFT で戻る */
void SystemSettingsMenu::run(const Config& config) {
    if (!config.lcd || !config.buttons) {
        return;
    }

    SettingsUiCache cache = {};
    SettingsState state = {};
    int cursor = 0;
    initSettingsScreen(config.lcd, cache, state, cursor);
    waitForButtonRelease(config.buttons);

    while (true) {
        config.buttons->update();
        if (config.on_frame) {
            config.on_frame(config.user_data);
        }

        const int old_cursor = cursor;
        bool cursor_changed = false;
        bool brightness_changed = false;
        bool volume_changed = false;
        bool ui_changed = false;

        const int encoder_volume_step = EncoderVolumeControl::volumeStep();
        if (encoder_volume_step != state.volume_step) {
            state.volume_step = encoder_volume_step;
            refreshSettingsRowLabels(state);
            volume_changed = true;
        }

        if (state.editing_brightness) {
            if (config.buttons->wasPressed(Button::LEFT)) {
                adjustBrightness(config.lcd, state, -kBacklightStepPercent);
                brightness_changed = true;
            }
            if (config.buttons->wasPressed(Button::RIGHT)) {
                adjustBrightness(config.lcd, state, kBacklightStepPercent);
                brightness_changed = true;
            }
            if (config.buttons->wasPressed(Button::FAR)) {
                state.editing_brightness = false;
                drawSettingsFooterHint(config.lcd, false);
                ui_changed = true;
            }
        } else {
            if (config.buttons->wasPressed(Button::UP) && cursor > 0) {
                cursor--;
                cursor_changed = true;
            }
            if (config.buttons->wasPressed(Button::DOWN) && cursor < kSettingsRowCount - 1) {
                cursor++;
                cursor_changed = true;
            }
            if (config.buttons->wasPressed(Button::LEFT) ||
                config.buttons->wasPressed(Button::OP_LEFT)) {
                break;
            }
            if (config.buttons->wasPressed(Button::NEAR)) {
                if (cursor == kBrightnessRowIndex) {
                    state.editing_brightness = true;
                    drawSettingsFooterHint(config.lcd, true);
                    ui_changed = true;
                } else if (cursor == 4 && config.on_run_input_test) {
                    config.on_run_input_test(config.user_data);
                    waitForButtonRelease(config.buttons);
                    initSettingsScreen(config.lcd, cache, state, cursor);
                } else if (cursor == 5) {
                    break;
                }
            }
        }

        if (brightness_changed || ui_changed) {
            drawSettingsRow(config.lcd, state, kBrightnessRowIndex, cursor);
        } else if (volume_changed) {
            drawSettingsRow(config.lcd, state, kVolumeRowIndex, cursor);
        } else if (cursor_changed) {
            updateSettingsCursor(config.lcd, cache, state, old_cursor, cursor);
        }
        sleep_ms(config.frame_interval_ms);
    }
    waitForButtonRelease(config.buttons);
}
