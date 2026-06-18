# Star Hop（tile_test）

タイルレイヤー（`machine.set_draw_mode("layers")`）を使った横スクロールプラットフォームゲームです。  
星をすべて集めるとクリア。水に落ちるとゲームオーバー。

## フォルダ構成

```
Test_Lua/tile_test/
├── tile_test.lua       … ゲーム本体
├── tiles/
│   ├── tiles.bin       … 128×128 タイルセット（16×16 タイル × 8 列）
│   └── player.bin      … 16×16 プレイヤースプライト
└── README.md
```

## SD カードへの配置

他サンプルと同様、`/games/` 配下に置きます。

```
/games/tile_test/
├── tile_test.lua
└── tiles/
    ├── tiles.bin
    └── player.bin
```

メニューから **tile test**（フォルダ名）として起動できます。

## 操作

| 入力 | 動作 |
|------|------|
| LEFT（2） | 左移動 |
| RIGHT（0） | 右移動 |
| UP（1） / NEAR（7） | ジャンプ |
| ジャンプ系ボタン | クリア / ゲームオーバー後にリトライ |

## ルール

- マップ上の **星タイル（8）** をすべて集めるとクリア
- **水タイル（4）** に触れる、または画面下に落下するとゲームオーバー
- カメラはプレイヤーを追従。雲レイヤーはパララックススクロール

## 表示の仕組み

| 要素 | 内容 |
|------|------|
| レイヤー 0 | 地面・浮島・星・水 |
| レイヤー 1 | 雲・茂み（透過） |
| `game_draw` | プレイヤースプライト + UI テキスト |

`layers` モードでは `machine.clear` は使いません。

## タイル番号

`generate_assets.py` が生成するタイルセットの対応:

| 番号 | 内容 | 透過 | ゲーム内の扱い |
|------|------|------|----------------|
| 1 | 草地 | なし | 足場 |
| 2 | 草地（模様） | なし | 足場 |
| 3 | 土 | なし | 足場 |
| 4 | 水 | なし | 即死 |
| 5 | 雲 | あり | 背景装飾 |
| 6 | 茂み | あり | 背景装飾 |
| 7 | レンガ | なし | 浮島足場 |
| 8 | 星 | なし | 収集対象 |

透過タイルのキー色はマゼンタ `0xF81F` です。

## レベル編集

`tile_test.lua` の **`build_level_map()`** を編集します。

- `put(col, row, tile_id)` … 1 タイル配置
- `fill(col0, row0, w, h, tile_id)` … 矩形配置
- マップサイズを変える場合は `MAP_COLS` / `MAP_ROWS` と `set_layer` の `map_cols` / `map_rows` を揃えてください

## 画像の再生成

```bash
cd Test_Lua
python generate_assets.py
```

出力先は `tile_test/tiles/` です。

自作 PNG から変換する場合:

```bash
python tool/image_to_rgb565_bin.py my_sheet.png --resize 128x128 -o Test_Lua/tile_test/tiles/tiles.bin
python tool/image_to_rgb565_bin.py my_player.png --resize 16x16 -o Test_Lua/tile_test/tiles/player.bin
```

## 関連ドキュメント

- [LUA_API.md](../../LUA_API.md) … `set_layer` / `set_draw_mode`
- [Test_Lua/README.md](../README.md) … サンプル一覧
