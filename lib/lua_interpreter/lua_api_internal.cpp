// ============================================
// ファイル: lua_api_internal.cpp
// Lua バインディング共通状態
// ============================================

#include "lua_api_internal.hpp"

#include <cstdio>
#include <cstring>

#include "game_display.hpp"
#include "lua_interpreter.hpp"
#include "pico/stdlib.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

LuaInterpreter* g_active_interpreter = nullptr;

#ifdef GAME_MACHINE_DEBUG
class FpsOverlay {
public:
    void reset() {
        last_ms_ = 0;
        accum_ms_ = 0;
        frames_ = 0;
        displayed_fps_ = 0;
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
            accum_ms_ = 0;
            frames_ = 0;
        }
    }

    void draw(GameDisplay* disp) const {
        if (!disp) {
            return;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "FPS:%u", static_cast<unsigned>(displayed_fps_));
        const int text_w = static_cast<int>(strlen(buf)) * 8;
        const int x = static_cast<int>(disp->width()) - text_w;
        disp->drawTextBg(x, 0, buf, Color::WHITE, Color::BLACK);
    }

private:
    uint32_t last_ms_ = 0;
    uint32_t accum_ms_ = 0;
    uint32_t frames_ = 0;
    uint16_t displayed_fps_ = 0;
};

FpsOverlay g_fps_overlay;
#endif

}  // namespace

LuaInterpreter* luaApiActiveInterpreter() { return g_active_interpreter; }

void luaApiSetActiveInterpreter(LuaInterpreter* interp) { g_active_interpreter = interp; }

GameDisplay* luaApiActiveDisplay() {
    if (!g_active_interpreter) {
        return nullptr;
    }
    return g_active_interpreter->hostHooks().display;
}

uint16_t luaApiParseColor(lua_State* L, int idx) {
    const int n = lua_gettop(L);
    if (n >= idx + 2 && lua_isnumber(L, idx) && lua_isnumber(L, idx + 1) &&
        lua_isnumber(L, idx + 2)) {
        int r = static_cast<int>(luaL_checkinteger(L, idx));
        int g = static_cast<int>(luaL_checkinteger(L, idx + 1));
        int b = static_cast<int>(luaL_checkinteger(L, idx + 2));
        if (r < 0) r = 0;
        if (r > 255) r = 255;
        if (g < 0) g = 0;
        if (g > 255) g = 255;
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        return GameDisplay::rgb(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                static_cast<uint8_t>(b));
    }
    return static_cast<uint16_t>(luaL_checkinteger(L, idx));
}

#ifdef GAME_MACHINE_DEBUG
void luaApiFpsOverlayReset() { g_fps_overlay.reset(); }

void luaApiFpsOverlayTick(uint32_t now_ms) { g_fps_overlay.tick(now_ms); }

void luaApiFpsOverlayDraw(GameDisplay* disp) { g_fps_overlay.draw(disp); }
#endif
