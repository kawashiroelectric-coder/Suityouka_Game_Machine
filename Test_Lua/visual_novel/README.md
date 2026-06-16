# 簡易ビジュアルノベル「星降る夜に」



テキスト送り・分岐選択・**背景画像・立ち絵**付きのサンプルビジュアルノベルです。



## フォルダ構成



```

Test_Lua/visual_novel/

├── visual_novel.lua     … ゲーム本体（エンジン）

├── scenario.lua         … シナリオ編集用（ここを編集）

├── assets.lua           … 画像 id と .bin ファイルの対応表

├── generate_font.py     … 美咲サブセット MISF 生成

├── generate_images.py   … プレースホルダ背景・立ち絵の生成

├── fonts/

│   ├── game_font.bin    … Pico 用フォント（SD に同梱）

│   └── LICENSE.txt      … 美咲フォント著作権表記

├── images/              … generate_images.py の出力（SD に同梱）

│   ├── bg/

│   └── chars/

└── README.md

```



## SD カードへの配置



`visual_novel` フォルダごと SD ルートへコピーしてください。



```

/（SD ルート）

└── visual_novel/

    ├── visual_novel.lua

    ├── scenario.lua

    ├── assets.lua

    ├── fonts/game_font.bin

    └── images/

        ├── bg/*.bin

        └── chars/*.bin

```



### 起動方法



1. **自動検出（推奨）** … SD ルートに `visual_novel/` を置く

2. **VN だけ試す** … 他ゲーム用 `.lua` が無いことを確認するか、`visual_novel.lua` を `game.lua` にリネーム



> `scenario.lua` だけでは起動しません。**visual_novel.lua + scenario.lua** の両方が必要です。



## 操作



| 入力 | 動作 |

|------|------|

| A-R (OP_RIGHT) / NEAR | 次のセリフへ / 決定 |

| UP / DOWN | 選択肢モードでカーソル移動 |

| FAR 長押し (約 1.5 秒) | ゲーム終了 |



## ストーリーの拡張（scenario.lua）



### 基本シーン



```lua

{

  id = "my_scene",

  bg = { r = 40, g = 60, b = 100 },   -- 画像読込失敗時の単色

  bg_image = "classroom",              -- assets.lua で定義した背景 id

  character = { id = "hero", pos = "right" },  -- 立ち絵

  name = "主人公",

  lines = {

    "1 ページ目のセリフ。",

    "2 ページ目のセリフ。",

  },

  next = "next_scene_id",

},

```



### 立ち絵の位置



`character` は次のどちらか:



- 文字列 … `"hero"`（中央配置）

- テーブル … `{ id = "hero", pos = "left" }` または `{ id = "hero", x = 20 }`



`pos` は `left` / `center` / `right`。**`x` を指定すると `pos` より `x` が優先**されます。



### 立ち絵 2 枚同時



`characters` 配列で最大 2 枚指定できます（後から書いた方が手前）。



```lua

characters = {

  { id = "hero", x = 8 },

  { id = "mysterious", x = 184 },

},

```



シーン全体または `lines` 内テーブルで指定可能です。1 ページだけ差し替える例:



```lua

{

  text = "2 人が向かい合う。",

  characters = {

    { id = "hero", x = 8 },

    { id = "mysterious", x = 184 },

  },

},

```



### 1 ページだけ差し替え



```lua

lines = {

  "通常のセリフ",

  {

    text = "表情が変わった！",

    name = "？？？",

    character = { id = "mysterious", pos = "left" },

    bg_image = "street",

  },

},

```



### 分岐



```lua

choices = {

  { label = "左の道", next = "left_route" },

  { label = "右の道", next = "right_route" },

},

```



## 画像の用意



### プレースホルダを生成（同梱サンプル）



```bash

cd Test_Lua/visual_novel

python generate_images.py

```



| 種類 | サイズ | 備考 |

|------|--------|------|

| 背景 (`images/bg/`) | **320 × 168** | テキストボックス上の領域いっぱい |

| 立ち絵 (`images/chars/`) | **128 × 168** | 下揃えで表示。背景は **マゼンタ透過** |



### 自作 PNG から変換



```bash

pip install pillow

python ../../tool/png_to_rgb565bin.py my_bg.png --resize 320x168 -o images/bg/my_bg.bin

python ../../tool/png_to_rgb565bin.py my_char.png --resize 128x168 -o images/chars/my_char.bin

```



立ち絵は **背景を #FF00FF（マゼンタ）** にすると透過されます（`draw_vn_stream` の `key` / `keyed`）。



### assets.lua に登録



```lua

return {

  bg = {

    my_bg = { file = "bg/my_bg.bin", w = 320, h = 168 },

  },

  char = {

    my_char = { file = "chars/my_char.bin", w = 128, h = 168, keyed = true },

  },

}

```



`scenario.lua` で `bg_image = "my_bg"` / `character = "my_char"` のように id を指定します。



> **背景・立ち絵のメモリ**  
> 背景（320×168）と立ち絵（128×168、最大 2 枚）は `machine.draw_vn_stream` で SD から **バンド単位** に合成描画します（RAM に全枚載せない）。  
> 背景のみ使う場合は `machine.draw_bg_stream` も利用可能です。  
> ファームウェア更新後に `visual_novel.lua` も SD へコピーしてください。



## 日本語フォント



`machine.load_font` で SD 上の `game_font.bin` を読み込みます。シナリオを増やしたら:



```bash

python generate_font.py

```



生成した `fonts/game_font.bin` を SD にコピーしてください。



> **名前が `?` になるとき**  
> フォントは `scenario.lua` 内の文字だけを収集して作ります。  
> **`name = "ゆかり"` の「ゆ」のように、名前にしか無い文字**は、  
> `scenario.lua` だけ SD にコピーして **`game_font.bin` を更新し忘れると `?` 表示**になります。  
> （「か」「り」はセリフにも出るので表示される、というパターンがよくあります。）  
> `python generate_font.py` 実行時に「名前専用の文字」として警告が出ます。



## カスタマイズ（visual_novel.lua の CONFIG）



- テキストボックスの高さ (`TEXT_BOX_H`)

- 色（`COL_*`）

- 終了ボタンの長押し時間 (`EXIT_HOLD_MS`)



## 今後の拡張アイデア



- `machine.play_se` / BGM

- セーブ／ロード（`state.scene_id` と `line_index`）

- フェード切り替え

