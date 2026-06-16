// ============================================
// ファイル: game_select_menu.cpp
// ============================================

#include "game_select_menu.hpp"

#include <cstdio>
#include <cstring>

#include "button_input.hpp"
#include "config.hpp"
#include "game_catalog.hpp"
#include "pico/stdlib.h"
#include "sd_service.hpp"
#include "st7789_lcd.hpp"
#include "system_settings_menu.hpp"

namespace {

constexpr int kMaxVisibleRows = 10;
constexpr int kMenuListFirstRowY = 26;
constexpr int kMenuRowPitch = 18;
constexpr int kMenuRowBgH = 8;
constexpr int kMenuListX = 10;
constexpr int kMenuListW = 180;
constexpr int kMenuListTitleChars = 21;
constexpr uint16_t kMenuBg = Color::rgb(8, 24, 45);
constexpr uint16_t kMenuChromeBg = Color::rgb(37, 61, 87);
constexpr uint16_t kMenuDividerColor = Color::rgb(0, 0, 0);
constexpr uint16_t kMenuSelBg = Color::rgb(20, 70, 120);
constexpr uint16_t kMenuRowBg = Color::rgb(8, 24, 45);
constexpr int kMenuTopChromeH = 24;
constexpr int kMenuBottomChromeH = 22;
constexpr int kMenuDividerYTop = kMenuTopChromeH;
constexpr int kMenuContentY0 = kMenuDividerYTop + 1;
constexpr int kMenuDividerYBottom = GameConfig::SCREEN_HEIGHT - kMenuBottomChromeH - 1;
constexpr int kMenuContentY1 = kMenuDividerYBottom - 1;
constexpr int kMenuBottomChromeY = kMenuDividerYBottom + 1;
constexpr int kMenuContentH = kMenuContentY1 - kMenuContentY0 + 1;
constexpr int kRightPanelX = 198;
constexpr int kRightPanelY = kMenuContentY0 + 1;
constexpr int kMenuListDividerX = kRightPanelX - 1;
constexpr int kRightPanelInnerW = 110;
constexpr int kRightMetaX = 200;
constexpr int kRightTitleY = 144;
constexpr int kRightSizeY = 160;
constexpr int kRightTitleChars = 14;
constexpr int kMenuHeaderTitleY = 10;
constexpr int kMenuFooterHintY = 226;
constexpr uint32_t kTitleScrollHoldMs = 600;
constexpr uint32_t kTitleScrollStepMs = 180;
constexpr uint32_t kTitleScrollLoopGapMs = 500;

struct MenuState {
    GameCatalogEntry games[GameCatalog::kMaxEntries];
    int count = 0;
    int selected = 0;
    int loaded_preview_index = -1;
    bool preview_loaded = false;
    uint16_t preview_pixels[GameCatalog::kPreviewW * GameCatalog::kPreviewH];
};

struct MenuUiCache {
    bool ready = false;
    int prev_selected = -1;
    char prev_scroll_title[48] = {};
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

void drawTextCenteredInRect(ST7789_LCD* lcd, int rect_x, int rect_y, int rect_w, int rect_h,
                            const char* text, uint16_t fg, uint16_t bg) {
    if (!lcd || !text) {
        return;
    }
    constexpr int kFontH = 8;
    const int text_w = textWidthPx(text);
    const int x = rect_x + (rect_w - text_w) / 2;
    const int y = rect_y + (rect_h - kFontH) / 2;
    lcd->drawTextBg(x < rect_x ? rect_x : x, y, text, fg, bg);
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

void loadGameMenuEntries(MenuState& state, const char* games_dir) {
    state.count = 0;
    state.selected = 0;
    state.loaded_preview_index = -1;
    state.preview_loaded = false;
    state.count =
        GameCatalog::loadEntries(games_dir, state.games, GameCatalog::kMaxEntries);
}

bool loadPreviewForSelected(MenuState& state) {
    if (state.selected < 0 || state.selected >= state.count) {
        state.preview_loaded = false;
        state.loaded_preview_index = -1;
        return false;
    }
    if (state.loaded_preview_index == state.selected) {
        return state.preview_loaded;
    }
    state.loaded_preview_index = state.selected;
    state.preview_loaded = false;

    const GameCatalogEntry& e = state.games[state.selected];
    if (e.preview_path[0] == '\0') {
        return false;
    }
    state.preview_loaded = GameCatalog::loadPreviewRgb565(
        e.preview_path, state.preview_pixels,
        static_cast<size_t>(GameCatalog::kPreviewW) * static_cast<size_t>(GameCatalog::kPreviewH));
    return state.preview_loaded;
}

void buildTruncatedTitle(const char* title, char* out, size_t out_len, int max_chars) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!title || max_chars <= 0) {
        return;
    }
    const int len = static_cast<int>(std::strlen(title));
    if (len <= max_chars) {
        std::snprintf(out, out_len, "%s", title);
        return;
    }
    std::snprintf(out, out_len, "%.*s", max_chars, title);
}

void buildScrollingTitle(const char* title, char* out, size_t out_len, int max_chars, bool scrolling) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!title || max_chars <= 0) {
        return;
    }

    const int len = static_cast<int>(std::strlen(title));
    if (len <= max_chars) {
        std::snprintf(out, out_len, "%s", title);
        return;
    }

    if (!scrolling) {
        std::snprintf(out, out_len, "%.*s", max_chars, title);
        return;
    }

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const int hidden = len - max_chars;
    const uint32_t cycle = kTitleScrollHoldMs + static_cast<uint32_t>(hidden) * kTitleScrollStepMs +
                           kTitleScrollLoopGapMs;
    const uint32_t phase = cycle > 0 ? now % cycle : 0;

    int offset = 0;
    if (phase > kTitleScrollHoldMs) {
        const uint32_t travel = phase - kTitleScrollHoldMs;
        offset = static_cast<int>(travel / kTitleScrollStepMs);
        if (offset > hidden) {
            offset = hidden;
        }
    }
    std::snprintf(out, out_len, "%.*s", max_chars, title + offset);
}

void drawEmptyGamesScreen(ST7789_LCD* lcd) {
    if (!lcd) {
        return;
    }
    lcd->fill(Color::BLACK);
    lcd->drawTextBg(10, 100, "No game in /games", Color::YELLOW, Color::BLACK);
    lcd->drawTextBg(10, 116, "Press LEFT for settings", Color::WHITE, Color::BLACK);
    lcd->drawTextBg(10, 132, "Press A to refresh", Color::GRAY, Color::BLACK);
}

void drawMenuListRow(ST7789_LCD* lcd, const MenuState& state, int index) {
    if (!lcd || index < 0 || index >= state.count || index >= kMaxVisibleRows) {
        return;
    }
    const int row_top = kMenuListFirstRowY + index * kMenuRowPitch;
    const int bg_y = row_top + (kMenuRowPitch - kMenuRowBgH) / 2;
    char title_part[40];
    char line[48];
    const int number = index + 1;
    char num_prefix[8];
    const int prefix_len = std::snprintf(num_prefix, sizeof(num_prefix), "%d.", number);
    const int max_title_chars =
        kMenuListTitleChars - prefix_len > 0 ? kMenuListTitleChars - prefix_len : 1;
    buildTruncatedTitle(state.games[index].title, title_part, sizeof(title_part), max_title_chars);
    std::snprintf(line, sizeof(line), "%d.%s", number, title_part);
    const bool selected = (index == state.selected);
    const uint16_t fg = selected ? Color::rgb(255, 180, 70) : Color::WHITE;
    const uint16_t bg = selected ? kMenuSelBg : kMenuRowBg;
    lcd->fillRect(kMenuListX, bg_y, kMenuListW, kMenuRowBgH, bg);
    lcd->drawTextBg(kMenuListX + 2, bg_y, selected ? ">" : " ", fg, bg);
    lcd->drawTextBg(kMenuListX + 12, bg_y, line, fg, bg);
}

void drawMenuStaticChrome(ST7789_LCD* lcd) {
    if (!lcd) {
        return;
    }
    lcd->fillRect(0, 0, GameConfig::SCREEN_WIDTH, kMenuTopChromeH, kMenuChromeBg);
    lcd->fillRect(0, kMenuContentY0, GameConfig::SCREEN_WIDTH, kMenuContentH, kMenuBg);
    lcd->fillRect(0, kMenuBottomChromeY, GameConfig::SCREEN_WIDTH, kMenuBottomChromeH, kMenuChromeBg);

    lcd->fillRect(0, kMenuDividerYTop, GameConfig::SCREEN_WIDTH, 1, kMenuDividerColor);
    lcd->fillRect(0, kMenuDividerYBottom, GameConfig::SCREEN_WIDTH, 1, kMenuDividerColor);
    lcd->fillRect(kMenuListDividerX, kMenuContentY0, 1, kMenuContentH, kMenuDividerColor);

    drawTextCenteredBg(lcd, kMenuHeaderTitleY, "GAME SELECT MENU", Color::WHITE, kMenuChromeBg);
    drawTextCenteredBg(lcd, kMenuFooterHintY, "[NEAR] Launch  [LEFT] Settings", Color::YELLOW,
                       kMenuChromeBg);
    lcd->drawRect(kRightPanelX, kRightPanelY, kRightPanelInnerW + 2, kRightPanelInnerW + 2,
                  Color::WHITE);
    lcd->fillRect(kRightMetaX, kRightTitleY, kRightTitleChars * 8, 24, kMenuBg);
}

void drawRightPreviewPanel(ST7789_LCD* lcd, MenuState& state) {
    if (!lcd) {
        return;
    }
    loadPreviewForSelected(state);
    lcd->fillRect(kRightPanelX + 1, kRightPanelY + 1, kRightPanelInnerW, kRightPanelInnerW,
                  Color::rgb(20, 20, 40));
    if (state.preview_loaded) {
        lcd->drawRawImage(static_cast<uint16_t>(kRightPanelX + 6),
                          static_cast<uint16_t>(kRightPanelY + 6), GameCatalog::kPreviewW,
                          GameCatalog::kPreviewH, state.preview_pixels);
    } else {
        static const char kNoImage[] = "NO IMAGE";
        const uint16_t preview_bg = Color::rgb(20, 20, 40);
        drawTextCenteredInRect(lcd, kRightPanelX + 1, kRightPanelY + 1, kRightPanelInnerW,
                               kRightPanelInnerW, kNoImage, Color::GRAY, preview_bg);
    }
}

void drawRightSizeLine(ST7789_LCD* lcd, const MenuState& state) {
    if (!lcd || state.selected < 0 || state.selected >= state.count) {
        return;
    }
    const GameCatalogEntry& e = state.games[state.selected];
    char line2[48];
    const uint32_t kb = (e.script_size + 1023) / 1024;
    std::snprintf(line2, sizeof(line2), "Size : %lu KB", static_cast<unsigned long>(kb));
    lcd->fillRect(kRightMetaX, kRightSizeY, kRightTitleChars * 8, 10, kMenuBg);
    lcd->drawTextBg(kRightMetaX, kRightSizeY, line2, Color::WHITE, kMenuBg);
}

bool drawRightTitleScroll(ST7789_LCD* lcd, MenuState& state, MenuUiCache& cache) {
    if (!lcd || state.selected < 0 || state.selected >= state.count) {
        return false;
    }
    char title_view[48];
    buildScrollingTitle(state.games[state.selected].title, title_view, sizeof(title_view),
                        kRightTitleChars, true);
    if (cache.ready && std::strcmp(cache.prev_scroll_title, title_view) == 0) {
        return false;
    }
    lcd->fillRect(kRightMetaX, kRightTitleY, kRightTitleChars * 8, 10, kMenuBg);
    lcd->drawTextBg(kRightMetaX, kRightTitleY, title_view, Color::WHITE, kMenuBg);
    std::strncpy(cache.prev_scroll_title, title_view, sizeof(cache.prev_scroll_title) - 1);
    cache.prev_scroll_title[sizeof(cache.prev_scroll_title) - 1] = '\0';
    return true;
}

void initGameSelectMenu(ST7789_LCD* lcd, MenuState& state, MenuUiCache& cache) {
    cache.ready = false;
    cache.prev_selected = -1;
    cache.prev_scroll_title[0] = '\0';
    drawMenuStaticChrome(lcd);
    for (int i = 0; i < state.count && i < kMaxVisibleRows; i++) {
        drawMenuListRow(lcd, state, i);
    }
    drawRightPreviewPanel(lcd, state);
    drawRightTitleScroll(lcd, state, cache);
    drawRightSizeLine(lcd, state);
    cache.ready = true;
    cache.prev_selected = state.selected;
}

void updateGameSelectSelection(ST7789_LCD* lcd, MenuState& state, MenuUiCache& cache,
                               int old_selected) {
    if (old_selected >= 0 && old_selected < state.count) {
        drawMenuListRow(lcd, state, old_selected);
    }
    drawMenuListRow(lcd, state, state.selected);
    state.loaded_preview_index = -1;
    state.preview_loaded = false;
    drawRightPreviewPanel(lcd, state);
    cache.prev_scroll_title[0] = '\0';
    drawRightTitleScroll(lcd, state, cache);
    drawRightSizeLine(lcd, state);
    cache.prev_selected = state.selected;
}

void runSettingsFromMenu(const GameSelectMenu::Config& config) {
    SystemSettingsMenu::Config settings = {};
    settings.lcd = config.lcd;
    settings.buttons = config.buttons;
    settings.on_frame = config.on_frame;
    settings.on_run_input_test = config.on_run_input_test;
    settings.user_data = config.user_data;
    settings.frame_interval_ms = config.frame_interval_ms;
    SystemSettingsMenu::run(settings);
}

}  // namespace

bool GameSelectMenu::run(const Config& config) {
    if (!config.lcd || !config.buttons || !SdService::isMounted()) {
        return false;
    }

    const char* games_dir =
        (config.games_dir && config.games_dir[0] != '\0') ? config.games_dir : GameConfig::GAMES_DIR;

    int resume_selected = 0;
    waitForButtonRelease(config.buttons);

    while (SdService::isMounted() && SdService::isCardPresent()) {
        char pending_launch_path[96] = {};
        bool pending_launch = false;

        {
            MenuState state = {};
            MenuUiCache ui_cache = {};
            loadGameMenuEntries(state, games_dir);
            if (resume_selected > 0 && resume_selected < state.count) {
                state.selected = resume_selected;
            } else if (state.selected >= state.count && state.count > 0) {
                state.selected = state.count - 1;
            }

            if (state.count <= 0) {
                drawEmptyGamesScreen(config.lcd);
            } else {
                initGameSelectMenu(config.lcd, state, ui_cache);
            }

            uint32_t last_scroll_anim_ms = 0;
            while (SdService::isMounted() && SdService::isCardPresent() && !pending_launch) {
                config.buttons->update();
                if (config.on_frame) {
                    config.on_frame(config.user_data);
                }

                const uint32_t now = to_ms_since_boot(get_absolute_time());
                if (state.count > 0 && ui_cache.ready &&
                    now - last_scroll_anim_ms >= kTitleScrollStepMs) {
                    drawRightTitleScroll(config.lcd, state, ui_cache);
                    last_scroll_anim_ms = now;
                }

                bool need_full_redraw = false;
                const int old_selected = state.selected;
                if (config.buttons->wasPressed(Button::LEFT)) {
                    runSettingsFromMenu(config);
                    need_full_redraw = true;
                }
                if (config.buttons->wasPressed(Button::UP) && state.selected > 0) {
                    state.selected--;
                } else if (config.buttons->wasPressed(Button::DOWN) &&
                           state.selected + 1 < state.count) {
                    state.selected++;
                }
                const bool selection_changed =
                    state.count > 0 && ui_cache.ready && state.selected != ui_cache.prev_selected;
                if (selection_changed) {
                    updateGameSelectSelection(config.lcd, state, ui_cache, old_selected);
                }
                if (config.buttons->wasPressed(Button::NEAR) ||
                    config.buttons->wasPressed(Button::OP_RIGHT)) {
                    if (state.count <= 0) {
                        loadGameMenuEntries(state, games_dir);
                        need_full_redraw = true;
                    } else if (config.on_run_game) {
                        std::snprintf(pending_launch_path, sizeof(pending_launch_path), "%s",
                                      state.games[state.selected].script_path);
                        resume_selected = state.selected;
                        pending_launch = true;
                    }
                }
                if (need_full_redraw) {
                    if (state.count <= 0) {
                        drawEmptyGamesScreen(config.lcd);
                        ui_cache.ready = false;
                    } else {
                        initGameSelectMenu(config.lcd, state, ui_cache);
                    }
                }
                sleep_ms(config.frame_interval_ms);
            }
        }

        if (!SdService::isMounted() || !SdService::isCardPresent()) {
            break;
        }
        if (pending_launch && config.on_run_game) {
            config.on_run_game(pending_launch_path, config.user_data);
        }
    }
    return true;
}
