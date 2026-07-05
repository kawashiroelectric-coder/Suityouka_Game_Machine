# Test_Lua サンプル集

Pico 向け Lua ゲームサンプルです。各フォルダの README に編集方法があります。

| フォルダ | 内容 | 詳細 |
|----------|------|------|
| [visual_novel/](visual_novel/) | ビジュアルノベル（`draw_vn_stream` で背景・立ち絵を SD ストリーム描画） | [visual_novel/README.md](visual_novel/README.md) |
| [tile_test/](tile_test/) | タイル横スクロール「Star Hop」（星集めプラットフォーム） | [tile_test/README.md](tile_test/README.md) |
| [save_test/](save_test/) | セーブ／ロード API テスト（`save_data` / `load_data`） | [save_test/README.md](save_test/README.md) |
| [stg/](stg/) | 縦スクロール弾幕 STG「翠晶撃線」 | [stg/README.md](stg/README.md) |
| [stg_fast/](stg_fast/) | 翠晶撃線（Lua 描画ホットパス最適化版） | [stg_fast/README.md](stg_fast/README.md) |
| [sokoban/](sokoban/) | 倉庫番（ランダム生成・得点制） | [sokoban/README.md](sokoban/README.md) |

## SD 配置の早見表

| サンプル | SD 上のパス |
|----------|-------------|
| visual_novel | `/visual_novel/visual_novel.lua` + `scenario.lua` + `fonts/` + `images/` |
| tile_test | `/games/tile_test/tile_test.lua` + `tiles/*.bin` |
| save_test | `/save_test/save_test.lua`（セーブ時 `/save_test/save.dat` が生成） |
| stg | `/games/stg/stg.lua` + `img/*.bin`（任意） |
| stg_fast | `/games/stg_fast/stg_fast.lua` + `img/*.bin`（任意） |
| sokoban | `/games/sokoban/sokoban.lua` |

## 共通ツール

| スクリプト | 用途 |
|------------|------|
| [generate_assets.py](generate_assets.py) | tile_test 用タイルセット `.bin` 生成 |
| [../tool/image_to_rgb565_bin.py](../tool/image_to_rgb565_bin.py) | PNG → RGB565 `.bin` 変換 |

API リファレンス: [LUA_API.md](../LUA_API.md)
