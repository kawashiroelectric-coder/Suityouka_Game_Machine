// ============================================
// ファイル: game_catalog.hpp
// /games 配下のゲームエントリ検出とプレビュー読込
// ============================================

#ifndef GAME_CATALOG_HPP
#define GAME_CATALOG_HPP

#include <cstddef>
#include <cstdint>

/** メニュー表示用の 1 ゲーム分メタデータ */
struct GameCatalogEntry {
    char title[40];
    char script_path[96];
    char preview_path[96];
    uint32_t script_size = 0;
};

/** SD 上のゲームフォルダ / .lua から起動スクリプトとプレビューを解決する */
class GameCatalog {
public:
    static constexpr int kMaxEntries = 32;
    static constexpr int kPreviewW = 100;
    static constexpr int kPreviewH = 100;
    static constexpr size_t kPreviewBytes =
        static_cast<size_t>(kPreviewW) * static_cast<size_t>(kPreviewH) * 2;

    /** games_dir を走査してゲームエントリ一覧を構築する。メニュー初期化時に呼ぶ */
    static int loadEntries(const char* games_dir, GameCatalogEntry* out_entries, int max_entries);

    /** プレビュー .bin を 100x100 RGB565 バッファへ読み込む。選択ゲームのプレビュー表示時に呼ぶ */
    static bool loadPreviewRgb565(const char* preview_path, uint16_t* pixels, size_t pixel_count);
};

#endif  // GAME_CATALOG_HPP
