# 翠晶撃線  CRYSTAL FRONTLINE（最適化版）

[stg/](../stg/) と同一内容の縦スクロール弾幕 STG です。Lua 描画ホットパスをバンド単位で最適化し、実機 FPS を改善するための検証用ビルドです。

## 元版との違い

| 項目 | stg | stg_fast |
|------|-----|----------|
| SD パス | `/games/stg/stg.lua` | `/games/stg_fast/stg_fast.lua` |
| プレビュー | `stg.bin` | `stg_fast.bin` |
| 描画 | 毎バンド全体を描画 | `rect_in_band` / `band_top` で交差部分のみ |
| HUD | 毎バンド文字列生成 | スコア・残機をキャッシュ |
| API | `machine.*` 直呼び | `fill_rect` 等をローカル変数に束縛 |

ゲームプレイ・難易度・当たり判定は元版と同じです。

## SD 配置

```
/games/stg_fast/
├── stg_fast.lua
├── stg_fast.bin          … メニュープレビュー（100×100、任意）
├── fonts/
│   └── game_font.bin     … 日本語タイトル用（任意。stg の fonts をコピー可）
└── img/                  … スプライト（任意。無ければ図形描画）
    ├── player.bin
    ├── scout.bin
    ├── fighter.bin
    ├── diver.bin
    ├── boss.bin
    ├── pbullet.bin
    └── ebullet.bin
```

`generate_images.py` を実行すると `img/` と `stg_fast.bin` のプレースホルダーが生成されます。

```bash
cd Test_Lua/stg_fast
python generate_images.py
```

画像は [stg/img/](../stg/img/) からコピーしても構いません。

## 操作

| 入力 | 動作 |
|------|------|
| 十字キー | 自機移動 |
| OP_RIGHT / NEAR | 射撃 |
| ジャンプ系ボタン | タイトル開始 / リトライ / クリア後タイトルへ |
| OP_LEFT（ゲームオーバー時） | タイトルへ戻る |

## ゲーム構成

4 面構成・残機 3・各面ウェーブ撃破後にボス戦。詳細は [stg/README.md](../stg/README.md) を参照してください。

## 画像差し替え仕様

[stg/README.md](../stg/README.md) と同じです。ファイル名・サイズ・RGB565 形式は共通です。
