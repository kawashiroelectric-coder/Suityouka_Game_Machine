// ============================================
// ファイル: game_select_menu.cpp
// ============================================

#include "game_select_menu.hpp"

#include <cstdio>
#include <cstring>

#include "button_input.hpp"
#include "config.hpp"
#include "game_catalog.hpp"
#include "heap_budget.hpp"
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
constexpr int kMenuListDividerX = kMenuListX + kMenuListW + 7;
constexpr int kRightPanelContentX = kMenuListDividerX + 1;
constexpr int kRightPanelContentW = GameConfig::SCREEN_WIDTH - kRightPanelContentX;
constexpr int kPreviewImageW = GameCatalog::kPreviewW;
constexpr int kPreviewImageH = GameCatalog::kPreviewH;
constexpr int kRightPanelInnerW = kPreviewImageW;
constexpr int kRightPanelInnerH = kPreviewImageH;
constexpr int kRightPanelFrameW = kPreviewImageW + 2;
constexpr int kRightPanelFrameH = kPreviewImageH + 2;
constexpr int kRightTitleChars = 14;
constexpr int kRightMetaBlockW = kRightTitleChars * 8;
/** プレビュー枠 + タイトル + サイズ行の高さ（右パネル内で縦中央） */
constexpr int kRightBlockH = kRightPanelFrameH + 6 + 16 + 8;
constexpr int kRightBlockOffsetY = (kMenuContentH - kRightBlockH) / 2;
constexpr int kRightPanelY = kMenuContentY0 + kRightBlockOffsetY;
constexpr int kRightPanelX = kRightPanelContentX + (kRightPanelContentW - kRightPanelFrameW) / 2;
constexpr int kPreviewImageX = kRightPanelX + 1;
constexpr int kPreviewImageY = kRightPanelY + 1;
constexpr int kRightMetaX = kRightPanelContentX + (kRightPanelContentW - kRightMetaBlockW) / 2;
constexpr int kRightTitleY = kRightPanelY + kRightPanelFrameH + 6;
constexpr int kRightSizeY = kRightTitleY + 16;
constexpr int kMenuHeaderTitleY = 10;
constexpr int kMenuFooterHintY = 226;
constexpr uint32_t kTitleScrollHoldMs = 600;
constexpr uint32_t kTitleScrollStepMs = 180;
constexpr uint32_t kTitleScrollLoopGapMs = 500;
constexpr uint32_t kSdMountRetryMs = 2000;

/** メニュー表示中だけ HeapBudget から確保するプレビュー RGB565 バッファ */
struct PreviewPixelBuffer {
    uint16_t* pixels = nullptr;
    static constexpr size_t kByteSize = GameCatalog::kPreviewBytes;

    PreviewPixelBuffer() = default;
    /** 確保したプレビューバッファを HeapBudget へ返却する。メニュー終了時やバッファ破棄時に呼ぶ */
    ~PreviewPixelBuffer() { release(); }

    PreviewPixelBuffer(const PreviewPixelBuffer&) = delete;
    PreviewPixelBuffer& operator=(const PreviewPixelBuffer&) = delete;

    /** プレビュー画像用 RGB565 バッファを HeapBudget から確保する。右パネル描画の直前に呼ぶ */
    bool acquire() {
        if (pixels) {
            return true;
        }
        void* ptr = nullptr;
        if (!HeapBudget::tryAlloc(kByteSize, &ptr)) {
            printf("GameSelectMenu: preview buffer alloc failed (%u bytes)\n",
                   static_cast<unsigned>(kByteSize));
            return false;
        }
        pixels = static_cast<uint16_t*>(ptr);
        std::memset(pixels, 0, kByteSize);
        return true;
    }

    /** 確保済みプレビューバッファを解放する。ゲーム起動前やメニュー再入時に呼ぶ */
    void release() {
        if (!pixels) {
            return;
        }
        HeapBudget::release(pixels, kByteSize);
        pixels = nullptr;
    }
};

struct MenuState {
    GameCatalogEntry games[GameCatalog::kMaxEntries];
    int count = 0;
    int selected = 0;
    int loaded_preview_index = -1;
    bool preview_loaded = false;
};

struct MenuUiCache {
    bool ready = false;
    int prev_selected = -1;
    char prev_scroll_title[48] = {};
};

/** 8x8 フォント前提で文字列の描画幅（px）を返す。中央揃え計算時に使う */
int textWidthPx(const char* text) {
    if (!text) {
        return 0;
    }
    return static_cast<int>(std::strlen(text)) * 8;
}

/** 画面幅中央に背景付きテキストを描く。ヘッダー・フッター表示時に使う */
void drawTextCenteredBg(ST7789_LCD* lcd, int y, const char* text, uint16_t fg, uint16_t bg) {
    if (!lcd) {
        return;
    }
    const int x = (GameConfig::SCREEN_WIDTH - textWidthPx(text)) / 2;
    lcd->drawTextBg(x < 0 ? 0 : x, y, text, fg, bg);
}

/** 指定矩形内に背景付きテキストを中央配置する。プレビュー枠内のプレースホルダ表示時に使う */
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

/** 全ボタンが離されるまでブロックする。メニュー入場時やゲーム復帰直後のチャタリング防止に使う */
void waitForButtonRelease(ButtonInput* buttons) {
    if (!buttons) {
        return;
    }
    printf("[MENU-DBG] menu: waitForButtonRelease enter\n");
    fflush(stdout);
    uint32_t waited_ms = 0;
    while (true) {
        buttons->update();
        if (!isAnyButtonPressed(buttons)) {
            printf("[MENU-DBG] menu: waitForButtonRelease done (%lu ms)\n",
                   static_cast<unsigned long>(waited_ms));
            fflush(stdout);
            break;
        }
        if (waited_ms == 0 || (waited_ms % 500) == 0) {
            printf("[MENU-DBG] menu: still waiting button release (%lu ms)\n",
                   static_cast<unsigned long>(waited_ms));
            fflush(stdout);
        }
        sleep_ms(50);
        waited_ms += 50;
    }
}

/** SD 上の games_dir を走査しメニュー用ゲーム一覧を再構築する。マウント直後や更新時に呼ぶ */
void loadGameMenuEntries(MenuState& state, const char* games_dir) {
    state.count = 0;
    state.selected = 0;
    state.loaded_preview_index = -1;
    state.preview_loaded = false;
    state.count =
        GameCatalog::loadEntries(games_dir, state.games, GameCatalog::kMaxEntries);
}

/** 選択中ゲームのプレビュー画像を SD から読み込む。右パネル描画の直前に呼ぶ（キャッシュあり） */
bool loadPreviewForSelected(MenuState& state, PreviewPixelBuffer& preview_buf) {
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
        printf("GameCatalog: no preview for %s\n", e.script_path);
        return false;
    }
    if (!preview_buf.acquire()) {
        return false;
    }
    std::memset(preview_buf.pixels, 0, preview_buf.kByteSize);
    state.preview_loaded = GameCatalog::loadPreviewRgb565(
        e.preview_path, preview_buf.pixels,
        static_cast<size_t>(GameCatalog::kPreviewW) * static_cast<size_t>(GameCatalog::kPreviewH));
    return state.preview_loaded;
}

/** 長いタイトルを最大文字数で切り詰めて出力する。左リスト行の描画時に使う */
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

/** 右パネル用にタイトルを切り詰めまたは横スクロール表示用に整形する。スクロールアニメ時に呼ぶ */
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

/** メニューの固定枠を描く（前方宣言）。drawEmptyGamesScreen から呼ばれる */
void drawMenuStaticChrome(ST7789_LCD* lcd);

/** 左リスト領域に背景付きテキストを描く。空一覧メッセージ等の表示時に使う */
void drawTextInListPanel(ST7789_LCD* lcd, int y, const char* text, uint16_t fg, uint16_t bg) {
    if (!lcd || !text) {
        return;
    }
    lcd->drawTextBg(kMenuListX + 4, y, text, fg, bg);
}

/** ゲーム未検出または SD 未挿入時のメニュー画面を描く。一覧が空のときに呼ぶ */
void drawEmptyGamesScreen(ST7789_LCD* lcd, bool sd_mounted) {
    if (!lcd) {
        return;
    }
    drawMenuStaticChrome(lcd);
    lcd->fillRect(kMenuListX, kMenuListFirstRowY, kMenuListW,
                  kMenuContentY1 - kMenuListFirstRowY + 1, kMenuBg);
    const int msg_y = kMenuListFirstRowY + 24;
    if (!sd_mounted) {
        drawTextInListPanel(lcd, msg_y, "No SD card inserted", Color::YELLOW, kMenuBg);
        drawTextInListPanel(lcd, msg_y + 12, "Insert card to", Color::WHITE, kMenuBg);
        drawTextInListPanel(lcd, msg_y + 24, "load games", Color::WHITE, kMenuBg);
    } else {
        drawTextInListPanel(lcd, msg_y, "No game in /games", Color::YELLOW, kMenuBg);
        drawTextInListPanel(lcd, msg_y + 12, "[NEAR] Refresh", Color::GRAY, kMenuBg);
    }
    lcd->fillRect(kPreviewImageX, kPreviewImageY, kRightPanelInnerW, kRightPanelInnerH,
                  Color::rgb(20, 20, 40));
    drawTextCenteredInRect(lcd, kPreviewImageX, kPreviewImageY, kRightPanelInnerW,
                           kRightPanelInnerH, "---", Color::GRAY, Color::rgb(20, 20, 40));
    lcd->drawRect(static_cast<uint16_t>(kRightPanelX), static_cast<uint16_t>(kRightPanelY),
                  static_cast<uint16_t>(kRightPanelFrameW), static_cast<uint16_t>(kRightPanelFrameH),
                  Color::WHITE);
}

/** SD カードが挿され未マウントならマウントを試みる。挿入検知時や手動更新時に呼ぶ */
bool tryMountSdCard(const GameSelectMenu::Config& config) {
    if (!SdService::isCardPresent() || SdService::isMounted()) {
        return false;
    }
    if (!SdService::mount()) {
        return false;
    }
    if (config.on_sd_state_changed) {
        config.on_sd_state_changed(config.user_data);
    }
    return true;
}

/** SD が抜かれたときマウント済みならアンマウントする。ホットプラグ監視で抜去検知時に呼ぶ */
void unmountSdCardIfNeeded(const GameSelectMenu::Config& config) {
    if (!SdService::isMounted()) {
        return;
    }
    SdService::unmount();
    if (config.on_sd_state_changed) {
        config.on_sd_state_changed(config.user_data);
    }
}

/** メニュー各フレームで SD 挿抜を監視しマウント／アンマウントを処理する。メインループから呼ぶ */
bool serviceSdHotplug(const GameSelectMenu::Config& config, uint32_t now_ms,
                      uint32_t* last_mount_attempt_ms, bool* sd_state_changed) {
    if (!last_mount_attempt_ms || !sd_state_changed) {
        return false;
    }
    *sd_state_changed = false;

    if (!SdService::isCardPresent()) {
        if (SdService::isMounted()) {
            unmountSdCardIfNeeded(config);
            *sd_state_changed = true;
        }
        return *sd_state_changed;
    }

    if (SdService::isMounted()) {
        return false;
    }

    if (now_ms - *last_mount_attempt_ms < kSdMountRetryMs) {
        return false;
    }
    *last_mount_attempt_ms = now_ms;
    if (tryMountSdCard(config)) {
        *sd_state_changed = true;
    }
    return *sd_state_changed;
}

/** ゲーム一覧の 1 行を描画または更新する。初期表示・カーソル移動時に呼ぶ */
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

/** ゲーム描画後の DMA 転送を完了させメニュー直描画の準備をする。メニュー再表示の直前に呼ぶ */
void prepareLcdForMenuDraw(ST7789_LCD* lcd) {
    if (!lcd) {
        return;
    }
    lcd->finishDrawRawImageDMA();
}

/** メニューの固定枠（ヘッダー・区切り・フッター）を描く。初期化や全面再描画時に呼ぶ */
void drawMenuStaticChrome(ST7789_LCD* lcd) {
    if (!lcd) {
        return;
    }
    prepareLcdForMenuDraw(lcd);
    lcd->fillRect(0, 0, GameConfig::SCREEN_WIDTH, kMenuTopChromeH, kMenuChromeBg);
    lcd->fillRect(0, kMenuContentY0, GameConfig::SCREEN_WIDTH, kMenuContentH, kMenuBg);
    lcd->fillRect(0, kMenuBottomChromeY, GameConfig::SCREEN_WIDTH, kMenuBottomChromeH, kMenuChromeBg);

    lcd->fillRect(0, kMenuDividerYTop, GameConfig::SCREEN_WIDTH, 1, kMenuDividerColor);
    lcd->fillRect(0, kMenuDividerYBottom, GameConfig::SCREEN_WIDTH, 1, kMenuDividerColor);
    lcd->fillRect(kMenuListDividerX, kMenuContentY0, 1, kMenuContentH, kMenuDividerColor);

    drawTextCenteredBg(lcd, kMenuHeaderTitleY, "GAME SELECT MENU", Color::WHITE, kMenuChromeBg);
    drawTextCenteredBg(lcd, kMenuFooterHintY, "[NEAR] Launch  [LEFT] Settings", Color::GREEN,
                       kMenuChromeBg);
    lcd->fillRect(kRightMetaX, kRightTitleY, kRightTitleChars * 8, 24, kMenuBg);
}

/** 読み込み済みプレビュー RGB565 を右パネルへ描画する。プレビュー取得成功時に呼ぶ */
void drawPreviewImage(ST7789_LCD* lcd, const uint16_t* pixels) {
    if (!lcd || !pixels) {
        return;
    }
    prepareLcdForMenuDraw(lcd);
    constexpr uint16_t kPreviewBg = Color::rgb(20, 20, 40);
    lcd->fillRect(kPreviewImageX, kPreviewImageY, kPreviewImageW, kPreviewImageH, kPreviewBg);
    lcd->drawRawImage(static_cast<uint16_t>(kPreviewImageX),
                      static_cast<uint16_t>(kPreviewImageY),
                      static_cast<uint16_t>(kPreviewImageW),
                      static_cast<uint16_t>(kPreviewImageH), pixels);
    lcd->drawRect(static_cast<uint16_t>(kRightPanelX), static_cast<uint16_t>(kRightPanelY),
                  static_cast<uint16_t>(kRightPanelFrameW), static_cast<uint16_t>(kRightPanelFrameH),
                  Color::WHITE);
}

/** 右パネルにプレビュー画像または NO IMAGE を描く。選択変更・スクロール更新時に呼ぶ */
void drawRightPreviewPanel(const GameSelectMenu::Config& config, MenuState& state,
                           PreviewPixelBuffer& preview_buf) {
    if (!config.lcd) {
        return;
    }
    ST7789_LCD* lcd = config.lcd;
    loadPreviewForSelected(state, preview_buf);
    if (state.preview_loaded && preview_buf.pixels) {
        drawPreviewImage(lcd, preview_buf.pixels);
    } else {
        constexpr uint16_t kPreviewBg = Color::rgb(20, 20, 40);
        lcd->fillRect(kPreviewImageX, kPreviewImageY, kRightPanelInnerW, kRightPanelInnerH,
                      kPreviewBg);
        static const char kNoImage[] = "NO IMAGE";
        drawTextCenteredInRect(lcd, kPreviewImageX, kPreviewImageY, kRightPanelInnerW,
                               kRightPanelInnerH, kNoImage, Color::GRAY, kPreviewBg);
        lcd->drawRect(static_cast<uint16_t>(kRightPanelX), static_cast<uint16_t>(kRightPanelY),
                      static_cast<uint16_t>(kRightPanelFrameW),
                      static_cast<uint16_t>(kRightPanelFrameH), Color::WHITE);
    }
}

/** 選択中ゲームのスクリプトサイズ行を右パネルに描く。選択変更時に呼ぶ */
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

/** 右パネルのスクロールタイトルを描画する。変化があったとき true を返す。アニメ tick 時に呼ぶ */
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

/** メニュー画面を初回または全面再描画する。一覧読込後・設定復帰後に呼ぶ */
void initGameSelectMenu(const GameSelectMenu::Config& config, MenuState& state, MenuUiCache& cache,
                        PreviewPixelBuffer& preview_buf) {
    ST7789_LCD* lcd = config.lcd;
    if (!lcd) {
        printf("[MENU-DBG] initGameSelectMenu: no lcd\n");
        fflush(stdout);
        return;
    }
    printf("[MENU-DBG] initGameSelectMenu: start count=%d selected=%d\n", state.count,
           state.selected);
    fflush(stdout);
    cache.ready = false;
    cache.prev_selected = -1;
    cache.prev_scroll_title[0] = '\0';
    drawMenuStaticChrome(lcd);
    for (int i = 0; i < state.count && i < kMaxVisibleRows; i++) {
        drawMenuListRow(lcd, state, i);
    }
    drawRightTitleScroll(lcd, state, cache);
    drawRightSizeLine(lcd, state);
    drawRightPreviewPanel(config, state, preview_buf);
    cache.ready = true;
    cache.prev_selected = state.selected;
    printf("[MENU-DBG] initGameSelectMenu: done\n");
    fflush(stdout);
}

/** カーソル移動時に変更行と右パネルのみ部分更新する。UP/DOWN 入力時に呼ぶ */
void updateGameSelectSelection(const GameSelectMenu::Config& config, MenuState& state,
                               MenuUiCache& cache, int old_selected,
                               PreviewPixelBuffer& preview_buf) {
    ST7789_LCD* lcd = config.lcd;
    if (!lcd) {
        return;
    }
    if (old_selected >= 0 && old_selected < state.count) {
        drawMenuListRow(lcd, state, old_selected);
    }
    drawMenuListRow(lcd, state, state.selected);
    state.loaded_preview_index = -1;
    state.preview_loaded = false;
    cache.prev_scroll_title[0] = '\0';
    drawRightTitleScroll(lcd, state, cache);
    drawRightSizeLine(lcd, state);
    drawRightPreviewPanel(config, state, preview_buf);
    cache.prev_selected = state.selected;
}

/** システム設定メニューを起動する。LEFT ボタン押下時に呼ぶ */
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

/** ゲーム選択メニューのメインループ。起動時からゲーム終了まで繰り返し表示する */
bool GameSelectMenu::run(const Config& config) {
    if (!config.lcd || !config.buttons) {
        return false;
    }

    const char* games_dir =
        (config.games_dir && config.games_dir[0] != '\0') ? config.games_dir : GameConfig::GAMES_DIR;

    int resume_selected = 0;
    uint32_t last_mount_attempt_ms = 0;
    waitForButtonRelease(config.buttons);

    while (true) {
        char pending_launch_path[96] = {};
        bool pending_launch = false;
        PreviewPixelBuffer preview_buf;

        printf("[MENU-DBG] menu: outer loop iteration begin\n");
        fflush(stdout);

        const uint32_t session_now = to_ms_since_boot(get_absolute_time());
        last_mount_attempt_ms = session_now - kSdMountRetryMs;
        bool sd_changed = false;
        serviceSdHotplug(config, session_now, &last_mount_attempt_ms, &sd_changed);

        MenuState state = {};
        MenuUiCache ui_cache = {};
        if (SdService::isMounted()) {
            loadGameMenuEntries(state, games_dir);
        }
        if (resume_selected > 0 && resume_selected < state.count) {
            state.selected = resume_selected;
        } else if (state.selected >= state.count && state.count > 0) {
            state.selected = state.count - 1;
        }

        if (state.count <= 0) {
            drawEmptyGamesScreen(config.lcd, SdService::isMounted());
        } else {
            initGameSelectMenu(config, state, ui_cache, preview_buf);
        }

        uint32_t last_scroll_anim_ms = 0;
        while (!pending_launch) {
            config.buttons->update();
            if (config.on_frame) {
                config.on_frame(config.user_data);
            }

            const uint32_t now = to_ms_since_boot(get_absolute_time());
            sd_changed = false;
            if (serviceSdHotplug(config, now, &last_mount_attempt_ms, &sd_changed)) {
                const int prev_selected = state.selected;
                if (SdService::isMounted()) {
                    loadGameMenuEntries(state, games_dir);
                    if (prev_selected > 0 && prev_selected < state.count) {
                        state.selected = prev_selected;
                    } else if (state.selected >= state.count && state.count > 0) {
                        state.selected = state.count - 1;
                    }
                } else {
                    state.count = 0;
                    state.selected = 0;
                    state.loaded_preview_index = -1;
                    state.preview_loaded = false;
                }
                if (state.count <= 0) {
                    drawEmptyGamesScreen(config.lcd, SdService::isMounted());
                    ui_cache.ready = false;
                    ui_cache.prev_selected = -1;
                } else {
                    initGameSelectMenu(config, state, ui_cache, preview_buf);
                }
            }

            if (state.count > 0 && ui_cache.ready &&
                now - last_scroll_anim_ms >= kTitleScrollStepMs) {
                if (drawRightTitleScroll(config.lcd, state, ui_cache)) {
                    drawRightPreviewPanel(config, state, preview_buf);
                }
                last_scroll_anim_ms = now;
            }

            bool need_full_redraw = false;
            const int old_selected = state.selected;
            if (config.buttons->wasPressed(Button::LEFT)) {
                runSettingsFromMenu(config);
                need_full_redraw = true;
            }
            if (state.count > 0) {
                if (config.buttons->wasPressed(Button::UP) && state.selected > 0) {
                    state.selected--;
                } else if (config.buttons->wasPressed(Button::DOWN) &&
                           state.selected + 1 < state.count) {
                    state.selected++;
                }
            }
            const bool selection_changed =
                state.count > 0 && ui_cache.ready && state.selected != ui_cache.prev_selected;
            if (selection_changed) {
                updateGameSelectSelection(config, state, ui_cache, old_selected, preview_buf);
            }
            if (config.buttons->wasPressed(Button::NEAR) ||
                config.buttons->wasPressed(Button::OP_RIGHT)) {
                if (!SdService::isMounted()) {
                    if (tryMountSdCard(config)) {
                        loadGameMenuEntries(state, games_dir);
                        need_full_redraw = true;
                    }
                } else if (state.count <= 0) {
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
                    drawEmptyGamesScreen(config.lcd, SdService::isMounted());
                    ui_cache.ready = false;
                    ui_cache.prev_selected = -1;
                } else {
                    initGameSelectMenu(config, state, ui_cache, preview_buf);
                }
            }
            sleep_ms(config.frame_interval_ms);
        }

        preview_buf.release();
        if (pending_launch && config.on_run_game) {
            printf("[MENU-DBG] menu: calling on_run_game %s\n", pending_launch_path);
            fflush(stdout);
            config.on_run_game(pending_launch_path, config.user_data);
            printf("[MENU-DBG] menu: on_run_game returned\n");
            fflush(stdout);
            waitForButtonRelease(config.buttons);
            prepareLcdForMenuDraw(config.lcd);
            printf("[MENU-DBG] menu: LCD prepared, restarting outer loop\n");
            fflush(stdout);
        }
    }
    return true;
}
