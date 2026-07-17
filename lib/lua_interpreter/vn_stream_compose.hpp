// ============================================
// ファイル: vn_stream_compose.hpp
// VN 用 SD ストリーム合成（背景 + 立ち絵最大 2 枚）
//
// LuaInterpreter::vn_stream_ に FIL を保持し、各バンドで
// 背景 → 立ち絵 0 → 立ち絵 1 の順に GameDisplay へ転送する。
// FIL はフレーム跨ぎで開きっぱなし（ゲーム終了時に close）。
// ============================================

#ifndef VN_STREAM_COMPOSE_HPP
#define VN_STREAM_COMPOSE_HPP

#include <cstdint>

extern "C" {
#include "ff.h"
}

struct lua_State;
class LuaInterpreter;

/** 最大同時立ち絵枚数 */
constexpr int kVnStreamCharLayers = 2;

/** SD から読む 1 レイヤー（背景 or 立ち絵） */
struct VnStreamLayer {
    FIL file{};
    char path[FF_LFN_BUF + 4]{};
    char fail_path[FF_LFN_BUF + 4]{};
    bool open = false;
    bool active = false;
    uint16_t width = 0;
    uint16_t height = 0;
    int dx = 0;
    int dy = 0;
    uint16_t key_color = 0xF81F;
    bool keyed = true;
};

/** vn_stream_ の状態（背景 1 + 立ち絵 2） */
struct VnStreamComposeState {
    VnStreamLayer bg;
    VnStreamLayer chars[kVnStreamCharLayers];
    int char_count = 0;
};

/** 現在バンド分を合成描画。Lua テーブルを毎バンド parse し FIL は再利用 */
bool vnStreamComposeDraw(LuaInterpreter* interp, lua_State* L, int table_index);

/** 既に sync 済みの vn_stream_ を現在バンドへ描画（command list 再生用） */
bool vnStreamComposeReplayBand(LuaInterpreter* interp);

/** レイヤーだけ同期し SD 読み描画はしない（command list 録画用） */
bool vnStreamComposeSyncOnly(LuaInterpreter* interp, lua_State* L, int table_index);

// フレーム末に bg_stream_ の FIL を閉じる。
/** オープン中の合成用 SD ファイルをすべて閉じ、状態をリセット */
void vnStreamComposeClose(LuaInterpreter* interp, bool abandon_open_files = false);

#endif  // VN_STREAM_COMPOSE_HPP
