// ============================================
// ファイル: menu_cursor_se.hpp
// メニュー共通カーソル移動 SE（flash 埋め込み）
// ============================================

#ifndef MENU_CURSOR_SE_HPP
#define MENU_CURSOR_SE_HPP

#include "cursor_move.h"
#include "lua_audio.hpp"

/** ゲーム選択・システム設定メニューのカーソル移動音を再生する */
inline void playMenuCursorSe(LuaAudio* audio) {
    if (!audio) {
        return;
    }
    audio->playSeFromEmbedded(cursor_move_pcm, cursor_move_frame_count, cursor_move_channels,
                              cursor_move_sample_rate);
}

#endif  // MENU_CURSOR_SE_HPP
