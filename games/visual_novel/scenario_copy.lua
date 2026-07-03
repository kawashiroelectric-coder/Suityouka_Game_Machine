-- ============================================================================
-- ファイル: scenario_copy.lua
-- ビジュアルノベル「星降る夜に」シナリオ（改稿版・執筆用コピー）
-- 構成案: story terring.txt（5章 + 分岐エンディング3種）
-- 二次創作：河城にとり / 八雲紫（東方Project）
-- ============================================================================
--
-- 【このファイルについて】
--   本編の scenario.lua は編集しない。試験・改稿は scenario_copy.lua で行う。
--   実機反映時は内容を scenario.lua へコピーするか、visual_novel.lua の
--   SCENARIO_PATHS に "scenario_copy.lua" を追加して読み込ませる。
--
-- 【立ち絵 id（assets.lua 未登録・要追加素材）】
--   hero       … 既存（にとり）
--   mysterious … 既存（八雲紫）
--   tengu      … chars/tengu.bin      妖怪の山・天狗の聞き込み用（128x168, keyed）
--   reimu      … chars/reimu.bin      博麗霊夢（128x168, keyed）
--   villager   … chars/villager.bin   人里の老人（128x168, keyed）
--   kappa      … chars/kappa.bin      河童仲間・忠告シーン用（128x168, keyed）
--   上記が無い間は character 未指定または hero のみで進行（台詞の name はそのまま）。
--
-- 【背景 id】既存 assets.lua のみ使用（home / classroom / street / star_path / ending 等）
--
-- 【編集のしかた】
--   lines の各要素は話者変更・立ち絵変更・演出の「間」で区切る。
--   編集後: python generate_font.py（対象を scenario_copy.lua に差し替えて実行可）

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
      "",
      "【操作】\n決定: A-R / NEAR\n選択: 十字キー\n終了: FAR 長押し",
    },
    next = "ch1_01",
  },

  -- --------------------------------------------------------------------------
  -- 第1章：改造とノイズ（固定パート）
  -- --------------------------------------------------------------------------
  {
    id = "ch1_01",
    bg = { r = 25, g = 35, b = 55 },
    bg_image = "home",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "幻想郷・玄武沢。河童の工房に、携帯ゲーム機が置かれている。河城にとりは改良を重ね、試験のために電源を入れた。しかし、画面には星空が映っていた。「なんだこれ？」",
    },
    next = "ch1_02",
  },

  {
    id = "ch1_02",
    bg = { r = 25, g = 35, b = 55 },
    bg_image = "home",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "画面に星空が映る。外の星々とは並び方が違う。筐体の中だけでもう一つの夜空が動いている。耳を近づけると、ノイズに混じりかすかな声のようなものが聞こえる。何かささやいているようだが、音量を最大にしても聞こえない。これは何なんだろう？謎が増えてしまった。",
    },
    next = "ch1_03",
  },

  {
    id = "ch1_03",
    bg = { r = 25, g = 35, b = 55 },
    bg_image = "home",
    character = { id = "hero", pos = "left" },
    name = "河童の仲間",
  -- character = { id = "kappa", pos = "right" },  -- 素材追加後に有効化
    lines = {
      "翌日、仲間の河童たちに見せ意見を聞いてみた。「なんだろう？」「故障…ではなさそうだけど。」「みたことない！」どの意見もあまり参考にはならなかった。",
      {
        text = "初めての事だから誰もわかんないか…",
        name = "にとり",
      },
      "他の仲間も首を振る。「元に戻したら？」——誰かがそう言った。",
    },
    next = "ch1_04",
  },

  {
    id = "ch1_04",
    bg = { r = 30, g = 40, b = 70 },
    bg_image = "classroom",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "戻すなんてもったいないじゃん！。",
      "ゲーム機が星空を表示するなんて面白いじゃないか。",
    },
    next = "ch1_05",
  },

  {
    id = "ch1_05",
    bg = { r = 30, g = 40, b = 70 },
    bg_image = "classroom",
    character = { id = "tengu", pos = "left" },
    name = "文",
    lines = {
      "誰かと思えば、面白そうな話をしているじゃないですか！",
      "私も混ぜてくださいよ。",
    },
    next = "ch1_06",
  },
  -- --------------------------------------------------------------------------
  -- 第2章：漏れ出す忘れ物（調査・選択あり）
  -- --------------------------------------------------------------------------
  {
    id = "ch2_01",
    bg = { r = 30, g = 45, b = 90 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "数日後。幻想郷のあちこちで小さな異変が始まった。昔話の細部が曖昧になる。誰かの思い出が、ふと薄くなる。筐体のノイズはじわじわ強くなり、時間とともに星の流れが速い。",
    },
    next = "ch2_02",
  },
  {
    id = "ch2_02",
    bg = { r = 30, g = 45, b = 90 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "このノイズ、時間と一緒に強くなってないか？　日が暮れる前に手を打った方がいい。夜になると、向こうの「声」が強くなる気がする。",
      "妖怪の山、人里、博麗神社——回れるのは二箇所までだ。全部回ろうとしたら、日が暮れてしまう。",
    },
    next = "ch2_stance",
  },
  {
    id = "ch2_stance",
    bg = { r = 35, g = 50, b = 85 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "限られた時間で、どう動く？",
    },
    choices = {
      { label = "改造を深追いする（技術寄り）", next = "ch2_stance_tech" },
      { label = "被害者の話を聞く（感傷寄り）", next = "ch2_stance_feel" },
      { label = "早めに紫へ接触する（慎重）", next = "ch2_stance_care" },
    },
  },
  {
    id = "ch2_stance_tech",
    bg = { r = 35, g = 50, b = 85 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "原因は装置にあるはずだ。聞き込みもするが、最後はデータで割り切る。河童は技術者だ。",
    },
    next = "ch2_first",
  },
  {
    id = "ch2_stance_feel",
    bg = { r = 35, g = 50, b = 85 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "忘れられる怖さを、数字だけじゃ測れない。誰かの記憶が薄れていくなら、まず耳を傾けるべきだ。",
    },
    next = "ch2_first",
  },
  {
    id = "ch2_stance_care",
    bg = { r = 35, g = 50, b = 85 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "境界のことは、境界の妖怪に聞くのが早い。深入りする前に、紫の意見を聞いておこう。",
    },
    next = "ch2_first",
  },
  {
    id = "ch2_first",
    bg = { r = 35, g = 50, b = 85 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "まずどこへ向かう？",
    },
    choices = {
      { label = "妖怪の山", next = "ch2_mt_01" },
      { label = "人里", next = "ch2_vl_01" },
      { label = "博麗神社", next = "ch2_sh_01" },
    },
  },

  -- 第2章：1箇所目 — 妖怪の山
  {
    id = "ch2_mt_01",
    bg = { r = 30, g = 45, b = 90 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "天狗",
  -- character 右: tengu（素材追加後）
    lines = {
      "河童、お前の機械が変なことになってんじゃないか。空気が薄いのは境界のせいだろ。",
      {
        text = "疑うなら一緒に計測してよ。データは嘘をつかない。",
        name = "にとり",
      },
      "別の天狗が笑う。「面白いな、便乗してやろうか」。技術者としてのスタンスが、少しだけ強くなる。",
    },
    next = "ch2_mt_pick2",
  },
  {
    id = "ch2_mt_pick2",
    bg = { r = 35, g = 50, b = 85 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "日が傾いている。次はどこへ？",
    },
    choices = {
      { label = "人里へ", next = "ch2_mt_vl" },
      { label = "博麗神社へ", next = "ch2_mt_sh" },
    },
  },

  -- 第2章：1箇所目 — 人里
  {
    id = "ch2_vl_01",
    bg = { r = 40, g = 45, b = 60 },
    bg_image = "home",
    character = { id = "hero", pos = "left" },
    name = "老人",
  -- character 右: villager
    lines = {
      "最近、昔よく話してた孫の名前を、ふと思い出せなくなる時があるのう。忘れるのが怖いわい。",
      {
        text = "……そういうのが、境界の漏れ出しなのかもしれない。",
        name = "にとり",
      },
      "生活の実感として、「忘れる」怖さが胸に残る。",
    },
    next = "ch2_vl_pick2",
  },
  {
    id = "ch2_vl_pick2",
    bg = { r = 35, g = 50, b = 85 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "もう一箇所、行ける。",
    },
    choices = {
      { label = "妖怪の山へ", next = "ch2_vl_mt" },
      { label = "博麗神社へ", next = "ch2_vl_sh" },
    },
  },

  -- 第2章：1箇所目 — 博麗神社
  {
    id = "ch2_sh_01",
    bg = { r = 35, g = 40, b = 95 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "霊夢",
  -- character 右: reimu
    lines = {
      "最近、なんかこの辺の空気薄いのよね。神社の結界は平気だけど、空の感じが違う。",
      {
        text = "紫の出番かもな。……あと一箇所、回れる。",
        name = "にとり",
      },
    },
    next = "ch2_sh_pick2",
  },
  {
    id = "ch2_sh_pick2",
    bg = { r = 35, g = 50, b = 85 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "残りはどちらへ？",
    },
    choices = {
      { label = "妖怪の山へ", next = "ch2_sh_mt" },
      { label = "人里へ", next = "ch2_sh_vl" },
    },
  },

  -- 第2章：2箇所目（短い追加入り）
  {
    id = "ch2_mt_vl",
    bg = { r = 40, g = 45, b = 60 },
    bg_image = "home",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "人里では、忘れかけた名前の話がまた一つ増えた。日没が近い。空のノイズが、耳の奥でうなり始める。",
    },
    next = "ch3_01",
  },
  {
    id = "ch2_mt_sh",
    bg = { r = 35, g = 40, b = 95 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "神社では霊夢の直感が、境界の違和感を裏付けた。紫に会う準備ができた。西の空が赤く染まる。",
    },
    next = "ch3_01",
  },
  {
    id = "ch2_vl_mt",
    bg = { r = 30, g = 45, b = 90 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "山では天狗たちが装置を疑い、好奇心で便乗しようとした。技術者としての自信と不安が混ざる。",
    },
    next = "ch3_01",
  },
  {
    id = "ch2_vl_sh",
    bg = { r = 35, g = 40, b = 95 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "神社の空気は薄い。霊夢は「紫が分かるんじゃないの」と言い残した。",
    },
    next = "ch3_01",
  },
  {
    id = "ch2_sh_mt",
    bg = { r = 30, g = 45, b = 90 },
    bg_image = "street",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "山では「お前の機械のせいかもな」と天狗に言われた。否定はできない。",
    },
    next = "ch3_01",
  },
  {
    id = "ch2_sh_vl",
    bg = { r = 40, g = 45, b = 60 },
    bg_image = "home",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "人里の老人の声が、夕暮れとともに遠のく。忘れ物が、確かに増えている。",
    },
    next = "ch3_01",
  },

  -- --------------------------------------------------------------------------
  -- 第3章：紫の介入と真実
  -- --------------------------------------------------------------------------
  {
    id = "ch3_01",
    bg = { r = 35, g = 40, b = 95 },
    bg_image = "street",
    characters = {
      { id = "hero", pos = "left" },
      { id = "mysterious", pos = "right" },
    },
    name = "八雲",
    lines = {
      "遅かったわね、にとり。あなたの改造が、境界の隙間に風穴を開けた。星空の正体は、外の世界で忘れられた物語よ。",
      {
        text = "……やっぱり、玩具じゃなかったか。",
        name = "にとり",
      },
      "外の世界、人がどんどん減っているのよ。語る人がいなくなれば、聞く人もいなくなる。当たり前の話が、当たり前に忘れられる速度が上がってるだけ。",
      {
        text = "そういうもんか、とは言えないな。でも、納得はした。",
        name = "にとり",
      },
      "隙間を閉じるには協力が要る。あなたの技術と、私の境界操作。どうする？",
    },
    next = "ch3_choice",
  },
  {
    id = "ch3_choice",
    bg = { r = 35, g = 40, b = 95 },
    bg_image = "street",
    characters = {
      { id = "hero", pos = "left" },
      { id = "mysterious", pos = "right" },
    },
    name = "にとり",
    lines = {
      "……どう関わる？",
    },
    choices = {
      { label = "紫に協力し、隙間の中へ入る", next = "ch4_01" },
      { label = "関わりを最小限にする", next = "ch3_minimal" },
    },
  },
  {
    id = "ch3_minimal",
    bg = { r = 35, g = 40, b = 95 },
    bg_image = "street",
    characters = {
      { id = "hero", pos = "left" },
      { id = "mysterious", pos = "right" },
    },
    name = "にとり",
    lines = {
      "深入りは避けよう。紫に任せて、俺は装置の安全対策だけ。",
      {
        text = "わかったわ。最小限の処置で済ませる。……根本は、またいつか。",
        name = "八雲",
      },
      "表面上は収まったように見える。だが、筐体の奥に小さな違和感が残る。",
    },
    next = "ending_interrupt",
  },

  -- --------------------------------------------------------------------------
  -- 第4章：隙間の中（Aルートのみ）
  -- --------------------------------------------------------------------------
  {
    id = "ch4_01",
    bg = { r = 20, g = 30, b = 100 },
    bg_image = "star_path",
    characters = {
      { id = "hero", pos = "left" },
      { id = "mysterious", pos = "right" },
    },
    name = "ナレーター",
    lines = {
      "紫に導かれ、隙間へ入る。漂う物語の欠片が星のように並ぶ。にとりは筐体を抱え、一歩ずつ進んだ。",
      {
        text = "ここにあるのは、誰の記憶にも残らなかった話よ。拾うなら、一つだけ選びなさい。",
        name = "八雲",
      },
    },
    next = "ch4_story_choice",
  },
  {
    id = "ch4_story_choice",
    bg = { r = 20, g = 30, b = 100 },
    bg_image = "star_path",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "三つの欠片が、特に胸に触れる。どれを手元に残す想定で進む？",
    },
    choices = {
      { label = "屋上の老人の星", next = "ch4_rooftop" },
      { label = "最後の運動会", next = "ch4_sports" },
      { label = "電話の向こうの声", next = "ch4_phone" },
    },
  },
  {
    id = "ch4_rooftop",
    bg = { r = 20, g = 30, b = 100 },
    bg_image = "star_path",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "集合住宅の屋上で、毎晩同じ時間に星を見上げていた老人。子供たちは町を離れ、誰も屋上に来なくなった。それでも一人で星を見続けた。",
      "彼が亡くなった後、屋上の鍵を持っていたのは彼だけ。誰も、そこに何があったか知らない。",
      {
        text = "……特別な星座でもない。ただ「今日も見えた」という確認だけだったのね。",
        name = "にとり",
      },
    },
    next = "ch5_choice",
  },
  {
    id = "ch4_sports",
    bg = { r = 20, g = 30, b = 100 },
    bg_image = "star_path",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "小さな町の小学校。生徒は一桁、最後の運動会。保護者より近所の年寄りが多く、徒競走は全員が一等賞になる人数だった。",
      "先生が涙ぐみながら「来年はもうこの学校はありません」と挨拶した、その一日だけの記憶。",
      {
        text = "誰も覚えていない一日が、こんなに重いなんて。",
        name = "にとり",
      },
    },
    next = "ch5_choice",
  },
  {
    id = "ch4_phone",
    bg = { r = 20, g = 30, b = 100 },
    bg_image = "star_path",
    character = { id = "hero", pos = "left" },
    name = "ナレーター",
    lines = {
      "離れた娘が、毎週日曜の夜に年老いた母へ電話をかけていた。母が施設に入り、呼びかけに応えなくなってから、その習慣は途絶えた。",
      "娘の手元に残ったのは、特別でもない世間話ばかりの留守番電話のテープ一本だけ。",
      {
        text = "記録は残っても、聞く人がいなくなったら同じだ。",
        name = "にとり",
      },
    },
    next = "ch5_choice",
  },

  -- --------------------------------------------------------------------------
  -- 第5章：閉じるか、閉じないか
  -- --------------------------------------------------------------------------
  {
    id = "ch5_choice",
    bg = { r = 20, g = 30, b = 100 },
    bg_image = "star_path",
    characters = {
      { id = "hero", pos = "left" },
      { id = "mysterious", pos = "right" },
    },
    name = "八雲",
    lines = {
      "隙間を弔うように閉じる方法はある。ただし、拾った物語のひとつを「あなたが覚える代わりに、星として手放す」必要がある。",
      {
        text = "全部、自分の中に残そうとしたら？",
        name = "にとり",
      },
      "技術で保存しようとしても、隙間は中途半端にしか閉じない。選びなさい。",
    },
    next = "ch5_branch",
  },
  {
    id = "ch5_branch",
    bg = { r = 20, g = 30, b = 100 },
    bg_image = "star_path",
    character = { id = "hero", pos = "left" },
    name = "にとり",
    lines = {
      "最後の判断は——",
    },
    choices = {
      { label = "覚えている代わりに手放す", next = "ch5_release" },
      { label = "全部覚えておこうとする", next = "ch5_keep" },
    },
  },
  {
    id = "ch5_release",
    bg = { r = 25, g = 35, b = 55 },
    bg_image = "home",
    characters = {
      { id = "hero", pos = "left" },
      { id = "mysterious", pos = "right" },
    },
    name = "にとり",
    lines = {
      "一つの物語を、自分だけの記憶として引き受けた。星は消え、隙間は閉じた。筐体はただの機械に戻る。",
      {
        text = "悪くない仕事だったわね。",
        name = "八雲",
      },
      "工房に戻ると、夜空は静かだった。にとりは時々、あの小さな営みを思い出す。",
    },
    next = "ending_memorial",
  },
  {
    id = "ch5_keep",
    bg = { r = 25, g = 35, b = 55 },
    bg_image = "home",
    characters = {
      { id = "hero", pos = "left" },
      { id = "mysterious", pos = "right" },
    },
    name = "にとり",
    lines = {
      "欲張って、全部を筐体に押し込もうとした。隙間は閉じきらず、星空は弱まったが、画面の隅に小さな歪みが残る。",
      {
        text = "まあ、それもお前らしいか。",
        name = "八雲",
      },
      "日常生活に支障はない。だが、未処理の謎が手元に残り続ける。",
    },
    next = "ending_half",
  },

  -- --------------------------------------------------------------------------
  -- エンディング（3種）
  -- --------------------------------------------------------------------------
  {
    id = "ending_memorial",
    bg = { r = 10, g = 15, b = 35 },
    bg_image = "ending",
    name = "",
    lines = {
      "— END —\n【弔いの星】\n隙間は完全に閉じた。にとりは一つの物語を、自分だけの記憶として持ち続ける。",
    },
    next = "title",
  },
  {
    id = "ending_half",
    bg = { r = 10, g = 15, b = 35 },
    bg_image = "ending",
    name = "",
    lines = {
      "— END —\n【半端な蓋】\n隙間は閉じきらず、筐体に小さな歪みが残り続ける。",
    },
    next = "title",
  },
  {
    id = "ending_interrupt",
    bg = { r = 10, g = 15, b = 35 },
    bg_image = "ending",
    name = "",
    lines = {
      "— END —\n【紫に任せた日】\n最小限の処置で収まったが、根本は解けていない。いつか、また同じ気配が戻るかもしれない。",
    },
    next = "title",
  },

}
