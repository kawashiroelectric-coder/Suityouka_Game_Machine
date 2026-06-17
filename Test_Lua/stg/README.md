# 翠晶撃線  CRYSTAL FRONTLINE

4 面構成の縦スクロール弾幕 STG です。

## SD 配置

```
/games/stg/
├── stg.lua
├── stg.bin              … メニュープレビュー（100×100、任意）
├── fonts/
│   └── game_font.bin    … 日本語タイトル用（任意。VN の fonts をコピー可）
└── img/                 … スプライト（任意。無ければ図形描画）
    ├── player.bin
    ├── scout.bin
    ├── fighter.bin
    ├── diver.bin
    ├── boss.bin
    ├── pbullet.bin
    └── ebullet.bin
```

`generate_images.py` を実行すると `img/` と `stg.bin` のプレースホルダーが生成されます。

```bash
cd Test_Lua/stg
python generate_images.py
```

## 操作

| 入力 | 動作 |
|------|------|
| 十字キー | 自機移動 |
| OP_RIGHT / NEAR | 射撃 |
| ジャンプ系ボタン | タイトル開始 / リトライ / クリア後タイトルへ |
| OP_LEFT（ゲームオーバー時） | タイトルへ戻る |

## ゲーム構成

| 面 | サブタイトル | 雑魚敵 | ボス |
|----|-------------|--------|------|
| 1 | ORBIT GATE | スカウト・ファイター | GATE KEEPER |
| 2 | NEBULA DRIFT | ファイター・スカウト・ダイバー | NEBULA CORE |
| 3 | ASTEROID BELT | ダイバー中心 | CRUSHER |
| 4 | CRYSTAL CORE | 全種混合 | SUIKYO EMPEROR |

- 残機 3
- 被弾後しばらく無敵
- 各面ウェーブ撃破後にボス警告 → ボス戦
- 4 面クリアで「MISSION COMPLETE」

## 画像差し替え仕様

すべて **RGB565 リトルエンディアン生データ**（`.bin`）。透過は **マゼンタ `0xF81F`**（`machine.draw_image_keyed`）。

| ファイル | サイズ (px) | 用途 |
|----------|-------------|------|
| `img/player.bin` | 16×16 | 自機 |
| `img/scout.bin` | 20×16 | 雑魚・スカウト |
| `img/fighter.bin` | 28×20 | 雑魚・ファイター |
| `img/diver.bin` | 22×22 | 雛魚・ダイバー |
| `img/boss.bin` | 56×44 | ボス（当たり判定は面ごとに Lua 側で調整） |
| `img/pbullet.bin` | 4×10 | 自機弾 |
| `img/ebullet.bin` | 5×5 | 敵弾 |
| `stg.bin` | 100×100 | メニュープレビュー |

PNG から変換する場合（プロジェクト共通ツール）:

```bash
python ../../tool/png_to_rgb565bin.py ship.png --resize 16x16 -o img/player.bin
```

## カスタム画像を作るときのヒント

- 自機・敵は **下向き／上から見下ろし気味** が合います（縦 STG）
- ボスは `boss.bin` を大きくしても、当たり判定は `STAGES[n].boss.w/h` で変更してください
- 1 枚あたり 200KB 以下、同時ロード最大 16 枚（他ゲームと共用しないよう STG 起動時のみロード）

## タイトル名

- 日本語: **翠晶撃線**
- 英語: **CRYSTAL FRONTLINE**

`fonts/game_font.bin` があるとタイトルとクリア画面で日本語が表示されます。
