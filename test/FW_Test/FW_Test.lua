-- ============================================================================
-- FW_Test : 本体ファームウェア変更の確認用
--   1: LCD 転送を 16bit 直接 DMA に（CPU が止まっても転送が進む）
--   2: フレーム末の LCD 転送完了待ちを廃止（次フレームの処理と重ねる）
--   6: 帯ハッシュ 1 パス化 / 7: BGM まとめ読み ＋ BGM 不具合修正（補間・終了・順序・途切れ）
-- SD: /games/FW_Test/FW_Test.lua   （リポジトリでは test/FW_Test/）
--
-- 旧ファームと新ファームの両方で同じ自動ベンチマークを実行すると、
-- 結果が save.dat に残り「結果比較」画面で前回（旧）と今回（新）を並べて見られる。
--
-- 項目
--   1 描画スイープ     … 11 種類の描画命令が 12 本の帯をまたいで動く。残像・欠けが無ければ OK（変更 6）
--   2 帯スキップ FPS    … 変化する帯の数を変えて FPS を測る（変更 6 で dirty 判定が壊れていないか）
--   3 命令多め・変化小  … 大量の静止命令＋1 帯だけ変化。帯ハッシュ計算の速さが FPS に出る（変更 6）
--   4 BGM 形式テスト    … 各サンプルレート・チャンネル・壊れ気味 WAV を再生して耳で確認（変更 7）
--   5 BGM 負荷測定      … BGM 無し／各形式で FPS を比べ、ストリーム処理の CPU 負荷を出す（変更 7）
--   6 自動ベンチマーク  … 2・3・5 をまとめて実行して保存（約 70 秒）
--   7 結果比較          … 前回と今回の測定値を並べて表示
--   8 おわる
--   ほかに「0 色・表示チェック」「BGM途切れテスト」「エラー表示テスト」あり
--
-- 操作: 上下で選択 / OP_RIGHT 決定 / OP_LEFT 戻る（測定中は中断）
-- ============================================================================

local M = machine
local W, H = M.width(), M.height()
local text = M.text
local fill_rect = M.fill_rect
local rgb = M.rgb

local B_RIGHT, B_UP, B_LEFT, B_DOWN = 0, 1, 2, 3
local B_OPL, B_OPR, B_FAR, B_NEAR = 4, 5, 6, 7

local C_BG = rgb(12, 16, 24)
local C_TX = rgb(230, 235, 240)
local C_DIM = rgb(130, 140, 150)
local C_OK = rgb(120, 230, 140)
local C_WARN = rgb(255, 200, 90)
local C_SEL = rgb(90, 200, 255)
local C_BAR = rgb(40, 50, 70)

local font_ok = false
local LINE = 12

-- ---------------------------------------------------------------------------
-- 入力（測定中は OP_LEFT だけ読む: pressed() は 1 回 約 0.4ms の I2C 読み出し）
-- ---------------------------------------------------------------------------
local btn, prv = {}, {}
for i = 0, 7 do btn[i] = false; prv[i] = false end
local function poll(only_back)
  for i = 0, 7 do prv[i] = btn[i] end
  if only_back then
    btn[B_OPL] = M.pressed(B_OPL)
  else
    for i = 0, 7 do btn[i] = M.pressed(i) end
  end
end
local function hit(i) return btn[i] and not prv[i] end

-- ---------------------------------------------------------------------------
-- 結果の保存（前回 = 直前の起動時の結果、今回 = この起動での結果）
-- ---------------------------------------------------------------------------
local SAVE = "save.dat"
local prev_run = {}
local cur_run = {}
local run_no = 1

local function load_results()
  if M.file_exists(SAVE) then
    local d = M.load_data(SAVE)
    if type(d) == "table" and type(d.cur) == "table" then
      -- 直前の起動で 1 つでも測っていれば、それを「前回」とする
      local has = false
      for _ in pairs(d.cur) do has = true break end
      if has then
        prev_run = d.cur
      elseif type(d.prev) == "table" then
        prev_run = d.prev
      end
      run_no = (tonumber(d.run_no) or 0) + 1
    end
  end
end

local function save_results()
  M.save_data(SAVE, { prev = prev_run, cur = cur_run, run_no = run_no })
end

local function record(key, v)
  cur_run[key] = v
  save_results()
end

-- ---------------------------------------------------------------------------
-- FPS 測定（ウォームアップ後、指定 ms の間のフレーム数を数える）
-- ---------------------------------------------------------------------------
local meas = nil  -- { warm, len, t0, frames, done, fps, ms }
local function meas_start(warm_ms, len_ms)
  meas = { warm = warm_ms or 500, len = len_ms or 3000, t0 = M.time_ms(), frames = 0, done = false }
end
local function meas_tick()
  if not meas or meas.done then return end
  local now = M.time_ms()
  local e = now - meas.t0
  if e < meas.warm then return end
  if not meas.s0 then meas.s0 = now; meas.frames = 0 return end
  meas.frames = meas.frames + 1
  local m = now - meas.s0
  if m >= meas.len then
    meas.done = true
    meas.fps = meas.frames * 1000 / m
    meas.ms = m / meas.frames
  end
end
local function fmt1(v) return v and string.format("%.1f", v) or "-" end
local function fmt2(v) return v and string.format("%.2f", v) or "-" end

-- ---------------------------------------------------------------------------
-- 共通描画
-- ---------------------------------------------------------------------------
local function header(title)
  fill_rect(0, 0, W, 14, C_BAR)
  text(4, 1, title, C_TX)
end

local function footer(s)
  fill_rect(0, H - 14, W, 14, C_BAR)
  text(4, H - 13, s, C_DIM)
end

local function lines(x, y, t, col)
  for i = 1, #t do text(x, y + (i - 1) * LINE, t[i], col or C_TX) end
end

-- ---------------------------------------------------------------------------
-- 画像（テスト用 16x16、マゼンタ透過）
-- ---------------------------------------------------------------------------
local spr = nil

-- ---------------------------------------------------------------------------
-- 1. 描画スイープ
-- ---------------------------------------------------------------------------
local sweep = { t = 0, speed = 2, use_clear = false }
local STRIPE = {}
for b = 0, 11 do STRIPE[b] = (b % 2 == 0) and rgb(24, 30, 44) or rgb(34, 40, 58) end
local SWEEP_NAMES = { "rect", "rects", "alpha", "line", "circ", "fcirc", "text", "img", "sub", "x2", "clip" }

local function sweep_y(i, t)
  -- 画面外（上 -30 〜 下 +30）まで動かして境界・はみ出しも確認
  return (t * sweep.speed + i * 23) % 300 - 30
end

local function draw_sweep()
  if sweep.use_clear then
    M.clear(C_BG)          -- Clear 命令（全帯に影響）の経路も確認
  end
  for b = 0, 11 do
    fill_rect(0, b * 20, W, 20, STRIPE[b])
    text(2, b * 20 + 4, "B" .. b, C_DIM)
  end
  local t = sweep.t
  for i = 1, 11 do
    local x = 18 + (i - 1) * 27
    local y = sweep_y(i, t)
    local c = rgb(80 + i * 15, 220 - i * 12, 120 + i * 10)
    if i == 1 then
      fill_rect(x, y, 18, 10, c)
    elseif i == 2 then
      M.fill_rects({ { x, y, 8, 22, c }, { x + 10, y + 12, 8, 22, C_WARN } })
    elseif i == 3 then
      M.fill_rect_alpha(x - 2, y, 22, 26, rgb(255, 255, 255), 140)
    elseif i == 4 then
      M.draw_line(x, y + 30, x + 18, y, c)            -- y0 > y1
    elseif i == 5 then
      M.draw_circle(x + 9, y + 9, 10, c)
    elseif i == 6 then
      M.fill_circle(x + 9, y + 9, 8, c)
    elseif i == 7 then
      text(x, y, "AB\nCD", C_WARN)                    -- 2 行（24px）
    elseif i == 8 then
      if spr then M.draw_image_keyed(spr, x, y) end
    elseif i == 9 then
      if spr then M.draw_image(spr, x, y, 4, 4, 8, 8) end
    elseif i == 10 then
      if spr then M.draw_image_affine(spr, 2, 0, x - 6, 0, 2, y) end   -- 整数 2 倍（録画対応）
    else
      -- 画面上端・下端をまたぐ大きな矩形（y<0 や y+h>240 のクリップ）
      fill_rect(x, y - 40, 14, 80, rgb(200, 90, 200))
    end
  end
  header("1 描画スイープ  速度:" .. sweep.speed .. (sweep.use_clear and "  clear使用" or ""))
  footer("残像・欠けが無ければOK  上下:速度 OP-R:clear切替")
  -- 名前は 2 段に互い違いで表示（重なり防止）
  for i = 1, 11 do
    text(12 + (i - 1) * 27, (i % 2 == 1) and H - 38 or H - 26, SWEEP_NAMES[i], C_DIM)
  end
end

local function update_sweep()
  sweep.t = sweep.t + 1
  if hit(B_UP) then sweep.speed = math.min(8, sweep.speed + 1) end
  if hit(B_DOWN) then sweep.speed = math.max(0, sweep.speed - 1) end
  if hit(B_OPR) then sweep.use_clear = not sweep.use_clear end
end

-- ---------------------------------------------------------------------------
-- 2. 帯スキップ FPS（変化する帯の数を切り替え）
-- ---------------------------------------------------------------------------
local BAND_MODES = { "none", "one", "half", "all" }
local BAND_LABEL = { none = "変化なし", one = "1帯だけ変化", half = "6帯変化", all = "全12帯変化" }
local bandfps = { mode = 1, t = 0, auto = false }

local function draw_bandfps()
  local mode = BAND_MODES[bandfps.mode]
  for b = 0, 11 do fill_rect(0, b * 20, W, 20, STRIPE[b]) end
  local t = bandfps.t
  -- 変化させる帯（帯 1〜10 の中）
  if mode == "one" then
    fill_rect(160 + (t % 60), 5 * 20 + 6, 8, 8, C_WARN)
  elseif mode == "half" then
    for b = 1, 11, 2 do fill_rect(100 + (t % 100), b * 20 + 6, 8, 8, C_WARN) end
  elseif mode == "all" then
    for b = 0, 11 do fill_rect(100 + ((t + b * 7) % 100), b * 20 + 6, 8, 8, C_WARN) end
  end
  -- 表示は帯 1〜4 に固定（測定結果が変わった時だけ再描画される）
  text(8, 22, "2 帯スキップFPS: " .. BAND_LABEL[mode], C_TX)
  if meas and meas.done then
    text(8, 46, "FPS " .. fmt1(meas.fps) .. "   " .. fmt2(meas.ms) .. " ms/frame", C_OK)
  else
    text(8, 46, "測定中...", C_DIM)
  end
  local key = "band_" .. mode
  text(8, 70, "前回 " .. fmt1(prev_run[key]) .. " fps", C_DIM)
  if not bandfps.auto then
    text(8, 222, "上下:モード切替  OP-L:戻る", C_DIM)
  end
end

local function update_bandfps()
  bandfps.t = bandfps.t + 1
  meas_tick()
  if meas and meas.done and not meas.saved and bandfps.auto then
    meas.saved = true
    record("band_" .. BAND_MODES[bandfps.mode], meas.fps)   -- 自動ベンチ時のみ保存
  end
end

-- ---------------------------------------------------------------------------
-- 3. 命令多め・変化小（帯ハッシュの計算量が効く）
-- ---------------------------------------------------------------------------
local HEAVY_COUNTS = { 300, 600, 900 }
local heavy = { idx = 1, t = 0, auto = false, cols = {} }
for i = 1, 900 do
  heavy.cols[i] = rgb((i * 37) % 200 + 30, (i * 71) % 200 + 30, (i * 13) % 200 + 30)
end

local function draw_heavy()
  local n = HEAVY_COUNTS[heavy.idx]
  local cols = heavy.cols
  -- 静止した小矩形を n 個（fill_rect 1 個 = 録画 11 バイト。900 個で約 10KB）
  fill_rect(0, 0, W, H, C_BG)   -- 前の画面の残りを消す（静止なので転送はされない）
  local k = 0
  for y = 16, 223, 7 do
    for x = 0, 319, 10 do
      k = k + 1
      if k > n then break end
      fill_rect(x, y, 9, 6, cols[k])
    end
    if k > n then break end
  end
  -- 帯 6 だけ毎フレーム変化
  fill_rect(20 + (heavy.t % 280), 124, 10, 10, C_WARN)
  fill_rect(0, 0, W, 14, C_BAR)
  local s = "3 命令" .. n .. "個 "
  if meas and meas.done then
    s = s .. fmt1(meas.fps) .. "fps " .. fmt2(meas.ms) .. "ms"
  else
    s = s .. "測定中..."
  end
  text(4, 1, s, C_TX)
  text(220, 1, "前回 " .. fmt1(prev_run["heavy" .. n]), C_DIM)
end

local function update_heavy()
  heavy.t = heavy.t + 1
  meas_tick()
  if meas and meas.done and not meas.saved and heavy.auto then
    meas.saved = true
    record("heavy" .. HEAVY_COUNTS[heavy.idx], meas.fps)   -- 自動ベンチ時のみ保存
  end
end

-- ---------------------------------------------------------------------------
-- 4. BGM 形式テスト（耳で確認）
-- ---------------------------------------------------------------------------
local FORMATS = {
  { "t44s.wav", "44100Hz ステレオ" },
  { "t44m.wav", "44100Hz モノラル" },
  { "t22s.wav", "22050Hz ステレオ" },
  { "t22m.wav", "22050Hz モノラル" },
  { "t11m.wav", "11025Hz モノラル" },
  { "t48s.wav", "48000Hz ステレオ(縮小)" },
  { "t32m.wav", "32000Hz モノラル" },
  { "t8m.wav", "8000Hz モノラル" },
  { "t44odd.wav", "44100Hz 端数バイト付" },
  { "t22list.wav", "22050Hz LISTチャンク付" },
  { "t44cut.wav", "44100Hz 途中で切れ(3.5秒)" },
}
local fmt = { cur = 1, playing = false, t0 = 0, err = nil }

local function play_format(i)
  M.stop_sound()
  local ok, err = M.play_wav("audio/" .. FORMATS[i][1])
  fmt.playing = ok == true
  fmt.err = (not ok) and tostring(err) or nil
  fmt.t0 = M.time_ms()
end

local function draw_formats()
  M.clear(C_BG)
  header("4 BGM形式テスト（全ファイル同じ内容・4秒）")
  for i = 1, #FORMATS do
    local sel = i == fmt.cur
    text(8, 16 + (i - 1) * LINE, (sel and "▶" or " ") .. FORMATS[i][2], sel and C_SEL or C_TX)
  end
  local y = 16 + #FORMATS * LINE + 4
  if fmt.playing then
    local e = (M.time_ms() - fmt.t0) / 1000
    local beat = math.floor(e * 2) % 2 == 0
    fill_rect(250, 20, 60, 30, beat and C_WARN or C_BAR)
    text(252, 56, string.format("%.1f 秒", e), C_TX)
  end
  if fmt.err then text(8, y, "エラー: " .. fmt.err, C_WARN) end
  lines(8, y + 12, {
    "0-1秒: 左だけ440Hz / 1-2秒: 右だけ440Hz",
    "0.5秒ごとにクリック（右上の点滅と一致）",
    "3.8秒で高い音、4秒で止まる",
    "音の高さが全て同じ・プツプツしなければOK",
  }, C_DIM)
  footer("上下:選択 OP-R:再生 OP-L:戻る")
end

local function update_formats()
  if hit(B_UP) then fmt.cur = (fmt.cur - 2) % #FORMATS + 1 end
  if hit(B_DOWN) then fmt.cur = fmt.cur % #FORMATS + 1 end
  if hit(B_OPR) then play_format(fmt.cur) end
end

-- ---------------------------------------------------------------------------
-- 5. BGM 負荷測定（静止画面で FPS を比較）
-- ---------------------------------------------------------------------------
local LOAD_STEPS = {
  { key = "bgm_off", file = nil, label = "BGMなし" },
  { key = "bgm_44s", file = "long44s.wav", label = "44100Hz ステレオ" },
  { key = "bgm_22m", file = "long22m.wav", label = "22050Hz モノラル" },
  { key = "bgm_48s", file = "long48s.wav", label = "48000Hz ステレオ" },
}
local load = { step = 1, auto = false, results = {} }

local function load_begin(i)
  load.step = i
  M.stop_sound()
  local s = LOAD_STEPS[i]
  if s.file then M.play_wav("audio/" .. s.file) end
  meas_start(700, 3000)
end

local function draw_load()
  -- 静止画面（測定値が変わる時以外は転送されない）
  fill_rect(0, 0, W, H, C_BG)
  header("5 BGM負荷測定（静止画面のFPS）")
  local off = load.results.bgm_off
  for i = 1, #LOAD_STEPS do
    local s = LOAD_STEPS[i]
    local v = load.results[s.key]
    local y = 24 + (i - 1) * 30
    text(8, y, s.label, i == load.step and C_SEL or C_TX)
    local line = "今回 " .. fmt1(v) .. " fps"
    if v and off and s.key ~= "bgm_off" then
      line = line .. string.format("  負荷 %.1f%%", (1 - v / off) * 100)
    end
    text(20, y + 12, line, v and C_OK or C_DIM)
    local pv, poff = prev_run[s.key], prev_run.bgm_off
    local pl = "前回 " .. fmt1(pv) .. " fps"
    if pv and poff and s.key ~= "bgm_off" then
      pl = pl .. string.format("  負荷 %.1f%%", (1 - pv / poff) * 100)
    end
    text(170, y + 12, pl, C_DIM)
  end
  footer(load.step <= #LOAD_STEPS and "測定中… OP-L:中断" or "完了  OP-L:戻る")
end

local function update_load()
  meas_tick()
  if meas and meas.done and load.step <= #LOAD_STEPS then
    local s = LOAD_STEPS[load.step]
    load.results[s.key] = meas.fps
    record(s.key, meas.fps)
    if load.step < #LOAD_STEPS then
      load_begin(load.step + 1)
    else
      load.step = #LOAD_STEPS + 1
      M.stop_sound()
      meas = nil
    end
  end
end

-- ---------------------------------------------------------------------------
-- 6. 自動ベンチマーク（2 → 3 → 5 を順に）
-- ---------------------------------------------------------------------------
local auto = nil   -- { list, i }
local page = "menu"

local function auto_next()
  auto.i = auto.i + 1
  local item = auto.list[auto.i]
  if not item then
    auto = nil
    bandfps.auto, heavy.auto, load.auto = false, false, false
    page = "results"
    return
  end
  page = item.page
  if item.page == "bandfps" then
    bandfps.mode, bandfps.auto = item.mode, true
    meas_start(500, 3000)
  elseif item.page == "heavy" then
    heavy.idx, heavy.auto = item.idx, true
    meas_start(500, 3000)
  elseif item.page == "load" then
    load.auto = true
    load.results = {}
    load_begin(1)
  end
end

local function auto_start()
  cur_run = {}
  auto = { i = 0, list = {
    { page = "bandfps", mode = 1 }, { page = "bandfps", mode = 2 },
    { page = "bandfps", mode = 3 }, { page = "bandfps", mode = 4 },
    { page = "heavy", idx = 1 }, { page = "heavy", idx = 2 }, { page = "heavy", idx = 3 },
    { page = "load" },
  } }
  auto_next()
end

-- ---------------------------------------------------------------------------
-- 色・表示チェック（変更 1: 16bit 直接 DMA のバイト順が正しいか）
-- ---------------------------------------------------------------------------
local COLOR_BARS = {
  { "赤", 255, 0, 0 }, { "緑", 0, 255, 0 }, { "青", 0, 0, 255 }, { "白", 255, 255, 255 },
  { "黄", 255, 255, 0 }, { "水色", 0, 255, 255 }, { "紫", 255, 0, 255 }, { "灰", 128, 128, 128 },
}

local function draw_colors()
  M.clear(C_BG)
  header("0 色・表示チェック")
  for i, c in ipairs(COLOR_BARS) do
    local x = 8 + (i - 1) * 38
    fill_rect(x, 20, 34, 70, rgb(c[2], c[3], c[4]))
    text(x + 2, 94, c[1], C_TX)
  end
  -- 階調（赤・緑・青・灰）
  for k = 0, 31 do
    local v = k * 8
    fill_rect(8 + k * 9, 116, 9, 14, rgb(v, 0, 0))
    fill_rect(8 + k * 9, 132, 9, 14, rgb(0, v, 0))
    fill_rect(8 + k * 9, 148, 9, 14, rgb(0, 0, v))
    fill_rect(8 + k * 9, 164, 9, 14, rgb(v, v, v))
  end
  if spr then
    M.draw_image_keyed(spr, 250, 190)
    text(8, 190, "画像: 左上赤・右上緑・左下青・右下黄", C_DIM)
  end
  footer("名前どおりの色・左→右に明るくなればOK")
end

-- ---------------------------------------------------------------------------
-- BGM 途切れテスト（重い描画をしながら BGM を鳴らす）
-- ---------------------------------------------------------------------------
local gap = { t = 0 }

local function draw_gap()
  -- 全帯が毎フレーム変化する重い画面（翠灯夜行の弾幕に近い負荷）
  fill_rect(0, 0, W, H, C_BG)
  local t = gap.t
  for i = 0, 179 do
    local x = (i * 37 + t * (1 + i % 3)) % 312
    local y = (i * 53 + t * (1 + i % 2)) % 226 + 14
    fill_rect(x, y, 6, 6, heavy.cols[i + 1])
  end
  header("BGM途切れテスト")
  text(8, 20, "44.1kHz ステレオの BGM を再生中", C_TX)
  text(8, 34, "プツプツ・途切れ・ザラつきが無ければOK", C_DIM)
  footer("OP-L:戻る")
end

local function update_gap()
  gap.t = gap.t + 1
end

-- ---------------------------------------------------------------------------
-- 7. 結果比較
-- ---------------------------------------------------------------------------
local RESULT_ROWS = {
  { "band_none", "帯: 変化なし" }, { "band_one", "帯: 1帯変化" },
  { "band_half", "帯: 6帯変化" }, { "band_all", "帯: 全帯変化" },
  { "heavy300", "命令300個" }, { "heavy600", "命令600個" }, { "heavy900", "命令900個" },
  { "bgm_off", "BGMなし" }, { "bgm_44s", "BGM 44k ST" }, { "bgm_22m", "BGM 22k MO" },
  { "bgm_48s", "BGM 48k ST" },
}

local function draw_results()
  M.clear(C_BG)
  header("7 結果比較（FPS・大きいほど速い）  #" .. run_no)
  text(8, 16, "項目", C_DIM)
  text(130, 16, "前回", C_DIM)
  text(190, 16, "今回", C_DIM)
  text(250, 16, "変化", C_DIM)
  for i, r in ipairs(RESULT_ROWS) do
    local y = 16 + i * LINE
    local p, c = prev_run[r[1]], cur_run[r[1]]
    text(8, y, r[2], C_TX)
    text(130, y, fmt1(p), C_DIM)
    text(190, y, fmt1(c), C_TX)
    if p and c and p > 0 then
      local d = (c / p - 1) * 100
      text(250, y, string.format("%+.1f%%", d), d >= -1 and C_OK or C_WARN)
    end
  end
  local y = 16 + (#RESULT_ROWS + 1) * LINE + 4
  -- BGM 負荷（%）の比較
  local function loadpct(run, key)
    if run[key] and run.bgm_off then return (1 - run[key] / run.bgm_off) * 100 end
  end
  local s = "BGM負荷% 44kST 前回" .. fmt1(loadpct(prev_run, "bgm_44s")) .. " 今回" .. fmt1(loadpct(cur_run, "bgm_44s"))
  text(8, y, s, C_TX)
  s = "        22kMO 前回" .. fmt1(loadpct(prev_run, "bgm_22m")) .. " 今回" .. fmt1(loadpct(cur_run, "bgm_22m"))
  text(8, y + LINE, s, C_TX)
  footer("前回=前の起動時の結果  OP-L:戻る")
end

-- ---------------------------------------------------------------------------
-- メニュー
-- ---------------------------------------------------------------------------
local MENU = {
  { "0 色・表示チェック（目視）", "colors" },
  { "1 描画スイープ（目視）", "sweep" },
  { "2 帯スキップFPS", "bandfps" },
  { "3 命令多め・変化小", "heavy" },
  { "4 BGM形式テスト（耳で確認）", "formats" },
  { "5 BGM負荷測定", "load" },
  { "  BGM途切れテスト（耳で確認）", "gap" },
  { "6 自動ベンチマーク（約70秒）", "auto" },
  { "7 結果比較", "results" },
  { "  エラー表示テスト（終了します）", "error" },
  { "8 おわる", "exit" },
}
local menu_cur = 1

local function draw_menu()
  M.clear(C_BG)
  header("FW_Test  本体変更の確認ソフト  #" .. run_no)
  for i = 1, #MENU do
    local sel = i == menu_cur
    text(16, 18 + (i - 1) * 14, (sel and "▶ " or "  ") .. MENU[i][1], sel and C_SEL or C_TX)
  end
  lines(16, 176, {
    "旧ファーム→新ファームの順に「6 自動ベンチマーク」",
    "を実行すると「7 結果比較」で差が見られます。",
    font_ok and "" or "(fonts/game_font.bin が無いため英字表示)",
  }, C_DIM)
  footer("上下:選択 OP-R:決定")
end

local function enter(target)
  if target == "exit" then return true end
  if target == "auto" then auto_start() return false end
  page = target
  if target == "bandfps" then
    bandfps.auto = false
    meas_start(500, 3000)
  elseif target == "heavy" then
    heavy.auto = false
    meas_start(500, 3000)
  elseif target == "load" then
    load.auto = false
    load.results = {}
    load_begin(1)
  elseif target == "formats" then
    fmt.playing = false
  elseif target == "gap" then
    gap.t = 0
    M.stop_sound()
    M.play_wav("audio/long44s.wav")
  elseif target == "error" then
    page = "error"
  end
  return false
end

local function back_to_menu()
  M.stop_sound()
  meas = nil
  auto = nil
  bandfps.auto, heavy.auto, load.auto = false, false, false
  page = "menu"
end

-- ---------------------------------------------------------------------------
-- エントリ
-- ---------------------------------------------------------------------------
function game_init()
  font_ok = M.load_font("fonts/game_font.bin") == true
  if not font_ok then LINE = 10 end
  spr = M.load_image("img/test16.bin", 16, 16)
  load_results()
  save_results()
end

function game_update(dt)
  -- 自動ベンチと BGM 負荷測定中は OP_LEFT だけ読む（測定条件をそろえる）
  poll(auto ~= nil or page == "load")

  if page == "menu" then
    if hit(B_UP) then menu_cur = (menu_cur - 2) % #MENU + 1 end
    if hit(B_DOWN) then menu_cur = menu_cur % #MENU + 1 end
    if hit(B_OPR) then return enter(MENU[menu_cur][2]) end
    return false
  end

  if hit(B_OPL) then
    back_to_menu()
    return false
  end

  if page == "sweep" then
    update_sweep()
  elseif page == "bandfps" then
    update_bandfps()
    if not bandfps.auto then
      if hit(B_UP) then bandfps.mode = (bandfps.mode - 2) % #BAND_MODES + 1; meas_start(500, 3000) end
      if hit(B_DOWN) then bandfps.mode = bandfps.mode % #BAND_MODES + 1; meas_start(500, 3000) end
    elseif meas and meas.done then
      auto_next()
    end
  elseif page == "heavy" then
    update_heavy()
    if not heavy.auto then
      if hit(B_UP) then heavy.idx = heavy.idx % #HEAVY_COUNTS + 1; meas_start(500, 3000) end
      if hit(B_DOWN) then heavy.idx = (heavy.idx - 2) % #HEAVY_COUNTS + 1; meas_start(500, 3000) end
    elseif meas and meas.done then
      auto_next()
    end
  elseif page == "formats" then
    update_formats()
  elseif page == "gap" then
    update_gap()
  elseif page == "error" then
    -- 描画中の LCD 転送と、本体のエラー表示（LCD 直接描画）が重なっても
    -- 画面が乱れず「Game start failed」画面→メニューに戻れることを確認する
    error("FW_Test: 意図的なエラー（表示確認用）")
  elseif page == "load" then
    update_load()
    if load.auto and load.step > #LOAD_STEPS then auto_next() end
  end
  return false
end

function game_draw()
  if page == "menu" then
    draw_menu()
  elseif page == "sweep" then
    draw_sweep()
  elseif page == "bandfps" then
    draw_bandfps()
  elseif page == "heavy" then
    draw_heavy()
  elseif page == "formats" then
    draw_formats()
  elseif page == "load" then
    draw_load()
  elseif page == "colors" then
    draw_colors()
  elseif page == "gap" then
    draw_gap()
  elseif page == "error" then
    -- 全帯を変化させて転送中の状態を作る
    fill_rect(0, 0, W, H, rgb((M.time_ms() // 3) % 256, 40, 80))
    text(8, 100, "エラーを発生させます…", C_TX)
  else
    draw_results()
  end
end
