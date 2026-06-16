# Test_Lua サンプル集

Pico 向け Lua ゲームサンプルです。各フォルダの README に編集方法があります。

| フォルダ | 内容 | 詳細 |
|----------|------|------|
| [visual_novel/](visual_novel/) | ビジュアルノベル（`draw_vn_stream` で背景・立ち絵を SD ストリーム描画） | [visual_novel/README.md](visual_novel/README.md) |
| [tile_test/](tile_test/) | タイルレイヤー表示テスト（GBA 風 parallax） | [tile_test/README.md](tile_test/README.md) |
| [save_test/](save_test/) | セーブ／ロード API テスト（`save_data` / `load_data`） | [save_test/README.md](save_test/README.md) |

## SD 配置の早見表

| サンプル | SD 上のパス |
|----------|-------------|
| visual_novel | `/visual_novel/visual_novel.lua` + `scenario.lua` + `fonts/` + `images/` |
| tile_test | `/layers_test.lua` + `/tiles/*.bin` |
| save_test | `/save_test/save_test.lua`（セーブ時 `/save_test/save.dat` が生成） |

## 共通ツール

| スクリプト | 用途 |
|------------|------|
| [generate_assets.py](generate_assets.py) | tile_test 用タイルセット `.bin` 生成 |
| [../tool/png_to_rgb565bin.py](../tool/png_to_rgb565bin.py) | PNG → RGB565 `.bin` 変換 |

API リファレンス: [LUA_API.md](../LUA_API.md)
