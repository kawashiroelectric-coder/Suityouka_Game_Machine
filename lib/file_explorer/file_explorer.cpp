// ============================================
// ファイル: file_explorer.cpp
// SD カード上の GUI ファイルエクスプローラ（3×2 グリッド）
// ============================================

#include "file_explorer.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "assets/file.h"
#include "assets/folder.h"
#include "assets/game.h"
#include "button_input.hpp"
#include "config.hpp"
#include "encoder_volume.hpp"
#include "pico/stdlib.h"
#include "st7789_lcd.hpp"

extern "C" {
#include "ff.h"
}

namespace {

constexpr int kMaxEntries = 48;
constexpr int kGridCols = 3;
constexpr int kGridRows = 2;
constexpr int kPageSize = kGridCols * kGridRows;
constexpr int kCellW = GameConfig::SCREEN_WIDTH / kGridCols;
constexpr int kCellH = 98;
constexpr int kGridTopY = 30;
constexpr int kPathY = 16;
constexpr int kTitleY = 4;
constexpr int kFooterY = 228;
constexpr int kToastY = 214;
constexpr int kLabelMaxChars = 12;
constexpr uint32_t kRepeatDelayMs = 400;
constexpr uint32_t kRepeatIntervalMs = 80;
constexpr uint32_t kToastMs = 1200;
constexpr uint16_t kCellSelBg = Color::rgb(25, 30, 70);
constexpr uint16_t kCellBg = Color::BLACK;

struct Entry {
    char name[FF_LFN_BUF + 1];
    bool is_dir = false;
    uint32_t size = 0;
};

struct IconAsset {
    int w = 0;
    int h = 0;
    const uint16_t* pixels = nullptr;
};

struct ExplorerState {
    char cwd[FF_LFN_BUF + 4] = "/";
    Entry entries[kMaxEntries];
    int entry_count = 0;
    int cursor = 0;
    int scroll_top = 0;
    bool dirty_full = true;
    bool dirty_toast = false;
    char toast[48] = {};
    uint32_t toast_until_ms = 0;
};

struct DrawCache {
    int prev_cursor = -1;
    int prev_scroll_top = -1;
    char prev_toast[48] = {};
    bool grid_ready = false;
};

enum class CursorMoveKind { None, Moved, Scrolled };

bool isDotEntry(const char* name) {
    return name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

bool endsWithLua(const char* name) {
    const size_t len = std::strlen(name);
    if (len < 5) {
        return false;
    }
    const char* ext = name + len - 4;
    return ext[0] == '.' && std::tolower(static_cast<unsigned char>(ext[1])) == 'l' &&
           std::tolower(static_cast<unsigned char>(ext[2])) == 'u' &&
           std::tolower(static_cast<unsigned char>(ext[3])) == 'a';
}

int entrySortRank(const Entry& entry) {
    if (entry.name[0] == '.' && entry.name[1] == '.' && entry.name[2] == '\0') {
        return 0;
    }
    return entry.is_dir ? 1 : 2;
}

int strcasecmpAscii(const char* a, const char* b) {
    while (*a && *b) {
        const int ca = std::tolower(static_cast<unsigned char>(*a));
        const int cb = std::tolower(static_cast<unsigned char>(*b));
        if (ca != cb) {
            return ca - cb;
        }
        ++a;
        ++b;
    }
    return std::tolower(static_cast<unsigned char>(*a)) -
           std::tolower(static_cast<unsigned char>(*b));
}

void sortEntries(Entry* entries, int count) {
    for (int i = 1; i < count; ++i) {
        Entry key = entries[i];
        int j = i - 1;
        while (j >= 0) {
            const int rank_key = entrySortRank(key);
            const int rank_j = entrySortRank(entries[j]);
            if (rank_key < rank_j ||
                (rank_key == rank_j && strcasecmpAscii(key.name, entries[j].name) < 0)) {
                entries[j + 1] = entries[j];
                --j;
            } else {
                break;
            }
        }
        entries[j + 1] = key;
    }
}

void setToast(ExplorerState& state, const char* message) {
    if (!message) {
        state.toast[0] = '\0';
        state.toast_until_ms = 0;
        state.dirty_toast = true;
        return;
    }
    std::strncpy(state.toast, message, sizeof(state.toast) - 1);
    state.toast[sizeof(state.toast) - 1] = '\0';
    state.toast_until_ms = to_ms_since_boot(get_absolute_time()) + kToastMs;
    state.dirty_toast = true;
}

void pathParent(const char* path, char* out, size_t out_len) {
    if (!path || !out || out_len == 0) {
        return;
    }
    if (std::strcmp(path, "/") == 0) {
        std::strncpy(out, "/", out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }

    std::strncpy(out, path, out_len - 1);
    out[out_len - 1] = '\0';
    char* slash = std::strrchr(out, '/');
    if (!slash) {
        std::strncpy(out, "/", out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }
    if (slash == out) {
        out[1] = '\0';
    } else {
        *slash = '\0';
    }
}

bool pathJoin(const char* base, const char* name, char* out, size_t out_len) {
    if (!base || !name || !out || out_len == 0) {
        return false;
    }
    if (std::strcmp(base, "/") == 0) {
        return std::snprintf(out, out_len, "/%s", name) > 0;
    }
    return std::snprintf(out, out_len, "%s/%s", base, name) > 0;
}

bool loadDirectory(ExplorerState& state) {
    state.entry_count = 0;
    state.cursor = 0;
    state.scroll_top = 0;

    if (std::strcmp(state.cwd, "/") != 0) {
        std::strncpy(state.entries[0].name, "..", sizeof(state.entries[0].name) - 1);
        state.entries[0].name[sizeof(state.entries[0].name) - 1] = '\0';
        state.entries[0].is_dir = true;
        state.entries[0].size = 0;
        state.entry_count = 1;
    }

    DIR dir;
    FILINFO fno;
    const FRESULT fr = f_opendir(&dir, state.cwd);
    if (fr != FR_OK) {
        printf("FileExplorer: opendir failed %s (%d)\n", state.cwd, fr);
        setToast(state, "Open dir failed");
        return false;
    }

    while (state.entry_count < kMaxEntries) {
        const FRESULT rd = f_readdir(&dir, &fno);
        if (rd != FR_OK || fno.fname[0] == 0) {
            break;
        }
        if (isDotEntry(fno.fname)) {
            continue;
        }

        Entry& entry = state.entries[state.entry_count];
        std::strncpy(entry.name, fno.fname, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.is_dir = (fno.fattrib & AM_DIR) != 0;
        entry.size = entry.is_dir ? 0u : static_cast<uint32_t>(fno.fsize);
        ++state.entry_count;
    }
    f_closedir(&dir);

    if (state.entry_count > 1 || (state.entry_count == 1 && state.entries[0].name[0] != '.')) {
        const int start = (std::strcmp(state.cwd, "/") == 0) ? 0 : 1;
        if (state.entry_count > start) {
            sortEntries(state.entries + start, state.entry_count - start);
        }
    }

    if (state.entry_count == 0) {
        setToast(state, "Empty folder");
    }
    state.dirty_full = true;
    return true;
}

void clampScroll(ExplorerState& state) {
    if (state.entry_count <= 0) {
        state.cursor = 0;
        state.scroll_top = 0;
        return;
    }
    if (state.cursor < 0) {
        state.cursor = 0;
    }
    if (state.cursor >= state.entry_count) {
        state.cursor = state.entry_count - 1;
    }
    state.scroll_top = (state.cursor / kPageSize) * kPageSize;
}

void truncateForDisplay(const char* text, char* out, size_t out_len, int max_chars) {
    if (!text || !out || out_len == 0 || max_chars <= 0) {
        if (out && out_len > 0) {
            out[0] = '\0';
        }
        return;
    }
    const size_t len = std::strlen(text);
    if (static_cast<int>(len) <= max_chars) {
        std::strncpy(out, text, out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }
    if (out_len < 5) {
        out[0] = '\0';
        return;
    }
    std::snprintf(out, out_len, "...%s", text + len - static_cast<size_t>(max_chars - 3));
}

IconAsset iconForEntry(const Entry& entry) {
    if (entry.is_dir) {
        return {folder_width, folder_height, folder_pixels};
    }
    if (endsWithLua(entry.name)) {
        return {game_width, game_height, game_pixels};
    }
    return {file_width, file_height, file_pixels};
}

int cellSlotForIndex(int scroll_top, int entry_index) {
    if (entry_index < scroll_top || entry_index >= scroll_top + kPageSize) {
        return -1;
    }
    return entry_index - scroll_top;
}

void drawGridCell(ST7789_LCD* lcd, const ExplorerState& state, int slot) {
    if (!lcd || slot < 0 || slot >= kPageSize) {
        return;
    }

    const int col = slot % kGridCols;
    const int row = slot / kGridCols;
    const int cell_x = col * kCellW;
    const int cell_y = kGridTopY + row * kCellH;
    const int entry_index = state.scroll_top + slot;
    const bool selected = entry_index == state.cursor;
    const uint16_t bg = selected ? kCellSelBg : kCellBg;

    lcd->fillRect(static_cast<uint16_t>(cell_x), static_cast<uint16_t>(cell_y),
                  static_cast<uint16_t>(kCellW), static_cast<uint16_t>(kCellH), bg);
    if (selected) {
        lcd->drawRect(static_cast<uint16_t>(cell_x), static_cast<uint16_t>(cell_y),
                      static_cast<uint16_t>(kCellW - 1), static_cast<uint16_t>(kCellH - 1),
                      Color::YELLOW);
    }

    if (entry_index < 0 || entry_index >= state.entry_count) {
        return;
    }

    const Entry& entry = state.entries[entry_index];
    const IconAsset icon = iconForEntry(entry);
    if (icon.pixels && icon.w > 0 && icon.h > 0) {
        const int ix = cell_x + (kCellW - icon.w) / 2;
        const int iy = cell_y + 6;
        lcd->drawRawImage(static_cast<uint16_t>(ix), static_cast<uint16_t>(iy),
                          static_cast<uint16_t>(icon.w), static_cast<uint16_t>(icon.h),
                          icon.pixels);
    }

    char label[16];
    truncateForDisplay(entry.name, label, sizeof(label), kLabelMaxChars);
    const int text_w = static_cast<int>(std::strlen(label)) * 8;
    int label_x = cell_x + (kCellW - text_w) / 2;
    if (label_x < cell_x + 2) {
        label_x = cell_x + 2;
    }
    const int label_y = cell_y + kCellH - 12;
    lcd->drawTextBg(static_cast<uint16_t>(label_x), static_cast<uint16_t>(label_y), label,
                    Color::WHITE, bg);
}

void drawGridPage(ST7789_LCD* lcd, const ExplorerState& state) {
    for (int slot = 0; slot < kPageSize; ++slot) {
        drawGridCell(lcd, state, slot);
    }
}

void drawFooter(ST7789_LCD* lcd, const ExplorerState& state) {
    if (!lcd) {
        return;
    }
    char footer[48];
    if (state.entry_count > 0) {
        const int page = state.scroll_top / kPageSize + 1;
        const int page_count = (state.entry_count + kPageSize - 1) / kPageSize;
        std::snprintf(footer, sizeof(footer), "Pg %d/%d  Cross:Move A-R:Open A-L:Up", page,
                      page_count);
    } else {
        std::snprintf(footer, sizeof(footer), "Cross:Move  A-R:Open  A-L:Up");
    }
    lcd->fillRect(0, static_cast<uint16_t>(kFooterY - 1), GameConfig::SCREEN_WIDTH, 12,
                  Color::BLACK);
    lcd->drawTextBg(4, kFooterY, footer, Color::GRAY, Color::BLACK);
}

void drawToastLine(ST7789_LCD* lcd, ExplorerState& state, DrawCache& cache) {
    if (!lcd) {
        return;
    }

    constexpr int kToastClearW = 312;
    constexpr int kToastClearH = 10;

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const bool toast_active = state.toast[0] != '\0' && now < state.toast_until_ms;
    const char* toast_text = toast_active ? state.toast : "";

    if (std::strcmp(cache.prev_toast, toast_text) == 0) {
        return;
    }

    lcd->fillRect(4, static_cast<uint16_t>(kToastY), kToastClearW, kToastClearH, Color::BLACK);
    if (toast_active) {
        lcd->drawTextBg(4, kToastY, state.toast, Color::ORANGE, Color::BLACK);
    } else {
        state.toast[0] = '\0';
    }

    std::strncpy(cache.prev_toast, toast_text, sizeof(cache.prev_toast) - 1);
    cache.prev_toast[sizeof(cache.prev_toast) - 1] = '\0';
}

void drawExplorerFull(ST7789_LCD* lcd, ExplorerState& state, DrawCache& cache) {
    if (!lcd) {
        return;
    }

    lcd->fill(Color::BLACK);
    lcd->drawTextBg(4, kTitleY, "FILE EXPLORER", Color::WHITE, Color::BLACK);

    char path_line[40];
    truncateForDisplay(state.cwd, path_line, sizeof(path_line), 38);
    lcd->drawTextBg(4, kPathY, path_line, Color::CYAN, Color::BLACK);

    if (state.entry_count == 0) {
        lcd->drawTextBg(80, kGridTopY + 40, "(empty)", Color::GRAY, Color::BLACK);
    } else {
        drawGridPage(lcd, state);
    }

    drawFooter(lcd, state);

    cache.prev_cursor = state.cursor;
    cache.prev_scroll_top = state.scroll_top;
    cache.grid_ready = state.entry_count > 0;
    cache.prev_toast[0] = '\0';
    state.dirty_toast = true;
}

void drawExplorerCursorMove(ST7789_LCD* lcd, ExplorerState& state, DrawCache& cache) {
    if (!lcd || !cache.grid_ready || state.entry_count <= 0) {
        return;
    }

    if (cache.prev_scroll_top != state.scroll_top) {
        drawGridPage(lcd, state);
        drawFooter(lcd, state);
    } else if (cache.prev_cursor != state.cursor) {
        const int prev_slot = cellSlotForIndex(cache.prev_scroll_top, cache.prev_cursor);
        const int new_slot = cellSlotForIndex(state.scroll_top, state.cursor);
        if (prev_slot >= 0) {
            drawGridCell(lcd, state, prev_slot);
        }
        if (new_slot >= 0) {
            drawGridCell(lcd, state, new_slot);
        }
    }

    cache.prev_cursor = state.cursor;
    cache.prev_scroll_top = state.scroll_top;
}

void refreshExplorer(ST7789_LCD* lcd, ExplorerState& state, DrawCache& cache) {
    if (state.dirty_full) {
        drawExplorerFull(lcd, state, cache);
        state.dirty_full = false;
    }

    if (state.dirty_toast) {
        drawToastLine(lcd, state, cache);
        state.dirty_toast = false;
    }
}

CursorMoveKind moveCursor(ExplorerState& state, DrawCache& cache, int delta) {
    if (state.entry_count <= 0 || delta == 0) {
        return CursorMoveKind::None;
    }
    const int next = state.cursor + delta;
    if (next < 0 || next >= state.entry_count) {
        return CursorMoveKind::None;
    }

    cache.prev_cursor = state.cursor;
    cache.prev_scroll_top = state.scroll_top;
    state.cursor = next;
    clampScroll(state);

    if (cache.prev_scroll_top != state.scroll_top) {
        return CursorMoveKind::Scrolled;
    }
    return CursorMoveKind::Moved;
}

bool goParent(ExplorerState& state) {
    char parent[FF_LFN_BUF + 4];
    pathParent(state.cwd, parent, sizeof(parent));
    if (std::strcmp(parent, state.cwd) == 0) {
        setToast(state, "Root directory");
        return false;
    }
    std::strncpy(state.cwd, parent, sizeof(state.cwd) - 1);
    state.cwd[sizeof(state.cwd) - 1] = '\0';
    loadDirectory(state);
    return true;
}

bool activateEntry(ExplorerState& state, const FileExplorer::Config& config) {
    if (state.entry_count <= 0 || state.cursor < 0 || state.cursor >= state.entry_count) {
        return false;
    }
    const Entry& entry = state.entries[state.cursor];

    if (entry.is_dir) {
        if (entry.name[0] == '.' && entry.name[1] == '.' && entry.name[2] == '\0') {
            return goParent(state);
        }
        char next[FF_LFN_BUF + 4];
        if (!pathJoin(state.cwd, entry.name, next, sizeof(next))) {
            setToast(state, "Path too long");
            return false;
        }
        std::strncpy(state.cwd, next, sizeof(state.cwd) - 1);
        state.cwd[sizeof(state.cwd) - 1] = '\0';
        loadDirectory(state);
        return false;
    }

    if (!endsWithLua(entry.name)) {
        setToast(state, "Lua files only");
        return false;
    }

    char script_path[FF_LFN_BUF + 4];
    if (!pathJoin(state.cwd, entry.name, script_path, sizeof(script_path))) {
        setToast(state, "Path too long");
        return false;
    }

    if (config.on_run_lua) {
        config.on_run_lua(script_path, config.user_data);
    }
    loadDirectory(state);
    return false;
}

bool handleRepeatPress(ButtonInput* buttons, Button button, uint32_t& held_since,
                       uint32_t& last_repeat) {
    if (!buttons) {
        return false;
    }
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (buttons->wasPressed(button)) {
        held_since = now;
        last_repeat = now;
        return true;
    }
    if (!buttons->isPressed(button)) {
        held_since = 0;
        return false;
    }
    if (held_since == 0) {
        return false;
    }
    if (now - held_since < kRepeatDelayMs) {
        return false;
    }
    if (now - last_repeat < kRepeatIntervalMs) {
        return false;
    }
    last_repeat = now;
    return true;
}

void serviceFrame(const FileExplorer::Config& config) {
    if (config.on_frame) {
        config.on_frame(config.user_data);
    }
}

CursorMoveKind applyGridMove(ExplorerState& state, DrawCache& cache, ButtonInput* buttons,
                             uint32_t& up_held, uint32_t& up_repeat, uint32_t& down_held,
                             uint32_t& down_repeat, uint32_t& left_held, uint32_t& left_repeat,
                             uint32_t& right_held, uint32_t& right_repeat) {
    CursorMoveKind kind = CursorMoveKind::None;

    if (buttons->wasPressed(Button::UP) ||
        handleRepeatPress(buttons, Button::UP, up_held, up_repeat)) {
        const CursorMoveKind k = moveCursor(state, cache, -kGridCols);
        if (k != CursorMoveKind::None) {
            kind = k;
        }
    }
    if (buttons->wasPressed(Button::DOWN) ||
        handleRepeatPress(buttons, Button::DOWN, down_held, down_repeat)) {
        const CursorMoveKind k = moveCursor(state, cache, kGridCols);
        if (k != CursorMoveKind::None) {
            kind = k;
        }
    }
    if (buttons->wasPressed(Button::LEFT) ||
        handleRepeatPress(buttons, Button::LEFT, left_held, left_repeat)) {
        const int col = state.cursor % kGridCols;
        if (col > 0) {
            const CursorMoveKind k = moveCursor(state, cache, -1);
            if (k != CursorMoveKind::None) {
                kind = k;
            }
        }
    }
    if (buttons->wasPressed(Button::RIGHT) ||
        handleRepeatPress(buttons, Button::RIGHT, right_held, right_repeat)) {
        const int col = state.cursor % kGridCols;
        if (col < kGridCols - 1 && state.cursor + 1 < state.entry_count) {
            const CursorMoveKind k = moveCursor(state, cache, +1);
            if (k != CursorMoveKind::None) {
                kind = k;
            }
        }
    }

    return kind;
}

}  // namespace

void FileExplorer::run(const Config& config) {
    if (!config.lcd || !config.buttons) {
        return;
    }

    ExplorerState state;
    DrawCache cache;
    loadDirectory(state);

    uint32_t up_held = 0;
    uint32_t up_repeat = 0;
    uint32_t down_held = 0;
    uint32_t down_repeat = 0;
    uint32_t left_held = 0;
    uint32_t left_repeat = 0;
    uint32_t right_held = 0;
    uint32_t right_repeat = 0;

    while (true) {
        if (config.is_sd_present && !config.is_sd_present(config.user_data)) {
            return;
        }

        config.buttons->update();
        serviceFrame(config);

        const CursorMoveKind move_kind =
            applyGridMove(state, cache, config.buttons, up_held, up_repeat, down_held, down_repeat,
                          left_held, left_repeat, right_held, right_repeat);

        if (config.buttons->wasPressed(Button::OP_RIGHT) ||
            config.buttons->wasPressed(Button::NEAR)) {
            activateEntry(state, config);
        }
        if (config.buttons->wasPressed(Button::OP_LEFT)) {
            goParent(state);
        }

        const uint32_t now = to_ms_since_boot(get_absolute_time());
        if (state.toast[0] != '\0' && now >= state.toast_until_ms) {
            state.dirty_toast = true;
        }

        if (state.dirty_full) {
            refreshExplorer(config.lcd, state, cache);
        } else if (move_kind != CursorMoveKind::None) {
            drawExplorerCursorMove(config.lcd, state, cache);
        }

        if (state.dirty_toast) {
            drawToastLine(config.lcd, state, cache);
            state.dirty_toast = false;
        }

        sleep_ms(config.frame_interval_ms);
    }
}
