// ============================================
// ファイル: debug_overlay.cpp
// Lua ゲーム中の FPS / 動的 RAM デバッグ表示
// ============================================

#include "debug_overlay.hpp"

#include <cstdio>
#include <cstring>

#include "heap_budget.hpp"
#include "st7789_lcd.hpp"

#if defined(GAME_MACHINE_DEBUG)

namespace {

class DebugOverlay {
public:
    void reset() {
        last_ms_ = 0;
        accum_ms_ = 0;
        frames_ = 0;
        displayed_fps_ = 0;
        displayed_ram_kb_ = 0;
        displayed_ram_pct_ = 0;
    }

    void tick(uint32_t now_ms) {
        if (last_ms_ == 0) {
            last_ms_ = now_ms;
            return;
        }
        const uint32_t dt = now_ms - last_ms_;
        last_ms_ = now_ms;
        accum_ms_ += dt;
        frames_++;
        if (accum_ms_ >= 250) {
            displayed_fps_ = static_cast<uint16_t>((frames_ * 1000u + accum_ms_ / 2) / accum_ms_);
            updateRamStats();
            accum_ms_ = 0;
            frames_ = 0;
        }
    }

    void draw(ST7789_LCD* lcd, int screen_width) const {
        if (!lcd || screen_width <= 0) {
            return;
        }

        char buf[24];
        snprintf(buf, sizeof(buf), "FPS:%u", static_cast<unsigned>(displayed_fps_));
        int text_w = static_cast<int>(strlen(buf)) * 8;
        int x = screen_width - text_w;
        lcd->drawTextBg(static_cast<uint16_t>(x), 0, buf, Color::WHITE, Color::BLACK);

        snprintf(buf, sizeof(buf), "RAM:%uK %u%%", static_cast<unsigned>(displayed_ram_kb_),
                 static_cast<unsigned>(displayed_ram_pct_));
        text_w = static_cast<int>(strlen(buf)) * 8;
        x = screen_width - text_w;
        lcd->drawTextBg(static_cast<uint16_t>(x), 8, buf, Color::CYAN, Color::BLACK);
    }

private:
    void updateRamStats() {
        const size_t used = HeapBudget::used();
        const size_t budget = HeapBudget::budget();
        const size_t reserve = HeapBudget::reserve();
        const size_t limit = (budget > reserve) ? (budget - reserve) : 1u;
        displayed_ram_kb_ = static_cast<uint16_t>((used + 512u) / 1024u);
        displayed_ram_pct_ = static_cast<uint16_t>((used * 100u + limit / 2) / limit);
        if (displayed_ram_pct_ > 100u) {
            displayed_ram_pct_ = 100u;
        }
    }

    uint32_t last_ms_ = 0;
    uint32_t accum_ms_ = 0;
    uint32_t frames_ = 0;
    uint16_t displayed_fps_ = 0;
    uint16_t displayed_ram_kb_ = 0;
    uint16_t displayed_ram_pct_ = 0;
};

DebugOverlay g_debug_overlay;

}  // namespace

void debugOverlayReset() { g_debug_overlay.reset(); }

void debugOverlayTick(uint32_t now_ms) { g_debug_overlay.tick(now_ms); }

void debugOverlayDrawAfterFrame(ST7789_LCD* lcd, int screen_width) {
    g_debug_overlay.draw(lcd, screen_width);
}

#else

void debugOverlayReset() {}

void debugOverlayTick(uint32_t) {}

void debugOverlayDrawAfterFrame(ST7789_LCD*, int) {}

#endif  // GAME_MACHINE_DEBUG
