# タイルレイヤー表示テスト（layers モード）

GBA 風タイルレイヤー合成（`machine.set_draw_mode("layers")`）の動作確認用サンプルです。  
地面・雲の **2 レイヤーが自動スクロール**し、手前にプレイヤースプライトを描画します。

## フォルダ構成

```
Test_Lua/tile_test/
├── layers_test.lua      … ゲーム本体（ここを編集）
├── tiles/
│   ├── tiles.bin        … 128×128 タイルセット（16×16 タイル × 8 列）
│   └── player.bin       … 16×16 プレイヤースプライト
└── README.md

Test_Lua/（親フォルダ）
└── generate_assets.py   … tiles/*.bin 再生成スクリプト
```

## SD カードへの配置

`layers_test.lua` と `tiles/` は **SD ルート直下** に置きます（Lua 内パスが `tiles/...` のため）。

```
/（SD ルート）
├── layers_test.lua
└── tiles/
    ├── tiles.bin
    └── player.bin
```

### 起動方法

ファイルエクスプローラから **`layers_test.lua`** を選択して起動します（推奨）。

SD ルート自動実行（`main.lua` → `game.lua` → `boot.lua` → 最初の `.lua`）を使う場合は、ルートに `layers_test.lua` を置き、他の `.lua` より先に検出されないよう注意してください。

確実に試す場合:

- `layers_test.lua` と `tiles/` を SD ルートへコピー
- または `game.lua` にリネーム（`main.lua` より優先度は低い）

> `tiles/` だけでは起動しません。**`layers_test.lua` + `tiles/*.bin`** の両方が必要です。

## 操作

| 入力 | 動作 |
|------|------|
| RIGHT（ボタン 0） | プレイヤー右移動 |
| DOWN（ボタン 3） | プレイヤー左移動 |
| ジャンプ系ボタン | 終了（`game_update` で `return true` を有効にした場合） |

画面表示のヒント文字: `R:RIGHT D:LEFT`

## 表示の仕組み

| 要素 | 内容 |
|------|------|
| レイヤー 0 | 地面タイルマップ（横スクロール・遅め） |
| レイヤー 1 | 雲・茂み（透過、横スクロール・速め） |
| `game_draw` | プレイヤースプライト + テキスト |

`layers` モードでは **`machine.clear` は使いません**。背景はレイヤー合成で描かれます。

---

## 編集のしかた

### 1. マップ（タイル配置）を変える

`layers_test.lua` の **`build_ground_map()`** と **`build_cloud_map()`** を編集します。

タイル番号は **1 始まり** です。**0 以下 = 空（描画しない）**。

```lua
-- 地面レイヤーの例（build_ground_map 内）
if row >= MAP_ROWS - 2 then
    m[idx] = 4          -- 下 2 行を「水」(タイル 4)
elseif (col + row) % 11 == 0 then
    m[idx] = 2          -- 模様付き草地
else
    m[idx] = 1          -- 通常の草地
end
```

```lua
-- 雲レイヤーの例（build_cloud_map 内）
if row <= 2 and col % 6 == 1 then
    m[idx] = 5          -- 雲（透過）
else
    m[idx] = 0          -- 空
end
```

**マップサイズ**を変えるときは、次を **同じ値** に揃えてください。

| 定数 / 設定 | 場所 |
|-------------|------|
| `MAP_COLS`, `MAP_ROWS` | ファイル先頭 |
| `map_cols`, `map_rows` | `machine.set_layer(0/1, { ... })` |
| 配列の要素数 | `MAP_COLS * MAP_ROWS` 個（`build_*_map` の戻り値） |

### 2. 使えるタイル番号

`generate_assets.py` が生成するタイルセット（`tiles.bin`）の対応:

| 番号 | 内容 | 透過 | 主な用途 |
|------|------|------|----------|
| 1 | 草地 | なし | 地面 |
| 2 | 草地（模様） | なし | 地面 |
| 3 | 土 | なし | 地面 |
| 4 | 水 | なし | 地面（下段など） |
| 5 | 雲 | **あり** | 前景レイヤー |
| 6 | 茂み | **あり** | 前景レイヤー |
| 7 | レンガ | なし | 地面 |
| 8 | 星 | なし | 予備 |
| 9〜 | グレー枠 | あり | 未使用枠（プレースホルダ） |

透過タイルはキー色 **マゼンタ `0xF81F`** です。レイヤー側で `transparent = true` を指定します（レイヤー 1 は設定済み）。

### 3. スクロール・プレイヤー位置

`game_update` 内の定数・式を変更します。

```lua
scroll_ground = scroll_ground + dt / 8   -- 小さいほど速い
scroll_cloud  = scroll_cloud + dt / 3    -- 雲は地面より速く

player_y = H - TILE * 4 + math.floor(math.sin(elapsed / 400) * 4)  -- 上下の揺れ
```

プレイヤーの左右移動は `player_x` と `machine.pressed(0)` / `machine.pressed(3)` の処理です。

### 4. レイヤーを追加・変更する

最大 **4 レイヤー**（0〜3）使えます（`machine.layer_count()`）。

```lua
machine.set_layer(2, {
    tileset = sheet_id,
    tile_w = TILE,
    tile_h = TILE,
    sheet_cols = SHEET_COLS,   -- タイルセット画像の横タイル数（8）
    map_cols = MAP_COLS,
    map_rows = MAP_ROWS,
    map_x = 0,
    map_y = 0,
    scroll_x = 0,
    scroll_y = 0,
    enabled = true,
    transparent = true,        -- 透過レイヤーにする場合
})
machine.set_layer_tiles(2, build_my_map())
```

詳細 API は [LUA_API.md](../../LUA_API.md) の「描画モード・タイルレイヤー」を参照してください。

### 5. 背景色・表示モード

```lua
machine.set_draw_mode("layers")
machine.set_layer_backdrop(machine.rgb(25, 35, 70))
```

`set_draw_mode("direct")` に戻すと通常の `clear` + 自前描画モードになります。

---

## 画像（タイル・スプライト）の再生成

### 同梱スクリプトで生成

```bash
cd Test_Lua
python generate_assets.py
```

出力先は `Test_Lua/tiles/` です。生成後、**`tile_test/tiles/` および SD の `/tiles/`** へコピーしてください。

| ファイル | サイズ | 内容 |
|----------|--------|------|
| `tiles.bin` | 128×128 | 16×16 タイル × 8 列のシート |
| `player.bin` | 16×16 | プレイヤー（マゼンタ透過） |

`generate_assets.py` の `TILE_BUILDERS` リストを編集すると、タイル 1〜8 の見た目を変更できます。

### 自作 PNG から変換

```bash
pip install pillow
python ../../tool/png_to_rgb565bin.py my_tile.png --resize 16x16 -o tiles/my_tile.bin
python ../../tool/png_to_rgb565bin.py my_sheet.png --resize 128x128 -o tiles/tiles.bin
```

透過が必要なスプライト・雲タイルは **背景を #FF00FF（マゼンタ）** にしてください。

`layers_test.lua` の読み込みサイズも一致させます。

```lua
sheet_id = machine.load_sprite("tiles/tiles.bin", 128, 128)
player_id = machine.load_sprite("tiles/player.bin", 16, 16)
```

---

## よくあるトラブル

| 症状 | 原因・対処 |
|------|------------|
| 真っ暗 / 何も出ない | `tiles/tiles.bin` が SD に無い、またはパス不一致 |
| タイルがずれる | `sheet_cols`（8）と `tiles.bin` の横タイル数が不一致 |
| マップがおかしい | `map_cols * map_rows` と配列長が不一致 |
| 透過部分が黒 | レイヤーに `transparent = true` 未設定、またはキー色がマゼンタでない |
| 起動しない | SD ルートに `layers_test.lua` が無い、または `main.lua` 等が先に実行されている |

シリアルログに `layers_test: load tiles/tiles.bin failed` と出た場合は、SD 上の `tiles/` 配置を確認してください。

## カスタマイズの入口（layers_test.lua 先頭）

| 定数 | 意味 |
|------|------|
| `TILE` | 1 タイルのピクセルサイズ（16） |
| `MAP_COLS`, `MAP_ROWS` | マップのタイル数（40×15） |
| `SHEET_COLS` | タイルセット画像の横タイル数（8） |

## 関連ドキュメント

- [LUA_API.md](../../LUA_API.md) … `set_layer` / `set_layer_tiles` / `set_draw_mode`
- [visual_novel/README.md](../visual_novel/README.md) … 別サンプル（画像 + テキスト VN）
- [Test_Lua/README.md](../README.md) … Test_Lua サンプル一覧
