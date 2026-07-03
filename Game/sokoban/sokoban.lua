-- ============================================================================
-- 倉庫番（Sokoban）— ランダム生成・得点制
-- Suityouka Game Machine 用サンプル
--
-- SD 配置: /games/sokoban/sokoban.lua
--
-- 操作:
--   タイトル … LEFT/RIGHT で難易度、決定で開始
--   十字キー … 移動（箱を押す）
--   NEAR 等（決定） … クリア画面スキップ
--   OP_LEFT … プレイ中にタイトルへ戻る
-- ============================================================================

local DIFFICULTIES = {
    {
        id = "easy",
        label = "EASY",
        pillar_min = 2,
        pillar_rand = 2,
        box_base = 2,
        shuffle_mul = 0.85,
        score_mul = 1.0,
    },
    {
        id = "normal",
        label = "NORMAL",
        pillar_min = 4,
        pillar_rand = 3,
        box_base = 2,
        shuffle_mul = 1.0,
        score_mul = 1.25,
    },
    {
        id = "hard",
        label = "HARD",
        pillar_min = 7,
        pillar_rand = 4,
        box_base = 3,
        shuffle_mul = 1.2,
        score_mul = 1.5,
    },
}
local DIFF_COUNT = #DIFFICULTIES

local W = machine.width()
local H = machine.height()

-- マップ（1 始まり座標）
local MAP_W = 13
local MAP_H = 13
local CELL = 16
local BOARD_W = MAP_W * CELL
local BOARD_H = MAP_H * CELL
local BOARD_X = (W - BOARD_W) // 2
local BOARD_Y = 22

local TILE_WALL = 1
local TILE_FLOOR = 2
local TILE_GOAL = 3

local BTN_RIGHT = 0
local BTN_UP = 1
local BTN_LEFT = 2
local BTN_DOWN = 3
local BTN_OP_LEFT = 4

local COL_BG = machine.rgb(18, 22, 36)
local COL_WALL = machine.rgb(72, 58, 44)
local COL_WALL_EDGE = machine.rgb(48, 38, 28)
local COL_FLOOR = machine.rgb(140, 118, 88)
local COL_FLOOR_ALT = machine.rgb(128, 108, 80)
local COL_GOAL = machine.rgb(60, 180, 90)
local COL_GOAL_MARK = machine.rgb(40, 120, 60)
local COL_BOX = machine.rgb(210, 130, 50)
local COL_BOX_EDGE = machine.rgb(160, 90, 30)
local COL_BOX_GOAL = machine.rgb(100, 220, 120)
local COL_PLAYER = machine.rgb(80, 160, 255)
local COL_PLAYER_EDGE = machine.rgb(40, 100, 200)
local COL_HUD = machine.rgb(230, 240, 255)
local COL_HUD_DIM = machine.rgb(140, 150, 180)
local COL_ACCENT = machine.rgb(255, 220, 100)
local COL_CLEAR = machine.rgb(120, 255, 180)

local SAVE_PATH = "hi_score.dat"

local mode = "title" -- title | play | clear
local font_ok = false
local hi_scores = { 0, 0, 0 }
local difficulty = 2
local score = 0
local stage = 1
local moves = 0
local blink = 0
local clear_timer = 0
local last_clear_points = 0
local prev_pressed = {}

local static_map = {}
local boxes = {}
local box_lookup = {}
local px = 1
local py = 1
local rng_state = 1

-- ---------------------------------------------------------------------------
-- 乱数（math.random 非依存）
-- ---------------------------------------------------------------------------

local function seed_rng(seed)
    rng_state = seed % 2147483646
    if rng_state <= 0 then
        rng_state = rng_state + 2147483645
    end
end

local function rand_int(max_val)
    rng_state = (rng_state * 48271) % 2147483647
    return 1 + (rng_state % max_val)
end

local function shuffle_dirs(out)
    local dirs = {
        {0, -1},
        {0, 1},
        {-1, 0},
        {1, 0},
    }
    for i = #dirs, 2, -1 do
        local j = rand_int(i)
        dirs[i], dirs[j] = dirs[j], dirs[i]
    end
    for i = 1, 4 do
        out[i] = dirs[i]
    end
end

-- ---------------------------------------------------------------------------
-- ユーティリティ
-- ---------------------------------------------------------------------------

local function confirm_pressed()
    return machine.jump_pressed()
end

local function just_pressed(btn)
    local now = machine.pressed(btn)
    local was = prev_pressed[btn]
    prev_pressed[btn] = now
    return now and not was
end

local function draw_center_text(y, text, col)
    local tw = #text * 8
    local x = (W - tw) // 2
    if x < 0 then
        x = 0
    end
    machine.text(x, y, text, col, COL_BG)
end

local function load_hi_scores()
    if not machine.file_exists(SAVE_PATH) then
        return
    end
    local data = machine.load_data(SAVE_PATH)
    if not data then
        return
    end
    if type(data.scores) == "table" then
        for i = 1, DIFF_COUNT do
            if type(data.scores[i]) == "number" then
                hi_scores[i] = math.max(0, math.floor(data.scores[i]))
            end
        end
    elseif type(data.hi_score) == "number" then
        hi_scores[2] = math.max(0, math.floor(data.hi_score))
    end
end

local function save_hi_scores()
    machine.save_data(SAVE_PATH, {
        scores = { hi_scores[1], hi_scores[2], hi_scores[3] },
    })
end

local function hi_score_for_difficulty(diff)
    return hi_scores[diff] or 0
end

local function record_hi_score()
    local hs = hi_scores[difficulty] or 0
    if score > hs then
        hi_scores[difficulty] = score
        save_hi_scores()
    end
end

local function in_bounds(x, y)
    return x >= 1 and x <= MAP_W and y >= 1 and y <= MAP_H
end

local function tile_at(x, y)
    if not in_bounds(x, y) then
        return TILE_WALL
    end
    return static_map[y][x]
end

local function is_wall(x, y)
    return tile_at(x, y) == TILE_WALL
end

local function is_goal(x, y)
    return tile_at(x, y) == TILE_GOAL
end

local function is_walkable_floor(x, y)
    local t = tile_at(x, y)
    return t == TILE_FLOOR or t == TILE_GOAL
end

local function clear_boxes()
    boxes = {}
    box_lookup = {}
end

local function has_box(x, y)
    return box_lookup[y] and box_lookup[y][x] == true
end

local function add_box(x, y)
    boxes[#boxes + 1] = { x = x, y = y }
    if not box_lookup[y] then
        box_lookup[y] = {}
    end
    box_lookup[y][x] = true
end

local function remove_box(x, y)
    if box_lookup[y] then
        box_lookup[y][x] = nil
    end
    for i = #boxes, 1, -1 do
        local b = boxes[i]
        if b.x == x and b.y == y then
            table.remove(boxes, i)
            break
        end
    end
end

local function all_boxes_on_goals()
    for i = 1, #boxes do
        if not is_goal(boxes[i].x, boxes[i].y) then
            return false
        end
    end
    return #boxes > 0
end

local function count_boxes_off_goal()
    local n = 0
    for i = 1, #boxes do
        if not is_goal(boxes[i].x, boxes[i].y) then
            n = n + 1
        end
    end
    return n
end

-- 運ぶ箱が無い（0 個／既クリア）ステージを除外
local function is_stage_playable(expected_box_count)
    if #boxes ~= expected_box_count or #boxes == 0 then
        return false
    end
    if count_boxes_off_goal() < 1 then
        return false
    end
    return true
end

local function diff_def()
    return DIFFICULTIES[difficulty] or DIFFICULTIES[2]
end

-- ---------------------------------------------------------------------------
-- ステージ生成（逆生成: 解けた状態から箱を引く → 必ず解ける）
-- ---------------------------------------------------------------------------

local function box_count_for_stage(stg)
    local def = diff_def()
    local n = def.box_base + (stg - 1) // 2
    if difficulty >= 3 then
        n = n + (stg - 1) // 3
    end
    local cap = (difficulty >= 3) and 6 or 5
    if n > cap then
        n = cap
    end
    return n
end

local function pillar_count_for_difficulty()
    local def = diff_def()
    return def.pillar_min + rand_int(def.pillar_rand)
end

local function init_static_border()
    static_map = {}
    for y = 1, MAP_H do
        static_map[y] = {}
        for x = 1, MAP_W do
            if x == 1 or x == MAP_W or y == 1 or y == MAP_H then
                static_map[y][x] = TILE_WALL
            else
                static_map[y][x] = TILE_FLOOR
            end
        end
    end
end

local function place_random_pillars(count)
    local placed = 0
    local tries = 0
    while placed < count and tries < 200 do
        tries = tries + 1
        local x = 2 + rand_int(MAP_W - 2)
        local y = 2 + rand_int(MAP_H - 2)
        if static_map[y][x] == TILE_FLOOR then
            static_map[y][x] = TILE_WALL
            placed = placed + 1
        end
    end
end

local function collect_floor_cells()
    local cells = {}
    for y = 2, MAP_H - 1 do
        for x = 2, MAP_W - 1 do
            if static_map[y][x] == TILE_FLOOR then
                cells[#cells + 1] = { x = x, y = y }
            end
        end
    end
    return cells
end

local function manhattan(a, b)
    return math.abs(a.x - b.x) + math.abs(a.y - b.y)
end

local function pick_goal_positions(count, floor_cells)
    local goals = {}
    local pool = {}
    for i = 1, #floor_cells do
        pool[i] = floor_cells[i]
    end

    for _ = 1, count do
        if #pool == 0 then
            return nil
        end
        local best_idx = 1
        local best_score = -1
        for i = 1, #pool do
            local c = pool[i]
            local min_dist = 999
            for j = 1, #goals do
                local d = manhattan(c, goals[j])
                if d < min_dist then
                    min_dist = d
                end
            end
            if min_dist > best_score then
                best_score = min_dist
                best_idx = i
            end
        end
        local chosen = pool[best_idx]
        goals[#goals + 1] = chosen
        static_map[chosen.y][chosen.x] = TILE_GOAL
        table.remove(pool, best_idx)
    end
    return goals
end

local function pick_player_start(goals)
    local candidates = {}
    for y = 2, MAP_H - 1 do
        for x = 2, MAP_W - 1 do
            if is_walkable_floor(x, y) and not has_box(x, y) then
                local ok = true
                for i = 1, #goals do
                    if goals[i].x == x and goals[i].y == y then
                        ok = false
                        break
                    end
                end
                if ok then
                    candidates[#candidates + 1] = { x = x, y = y }
                end
            end
        end
    end
    if #candidates == 0 then
        return false
    end
    local pick = candidates[rand_int(#candidates)]
    px = pick.x
    py = pick.y
    return true
end

local function reverse_pull(dx, dy)
    local nx = px + dx
    local ny = py + dy
    local bx = px - dx
    local by = py - dy
    if not in_bounds(nx, ny) or not in_bounds(bx, by) then
        return false
    end
    if not is_walkable_floor(nx, ny) then
        return false
    end
    if has_box(nx, ny) then
        return false
    end
    if not has_box(bx, by) then
        return false
    end
    if not is_walkable_floor(px, py) then
        return false
    end
    remove_box(bx, by)
    add_box(px, py)
    px = nx
    py = ny
    return true
end

local function reverse_walk(dx, dy)
    local nx = px + dx
    local ny = py + dy
    if not in_bounds(nx, ny) then
        return false
    end
    if not is_walkable_floor(nx, ny) then
        return false
    end
    if has_box(nx, ny) then
        return false
    end
    px = nx
    py = ny
    return true
end

local function reverse_shuffle(stg, box_count)
    local mul = diff_def().shuffle_mul
    local target = math.floor((35 + stg * 12 + box_count * 8) * mul)
    if target < 20 then
        target = 20
    end
    local done = 0
    local fails = 0
    local dirs = {}
    while done < target and fails < target * 4 do
        shuffle_dirs(dirs)
        local moved = false
        for i = 1, 4 do
            local dx = dirs[i][1]
            local dy = dirs[i][2]
            if reverse_pull(dx, dy) then
                moved = true
                break
            end
        end
        if not moved then
            for i = 1, 4 do
                local dx = dirs[i][1]
                local dy = dirs[i][2]
                if reverse_walk(dx, dy) then
                    moved = true
                    break
                end
            end
        end
        if moved then
            done = done + 1
            fails = 0
        else
            fails = fails + 1
        end
    end
    return done >= math.floor(target * 0.45)
end

local function apply_fallback_stage(box_count)
    init_static_border()
    if difficulty >= 2 then
        place_random_pillars(math.min(pillar_count_for_difficulty(), 4))
    end

    local goal_spots = {
        { x = 5, y = 3 },
        { x = 7, y = 3 },
        { x = 6, y = 5 },
        { x = 4, y = 7 },
        { x = 8, y = 7 },
        { x = 6, y = 9 },
    }
    local use = math.min(box_count, #goal_spots)
    for i = 1, use do
        local g = goal_spots[i]
        static_map[g.y][g.x] = TILE_GOAL
    end

    clear_boxes()
    local off_spots = {
        { x = 4, y = 3 },
        { x = 8, y = 3 },
        { x = 5, y = 5 },
        { x = 7, y = 5 },
        { x = 5, y = 7 },
        { x = 7, y = 7 },
    }
    for i = 1, use do
        local p = off_spots[i]
        add_box(p.x, p.y)
    end
    px = 9
    py = 6
    moves = 0
    return is_stage_playable(box_count)
end

local function generate_stage(stg)
    local box_count = box_count_for_stage(stg)
    local seed = (machine.time_ms() + stg * 7919 + score * 13 + difficulty * 3571) % 2147483646
    if seed <= 0 then
        seed = 1
    end

    for attempt = 1, 32 do
        seed_rng(seed + attempt * 9973)
        init_static_border()
        place_random_pillars(pillar_count_for_difficulty())

        local floor_cells = collect_floor_cells()
        if #floor_cells >= box_count + 4 then
            local goals = pick_goal_positions(box_count, floor_cells)
            if goals then
                clear_boxes()
                for i = 1, #goals do
                    add_box(goals[i].x, goals[i].y)
                end

                if pick_player_start(goals) and reverse_shuffle(stg, box_count)
                    and is_stage_playable(box_count) then
                    moves = 0
                    return true
                end
            end
        end
    end

    seed_rng(seed + 424242)
    return apply_fallback_stage(box_count)
end

-- ---------------------------------------------------------------------------
-- プレイ操作
-- ---------------------------------------------------------------------------

local function try_push(dx, dy)
    local nx = px + dx
    local ny = py + dy
    if not in_bounds(nx, ny) then
        return false
    end
    if is_wall(nx, ny) then
        return false
    end

    if has_box(nx, ny) then
        local bx = nx + dx
        local by = ny + dy
        if not in_bounds(bx, by) then
            return false
        end
        if is_wall(bx, by) or has_box(bx, by) then
            return false
        end
        if not is_walkable_floor(bx, by) then
            return false
        end
        remove_box(nx, ny)
        add_box(bx, by)
        px = nx
        py = ny
        moves = moves + 1
        machine.play_tone(320, 25)
        return true
    end

    if is_walkable_floor(nx, ny) then
        px = nx
        py = ny
        moves = moves + 1
        return true
    end
    return false
end

local function calc_stage_clear_points(stg, move_count)
    local base = 100 * stg
    local move_bonus = math.max(0, 400 - move_count * 4)
    local box_bonus = box_count_for_stage(stg) * 50
    local mul = diff_def().score_mul
    return math.floor((base + move_bonus + box_bonus) * mul)
end

local function begin_new_run()
    score = 0
    stage = 1
    for attempt = 1, 8 do
        if generate_stage(stage) then
            break
        end
    end
    mode = "play"
    clear_timer = 0
end

local function advance_to_next_stage()
    stage = stage + 1
    generate_stage(stage)
    mode = "play"
    clear_timer = 0
end

-- ---------------------------------------------------------------------------
-- 描画
-- ---------------------------------------------------------------------------

local function draw_title_bg()
    machine.clear(COL_BG)
    for y = 0, H - 1, 8 do
        local c = machine.rgb(12 + y // 10, 16 + y // 8, 32 + y // 5)
        machine.fill_rect(0, y, W, 8, c)
    end
    local t = machine.time_ms() * 0.001
    for i = 0, 20 do
        local sx = (i * 41 + math.floor(t * 30 + i * 7)) % W
        local sy = (i * 29 + math.floor(t * 18)) % H
        machine.fill_rect(sx, sy, 2, 2, COL_HUD_DIM)
    end
end

local function draw_title()
    draw_title_bg()
    if font_ok then
        machine.text(108, 40, "倉庫番", COL_ACCENT, COL_BG)
    end
    draw_center_text(64, "SOKOBAN", COL_ACCENT)
    draw_center_text(84, "RANDOM STAGES", COL_HUD_DIM)

    draw_center_text(108, "DIFFICULTY", COL_HUD_DIM)
    local def = diff_def()
    local label = def.label
    local tw = #label * 8
    local cx = W // 2
    machine.text(cx - tw // 2 - 16, 120, "<", COL_HUD_DIM, COL_BG)
    machine.text(cx - tw // 2, 120, label, COL_ACCENT, COL_BG)
    machine.text(cx + tw // 2 + 8, 120, ">", COL_HUD_DIM, COL_BG)

    local pillar_hint = "OBST:" .. def.pillar_min .. "-" .. (def.pillar_min + def.pillar_rand - 1)
    draw_center_text(136, pillar_hint, COL_HUD_DIM)

    local hs = hi_score_for_difficulty(difficulty)
    if hs > 0 then
        draw_center_text(152, "HI " .. hs, COL_HUD_DIM)
    end

    if (math.floor(blink / 500) % 2) == 0 then
        draw_center_text(172, "PRESS TO START", COL_HUD)
    end
    draw_center_text(192, "L/R: DIFF  D-PAD: MOVE", COL_HUD_DIM)
    draw_center_text(208, "OP_LEFT: BACK TO TITLE", COL_HUD_DIM)
end

local function draw_hud()
    local def = diff_def()
    machine.text(4, 4, "SC:" .. score, COL_HUD, COL_BG)
    machine.text(4, 14, "ST:" .. stage .. " " .. def.label:sub(1, 1), COL_HUD, COL_BG)
    machine.text(W - 72, 4, "MV:" .. moves, COL_HUD, COL_BG)
    machine.text(W - 72, 14, "BX:" .. #boxes, COL_HUD_DIM, COL_BG)
end

local function draw_cell(x, y)
    local sx = BOARD_X + (x - 1) * CELL
    local sy = BOARD_Y + (y - 1) * CELL
    local t = tile_at(x, y)

    if t == TILE_WALL then
        machine.fill_rect(sx, sy, CELL, CELL, COL_WALL)
        machine.fill_rect(sx, sy, CELL, 2, COL_WALL_EDGE)
        machine.fill_rect(sx, sy, 2, CELL, COL_WALL_EDGE)
        return
    end

    local floor_col = ((x + y) % 2 == 0) and COL_FLOOR or COL_FLOOR_ALT
    machine.fill_rect(sx, sy, CELL, CELL, floor_col)

    if t == TILE_GOAL then
        machine.fill_rect(sx + 4, sy + 4, CELL - 8, CELL - 8, COL_GOAL)
        machine.fill_rect(sx + 6, sy + 6, CELL - 12, CELL - 12, COL_GOAL_MARK)
    end

    if has_box(x, y) then
        local col = is_goal(x, y) and COL_BOX_GOAL or COL_BOX
        local edge = is_goal(x, y) and COL_GOAL_MARK or COL_BOX_EDGE
        machine.fill_rect(sx + 2, sy + 2, CELL - 4, CELL - 4, col)
        machine.fill_rect(sx + 2, sy + 2, CELL - 4, 2, edge)
        machine.fill_rect(sx + 2, sy + 2, 2, CELL - 4, edge)
    end

    if px == x and py == y then
        local cx = sx + CELL // 2
        local cy = sy + CELL // 2
        machine.fill_circle(cx, cy, 5, COL_PLAYER)
        machine.fill_circle(cx, cy, 3, COL_PLAYER_EDGE)
    end
end

local function draw_board()
    for y = 1, MAP_H do
        for x = 1, MAP_W do
            draw_cell(x, y)
        end
    end
end

local function draw_play()
    machine.clear(COL_BG)
    draw_hud()
    draw_board()
end

local function draw_clear_overlay()
    machine.fill_rect(40, 80, W - 80, 80, machine.rgb(10, 20, 30))
    machine.fill_rect(40, 80, W - 80, 2, COL_CLEAR)
    machine.fill_rect(40, 158, W - 80, 2, COL_CLEAR)
    draw_center_text(96, "STAGE CLEAR!", COL_CLEAR)
    draw_center_text(112, "+" .. last_clear_points .. " PTS", COL_ACCENT)
    draw_center_text(128, "TOTAL " .. score, COL_HUD)
    if (math.floor(clear_timer / 400) % 2) == 0 then
        draw_center_text(144, "NEXT STAGE...", COL_HUD_DIM)
    end
end

-- ---------------------------------------------------------------------------
-- エントリ
-- ---------------------------------------------------------------------------

function game_init()
    font_ok = machine.load_font("fonts/game_font.bin") == true
    mode = "title"
    score = 0
    stage = 1
    moves = 0
    blink = 0
    clear_timer = 0
    prev_pressed = {}
    difficulty = 2
    hi_scores = { 0, 0, 0 }
    load_hi_scores()
    clear_boxes()
end

function game_update(dt)
    blink = blink + dt

    if mode == "title" then
        if just_pressed(BTN_LEFT) then
            difficulty = difficulty - 1
            if difficulty < 1 then
                difficulty = DIFF_COUNT
            end
        elseif just_pressed(BTN_RIGHT) then
            difficulty = difficulty + 1
            if difficulty > DIFF_COUNT then
                difficulty = 1
            end
        elseif confirm_pressed() then
            begin_new_run()
        end
        return false
    end

    if mode == "clear" then
        clear_timer = clear_timer + dt
        if confirm_pressed() or clear_timer >= 2600 then
            advance_to_next_stage()
        end
        return false
    end

    -- play
    if just_pressed(BTN_OP_LEFT) then
        record_hi_score()
        mode = "title"
        return false
    end

    local moved = false
    if just_pressed(BTN_UP) then
        moved = try_push(0, -1)
    elseif just_pressed(BTN_DOWN) then
        moved = try_push(0, 1)
    elseif just_pressed(BTN_LEFT) then
        moved = try_push(-1, 0)
    elseif just_pressed(BTN_RIGHT) then
        moved = try_push(1, 0)
    end

    if moved and all_boxes_on_goals() then
        last_clear_points = calc_stage_clear_points(stage, moves)
        score = score + last_clear_points
        record_hi_score()
        machine.play_tone(660, 60)
        machine.play_tone(880, 100)
        mode = "clear"
        clear_timer = 0
    end

    return false
end

function game_draw()
    if mode == "title" then
        draw_title()
        return
    end

    draw_play()
    if mode == "clear" then
        draw_clear_overlay()
    end
end
