-- ============================================================================
-- ファイル: visual_novel.lua
-- 簡易ビジュアルノベル（Suityouka Game Machine）
-- ============================================================================
--
-- SD カード配置例（フォルダごとコピー）:
--   /visual_novel/visual_novel.lua
--   /visual_novel/fonts/game_font.bin
--   /visual_novel/scenario.lua
--   /visual_novel/assets.lua
--   /visual_novel/images/bg/*.bin
--   /visual_novel/images/chars/*.bin
--
-- 起動: ファイルエクスプローラから visual_novel.lua を選択。
-- 相対パスは visual_novel.lua があるフォルダ基準で解決されます。
--
-- ============================================================================
-- [SCENARIO] シナリオは scenario.lua から game_init 時に読み込む
--            （起動時メモリ節約。編集は scenario.lua を変更）
-- ============================================================================

local SCENES = nil
local ASSETS = { bg = {}, char = {} }

local SCENARIO_PATHS = {
  "scenario.lua",
}

local ASSET_PATHS = {
  "assets.lua",
}

local function load_scenes()
  for i = 1, #SCENARIO_PATHS do
    local data = machine.load_return(SCENARIO_PATHS[i])
    if type(data) == "table" then
      SCENES = data
      print("Scenario OK: " .. SCENARIO_PATHS[i])
      return true
    end
  end
  SCENES = {
    {
      id = "title",
      bg = { r = 15, g = 20, b = 45 },
      name = "",
      lines = {
        "scenario.lua not found",
        "Place scenario.lua next to visual_novel.lua",
      },
      next = "title",
    },
  }
  print("Scenario NG: using fallback")
  return false
end

local function load_assets()
  for i = 1, #ASSET_PATHS do
    local data = machine.load_return(ASSET_PATHS[i])
    if type(data) == "table" then
      ASSETS = data
      print("Assets OK: " .. ASSET_PATHS[i])
      return true
    end
  end
  print("Assets NG: assets.lua not found (bg_image / character disabled)")
  ASSETS = { bg = {}, char = {} }
  return false
end

-- ============================================================================
-- [CONFIG] 表示・入力の設定
-- ============================================================================

local W = machine.width()
local H = machine.height()

-- ボタン番号（config.hpp / LUA_API.md 参照）
local BTN_RIGHT    = 0
local BTN_UP       = 1
local BTN_LEFT     = 2
local BTN_DOWN     = 3
local BTN_OP_LEFT  = 4
local BTN_OP_RIGHT = 5
local BTN_FAR      = 6
local BTN_NEAR     = 7

-- 美咲フォントサブセット（generate_font.py で生成）
local FONT_PATHS = {
  "fonts/game_font.bin",
}

-- テキストウィンドウ（LINE_H / CHAR_W は game_init で font_height から更新）
local TEXT_BOX_H   = 72
local TEXT_BOX_Y   = H - TEXT_BOX_H
local NAME_PLATE_H = 12
LINE_H = 10
local MARGIN_X     = 8
CHAR_W = 8

-- 色
local COL_BOX      = machine.rgb(10, 10, 25)
local COL_BOX_EDGE = machine.rgb(80, 90, 140)
local COL_NAME     = machine.rgb(255, 220, 120)
local COL_TEXT     = machine.rgb(240, 240, 240)
local COL_HINT     = machine.rgb(160, 160, 180)
local COL_CHOICE   = machine.rgb(200, 210, 255)
local COL_CURSOR   = machine.rgb(255, 255, 100)

-- 終了: FAR をこの時間(ms)以上押し続ける
local EXIT_HOLD_MS = 1500

-- 立ち絵の横位置（assets.lua の char は 128x168 想定）
local CHAR_POS = {
  left   = function(w) return 0 end,
  center = function(w) return math.floor((W - w) / 2) end,
  right  = function(w) return W - w end,
}

-- 透過キー（PNG 変換時のマゼンタと一致）
local KEY_COLOR = 0xF81F

-- ============================================================================
-- [ENGINE] エンジン本体 — 通常は触らなくて OK
-- ============================================================================

local scene_by_id = {}
local state = {}
local loaded_img = { char = nil }
local bg_stream_fail = {}

local function rgb_from_scene(bg)
  if type(bg) ~= "table" then
    return machine.rgb(0, 0, 0)
  end
  return machine.rgb(bg.r or 0, bg.g or 0, bg.b or 0)
end

local function resolve_image_path(rel)
  if not rel or rel == "" then
    return nil
  end
  if rel:sub(1, 1) == "/" then
    return rel
  end
  return "images/" .. rel
end

local function parse_character(spec)
  if not spec or spec == "" or spec == false then
    return nil, "center"
  end
  if type(spec) == "string" then
    return spec, "center"
  end
  if type(spec) == "table" then
    return spec.id or spec.sprite or spec.char, spec.pos or "center"
  end
  return nil, "center"
end

local function get_line_page(scene, line_index)
  local raw = scene.lines and scene.lines[line_index]
  if type(raw) == "table" then
    return {
      text = raw.text or raw.line or "",
      name = raw.name,
      character = raw.character,
      bg_image = raw.bg_image,
    }
  end
  return {
    text = raw or "",
    name = nil,
    character = nil,
    bg_image = nil,
  }
end

local function get_asset_def(category, asset_id)
  if not asset_id or asset_id == "" then
    return nil
  end
  local cat = ASSETS[category]
  if type(cat) ~= "table" then
    return nil
  end
  return cat[asset_id]
end

local function release_loaded(category)
  local entry = loaded_img[category]
  if entry then
    machine.free_image(entry.id)
    loaded_img[category] = nil
  end
end

local function ensure_image(category, asset_id)
  if not asset_id or asset_id == "" then
    return nil
  end
  local cache_key = category .. ":" .. asset_id
  local entry = loaded_img[category]
  if entry and entry.key == cache_key then
    return entry
  end
  release_loaded(category)

  local def = get_asset_def(category, asset_id)
  if not def then
    print("VN: unknown asset: " .. category .. "/" .. tostring(asset_id))
    return nil
  end
  local path = resolve_image_path(def.file)
  if not path then
    return nil
  end
  local id = machine.load_image(path, def.w, def.h)
  if not id then
    print("VN: load_image failed: " .. path)
    return nil
  end
  entry = {
    key = cache_key,
    id = id,
    w = def.w,
    h = def.h,
    keyed = def.keyed == true,
  }
  loaded_img[category] = entry
  return entry
end

local function char_draw_x(entry, pos_name)
  local fn = CHAR_POS[pos_name] or CHAR_POS.center
  return fn(entry.w)
end

local function draw_background(scene, page)
  local bg_id = page.bg_image or scene.bg_image
  if not bg_id or bg_id == "" then
    return false
  end
  local def = get_asset_def("bg", bg_id)
  if not def then
    print("VN: unknown bg: " .. tostring(bg_id))
    return false
  end
  local path = resolve_image_path(def.file)
  if not path then
    return false
  end
  -- 背景は SD からバンド単位でストリーム描画（RAM に全枚載せない）
  if machine.draw_bg_stream(path, 0, 0, def.w, def.h) then
    return true
  end
  if not bg_stream_fail[path] then
    bg_stream_fail[path] = true
    print("VN: draw_bg_stream failed: " .. path)
  end
  return false
end

local function draw_character(scene, page)
  local char_id, pos = parse_character(page.character or scene.character)
  if not char_id or char_id == "" then
    release_loaded("char")
    return
  end
  local img = ensure_image("char", char_id)
  if not img then
    return
  end
  local x = char_draw_x(img, pos)
  local y = TEXT_BOX_Y - img.h
  if y < 0 then
    y = 0
  end
  if img.keyed then
    machine.draw_image_keyed(img.id, x, y, KEY_COLOR)
  else
    machine.draw_image(img.id, x, y)
  end
end

local function build_scene_index()
  scene_by_id = {}
  for i = 1, #SCENES do
    local s = SCENES[i]
    scene_by_id[s.id] = s
  end
end

local function get_scene(id)
  return scene_by_id[id]
end

local function confirm_pressed()
  return machine.pressed(BTN_OP_RIGHT) or machine.pressed(BTN_NEAR)
end

local function any_confirm_edge(prev)
  local now = confirm_pressed()
  return now and not prev
end

-- シーンを読み込み、表示位置をリセット
local function enter_scene(id)
  local scene = get_scene(id)
  if not scene then
    print("VN: unknown scene id: " .. tostring(id))
    scene = get_scene("title")
  end
  state.scene_id = scene.id
  state.scene = scene
  state.line_index = 1
  state.mode = "lines"
  state.choice_index = 1
  state.bg_color = rgb_from_scene(scene.bg)
end

local function advance_line()
  local scene = state.scene
  if not scene or not scene.lines then
    return
  end
  if state.line_index < #scene.lines then
    state.line_index = state.line_index + 1
    return
  end
  -- 最後のページを超えた → 分岐 or 次シーン
  if scene.choices and #scene.choices > 0 then
    state.mode = "choice"
    state.choice_index = 1
    return
  end
  if scene.next then
    enter_scene(scene.next)
  end
end

local function confirm_choice()
  local scene = state.scene
  if not scene or not scene.choices then
    return
  end
  local c = scene.choices[state.choice_index]
  if c and c.next then
    enter_scene(c.next)
  end
end

local function update_choice_input(prev_up, prev_down)
  local scene = state.scene
  if not scene or not scene.choices then
    return
  end
  local n = #scene.choices
  if machine.pressed(BTN_UP) and not prev_up then
    state.choice_index = state.choice_index - 1
    if state.choice_index < 1 then
      state.choice_index = n
    end
  end
  if machine.pressed(BTN_DOWN) and not prev_down then
    state.choice_index = state.choice_index + 1
    if state.choice_index > n then
      state.choice_index = 1
    end
  end
end

-- 星の簡易装飾（タイトル・夜空用）
local function draw_stars(t)
  if W <= 0 or TEXT_BOX_Y <= 16 then
    return
  end
  local sky_h = TEXT_BOX_Y - 16
  local count = 24
  for i = 1, count do
    local seed = i * 7919
    local x = math.floor((seed * 3 + t // 40) % W)
    local y = math.floor((seed * 7) % sky_h)
    if (seed + t // 200) % 5 ~= 0 then
      machine.fill_rect(x, y, 2, 2, machine.rgb(220, 220, 255))
    end
  end
end

local function draw_text_box()
  machine.fill_rect(0, TEXT_BOX_Y, W, TEXT_BOX_H, COL_BOX)
  machine.fill_rect(0, TEXT_BOX_Y, W, 1, COL_BOX_EDGE)
  machine.fill_rect(0, H - 1, W, 1, COL_BOX_EDGE)
end

local function draw_name_plate(name)
  if not name or name == "" then
    return
  end
  local plate_w = #name * 8 + 16
  if plate_w > W - 16 then
    plate_w = W - 16
  end
  machine.fill_rect(MARGIN_X, TEXT_BOX_Y - NAME_PLATE_H, plate_w, NAME_PLATE_H,
                    machine.rgb(50, 55, 90))
  machine.text(MARGIN_X + 4, TEXT_BOX_Y - NAME_PLATE_H + 3, name, COL_NAME,
               machine.rgb(50, 55, 90))
end

local function draw_body_text(text)
  machine.text(MARGIN_X, TEXT_BOX_Y + 8, text, COL_TEXT, COL_BOX)
end

local function draw_choice_menu()
  local scene = state.scene
  if not scene or not scene.choices then
    return
  end
  local base_y = TEXT_BOX_Y + 8
  for i = 1, #scene.choices do
    local c = scene.choices[i]
    local prefix = "  "
    local color = COL_CHOICE
    if i == state.choice_index then
      prefix = "> "
      color = COL_CURSOR
    end
    machine.text(MARGIN_X, base_y + (i - 1) * LINE_H, prefix .. c.label, color, COL_BOX)
  end
  machine.text(MARGIN_X, H - 12, "UP/DOWN 選択  A-R 決定", COL_HINT, COL_BOX)
end

local function draw_page_indicator()
  local scene = state.scene
  if not scene or not scene.lines or #scene.lines <= 1 then
    return
  end
  if state.mode ~= "lines" then
    return
  end
  local msg = string.format("%d / %d", state.line_index, #scene.lines)
  machine.text(W - #msg * CHAR_W - 4, TEXT_BOX_Y + TEXT_BOX_H - 12, msg, COL_HINT, COL_BOX)
end

-- ============================================================================
-- ホストが呼ぶコールバック
-- ============================================================================

function game_init()
  -- 日本語表示: SD 上の MISF フォント（美咲サブセット）を読み込む
  local font_ok = false
  for i = 1, #FONT_PATHS do
    local p = FONT_PATHS[i]
    if machine.load_font(p) then
      print("Font OK: " .. p)
      font_ok = true
      break
    end
  end
  if font_ok then
    LINE_H = machine.font_height() + 2
    CHAR_W = 8
  else
    print("Font NG: game_font.bin not found (ASCII fallback)")
  end

  load_assets()
  load_scenes()
  build_scene_index()
  state.scene_id = "title"
  state.scene = nil
  state.line_index = 1
  state.mode = "lines"
  state.choice_index = 1
  state.bg_color = machine.rgb(0, 0, 0)
  state.prev_confirm = false
  state.prev_up = false
  state.prev_down = false
  state.exit_hold = 0
  enter_scene("title")
end

function game_update(dt)
  local prev_c = state.prev_confirm
  local prev_u = state.prev_up
  local prev_d = state.prev_down
  state.prev_confirm = confirm_pressed()
  state.prev_up = machine.pressed(BTN_UP)
  state.prev_down = machine.pressed(BTN_DOWN)

  -- FAR 長押しでゲーム終了
  if machine.pressed(BTN_FAR) then
    state.exit_hold = state.exit_hold + dt
    if state.exit_hold >= EXIT_HOLD_MS then
      return true
    end
  else
    state.exit_hold = 0
  end

  if state.mode == "choice" then
    update_choice_input(prev_u, prev_d)
    if any_confirm_edge(prev_c) then
      confirm_choice()
    end
    return false
  end

  -- セリフ送り
  if any_confirm_edge(prev_c) then
    advance_line()
  end

  return false
end

function game_draw()
  local scene = state.scene
  local page = scene and get_line_page(scene, state.line_index) or { text = "" }

  machine.clear(state.bg_color)

  local has_bg_image = scene and draw_background(scene, page)
  if not has_bg_image and scene and scene.decor_stars ~= false then
    draw_stars(machine.time_ms())
  end

  if scene then
    draw_character(scene, page)
  end

  draw_text_box()

  if not scene then
    return
  end

  local speaker = page.name
  if speaker == nil then
    speaker = scene.name
  end

  if state.mode == "choice" then
    draw_name_plate(speaker)
    draw_choice_menu()
    return
  end

  draw_name_plate(speaker)
  draw_body_text(page.text)

  draw_page_indicator()

  local lines = scene.lines or {}
  if state.line_index >= #lines then
    if scene.choices and #scene.choices > 0 then
      machine.text(MARGIN_X, H - 12, "A-R で選択へ", COL_HINT, COL_BOX)
    elseif scene.next then
      machine.text(MARGIN_X, H - 12, "A-R で次へ", COL_HINT, COL_BOX)
    end
  else
    machine.text(MARGIN_X, H - 12, "A-R / NEAR で次へ", COL_HINT, COL_BOX)
  end
end
