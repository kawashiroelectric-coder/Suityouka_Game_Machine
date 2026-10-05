-- ============================================================================
-- 翠灯夜行 ～ Jade Lantern Night
-- 東方Project風 縦スクロール弾幕STG（全6面） / Suityouka Game Machine
-- SD: /games/JadeLantern/JadeLantern.lua
--
-- 【30FPS 設計】（game_machine_main.cpp → LuaInterpreter のループを前提）
--  * ゲームロジックは 1/30 秒固定ステップ。game_update の dt を積算して進める
--    （描画が遅れても最大 2 ステップ追いつき、ゲーム速度は一定）。
--  * 描画は game_draw 1 回を C 側で録画 → 変化した 20px 帯だけ LCD へ転送される。
--    LCD SPI は実効約 37.5MHz なので全 12 帯転送で約 33ms かかる。
--    → 弾が飛ぶプレイフィールドを y=20..199 の 9 帯に限定し、
--      上 1 帯・下 2 帯は静的（会話・カットイン時以外は転送されない）にしている。
--  * 録画非対応 API（draw_tilemap / 回転 affine / xform）は使わない。
--    座標は必ず整数（//1）で渡す。set_font_scale も使わない（録画に残らない）。
--  * machine.pressed() は呼ぶたび I2C 読み出し（約 0.4ms）なので
--    1 フレームあたり 6 回までに抑える（方向 4 + 交互に 2）。
--  * 弾・自機弾・アイテムは並列配列プール（テーブル生成なし → GC 負荷なし）。
--
-- 操作
--   十字キー   移動
--   FAR        ショット（押しっぱなし） / 決定 / 会話送り
--   NEAR       ボム「灯籠結界」 / キャンセル（会話中は長押しで早送り）
--   OP_RIGHT   押すたびに 低速移動（当たり判定を表示） ⇔ 通常移動
--   OP_LEFT    ポーズ / ポーズ解除
--   ※FAR・NEAR が押しやすい位置、OP_RIGHT・OP_LEFT は START/SELECT 的な位置のため
-- ============================================================================

local M = machine
local W, H = M.width(), M.height()
local fill_rect = M.fill_rect
local fill_alpha = M.fill_rect_alpha
local blitk = M.draw_image_keyed
local text = M.text
local rgb = M.rgb
local pressed = M.pressed
local draw_circle = M.draw_circle
local fill_circle = M.fill_circle
local draw_line = M.draw_line
local sin, cos, atan, sqrt, floor, abs = math.sin, math.cos, math.atan, math.sqrt, math.floor, math.abs
local min, max, random = math.min, math.max, math.random
local PI = math.pi
local TAU = PI * 2
local HALFPI = PI / 2

local TEST = rawget(_G, "JL_TEST")   -- PC 検証用（実機では nil）

-- 画面遷移・演出用の状態（ローカル変数上限 200 対策でテーブルにまとめる）
local S = {
  paused = false, pause_cur = 1, intro_t = 0, msg = nil, msg_t = 0, msg_c = 0xFFFF,
  clear_t = 0, clear_bonus = 0, menu_cur = 1, last_power_lv = 0, flash_t = 0,
  gphase_before_continue = "stage", hud_cache = {}, title_mode = "main",
  end_i = 1, end_shown = 0, end_t = 0, staff_y = 0, staff_h = 0,
}
local F = {}   -- 非ホットパス関数の置き場


-- ---------------------------------------------------------------------------
-- 画面レイアウト
-- ---------------------------------------------------------------------------
local PF_X, PF_Y, PF_W, PF_H = 4, 20, 224, 180
local PF_R, PF_B = PF_X + PF_W, PF_Y + PF_H     -- 228, 200
local PF_CX = PF_X + PF_W // 2                  -- 116
local PN_X = 234                                -- 右パネル
local BOX_Y = 202                               -- 下部テキスト枠

local STEP_MS = 1000 / 30

-- ボタン
local B_RIGHT, B_UP, B_LEFT, B_DOWN = 0, 1, 2, 3
local B_OPL, B_OPR, B_FAR, B_NEAR = 4, 5, 6, 7

-- 色
local C = {
  BLACK = rgb(0, 0, 0),
  WHITE = rgb(255, 255, 255),
  FRAME = rgb(22, 30, 36),
  FRAME2 = rgb(40, 70, 64),
  FRAME3 = rgb(90, 150, 130),
  GOLD = rgb(240, 210, 120),
  JADE = rgb(110, 240, 190),
  TXT = rgb(230, 240, 235),
  DIM = rgb(130, 150, 145),
  RED = rgb(255, 90, 90),
  PINK = rgb(255, 140, 190),
  ORANGE = rgb(255, 170, 60),
  BOX = rgb(10, 16, 20),
  BLUE = rgb(120, 180, 255),
}

-- ---------------------------------------------------------------------------
-- データ・アセット
-- ---------------------------------------------------------------------------
local DATA = M.load_return("data.lua")
if type(DATA) ~= "table" then
  DATA = { stages = {}, names = {}, titles = {}, ending = {}, staff = {}, manual = {} }
end
local BGM_LEN = M.load_return("bgm/bgm_len.lua")
if type(BGM_LEN) ~= "table" then BGM_LEN = {} end

local img = {}          -- 名前 → image id
local font_ok = false

function F.load_img(name, path, w, h)
  if img[name] then return img[name] end
  local id = M.load_image(path, w, h)
  if id then img[name] = id end
  return id
end

function F.free_img(name)
  if img[name] then
    M.free_image(img[name])
    img[name] = nil
  end
end

-- ---------------------------------------------------------------------------
-- サウンド
-- ---------------------------------------------------------------------------
local bgm_cur = nil
local bgm_timer = 0
local se_cd = 0
local SE_PRIO = { death = true, bomb = true, spell = true, bossdie = true, extend = true, select = true }

function F.play_bgm(name)
  if bgm_cur == name then return end
  bgm_cur = name
  M.stop_sound()
  if name then
    M.play_wav("bgm/" .. name .. ".wav")
    bgm_timer = BGM_LEN[name] or 60000
  end
end

function F.service_bgm(ms)
  if bgm_cur then
    bgm_timer = bgm_timer - ms
    if bgm_timer <= 0 then
      -- play_wav はループしないので曲長で再スタート
      M.play_wav("bgm/" .. bgm_cur .. ".wav")
      bgm_timer = BGM_LEN[bgm_cur] or 60000
    end
  end
end

-- play_se は毎回 SD から RAM に読み込むので間引く
local function se(name)
  if se_cd > 0 and not SE_PRIO[name] then return end
  M.play_se("se/" .. name .. ".wav")
  se_cd = 3
end

local tone_cd = 0
local function tone(freq, ms)
  if tone_cd > 0 then return end
  M.play_tone(freq, ms)
  tone_cd = 2
end

-- ---------------------------------------------------------------------------
-- 入力（フレーム単位でキャッシュ）
-- ---------------------------------------------------------------------------
local btn = { [0] = false, false, false, false, false, false, false, false }
local prev = { [0] = false, false, false, false, false, false, false, false }
local edge = { [0] = false, false, false, false, false, false, false, false }
local poll_phase = 0

function F.poll_input(all)
  for i = 0, 7 do prev[i] = btn[i] end
  btn[B_RIGHT] = pressed(B_RIGHT)
  btn[B_UP] = pressed(B_UP)
  btn[B_LEFT] = pressed(B_LEFT)
  btn[B_DOWN] = pressed(B_DOWN)
  poll_phase = 1 - poll_phase
  if all then
    for i = 4, 7 do btn[i] = pressed(i) end
  elseif poll_phase == 0 then
    btn[B_OPR] = pressed(B_OPR)
    btn[B_FAR] = pressed(B_FAR)
  else
    btn[B_OPL] = pressed(B_OPL)
    btn[B_NEAR] = pressed(B_NEAR)
  end
  -- エッジはステップで消費されるまで保持（ステップ 0 回のフレームで取りこぼさない）
  for i = 0, 7 do edge[i] = edge[i] or (btn[i] and not prev[i]) end
end

-- ---------------------------------------------------------------------------
-- セーブ
-- ---------------------------------------------------------------------------
local SAVE = "save.dat"
local save = { hi = { 0, 0, 0 }, clear = { 0, 0, 0 } }

function F.load_save()
  if not M.file_exists(SAVE) then return end
  local d = M.load_data(SAVE)
  if type(d) == "table" then
    if type(d.hi) == "table" then
      for i = 1, 3 do save.hi[i] = tonumber(d.hi[i]) or 0 end
    end
    if type(d.clear) == "table" then
      for i = 1, 3 do save.clear[i] = tonumber(d.clear[i]) or 0 end
    end
  end
end

function F.write_save()
  M.save_data(SAVE, save)
end

-- ---------------------------------------------------------------------------
-- 弾スプライト表（img/bullets.bin 128x40）
--   種類 k: 0=小玉6 1=中玉10 2=大玉16 3=星8 / 色 c: 0赤 1橙 2黄 3緑 4水 5青 6紫 7白
--   弾タイプ番号 t = k*8 + c
-- ---------------------------------------------------------------------------
local BSX, BSY, BSZ, BHF, BHR2, BGR2 = {}, {}, {}, {}, {}, {}
do
local BK_SIZE = { [0] = 6, 10, 16, 8 }
local BK_Y = { [0] = 0, 6, 16, 32 }
local BK_R = { [0] = 2.0, 3.5, 6.0, 2.5 }
for k = 0, 3 do
  for c = 0, 7 do
    local t = k * 8 + c
    local s = BK_SIZE[k]
    BSX[t] = c * s
    BSY[t] = BK_Y[k]
    BSZ[t] = s
    BHF[t] = s // 2
    BHR2[t] = (BK_R[k] + 1.5) * (BK_R[k] + 1.5)
    BGR2[t] = (BK_R[k] + 12) * (BK_R[k] + 12)
  end
end
end
local SMALL, MID, BIG, STAR = 0, 8, 16, 24
local RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, PURPLE, WHITE = 0, 1, 2, 3, 4, 5, 6, 7

-- ---------------------------------------------------------------------------
-- プレイフィールドにクリップして描画
-- ---------------------------------------------------------------------------
local function cblit(id, x, y, sx, sy, w, h)
  if x >= PF_X and y >= PF_Y and x + w <= PF_R and y + h <= PF_B then
    blitk(id, x, y, sx, sy, w, h)
    return
  end
  if x < PF_X then
    local d = PF_X - x
    sx = sx + d; w = w - d; x = PF_X
  end
  if y < PF_Y then
    local d = PF_Y - y
    sy = sy + d; h = h - d; y = PF_Y
  end
  if x + w > PF_R then w = PF_R - x end
  if y + h > PF_B then h = PF_B - y end
  if w > 0 and h > 0 then
    blitk(id, x, y, sx, sy, w, h)
  end
end

function F.crect(x, y, w, h, c)
  if x < PF_X then w = w - (PF_X - x); x = PF_X end
  if y < PF_Y then h = h - (PF_Y - y); y = PF_Y end
  if x + w > PF_R then w = PF_R - x end
  if y + h > PF_B then h = PF_B - y end
  if w > 0 and h > 0 then fill_rect(x, y, w, h, c) end
end

local function text_w(s)
  -- ASCII 6px / 全角 12px（PixelMplus12）
  local w = 0
  for _, cp in utf8.codes(s) do
    w = w + (cp < 128 and 6 or 12)
  end
  return w
end

local function ctext(cx, y, s, c)
  text(cx - text_w(s) // 2, y, s, c)
end

-- 数字を桁区切りなしで右寄せ
local function rtext(rx, y, s, c)
  text(rx - text_w(s), y, s, c)
end

-- ---------------------------------------------------------------------------
-- ゲーム状態
-- ---------------------------------------------------------------------------
local scene = "title"      -- title / game / ending / staff / theend
local diff = 2             -- 1 Easy / 2 Normal / 3 Hard
local DIFF_NAME = { "EASY", "NORMAL", "HARD" }
local DIFF_COL = { rgb(120, 230, 140), rgb(120, 180, 255), rgb(255, 110, 110) }
local DM = { 0.55, 1.0, 1.6 }    -- 弾数倍率
local SM = { 0.8, 1.0, 1.22 }    -- 弾速倍率

local stage = 1
local gphase = "stage"     -- stage / talk / boss / post / clear / dead / continue
local gtime = 0            -- ステージ内ステップ
local frame = 0            -- 全体ステップ（アニメ用）
local acc_ms = 0

local score, hiscore = 0, 0
local lives, bombs = 3, 3
local power = 0            -- 0..128
local graze = 0
local continues = 0
local next_extend = 1
local EXTENDS = { 5000000, 15000000, 30000000, 50000000, 80000000 }
local MAX_POWER = 128

-- 自機
local px, py = PF_CX, PF_B - 24
local p_inv = 0            -- 無敵ステップ
local p_dying = 0          -- 喰らいボム猶予
local p_shot_cd = 0
local focus = false
local bomb_t = 0           -- ボム効果残り
local bomb_x, bomb_y = 0, 0
local spell_fail = false   -- スペル取得失敗（被弾・ボム）

local SPD_FAST, SPD_SLOW = 3.2, 1.4
local POWER_STEPS = { 8, 24, 48, 80 }

local function power_level()
  local lv = 0
  for i = 1, 4 do if power >= POWER_STEPS[i] then lv = i end end
  return lv
end

-- ---------------------------------------------------------------------------
-- 敵弾プール（並列配列）
-- ---------------------------------------------------------------------------
local MAXB = 220
local nb = 0
local bx, by, bvx, bvy, bm, bay, bt, bgz, btm, bev, bsp = {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}
for i = 1, MAXB do
  bx[i], by[i], bvx[i], bvy[i], bm[i], bay[i] = 0, 0, 0, 0, 1, 0
  bt[i], bgz[i], btm[i], bev[i], bsp[i] = 0, 0, 0, 0, 0
end

local function eb(x, y, vx, vy, t)
  if nb >= MAXB then return 0 end
  nb = nb + 1
  local i = nb
  bx[i], by[i], bvx[i], bvy[i] = x, y, vx, vy
  bm[i], bay[i], bt[i], bgz[i], btm[i], bev[i], bsp[i] = 1, 0, t, 0, 0, 0, 0
  return i
end

local function eba(x, y, a, s, t)
  return eb(x, y, cos(a) * s, sin(a) * s, t)
end

-- 拡張: m=減衰率, g=重力, tm=イベント時刻, ev=イベント(1:自機狙い 2:角度回転), sp=イベント引数
local function ebx(x, y, a, s, t, m, g, tm, ev, sp)
  local i = eba(x, y, a, s, t)
  if i > 0 then
    bm[i], bay[i], btm[i], bev[i], bsp[i] = m or 1, g or 0, tm or 0, ev or 0, sp or 0
  end
  return i
end

local function aim(x, y)
  return atan(py - y, px - x)
end

local function ring(x, y, n, a0, s, t)
  local da = TAU / n
  for k = 0, n - 1 do eba(x, y, a0 + da * k, s, t) end
end

local function nway(x, y, n, a, spread, s, t)
  if n <= 1 then eba(x, y, a, s, t) return end
  local a0 = a - spread / 2
  local da = spread / (n - 1)
  for k = 0, n - 1 do eba(x, y, a0 + da * k, s, t) end
end

local function del_bullet(i)
  local j = nb
  if i ~= j then
    bx[i], by[i], bvx[i], bvy[i], bm[i], bay[i] = bx[j], by[j], bvx[j], bvy[j], bm[j], bay[j]
    bt[i], bgz[i], btm[i], bev[i], bsp[i] = bt[j], bgz[j], btm[j], bev[j], bsp[j]
  end
  nb = j - 1
end

-- 難易度スケール
local function nd(n) return max(1, floor(n * DM[diff] + 0.5)) end
local function sd(s) return s * SM[diff] end

-- ---------------------------------------------------------------------------
-- 自機弾プール
-- ---------------------------------------------------------------------------
local MAXS = 40
local ns = 0
local sx, sy, svx, svy, sdm, sk = {}, {}, {}, {}, {}, {}
for i = 1, MAXS do sx[i], sy[i], svx[i], svy[i], sdm[i], sk[i] = 0, 0, 0, 0, 0, 0 end

local function add_shot(x, y, vx, vy, dmg, k)
  if ns >= MAXS then return end
  ns = ns + 1
  sx[ns], sy[ns], svx[ns], svy[ns], sdm[ns], sk[ns] = x, y, vx, vy, dmg, k
end

local function del_shot(i)
  local j = ns
  if i ~= j then
    sx[i], sy[i], svx[i], svy[i], sdm[i], sk[i] = sx[j], sy[j], svx[j], svy[j], sdm[j], sk[j]
  end
  ns = j - 1
end

-- ---------------------------------------------------------------------------
-- アイテムプール  k: 0=P 1=点 2=B 3=1UP 4=星(弾消し)
-- ---------------------------------------------------------------------------
local MAXI = 90
local ni = 0
local ix, iy, ivy, ik, ia = {}, {}, {}, {}, {}
for i = 1, MAXI do ix[i], iy[i], ivy[i], ik[i], ia[i] = 0, 0, 0, 0, 0 end
local I_P, I_PT, I_B, I_1UP, I_STAR = 0, 1, 2, 3, 4
local ITEM_SX = { [0] = 12, 22, 32, 42, 52 }
local ITEM_SZ = { [0] = 10, 10, 10, 10, 6 }

local function add_item(x, y, k, auto)
  if ni >= MAXI then return false end
  ni = ni + 1
  ix[ni], iy[ni], ivy[ni], ik[ni], ia[ni] = x, y, -1.6 - random() * 0.8, k, auto and 1 or 0
  return true
end

local function del_item(i)
  local j = ni
  if i ~= j then
    ix[i], iy[i], ivy[i], ik[i], ia[i] = ix[j], iy[j], ivy[j], ik[j], ia[j]
  end
  ni = j - 1
end

function F.drop_items(x, y, np, npt)
  for _ = 1, np do add_item(x + random(-10, 10), y + random(-8, 8), I_P) end
  for _ = 1, npt do add_item(x + random(-10, 10), y + random(-8, 8), I_PT) end
end

-- ---------------------------------------------------------------------------
-- エフェクト（爆発リング等）: 小テーブルだが同時数は少ない
-- ---------------------------------------------------------------------------
local fx = {}
function F.add_fx(x, y, r0, r1, life, col)
  if #fx >= 24 then table.remove(fx, 1) end
  fx[#fx + 1] = { x = x, y = y, r0 = r0, r1 = r1, t = 0, life = life, c = col }
end

-- ---------------------------------------------------------------------------
-- スコア
-- ---------------------------------------------------------------------------
local function add_score(v)
  score = score + v
  if score > hiscore then hiscore = score end
  local e = EXTENDS[next_extend]
  if e and score >= e then
    next_extend = next_extend + 1
    if lives < 8 then lives = lives + 1 end
    se("extend")
  end
end

-- 弾を全消去して星アイテムへ
function F.cancel_bullets(to_items)
  local made = 0
  for i = 1, nb do
    if to_items and made < 40 and (i % 2 == 1) then
      if add_item(bx[i], by[i], I_STAR, true) then made = made + 1 end
    else
      add_score(10)
    end
  end
  nb = 0
end

-- ---------------------------------------------------------------------------
-- 敵（雑魚）
--   spr: 0..3 妖精(青赤緑黄) / 4 鬼火 / 5 提灯お化け / 6..9 大妖精(色 0..3)
--   mv : 1 等加速直線 / 2 停止して離脱 / 3 サイン波 / 4 旋回
--   fk : 0 撃たない / 1 自機狙い n-way / 2 全方位 / 3 ばらまき
-- ---------------------------------------------------------------------------
local enemies = {}

function F.spawn(x, y, spr, hp, o)
  local big = spr >= 6
  enemies[#enemies + 1] = {
    x = x, y = y, vx = o.vx or 0, vy = o.vy or 1.5, spr = spr, hp = hp, t = 0,
    mv = o.mv or 1, ax = o.ax or 0, ay = o.ay or 0, t1 = o.t1 or 30, t2 = o.t2 or 90,
    amp = o.amp or 1.5, frq = o.frq or 0.06, da = o.da or 0,
    fk = o.fk or 0, fs = o.fs or 30, fi = o.fi or 60, fn = o.fn or 1,
    fc = o.fc or 1, fsp = o.fsp or 2, fsr = o.fsr or 0.6, ft = o.ft or (SMALL + RED),
    dp = o.dp or 1, dpt = o.dpt or 1, sc = o.sc or (big and 3000 or 300),
    r = big and 11 or (spr == 4 and 6 or 8), big = big,
  }
end

function F.enemy_fire(e)
  local fk = e.fk
  local x, y = e.x, e.y
  if fk == 1 then
    nway(x, y, nd(e.fc), aim(x, y), e.fsr, sd(e.fsp), e.ft)
  elseif fk == 2 then
    ring(x, y, nd(e.fc), random() * TAU, sd(e.fsp), e.ft)
  elseif fk == 3 then
    for _ = 1, nd(e.fc) do
      eba(x, y, HALFPI + (random() - 0.5) * 1.6, sd(e.fsp) * (0.6 + random() * 0.6), e.ft)
    end
  end
end

function F.kill_enemy(i)
  local e = enemies[i]
  add_score(e.sc)
  F.drop_items(e.x, e.y, e.dp, e.dpt)
  F.add_fx(e.x, e.y, 2, e.big and 26 or 14, 10, e.big and C.GOLD or C.WHITE)
  se("kill")
  table.remove(enemies, i)
end

function F.update_enemies()
  for i = #enemies, 1, -1 do
    local e = enemies[i]
    local t = e.t + 1
    e.t = t
    local mv = e.mv
    if mv == 2 then
      if t < e.t1 then
        e.vx = e.vx * 0.9; e.vy = e.vy * 0.9
      elseif t < e.t2 then
        e.vx, e.vy = 0, 0
      else
        e.vx = e.vx + e.ax; e.vy = e.vy + e.ay
      end
    elseif mv == 3 then
      e.vx = e.amp * cos(t * e.frq)
    elseif mv == 4 then
      local c, s = cos(e.da), sin(e.da)
      e.vx, e.vy = e.vx * c - e.vy * s, e.vx * s + e.vy * c
    else
      e.vx = e.vx + e.ax; e.vy = e.vy + e.ay
    end
    e.x = e.x + e.vx
    e.y = e.y + e.vy
    if e.fk > 0 and e.fn > 0 and t >= e.fs and (t - e.fs) % e.fi == 0
        and e.y < PF_B - 40 and e.y > PF_Y then
      e.fn = e.fn - 1
      F.enemy_fire(e)
    end
    if t > 30 and (e.x < PF_X - 30 or e.x > PF_R + 30 or e.y < PF_Y - 40 or e.y > PF_B + 30) then
      table.remove(enemies, i)
    end
  end
end

-- ---------------------------------------------------------------------------
-- ステージ道中（ウェーブ定義）
-- ---------------------------------------------------------------------------
local events = {}
local ev_i = 1

function F.at(t, f) events[#events + 1] = { t, f } end

-- 左右から弧を描いて横切る妖精
function F.w_arc(t, side, n, spr, fk, extra)
  for k = 0, n - 1 do
    F.at(t + k * 10, function()
      local x = side < 0 and PF_X - 8 or PF_R + 8
      F.spawn(x, PF_Y + 20 + k * 3, spr, 8, {
        vx = -side * 2.6, vy = 0.6, mv = 4, da = side * 0.018,
        fk = fk or 1, fs = 20, fi = 40, fn = 2, fc = extra or 1, fsp = 2.2,
        ft = SMALL + (spr == 1 and RED or BLUE), dp = 1, dpt = 1,
      })
    end)
  end
end

-- 上から降りてきて止まり、撃ってから帰る
function F.w_drop(t, xs, spr, hp, fk, fc, ft, fsp)
  for k, xx in ipairs(xs) do
    F.at(t + (k - 1) * 12, function()
      F.spawn(PF_X + xx, PF_Y - 12, spr, hp, {
        vx = 0, vy = 3.2, mv = 2, t1 = 30, t2 = 110, ax = 0, ay = -0.06,
        fk = fk, fs = 40, fi = 30, fn = 3, fc = fc, fsp = fsp or 1.8, fsr = 0.9,
        ft = ft, dp = 2, dpt = 2,
      })
    end)
  end
end

-- サイン波で降りる鬼火の列
function F.w_wisps(t, x, n, dir)
  for k = 0, n - 1 do
    F.at(t + k * 8, function()
      F.spawn(PF_X + x, PF_Y - 10, 4, 3, {
        vx = 0, vy = 1.6, mv = 3, amp = 2.0 * (dir or 1), frq = 0.08,
        fk = 1, fs = 25 + k * 3, fi = 50, fn = 1, fc = 1, fsp = 2.4, ft = SMALL + CYAN,
        dp = 0, dpt = 1, sc = 150,
      })
    end)
  end
end

-- 画面横から流れる提灯お化け
function F.w_ghosts(t, n, side, y0)
  for k = 0, n - 1 do
    F.at(t + k * 16, function()
      local x = side < 0 and PF_X - 10 or PF_R + 10
      F.spawn(x, PF_Y + y0 + (k % 3) * 14, 5, 18, {
        vx = -side * 1.4, vy = 0.2, mv = 1,
        fk = 3, fs = 30, fi = 45, fn = 2, fc = 3, fsp = 1.8, ft = SMALL + ORANGE,
        dp = 1, dpt = 2, sc = 600,
      })
    end)
  end
end

-- 大妖精: 止まって全方位弾
function F.w_big(t, x, color, hp, fc, ft, fn)
  F.at(t, function()
    F.spawn(PF_X + x, PF_Y - 14, 6 + color, hp, {
      vx = 0, vy = 2.6, mv = 2, t1 = 40, t2 = 40 + (fn or 4) * 35, ax = 0, ay = -0.05,
      fk = 2, fs = 45, fi = 35, fn = fn or 4, fc = fc, fsp = 1.6, ft = ft,
      dp = 6, dpt = 6, sc = 5000,
    })
  end)
end

-- 急降下のばらまき妖精
function F.w_rain(t, n, spr)
  for k = 0, n - 1 do
    F.at(t + k * 7, function()
      F.spawn(PF_X + 12 + random(0, PF_W - 24), PF_Y - 10, spr, 4, {
        vx = 0, vy = 2.6, mv = 1, ay = 0.01,
        fk = 1, fs = 20, fi = 99, fn = 1, fc = 1, fsp = 2.6, ft = SMALL + GREEN,
        dp = 0, dpt = 1, sc = 200,
      })
    end)
  end
end

-- V 字編隊
function F.w_vform(t, spr, fk, fc, ft)
  for k = 0, 6 do
    local off = (k - 3)
    F.at(t + abs(off) * 8, function()
      F.spawn(PF_CX + off * 22, PF_Y - 10, spr, 10, {
        vx = 0, vy = 1.8, mv = 2, t1 = 35, t2 = 100, ax = off * 0.03, ay = -0.03,
        fk = fk, fs = 36, fi = 32, fn = 2, fc = fc, fsp = 2.0, fsr = 0.5, ft = ft,
        dp = 1, dpt = 1,
      })
    end)
  end
end

local STAGE_BUILD = {
  -- 1: 黄昏の畦道
  function()
    F.w_arc(60, -1, 5, 0)
    F.w_arc(190, 1, 5, 0)
    F.w_wisps(320, 60, 6, 1)
    F.w_wisps(360, 164, 6, -1)
    F.w_drop(500, { 50, 112, 174 }, 1, 14, 1, 3, SMALL + RED)
    F.w_arc(640, -1, 6, 3, 1, 2)
    F.w_arc(700, 1, 6, 3, 1, 2)
    F.w_big(820, 112, 0, 90, 12, SMALL + BLUE, 3)
    F.w_ghosts(1000, 6, -1, 30)
    F.w_rain(1120, 12, 2)
    F.w_vform(1260, 1, 1, 3, SMALL + RED)
    F.w_wisps(1380, 40, 8, 1)
    F.w_wisps(1400, 184, 8, -1)
  end,
  -- 2: 稲荷の千本鳥居
  function()
    F.w_vform(60, 1, 1, 3, SMALL + ORANGE)
    F.w_arc(200, 1, 7, 1, 1, 2)
    F.w_arc(260, -1, 7, 1, 1, 2)
    F.w_ghosts(380, 6, 1, 20)
    F.w_ghosts(420, 6, -1, 50)
    F.w_big(560, 60, 1, 110, 14, SMALL + ORANGE, 4)
    F.w_big(620, 164, 1, 110, 14, SMALL + RED, 4)
    F.w_drop(840, { 30, 80, 144, 194 }, 3, 16, 2, 10, MID + YELLOW)
    F.w_rain(1000, 16, 1)
    F.w_vform(1160, 0, 1, 3, SMALL + BLUE)
    F.w_arc(1300, -1, 8, 3, 1, 3)
    F.w_arc(1330, 1, 8, 3, 1, 3)
    F.w_big(1460, 112, 3, 140, 20, SMALL + YELLOW, 4)
  end,
  -- 3: 霧の渓谷
  function()
    F.w_wisps(60, 40, 8, 1)
    F.w_wisps(80, 112, 8, -1)
    F.w_wisps(100, 184, 8, 1)
    F.w_drop(300, { 40, 90, 134, 184 }, 0, 16, 1, 5, SMALL + CYAN, 2.2)
    F.w_rain(460, 18, 0)
    F.w_big(640, 112, 0, 150, 18, MID + BLUE, 4)
    F.w_arc(800, 1, 8, 0, 1, 3)
    F.w_arc(830, -1, 8, 0, 1, 3)
    F.w_ghosts(980, 8, -1, 20)
    F.w_ghosts(1000, 8, 1, 60)
    F.w_vform(1180, 0, 2, 10, SMALL + CYAN)
    F.w_big(1360, 60, 2, 150, 20, SMALL + GREEN, 3)
    F.w_big(1400, 164, 0, 150, 20, SMALL + CYAN, 3)
  end,
  -- 4: からくり屋敷
  function()
    F.w_drop(60, { 40, 112, 184 }, 3, 18, 2, 12, SMALL + YELLOW)
    F.w_drop(200, { 76, 148 }, 1, 18, 1, 5, MID + PURPLE)
    F.w_ghosts(340, 8, 1, 20)
    F.w_ghosts(360, 8, -1, 70)
    F.w_vform(520, 3, 2, 10, SMALL + PURPLE)
    F.w_big(700, 112, 3, 180, 24, SMALL + YELLOW, 5)
    F.w_rain(900, 20, 3)
    F.w_arc(1060, -1, 9, 1, 1, 3)
    F.w_arc(1100, 1, 9, 1, 1, 3)
    F.w_drop(1240, { 30, 70, 112, 154, 194 }, 0, 16, 2, 8, SMALL + WHITE)
    F.w_big(1420, 60, 1, 180, 16, MID + PURPLE, 4)
    F.w_big(1440, 164, 1, 180, 16, MID + PURPLE, 4)
  end,
  -- 5: 雷雲の霊峰
  function()
    F.w_rain(60, 20, 3)
    F.w_arc(220, -1, 9, 3, 1, 3)
    F.w_arc(240, 1, 9, 3, 1, 3)
    F.w_big(400, 112, 3, 220, 24, STAR + YELLOW, 5)
    F.w_wisps(600, 30, 10, 1)
    F.w_wisps(620, 112, 10, -1)
    F.w_wisps(640, 194, 10, 1)
    F.w_drop(820, { 40, 90, 134, 184 }, 2, 20, 1, 5, SMALL + YELLOW, 2.6)
    F.w_ghosts(980, 10, -1, 20)
    F.w_ghosts(1000, 10, 1, 60)
    F.w_vform(1180, 3, 2, 12, STAR + YELLOW)
    F.w_big(1360, 60, 3, 220, 20, SMALL + WHITE, 4)
    F.w_big(1380, 164, 2, 220, 20, SMALL + GREEN, 4)
  end,
  -- 6: 翠月の天守
  function()
    F.w_vform(60, 2, 1, 5, SMALL + GREEN)
    F.w_arc(200, -1, 10, 2, 1, 3)
    F.w_arc(220, 1, 10, 2, 1, 3)
    F.w_big(380, 112, 2, 260, 28, MID + GREEN, 5)
    F.w_ghosts(560, 10, -1, 20)
    F.w_ghosts(580, 10, 1, 60)
    F.w_rain(740, 24, 2)
    F.w_drop(900, { 30, 70, 112, 154, 194 }, 2, 20, 2, 10, STAR + GREEN)
    F.w_wisps(1080, 40, 10, 1)
    F.w_wisps(1090, 184, 10, -1)
    F.w_big(1240, 60, 2, 260, 24, SMALL + CYAN, 4)
    F.w_big(1260, 164, 3, 260, 24, SMALL + WHITE, 4)
    F.w_vform(1440, 2, 1, 7, MID + GREEN)
  end,
}
local STAGE_BOSS_AT = 1600    -- 道中の長さ（ステップ）。以降、敵が消えたら会話へ

-- ---------------------------------------------------------------------------
-- ボス
-- ---------------------------------------------------------------------------
local BOSS_KEY = { "chirori", "kohaku", "shizuku", "nejika", "raika", "mikoto" }
local boss = nil

function F.bmove(b, x, y) b.tx, b.ty = x, y end
function F.bwander(b)
  local nx = b.x + random(-50, 50)
  nx = max(PF_X + 40, min(PF_R - 40, nx))
  F.bmove(b, nx, PF_Y + 30 + random(0, 26))
end

-- 各面のフェーズ: { hp, time(秒), spell(スペル名番号 or nil), f(b, t) }
-- f は 1 ステップごとに呼ばれる。t はフェーズ開始からのステップ数。
local PH = {}

-- 1 面 ちろり ---------------------------------------------------------------
PH[1] = {
  { hp = 700, time = 30, f = function(b, t)
    if t % 40 == 0 then ring(b.x, b.y, nd(16), t * 0.05, sd(1.6), SMALL + BLUE) end
    if t % 60 == 20 then nway(b.x, b.y, nd(3), aim(b.x, b.y), 0.5, sd(2.2), MID + CYAN) end
    if t % 120 == 60 then F.bwander(b) end
  end },
  { hp = 900, time = 40, spell = 1, f = function(b, t)
    local iv = ({ 6, 4, 3 })[diff]
    if t % iv == 0 then
      local a = t * 0.13
      ebx(b.x, b.y, a, sd(1.3), MID + BLUE, 1.012)
      ebx(b.x, b.y, a + PI, sd(1.3), MID + BLUE, 1.012)
    end
    if t % 50 == 25 then nway(b.x, b.y, nd(5), aim(b.x, b.y), 0.8, sd(2.4), SMALL + CYAN) end
    if t % 150 == 75 then F.bwander(b) end
  end },
}

-- 2 面 こはく ---------------------------------------------------------------
PH[2] = {
  { hp = 800, time = 30, f = function(b, t)
    if t % 50 == 0 then nway(b.x, b.y, nd(7), aim(b.x, b.y), 1.2, sd(2.0), MID + RED) end
    if t % 50 == 25 then ring(b.x, b.y, nd(20), random() * TAU, sd(1.5), SMALL + ORANGE) end
    if t % 100 == 70 then F.bwander(b) end
  end },
  { hp = 1000, time = 40, spell = 1, f = function(b, t)
    -- 鳥居状の弾の列が降ってくる（1 か所だけ隙間）
    local iv = ({ 48, 36, 30 })[diff]
    if t % iv == 0 then
      local gap = PF_X + 20 + random(0, PF_W - 40)
      local c = (t // iv) % 2 == 0 and RED or ORANGE
      for x = PF_X + 6, PF_R - 6, 13 do
        if abs(x - gap) > 20 then eb(x, PF_Y + 2, 0, sd(1.1), SMALL + c) end
      end
    end
    if t % 40 == 20 then nway(b.x, b.y, nd(3), aim(b.x, b.y), 0.4, sd(2.4), MID + RED) end
  end },
  { hp = 1100, time = 45, spell = 2, f = function(b, t)
    -- 九つの尾: 回転する 9 本の弾列
    local iv = ({ 9, 7, 5 })[diff]
    if t % iv == 0 then
      local n = ({ 6, 9, 9 })[diff]
      local a0 = t * 0.021
      for k = 0, n - 1 do eba(b.x, b.y, a0 + k * TAU / n, sd(1.7), SMALL + YELLOW) end
    end
    if t % 70 == 35 then nway(b.x, b.y, nd(5), aim(b.x, b.y), 0.9, sd(2.0), BIG + ORANGE) end
    if t % 140 == 100 then F.bwander(b) end
  end },
}

-- 3 面 しずく ---------------------------------------------------------------
PH[3] = {
  { hp = 900, time = 30, f = function(b, t)
    local iv = ({ 5, 3, 2 })[diff]
    if t % iv == 0 then
      ebx(PF_X + 4 + random(0, PF_W - 8), PF_Y + 2, HALFPI, sd(0.6), SMALL + CYAN, 1, 0.03)
    end
    if t % 70 == 0 then nway(b.x, b.y, nd(5), aim(b.x, b.y), 0.7, sd(2.0), MID + BLUE) end
    if t % 140 == 70 then F.bwander(b) end
  end },
  { hp = 1100, time = 40, spell = 1, f = function(b, t)
    -- 減速して止まったあと自機を狙い直すリング
    if t % 50 == 0 then
      local n = nd(18)
      local a0 = random() * TAU
      for k = 0, n - 1 do
        ebx(b.x, b.y, a0 + k * TAU / n, 3.0, MID + BLUE, 0.94, 0, 40, 1, sd(2.1))
      end
    end
    if t % 50 == 25 then ring(b.x, b.y, nd(10), random() * TAU, sd(1.2), SMALL + WHITE) end
    if t % 100 == 60 then F.bwander(b) end
  end },
  { hp = 1200, time = 45, spell = 2, f = function(b, t)
    -- 白糸の滝: ゆっくり動く数本の滝
    local cols = ({ 4, 5, 6 })[diff]
    if t % 4 == 0 then
      for k = 0, cols - 1 do
        local x = PF_X + (k + 0.5) * PF_W / cols + sin(t * 0.015 + k * 1.7) * 18
        eb(x, PF_Y + 2, 0, sd(3.2), SMALL + WHITE)
      end
    end
    if t % 45 == 0 then nway(b.x, b.y, nd(3), aim(b.x, b.y), 0.3, sd(2.2), MID + CYAN) end
  end },
}

-- 4 面 ねじか ---------------------------------------------------------------
PH[4] = {
  { hp = 1000, time = 30, f = function(b, t)
    if t % 30 == 0 then
      local n = nd(12)
      local a0 = random() * TAU
      local turn = (t // 30) % 2 == 0 and 2 or 3
      for k = 0, n - 1 do
        ebx(b.x, b.y, a0 + k * TAU / n, sd(2.0), MID + PURPLE, 1, 0, 30, turn, sd(2.0))
      end
    end
    if t % 120 == 90 then F.bwander(b) end
  end },
  { hp = 1300, time = 40, spell = 1, f = function(b, t)
    -- ゼンマイ: 振り子のように回る 4 本腕
    local iv = ({ 4, 3, 2 })[diff]
    if t % iv == 0 then
      local a = sin(t * 0.02) * 3
      for k = 0, 3 do eba(b.x, b.y, a + k * HALFPI, sd(2.2), SMALL + YELLOW) end
    end
    if t % 90 == 45 then ring(b.x, b.y, nd(24), random() * TAU, sd(1.3), SMALL + PURPLE) end
  end },
  { hp = 1400, time = 45, spell = 2, f = function(b, t)
    -- 十二刻: 時を打つたびに 12 方向リングが止まって曲がる + 時計の針
    if t % 60 == 0 then
      local layers = ({ 1, 2, 2 })[diff]
      for L = 1, layers do
        for k = 0, 11 do
          ebx(b.x, b.y, k * TAU / 12 + L * 0.26, 2.6 + L * 0.4, MID + PURPLE, 0.93, 0, 32,
            (L % 2 == 0) and 2 or 3, sd(1.7))
        end
      end
    end
    local hi = ({ 6, 4, 3 })[diff]
    if t % hi == 0 then eba(b.x, b.y, t * 0.052, sd(2.0), SMALL + WHITE) end
    if t % 10 == 0 then eba(b.x, b.y, -t * 0.011, sd(1.5), MID + WHITE) end
  end },
}

-- 5 面 らいか ---------------------------------------------------------------
PH[5] = {
  { hp = 1100, time = 30, f = function(b, t)
    if t % 50 == 0 then
      local a = aim(b.x, b.y)
      for k = 0, 7 do
        eba(b.x, b.y, a + (random() - 0.5) * 0.08, sd(2.0 + k * 0.35), SMALL + YELLOW)
      end
    end
    if t % 50 == 25 then ring(b.x, b.y, nd(16), random() * TAU, sd(1.6), STAR + YELLOW) end
    if t % 100 == 80 then F.bwander(b) end
  end },
  { hp = 1400, time = 40, spell = 1, f = function(b, t)
    -- 八連太鼓: 周囲の太鼓が交互に鳴る
    b.drum = t * 0.02
    if t % 40 == 0 then
      local odd = (t // 40) % 2
      for k = odd, 7, 2 do
        local a = b.drum + k * TAU / 8
        ring(b.x + cos(a) * 40, b.y + sin(a) * 26, nd(6), random() * TAU, sd(1.5), MID + YELLOW)
      end
    end
    if t % 60 == 30 then nway(b.x, b.y, nd(3), aim(b.x, b.y), 0.3, sd(2.6), SMALL + WHITE) end
  end },
  { hp = 1500, time = 45, spell = 2, f = function(b, t)
    -- 天降る稲妻: 予告線 → 高速の弾柱
    b.warn = b.warn or {}
    local wi = ({ 70, 55, 45 })[diff]
    if t % wi == 0 then
      local n = ({ 2, 3, 4 })[diff]
      for k = 1, n do
        local x = (k == 1) and px or (PF_X + 10 + random(0, PF_W - 20))
        b.warn[#b.warn + 1] = { x = floor(x), t = 30 }
      end
    end
    for k = #b.warn, 1, -1 do
      local w = b.warn[k]
      w.t = w.t - 1
      if w.t <= 0 and w.t > -12 then
        eb(w.x + random(-2, 2), PF_Y + 2, 0, 6.5, SMALL + YELLOW)
      elseif w.t <= -12 then
        table.remove(b.warn, k)
      end
    end
    if t % 20 == 0 then
      eba(PF_X + random(0, PF_W), PF_Y + 2, HALFPI + (random() - 0.5), sd(1.2), STAR + YELLOW)
    end
  end },
}

-- 6 面 ミコト ---------------------------------------------------------------
PH[6] = {
  { hp = 1300, time = 35, f = function(b, t)
    local iv = ({ 8, 6, 4 })[diff]
    if t % iv == 0 then
      eba(b.x, b.y, t * 0.07, sd(1.8), MID + GREEN)
      eba(b.x, b.y, -t * 0.07 + PI, sd(1.8), MID + GREEN)
    end
    if t % 60 == 30 then ring(b.x, b.y, nd(16), aim(b.x, b.y), sd(2.0), SMALL + CYAN) end
    if t % 120 == 90 then F.bwander(b) end
  end },
  { hp = 1700, time = 45, spell = 1, f = function(b, t)
    -- ムーンライト・ランタン: 星の輪が止まり、狙い直す
    if t % 45 == 0 then
      local n = nd(20)
      local a0 = random() * TAU
      for k = 0, n - 1 do
        ebx(b.x, b.y, a0 + k * TAU / n, 2.8, STAR + GREEN, 0.93, 0, 38, 1, sd(2.3))
      end
    end
    if t % 90 == 60 then nway(b.x, b.y, nd(3), aim(b.x, b.y), 0.6, sd(1.8), BIG + ORANGE) end
    if t % 135 == 100 then F.bwander(b) end
  end },
  { hp = 1400, time = 35, f = function(b, t)
    -- 左右の壁から流れる弾
    local iv = ({ 10, 7, 5 })[diff]
    if t % iv == 0 then
      local y = PF_Y + 6 + random(0, 110)
      eb(PF_X + 2, y, sd(1.8), 0.35, SMALL + PURPLE)
      eb(PF_R - 2, y + 30, -sd(1.8), 0.35, SMALL + PURPLE)
    end
    if t % 40 == 0 then nway(b.x, b.y, nd(5), aim(b.x, b.y), 0.9, sd(2.3), MID + GREEN) end
    if t % 100 == 50 then F.bwander(b) end
  end },
  { hp = 1900, time = 45, spell = 2, f = function(b, t)
    -- 百鬼灯籠行列: 左右に揺れて降りる灯籠の列 + 周囲の鬼火
    if t % 16 == 0 then
      local k = (t // 16) % 2
      local x = PF_CX + (k == 0 and -1 or 1) * (50 + sin(t * 0.03) * 40)
      eb(x, PF_Y + 2, 0, sd(1.0), BIG + ORANGE)
    end
    if t % 30 == 0 then ring(b.x, b.y, nd(14), t * 0.1, sd(1.7), SMALL + RED) end
    if t % 55 == 20 then nway(b.x, b.y, nd(3), aim(b.x, b.y), 0.35, sd(2.6), MID + CYAN) end
  end },
  { hp = 2400, time = 60, spell = 3, f = function(b, t)
    -- 明けない夜の翠灯: 三重螺旋 + 星の花
    F.bmove(b, PF_CX, PF_Y + 50)
    local iv = ({ 6, 4, 3 })[diff]
    if t % iv == 0 then
      local a = t * 0.045
      eba(b.x, b.y, a, sd(1.7), MID + GREEN)
      eba(b.x, b.y, a + TAU / 3, sd(1.7), SMALL + WHITE)
      eba(b.x, b.y, a + TAU * 2 / 3, sd(1.7), SMALL + PURPLE)
    end
    if t % 80 == 40 then
      local n = nd(24)
      local a0 = random() * TAU
      for k = 0, n - 1 do
        ebx(b.x, b.y, a0 + k * TAU / n, sd(1.1), STAR + CYAN, 1.01)
      end
    end
  end },
}

function F.boss_phase_start(b)
  local ph = PH[stage][b.ph]
  b.hp = ph.hp
  b.maxhp = ph.hp
  b.t = 0
  b.timer = ph.time * 30
  b.spell = ph.spell
  b.warn = nil
  spell_fail = false
  if ph.spell then
    b.spell_name = DATA.stages[stage] and DATA.stages[stage].spells[ph.spell] or "Spell"
    b.bonus = 400000 * stage + 200000 * diff
    b.cutin = 45
    se("spell")
  else
    b.spell_name = nil
  end
end

function F.spawn_boss()
  local key = BOSS_KEY[stage]
  boss = {
    key = key, x = PF_CX, y = PF_Y - 30, tx = PF_CX, ty = PF_Y + 40,
    hp = 1, maxhp = 1, ph = 1, t = 0, timer = 0, frame = 0, cutin = 0,
    active = false, dead = 0,
  }
end

-- ---------------------------------------------------------------------------
-- 会話
-- ---------------------------------------------------------------------------
local talk = nil     -- { list, i, shown, len, wait }

function F.talk_start(list)
  if not list or #list == 0 then talk = nil return end
  talk = { list = list, i = 1, shown = 0, len = utf8.len(list[1][2]) or 0, hold = 0 }
end

-- 戻り値 true = 会話終了
function F.talk_step()
  if not talk then return true end
  local fast = btn[B_NEAR]
  talk.shown = talk.shown + (fast and 6 or 2)
  local adv = edge[B_FAR]
  if fast then
    talk.hold = talk.hold + 1
    if talk.hold >= 4 then adv = true; talk.hold = 0 end
  end
  if adv then
    if talk.shown < talk.len and not fast then
      talk.shown = talk.len
    else
      talk.i = talk.i + 1
      local ln = talk.list[talk.i]
      if not ln then talk = nil return true end
      talk.shown = 0
      talk.len = utf8.len(ln[2]) or 0
      se("select")
    end
  end
  return false
end

function F.talk_visible_text()
  local s = talk.list[talk.i][2]
  if talk.shown >= talk.len then return s end
  local cut = utf8.offset(s, talk.shown + 1)
  return cut and s:sub(1, cut - 1) or s
end

-- ---------------------------------------------------------------------------
-- ゲーム進行
-- ---------------------------------------------------------------------------
local bg_scroll = 0
local opt_x, opt_y = { 0, 0, 0, 0 }, { 0, 0, 0, 0 }

function F.show_msg(s, c, t)
  S.msg, S.msg_c, S.msg_t = s, c or C.WHITE, t or 90
end

function F.clear_pools()
  nb, ns, ni = 0, 0, 0
  enemies = {}
  fx = {}
end

function F.begin_stage(n)
  stage = n
  F.clear_pools()
  events = {}
  ev_i = 1
  STAGE_BUILD[n]()
  table.sort(events, function(a, b) return a[1] < b[1] end)
  gtime = 0
  gphase = "stage"
  S.intro_t = 150
  boss = nil
  talk = nil
  bomb_t = 0
  bg_scroll = 0
  px, py = PF_CX, PF_B - 24
  p_inv = 60
  -- ボス画像は面ごとに入れ替え（ヒープ節約）
  for i = 1, 6 do
    F.free_img("face_" .. BOSS_KEY[i])
    F.free_img("boss_" .. BOSS_KEY[i])
  end
  local key = BOSS_KEY[n]
  F.load_img("face_" .. key, "img/face_" .. key .. ".bin", 80, 112)
  F.load_img("boss_" .. key, "img/boss_" .. key .. ".bin", 64, 40)
  bgm_cur = nil
  F.play_bgm("st" .. n)
end

function F.start_game()
  score = 0
  hiscore = save.hi[diff] or 0
  lives = diff == 1 and 4 or 3
  bombs = 3
  power = 0
  graze = 0
  continues = 0
  next_extend = 1
  S.last_power_lv = 0
  scene = "game"
  S.paused = false
  focus = false             -- 低速切り替えは新しいゲームで通常に戻す
  F.free_img("logo")        -- タイトル専用画像はプレイ中は解放
  F.begin_stage(1)
end

function F.record_hiscore()
  if score > (save.hi[diff] or 0) then
    save.hi[diff] = score
  end
  F.write_save()
end

function F.player_die()
  lives = lives - 1
  F.add_fx(px, py, 4, 60, 20, C.RED)
  F.add_fx(px, py, 2, 36, 14, C.WHITE)
  se("death")
  spell_fail = true
  local lost = min(power, 16)
  power = power - lost
  for k = 1, 5 do add_item(px + random(-30, 30), py - random(20, 50), I_P) end
  nb = 0
  p_inv = 120
  bombs = max(bombs, 3)
  if lives < 0 then
    lives = 0
    S.gphase_before_continue = gphase
    gphase = "continue"
    S.menu_cur = 1
    F.record_hiscore()
  end
  px, py = PF_CX, PF_B - 24
end

function F.use_bomb()
  if bombs <= 0 or bomb_t > 0 then return end
  bombs = bombs - 1
  bomb_t = 90
  bomb_x, bomb_y = px, py
  p_inv = max(p_inv, 130)
  p_dying = 0
  spell_fail = true
  S.flash_t = 6
  se("bomb")
  F.cancel_bullets(true)
end

-- 自機
local OPT_WIDE = { { -18, 4 }, { 18, 4 }, { -30, 12 }, { 30, 12 } }
local OPT_FOCUS = { { -8, -12 }, { 8, -12 }, { -16, -4 }, { 16, -4 } }

function F.update_player(can_shoot)
  if edge[B_OPR] then focus = not focus; se("select") end   -- 低速 ⇔ 通常 の切り替え
  local sp = focus and SPD_SLOW or SPD_FAST
  local dx, dy = 0, 0
  if btn[B_LEFT] then dx = -1 elseif btn[B_RIGHT] then dx = 1 end
  if btn[B_UP] then dy = -1 elseif btn[B_DOWN] then dy = 1 end
  if dx ~= 0 and dy ~= 0 then sp = sp * 0.7071 end
  px = max(PF_X + 8, min(PF_R - 8, px + dx * sp))
  py = max(PF_Y + 12, min(PF_B - 12, py + dy * sp))
  if p_inv > 0 then p_inv = p_inv - 1 end

  local lv = power_level()
  local tbl = focus and OPT_FOCUS or OPT_WIDE
  for k = 1, 4 do
    local o = tbl[k]
    opt_x[k] = opt_x[k] + (px + o[1] - opt_x[k]) * 0.3
    opt_y[k] = opt_y[k] + (py + o[2] - opt_y[k]) * 0.3
  end
  if lv > S.last_power_lv then se("power") end
  S.last_power_lv = lv

  if p_shot_cd > 0 then p_shot_cd = p_shot_cd - 1 end
  if can_shoot and btn[B_FAR] and p_shot_cd == 0 then
    p_shot_cd = 2
    add_shot(px - 5, py - 10, 0, -10, 2, 0)
    add_shot(px + 5, py - 10, 0, -10, 2, 0)
    for k = 1, lv do
      local vx = 0
      if not focus then vx = (k % 2 == 1 and -1 or 1) * (0.6 + (k > 2 and 0.8 or 0)) end
      add_shot(opt_x[k], opt_y[k] - 4, vx, -8, 1, 1)
    end
  end
  if can_shoot and edge[B_NEAR] then F.use_bomb() end   -- 会話中は NEAR が早送りなのでボム不可
end

function F.hit_boss(dmg)
  local b = boss
  if not b or not b.active or b.dead > 0 then return false end
  local mul = b.cutin > 0 and 0.2 or 1
  b.hp = b.hp - dmg * mul
  add_score(10)
  return true
end

function F.update_shots()
  local b = boss
  local bact = b and b.active and b.dead == 0
  for i = ns, 1, -1 do
    local x = sx[i] + svx[i]
    local y = sy[i] + svy[i]
    sx[i], sy[i] = x, y
    local hit = false
    if y < PF_Y - 12 or x < PF_X - 8 or x > PF_R + 8 then
      hit = true
    elseif bact and abs(x - b.x) < 18 and abs(y - b.y) < 22 then
      F.hit_boss(sdm[i])
      hit = true
    else
      for j = #enemies, 1, -1 do
        local e = enemies[j]
        local r = e.r
        if abs(x - e.x) < r + 3 and abs(y - e.y) < r + 6 then
          e.hp = e.hp - sdm[i]
          if e.hp <= 0 then F.kill_enemy(j) end
          hit = true
          break
        end
      end
    end
    if hit then del_shot(i) end
  end
end

function F.update_bullets()
  local ppx, ppy = px, py
  local can_hit = p_inv == 0 and p_dying == 0 and bomb_t == 0
  local i = 1
  while i <= nb do
    local vx, vy = bvx[i], bvy[i]
    local m = bm[i]
    if m ~= 1 then
      vx = vx * m; vy = vy * m
      bvx[i], bvy[i] = vx, vy
    end
    local g = bay[i]
    if g ~= 0 then
      vy = vy + g
      bvy[i] = vy
    end
    local x = bx[i] + vx
    local y = by[i] + vy
    bx[i], by[i] = x, y
    local tm = btm[i]
    if tm > 0 then
      tm = tm - 1
      btm[i] = tm
      if tm == 0 then
        local ev = bev[i]
        local s = bsp[i]
        if ev == 1 then
          local a = atan(ppy - y, ppx - x)
          bvx[i], bvy[i] = cos(a) * s, sin(a) * s
        else
          local a = atan(vy, vx) + (ev == 2 and 0.6 or -0.6)
          bvx[i], bvy[i] = cos(a) * s, sin(a) * s
        end
        bm[i] = 1
      end
    end
    if x < PF_X - 20 or x > PF_R + 20 or y > PF_B + 20 or y < PF_Y - 60 then
      del_bullet(i)
    else
      local dx = x - ppx
      local dy = y - ppy
      if dx < 20 and dx > -20 and dy < 20 and dy > -20 then
        local d2 = dx * dx + dy * dy
        local t = bt[i]
        if d2 < BHR2[t] then
          if can_hit then
            p_dying = 7
            can_hit = false
            del_bullet(i)
            i = i - 1
          end
        elseif bgz[i] == 0 and d2 < BGR2[t] and p_dying == 0 then
          bgz[i] = 1
          graze = graze + 1
          add_score(200)
          tone(1800, 12)
        end
      end
      i = i + 1
    end
  end
  -- 敵本体との接触
  if can_hit then
    for j = 1, #enemies do
      local e = enemies[j]
      if abs(e.x - ppx) < e.r - 2 and abs(e.y - ppy) < e.r - 2 then
        p_dying = 7
        break
      end
    end
  end
end

function F.update_items()
  local collect_all = py < PF_Y + 48 and p_dying == 0
  for i = ni, 1, -1 do
    local x, y = ix[i], iy[i]
    if collect_all then ia[i] = 1 end
    local dx, dy = px - x, py - y
    if ia[i] == 1 then
      local d = sqrt(dx * dx + dy * dy) + 0.01
      x = x + dx / d * 7
      y = y + dy / d * 7
    else
      local v = min(ivy[i] + 0.07, 1.8)
      ivy[i] = v
      y = y + v
      if focus and dx * dx + dy * dy < 1600 then
        x = x + dx * 0.15; y = y + dy * 0.15
      end
    end
    ix[i], iy[i] = x, y
    dx, dy = px - x, py - y
    if dx * dx + dy * dy < 200 then
      local k = ik[i]
      if k == I_P then
        if power < MAX_POWER then power = power + 1 else add_score(1000) end
        add_score(10)
      elseif k == I_PT then
        local v = ia[i] == 1 and 10000 or max(2000, floor(10000 - (y - PF_Y) * 45))
        add_score(v)
      elseif k == I_B then
        if bombs < 8 then bombs = bombs + 1 end
      elseif k == I_1UP then
        if lives < 8 then lives = lives + 1 end
        se("extend")
      else
        add_score(100 * stage)
      end
      tone(k == I_STAR and 1400 or 1000, 10)
      del_item(i)
    elseif y > PF_B + 10 then
      del_item(i)
    end
  end
end

function F.update_fx()
  for i = #fx, 1, -1 do
    local f = fx[i]
    f.t = f.t + 1
    if f.t >= f.life then table.remove(fx, i) end
  end
end

function F.boss_defeated()
  local b = boss
  b.dead = 1
  b.active = false
  F.cancel_bullets(true)
  F.add_fx(b.x, b.y, 4, 90, 30, C.WHITE)
  F.add_fx(b.x, b.y, 2, 60, 24, C.JADE)
  S.flash_t = 8
  se("bossdie")
  F.drop_items(b.x, b.y, 8, 12)
  if stage < 6 then add_item(b.x, b.y, I_B) end
  for k = 1, ni do ia[k] = 1 end
end

function F.boss_phase_end(captured)
  local b = boss
  if b.spell then
    if captured and not spell_fail then
      local bonus = floor(b.bonus * max(0.5, b.timer / (PH[stage][b.ph].time * 30)))
      add_score(bonus)
      F.show_msg("Get Spell Card Bonus!!  " .. bonus, C.GOLD, 120)
    else
      F.show_msg("Bonus Failed...", C.DIM, 90)
    end
  end
  F.cancel_bullets(true)
  F.add_fx(b.x, b.y, 4, 70, 18, C.JADE)
  F.drop_items(b.x, b.y, 4, 6)
  if b.ph >= #PH[stage] then
    F.boss_defeated()
  else
    b.ph = b.ph + 1
    F.bmove(b, PF_CX, PF_Y + 40)
    F.boss_phase_start(b)
  end
end

function F.update_boss()
  local b = boss
  if not b then return end
  b.frame = b.frame + 1
  b.x = b.x + (b.tx - b.x) * 0.06
  b.y = b.y + (b.ty - b.y) * 0.06
  if b.dead > 0 then
    b.dead = b.dead + 1
    return
  end
  if not b.active then return end
  if b.cutin > 0 then b.cutin = b.cutin - 1 end
  PH[stage][b.ph].f(b, b.t)
  b.t = b.t + 1
  b.timer = b.timer - 1
  if bomb_t > 0 and bomb_t % 3 == 0 then F.hit_boss(3) end
  if b.hp <= 0 then
    F.boss_phase_end(true)
  elseif b.timer <= 0 then
    F.boss_phase_end(false)
  elseif b.spell and b.timer <= 300 and b.timer % 30 == 0 then
    tone(900, 40)
  end
end

function F.update_bomb()
  if bomb_t <= 0 then return end
  bomb_t = bomb_t - 1
  if bomb_t % 4 == 0 then
    F.add_fx(bomb_x, bomb_y, 8, 110, 22, bomb_t % 8 == 0 and C.ORANGE or C.GOLD)
  end
  nb = 0
  for j = #enemies, 1, -1 do
    local e = enemies[j]
    e.hp = e.hp - 2
    if e.hp <= 0 then F.kill_enemy(j) end
  end
end


function F.game_step()
  gtime = gtime + 1
  bg_scroll = bg_scroll + 1
  if S.intro_t > 0 then S.intro_t = S.intro_t - 1 end
  if S.msg_t > 0 then S.msg_t = S.msg_t - 1 end
  if S.flash_t > 0 then S.flash_t = S.flash_t - 1 end

  if gphase == "continue" then
    F.continue_step()
    return
  end

  -- 喰らいボム猶予
  if p_dying > 0 then
    if edge[B_NEAR] and bombs > 0 then
      F.use_bomb()
    else
      p_dying = p_dying - 1
      if p_dying == 0 then F.player_die() end
      if gphase == "continue" then return end
    end
  end

  local in_talk = (gphase == "talk" or gphase == "post")
  if p_dying == 0 then F.update_player(not in_talk and gphase ~= "clear") end
  F.update_shots()

  if gphase == "stage" then
    while events[ev_i] and events[ev_i][1] <= gtime do
      events[ev_i][2]()
      ev_i = ev_i + 1
    end
    if gtime >= STAGE_BOSS_AT and #enemies == 0 then
      F.spawn_boss()
      gphase = "talk"
      F.talk_start(DATA.stages[stage] and DATA.stages[stage].pre)
      F.play_bgm(stage == 6 and "final" or "boss")
      nb = 0
    end
  elseif gphase == "talk" then
    if F.talk_step() then
      gphase = "boss"
      boss.active = true
      F.boss_phase_start(boss)
    end
  elseif gphase == "boss" then
    if boss and boss.dead > 45 then
      gphase = "post"
      F.talk_start(DATA.stages[stage] and DATA.stages[stage].post)
    end
  elseif gphase == "post" then
    if F.talk_step() then
      gphase = "clear"
      S.clear_t = 0
      S.clear_bonus = stage * 1000000 + power * 1000 + graze * 500 + (diff - 1) * 500000
      add_score(S.clear_bonus)
      boss = nil
    end
  elseif gphase == "clear" then
    S.clear_t = S.clear_t + 1
    if S.clear_t >= 150 then
      if stage < 6 then
        F.begin_stage(stage + 1)
      else
        F.record_hiscore()
        save.clear[diff] = 1
        F.write_save()
        scene = "ending"
        F.ending_start()
      end
      return
    end
  end

  F.update_enemies()
  F.update_boss()
  F.update_bullets()
  F.update_items()
  F.update_bomb()
  F.update_fx()
end

F.continue_step = function()
  local left = 3 - continues
  if edge[B_UP] or edge[B_DOWN] then S.menu_cur = 3 - S.menu_cur; se("select") end
  if edge[B_FAR] or edge[B_OPR] then
    if S.menu_cur == 1 and left > 0 then
      continues = continues + 1
      lives = diff == 1 and 4 or 3
      bombs = 3
      score = continues
      p_inv = 120
      gphase = S.gphase_before_continue or "stage"
    else
      F.record_hiscore()
      scene = "title"
      S.menu_cur = 1
      F.play_bgm("title")
    end
  end
end

-- ---------------------------------------------------------------------------
-- 背景（面ごと）: fill_rect 中心で軽量に。枠は最後に上書きするのではみ出し可
-- ---------------------------------------------------------------------------
local BGC = {
  { rgb(58, 46, 52), rgb(92, 84, 52), rgb(74, 96, 52), rgb(96, 76, 58), rgb(120, 96, 70), rgb(40, 60, 36) },
  { rgb(20, 34, 26), rgb(74, 64, 64), rgb(200, 44, 32), rgb(30, 20, 20), rgb(34, 58, 40), rgb(90, 80, 80) },
  { rgb(30, 68, 108), rgb(70, 72, 84), rgb(170, 210, 230), rgb(52, 54, 64), rgb(40, 86, 128), rgb(255, 255, 255) },
  { rgb(120, 80, 50), rgb(104, 68, 42), rgb(70, 44, 28), rgb(210, 170, 80), rgb(150, 110, 50), rgb(60, 40, 26) },
  { rgb(28, 28, 46), rgb(58, 58, 80), rgb(84, 84, 108), rgb(255, 250, 170), rgb(40, 40, 62), rgb(110, 110, 140) },
  { rgb(8, 22, 30), rgb(20, 60, 50), rgb(44, 110, 88), rgb(140, 250, 200), rgb(210, 255, 235), rgb(14, 40, 36) },
}

local function hash(n)
  return (n * 7919 + 104729) % 997
end

function F.draw_bg()
  local s = bg_scroll
  local c = BGC[stage]
  if stage == 1 then
    fill_rect(PF_X, PF_Y, PF_W, PF_H, c[1])
    local off = (s % 36)
    local base = s // 36
    for r = -1, 5 do
      local y = PF_Y + r * 36 + off
      local g = r - base
      local col = (g % 2 == 0) and c[2] or c[3]
      fill_rect(PF_X, y + 2, PF_CX - 20 - PF_X, 32, col)
      fill_rect(PF_CX + 20, y + 2, PF_R - PF_CX - 20, 32, (g % 2 == 0) and c[3] or c[2])
      fill_rect(PF_X, y + 12, PF_CX - 20 - PF_X, 1, c[6])
      fill_rect(PF_X, y + 24, PF_CX - 20 - PF_X, 1, c[6])
      fill_rect(PF_CX + 20, y + 12, PF_R - PF_CX - 20, 1, c[6])
      fill_rect(PF_CX + 20, y + 24, PF_R - PF_CX - 20, 1, c[6])
      if g % 2 == 0 then
        fill_rect(PF_CX - 22, y + 10, 3, 8, c[4])
        fill_rect(PF_CX - 23, y + 6, 5, 5, C.ORANGE)
        fill_rect(PF_CX + 19, y + 10, 3, 8, c[4])
        fill_rect(PF_CX + 18, y + 6, 5, 5, C.ORANGE)
      end
    end
    fill_rect(PF_CX - 16, PF_Y, 32, PF_H, c[5])
    fill_rect(PF_CX - 1, PF_Y, 2, PF_H, c[4])
  elseif stage == 2 then
    fill_rect(PF_X, PF_Y, PF_W, PF_H, c[1])
    fill_rect(PF_CX - 44, PF_Y, 88, PF_H, c[2])
    local off = (s * 3 // 2) % 30
    for r = -1, 6 do
      local y = PF_Y + r * 30 + off
      fill_rect(PF_CX - 58, y - 2, 116, 3, c[4])
      fill_rect(PF_CX - 54, y + 1, 108, 5, c[3])
      fill_rect(PF_CX - 44, y + 9, 88, 2, c[3])
      fill_rect(PF_CX - 48, y + 6, 7, 7, c[3])
      fill_rect(PF_CX + 41, y + 6, 7, 7, c[3])
    end
    local off2 = s % 48
    for r = -1, 4 do
      local y = PF_Y + r * 48 + off2
      local g = r - s // 48
      fill_circle(PF_X + 16 + hash(g) % 24, y + 10, 14, c[5])
      fill_circle(PF_R - 16 - hash(g + 3) % 24, y + 30, 14, c[5])
    end
  elseif stage == 3 then
    fill_rect(PF_X, PF_Y, PF_W, PF_H, c[1])
    local off = s % 40
    for r = -1, 5 do
      local y = PF_Y + r * 40 + off
      local g = r - s // 40
      local w1 = 26 + hash(g) % 20
      local w2 = 26 + hash(g + 7) % 20
      fill_rect(PF_X, y, w1, 40, c[2])
      fill_rect(PF_X, y + 8, w1 + 8, 20, c[4])
      fill_rect(PF_R - w2, y, w2, 40, c[2])
      fill_rect(PF_R - w2 - 8, y + 14, w2 + 8, 18, c[4])
    end
    local off3 = (s * 5 // 2) % 180
    for k = 0, 13 do
      local x = PF_X + 50 + hash(k) % 124
      local y = PF_Y + (hash(k + 20) + off3) % 180
      fill_rect(x, y, 1, 10 + k % 3 * 4, c[5])
      fill_rect(x + 2, y + 4, 1, 6, c[3])
    end
    local mx = (s // 2) % 300
    fill_alpha(PF_X, PF_Y + 30 + (mx % 120), PF_W, 18, c[6], 36)
    fill_alpha(PF_X, PF_Y + 110 - (mx % 90), PF_W, 14, c[6], 28)
  elseif stage == 4 then
    local off = s % 40
    for k = 0, 13 do
      local x = PF_X + k * 16
      fill_rect(x, PF_Y, 16, PF_H, (k % 2 == 0) and c[1] or c[2])
      local ph = (hash(k) % 40)
      for r = -1, 5 do
        fill_rect(x, PF_Y + r * 40 + ((off + ph) % 40), 16, 1, c[3])
      end
      fill_rect(x, PF_Y, 1, PF_H, c[6])
    end
    local ang = s * 0.03
    for gi = 0, 1 do
      local gx = gi == 0 and PF_X + 32 or PF_R - 32
      local gy = gi == 0 and PF_Y + 60 or PF_Y + 128
      local a = gi == 0 and ang or -ang
      fill_circle(gx, gy, 22, c[5])
      for k = 0, 5 do
        local aa = a + k * PI / 3
        fill_circle(gx + floor(cos(aa) * 24), gy + floor(sin(aa) * 24), 5, c[5])
      end
      fill_circle(gx, gy, 8, c[4])
      fill_circle(gx, gy, 3, c[6])
    end
  elseif stage == 5 then
    fill_rect(PF_X, PF_Y, PF_W, PF_H, (s % 220 < 4) and c[6] or c[1])
    local off = (s * 3 // 2) % 60
    for r = -1, 3 do
      local y = PF_Y + r * 60 + off
      local g = r - (s * 3 // 2) // 60
      for k = 0, 2 do
        local x = PF_X + (hash(g * 3 + k) % PF_W)
        fill_circle(x, y + k * 16, 16 + k * 4, (k == 1) and c[3] or c[2])
      end
      fill_rect(PF_X + hash(g) % 150, y + 30, 70, 10, c[5])
    end
    if s % 220 < 6 then
      local x = PF_X + 30 + hash(s // 220) % 160
      local y = PF_Y
      for k = 1, 6 do
        local nx = x + random(-14, 14)
        local ny = y + 30
        draw_line(x, y, nx, ny, c[4])
        draw_line(x + 1, y, nx + 1, ny, c[4])
        x, y = nx, ny
      end
    end
  else
    fill_rect(PF_X, PF_Y, PF_W, PF_H, c[1])
    for k = 0, 17 do
      local x = PF_X + hash(k) % PF_W
      local y = PF_Y + (hash(k + 40) + s // 4) % PF_H
      fill_rect(x, y, 1, 1, c[5])
    end
    fill_circle(PF_R - 50, PF_Y + 42, 30, c[4])
    fill_circle(PF_R - 42, PF_Y + 36, 22, c[5])
    local off = (s * 4 // 5) % 16
    for r = 0, 6 do
      local y = PF_Y + 84 + r * 16 + off
      fill_rect(PF_X, y, PF_W, 12, c[2])
      fill_rect(PF_X, y, PF_W, 2, c[3])
      local sh = ((r - (s * 4 // 5) // 16) % 2) * 6
      for x = PF_X + sh, PF_R - 2, 12 do
        fill_rect(x, y + 2, 1, 10, c[6])
      end
    end
  end
end

-- ---------------------------------------------------------------------------
-- 描画: プレイフィールド
-- ---------------------------------------------------------------------------
function F.draw_enemies(anim)
  local fid, mid = img.fairy, img.mob
  for i = 1, #enemies do
    local e = enemies[i]
    local x, y = e.x // 1, e.y // 1
    local spr = e.spr
    if spr <= 3 or spr >= 6 then
      local ci = spr >= 6 and spr - 6 or spr
      if e.big then draw_circle(x, y, 12 + anim, C.WHITE) end
      if fid then cblit(fid, x - 8, y - 8, (ci * 2 + anim) * 16, 0, 16, 16) end
    elseif spr == 4 then
      if mid then cblit(mid, x - 6, y - 6, anim * 12, 2, 12, 12) end
    else
      if mid then cblit(mid, x - 8, y - 8, 24 + anim * 16, 0, 16, 16) end
    end
  end
end

function F.draw_boss(anim)
  local b = boss
  if not b or (b.dead > 0 and b.dead % 4 < 2) then return end
  local x, y = b.x // 1, b.y // 1
  local r = 30 + floor(sin(frame * 0.1) * 3)
  local col = b.spell and C.RED or C.JADE
  if b.active or b.dead > 0 then
    draw_circle(x, y, r, col)
    draw_circle(x, y, r - 6, col)
    for k = 0, 5 do
      local a = frame * 0.05 + k * PI / 3
      fill_rect(x + floor(cos(a) * (r - 3)) - 1, y + floor(sin(a) * (r - 3)) - 1, 3, 3, col)
    end
  end
  local id = img["boss_" .. b.key]
  if id then cblit(id, x - 16, y - 20, anim * 32, 0, 32, 40) end
end

function F.draw_player(anim)
  local mid = img.misc
  if p_inv > 0 and (frame // 2) % 2 == 0 and p_dying == 0 then
    -- 点滅
  else
    local id = img.player
    if id then cblit(id, px // 1 - 10, py // 1 - 13, anim * 20, 0, 20, 25) end
  end
  if mid then
    local lv = power_level()
    for k = 1, lv do
      cblit(mid, opt_x[k] // 1 - 4, opt_y[k] // 1 - 4, 12 + anim * 8, 10, 8, 8)
    end
  end
end

function F.draw_shots()
  local mid = img.misc
  if not mid then return end
  for i = 1, ns do
    if sk[i] == 0 then
      cblit(mid, sx[i] // 1 - 3, sy[i] // 1 - 7, 0, 0, 6, 14)
    else
      cblit(mid, sx[i] // 1 - 3, sy[i] // 1 - 4, 6, 0, 6, 8)
    end
  end
end

function F.draw_items()
  local mid = img.misc
  if not mid then return end
  for i = 1, ni do
    local k = ik[i]
    local s = ITEM_SZ[k]
    local y = iy[i] // 1
    if y < PF_Y then
      -- 画面上の外にあるアイテムは位置マーカーのみ
      cblit(mid, ix[i] // 1 - 3, PF_Y, ITEM_SX[k], 0, s, 3)
    else
      cblit(mid, ix[i] // 1 - s // 2, y - s // 2, ITEM_SX[k], 0, s, s)
    end
  end
end

function F.draw_bullets()
  local id = img.bullets
  if not id then return end
  local L, T, R, B = PF_X, PF_Y, PF_R, PF_B
  for i = 1, nb do
    local t = bt[i]
    local h = BHF[t]
    local s = BSZ[t]
    local x = bx[i] // 1 - h
    local y = by[i] // 1 - h
    if x >= L and y >= T and x + s <= R and y + s <= B then
      blitk(id, x, y, BSX[t], BSY[t], s, s)
    else
      cblit(id, x, y, BSX[t], BSY[t], s, s)
    end
  end
end

function F.draw_fx()
  for i = 1, #fx do
    local f = fx[i]
    local r = f.r0 + (f.r1 - f.r0) * f.t // f.life
    draw_circle(f.x // 1, f.y // 1, r, f.c)
  end
  if boss and boss.warn then
    for k = 1, #boss.warn do
      local w = boss.warn[k]
      if w.t > 0 and (w.t // 3) % 2 == 0 then fill_rect(w.x, PF_Y, 1, PF_H, BGC[5][4]) end
    end
  end
end

function F.draw_boss_hud()
  local b = boss
  if not b or not b.active then return end
  -- HP バー
  local w = floor((PF_W - 40) * max(0, b.hp) / b.maxhp)
  fill_rect(PF_X + 4, PF_Y + 2, PF_W - 40, 3, C.FRAME)
  fill_rect(PF_X + 4, PF_Y + 2, w, 3, b.spell and C.RED or C.WHITE)
  -- 残りフェーズ数
  local rest = #PH[stage] - b.ph
  for k = 1, rest do fill_rect(PF_X + 4 + (k - 1) * 6, PF_Y + 7, 4, 4, C.GOLD) end
  -- タイマー
  local sec = b.timer // 30
  rtext(PF_R - 4, PF_Y + 1, tostring(sec), sec < 10 and C.RED or C.TXT)
  -- スペル名
  if b.spell_name then
    local sw = text_w(b.spell_name)
    local y = PF_Y + 14
    if b.cutin > 0 then y = PF_Y + 14 + b.cutin * 2 end
    fill_alpha(PF_R - sw - 8, y - 1, sw + 6, 14, C.BLACK, 150)
    text(PF_R - sw - 5, y, b.spell_name, C.TXT)
  end
  -- 自機との位置合わせ用マーカー（下端）
  fill_rect(b.x // 1 - 8, PF_B - 3, 16, 3, C.RED)
end

function F.draw_cutin()
  local b = boss
  if not b or b.cutin <= 0 then return end
  local id = img["face_" .. b.key]
  if id then
    local k = 45 - b.cutin
    local x = PF_R - 90 + max(0, 20 - k) * 4
    cblit(id, x, PF_Y + 30 - min(k, 20), 0, 0, 80, 112)
  end
end

function F.draw_talk()
  if not talk then return end
  local ln = talk.list[talk.i]
  local who = ln[1]
  local bkey = BOSS_KEY[stage]
  local fp, fb = img.face_hotaru, img["face_" .. bkey]
  local pdn = (who == "p") and 0 or 10
  local bdn = (who == "b") and 0 or 10
  if fp then cblit(fp, PF_X - 6, PF_B - 104 + pdn, 0, 0, 80, 112) end
  local boss_spoke = false
  for k = 1, talk.i do if talk.list[k][1] == "b" then boss_spoke = true break end end
  if fb and boss_spoke then
    cblit(fb, PF_R - 74, PF_B - 104 + bdn, 0, 0, 80, 112)
    if gphase == "talk" then
      local nm = DATA.names[bkey] or ""
      local tl = DATA.titles[bkey] or ""
      local bw = max(text_w(tl), text_w(nm)) + 12
      fill_alpha(PF_R - bw - 2, PF_Y + 58, bw, 30, C.BLACK, 140)
      rtext(PF_R - 6, PF_Y + 60, tl, C.DIM)
      rtext(PF_R - 6, PF_Y + 74, nm, C.JADE)
    end
  end
end

function F.draw_textbox(name, s, col)
  fill_rect(0, BOX_Y - 2, W, H - BOX_Y + 2, C.BOX)
  fill_rect(0, BOX_Y - 2, W, 1, C.FRAME3)
  fill_rect(4, BOX_Y + 1, 2, 34, col)
  if name then text(10, BOX_Y + 1, name, col) end
  local y = BOX_Y + (name and 13 or 6)
  text(10, y, s, C.TXT)
end

-- ---------------------------------------------------------------------------
-- 描画: 枠・右パネル（値が変わった時だけ文字列を作り直す）
-- ---------------------------------------------------------------------------
function F.hud_str(key, v, fmt)
  local c = S.hud_cache[key]
  if c and c[1] == v then return c[2] end
  local s = fmt(v)
  S.hud_cache[key] = { v, s }
  return s
end
function F.fmt_int(v) return tostring(floor(v)) end
function F.fmt_power(v) return string.format("%d.%02d", v // 32, (v % 32) * 100 // 32) end
function F.fmt_stars(v) return string.rep("★", v) end
function F.fmt_bombs(v) return string.rep("◆", v) end

function F.draw_frame_top()
  fill_rect(0, 0, W, PF_Y, C.FRAME)
  fill_rect(0, PF_Y - 2, W, 1, C.FRAME2)
  local st = DATA.stages[stage]
  text(6, 4, "STAGE " .. stage, C.GOLD)
  if st then text(62, 4, st.name, C.TXT) end
  rtext(W - 6, 4, DATA.title_jp or "", C.JADE)
end

function F.draw_panel()
  -- 左右の枠
  fill_rect(0, PF_Y, PF_X, PF_H, C.FRAME2)
  fill_rect(PF_R, PF_Y, PN_X - PF_R, PF_H, C.FRAME2)
  fill_rect(PN_X, PF_Y, W - PN_X, PF_H, C.FRAME)
  fill_rect(PF_R + 2, PF_Y, 1, PF_H, C.FRAME3)
  local x = PN_X + 4
  local rx = W - 4
  text(x, 24, "HiScore", C.DIM)
  rtext(rx, 36, F.hud_str("hi", hiscore, F.fmt_int), C.TXT)
  text(x, 50, "Score", C.DIM)
  rtext(rx, 62, F.hud_str("sc", score, F.fmt_int), C.WHITE)
  text(x, 80, "Player", C.DIM)
  text(x, 92, F.hud_str("lv", lives, F.fmt_stars), C.PINK)
  text(x, 106, "Bomb", C.DIM)
  text(x, 118, F.hud_str("bm", bombs, F.fmt_bombs), rgb(120, 230, 140))
  text(x, 136, "Power", C.DIM)
  rtext(rx, 136, F.hud_str("pw", power, F.fmt_power), C.ORANGE)
  text(x, 150, "Graze", C.DIM)
  rtext(rx, 150, F.hud_str("gz", graze, F.fmt_int), C.BLUE)
  text(x, 168, DIFF_NAME[diff], DIFF_COL[diff])
  fill_circle(W - 18, 184, 9, C.JADE)
  fill_circle(W - 14, 181, 8, C.FRAME)
end

function F.draw_frame_bottom()
  if talk then
    local ln = talk.list[talk.i]
    local who = ln[1]
    local key = who == "p" and "hotaru" or BOSS_KEY[stage]
    F.draw_textbox(DATA.names[key], F.talk_visible_text(), who == "p" and C.ORANGE or C.JADE)
    return
  end
  fill_rect(0, PF_B, W, H - PF_B, C.FRAME)
  fill_rect(0, PF_B + 1, W, 1, C.FRAME2)
  local st = DATA.stages[stage]
  if st then
    local nm = (gphase == "boss" or gphase == "post") and st.boss_bgm_name or st.bgm_name
    text(8, PF_B + 8, "♪ " .. nm, C.DIM)
  end
  text(8, PF_B + 24, "SHOT:FAR BOMB:NEAR SLOW:OP-R PAUSE:OP-L", rgb(80, 100, 96))
  if focus then text(276, PF_B + 24, "[SLOW]", C.ORANGE) end
end

-- ---------------------------------------------------------------------------
-- 描画: ゲーム画面
-- ---------------------------------------------------------------------------
function F.draw_center_box(lines, y0)
  local h = #lines * 14 + 10
  fill_alpha(PF_X + 16, y0 - 6, PF_W - 32, h, C.BLACK, 170)
  for k = 1, #lines do
    local l = lines[k]
    ctext(PF_CX, y0 + (k - 1) * 14, l[1], l[2])
  end
end

function F.draw_game()
  local anim = (frame // 8) % 2
  F.draw_bg()
  if S.flash_t > 0 then fill_alpha(PF_X, PF_Y, PF_W, PF_H, C.WHITE, 90) end
  F.draw_items()
  F.draw_enemies(anim)
  F.draw_boss(anim)
  F.draw_player(anim)
  F.draw_shots()
  F.draw_bullets()
  F.draw_fx()
  if focus and img.misc then
    cblit(img.misc, px // 1 - 2, py // 1 - 2, 28, 10, 5, 5)
  end
  F.draw_boss_hud()
  F.draw_cutin()
  F.draw_talk()

  if S.intro_t > 0 and S.intro_t < 140 and not S.paused then
    local st = DATA.stages[stage]
    if st then
      F.draw_center_box({
        { "STAGE " .. stage, C.GOLD },
        { st.name, C.WHITE },
        { st.en, C.DIM },
      }, PF_Y + 50)
      if S.intro_t < 110 then
        text(PF_X + 4, PF_B - 14, "♪ " .. st.bgm_name, C.TXT)
      end
    end
  end
  if S.msg_t > 0 and S.msg then
    fill_alpha(PF_X, PF_Y + 32, PF_W, 15, C.BLACK, 150)
    ctext(PF_CX, PF_Y + 34, S.msg, S.msg_c)
  end
  if gphase == "clear" then
    F.draw_center_box({
      { "STAGE CLEAR!", C.GOLD },
      { "Bonus " .. S.clear_bonus, C.WHITE },
    }, PF_Y + 60)
  end
  if gphase == "continue" then
    local left = 3 - continues
    F.draw_center_box({
      { "満身創痍……", C.RED },
      { "コンティニューしますか？", C.TXT },
      { (S.menu_cur == 1 and "▶ " or "  ") .. "はい（残り " .. left .. " 回）", left > 0 and C.WHITE or C.DIM },
      { (S.menu_cur == 2 and "▶ " or "  ") .. "いいえ（タイトルへ）", C.WHITE },
    }, PF_Y + 50)
  end
  if S.paused then
    local items = { "ゲームを再開", "最初からやり直す", "タイトルへ戻る" }
    local lines = { { "- PAUSE -", C.GOLD } }
    for k = 1, 3 do
      lines[#lines + 1] = { (S.pause_cur == k and "▶ " or "  ") .. items[k], S.pause_cur == k and C.WHITE or C.DIM }
    end
    F.draw_center_box(lines, PF_Y + 56)
  end

  -- 枠は最後に描いて、はみ出した円などを隠す
  F.draw_frame_top()
  F.draw_panel()
  F.draw_frame_bottom()
end

-- ---------------------------------------------------------------------------
-- タイトル
-- ---------------------------------------------------------------------------
local TITLE_ITEMS = { "はじめる", "操作説明", "おわる" }
local SKY = {}
for k = 0, 11 do SKY[k] = rgb(6 + k * 2, 10 + k * 4, 30 + k * 3) end

function F.draw_sky(tint)
  for k = 0, 11 do
    fill_rect(0, k * 20, W, 20, SKY[k])
  end
end

function F.draw_lanterns(s, n, y0, span)
  for k = 0, n - 1 do
    local x = (hash(k) * 3 + floor(sin(s * 0.02 + k) * 8)) % W
    local y = y0 + span - ((hash(k + 11) + s // 2) % span)
    fill_rect(x, y, 4, 5, C.ORANGE)
    fill_rect(x + 1, y + 1, 2, 3, rgb(255, 240, 180))
  end
end

function F.draw_title()
  local s = frame
  F.draw_sky()
  fill_circle(290, 36, 24, rgb(140, 250, 200))
  fill_circle(300, 28, 22, SKY[1])
  for k = 0, 20 do
    fill_rect(hash(k) % W, hash(k + 50) % 120, 1, 1, C.WHITE)
  end
  F.draw_lanterns(s, 18, 100, 140)
  local manual = S.title_mode ~= "main" and S.title_mode ~= "diff"
  if img.face_hotaru and not manual then blitk(img.face_hotaru, 10, 128, 0, 0, 80, 112) end
  if img.logo then
    blitk(img.logo, 118, 22, 0, 0, 144, 40)
  else
    text(180, 40, DATA.title_jp or "翠灯夜行", C.JADE)
  end
  ctext(196, 70, "～ " .. (DATA.title_en or "") .. " ～", C.GOLD)

  if S.title_mode == "main" then
    for k = 1, 3 do
      local sel = S.menu_cur == k
      ctext(210, 112 + k * 18, (sel and "▶ " or "") .. TITLE_ITEMS[k], sel and C.WHITE or C.DIM)
    end
  elseif S.title_mode == "diff" then
    ctext(210, 112, "難易度を選んでください", C.TXT)
    for k = 1, 3 do
      local sel = S.menu_cur == k
      local y = 108 + k * 26
      ctext(210, y, (sel and "▶ " or "") .. DIFF_NAME[k], sel and DIFF_COL[k] or C.DIM)
      local info = "HI " .. (save.hi[k] or 0) .. ((save.clear[k] or 0) > 0 and "  CLEAR" or "")
      ctext(210, y + 12, info, sel and C.TXT or rgb(70, 90, 90))
    end
  else
    F.draw_manual()
  end
  if not manual then text(100, 228, "2026 Kawashiro Electric", rgb(90, 110, 110)) end
end

-- 操作説明: 画面中央のパネルに「キー｜説明」の 2 列で表示（説明の無い行は中央ぞろえ）
function F.draw_manual()
  local m = DATA.manual or {}
  fill_alpha(20, 86, 280, 150, C.BLACK, 200)
  fill_rect(20, 86, 280, 1, C.JADE)
  fill_rect(20, 235, 280, 1, C.JADE)
  if not S.manual_cols then
    local kw, dw = 0, 0
    for k = 1, #m do
      local key, desc = m[k]:match("^(.-)：(.*)$")
      if key then
        kw = max(kw, text_w(key))
        dw = max(dw, text_w(desc))
      elseif m[k]:sub(1, 3) == "（" then
        dw = max(dw, text_w(m[k]))
      end
    end
    local x0 = 160 - (kw + 10 + dw) // 2
    S.manual_cols = { x0, x0 + kw + 10 }
  end
  local kx, dx = S.manual_cols[1], S.manual_cols[2]
  local y = 94
  for k = 1, #m do
    local line = m[k]
    if line == "" then
      y = y + 6
    else
      local key, desc = line:match("^(.-)：(.*)$")
      if key then
        text(kx, y, key, C.GOLD)
        text(dx, y, desc, C.TXT)
      elseif line:sub(1, 3) == "（" then
        text(dx, y, line, C.DIM)
      else
        ctext(160, y, line, C.TXT)
      end
      y = y + 13
    end
  end
end

function F.title_step()
  if S.title_mode == "main" then
    if edge[B_UP] then S.menu_cur = (S.menu_cur + 1) % 3 + 1; se("select") end
    if edge[B_DOWN] then S.menu_cur = S.menu_cur % 3 + 1; se("select") end
    if edge[B_FAR] or edge[B_OPR] then
      se("select")
      if S.menu_cur == 1 then S.title_mode = "diff"; S.menu_cur = diff
      elseif S.menu_cur == 2 then S.title_mode = "manual"
      else return true end
    end
  elseif S.title_mode == "diff" then
    if edge[B_UP] then S.menu_cur = (S.menu_cur + 1) % 3 + 1; se("select") end
    if edge[B_DOWN] then S.menu_cur = S.menu_cur % 3 + 1; se("select") end
    if edge[B_NEAR] or edge[B_OPL] then S.title_mode = "main"; S.menu_cur = 1 end
    if edge[B_FAR] or edge[B_OPR] then
      diff = S.menu_cur
      S.title_mode = "main"
      S.menu_cur = 1
      F.start_game()
    end
  else
    if edge[B_FAR] or edge[B_NEAR] or edge[B_OPL] or edge[B_OPR] then S.title_mode = "main" end
  end
  return false
end

function F.go_title()
  scene = "title"
  S.title_mode = "main"
  S.menu_cur = 1
  S.paused = false
  F.clear_pools()
  boss = nil
  talk = nil
  F.load_img("face_hotaru", "img/face_hotaru.bin", 80, 112)
  F.load_img("logo", "img/logo.bin", 144, 40)
  bgm_cur = nil
  F.play_bgm("title")
end

-- ---------------------------------------------------------------------------
-- エンディング → スタッフロール → THE END
-- ---------------------------------------------------------------------------

F.ending_start = function()
  S.end_i, S.end_shown, S.end_t = 1, 0, 0
  F.clear_pools()
  boss = nil
  talk = nil
  for i = 1, 5 do
    F.free_img("face_" .. BOSS_KEY[i])
    F.free_img("boss_" .. BOSS_KEY[i])
  end
  F.load_img("face_mikoto", "img/face_mikoto.bin", 80, 112)
  bgm_cur = nil
  F.play_bgm("ending")
end

function F.staff_start()
  scene = "staff"
  S.staff_y = 0
  F.free_img("face_mikoto")
  F.free_img("boss_mikoto")
  for i = 1, 6 do
    F.load_img("boss_" .. BOSS_KEY[i], "img/boss_" .. BOSS_KEY[i] .. ".bin", 64, 40)
  end
  S.staff_h = 0
  for _, l in ipairs(DATA.staff or {}) do
    local k = l[1]
    S.staff_h = S.staff_h + (k == "g" and l[2] or (k == "c" and 46 or (k == "t" and 20 or 16)))
  end
end

function F.ending_step()
  S.end_t = S.end_t + 1
  local ln = DATA.ending[S.end_i]
  if not ln then F.staff_start() return end
  local len = utf8.len(ln[2]) or 0
  S.end_shown = S.end_shown + 1
  if edge[B_FAR] or (btn[B_NEAR] and S.end_t % 4 == 0) then
    if S.end_shown < len then
      S.end_shown = len
    else
      S.end_i = S.end_i + 1
      S.end_shown = 0
    end
  end
end

function F.draw_ending()
  local prog = min(1, (S.end_i - 1) / max(1, #DATA.ending - 1))
  for k = 0, 11 do
    local t = k / 11
    local r = floor(20 + prog * 200 * t + prog * 30)
    local g = floor(20 + prog * 120 * t + prog * 40)
    local b = floor(50 + prog * 60 * (1 - t) + 20)
    fill_rect(0, k * 20, W, 20, rgb(min(255, r), min(255, g), min(255, b)))
  end
  fill_circle(160, 170 - floor(prog * 50), 26, rgb(255, 220, 150))
  fill_rect(0, 160, W, 80, rgb(30 + floor(prog * 40), 50 + floor(prog * 50), 80 + floor(prog * 40)))
  for k = 0, 12 do
    local x = (hash(k) + S.end_t // 2) % (W + 20) - 10
    local y = 172 + (hash(k + 5) % 24)
    fill_rect(x, y, 5, 4, C.ORANGE)
    fill_rect(x + 1, y + 1, 3, 2, rgb(255, 240, 180))
  end
  local ln = DATA.ending[S.end_i]
  if not ln then return end
  local who = ln[1]
  if who == "mikoto" and img.face_mikoto then blitk(img.face_mikoto, 220, 90, 0, 0, 80, 112) end
  if who == "hotaru" and img.face_hotaru then blitk(img.face_hotaru, 20, 90, 0, 0, 80, 112) end
  local s = ln[2]
  if S.end_shown < (utf8.len(s) or 0) then
    local cut = utf8.offset(s, S.end_shown + 1)
    if cut then s = s:sub(1, cut - 1) end
  end
  local name = who ~= "n" and DATA.names[who] or nil
  F.draw_textbox(name, s, who == "hotaru" and C.ORANGE or (who == "mikoto" and C.JADE or C.DIM))
end

function F.staff_step()
  S.staff_y = S.staff_y + 0.7
  if btn[B_NEAR] then S.staff_y = S.staff_y + 3 end
  if S.staff_y > S.staff_h + H then
    scene = "theend"
    F.load_img("logo", "img/logo.bin", 144, 40)
    S.end_t = 0
  end
end

function F.draw_staff()
  F.draw_sky()
  fill_circle(60, 60, 20, rgb(255, 230, 170))
  F.draw_lanterns(frame, 22, 20, 220)
  local y = H - S.staff_y // 1
  local cx = 160
  -- キャスト欄（ドット絵 40px + 肩書き・名前）を 1 つの塊として中央に置く
  if not S.cast_x0 then
    local tw = 0
    for _, l in ipairs(DATA.staff or {}) do
      if l[1] == "c" then
        tw = max(tw, text_w(DATA.titles[l[2]] or "自機・灯籠守りの少女"), text_w(DATA.names[l[2]] or l[2]))
      end
    end
    S.cast_x0 = cx - (40 + tw) // 2
  end
  local cast_x = S.cast_x0
  for _, l in ipairs(DATA.staff or {}) do
    local k = l[1]
    if k == "g" then
      y = y + l[2]
    elseif k == "c" then
      if y > -44 and y < H then
        local key = l[2]
        local id = key == "hotaru" and img.player or img["boss_" .. key]
        if id then
          if key == "hotaru" then
            blitk(id, cast_x + 6, y + 8, ((frame // 8) % 2) * 20, 0, 20, 25)
          else
            blitk(id, cast_x, y, ((frame // 8) % 2) * 32, 0, 32, 40)
          end
        end
        text(cast_x + 40, y + 6, (DATA.titles[key] or "自機・灯籠守りの少女"), C.DIM)
        text(cast_x + 40, y + 20, DATA.names[key] or key, C.WHITE)
      end
      y = y + 46
    else
      if y > -20 and y < H then
        local col = k == "t" and C.JADE or (k == "h" and C.GOLD or C.TXT)
        ctext(cx, y, l[2], col)
      end
      y = y + (k == "t" and 20 or 16)
    end
  end
end

function F.theend_step()
  S.end_t = S.end_t + 1
  if S.end_t > 60 and (edge[B_FAR] or edge[B_NEAR] or edge[B_OPR] or edge[B_OPL]) then
    F.record_hiscore()
    F.go_title()
  end
end

function F.draw_theend()
  F.draw_sky()
  F.draw_lanterns(frame, 24, 40, 200)
  if img.logo then blitk(img.logo, 88, 40, 0, 0, 144, 40) end
  ctext(160, 100, "THE END", C.GOLD)
  ctext(160, 124, "Thank you for playing!", C.TXT)
  ctext(160, 150, DIFF_NAME[diff] .. "  SCORE " .. score, C.WHITE)
  ctext(160, 166, "CONTINUE " .. continues, C.DIM)
  if S.end_t > 60 and (frame // 15) % 2 == 0 then ctext(160, 200, "PRESS FAR", C.DIM) end
end

-- ---------------------------------------------------------------------------
-- ポーズ
-- ---------------------------------------------------------------------------
function F.pause_step()
  if edge[B_UP] then S.pause_cur = (S.pause_cur + 1) % 3 + 1 end
  if edge[B_DOWN] then S.pause_cur = S.pause_cur % 3 + 1 end
  if edge[B_NEAR] or edge[B_OPL] then S.paused = false end
  if edge[B_FAR] or edge[B_OPR] then
    S.paused = false
    if S.pause_cur == 2 then
      F.start_game()
    elseif S.pause_cur == 3 then
      F.record_hiscore()
      F.go_title()
    end
  end
end

-- ---------------------------------------------------------------------------
-- 1 ステップ（1/30 秒）
-- ---------------------------------------------------------------------------
function F.step()
  frame = frame + 1
  if se_cd > 0 then se_cd = se_cd - 1 end
  if tone_cd > 0 then tone_cd = tone_cd - 1 end
  if scene == "title" then
    return F.title_step()
  elseif scene == "game" then
    if S.paused then
      F.pause_step()
    elseif edge[B_OPL] and not talk and gphase ~= "continue" and gphase ~= "clear" then
      S.paused = true
      S.pause_cur = 1
    else
      F.game_step()
    end
  elseif scene == "ending" then
    F.ending_step()
  elseif scene == "staff" then
    F.staff_step()
  elseif scene == "theend" then
    F.theend_step()
  end
  return false
end

-- ---------------------------------------------------------------------------
-- エントリ
-- ---------------------------------------------------------------------------
function game_init()
  math.randomseed(M.time_ms())
  -- ヒープ予算が小さいので GC を早めに回す（既定 pause 200% だと倍まで膨らむ）
  collectgarbage("incremental", 120, 200)
  font_ok = M.load_font("fonts/game_font.bin") == true
  F.load_img("player", "img/player.bin", 40, 25)
  F.load_img("fairy", "img/fairy.bin", 128, 16)
  F.load_img("mob", "img/mob.bin", 56, 16)
  F.load_img("bullets", "img/bullets.bin", 128, 40)
  F.load_img("misc", "img/misc.bin", 64, 18)
  F.load_save()
  M.set_volume(0.8)
  F.go_title()
  if TEST and TEST.init then TEST.init() end
end

function game_update(dt)
  -- 操作中の画面だけ入力を間引く（メニューでは全ボタン読む）
  F.service_bgm(dt)
  acc_ms = acc_ms + dt
  if acc_ms > STEP_MS * 3 then acc_ms = STEP_MS * 3 end
  if acc_ms < STEP_MS then return false end   -- このフレームはロジックを進めない
  local in_play = scene == "game" and not S.paused and gphase ~= "continue"
  F.poll_input(not in_play)
  if TEST and TEST.input then TEST.input(btn, edge) end
  local steps = 0
  while acc_ms >= STEP_MS and steps < 2 do
    acc_ms = acc_ms - STEP_MS
    steps = steps + 1
    if F.step() then return true end
    -- エッジは 1 ステップでのみ有効
    for i = 0, 7 do edge[i] = false end
  end
  return false
end

function game_draw()
  if scene == "title" then
    F.draw_title()
  elseif scene == "game" then
    F.draw_game()
  elseif scene == "ending" then
    F.draw_ending()
  elseif scene == "staff" then
    F.draw_staff()
  else
    F.draw_theend()
  end
end

-- PC 検証用フック（実機では未使用）
if TEST then
  TEST.api = {
    state = function() return scene, gphase, stage, nb, ns, ni, #enemies, boss and boss.ph or 0, boss and boss.hp or 0 end,
    set = function(k, v)
      if k == "inv" then p_inv = v elseif k == "stage" then F.begin_stage(v)
      elseif k == "power" then power = v elseif k == "diff" then diff = v
      elseif k == "gtime" then gtime = v end
    end,
    start = function(d) diff = d or 2; F.start_game() end,
    kill_boss_phase = function() if boss and boss.active then boss.hp = 0 end end,
    score = function() return score end,
    bpos = function() if boss then return boss.x, boss.y, boss.hp end return nil end,
    ppos = function() return px, py, power, lives, bombs end,
    bullets = function() return nb, bx, by, bvx, bvy, bt, BHR2 end,
    ens = function() return enemies end,
    ctl = function() return focus, S.paused, talk ~= nil and talk.i or 0, bomb_t, bombs, ns, gphase end,
  }
end
