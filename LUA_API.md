# Lua API リファレンス（Suityouka Game Machine）

SD カード上の Lua ゲームから使える API の一覧です。  
実装: `lib/lua_interpreter/`（`lua_api_machine.cpp` / `lua_api_draw.cpp` / `lua_api_audio.cpp`、`lua_interpreter.cpp`、`vn_stream_compose.cpp` 等）。

標準 Lua 5.4 ライブラリ（`luaL_openlibs`）も利用可能です（`string`, `math`, `table` など）。

---

## ゲーム用コールバック（ホストが呼ぶ）

`runGameLoopFromSd()` でスクリプトを読み込んだあと、次のグローバル関数があればホストが呼び出します。

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `game_init()` | なし | なし | ゲーム開始時に **1 回だけ** 呼ばれる。スコア・オブジェクトの初期化に使う。 |
| `game_update(dt)` | `dt` … 前回からの経過時間（ミリ秒、整数） | `boolean` | 毎フレーム **1 回** 呼ばれる。物理・入力・スコア更新。**`true` を返すとループ終了**（ゲーム終了）。`dt` は最大 100 にクランプされる。 |
| `game_draw()` | なし | なし | 画面描画。バンド（ラインバッファ）方式のため **1 フレームあたり複数回**（画面を横帯に分割した回数）呼ばれる。論理座標は常に `0 .. width()-1`, `0 .. height()-1`。 |

### 描画ループの流れ（ホスト側）

1. `game_update(dt)` を 1 回実行  
2. 画面を高さ 20 ドットのバンドに分割（例: 240 行 → 12 バンド。`GameConfig::BUFFER_HEIGHT`）  
3. 各バンドで `beginBand` → **`game_draw()`** → LCD へ DMA 転送  
4. フレーム末尾で転送完了待ち・SD ストリーム用ファイルを閉じる  

そのため **`machine.present()` は呼ばなくてよい**（呼んでも無視される）。

### 描画モード（`machine.set_draw_mode`）

| モード | 文字列 | 説明 |
|--------|--------|------|
| **直接描画**（既定） | `"direct"` | 従来どおり。`game_draw` 内で `clear` / `fill_rect` / `draw_image` / `draw_tilemap` 等を呼ぶ。 |
| **レイヤー合成** | `"layers"` | GBA 風。各バンド描画前にホストが **タイル背景レイヤー（最大 4 層）** を下から順に合成し、その上に `game_draw` でスプライト等を描く。**`game_draw` 内で `machine.clear()` は呼ばない**（背景が消える）。 |

`game_init` などで一度 `machine.set_draw_mode("layers")` を宣言する。

毎フレーム `machine.clear()` してから全体を描き直す方式（`dino.lua` / `stg.lua`）は **`direct` モード**向け。

---

## グローバル関数

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `print(...)` | 任意（タブ区切りで連結） | なし | 引数を文字列化し **USB シリアル（stdout）** に出力。改行付き。 |
| `sleep_ms(ms)` | `ms` … 待ち時間（ミリ秒、0 未満は 0） | なし | 指定ミリ秒だけブロックして待つ。 |

---

## `machine` テーブル

すべて `machine.関数名` で呼びます。

### テキスト・画面サイズ

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.text(x, y, str [, fg [, bg]])` | `x`, `y` … 左上座標（ピクセル）<br>`str` … 文字列（UTF-8）<br>`fg` … 前景色（省略時: 白）<br>`bg` … 背景色（省略時: 黒） | なし | 8×8 テキスト描画。`machine.load_font` 済みなら **UTF-8 日本語（美咲サブセット）**、未 load 時は ASCII のみ。 |
| `machine.load_font(path)` | SD 上の MISF v1 `.bin`（`generate_font.py` 生成） | `true` / `nil`, errmsg | 美咲フォントサブセットを読み込み、`machine.text` を UTF-8 対応にする。 |
| `machine.font_loaded()` | なし | `boolean` | フォント読込済みか。 |
| `machine.font_height()` | なし | `integer` | フォント行高（通常 **8**）。 |
| `machine.width()` | なし | `integer` | 論理画面幅（ピクセル）。通常 **320**（`GameConfig::SCREEN_WIDTH`）。 |
| `machine.height()` | なし | `integer` | 論理画面高さ（ピクセル）。通常 **240**（`GameConfig::SCREEN_HEIGHT`）。 |

### 図形描画

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.clear(color)` | `color` … 塗りつぶし色 | なし | **現在描画中のバンド**を単色で塗る。全面クリアは毎バンドで呼ぶか、各 `fill_rect` で賄う。 |
| `machine.fill_rect(x, y, w, h, color)` | 矩形の位置・サイズ・色 | なし | クリッピング付き矩形塗りつぶし。論理座標。バンド外の部分は自動でクリップされる。 |
| `machine.fill_rects(rects)` | `rects` … 配列。各要素は `{x, y, w, h, color}` のテーブル（`color` は RGB565 整数） | なし | 複数矩形をまとめて塗る（最大 64 個ずつバッチ処理）。 |
| `machine.draw_line(x0, y0, x1, y1, color)` | 始点・終点・色 | なし | クリッピング付き直線（Bresenham）。 |
| `machine.draw_circle(cx, cy, radius, color)` | 中心・半径・色 | なし | クリッピング付き円の輪郭。 |
| `machine.fill_circle(cx, cy, radius, color)` | 中心・半径・色 | なし | クリッピング付き塗りつぶし円。 |

### バンド（ラインバッファ）情報

`game_draw()` は 1 フレームあたり **バンド数**（例: 240÷20＝12）回呼ばれます。重い描画は `rect_in_band` でスキップできます。

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.band_index()` | なし | `integer` | 現在のバンド番号（0 始まり）。 |
| `machine.band_count()` | なし | `integer` | 1 フレームのバンド総数。 |
| `machine.band_top()` | なし | `integer` | 現在バンドの画面上端 y（含む）。 |
| `machine.band_bottom()` | なし | `integer` | 現在バンドの画面下端 y（**含まない**）。 |
| `machine.band_height()` | なし | `integer` | 1 バンドの行数（通常 **20**）。 |
| `machine.rect_in_band(y, h)` | 矩形の上端 y と高さ h | `boolean` | 論理矩形が現在バンドと交差すれば `true`。 |

### 描画モード・タイルレイヤー（GBA 風）

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.set_draw_mode(mode)` | `"direct"` または `"layers"` | なし | 描画方式を切り替え（既定: `"direct"`）。 |
| `machine.draw_mode()` | なし | `string` | 現在の描画モード。 |
| `machine.layer_count()` | なし | `integer` | タイル背景レイヤー数（**4**）。0=最背面。 |
| `machine.set_layer_backdrop(color)` | 塗りつぶし色 | なし | `layers` モード時、各バンド合成前の背景色（既定: 黒）。 |
| `machine.set_layer(index, config)` | レイヤー 0〜3、設定テーブル | なし | タイルレイヤー定義。下記 `config` 参照。 |
| `machine.set_layer_scroll(index, sx, sy)` | レイヤー番号、スクロール px | なし | パララックス用スクロール更新。 |
| `machine.set_layer_tiles(index, data)` | レイヤー番号、タイル番号配列 | なし | タイルマップデータ（**1 始まり**、0 以下=空）。`map_cols*map_rows` 個。 |
| `machine.clear_layer(index)` | レイヤー番号 | なし | 指定レイヤーを解除。 |
| `machine.clear_all_layers()` | なし | なし | 全レイヤーを解除（背景色設定は維持）。 |

**`set_layer` の config テーブル**

| キー | 必須 | 意味 |
|------|------|------|
| `tileset` | ○ | `load_image` / `load_sprite` の ID（タイルセット） |
| `tile_w`, `tile_h` | ○ | 1 タイルのピクセルサイズ |
| `sheet_cols` | ○ | タイルセット画像内の横タイル数 |
| `map_cols`, `map_rows` | ○ | タイルマップの幅・高さ（タイル数） |
| `map_x`, `map_y` | — | マップ左上の画面座標（既定 0） |
| `scroll_x`, `scroll_y` | — | スクロール量 px（既定 0） |
| `enabled` | — | `false` でレイヤー無効（既定 `true`） |
| `transparent` | — | 透過色。数値=RGB565、`true`=マゼンタ `0xF81F`、省略=不透過 |

`layers` モードの 1 フレームの流れ: 各バンドで **レイヤー合成 → `game_draw()`（スプライト等）→ LCD 転送**。

### 色

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.rgb(r, g, b)` | `r`, `g`, `b` … 0〜255 | `integer` | RGB888 から **RGB565** 色値を生成。`fill_rect` / `clear` / `text` の色引数に使う。 |
| （色引数の別形式） | 描画系の `color` / `fg` / `bg` | — | **1 引数**: RGB565 の整数（`machine.rgb` の戻り値）。<br>**3 引数**: `r, g, b`（0〜255）を続けて指定してもよい（内部で `rgb` 変換）。 |

### 入力（ボタン）

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.pressed(button_index)` | `button_index` … 0〜7 | `boolean` | 指定ボタンが **押されている間** `true`。 |
| `machine.jump_pressed()` | なし | `boolean` | ジャンプ／決定用として扱うボタンのいずれかが押されていれば `true`（内部インデックス: **1, 5, 0, 3, 7** = UP, OP_RIGHT, RIGHT, DOWN, NEAR）。 |

#### ボタンインデックス（`config.hpp` / PCA9539 Port0）

| index | 定数名 | おおよその配置 |
|-------|--------|----------------|
| 0 | `BUTTON_RIGHT` | 十字キー 右 |
| 1 | `BUTTON_UP` | 十字キー 上 |
| 2 | `BUTTON_LEFT` | 十字キー 左 |
| 3 | `BUTTON_DOWN` | 十字キー 下 |
| 4 | `BUTTON_OP_LEFT` | 操作ボタン 左 |
| 5 | `BUTTON_OP_RIGHT` | 操作ボタン 右 |
| 6 | `BUTTON_FAR` | FAR |
| 7 | `BUTTON_NEAR` | NEAR |

### 時間

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.time_ms()` | なし | `integer` | Pico 起動からの経過ミリ秒。 |

### 音声（PCM5102 I2S / Core 1）

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.play_tone(freq_hz, duration_ms)` | 周波数（Hz）、長さ（ミリ秒） | `boolean` | サイン波を BGM/SE に加算。新しいトーンは前のトーンのみ置き換え。 |
| `machine.play_wav(path)` | SD 上の WAV パス | 成功: `true`<br>失敗: `nil`, `errmsg` | **BGM** 用。16bit PCM WAV を SD からストリーミング再生。SE は止めない。22050Hz 推奨。 |
| `machine.play_se(path)` | SD 上の WAV パス | 成功: `true`<br>失敗: `nil`, `errmsg` | **SE** 用。16bit PCM WAV を RAM 載せで BGM に加算。**最大 8 系統**、超過時は最も古い SE を上書き。1 ファイル最大 **32KB**。 |
| `machine.stop_sound()` | なし | なし | BGM・SE・トーンをすべて停止。 |
| `machine.set_volume(vol)` | `vol` … 0.0〜1.0 | なし | マスター音量。 |
| `machine.heap_used()` | なし | `integer` | 動的ヒープ使用量（バイト）。 |
| `machine.heap_available()` | なし | `integer` | 残り確保可能バイト（予算−使用中−予備）。 |

動的確保は `config.hpp` の `HeapConfig::BUDGET_BYTES`（既定 **256KB**、`RESERVE_BYTES` 8KB を除いた分が実質上限）を超えないよう拒否されます。

### 画面更新（互換用・実質 no-op）

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.set_present_mode(mode)` | `"full"` または `"partial"`（`"dirty"` も可） | なし | **現在は無視**（バンド描画でホストが転送するため）。 |
| `machine.present([mode])` | 省略可。省略時は内部デフォルト | なし | **現在は無視**。フレーム表示はホストが `game_draw` 後に自動で行う。 |

### 画像（SD 上の RGB565 バイナリ）

事前に `tool/png_to_rgb565bin.py` などで **RGB565 リトルエンディアン・生バイナリ**（`.bin`）を用意し、SD から読み込みます。

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.load_image(path, width, height)` | `path` … SD 上のパス（例: `"img/logo.bin"`）<br>`width`, `height` … 画像のピクセルサイズ | 成功: `id`（整数）<br>失敗: `nil`, `errmsg` | ファイルを読み込み RAM スロットに保持。最大 **16 枚**、1 枚あたり最大 **200KB**。 |
| `machine.draw_image(id, x, y [, sx, sy, sw, sh])` | `id` … `load_image` の戻り値<br>`x`, `y` … 描画先左上<br>省略時: 画像全体<br>7 引数時: ソース矩形 `(sx,sy,sw,sh)` を指定して部分転写 | なし | RGB565 画像を画面に描画（クリッピング付き）。 |
| `machine.draw_image_keyed(id, x, y [, key_color] \| id, x, y, sx, sy, sw, sh [, key_color])` | 透過色（RGB565）のピクセルをスキップして描画。`key_color` 省略時は **0xF81F**（マゼンタ） | なし | 立ち絵など透過 PNG を `.bin` 化した画像向け。 |
| `machine.draw_bg_stream(path, x, y, width, height)` | SD 上の RGB565 `.bin` を **現在バンド分だけ** 読み込んで描画 | `boolean` | 背景など大きい画像向け。RAM に全枚載せない。次バンド分を **SD 先読み** し LCD DMA と重ねる（VN 背景 320×168 等）。 |
| `machine.draw_vn_stream(config)` | 設定テーブル `config` に従い、背景 + 立ち絵（最大 **2 枚**）を SD から **現在バンド分だけ** 合成描画 | `boolean` | VN 向け。`load_image` なしで背景・立ち絵を SD ストリーム合成。下記 `config` 参照。 |
| `machine.free_image(id)` | `id` | なし | 画像スロットを解放。 |
| `machine.image_size(id)` | `id` | `width`, `height` | 読み込み済み画像のサイズを返す。無効 `id` はエラー。 |
| `machine.load_sprite(...)` | `load_image` と同じ | 同左 | 画像 API の別名（スプライト／タイルセット用）。 |
| `machine.draw_sprite(...)` | `draw_image` と同じ | なし | 同上。 |
| `machine.draw_sprite_keyed(...)` | `draw_image_keyed` と同じ | なし | 同上。 |
| `machine.free_sprite(id)` | `free_image` と同じ | なし | 同上。 |
| `machine.draw_tilemap(id, map_x, map_y, cols, rows, tile_w, tile_h, sheet_cols, data)` | `id` … タイルセット画像<br>`map_x`, `map_y` … マップ左上<br>`cols`, `rows` … マップのタイル数<br>`tile_w`, `tile_h` … 1 タイルのピクセルサイズ<br>`sheet_cols` … タイルセット画像内の横タイル数<br>`data` … **1 始まり**のタイル番号配列（行優先、最大 2048 セル）。**負の値はスキップ** | なし | タイルマップを描画。現在バンドと交差する行のみ処理。 |

**ファイルサイズ**: `width * height * 2` バイト（RGB565）と一致している必要があります。

**`draw_vn_stream` の config テーブル**

| キー | 必須 | 意味 |
|------|------|------|
| `bg` | — | 背景レイヤー。省略可。テーブル: `{ path, x, y, w, h }` |
| `chars` | — | 立ち絵レイヤー配列（**最大 2 要素**）。後から書いた方が手前。各要素: `{ path, x, y, w, h [, key] [, keyed] }` |

- `path` … SD 上の `.bin` パス（実行中スクリプト基準の相対パス可）
- `key` … 透過色 RGB565（省略時 **0xF81F**）
- `keyed` … 透過を有効にするか（立ち絵は通常 `true`。背景は内部で非透過）

```lua
machine.draw_vn_stream({
  bg = { path = "images/bg/classroom.bin", x = 0, y = 0, w = 320, h = 168 },
  chars = {
    { path = "images/chars/hero.bin", x = 8,  y = 72, w = 128, h = 168, key = 0xF81F },
    { path = "images/chars/mysterious.bin", x = 184, y = 72, w = 128, h = 168 },
  },
})
```

### SD パス・外部 Lua 読み込み

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.load_return(path)` | SD 上の `.lua` パス | 成功: `return` 値 1 個<br>失敗: `nil`, errmsg | 別ファイルを読み込み **1 回だけ** 実行して戻り値を得る（`scenario.lua` / `assets.lua` 向け）。 |
| `machine.script_dir()` | なし | `string` | 実行中 `.lua` のディレクトリ（末尾 `/` 付き）。未実行時は `"/"`。 |
| `machine.resolve_path(rel)` | 相対パス | `string` | スクリプトディレクトリ基準で SD 絶対パスに解決。 |

### セーブデータ（SD 上の Lua リテラル）

ゲーム進行をテーブルとして SD に保存・読み込みします。ファイル形式は `return { ... }`（先頭に `-- game_machine save v1` コメント）。

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.save_data(path, table)` | `path` … SD 上の相対パス<br>`table` … 保存する Lua テーブル | 成功: `true`<br>失敗: `nil`, errmsg | テーブルをシリアライズして SD に書き込み。**最大 16KB**。 |
| `machine.load_data(path)` | `path` … SD 上の相対パス | 成功: テーブル 1 個<br>失敗: `nil`, errmsg | セーブファイルを読み込みテーブルを返す。 |
| `machine.file_exists(path)` | `path` … SD 上の相対パス | `boolean` | ファイルが存在すれば `true`。 |

**保存可能な型**: `nil` / `boolean` / `number` / `string` / `table`（ネスト最大 **16** 段）。  
**不可**: `function` / `userdata` など。キーは **整数** または **文字列** のみ。

**例**:

```lua
local ok, err = machine.save_data("save.dat", {
  score = score,
  x = player_x,
  y = player_y,
})
if not ok then print("save failed:", err) end

local data, err = machine.load_data("save.dat")
if data then
  score = data.score or 0
  player_x = data.x or 0
  player_y = data.y or 0
end
```

**タイルマップ例**（2×2 マップ、16×16 タイル、シート横 4 タイル）:

```lua
local tiles = {0, 1, 2, 3}
machine.draw_tilemap(sheet_id, 0, 0, 2, 2, 16, 16, 4, tiles)
```

---

## 実行の種類

| ホスト処理 | 説明 |
|------------|------|
| `runGameLoopFromSd(path)` | ゲームループ（`game_init` / `game_update` / `game_draw`）で実行。 |
| `runScriptFromSd(path)` | スクリプトを **1 回だけ** 読み込んで実行（上記コールバックは使わない）。 |
| `executeOnSdRoot()` | SD ルートの `main.lua` → `game.lua` → `boot.lua` → 最初の `.lua` の順で 1 本実行。 |

スクリプトサイズ上限の目安: **48KB**（`LuaInterpreter::kDefaultMaxScriptBytes`）。

---

## 使用例（最小）

```lua
function game_init()
end

function game_update(dt)
    if machine.jump_pressed() then
        return true  -- 終了
    end
    return false
end

function game_draw()
    machine.clear(machine.rgb(0, 0, 0))
    machine.fill_rect(10, 10, 40, 40, machine.rgb(255, 0, 0))
    machine.text(8, 8, "HELLO", machine.rgb(255, 255, 255), machine.rgb(0, 0, 0))
end
```

### レイヤー合成モードの例

```lua
function game_init()
    machine.set_draw_mode("layers")
    machine.set_layer_backdrop(machine.rgb(20, 40, 80))

    sheet = machine.load_sprite("tiles.bin", 128, 128)
    machine.set_layer(0, {
        tileset = sheet, tile_w = 16, tile_h = 16, sheet_cols = 8,
        map_cols = 20, map_rows = 15, scroll_x = 0, transparent = true,
    })
    machine.set_layer_tiles(0, {1,1,1,1, 2,2,2,2})  -- map_cols*map_rows 個

    machine.set_layer(1, {
        tileset = sheet, tile_w = 16, tile_h = 16, sheet_cols = 8,
        map_cols = 20, map_rows = 15, scroll_x = 32, transparent = true,
    })
    -- 雲レイヤー用タイルデータも set_layer_tiles(1, ...) で設定
end

function game_update(dt)
    return false
end

function game_draw()
    -- 背景はホストが合成済み。スプライトだけ draw_image
    machine.draw_image(player_id, px, py)
end
```

---

## 関連ファイル

| ファイル | 内容 |
|----------|------|
| `lib/lua_interpreter/lua_interpreter.cpp` | ゲームループ・画像スロット・`draw_bg_stream` |
| `lib/lua_interpreter/lua_api_machine.cpp` | `machine.*` 登録・パス API |
| `lib/lua_interpreter/lua_api_draw.cpp` | 描画・バンド・`draw_vn_stream` バインディング |
| `lib/lua_interpreter/vn_stream_compose.cpp` | VN 用 SD ストリーム合成 |
| `lib/lua_interpreter/bg_stream_util.cpp` | バンド単位 SD 行読み込み |
| `lib/lua_interpreter/lua_interpreter.hpp` | 画像スロット上限など |
| `config.hpp` | 画面サイズ・ボタン定義・ヒープ予算 |
| [lib/README.md](lib/README.md) | lib 配下の構成・依存関係 |
| `Test_Lua/visual_novel/` | VN サンプル（`draw_vn_stream` 使用） |
| `tool/png_to_rgb565bin.py` | PNG 等 → RGB565 `.bin` 変換 |
