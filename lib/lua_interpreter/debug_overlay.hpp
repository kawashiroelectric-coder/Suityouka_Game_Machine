// ============================================
// ファイル: debug_overlay.hpp
// Lua ゲーム中の FPS / 動的 RAM デバッグ表示
// GAME_MACHINE_DEBUG 無効時は空実装（呼び出し側は #ifdef 不要）
// ============================================

#ifndef DEBUG_OVERLAY_HPP
#define DEBUG_OVERLAY_HPP

#include <cstdint>

class ST7789_LCD;

/** デバッグオーバーレイの計測状態をリセットする */
void debugOverlayReset();
/** デバッグオーバーレイの FPS 計測を1フレーム進める */
void debugOverlayTick(uint32_t now_ms);
/** フレーム DMA 完了後、LCD へ直接描画する */
void debugOverlayDrawAfterFrame(ST7789_LCD* lcd, int screen_width);

#endif  // DEBUG_OVERLAY_HPP
