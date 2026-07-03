# Game Select Menu PC プレビュー

ファームのゲーム選択メニューを PC 上で簡易再現します。

- **BG1〜4** を入場時に乱数選択（直前と同じ BG は避ける）
- **左リスト**（スクロール・256 件上限行・選択ハイライト）
- **右パネル**は簡易版（`NO IMAGE` 枠 + タイトル + サイズ行）

## 要件

```bash
pip install -r tool/game_select_preview/requirements.txt
```

## 使い方

プロジェクトルートから:

```bash
python tool/game_select_preview/preview.py
python tool/game_select_preview/preview.py --games-dir Test_Lua
python tool/game_select_preview/preview.py --games-dir games --scale 3
python tool/game_select_preview/preview.py --seed 42
```

既定のゲームディレクトリ:

1. `games/` があればそれを使用
2. なければ `Test_Lua/`

## 操作

| キー | 動作 |
|------|------|
| ↑ / ↓ | ゲーム選択 |
| R | BG を再抽選（メニュー再入場相当） |
| Esc | 終了 |

## 注意

- ゲーム一覧の検出規則は `lib/game_catalog/game_catalog.cpp` に準拠
- プレビュー画像の読み込みは未実装（範囲外）。右側は常に `NO IMAGE` 表示
- BG 乱数は PC の `random.getrandbits(32)` を使用（実機の `pico_rand` とは系列は異なる）
