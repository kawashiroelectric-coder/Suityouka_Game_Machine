# Lua プレビューエミュレータ (lua_preview)

PC 上で Suityouka Game Machine 向け Lua ゲームを **320×240** で対話プレビューするツールです。  
実機の `game_init` / `game_update` / `game_draw` ループと、実機と同じ描画の流れ（下記）を再現します。

## 描画の流れ（実機と同じ）

実機は `game_draw` を **1 回だけ録画**し、C 側で 20px バンドごとに再生します（このとき `band_index()` は 0、`band_top()` は 0、`band_bottom()` は 240、`rect_in_band()` は常に true）。
次の場合だけ、そのフレームは `game_draw` を **バンドごとに 12 回**呼ぶ従来の方法になります。プレビューも同じ判定をします。

- 録画が 12KB を超えた
- 録画できない API を使った（回転・縮小・透過色つきの `draw_image_affine`、`draw_image_xform`、`draw_tilemap`）
- `set_draw_mode("layers")` のとき

画面下の行に、FPS とそのフレームの描画方法（`録画 1.2KB/12KB` / `バンド描画（理由）`）を表示します。
`バンド描画` が続くと実機では Lua の描画処理が 12 倍になるので、重いゲームでは注意してください。

録画は実機と同じく「あとで再生」なので、`set_font_scale` の倍率は `game_draw` 終了時点のものが全部の文字に使われます（実機と同じ見え方）。

## 速度

描画は numpy で行い、実機相当の 30 FPS を十分に超えて動きます（参考: 各ゲームで 1 フレーム 2〜12ms。旧版は 50〜500ms）。
既定では実機に合わせて **30 FPS** に制限しています。`--fps-limit 60` や `--fps-limit 0`（無制限）で変更できます。

| オプション | 意味 |
|------------|------|
| `--scale N` | 表示倍率（1〜4、既定 2） |
| `--fps-limit N` | FPS 上限（既定 30。0 = 無制限） |
| `--band` | 毎フレーム必ずバンドごとに 12 回 `game_draw` を呼ぶ（実機の録画失敗時の見え方の確認用） |
| `--watchdog-ms N` | Lua 1 フレームの上限 ms |

## 要件

- Python 3.10+
- 依存: `pip install -r tool/lua_preview/requirements.txt`

## 起動

プロジェクトルートから:

```bash
pip install -r tool/lua_preview/requirements.txt

# STG
python tool/lua_preview/preview.py games/stg/stg.lua

# タイル横スクロール（layers モード）
python tool/lua_preview/preview.py games/tile_test/tile_test.lua

# ビジュアルノベル
python tool/lua_preview/preview.py games/visual_novel/visual_novel.lua

# 将棋
python tool/lua_preview/preview.py games/Shogi/Shogi.lua

# Run!Yamame
python tool/lua_preview/preview.py "games/Run!Yamame/Run!Yamame.lua"

# 画面 3 倍（960×720）
python tool/lua_preview/preview.py games/stg/stg.lua --scale 3
```

## キー割り当て

| キー | ボタン index | 実機相当 |
|------|-------------|----------|
| ← → ↑ ↓ | 2 / 0 / 1 / 3 | LEFT / RIGHT / UP / DOWN |
| Z | 4 | OP_LEFT |
| X | 5 | OP_RIGHT |
| S | 6 | FAR |
| A | 7 | NEAR |
| Esc | — | エミュ終了 |

`machine.jump_pressed()` は UP / OP_RIGHT / RIGHT / DOWN / NEAR と同じ条件です。

## 実装済み API（概要）

- 描画: `clear`, `fill_rect`, `fill_rect_alpha`, `fill_rects`, `draw_line`, `draw_circle`, `fill_circle`, `text`
  - **`machine.text(x, y, str [, fg [, bg]])`**: `bg` 省略時は**透明背景**（実機と同じ）。矩形背景が必要なときだけ第5引数を指定
- バンド: `band_index`, `band_count`, `band_top`, `band_bottom`, `band_height`, `rect_in_band`
- 画像: `load_image`, `draw_image`, `draw_image_keyed`, `draw_image_affine`, `draw_image_xform`, `free_image`, `image_size`, スプライト別名
- タイル: `draw_tilemap`, `set_draw_mode`, レイヤー API 一式（`layers` モード）
- ストリーム: `draw_bg_stream`, `draw_vn_stream`, `draw_bw_stream`, `draw_bw_pack`
- フォント: `load_font`（MISF v1 `.bin`）, `font_height`, `font_advance`, `set_font_scale`
- パス: `script_dir`, `resolve_path`, `file_exists`, `load_return`
- セーブ: `save_data` / `load_data` → ゲームフォルダ内 `_preview_save/` に保存
- 入力: `pressed`, `jump_pressed`
- その他: `width`, `height`, `time_ms`, `rgb`, `heap_*`
- 音声 API はスタブ（無音）
- `fill_rects` は実機と同じ `{x, y, w, h, color}`（位置指定）。旧プレビューの `{x=, y=, ...}` も受け付けます
- `draw_tilemap` の引数順は実機と同じ `(id, map_x, map_y, cols, rows, tile_w, tile_h, sheet_cols, data)`

## パス解決

ゲーム Lua と同じディレクトリを `machine.script_dir()` 相当の基準にします。  
相対パス `img/player.bin` は `Test_Lua/stg/img/player.bin` のように解決されます。

## 制限

- 依存に **numpy** が加わりました（`pip install -r tool/lua_preview/requirements.txt`）。
- Lua 実行は **lupa (Lua 5.5)**。`//` はネイティブ対応。古い LuaJIT 向けには起動時変換も残しています。
- 実機との描画差異や音声未再現はあります。最終確認は実機推奨です。
- `machine.present` / `set_present_mode` はノーオペです。
- 将棋 AI など **1 フレームが重い処理**でも、プレビューは Lua instruction hook で
  ウィンドウイベントを処理し、「応答なし」で落ちにくくしています。
  重い最中でも **Esc / ウィンドウ閉じる**で中断できます。
  強制打ち切りしたい場合: `--watchdog-ms 3000` など。

## ファイル構成

| ファイル | 役割 |
|----------|------|
| `preview.py` | エントリ（pygame メインループ） |
| `machine_api.py` | `machine.*` モック |
| `framebuffer.py` | RGB565 描画（numpy）。全画面 / バンドの描画先 |
| `font8x8.py` | フォント未読込時の 8x8 ASCII |
| `tile_layers.py` | layers モード合成 |
| `bw_stream.py` | 1 ビット白黒ストリーム（`draw_bw_stream`） |
| `font_misf.py` | MISF フォント |
| `lua_compat.py` | Lua 5.4 → LuaJIT 前処理 |
