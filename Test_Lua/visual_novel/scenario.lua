-- ============================================================================

-- ファイル: scenario.lua

-- ビジュアルノベル「星降る夜に」のシナリオデータ

-- ============================================================================

--

-- 【編集のしかた】

--   このファイルだけを編集してください。visual_novel.lua は触らなくて OK です。

--   編集後、SD カードの /visual_novel/scenario.lua を上書きして試してください。

--

-- 【シーンの書き方】

--   id         … シーン識別子（英数字。choices の next から参照）

--   bg         … 背景色 {r,g,b}（画像が無いとき / 読込失敗時のフォールバック）

--   bg_image   … 背景画像 id（assets.lua の bg テーブルで定義）

--   character  … 立ち絵1枚（文字列 id または { id, pos = "left|center|right", x = 数値 }）

--   characters … 立ち絵最大2枚（配列）。例:

--     characters = {

--       { id = "hero", x = 8 },

--       { id = "mysterious", x = 184 },

--     }

--     pos は left/center/right、x を指定すると pos より x が優先されます。

--

--   name       … 話者名（空文字 "" で名前枠を非表示）

--

--   next       … 全セリフ表示後に進む次シーン id（choices が無いとき）

--   choices    … 分岐（省略可）。label=選択肢文言, next=進むシーン id

--   decor_stars … false で星アニメを無効（bg_image 無しのときのみ有効）

--

-- 【1 ページだけ差し替える例（lines 内テーブル）】

--   {

--     text = "表情が変わった！",

--     name = "？？？",

--     character = { id = "mysterious", pos = "center" },

--     bg_image = "street",  -- このページだけ背景変更（省略可）

--   },



return {

  -- --------------------------------------------------------------------------

  -- タイトル

  -- --------------------------------------------------------------------------

  {

    id = "title",

    bg = { r = 15, g = 20, b = 45 },

    bg_image = "title_night",

    name = "",

    decor_stars = false,

    lines = {

      "星降る夜に",

      "",

      "【操作】",

      "決定: A-R / NEAR",

      "選択: 十字キー",

      "終了: FAR 長押し",

    },

    next = "prologue",

  },



  -- --------------------------------------------------------------------------

  -- プロローグ

  -- --------------------------------------------------------------------------

  {

    id = "prologue",

    bg = { r = 25, g = 35, b = 70 },

    bg_image = "classroom",

    character = { id = "hero", pos = "left" },

    name = "ナレーター",

    lines = {

      "放課後、いつものゲーム機の前。",

      "画面には、小さな星が流れていた。",

    },

    next = "meet",

  },



  {

    id = "meet",

    bg = { r = 30, g = 45, b = 90 },

    bg_image = "street",

    characters = {
      { id = "hero", pos = "left" },
      { id = "mysterious", pos = "right" },
    },

    name = "八雲",

    lines = {

      "…ねえ、聞こえる？",
      {
        text = "この先に、まだ見ぬ物語があるの。",
        characters = {
          { id = "mysterious", pos = "center" },
        },
      },

    },

    next = "choice_path",

  },



  -- --------------------------------------------------------------------------

  -- 分岐

  -- --------------------------------------------------------------------------

  {

    id = "choice_path",

    bg = { r = 35, g = 50, b = 85 },

    bg_image = "street",

    characters = {
      { id = "hero", pos = "left" },
      { id = "mysterious", pos = "right" },
    },

    name = "？？？",

    lines = {

      "どちらへ進みますか？",

    },

    choices = {

      { label = "星の道を行く", next = "route_star" },

      { label = "家路につく",   next = "route_home" },

    },

  },



  {

    id = "route_star",

    bg = { r = 20, g = 30, b = 100 },

    bg_image = "star_path",

    character = { id = "hero", pos = "left" },

    name = "にとり",

    lines = {

      "星の並びに導かれて、",

      "僕は小さな扉を見つけた。",

      "…きっと、また会える。",

    },

    next = "ending",

  },



  {

    id = "route_home",

    bg = { r = 45, g = 40, b = 55 },

    bg_image = "home",

    character = { id = "hero", pos = "left" },

    name = "にとり",

    lines = {

      "今日はここまでにしよう。",

      "でも胸の奥で、小さな期待が残った。",

    },

    next = "ending",

  },



  -- --------------------------------------------------------------------------

  -- エンディング

  -- --------------------------------------------------------------------------

  {

    id = "ending",

    bg = { r = 10, g = 15, b = 35 },

    bg_image = "ending",

    name = "",

    lines = {

      "— END —",

      "",

      "お疲れさまでした。",

      "scenario.lua を編集して",

      "ストーリーを増やしてみてください。",

    },

    next = "title",

  },

}

