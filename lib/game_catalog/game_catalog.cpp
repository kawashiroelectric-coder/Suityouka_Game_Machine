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

bool pathJoin(const char* base, const char* name, char* out, size_t out_len) {
    if (!base || !name || !out || out_len == 0) {
        return false;
    }
    if (std::strcmp(base, "/") == 0) {
        return std::snprintf(out, out_len, "/%s", name) > 0;
    }
    return std::snprintf(out, out_len, "%s/%s", base, name) > 0;
}

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

    struct LuaPick {
        char path[96];
        uint32_t size = 0;
        bool has_game_init = false;
        bool is_auxiliary = false;
    };
    LuaPick candidates[16];
    int candidate_count = 0;

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
        if (candidate_count >= static_cast<int>(sizeof(candidates) / sizeof(candidates[0]))) {
            continue;
        }

        LuaPick& c = candidates[candidate_count++];
        if (!pathJoin(dir_path, fno.fname, c.path, sizeof(c.path))) {
            candidate_count--;
            continue;
        }
        c.size = static_cast<uint32_t>(fno.fsize);
        c.is_auxiliary = isAuxiliaryLuaScript(fno.fname);
        c.has_game_init = luaSourceDeclaresGameInit(c.path);
    }
    f_closedir(&dir);

    for (int i = 0; i < candidate_count; i++) {
        if (candidates[i].has_game_init && !candidates[i].is_auxiliary) {
            return tryPickScriptPath(candidates[i].path, out_script, out_script_len, out_size);
        }
    }
    for (int i = 0; i < candidate_count; i++) {
        if (candidates[i].has_game_init) {
            return tryPickScriptPath(candidates[i].path, out_script, out_script_len, out_size);
        }
    }
    for (int i = 0; i < candidate_count; i++) {
        if (!candidates[i].is_auxiliary) {
            return tryPickScriptPath(candidates[i].path, out_script, out_script_len, out_size);
        }
    }
    if (candidate_count > 0) {
        return tryPickScriptPath(candidates[0].path, out_script, out_script_len, out_size);
    }
    return false;
}

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

}  // namespace

int GameCatalog::loadEntries(const char* games_dir, GameCatalogEntry* out_entries, int max_entries) {
    if (!games_dir || !out_entries || max_entries <= 0) {
        return 0;
    }

    int count = 0;
    DIR dir;
    FILINFO fno;
    if (f_opendir(&dir, games_dir) != FR_OK) {
        return 0;
    }

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0 && count < max_entries) {
        if (fno.fname[0] == '.') {
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
        toTitleFromFilename(fno.fname, e.title, sizeof(e.title));
        pickPreviewPath(games_dir, e.script_path, e.preview_path, sizeof(e.preview_path));
        count++;
    }
    f_closedir(&dir);
    return count;
}

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
