// ============================================
// ファイル: game_catalog.cpp
// ============================================

#include "game_catalog.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "config.hpp"
#include "st7789_lcd.hpp"

extern "C" {
#include "ff.h"
}

namespace {

/** ファイル名が .lua 拡張子か判定する。ゲームスクリプト走査時に使う */
bool endsWithLuaExt(const char* name) {
    if (!name) {
        return false;
    }
    const size_t len = std::strlen(name);
    if (len < 5) {
        return false;
    }
    const char* ext = name + len - 4;
    return ext[0] == '.' && std::tolower(static_cast<unsigned char>(ext[1])) == 'l' &&
           std::tolower(static_cast<unsigned char>(ext[2])) == 'u' &&
           std::tolower(static_cast<unsigned char>(ext[3])) == 'a';
}

/** ファイル名から表示用タイトル文字列を生成する。単体 .lua エントリ登録時に使う */
void toTitleFromFilename(const char* filename, char* out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!filename) {
        return;
    }
    size_t j = 0;
    for (size_t i = 0; filename[i] != '\0' && j + 1 < out_len; i++) {
        char c = filename[i];
        if (c == '.') {
            break;
        }
        if (c == '_') {
            c = ' ';
        }
        out[j++] = c;
    }
    out[j] = '\0';
}

/** ディレクトリパスとファイル名を結合する。SD 上のパス組み立て時に使う */
bool pathJoin(const char* base, const char* name, char* out, size_t out_len) {
    if (!base || !name || !out || out_len == 0) {
        return false;
    }
    if (std::strcmp(base, "/") == 0) {
        return std::snprintf(out, out_len, "/%s", name) > 0;
    }
    return std::snprintf(out, out_len, "%s/%s", base, name) > 0;
}

/** パスが通常ファイルとして存在するか確認する。プレビュー・スクリプト解決時に使う */
bool fileExists(const char* path, uint32_t* out_size = nullptr) {
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) {
        return false;
    }
    if (fno.fattrib & AM_DIR) {
        return false;
    }
    if (out_size) {
        *out_size = static_cast<uint32_t>(fno.fsize);
    }
    return true;
}

/** パス文字列から末尾のファイル／フォルダ名を返す。同名スクリプト推測時に使う */
const char* baseNameOfPath(const char* path) {
    if (!path) {
        return "";
    }
    const char* last = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/') {
            last = p + 1;
        }
    }
    return last;
}

/** 2 つのファイル名を大文字小文字無視で比較する。補助スクリプト判定時に使う */
bool namesEqualIgnoreCase(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/** assets.lua 等の補助スクリプトか判定する。起動スクリプト選別時に使う */
bool isAuxiliaryLuaScript(const char* filename) {
    static const char* kAuxiliary[] = {
        "assets.lua",
        "scenario.lua",
        "config.lua",
        "data.lua",
        "constants.lua",
        "level_data.lua",
        "levels.lua",
    };
    if (!filename) {
        return false;
    }
    for (const char* aux : kAuxiliary) {
        if (namesEqualIgnoreCase(filename, aux)) {
            return true;
        }
    }
    return false;
}

/** Lua ソース先頭付近に game_init 定義があるか走査する。メインスクリプト判定時に使う */
bool luaSourceDeclaresGameInit(const char* path) {
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) {
        return false;
    }

    static const char* kMarkers[] = {"function game_init", "game_init =", "game_init="};
    constexpr int kOverlap = 24;
    constexpr int kChunk = 512;
    constexpr UINT kMaxProbe = 24 * 1024;
    char buf[kOverlap + kChunk + 1];
    int carry = 0;
    UINT total = 0;
    bool found = false;

    while (total < kMaxProbe) {
        UINT br = 0;
        const FRESULT fr = f_read(&file, buf + carry, kChunk, &br);
        if (fr != FR_OK || br == 0) {
            break;
        }

        const int len = carry + static_cast<int>(br);
        buf[len] = '\0';
        for (const char* marker : kMarkers) {
            if (std::strstr(buf, marker) != nullptr) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }

        carry = len > kOverlap ? kOverlap : len;
        std::memmove(buf, buf + len - carry, static_cast<size_t>(carry));
        total += br;
    }
    f_close(&file);
    return found;
}

/** 指定パスの .lua を起動スクリプトとして採用する。候補確定時に使う */
bool tryPickScriptPath(const char* path, char* out_script, size_t out_script_len, uint32_t* out_size) {
    uint32_t size = 0;
    if (!fileExists(path, &size)) {
        return false;
    }
    std::snprintf(out_script, out_script_len, "%s", path);
    if (out_size) {
        *out_size = size;
    }
    return true;
}

/** .lua 候補の優先度（高いほど起動スクリプトとして望ましい） */
int luaPickScore(bool has_game_init, bool is_auxiliary) {
    if (has_game_init && !is_auxiliary) {
        return 3;
    }
    if (has_game_init) {
        return 2;
    }
    if (!is_auxiliary) {
        return 1;
    }
    return 0;
}

/** ゲームフォルダ内から最適な起動 .lua を選ぶ。エントリ登録時に使う */
bool pickScriptInDir(const char* dir_path, char* out_script, size_t out_script_len, uint32_t* out_size) {
    if (!dir_path || !out_script || out_script_len == 0) {
        return false;
    }
    out_script[0] = '\0';

    static const char* kPriority[] = {"game.lua", "main.lua", "boot.lua"};
    char path[96];
    for (const char* name : kPriority) {
        if (!pathJoin(dir_path, name, path, sizeof(path))) {
            continue;
        }
        if (tryPickScriptPath(path, out_script, out_script_len, out_size)) {
            return true;
        }
    }

    const char* dir_name = baseNameOfPath(dir_path);
    if (dir_name[0] != '\0') {
        char named_script[FF_LFN_BUF + 1];
        std::snprintf(named_script, sizeof(named_script), "%s.lua", dir_name);
        if (pathJoin(dir_path, named_script, path, sizeof(path)) &&
            tryPickScriptPath(path, out_script, out_script_len, out_size)) {
            return true;
        }
    }

    char best_path[96] = {};
    int best_score = -1;

    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, dir_path) != FR_OK) {
        return false;
    }
    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        if ((fno.fattrib & AM_DIR) != 0) {
            continue;
        }
        if (!endsWithLuaExt(fno.fname)) {
            continue;
        }
        if (!pathJoin(dir_path, fno.fname, path, sizeof(path))) {
            continue;
        }
        const bool is_auxiliary = isAuxiliaryLuaScript(fno.fname);
        const bool has_game_init = luaSourceDeclaresGameInit(path);
        const int score = luaPickScore(has_game_init, is_auxiliary);
        if (score > best_score) {
            best_score = score;
            std::snprintf(best_path, sizeof(best_path), "%s", path);
        }
    }
    f_closedir(&dir);

    if (best_score < 0) {
        return false;
    }
    return tryPickScriptPath(best_path, out_script, out_script_len, out_size);
}

/** バイナリサイズから RGB565 プレビューの幅・高さを推定する。プレビュー読込前に使う */
bool inferPreviewDimensions(uint32_t byte_size, int* out_w, int* out_h) {
    if (!out_w || !out_h || byte_size < 2 || (byte_size % 2) != 0) {
        return false;
    }
    const uint32_t pixels = byte_size / 2;
    if (pixels == 0) {
        return false;
    }
    if (byte_size == static_cast<uint32_t>(GameCatalog::kPreviewBytes)) {
        *out_w = GameCatalog::kPreviewW;
        *out_h = GameCatalog::kPreviewH;
        return true;
    }

    int best_w = 0;
    int best_h = 0;
    uint32_t best_diff = UINT32_MAX;
    for (int w = 1; w <= GameCatalog::kPreviewW; w++) {
        if ((pixels % static_cast<uint32_t>(w)) != 0) {
            continue;
        }
        const int h = static_cast<int>(pixels / static_cast<uint32_t>(w));
        if (h < 1 || h > GameCatalog::kPreviewH) {
            continue;
        }
        const uint32_t diff =
            static_cast<uint32_t>(w > h ? (w - h) : (h - w));
        if (diff < best_diff) {
            best_diff = diff;
            best_w = w;
            best_h = h;
        }
    }
    if (best_w <= 0 || best_h <= 0) {
        return false;
    }
    *out_w = best_w;
    *out_h = best_h;
    return true;
}

/** title.bin / preview.bin 等のプレビュー画像パスを解決する。エントリ構築時に使う */
bool pickPreviewPath(const char* game_dir, const char* script_path, char* out_preview,
                     size_t out_preview_len) {
    if (!out_preview || out_preview_len == 0) {
        return false;
    }
    out_preview[0] = '\0';

    char candidate[96];
    if (game_dir && game_dir[0] != '\0') {
        if (pathJoin(game_dir, "title.bin", candidate, sizeof(candidate)) && fileExists(candidate)) {
            std::snprintf(out_preview, out_preview_len, "%s", candidate);
            return true;
        }
        if (pathJoin(game_dir, "preview.bin", candidate, sizeof(candidate)) && fileExists(candidate)) {
            std::snprintf(out_preview, out_preview_len, "%s", candidate);
            return true;
        }
    }

    if (script_path) {
        char base[96];
        std::snprintf(base, sizeof(base), "%s", script_path);
        char* dot = std::strrchr(base, '.');
        if (dot) {
            std::snprintf(dot, static_cast<size_t>(base + sizeof(base) - dot), ".bin");
            if (fileExists(base)) {
                std::snprintf(out_preview, out_preview_len, "%s", base);
                return true;
            }
        }
    }
    return false;
}

/** games_dir 直下の 1 エントリがゲーム候補か判定する（登録はしない） */
bool isGameCatalogCandidate(const char* games_dir, const FILINFO& fno) {
    if (fno.fname[0] == '.' || (fno.fattrib & AM_DIR) == 0) {
        if (fno.fname[0] == '.' || !endsWithLuaExt(fno.fname)) {
            return false;
        }
        char script_path[96];
        return pathJoin(games_dir, fno.fname, script_path, sizeof(script_path));
    }
    char game_dir[96];
    if (!pathJoin(games_dir, fno.fname, game_dir, sizeof(game_dir))) {
        return false;
    }
    char script_path[96];
    uint32_t script_size = 0;
    return pickScriptInDir(game_dir, script_path, sizeof(script_path), &script_size);
}

/** パスが FatFS 向け ASCII 絶対パスとして妥当か簡易判定する */
bool pathLooksValid(const char* path) {
    if (!path || path[0] != '/') {
        return false;
    }
    for (const char* p = path; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20 || c >= 0x7F) {
            return false;
        }
        if (c == '*' || c == '?' || c == ':' || c == '|' || c == '"') {
            return false;
        }
    }
    return true;
}

/** スクリプト絶対パスから親ディレクトリを取り出す */
void parentDirOfScript(const char* script_path, char* dir_out, size_t dir_len) {
    if (!dir_out || dir_len == 0) {
        return;
    }
    dir_out[0] = '\0';
    if (!script_path || script_path[0] == '\0') {
        return;
    }
    char norm[96];
    std::snprintf(norm, sizeof(norm), "%s", script_path);
    char* slash = std::strrchr(norm, '/');
    if (!slash || slash == norm) {
        dir_out[0] = '/';
        dir_out[1] = '\0';
        return;
    }
    *slash = '\0';
    std::snprintf(dir_out, dir_len, "%s", norm);
}

/** 解決済み script / preview を出力バッファへコピーする */
bool copyResolvedPaths(const char* script_path, const char* preview_path, uint32_t script_size,
                       char* out_script, size_t out_script_len, char* out_preview,
                       size_t out_preview_len, uint32_t* out_script_size) {
    if (!script_path || !pathLooksValid(script_path)) {
        return false;
    }
    if (out_script && out_script_len > 0) {
        std::snprintf(out_script, out_script_len, "%s", script_path);
    }
    if (out_preview && out_preview_len > 0) {
        if (preview_path && preview_path[0] != '\0') {
            std::snprintf(out_preview, out_preview_len, "%s", preview_path);
        } else {
            out_preview[0] = '\0';
        }
    }
    if (out_script_size) {
        *out_script_size = script_size;
    }
    return true;
}

/** install_name が .lua 単体かフォルダ内ゲームかで SD から script / preview を解決する */
bool resolveFromInstallName(const char* games_dir, const char* install_name, char* out_script,
                            size_t out_script_len, char* out_preview, size_t out_preview_len,
                            uint32_t* out_script_size) {
    if (!games_dir || !install_name || install_name[0] == '\0') {
        return false;
    }

    char script_path[96];
    char preview_path[96];
    preview_path[0] = '\0';
    uint32_t script_size = 0;

    if (endsWithLuaExt(install_name)) {
        if (!pathJoin(games_dir, install_name, script_path, sizeof(script_path))) {
            return false;
        }
        if (!tryPickScriptPath(script_path, script_path, sizeof(script_path), &script_size)) {
            return false;
        }
        pickPreviewPath(games_dir, script_path, preview_path, sizeof(preview_path));
        return copyResolvedPaths(script_path, preview_path, script_size, out_script, out_script_len,
                                 out_preview, out_preview_len, out_script_size);
    }

    char game_dir[96];
    if (!pathJoin(games_dir, install_name, game_dir, sizeof(game_dir))) {
        return false;
    }
    if (!pickScriptInDir(game_dir, script_path, sizeof(script_path), &script_size)) {
        return false;
    }
    pickPreviewPath(game_dir, script_path, preview_path, sizeof(preview_path));
    return copyResolvedPaths(script_path, preview_path, script_size, out_script, out_script_len,
                             out_preview, out_preview_len, out_script_size);
}

/** キャッシュ済み script_path が SD 上で有効ならそれを使い preview だけ再解決する */
bool resolveFromCachedScript(const GameCatalogEntry& entry, char* out_script, size_t out_script_len,
                             char* out_preview, size_t out_preview_len, uint32_t* out_script_size) {
    if (!pathLooksValid(entry.script_path)) {
        return false;
    }
    uint32_t script_size = 0;
    if (!fileExists(entry.script_path, &script_size)) {
        return false;
    }

    char preview_path[96];
    preview_path[0] = '\0';
    if (pathLooksValid(entry.preview_path) && fileExists(entry.preview_path)) {
        std::snprintf(preview_path, sizeof(preview_path), "%s", entry.preview_path);
    } else {
        char game_dir[96];
        parentDirOfScript(entry.script_path, game_dir, sizeof(game_dir));
        pickPreviewPath(game_dir, entry.script_path, preview_path, sizeof(preview_path));
    }
    return copyResolvedPaths(entry.script_path, preview_path, script_size, out_script, out_script_len,
                             out_preview, out_preview_len, out_script_size);
}

}  // namespace

/** games_dir 直下のフォルダと .lua を走査しメニュー用エントリ配列を構築する。メニュー読込時に呼ぶ */
int GameCatalog::loadEntries(const char* games_dir, GameCatalogEntry* out_entries, int max_entries,
                           bool* out_truncated) {
    if (out_truncated) {
        *out_truncated = false;
    }
    if (!games_dir || !out_entries || max_entries <= 0) {
        return 0;
    }

    int count = 0;
    bool truncated = false;
    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, games_dir) != FR_OK) {
        return 0;
    }

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        if (fno.fname[0] == '.') {
            continue;
        }

        if (count >= max_entries) {
            if (isGameCatalogCandidate(games_dir, fno)) {
                truncated = true;
            }
            continue;
        }

        GameCatalogEntry& e = out_entries[count];
        std::memset(&e, 0, sizeof(e));

        if (fno.fattrib & AM_DIR) {
            char game_dir[96];
            if (!pathJoin(games_dir, fno.fname, game_dir, sizeof(game_dir))) {
                continue;
            }
            if (!pickScriptInDir(game_dir, e.script_path, sizeof(e.script_path), &e.script_size)) {
                continue;
            }
            std::snprintf(e.install_name, sizeof(e.install_name), "%s", fno.fname);
            std::snprintf(e.title, sizeof(e.title), "%s", fno.fname);
            pickPreviewPath(game_dir, e.script_path, e.preview_path, sizeof(e.preview_path));
            count++;
            continue;
        }

        if (!endsWithLuaExt(fno.fname)) {
            continue;
        }
        if (!pathJoin(games_dir, fno.fname, e.script_path, sizeof(e.script_path))) {
            continue;
        }
        e.script_size = static_cast<uint32_t>(fno.fsize);
        std::snprintf(e.install_name, sizeof(e.install_name), "%s", fno.fname);
        toTitleFromFilename(fno.fname, e.title, sizeof(e.title));
        pickPreviewPath(games_dir, e.script_path, e.preview_path, sizeof(e.preview_path));
        count++;
    }
    f_closedir(&dir);
    if (out_truncated) {
        *out_truncated = truncated;
    }
    return count;
}

/** SD 上の .bin プレビューを 100x100 RGB565 パネルへ読み込む。右パネル描画前に呼ぶ */
bool GameCatalog::loadPreviewRgb565(const char* preview_path, uint16_t* pixels, size_t pixel_count) {
    if (!preview_path || preview_path[0] == '\0' || !pixels) {
        return false;
    }
    if (pixel_count < static_cast<size_t>(kPreviewW) * static_cast<size_t>(kPreviewH)) {
        return false;
    }

    FIL file;
    if (f_open(&file, preview_path, FA_READ) != FR_OK) {
        printf("GameCatalog: preview open failed: %s\n", preview_path);
        return false;
    }
    const FSIZE_t fsize = f_size(&file);
    if (fsize < 2 || fsize > static_cast<FSIZE_t>(kPreviewBytes)) {
        printf("GameCatalog: preview size invalid: %s (%lu bytes, max %u)\n", preview_path,
               static_cast<unsigned long>(fsize), static_cast<unsigned>(kPreviewBytes));
        f_close(&file);
        return false;
    }

    int img_w = 0;
    int img_h = 0;
    if (!inferPreviewDimensions(static_cast<uint32_t>(fsize), &img_w, &img_h)) {
        printf("GameCatalog: preview dimensions invalid: %s (%lu bytes)\n", preview_path,
               static_cast<unsigned long>(fsize));
        f_close(&file);
        return false;
    }

    constexpr uint16_t kPreviewBg = Color::rgb(20, 20, 40);
    const size_t panel_pixels =
        static_cast<size_t>(kPreviewW) * static_cast<size_t>(kPreviewH);
    for (size_t i = 0; i < panel_pixels; i++) {
        pixels[i] = kPreviewBg;
    }

    const int dst_x = (kPreviewW - img_w) / 2;
    const int dst_y = (kPreviewH - img_h) / 2;
    uint8_t row_buf[kPreviewW * 2];

    for (int y = 0; y < img_h; y++) {
        UINT br = 0;
        const FRESULT fr =
            f_read(&file, row_buf, static_cast<UINT>(img_w) * 2u, &br);
        if (fr != FR_OK || br != static_cast<UINT>(img_w) * 2u) {
            printf("GameCatalog: preview read failed: %s row %d\n", preview_path, y);
            f_close(&file);
            return false;
        }
        std::memcpy(pixels + static_cast<size_t>(dst_y + y) * kPreviewW + static_cast<size_t>(dst_x),
                    row_buf, static_cast<size_t>(img_w) * 2u);
    }
    f_close(&file);
    printf("GameCatalog: preview OK %s (%dx%d in %dx%d panel)\n", preview_path, img_w, img_h,
           kPreviewW, kPreviewH);
    return true;
}

bool GameCatalog::resolveEntryPaths(const char* games_dir, const GameCatalogEntry& entry,
                                    char* out_script, size_t out_script_len, char* out_preview,
                                    size_t out_preview_len, uint32_t* out_script_size) {
    if (resolveFromCachedScript(entry, out_script, out_script_len, out_preview, out_preview_len,
                                out_script_size)) {
        return true;
    }
    if (entry.install_name[0] != '\0') {
        if (resolveFromInstallName(games_dir, entry.install_name, out_script, out_script_len,
                                   out_preview, out_preview_len, out_script_size)) {
            return true;
        }
    }
    // install_name だけ壊れたフォルダ型エントリ向け（title はフォルダ名と同一）
    if (entry.title[0] != '\0' && !endsWithLuaExt(entry.title)) {
        return resolveFromInstallName(games_dir, entry.title, out_script, out_script_len, out_preview,
                                      out_preview_len, out_script_size);
    }
    return false;
}

bool GameCatalog::refreshEntryPaths(const char* games_dir, GameCatalogEntry& entry) {
    char script_path[96];
    char preview_path[96];
    uint32_t script_size = 0;
    if (!resolveEntryPaths(games_dir, entry, script_path, sizeof(script_path), preview_path,
                           sizeof(preview_path), &script_size)) {
        return false;
    }
    std::snprintf(entry.script_path, sizeof(entry.script_path), "%s", script_path);
    std::snprintf(entry.preview_path, sizeof(entry.preview_path), "%s", preview_path);
    entry.script_size = script_size;
    return true;
}
