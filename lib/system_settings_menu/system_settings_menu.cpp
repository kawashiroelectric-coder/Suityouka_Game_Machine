// ============================================
// ファイル: system_settings_menu.cpp
// ============================================

#include "system_settings_menu.hpp"

#include <cstdio>
#include <cstring>

#include "button_input.hpp"
#include "config.hpp"
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

struct SettingsUiCache {
    bool ready = false;
    int prev_cursor = -1;
};

int textWidthPx(const char* text) {
    if (!text) {
        return 0;
    }
    return static_cast<int>(std::strlen(text)) * 8;
}

void drawTextCenteredBg(ST7789_LCD* lcd, int y, const char* text, uint16_t fg, uint16_t bg) {
    if (!lcd) {
        return;
    }
    const int x = (GameConfig::SCREEN_WIDTH - textWidthPx(text)) / 2;
    lcd->drawTextBg(x < 0 ? 0 : x, y, text, fg, bg);
}

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

const char* settingsRowLabel(int index) {
    static const char* rows[] = {
        "WiFi: [CONNECTED]",
        "SSID: HOME_NET",
        "Volume: [=====-----] 50%",
        "Brightness: [========--] 80%",
        "Input Test Mode",
        "Back",
    };
    if (index < 0 || index >= kSettingsRowCount) {
        return "";
    }
    return rows[index];
}

void drawSettingsStaticChrome(ST7789_LCD* lcd) {
    if (!lcd) {
        return;
    }
    lcd->fill(kSettingsBg);
    lcd->drawRect(kSettingsPanelX, kSettingsPanelY, kSettingsPanelW, kSettingsPanelH, Color::CYAN);
    drawTextCenteredBg(lcd, kSettingsPanelY - 18, "== SYSTEM MENU ==", Color::CYAN, kSettingsBg);
    drawTextCenteredBg(lcd, 222, "[UP/DOWN] Select  [A] Enter  [LEFT] Back", Color::YELLOW,
                       kSettingsBg);
}

void drawSettingsRow(ST7789_LCD* lcd, int row_index, int cursor) {
    if (!lcd || row_index < 0 || row_index >= kSettingsRowCount) {
        return;
    }
    const int row_top = kSettingsRowFirstY + row_index * kSettingsRowPitch;
    const int bg_y = row_top + (kSettingsRowPitch - kSettingsRowBgH) / 2;
    const bool selected = (row_index == cursor);
    const uint16_t fg = selected ? Color::WHITE : Color::rgb(180, 205, 230);
    const uint16_t bg = selected ? kSettingsSelBg : kSettingsBg;
    lcd->fillRect(kSettingsPanelX + 10, bg_y, kSettingsPanelW - 20, kSettingsRowBgH, bg);
    lcd->drawTextBg(kSettingsPanelX + 12, bg_y, selected ? ">" : " ", fg, bg);
    lcd->drawTextBg(kSettingsPanelX + 26, bg_y, settingsRowLabel(row_index), fg, bg);
}

void initSettingsScreen(ST7789_LCD* lcd, SettingsUiCache& cache, int cursor) {
    cache.ready = false;
    cache.prev_cursor = -1;
    drawSettingsStaticChrome(lcd);
    for (int i = 0; i < kSettingsRowCount; i++) {
        drawSettingsRow(lcd, i, cursor);
    }
    cache.ready = true;
    cache.prev_cursor = cursor;
}

void updateSettingsCursor(ST7789_LCD* lcd, SettingsUiCache& cache, int old_cursor, int new_cursor) {
    if (old_cursor >= 0 && old_cursor < kSettingsRowCount) {
        drawSettingsRow(lcd, old_cursor, new_cursor);
    }
    drawSettingsRow(lcd, new_cursor, new_cursor);
    cache.prev_cursor = new_cursor;
}

}  // namespace

void SystemSettingsMenu::run(const Config& config) {
    if (!config.lcd || !config.buttons) {
        return;
    }

    SettingsUiCache cache = {};
    int cursor = 0;
    initSettingsScreen(config.lcd, cache, cursor);
    waitForButtonRelease(config.buttons);

    while (true) {
        config.buttons->update();
        if (config.on_frame) {
            config.on_frame(config.user_data);
        }

        const int old_cursor = cursor;
        bool cursor_changed = false;
        if (config.buttons->wasPressed(Button::UP) && cursor > 0) {
            cursor--;
            cursor_changed = true;
        }
        if (config.buttons->wasPressed(Button::DOWN) && cursor < kSettingsRowCount - 1) {
            cursor++;
            cursor_changed = true;
        }
        if (config.buttons->wasPressed(Button::LEFT) || config.buttons->wasPressed(Button::OP_LEFT)) {
            break;
        }
        if (config.buttons->wasPressed(Button::NEAR) || config.buttons->wasPressed(Button::OP_RIGHT)) {
            if (cursor == 4 && config.on_run_input_test) {
                config.on_run_input_test(config.user_data);
                initSettingsScreen(config.lcd, cache, cursor);
            } else if (cursor == 5) {
                break;
            }
        }
        if (cursor_changed) {
            updateSettingsCursor(config.lcd, cache, old_cursor, cursor);
        }
        sleep_ms(config.frame_interval_ms);
    }
    waitForButtonRelease(config.buttons);
}
