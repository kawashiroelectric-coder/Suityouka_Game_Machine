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
    /** SD 上のフォルダ名または .lua ファイル名（パス再解決のキー） */
    char install_name[40];
    char script_path[96];
    char preview_path[96];
    uint32_t script_size = 0;
};

/** SD 上のゲームフォルダ / .lua から起動スクリプトとプレビューを解決する */
class GameCatalog {
public:
    static constexpr int kMaxEntries = 256;
    static constexpr int kPreviewW = 100;
    static constexpr int kPreviewH = 100;
    static constexpr size_t kPreviewBytes =
        static_cast<size_t>(kPreviewW) * static_cast<size_t>(kPreviewH) * 2;

    /**
     * games_dir を走査してゲームエントリ一覧を構築する。メニュー初期化時に呼ぶ。
     * out_truncated: max_entries 超のゲームが SD 上にあった場合 true。
     */
    static int loadEntries(const char* games_dir, GameCatalogEntry* out_entries, int max_entries,
                           bool* out_truncated = nullptr);

    /** プレビュー .bin を 100x100 RGB565 バッファへ読み込む。選択ゲームのプレビュー表示時に呼ぶ */
    static bool loadPreviewRgb565(const char* preview_path, uint16_t* pixels, size_t pixel_count);

    /**
     * install_name から SD 上の script / preview パスを再解決する。
     * RAM 上のキャッシュが壊れていても SD を正とする。
     */
    static bool resolveEntryPaths(const char* games_dir, const GameCatalogEntry& entry,
                                  char* out_script, size_t out_script_len, char* out_preview,
                                  size_t out_preview_len, uint32_t* out_script_size);

    /** resolveEntryPaths の結果を entry に書き戻す。プレビュー表示・起動直前に呼ぶ */
    static bool refreshEntryPaths(const char* games_dir, GameCatalogEntry& entry);
};

#endif  // GAME_CATALOG_HPP
