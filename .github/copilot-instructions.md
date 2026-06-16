<!--
リポジトリ固有の AI エージェント向け簡潔ガイド。
-->

# Copilot / AI エージェント用指示

Raspberry Pi **Pico 2W** 向け携帯ゲーム機ファームウェア。SD 上の **Lua 5.4** ゲームをファイルエクスプローラから実行する。

## 全体像

| パス | 役割 |
|------|------|
| `game_machine_main.cpp` | 起動・初期化・メインループ |
| `config.hpp` | GPIO / SPI / 画面サイズ / ヒープ予算（`CFG_*` を編集） |
| `lib/game_display/` | RGB565 バンド（ラインバッファ）描画 + DMA |
| `lib/lua_interpreter/` | Lua 実行・`machine.*` API |
| `lib/file_explorer/` | SD 上 `.lua` 選択 GUI |
| `lib/sd_service/` | FatFS マウント |
| `lib/sd_path_util/` | スクリプト基準パス解決 |
| `lib/ST7789/` | ST7789 LCD ドライバ |
| `Test_Lua/` | サンプル Lua ゲーム |

## 描画アーキテクチャ

- 論理画面 **320×240**、バンド高さ **20 行**（`GameConfig::BUFFER_HEIGHT`）
- 1 フレーム = 各バンドで `beginBand` → `game_draw()` → DMA 転送
- 大きな背景: `machine.draw_bg_stream`（SD から帯ごと読み込み）
- VN 背景+立ち絵: `machine.draw_vn_stream`（最大 2 立ち絵、SD ストリーム合成）
- GBA 風背景: `machine.set_draw_mode("layers")` + タイルレイヤー

## Lua API

- リファレンス: [LUA_API.md](../LUA_API.md)
- 実装分割: `lua_api_draw.cpp` / `lua_api_audio.cpp` / `lua_api_machine.cpp`
- VN 合成: `vn_stream_compose.cpp`, `bg_stream_util.cpp`

## ビルド

```powershell
mkdir build; cd build
cmake ..
cmake --build .
```

デバッグオーバーレイ（FPS / RAM）: `cmake -DGAME_MACHINE_DEBUG=ON ..`

## 規約

- ピン変更は **`config.hpp` の `CFG_*` のみ**（C / C++ 共通）
- SD パスは実行中スクリプトディレクトリ基準（`machine.resolve_path`）
- 動的ヒープ予算: `HeapConfig::BUDGET_BYTES`（256KB）
- 組み込み向け: スコープ最小・既存パターンに合わせる

## 参照

- [README_GAME_MACHINE.md](../README_GAME_MACHINE.md) … ハード構成・SD 配置
- [lib/README.md](../lib/README.md) … lib 配下の構成・依存関係
- [Test_Lua/visual_novel/README.md](../Test_Lua/visual_novel/README.md) … VN サンプル
