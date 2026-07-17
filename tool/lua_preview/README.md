# Lua プレビューエミュレータ (lua_preview)

PC 上で Suityouka Game Machine 向け Lua ゲームを **320×240** で対話プレビューするツールです。  
実機の `game_init` / `game_update` / `game_draw` ループと **20px バンド描画** を再現します。

## 要件

- Python 3.10+
- 依存: `pip install -r tool/lua_preview/requirements.txt`

## 起動

プロジェクトルートから:

```bash
pip install -r tool/lua_preview/requirements.txt

# STG
python tool/lua_preview/preview.py Test_Lua/stg/stg.lua

# タイル横スクロール（layers モード）
python tool/lua_preview/preview.py Test_Lua/tile_test/tile_test.lua

# ビジュアルノベル
python tool/lua_preview/preview.py Test_Lua/visual_novel/visual_novel.lua

# Bad Apple!!（1 ビット差分ストリーム）
python tool/lua_preview/preview.py Test_Lua/bad_apple/bad_apple.lua

# 画面 3 倍（960×720）
python tool/lua_preview/preview.py Test_Lua/stg/stg.lua --scale 3
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

## パス解決

ゲーム Lua と同じディレクトリを `machine.script_dir()` 相当の基準にします。  
相対パス `img/player.bin` は `Test_Lua/stg/img/player.bin` のように解決されます。

## 制限

- Lua 実行は **lupa (Lua 5.5)**。Lua 5.4 の `//` 除算は古い環境向けに起動時変換も実施します。
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
| `framebuffer.py` | RGB565 + バンド描画 |
| `tile_layers.py` | layers モード合成 |
| `bw_stream.py` | 1 ビット白黒ストリーム（`draw_bw_stream`） |
| `font_misf.py` | MISF フォント |
| `lua_compat.py` | Lua 5.4 → LuaJIT 前処理 |
