-- ============================================================================
-- 翠晶撃線  CRYSTAL FRONTLINE
-- 縦スクロール弾幕 STG（4 面構成） / Suityouka Game Machine
-- SD: /games/stg/stg.lua
--
-- 【このファイルの目的】
--   Game Machine 向け STG のサンプル実装です。
--   game_init / game_update / game_draw の三コールバック構成、
--   machine.* API の典型的な使い方、ステージデータの切り出し方を
--   参考にできるようコメントを付けています。
--   API 一覧はプロジェクト直下の LUA_API.md を参照してください。
--
-- 【ホストとの契約】
--   game_init()     … 起動時に 1 回。アセット読込・変数初期化。
--   game_update(dt) … 毎フレーム 1 回。dt はミリ秒（最大 100 にクランプ）。
--                     true を返すとゲーム終了しメニューへ戻る（本サンプルは常に false）。
--   game_draw()     … 1 フレームあたり複数回（画面を 20px 帯に分割したバンド数）。
--                     論理座標は常に 0..machine.width()-1, 0..machine.height()-1。
--                     direct モードでは毎バンド machine.clear 相当の処理が必要。
--
-- 【描画モード】
--   本ゲームは direct モード（既定）を使用。
--   play 中は game_draw 先頭で machine.clear → 背景 → エンティティの順に全描画。
--   layers モード（タイル背景合成）を使う場合は clear を呼ばない点に注意（LUA_API.md 参照）。
--
-- 【状態機械】
--   mode 変数で画面遷移を管理: title → banner → play → alert → play(ボス)
--   → clear → banner … → victory / gameover
--
-- 操作
--   移動 … 十字キー
--   射撃 … OP_RIGHT / NEAR（連射可）
--   決定 … タイトル・リトライ時にジャンプ系ボタン（machine.jump_pressed）
--
-- 画像（任意）: img/*.bin を置くと自動で使用。無ければ図形スプライト。
--   詳細は README.md / generate_images.py 参照
-- ============================================================================

-- 画面サイズは config.hpp の GameConfig に依存（通常 320×240）。
-- ハード依存を避けるため定数は machine API から取得する。
local W = machine.width()
local H = machine.height()

-- ボタン index は config.hpp / LUA_API.md と一致させる。
-- machine.pressed(index) … 押している間 true
-- machine.jump_pressed() … UP, OP_RIGHT, RIGHT, DOWN, NEAR のいずれか
local BTN_RIGHT = 0
local BTN_UP = 1
local BTN_LEFT = 2
local BTN_DOWN = 3
local BTN_OP_LEFT = 4
local BTN_OP_RIGHT = 5
local BTN_NEAR = 7

-- RGB565 色。machine.rgb(r,g,b) で生成し、fill_rect / text 等に渡す。
local COL_BG0 = machine.rgb(4, 6, 18)
local COL_BG1 = machine.rgb(12, 18, 42)
local COL_STAR0 = machine.rgb(30, 40, 70)
local COL_STAR1 = machine.rgb(70, 90, 140)
local COL_HUD = machine.rgb(180, 220, 255)
local COL_HUD_DIM = machine.rgb(90, 110, 150)
local COL_WARN = machine.rgb(255, 80, 100)
local COL_PLAYER = machine.rgb(80, 240, 255)
local COL_PLAYER2 = machine.rgb(20, 140, 200)
local COL_PB = machine.rgb(255, 255, 120)
local COL_EB = machine.rgb(255, 90, 150)
local COL_EB2 = machine.rgb(255, 160, 60)
local COL_SCOUT = machine.rgb(160, 90, 255)
local COL_FIGHT = machine.rgb(255, 100, 80)
local COL_DIVER = machine.rgb(100, 220, 120)
local COL_BOSS = machine.rgb(255, 60, 180)
local COL_BOSS2 = machine.rgb(180, 40, 120)

-- 当たり判定・図形フォールバック用の論理サイズ。
-- 画像を差し替えても当たりを変えたい場合はここと IMG の w/h を揃える。
local PLAYER_W = 16
local PLAYER_H = 16
local PB_W = 4
local PB_H = 10
local EB_W = 5
local EB_H = 5

-- 同時存在数の上限（ヒープ・ループ負荷対策）。増やす場合は実機で FPS を確認。
local PB_SPEED = 0.45
local EB_SPEED = 0.11
local MAX_PBULLETS = 24
local MAX_EBULLETS = 72
local MAX_ENEMIES = 7
local MAX_LIVES = 3
local INV_TIME_MS = 1800

-- SD 上の RGB565 .bin（width*height*2 バイト）。id は load_image の戻り値。
-- path は起動スクリプト（stg.lua）のディレクトリ基準の相対パス。
-- 読込失敗時は id=nil のまま → draw_image_or_rect が図形描画にフォールバック。
local IMG = {
  player = { path = "img/player.bin", w = 16, h = 16, id = nil },
  scout = { path = "img/scout.bin", w = 20, h = 16, id = nil },
  fighter = { path = "img/fighter.bin", w = 28, h = 20, id = nil },
  diver = { path = "img/diver.bin", w = 22, h = 22, id = nil },
  boss = { path = "img/boss.bin", w = 56, h = 44, id = nil },
  pb = { path = "img/pbullet.bin", w = 4, h = 10, id = nil },
  eb = { path = "img/ebullet.bin", w = 5, h = 5, id = nil },
}

-- ステージ定義テーブル。面を増やすときはこの配列に要素を追加する。
--   waves … { kind, count, gap } の配列。gap は同一ウェーブ内の出現間隔（ms）。
--   boss  … hp / w,h（当たり）/ score / pattern（弾幕種別）を指定。
-- 新しい雑魚 kind を足す場合: enemy_size, spawn_enemy, fire_enemy_shot,
-- draw_entities の分岐を同じ名前で追加する。
local STAGES = {
  {
    id = 1,
    title = "STAGE 1",
    subtitle = "ORBIT GATE",
    bg_top = machine.rgb(8, 14, 36),
    bg_bot = machine.rgb(20, 30, 70),
    star_spd = 0.04,
    waves = {
      { kind = "scout", count = 5, gap = 700 },
      { kind = "scout", count = 4, gap = 600 },
      { kind = "fighter", count = 2, gap = 900 },
    },
    boss = {
      name = "GATE KEEPER",
      hp = 36,
      w = 48,
      h = 36,
      score = 5000,
      pattern = "aim",
    },
  },
  {
    id = 2,
    title = "STAGE 2",
    subtitle = "NEBULA DRIFT",
    bg_top = machine.rgb(18, 8, 32),
    bg_bot = machine.rgb(50, 20, 60),
    star_spd = 0.05,
    waves = {
      { kind = "fighter", count = 3, gap = 800 },
      { kind = "scout", count = 4, gap = 500 },
      { kind = "fighter", count = 3, gap = 700 },
      { kind = "diver", count = 3, gap = 650 },
    },
    boss = {
      name = "NEBULA CORE",
      hp = 48,
      w = 52,
      h = 40,
      score = 7000,
      pattern = "fan",
    },
  },
  {
    id = 3,
    title = "STAGE 3",
    subtitle = "ASTEROID BELT",
    bg_top = machine.rgb(24, 16, 10),
    bg_bot = machine.rgb(50, 35, 20),
    star_spd = 0.06,
    waves = {
      { kind = "diver", count = 4, gap = 550 },
      { kind = "fighter", count = 4, gap = 600 },
      { kind = "diver", count = 4, gap = 450 },
      { kind = "scout", count = 6, gap = 400 },
    },
    boss = {
      name = "CRUSHER",
      hp = 56,
      w = 56,
      h = 42,
      score = 9000,
      pattern = "spray",
    },
  },
  {
    id = 4,
    title = "STAGE 4",
    subtitle = "CRYSTAL CORE",
    bg_top = machine.rgb(6, 20, 28),
    bg_bot = machine.rgb(10, 60, 70),
    star_spd = 0.07,
    waves = {
      { kind = "fighter", count = 4, gap = 500 },
      { kind = "diver", count = 4, gap = 450 },
      { kind = "scout", count = 6, gap = 350 },
      { kind = "fighter", count = 4, gap = 450 },
    },
    boss = {
      name = "SUIKYO EMPEROR",
      hp = 80,
      w = 60,
      h = 48,
      score = 15000,
      pattern = "spiral",
    },
  },
}

-- ゲーム状態（モジュール内グローバル）。game_init で初期化、game_update で遷移。
local mode = "title" -- title | banner | play | alert | clear | victory | gameover
local stage_idx = 1
local wave_idx = 1
local wave_spawn_left = 0
local wave_gap_cd = 0
local boss_active = false
local boss_entering = false
local alert_timer = 0
local banner_timer = 0
local clear_timer = 0
local different = 0 --改修中　難易度を追加予定

local player_x = 0
local player_y = 0
local lives = MAX_LIVES
local score = 0
local hi_score = 0
local shoot_cd = 0
local inv_timer = 0
local blink = 0
local scroll_y = 0
local font_ok = false

-- オブジェクトプール代わりの配列。table.remove は末尾から削除して GC 負荷を抑える。
local pbullets = {}
local ebullets = {}
local enemies = {}
local spawn_serial = 0

-- SD セーブ（起動スクリプトのフォルダ内。stg / stg_fast は別ディレクトリなので同名で可）
local SAVE_PATH = "hi_score.dat"

-- ---------------------------------------------------------------------------
-- ユーティリティ
-- ---------------------------------------------------------------------------

-- AABB 矩形の重なり判定（ピクセル座標・整数化は呼び出し側で実施）。
local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function rects_overlap(ax, ay, aw, ah, bx, by, bw, bh)
  return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by
end

local function fire_pressed()
  return machine.pressed(BTN_OP_RIGHT) or machine.pressed(BTN_NEAR)
end

local function confirm_pressed()
  return machine.jump_pressed()
end

-- SD からハイスコアを読み込む（game_init で呼ぶ）
local function load_hi_score()
  if not machine.file_exists(SAVE_PATH) then
    return
  end
  local data, err = machine.load_data(SAVE_PATH)
  if data and type(data.hi_score) == "number" then
    hi_score = math.max(0, math.floor(data.hi_score))
  elseif err then
    print("hi_score load failed:", err)
  end
end

-- 現在の hi_score を SD に保存する
local function save_hi_score()
  local ok, err = machine.save_data(SAVE_PATH, { hi_score = hi_score })
  if not ok then
    print("hi_score save failed:", err)
  end
end

-- ゲームオーバー / 全クリア時: スコアを反映してセーブ
local function record_hi_score_and_save()
  if score > hi_score then
    hi_score = score
  end
  save_hi_score()
end

-- game_init から呼ぶ。フォント・画像は失敗してもゲーム続行（図形／ASCII にフォールバック）。
-- load_font / load_image はヒープ予算内でのみ成功（LUA_API.md の heap_* 参照）。
local function load_assets()
  font_ok = machine.load_font("fonts/game_font.bin") == true
  for _, spec in pairs(IMG) do
    local id = machine.load_image(spec.path, spec.w, spec.h)
    if id then
      spec.id = id
    end
  end
end

-- 画像があれば draw_image_keyed（透過 0xF81F）、なければ draw_proc または fill_rect。
-- 自ゲームでも「アセット任意」のパターンとして流用できる。
local function draw_image_or_rect(spec, x, y, w, h, color, draw_proc)
  if spec and spec.id then
    machine.draw_image_keyed(spec.id, math.floor(x), math.floor(y), 0xF81F)
  elseif draw_proc then
    draw_proc(math.floor(x), math.floor(y), w, h, color)
  else
    machine.fill_rect(math.floor(x), math.floor(y), w, h, color)
  end
end

-- ---------------------------------------------------------------------------
-- 図形スプライト（img/*.bin 未配置時のプレースホルダ）
-- machine.fill_rect のみで描く。バンド外はホスト側でクリップされる。
-- ---------------------------------------------------------------------------

local function draw_player_shape(x, y, w, h, _)
  local cx = x + w // 2
  machine.fill_rect(cx - 2, y, 4, h - 4, COL_PLAYER)
  machine.fill_rect(cx - 6, y + h - 8, 12, 6, COL_PLAYER2)
  machine.fill_rect(cx - 1, y + 2, 2, 6, machine.rgb(255, 255, 255))
end

local function draw_scout_shape(x, y, w, h, _)
  machine.fill_rect(x + 4, y + 2, w - 8, h - 6, COL_SCOUT)
  machine.fill_rect(x + 2, y + h - 6, w - 4, 4, machine.rgb(120, 60, 200))
end

local function draw_fighter_shape(x, y, w, h, _)
  machine.fill_rect(x + 6, y + 2, w - 12, h - 8, COL_FIGHT)
  machine.fill_rect(x + 2, y + 6, 6, h - 12, COL_FIGHT)
  machine.fill_rect(x + w - 8, y + 6, 6, h - 12, COL_FIGHT)
end

local function draw_diver_shape(x, y, w, h, _)
  machine.fill_rect(x + w // 2 - 3, y, 6, h - 4, COL_DIVER)
  machine.fill_rect(x + 2, y + 6, w - 4, 6, machine.rgb(60, 180, 100))
end

local function draw_boss_shape(x, y, w, h, pulse)
  local p = pulse or 0
  local c1 = COL_BOSS
  local c2 = COL_BOSS2
  machine.fill_rect(x + 8, y + 6 + p, w - 16, h - 12, c1)
  machine.fill_rect(x + 4, y + 10, 8, h - 20, c2)
  machine.fill_rect(x + w - 12, y + 10, 8, h - 20, c2)
  machine.fill_rect(x + w // 2 - 6, y + 2, 12, 8, machine.rgb(255, 200, 220))
end

-- ---------------------------------------------------------------------------
-- 弾・敵の生成と更新用データ
-- 座標は float で保持し、描画時に math.floor する（サブピクセル移動）。
-- ---------------------------------------------------------------------------

-- kind ごとの表示・当たりサイズ。boss は STAGES[].boss の w/h を優先。
local function enemy_size(kind, boss_def)
  if kind == "boss" and boss_def then
    return boss_def.w, boss_def.h
  end
  if kind == "fighter" then return 28, 20 end
  if kind == "diver" then return 22, 22 end
  return 20, 16
end

local function add_ebullet(x, y, vx, vy, big)
  if #ebullets >= MAX_EBULLETS then
    return
  end
  ebullets[#ebullets + 1] = {
    x = x, y = y,
    vx = vx or 0,
    vy = vy or 1,
    big = big or false,
  }
end

local function spawn_enemy(kind, boss_def)
  if #enemies >= MAX_ENEMIES then
    return false
  end
  spawn_serial = spawn_serial + 1
  local ew, eh = enemy_size(kind, boss_def)
  local e = {
    kind = kind,
    x = 20 + (spawn_serial * 53) % (W - ew - 40),
    y = -eh - 4,
    w = ew,
    h = eh,
    hp = 1,
    dir = (spawn_serial % 2 == 0) and 1 or -1,
    shoot_cd = 400 + (spawn_serial % 5) * 80,
    move_cd = 0,
    phase = 0,
  }
  if kind == "scout" then
    e.y = 24 + (spawn_serial % 3) * 18
    e.hp = 1
    e.shoot_cd = 700
  elseif kind == "fighter" then
    e.y = 32
    e.hp = 2
    e.shoot_cd = 900
  elseif kind == "diver" then
    e.x = 30 + (spawn_serial * 71) % (W - ew - 60)
    e.y = -eh
    e.vy = 0.12
    e.hp = 2
    e.shoot_cd = 500
  elseif kind == "boss" and boss_def then
    -- 画面上部へスライドイン後、左右に振動しながら弾幕（pattern 参照）。
    e.x = (W - boss_def.w) // 2
    e.y = -boss_def.h - 8
    e.w = boss_def.w
    e.h = boss_def.h
    e.hp = boss_def.hp
    e.max_hp = boss_def.hp
    e.name = boss_def.name
    e.pattern = boss_def.pattern
    e.score = boss_def.score
    e.shoot_cd = 1200
    e.entering = true
    boss_entering = true
  end
  enemies[#enemies + 1] = e
  return true
end

local function fire_player_bullet()
  if #pbullets >= MAX_PBULLETS then
    return
  end
  pbullets[#pbullets + 1] = {
    x = player_x + (PLAYER_W - PB_W) // 2,
    y = player_y - PB_H,
  }
end

local function fire_enemy_shot(e, px, py)
  local cx = e.x + e.w * 0.5
  local cy = e.y + e.h
  if e.kind == "scout" then
    add_ebullet(cx - EB_W * 0.5, cy, 0, 1, false)
  elseif e.kind == "fighter" then
    add_ebullet(cx - EB_W * 0.5 - 10, cy, -0.6, 1, false)
    add_ebullet(cx - EB_W * 0.5, cy, 0, 1, false)
    add_ebullet(cx - EB_W * 0.5 + 10, cy, 0.6, 1, false)
  elseif e.kind == "diver" then
    add_ebullet(cx - EB_W * 0.5, cy, 0, 1.2, false)
    add_ebullet(cx - EB_W * 0.5 - 6, cy, -0.4, 1, false)
    add_ebullet(cx - EB_W * 0.5 + 6, cy, 0.4, 1, false)
  elseif e.kind == "boss" then
    -- ボス弾幕パターン名は STAGES[].boss.pattern と対応:
    --   aim … 自機狙い + 左右弾
    --   fan … 扇状 5 方向
    --   spray … 8 方向ローテーション
    --   spiral … 双方向スパイラル + たまにサイド弾
    local pat = e.pattern
    if pat == "aim" then
      local dx = px - cx
      local dy = py - cy
      local len = math.sqrt(dx * dx + dy * dy)
      if len < 1 then len = 1 end
      local sp = 1.1
      add_ebullet(cx, cy, dx / len * sp, dy / len * sp, true)
      add_ebullet(cx - 12, cy, -0.3, 1, false)
      add_ebullet(cx + 12, cy, 0.3, 1, false)
    elseif pat == "fan" then
      for i = -2, 2 do
        add_ebullet(cx + i * 10, cy, i * 0.35, 1, i == 0)
      end
    elseif pat == "spray" then
      for i = 0, 7 do
        local ang = (e.phase + i) * 0.78
        add_ebullet(cx, cy, math.sin(ang) * 1.2, math.cos(ang) * 1.2 + 0.4, false)
      end
    elseif pat == "spiral" then
      local ang = e.phase * 0.22
      add_ebullet(cx, cy, math.sin(ang) * 1.4, math.cos(ang) * 1.4 + 0.2, true)
      add_ebullet(cx, cy, math.sin(ang + 3.14) * 1.4, math.cos(ang + 3.14) * 1.4 + 0.2, true)
      if (e.phase % 3) == 0 then
        add_ebullet(cx - 16, cy, -0.5, 1.2, false)
        add_ebullet(cx + 16, cy, 0.5, 1.2, false)
      end
    end
    e.phase = e.phase + 1
  end
end

-- ---------------------------------------------------------------------------
-- リセット・ステージ進行
-- begin_stage / reset_run でウェーブ・ボスフラグをまとめて初期化。
-- ---------------------------------------------------------------------------

local function clear_bullets()
  pbullets = {}
  ebullets = {}
end

local function reset_player_pos()
  player_x = (W - PLAYER_W) // 2
  player_y = H - PLAYER_H - 14
end

local function begin_stage(idx)
  stage_idx = idx
  wave_idx = 1
  wave_spawn_left = 0
  wave_gap_cd = 1200
  boss_active = false
  boss_entering = false
  enemies = {}
  clear_bullets()
  reset_player_pos()
  local st = STAGES[stage_idx]
  if st and st.waves[1] then
    wave_spawn_left = st.waves[1].count
    wave_gap_cd = 400
  end
end

local function reset_run()
  stage_idx = 1
  lives = MAX_LIVES
  score = 0
  inv_timer = 0
  shoot_cd = 0
  scroll_y = 0
  spawn_serial = 0
  begin_stage(1)
  mode = "banner"
  banner_timer = 0
end

-- 全ウェーブ消化かつ画面上の敵ゼロ → 警告演出 → ボス出現。
local function start_boss_alert()
  mode = "alert"
  alert_timer = 0
  clear_bullets()
end

local function spawn_boss()
  local st = STAGES[stage_idx]
  if st and st.boss then
    spawn_enemy("boss", st.boss)
    boss_active = true
  end
end

local function on_stage_cleared()
  if stage_idx >= #STAGES then
    mode = "victory"
    record_hi_score_and_save()
    blink = 0
    return
  end
  stage_idx = stage_idx + 1
  mode = "clear"
  clear_timer = 0
end

local function on_player_dead()
  lives = lives - 1
  clear_bullets()
  if lives <= 0 then
    mode = "gameover"
    blink = 0
    record_hi_score_and_save()
    return
  end
  reset_player_pos()
  inv_timer = INV_TIME_MS
end

-- ---------------------------------------------------------------------------
-- 更新（game_update から mode=="play" のとき update_play が呼ばれる）
-- dt ベースの移動: speed * dt でフレームレートに依存しない。
-- ---------------------------------------------------------------------------

-- 自機は画面下半分にクランプ（縦 STG の可動域制限の例）。
local function update_player(dt)
  local speed = 0.24 * dt
  if machine.pressed(BTN_LEFT) then player_x = player_x - speed end
  if machine.pressed(BTN_RIGHT) then player_x = player_x + speed end
  if machine.pressed(BTN_UP) then player_y = player_y - speed end
  if machine.pressed(BTN_DOWN) then player_y = player_y + speed end
  player_x = clamp(player_x, 4, W - PLAYER_W - 4)
  player_y = clamp(player_y, H // 2 - 8, H - PLAYER_H - 6)

  shoot_cd = shoot_cd - dt
  if fire_pressed() and shoot_cd <= 0 then
    fire_player_bullet()
    shoot_cd = 110
  end
end

local function update_pbullets(dt)
  local move = PB_SPEED * dt
  for i = #pbullets, 1, -1 do
    local b = pbullets[i]
    b.y = b.y - move
    if b.y + PB_H < 0 then
      table.remove(pbullets, i)
    end
  end
end

local function update_ebullets(dt)
  local move = EB_SPEED * dt
  for i = #ebullets, 1, -1 do
    local b = ebullets[i]
    b.x = b.x + b.vx * move
    b.y = b.y + b.vy * move
    if b.y > H + 10 or b.x < -14 or b.x > W + 14 or b.y < -14 then
      table.remove(ebullets, i)
    end
  end
end

local function update_enemies(dt, px, py)
  for i = #enemies, 1, -1 do
    local e = enemies[i]
    if e.kind == "boss" then
      if e.entering then
        e.y = e.y + 0.05 * dt
        if e.y >= 28 then
          e.y = 28
          e.entering = false
          boss_entering = false
        end
      else
        e.x = e.x + e.dir * 0.06 * dt
        if e.x < 12 then e.x = 12; e.dir = 1 end
        if e.x + e.w > W - 12 then e.x = W - 12 - e.w; e.dir = -1 end
      end
    elseif e.kind == "diver" then
      e.y = e.y + (e.vy or 0.12) * dt
      e.x = e.x + e.dir * 0.05 * dt
    else
      e.x = e.x + e.dir * 0.08 * dt
      if e.x < 8 then e.x = 8; e.dir = 1
      elseif e.x + e.w > W - 8 then e.x = W - 8 - e.w; e.dir = -1 end
    end

    if not e.entering then
      e.shoot_cd = e.shoot_cd - dt
      if e.shoot_cd <= 0 then
        fire_enemy_shot(e, px, py)
        if e.kind == "boss" then
          e.shoot_cd = 700
        elseif e.kind == "fighter" then
          e.shoot_cd = 1000
        else
          e.shoot_cd = 850
        end
      end
    end

    if e.kind ~= "boss" and e.y > H + 20 then
      table.remove(enemies, i)
    end
  end
end

local function check_player_hit(px, py)
  if inv_timer > 0 then
    return false
  end
  for i = 1, #ebullets do
    local b = ebullets[i]
    local bw = b.big and EB_W + 2 or EB_W
    local bh = b.big and EB_H + 2 or EB_H
    if rects_overlap(px, py, PLAYER_W, PLAYER_H, math.floor(b.x), math.floor(b.y), bw, bh) then
      return true
    end
  end
  for i = 1, #enemies do
    local e = enemies[i]
    if not e.entering and rects_overlap(px, py, PLAYER_W, PLAYER_H, math.floor(e.x), math.floor(e.y), e.w, e.h) then
      return true
    end
  end
  return false
end

local function check_shots_hit()
  for j = #enemies, 1, -1 do
    local e = enemies[j]
    local ex, ey = math.floor(e.x), math.floor(e.y)
    local hit = false
    for i = #pbullets, 1, -1 do
      local b = pbullets[i]
      if rects_overlap(math.floor(b.x) - 1, math.floor(b.y) - 1, PB_W + 2, PB_H + 2,
          ex - 2, ey - 2, e.w + 4, e.h + 4) then
        table.remove(pbullets, i)
        e.hp = e.hp - 1
        hit = true
        break
      end
    end
    if hit and e.hp <= 0 then
      if e.kind == "boss" then
        score = score + (e.score or 5000)
        boss_active = false
        on_stage_cleared()
      else
        score = score + (e.kind == "fighter" and 200 or (e.kind == "diver" and 150 or 100))
      end
      table.remove(enemies, j)
    end
  end
end

local function wave_active()
  local st = STAGES[stage_idx]
  if not st then return false end
  if wave_idx <= #st.waves then
    return true
  end
  return false
end

-- ウェーブ管理: 画面上に敵がいる間は次を出さない。gap は ms 単位のクールダウン。
local function update_waves(dt)
  if boss_active or boss_entering or mode ~= "play" then
    return
  end
  if #enemies > 0 then
    return
  end
  local st = STAGES[stage_idx]
  if not st then return end

  if wave_idx > #st.waves then
    start_boss_alert()
    return
  end

  wave_gap_cd = wave_gap_cd - dt
  if wave_gap_cd > 0 then
    return
  end

  local wave = st.waves[wave_idx]
  if wave_spawn_left <= 0 then
    wave_idx = wave_idx + 1
    if wave_idx > #st.waves then
      start_boss_alert()
      return
    end
    wave = st.waves[wave_idx]
    wave_spawn_left = wave.count
    wave_gap_cd = 500
    return
  end

  if spawn_enemy(wave.kind, nil) then
    wave_spawn_left = wave_spawn_left - 1
    wave_gap_cd = wave.gap
  end
end

local function update_play(dt)
  inv_timer = math.max(0, inv_timer - dt)
  local st = STAGES[stage_idx]
  if st then
    scroll_y = scroll_y + st.star_spd * dt
  end

  update_player(dt)
  update_pbullets(dt)
  update_enemies(dt, player_x + PLAYER_W * 0.5, player_y)
  update_ebullets(dt)
  check_shots_hit()

  local px, py = math.floor(player_x), math.floor(player_y)
  if check_player_hit(px, py) then
    on_player_dead()
    return
  end

  update_waves(dt)
end

-- ---------------------------------------------------------------------------
-- 描画（game_draw）
-- 各 mode ごとに専用関数。play 時は clear → 背景 → エンティティ → HUD の順。
-- machine.text は load_font 済みなら UTF-8 日本語、未 load なら 8x8 ASCII。
-- ---------------------------------------------------------------------------

-- 縦スクロール風背景: 2 色グラデ + 星（scroll_y は update_play で進行）。
local function draw_bg()
  local st = STAGES[stage_idx] or STAGES[1]
  machine.fill_rect(0, 0, W, H // 2, st.bg_top)
  machine.fill_rect(0, H // 2, W, H - H // 2, st.bg_bot)
  local seed = math.floor(scroll_y) % 240
  for i = 0, 18 do
    local sx = (i * 47 + seed * 3) % W
    local sy = (i * 29 + seed * 5) % H
    machine.fill_rect(sx, sy, 2, 2, (i % 3 == 0) and COL_STAR1 or COL_STAR0)
  end
end

local function draw_title_bg()
  machine.clear(COL_BG0)
  for y = 0, H - 1, 6 do
    local c = machine.rgb(8 + y // 8, 12 + y // 6, 28 + y // 4)
    machine.fill_rect(0, y, W, 6, c)
  end
  local t = machine.time_ms() * 0.001
  for i = 0, 24 do
    local sx = (i * 37 + math.floor(t * 40 + i * 11)) % W
    local sy = (i * 23 + math.floor(t * 25)) % H
    machine.fill_rect(sx, sy, 2, 2, COL_STAR1)
  end
end

local function draw_hud()
  local st = STAGES[stage_idx]
  machine.text(6, 4, "SC:" .. math.floor(score), COL_HUD, COL_BG0)
  machine.text(W - 72, 4, "STG " .. stage_idx, COL_HUD, COL_BG0)
  local life_str = ""
  for i = 1, lives do life_str = life_str .. "*" end
  machine.text(6, 16, life_str, COL_WARN, COL_BG0)
  if st then
    machine.text(6, 28, st.subtitle, COL_HUD_DIM, COL_BG0)
  end
end

local function draw_boss_bar()
  for i = 1, #enemies do
    local e = enemies[i]
    if e.kind == "boss" and e.max_hp then
      local bw = 200
      local bx = (W - bw) // 2
      local by = 6
      local ratio = e.hp / e.max_hp
      machine.fill_rect(bx, by, bw, 6, machine.rgb(30, 30, 50))
      machine.fill_rect(bx, by, math.floor(bw * ratio), 6, COL_WARN)
      machine.text(bx, by + 8, e.name or "BOSS", COL_HUD, COL_BG0)
      break
    end
  end
end

local function draw_entities(pulse)
  for i = 1, #ebullets do
    local b = ebullets[i]
    local col = b.big and COL_EB2 or COL_EB
    draw_image_or_rect(IMG.eb, b.x, b.y, EB_W, EB_H, col, function(x, y, w, h, c)
      machine.fill_rect(x, y, w, h, c)
    end)
  end
  for i = 1, #pbullets do
    local b = pbullets[i]
    draw_image_or_rect(IMG.pb, b.x, b.y, PB_W, PB_H, COL_PB, function(x, y, w, h, c)
      machine.fill_rect(x, y, w, h, c)
    end)
  end
  for i = 1, #enemies do
    local e = enemies[i]
    local x, y = math.floor(e.x), math.floor(e.y)
    if e.kind == "boss" then
      draw_image_or_rect(IMG.boss, x, y, e.w, e.h, COL_BOSS, function(px, py, pw, ph, _)
        draw_boss_shape(px, py, pw, ph, pulse)
      end)
    elseif e.kind == "fighter" then
      draw_image_or_rect(IMG.fighter, x, y, e.w, e.h, COL_FIGHT, draw_fighter_shape)
    elseif e.kind == "diver" then
      draw_image_or_rect(IMG.diver, x, y, e.w, e.h, COL_DIVER, draw_diver_shape)
    else
      draw_image_or_rect(IMG.scout, x, y, e.w, e.h, COL_SCOUT, draw_scout_shape)
    end
  end

  if inv_timer <= 0 or (math.floor(inv_timer / 80) % 2 == 0) then
    draw_image_or_rect(IMG.player, player_x, player_y, PLAYER_W, PLAYER_H, COL_PLAYER, draw_player_shape)
  end
end

-- 8px グリッド前提の簡易センタリング（#text * 8）。日本語は字幅が異なる点に注意。
local function draw_center_text(y, text, col)
  local x = (W - #text * 8) // 2
  machine.text(x, y, text, col)
end

local function draw_title()
  draw_title_bg()
  if font_ok then
    machine.text(72, 52, "翠晶撃線", machine.rgb(120, 255, 240), COL_BG0)
  end
  draw_center_text(78, "CRYSTAL FRONTLINE", machine.rgb(255, 220, 120))
  draw_center_text(108, "4 STAGE SHOOTER", COL_HUD_DIM)
  if (math.floor(machine.time_ms() / 500) % 2) == 0 then
    draw_center_text(150, "PRESS BUTTON", COL_HUD)
  end
  draw_center_text(176, "MOVE: D-PAD", COL_HUD_DIM)
  draw_center_text(192, "FIRE: NEAR", COL_HUD_DIM)
  draw_center_text(208, "Kawashiro Electric　@ 2026", COL_HUD_DIM)
  if hi_score > 0 then
    draw_center_text(214, "HI " .. hi_score, COL_HUD_DIM)
  end
end

local function draw_banner()
  draw_bg()
  local st = STAGES[stage_idx]
  if st then
    draw_center_text(96, st.title, COL_HUD)
    draw_center_text(116, st.subtitle, machine.rgb(255, 200, 100))
  end
end

local function draw_alert()
  draw_bg()
  draw_entities(math.floor(machine.time_ms() / 200) % 2)
  draw_hud()
  if (math.floor(alert_timer / 200) % 2) == 0 then
    draw_center_text(100, "!! WARNING !!", COL_WARN)
    draw_center_text(120, "BOSS APPROACH", COL_WARN)
  end
end

local function draw_clear()
  draw_bg()
  draw_center_text(100, "STAGE CLEAR", machine.rgb(120, 255, 180))
  local st = STAGES[stage_idx - 1]
  if st then
    draw_center_text(120, st.subtitle .. " CLEARED", COL_HUD)
  end
end

local function draw_victory()
  draw_title_bg()
  if font_ok then
    machine.text(84, 80, "全撃破！", machine.rgb(255, 240, 120), COL_BG0)
  end
  draw_center_text(108, "MISSION COMPLETE", machine.rgb(120, 255, 200))
  draw_center_text(132, "SCORE " .. score, COL_HUD)
  draw_center_text(148, "HI-SCORE " .. hi_score, COL_HUD_DIM)
  if (math.floor(blink / 500) % 2) == 0 then
    draw_center_text(176, "PRESS TO TITLE", COL_HUD_DIM)
  end
end

local function draw_gameover()
  draw_bg()
  machine.fill_rect(0, 0, W, H, machine.rgb(20, 0, 0))
  for y = 40, 200, 12 do
    machine.fill_rect(20, y, W - 40, 2, machine.rgb(60, 10, 20))
  end
  draw_center_text(72, "GAME OVER", COL_WARN)
  local st = STAGES[stage_idx]
  if st then
    draw_center_text(96, "REACHED: " .. st.title, COL_HUD_DIM)
    draw_center_text(112, st.subtitle, COL_HUD_DIM)
  end
  draw_center_text(136, "SCORE " .. score, COL_HUD)
  if hi_score > 0 then
    draw_center_text(152, "HI-SCORE " .. hi_score, COL_HUD_DIM)
  end
  if (math.floor(blink / 400) % 2) == 0 then
    draw_center_text(188, "OP_LEFT: TITLE", COL_HUD_DIM)
    draw_center_text(204, "OTHER: RETRY", COL_HUD)
  end
end

-- ---------------------------------------------------------------------------
-- エントリ（ホストが require せずグローバル関数として呼ぶ）
-- ---------------------------------------------------------------------------

function game_init()
  -- 1 回だけ: アセットとタイトル画面用の初期状態。
  load_assets()
  mode = "title"
  hi_score = 0
  load_hi_score()
  blink = 0
  enemies = {}
  clear_bullets()
  reset_player_pos()
end

function game_update(dt)
  -- 毎フレーム: 入力と mode に応じたロジック。false で継続、true でメニュー復帰。
  blink = blink + dt

  if mode == "title" then
    -- ジャンプ系ボタンで新規ゲーム開始。
    if confirm_pressed() then
      reset_run()
    end
    return false
  end

  if mode == "banner" then
    -- ステージ名表示（2.2 秒）後に play へ。
    banner_timer = banner_timer + dt
    if banner_timer >= 2200 then
      mode = "play"
    end
    return false
  end

  if mode == "alert" then
    -- ボス警告。一定時間後に spawn_boss して play に戻る。
    alert_timer = alert_timer + dt
    if alert_timer >= 1800 then
      mode = "play"
      spawn_boss()
    end
    return false
  end

  if mode == "clear" then
    -- 面クリア演出後、次面の banner へ（最終面クリア時は on_stage_cleared で victory）。
    clear_timer = clear_timer + dt
    if clear_timer >= 2500 then
      begin_stage(stage_idx)
      mode = "banner"
      banner_timer = 0
    end
    return false
  end

  if mode == "victory" then
    if confirm_pressed() and blink > 500 then
      mode = "title"
    end
    return false
  end

  if mode == "gameover" then
    -- OP_LEFT でタイトル、それ以外のジャンプ系で同ステージからリトライ。
    if blink > 500 then
      if machine.pressed(BTN_OP_LEFT) then
        mode = "title"
      elseif confirm_pressed() then
        reset_run()
      end
    end
    return false
  end

  update_play(dt)
  return false
end

function game_draw()
  -- 毎バンド呼ばれる。mode ごとに早期 return する画面もある。
  -- play 中は毎バンド machine.clear する（direct モードの典型パターン）。
  if mode == "title" then
    draw_title()
    return
  end
  if mode == "banner" then
    draw_banner()
    return
  end
  if mode == "alert" then
    draw_alert()
    return
  end
  if mode == "clear" then
    draw_clear()
    return
  end
  if mode == "victory" then
    draw_victory()
    return
  end
  if mode == "gameover" then
    draw_gameover()
    return
  end

  machine.clear(COL_BG0)
  draw_bg()
  draw_entities(math.floor(machine.time_ms() / 300) % 2)
  draw_hud()
  if boss_active then
    draw_boss_bar()
  end
end
