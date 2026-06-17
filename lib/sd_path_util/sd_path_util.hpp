// ============================================
// ファイル: sd_path_util.hpp
// FatFS 向けパス正規化（ヘッダのみ・インライン実装）
//
// LuaInterpreter::resolveGamePath / SD 読込全般で使用。
// script_dir は runGameLoopFromSd 開始時に extractScriptDir で設定される。
// ============================================

#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

/** FatFS (FF_FS_RPATH==0) 向け: 相対パスをルート絶対パス "/..." に正規化する */
inline void normalizeSdPath(const char* in, char* out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!in || in[0] == '\0') {
        return;
    }
    if (in[0] == '/' || (in[0] >= '0' && in[0] <= '9' && in[1] == ':')) {
        strncpy(out, in, out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }
    if (out_len < 2) {
        return;
    }
    out[0] = '/';
    strncpy(out + 1, in, out_len - 2);
    out[out_len - 1] = '\0';
}

/** "/foo/bar.lua" → "/foo"（ルート直下のスクリプトは "/"） */
inline void extractScriptDir(const char* script_path, char* dir_out, size_t dir_len) {
    if (!dir_out || dir_len == 0) {
        return;
    }
    dir_out[0] = '\0';
    if (!script_path || script_path[0] == '\0') {
        return;
    }

    char norm[512];
    normalizeSdPath(script_path, norm, sizeof(norm));
    if (norm[0] == '\0') {
        return;
    }

    char* slash = strrchr(norm, '/');
    if (!slash || slash == norm) {
        dir_out[0] = '/';
        dir_out[1] = '\0';
        return;
    }

    *slash = '\0';
    if (norm[0] == '\0') {
        dir_out[0] = '/';
        dir_out[1] = '\0';
        return;
    }
    strncpy(dir_out, norm, dir_len - 1);
    dir_out[dir_len - 1] = '\0';
}

/**
 * SD パス解決。
 * - 先頭 '/' … 絶対パス
 * - script_dir あり … script_dir + 相対
 * - それ以外 … SD ルート相対（normalizeSdPath）
 */
inline void resolveSdPath(const char* script_dir, const char* in, char* out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!in || in[0] == '\0') {
        return;
    }
    if (in[0] == '/' || (in[0] >= '0' && in[0] <= '9' && in[1] == ':')) {
        strncpy(out, in, out_len - 1);
        out[out_len - 1] = '\0';
        return;
    }
    if (script_dir && script_dir[0] != '\0') {
        if (strcmp(script_dir, "/") == 0) {
            if (out_len < 2) {
                return;
            }
            out[0] = '/';
            strncpy(out + 1, in, out_len - 2);
            out[out_len - 1] = '\0';
            return;
        }
        snprintf(out, out_len, "%s/%s", script_dir, in);
        return;
    }
    normalizeSdPath(in, out, out_len);
}
