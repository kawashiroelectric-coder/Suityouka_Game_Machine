# 翠灯夜行 ～ Jade Lantern Night

東方Project にインスパイアされた、オリジナルキャラクターによる縦スクロール弾幕 STG です（全 6 面）。
自機・敵・ボスすべてにドット絵、ボス戦前後の会話（立ち絵つき）、スペルカード、クリア後のエンディングとスタッフロールがあります。

## ストーリー

百年に一度、地上に近づく「翠の月」。その夜から村の灯籠はすべて緑色に燃え、夜が明けなくなった。
灯籠守りの少女・灯守ほたるは、緑の火の出どころを探して夜の道を行く――。

| 面 | ステージ | ボス | スペルカード |
|----|----------|------|--------------|
| 1 | 黄昏の畦道 | 鬼火のちろり | 鬼火「ゆらめく迎え火」 |
| 2 | 稲荷の千本鳥居 | 狐塚こはく | 狐火「千本鳥居の灯」／化術「九つ尾の幻灯」 |
| 3 | 霧の渓谷 | 水無月しずく | 渓流「逆巻く水鏡」／霧雨「白糸の滝」 |
| 4 | からくり屋敷 | 螺子巻ねじか | 発条「ゼンマイ仕掛けの円舞曲」／歯車「時計塔の十二刻」 |
| 5 | 雷雲の霊峰 | 鳴神らいか | 雷鼓「八連太鼓」／迅雷「天降る稲妻」 |
| 6 | 翠月の天守 | 夜翠ミコト | 翠月「ムーンライト・ランタン」／夜行「百鬼灯籠行列」／「明けない夜の翠灯」 |

## 操作

| ボタン | 動作 |
|--------|------|
| 十字キー | 移動 |
| FAR | ショット（押しっぱなし）／決定／会話送り |
| NEAR | ボム「灯籠結界」（敵弾消去・無敵・周囲にダメージ）／メニューで戻る／会話・エンディング・スタッフロールは長押しで早送り |
| OP_RIGHT | 押すたびに低速移動（当たり判定を表示）⇔通常移動を切り替え。低速中は画面右下に [SLOW] |
| OP_LEFT | ポーズ／ポーズ解除 |

押しやすい FAR・NEAR にショットとボムを置き、START/SELECT 位置の OP_RIGHT・OP_LEFT を低速切り替えとポーズにしています。
メニューの決定は OP_RIGHT でもできます。会話中はボムを撃てません（NEAR が早送りのため）。

## システム

- 難易度 EASY / NORMAL / HARD（ハイスコアとクリア状況を `save.dat` に保存）
- 残機 3（EASY は 4）、ボムは被弾ごとに 3 個まで回復。スコアエクステンドあり
- P アイテムでパワー（0.00〜4.00）。1.00 ごとにオプション（小さな灯籠）が増える
- 画面上部に行くとアイテムを自動回収。敵弾をかすめるとグレイズ加点
- 被弾直後の短い猶予中にボムを撃つと喰らいボム
- ボスは通常攻撃とスペルカードの複数フェーズ。被弾・ボムなしで撃破するとスペルカードボーナス
- コンティニュー 3 回まで（スコアの 1 の位がコンティニュー回数）

## SD 配置

```
/games/JadeLantern/
├── JadeLantern.lua      … 本体（フォルダ名と同名 → 起動スクリプト）
├── data.lua             … 会話・エンディング・スタッフロールのテキスト
├── title.bin            … メニュー用プレビュー 100x100
├── fonts/game_font.bin  … 12px 日本語フォント（使用文字のみのサブセット）
├── img/*.bin            … スプライト・立ち絵・弾（RGB565、マゼンタ透過）
├── img/*.png            … 上の画像の PNG 版（透過あり。編集・確認用）
├── bgm/*.wav            … BGM 10 曲（22050Hz モノラル）＋ bgm_len.lua
└── se/*.wav             … 効果音（11025Hz モノラル）
```

`tools/`・`README.md`・`img/*.png` は SD に置かなくても動作します。

`img/*.png` を編集したら、`tool/BinPngConverter` で `.bin` に変換して上書きしてください（「透明部分をマゼンタにする」は ON のまま）。
`img/title.png` はゲーム直下の `title.bin` の PNG 版です。

## 30FPS で動かすための設計

`game_machine_main.cpp` → `LuaInterpreter::runGameLoopFromSd()` のループ構造に合わせています。

- **固定 30Hz ロジック**: `game_update(dt)` の dt を積算し、1/30 秒ごとに 1 ステップ進めます（遅れても最大 2 ステップで追いつくので、ゲーム速度は一定）。
- **転送帯の削減**: 描画は `game_draw` を 1 回録画して、前フレームから変化した 20px 帯だけ LCD へ送られます。LCD SPI は 62.5MHz 指定で実効 37.5MHz（150MHz ÷ 4）なので、全 12 帯を送ると約 33ms かかります。そこで弾が飛ぶ範囲を y=20〜199 の 9 帯に限定し、上 1 帯・下 2 帯は静止した枠にしています（会話・カットイン中のみ更新）。
- **録画できる API だけを使用**: `draw_tilemap`・回転 `draw_image_affine`・`draw_image_xform`・`set_font_scale` は使いません。画像座標は常に整数で渡します。1 フレームの録画量は最大でも約 5KB（上限 12KB）です。
- **入力の間引き**: `machine.pressed()` は呼ぶたびに I2C（100kHz）で読み出すため 1 回約 0.4ms かかります。プレイ中は方向キー 4 個と、その他のボタンを 2 個ずつ交互に読む（1 フレーム 6 回）ようにしています。
- **GC を起こしにくい構造**: 敵弾（最大 220）・自機弾・アイテムは並列配列のプールで管理し、弾ごとのテーブルを作りません。`collectgarbage("incremental", 120, 200)` でヒープの膨らみも抑えています。
- **ヒープ予算**: ボスの立ち絵・ドット絵は面ごとに読み込み直し、タイトルロゴはプレイ中に解放します。
- **効果音**: `play_se` は毎回 SD から読み込むため、重要な音以外は間引いています。グレイズやアイテム取得は `play_tone` を使います。

## アセットの再生成

```bash
python games/JadeLantern/tools/generate_images.py         # img/*.bin, title.bin と、その PNG 版 img/*.png
python games/JadeLantern/tools/generate_font.py           # fonts/game_font.bin（data.lua を変えたら必ず再実行）
python games/JadeLantern/tools/generate_audio.py          # bgm/*.wav, se/*.wav（numpy が必要）
python games/JadeLantern/tools/import_portraits.py        # 立ち絵 img/face_*.bin を差し替え用 PNG から作り直す
```

- 立ち絵は `img/face_<名>2.png`（こはくは `img/indexed(2).png`）から `import_portraits.py` で作ります。
  - 背景を透明にし、目の高さと頭の大きさを 80x112 の枠にそろえて切り抜きます。
  - ほたる以外は左右反転します（画面右側に出るため）。
  - 位置の調整値はスクリプト冒頭の `PORTRAITS` 表にあります。
- `generate_images.py` は立ち絵も生成画像で上書きするので、実行したあとは `import_portraits.py` も実行してください。

- フォントの元データは `games/visual_novel/fonts/PixelMplus-20130602/PixelMplus12-Regular.ttf` を使います。
- 会話文は `data.lua` で編集できます。1 行は全角 16 文字まで（`generate_font.py --check` で確認）。

## PC でのプレビュー

```bash
python tool/lua_preview/preview.py games/JadeLantern/JadeLantern.lua --scale 3
```

キー: 矢印＝移動、S＝FAR（ショット）、A＝NEAR（ボム）、X＝OP_RIGHT（低速切り替え）、Z＝OP_LEFT（ポーズ）

## クレジット

- 企画・制作: 河城電気 (Kawashiro Electric)
- 立ち絵: 河城電気（`img/face_*2.png`）
- プログラム・ドット絵・音楽: Claude (Anthropic)。ドット絵、BGM、効果音は `tools/` のスクリプトで生成したオリジナルです
- フォント: PixelMplus (M+ FONTS License) — `games/visual_novel/fonts/PixelMplus-20130602/`
- 東方Project（上海アリス幻樂団）への敬意を込めた二次的な「東方風」作品で、公式の作品・キャラクターとは関係ありません。
