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
#include "battery_monitor.hpp"
#include "menu_cursor_se.hpp"
#include "menu_backgrounds.hpp"
#include "pico/stdlib.h"
#include "st7789_lcd.hpp"

namespace {

constexpr uint16_t kSettingsBg = Color::rgb(15, 22, 34);
constexpr uint16_t kSettingsSelBg = Color::rgb(30, 70, 110);
constexpr int kSettingsPanelW = 260;
constexpr int kSettingsPanelX = (GameConfig::SCREEN_WIDTH - kSettingsPanelW) / 2;
constexpr int kSettingsPanelY = 34;
constexpr int kSettingsPanelPadTop = 10;
constexpr int kSettingsPanelPadBottom = 10;
constexpr int kSettingsRowPitch = 18;
constexpr int kSettingsRowBgH = 8;
constexpr int kSettingsRowFirstY = kSettingsPanelY + kSettingsPanelPadTop;

constexpr int kSettingsRowCount = 7;
constexpr int kSettingsPanelH =
    kSettingsPanelPadTop + kSettingsRowCount * kSettingsRowPitch + kSettingsPanelPadBottom;

// ---------------------------------------------------------------------------
// About 画面（Code Ver 等）— NEAR で入場、FAR で設定メニューへ戻る
// 表示内容を変えるときは kAboutLines を編集してください。
// サードパーティの正式なライセンス表記は THIRD_PARTY_NOTICES.md を参照してください。
// ---------------------------------------------------------------------------
constexpr const char* kAboutLines[] = {
    "Code Ver 1.1.0",
    "SD: Apache2.0 carlk3",
};
constexpr int kAboutLineCount = static_cast<int>(sizeof(kAboutLines) / sizeof(kAboutLines[0]));
constexpr int kAboutLinePitch = 14;
constexpr int kAboutFirstLineY = 96;

// WiFi 未使用のため行を非表示（将来用にインデックスのみ残す）
// constexpr int kWifiRowIndex = 0;
// constexpr int kSsidRowIndex = 1;
constexpr int kVolumeRowIndex = 0;
constexpr int kBrightnessRowIndex = 1;
constexpr int kBatteryLedRowIndex = 2;
constexpr int kBgGalleryRowIndex = 3;
constexpr int kInputTestRowIndex = 4;
constexpr int kAboutRowIndex = 5;
constexpr int kBackRowIndex = 6;
constexpr int kBacklightStepPercent = 10;
constexpr int kSettingsFooterTextY = 222;
constexpr int kSettingsFooterClearY = 216;
constexpr int kSettingsFooterClearH = GameConfig::SCREEN_HEIGHT - kSettingsFooterClearY;

enum class SettingsFooterMode : uint8_t {
    Normal,
    BrightnessEdit,
    BgGallery,
    About,
};

struct SettingsState {
    int volume_step = EncoderVolumeControl::kVolumeStepMax;
    int brightness_percent = 80;
    DeviceSettings::BatteryLedMode battery_led_mode = DeviceSettings::BatteryLedMode::AlwaysOn;
    bool editing_brightness = false;
    bool viewing_bg = false;
    bool viewing_about = false;
    int bg_index = 0;
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

/** バッテリー LED 表示モード行のラベルを整形する */
void buildBatteryLedLine(char* out, size_t out_len, DeviceSettings::BatteryLedMode mode) {
    if (!out || out_len == 0) {
        return;
    }
    if (mode == DeviceSettings::BatteryLedMode::AlwaysOn) {
        std::snprintf(out, out_len, "Batt LED: Always On");
        return;
    }
    const unsigned long pulse_ms = static_cast<unsigned long>(BatteryConfig::LED_PULSE_MS);
    if (pulse_ms >= 1000u && (pulse_ms % 1000u) == 0u) {
        std::snprintf(out, out_len, "Batt LED: Pulse %lus", pulse_ms / 1000u);
    } else {
        std::snprintf(out, out_len, "Batt LED: Pulse %lums", pulse_ms);
    }
}

/** 全設定行の表示ラベルを最新状態で再構築する。音量・明るさ変更後に呼ぶ */
void refreshSettingsRowLabels(SettingsState& state) {
    // std::snprintf(state.row_labels[0], sizeof(state.row_labels[0]), "WiFi: [CONNECTED]");
    // std::snprintf(state.row_labels[1], sizeof(state.row_labels[1]), "SSID: HOME_NET");
    buildVolumeMeterLine(state.row_labels[0], sizeof(state.row_labels[0]), state.volume_step);
    buildMeterLine(state.row_labels[1], sizeof(state.row_labels[1]), "Brightness:",
                   state.brightness_percent);
    buildBatteryLedLine(state.row_labels[2], sizeof(state.row_labels[2]), state.battery_led_mode);
    std::snprintf(state.row_labels[3], sizeof(state.row_labels[3]), "BG Gallery");
    std::snprintf(state.row_labels[4], sizeof(state.row_labels[4]), "Input Test Mode");
    std::snprintf(state.row_labels[5], sizeof(state.row_labels[5]), "About / Code Ver");
    std::snprintf(state.row_labels[6], sizeof(state.row_labels[6]), "Back");
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

/** 永続設定からバッテリー LED モードを状態へ同期する */
void syncBatteryLedFromSettings(SettingsState& state) {
    state.battery_led_mode = DeviceSettings::batteryLedMode();
    refreshSettingsRowLabels(state);
}

/** バッテリー LED 表示モードを切り替え、LED へ即反映する */
void toggleBatteryLedMode(SettingsState& state) {
    const DeviceSettings::BatteryLedMode next =
        (state.battery_led_mode == DeviceSettings::BatteryLedMode::AlwaysOn)
            ? DeviceSettings::BatteryLedMode::PulseOnChange
            : DeviceSettings::BatteryLedMode::AlwaysOn;
    DeviceSettings::setBatteryLedMode(next);
    state.battery_led_mode = next;
    BatteryMonitor::onDisplayModeChanged();
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
void drawSettingsFooterHint(ST7789_LCD* lcd, SettingsFooterMode mode) {
    if (!lcd) {
        return;
    }
    lcd->fillRect(0, kSettingsFooterClearY, GameConfig::SCREEN_WIDTH, kSettingsFooterClearH,
                  kSettingsBg);
    const char* hint = "[U/D] Select  [NEAR] Enter  [L] Back";
    if (mode == SettingsFooterMode::BrightnessEdit) {
        hint = "[L/R] Brightness  [FAR] Back";
    } else if (mode == SettingsFooterMode::BgGallery) {
        hint = "[L/R] BG Change  [FAR] Back";
    } else if (mode == SettingsFooterMode::About) {
        hint = "[FAR] Back";
    }
    drawTextCenteredBg(lcd, kSettingsFooterTextY, hint, Color::GREEN, kSettingsBg);
}

/** BG 鑑賞画面を描く。BG Gallery モード入場時・L/R 切替時に呼ぶ */
void drawBgGalleryScreen(ST7789_LCD* lcd, int bg_index) {
    if (!lcd || kMenuBackgroundCount <= 0) {
        return;
    }
    if (bg_index < 0) {
        bg_index = 0;
    } else if (bg_index >= kMenuBackgroundCount) {
        bg_index = kMenuBackgroundCount - 1;
    }
    lcd->finishDrawRawImageDMA();
    const MenuBgEntry& bg = kMenuBackgrounds[bg_index];
    lcd->drawRawImage(0, 0, static_cast<uint16_t>(bg.width), static_cast<uint16_t>(bg.height),
                      bg.pixels);
    drawSettingsFooterHint(lcd, SettingsFooterMode::BgGallery);
}

/** About 画面（Code Ver 等）を描く。NEAR 入場時に呼ぶ */
void drawAboutScreen(ST7789_LCD* lcd) {
    if (!lcd) {
        return;
    }
    lcd->fill(kSettingsBg);
    lcd->drawRect(kSettingsPanelX, kSettingsPanelY, kSettingsPanelW, kSettingsPanelH, Color::CYAN);
    drawTextCenteredBg(lcd, kSettingsPanelY - 18, "== ABOUT ==", Color::CYAN, kSettingsBg);
    const uint16_t line_fg = Color::rgb(200, 220, 240);
    for (int i = 0; i < kAboutLineCount; i++) {
        drawTextCenteredBg(lcd, kAboutFirstLineY + i * kAboutLinePitch, kAboutLines[i], line_fg,
                           kSettingsBg);
    }
    drawSettingsFooterHint(lcd, SettingsFooterMode::About);
}

/** 設定画面の固定枠（パネル・タイトル・フッター）を描く。初期化や全面再描画時に呼ぶ */
void drawSettingsStaticChrome(ST7789_LCD* lcd) {
    if (!lcd) {
        return;
    }
    lcd->fill(kSettingsBg);
    lcd->drawRect(kSettingsPanelX, kSettingsPanelY, kSettingsPanelW, kSettingsPanelH, Color::CYAN);
    drawTextCenteredBg(lcd, kSettingsPanelY - 18, "== SYSTEM MENU ==", Color::CYAN, kSettingsBg);
    drawSettingsFooterHint(lcd, SettingsFooterMode::Normal);
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
    state.viewing_bg = false;
    state.viewing_about = false;
    syncVolumeFromEncoder(state);
    syncBrightnessFromLcd(lcd, state);
    syncBatteryLedFromSettings(state);
    cache.ready = false;
    cache.prev_cursor = -1;
    drawSettingsStaticChrome(lcd);
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
        bool battery_led_changed = false;
        bool bg_changed = false;
        bool ui_changed = false;

        const int encoder_volume_step = EncoderVolumeControl::volumeStep();
        if (encoder_volume_step != state.volume_step) {
            state.volume_step = encoder_volume_step;
            refreshSettingsRowLabels(state);
            volume_changed = true;
        }

        if (state.viewing_about) {
            if (config.buttons->wasPressed(Button::FAR)) {
                state.viewing_about = false;
                initSettingsScreen(config.lcd, cache, state, cursor);
                ui_changed = true;
            }
        } else if (state.viewing_bg) {
            if (config.buttons->wasPressed(Button::LEFT)) {
                state.bg_index = (state.bg_index + kMenuBackgroundCount - 1) % kMenuBackgroundCount;
                bg_changed = true;
            }
            if (config.buttons->wasPressed(Button::RIGHT)) {
                state.bg_index = (state.bg_index + 1) % kMenuBackgroundCount;
                bg_changed = true;
            }
            if (config.buttons->wasPressed(Button::FAR)) {
                state.viewing_bg = false;
                initSettingsScreen(config.lcd, cache, state, cursor);
                ui_changed = true;
            }
        } else if (state.editing_brightness) {
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
                drawSettingsFooterHint(config.lcd, SettingsFooterMode::Normal);
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
                    drawSettingsFooterHint(config.lcd, SettingsFooterMode::BrightnessEdit);
                    ui_changed = true;
                } else if (cursor == kBatteryLedRowIndex) {
                    toggleBatteryLedMode(state);
                    battery_led_changed = true;
                } else if (cursor == kBgGalleryRowIndex) {
                    state.viewing_bg = true;
                    state.bg_index = 0;
                    drawBgGalleryScreen(config.lcd, state.bg_index);
                    ui_changed = true;
                } else if (cursor == kInputTestRowIndex && config.on_run_input_test) {
                    config.on_run_input_test(config.user_data);
                    waitForButtonRelease(config.buttons);
                    initSettingsScreen(config.lcd, cache, state, cursor);
                } else if (cursor == kAboutRowIndex) {
                    state.viewing_about = true;
                    drawAboutScreen(config.lcd);
                    waitForButtonRelease(config.buttons);
                    ui_changed = true;
                } else if (cursor == kBackRowIndex) {
                    break;
                }
            }
        }

        if (bg_changed) {
            playMenuCursorSe(config.audio);
            drawBgGalleryScreen(config.lcd, state.bg_index);
        } else if (state.viewing_about) {
            // About 画面は入場時に全面描画済み。FAR で戻るまで部分更新しない。
        } else if (!state.viewing_bg && (brightness_changed || ui_changed)) {
            drawSettingsRow(config.lcd, state, kBrightnessRowIndex, cursor);
        } else if (!state.viewing_bg && battery_led_changed) {
            drawSettingsRow(config.lcd, state, kBatteryLedRowIndex, cursor);
        } else if (!state.viewing_bg && volume_changed) {
            drawSettingsRow(config.lcd, state, kVolumeRowIndex, cursor);
        } else if (!state.viewing_bg && cursor_changed) {
            playMenuCursorSe(config.audio);
            updateSettingsCursor(config.lcd, cache, state, old_cursor, cursor);
        }
        sleep_ms(config.frame_interval_ms);
    }
    DeviceSettings::flushPending();
    waitForButtonRelease(config.buttons);
}
