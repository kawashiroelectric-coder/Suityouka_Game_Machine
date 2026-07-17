# サンプルゲーム集

Pico 向け Lua ゲームサンプルです。各フォルダの README に編集方法・操作説明があります。  
SD カードではリポジトリの `games/` を **`/games/`** として配置してください。

| フォルダ | 内容 | 詳細 |
|----------|------|------|
| [stg/](stg/) | 縦スクロール弾幕 STG「翠晶撃線」 | [stg/README.md](stg/README.md) |
| [stg_fast/](stg_fast/) | 翠晶撃線（Lua 描画ホットパス最適化版） | [stg_fast/README.md](stg_fast/README.md) |
| [Shogi/](Shogi/) | 将棋 vs もみじ（9×9・難易度 3 段階・対局セーブ） | [Shogi/README.md](Shogi/README.md) |
| [Run!Yamame/](Run!Yamame/) | 洞窟ランナー「Run!Yamame」（HI SCORE 対応） | [Run!Yamame/README.md](Run!Yamame/README.md) |
| [visual_novel/](visual_novel/) | ビジュアルノベル（`draw_vn_stream` で背景・立ち絵を SD ストリーム描画） | [visual_novel/README.md](visual_novel/README.md) |
| [tile_test/](tile_test/) | タイル横スクロール「Star Hop」（星集めプラットフォーム） | [tile_test/README.md](tile_test/README.md) |
| [sokoban/](sokoban/) | 倉庫番（ランダム生成・得点制） | [sokoban/README.md](sokoban/README.md) |
| [save_test/](save_test/) | セーブ／ロード API テスト（`save_data` / `load_data`） | [save_test/README.md](save_test/README.md) |

> `reversi/` は将棋へ置き換え済みです → [Shogi/](Shogi/)

## SD 配置の早見表

| サンプル | SD 上のパス |
|----------|-------------|
| stg | `/games/stg/stg.lua` + `img/*.bin`（任意） |
| stg_fast | `/games/stg_fast/stg_fast.lua` + `img/*.bin`（任意） |
| Shogi | `/games/Shogi/Shogi.lua` + `img/*.bin`（Momiji / Koma / `Shogi_BGs.bin`）+ `fonts/game_font.bin` + `sound/piece_drop.wav` |
| Run!Yamame | `/games/Run!Yamame/Run!Yamame.lua` + スプライト `.bin`（セーブ時 `hi_score.dat`） |
| visual_novel | `/games/visual_novel/visual_novel.lua` + `scenario.lua` + `fonts/` + `images/` |
| tile_test | `/games/tile_test/tile_test.lua` + `tiles/*.bin` |
| sokoban | `/games/sokoban/sokoban.lua` |
| save_test | `/games/save_test/save_test.lua`（セーブ時 `save_a.dat` / `save_b.dat`） |

## 共通ツール

| スクリプト | 用途 |
|------------|------|
| [generate_assets.py](generate_assets.py) | tile_test 用タイルセット `.bin` 生成 |
| [Shogi/generate_font.py](Shogi/generate_font.py) | 美咲 `misaki.hpp` → 将棋用 `game_font.bin` |
| [../tool/image_to_rgb565_bin.py](../tool/image_to_rgb565_bin.py) | PNG → RGB565 `.bin` 変換 |

API リファレンス: [LUA_API.md](../LUA_API.md)
