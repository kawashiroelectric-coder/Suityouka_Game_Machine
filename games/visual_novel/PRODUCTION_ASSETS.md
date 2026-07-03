# 星降る夜に — 制作・素材指示（内部用）

> 二次創作（東方Project：河城にとり / 八雲紫）。  
> シナリオは `scenario.lua` に執筆済み。  
> **このファイルは作者向け。** プレイヤー向け README には載せない。

---

## 1. 作品の前提（執筆・素材の共通認識）

| 項目 | 内容 |
|------|------|
| 主人公 | **河城にとり**（河童・発明家） |
| キーキャラ | **八雲紫**（境界の妖怪） |
| 骨格 | **ゲーム機（観測端末）** ＋ **星のログ（境界／未踏の物語）** |
| 目標尺 | 現サンプル約 2 分 → **35〜45 分**（約 20 倍） |
| 既存エンジン | `visual_novel.lua` は触らず **`scenario.lua` のみ拡張** |

---

## 2. 既存素材（そのまま使う）

### 背景（320×168, `images/bg/*.bin`）

| id | ファイル | 東方版での用途 |
|----|----------|----------------|
| `title_night` | `title_night.bin` | タイトル |
| `home` | `home.bin` | **玄武沢・にとりの工房**（第1章） |
| `classroom` | `classroom.bin` | **ログ再生／記録閲覧**（観測記録 UI 風） |
| `street` | `street.bin` | **境界付近の外面の道**（第2章・邂逅） |
| `star_path` | `star_path.bin` | **星の道**（ルート A 本編） |
| `ending` | `ending.bin` | エンディング（ノーマル／トゥルー共用可） |

### 立ち絵（128×168, マゼンタ透過, `images/chars/*.bin`）

| id | 用途 |
|----|------|
| `hero` | **にとり**（通常） |
| `mysterious` | **八雲紫**（通常） |
| `narrator` | ナレーション用（任意・使わなくても可） |

---

## 3. 追加を推奨する素材

執筆側は **未追加でも既存 id で進められる** が、40 分級では以下があると表現が楽。

### 背景（優先度順）

| 推奨 id | 用途 | 備考 |
|---------|------|------|
| `workshop_night` | 工房・夜（`home` の別バリエーション） | 第1章後半〜封印シーン |
| `ravine` | 玄武沢（川・岩） | 河童らしさ。`home` と使い分け |
| `border_twilight` | 境界の夕暮れ | `street` の差し替え・第2章クライマックス |
| `gap_space` | 隙間／星の内部 | ルート A 中盤・紫の台詞が多い場面 |
| `ending_true` | トゥルー ED 専用 | `ending` と色調を変える（任意） |

**サイズ:** 320×168 RGB565（`image_to_rgb565_bin.py --resize 320x168`）

### 立ち絵（優先度順）

| 推奨 id | キャラ | 用途 |
|---------|--------|------|
| `nitori_work` | にとり | リュック・工具（説明シーン） |
| `nitori_think` | にとり | 困惑・分析 |
| `yukari_smile` | 紫 | からかい・含みのある笑み |
| `yukari_serious` | 紫 | 境界／物語の話 |

**サイズ:** 128×168、背景 **#FF00FF** 透過

> 立ち絵は **同時 2 枚まで**（`characters` 配列）。表情差分は「1 ページだけ `lines` 内テーブルで差し替え」向き。

---

## 4. 作者が用意する手順（素材追加時）

1. PNG を用意 → `tool/image_to_rgb565_bin.py` で `.bin` 化
2. `Test_Lua/visual_novel/images/bg/` または `chars/` に配置
3. **`assets.lua` に id を登録**（`w` / `h` / `file` / 立ち絵は `keyed = true`）
4. **`scenario.lua` で `bg_image` / `character` id を指定**（未登録 id は執筆側が将来差し替え）
5. 新しい漢字・記号を増やしたら **`python generate_font.py`** → `fonts/game_font.bin` を SD に再コピー

執筆（Agent）側は **id 名をこの表に合わせて指定**する。未用意の id は既存にフォールバックする。

---

## 5. シナリオ構成（執筆済み・確定案）

### 第1章：河童の工房と星のログ（`home` → `classroom`）— 約 8 分

- にとり＝河童・発明家、ゲーム機＝境界観測端末
- 星＝未踏の物語の痕
- 伏線：特定の「名前」で反応が強くなる → 紫登場へ

### 第2章：境界の向こう（`street`）— 約 10 分

- 紫登場、**双方妖怪**の認識
- 中間選択（止める／追う）→ 本選択（星の道／玄武沢へ）

### 第3章：分岐本編 — 各 12〜15 分

**ルート A「星の道」**（`star_path`）

- 小分岐：記録する／最後まで読む
- ノーマル ED A（記録）／トゥルー ED 手前（最後まで読む）

**ルート B「家路」**（`home` → `classroom`）

- 水で封印、後悔、記録に残る紫の声
- 小分岐：封印解除（A 合流）／維持（B ED）

### 第4章：トゥルー ED（`ending`）— 約 5 分

- 紫の仕掛け＋にとりとの「取引」
- 河童と境界妖怪の共犯で締め

### シーン id 早見（`scenario.lua`）

| ブロック | 先頭 id | 備考 |
|----------|---------|------|
| タイトル | `title` | |
| 第1章 | `ch1_01` … `ch1_10` | |
| 第2章 | `ch2_01` … `ch2_choice` | 中間分岐 `ch2_stop` / `ch2_chase` |
| 本分岐 | `choice_path` | 星の道 / 玄武沢 |
| ルート A | `a_01` … `a_choice` | `a_rec_*` / `a_read_*` |
| ルート B | `b_01` … `b_choice` | `b_unseal_*` / `b_keep_*` |
| ED | `ending_a_record` / `ending_b` / `true_*` / `ending` | |

---

## 6. 執筆後のチェックリスト

- [x] `scenario.lua` を構成案どおり拡張（約 70 シーン）
- [ ] SD に `scenario.lua` をコピー
- [x] **`python generate_font.py`** を実行し `game_font.bin` を更新（580 glyphs）
- [ ] 追加素材を入れたら `assets.lua` と本表の「状態」を更新
- [ ] 全ルート＋トゥルー ED の通しプレイ（目安 30〜40 分）

---

## 7. 著作権・配布

- 東方Project 二次創作ガイドラインに従う
- 配布時は二次創作である旨を README 等に記載（作者判断）

---

## 8. 素材 id 一覧（執筆・作者共有用・更新欄）

| 種別 | id | 状態 | メモ |
|------|-----|------|------|
| bg | title_night | 済 | |
| bg | home | 済 | 工房 |
| bg | classroom | 済 | ログ再生 |
| bg | street | 済 | 境界付近 |
| bg | star_path | 済 | |
| bg | ending | 済 | |
| bg | workshop_night | 未 | 任意 |
| bg | ravine | 未 | 任意 |
| bg | border_twilight | 未 | 任意 |
| bg | gap_space | 未 | 任意 |
| bg | ending_true | 未 | 任意 |
| char | hero | 済 | にとり |
| char | mysterious | 済 | 紫 |
| char | narrator | 済 | 任意 |
| char | nitori_work | 未 | 任意 |
| char | nitori_think | 未 | 任意 |
| char | yukari_smile | 未 | 任意 |
| char | yukari_serious | 未 | 任意 |

**作者:** 追加した id はこの表の「状態」を `済` に更新してください。
