# Run!Yamame — 洞窟ランナー

Chrome Dino 風の横スクロール回避ゲームです。洞窟を舞台にジャンプ・スライド・白い球で岩・コウモリを対処します。

## SD 配置

```
/games/Run!Yamame/
  Run!Yamame.lua
  run1.bin / run2.bin / run3.bin
  slide.bin
  smoke.bin
  spiderweb.bin         … Spiderweb.png から変換（爆発の巣演出）
  cave.bin
  cave_rock.bin
  bat.bin
  hi_score.dat          … 初回ハイスコア更新後に自動生成
  Run!Yamame.bin        … 任意（メニュー用 100×100 プレビュー）
```

## 操作

| 入力 | 動作 |
|------|------|
| タイトルで UP / OP_RIGHT / NEAR | ゲーム開始 |
| UP / OP_RIGHT / NEAR | ジャンプ |
| DOWN | スライド（コウモリ潜り） |
| FAR | 白い球を発射（ゲージ満タン時のみ・約5秒で再チャージ） |
| ゲームオーバーで Jump | リトライ |
| ゲームオーバーで OP_LEFT | タイトルへ |

## 画面構成

- **タイトル** … 背景スクロール＋走行モーション、`HI SCORE`、スタート待ち
- **プレイ** … スコア／HI／WEB ゲージ表示。開始は低速で、表示スコア約 850 で通常速度に到達
- **ゲームオーバー** … 新記録時は `NEW RECORD!`。HI SCORE は `hi_score.dat` に保存

## 関連

- [LUA_API.md](../../LUA_API.md) — `save_data` / `load_data` / 画像 API
