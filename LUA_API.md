# Lua API リファレンス（Suityouka Game Machine）

SD カード上の Lua ゲームから使える API の一覧です。  
実装: `lib/lua_interpreter/lua_interpreter.cpp`（`registerLuaHostApi` / `runGameLoopFromSd`）。

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
2. 画面を高さ 20 ドットのバンドに分割（例: 240 行 → 12 バンド）  
3. 各バンドで `beginBand` → **`game_draw()`** → LCD へ DMA 転送  
4. フレーム末尾で転送完了待ち  
5. 約 16 ms スリープ（簡易フレームレート制御）  

そのため **`machine.present()` は呼ばなくてよい**（呼んでも無視される）。毎フレーム `machine.clear()` してから全体を描き直す方式（`dino.lua` / `stg.lua`）と相性が良い。

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
| `machine.text(x, y, str [, fg [, bg]])` | `x`, `y` … 左上座標（ピクセル）<br>`str` … 文字列<br>`fg` … 前景色（省略時: 白）<br>`bg` … 背景色（省略時: 黒） | なし | 8×8 ビットマップフォントでテキスト描画（背景付き）。色は下記「色の指定」を参照。 |
| `machine.width()` | なし | `integer` | 論理画面幅（ピクセル）。通常 **320**（`GameConfig::SCREEN_WIDTH`）。 |
| `machine.height()` | なし | `integer` | 論理画面高さ（ピクセル）。通常 **240**（`GameConfig::SCREEN_HEIGHT`）。 |

### 図形描画

| 関数 | 引数 | 戻り値 | 意味 |
|------|------|--------|------|
| `machine.clear(color)` | `color` … 塗りつぶし色 | なし | **現在描画中のバンド**を単色で塗る。全面クリアは毎バンドで呼ぶか、各 `fill_rect` で賄う。 |
| `machine.fill_rect(x, y, w, h, color)` | 矩形の位置・サイズ・色 | なし | クリッピング付き矩形塗りつぶし。論理座標。バンド外の部分は自動でクリップされる。 |
| `machine.fill_rects(rects)` | `rects` … 配列。各要素は `{x, y, w, h, color}` のテーブル（`color` は RGB565 整数） | なし | 複数矩形をまとめて塗る（最大 64 個ずつバッチ処理）。 |

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
| `machine.free_image(id)` | `id` | なし | 画像スロットを解放。 |
| `machine.image_size(id)` | `id` | `width`, `height` | 読み込み済み画像のサイズを返す。無効 `id` はエラー。 |

**ファイルサイズ**: `width * height * 2` バイト（RGB565）と一致している必要があります。

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

---

## 関連ファイル

| ファイル | 内容 |
|----------|------|
| `lib/lua_interpreter/lua_interpreter.cpp` | API 実装・ゲームループ |
| `lib/lua_interpreter/lua_interpreter.hpp` | 画像スロット上限など |
| `config.hpp` | 画面サイズ・ボタン定義 |
| `dino.lua` / `stg.lua` | サンプルゲーム |
| `tool/png_to_rgb565bin.py` | PNG 等 → RGB565 `.bin` 変換 |
