-- ============================================================================
-- Star Hop（タイル横スクロール）
-- Suityouka Game Machine 用サンプル（layers モード）
--
-- SD 配置: /games/tile_test/tile_test.lua + tiles/*.bin
--
-- 操作:
--   LEFT / RIGHT … 移動
--   UP / NEAR      … ジャンプ
--   クリア / ゲームオーバー後 … ジャンプ系ボタンでリトライ
-- ============================================================================

local W = machine.width()
local H = machine.height()

local TILE = 16
local MAP_COLS = 64
local MAP_ROWS = 15
local SHEET_COLS = 8
local PLAYER_W = 16
local PLAYER_H = 16

local MOVE_SPEED = 0.11
local JUMP_SPEED = -0.34
local GRAVITY = 0.00095
local MAX_FALL = 0.42

local TILE_GRASS = 1
local TILE_GRASS_DOT = 2
local TILE_DIRT = 3
local TILE_WATER = 4
local TILE_BRICK = 7
local TILE_STAR = 8

local SOLID = {
    [TILE_GRASS] = true,
    [TILE_GRASS_DOT] = true,
    [TILE_DIRT] = true,
    [TILE_BRICK] = true,
}

local sheet_id = nil
local player_id = nil
local ground_map = nil

local player_x = 0.0
local player_y = 0.0
local vel_x = 0.0
local vel_y = 0.0
local scroll_x = 0.0
local on_ground = false

local stars_total = 0
local stars_left = 0
local state = "title" -- title | play | win | dead

local function draw_center_text(y, text, fg, bg)
  local x = (W - #text * 8) // 2
  machine.text(x, y, text, fg, bg)
end

local function draw_title_screen()
  local bg = machine.rgb(12, 18, 40)
  machine.fill_rect(0, 0, W, H, bg)
  draw_center_text(72, "STAR HOP", machine.rgb(255, 220, 100), bg)
  draw_center_text(96, "TILE SCROLL DEMO", machine.rgb(180, 200, 230), bg)
  if (math.floor(machine.time_ms() / 500) % 2) == 0 then
    draw_center_text(136, "PRESS BUTTON", machine.rgb(255, 255, 255), bg)
  end
  draw_center_text(160, "L/R MOVE  UP/NEAR JUMP", machine.rgb(140, 160, 190), bg)
  draw_center_text(184, "Collect all stars!", machine.rgb(120, 140, 170), bg)
end

local function map_index(col, row)
    return row * MAP_COLS + col + 1
end

local function in_bounds(col, row)
    return col >= 0 and col < MAP_COLS and row >= 0 and row < MAP_ROWS
end

local function get_tile(col, row)
    if not in_bounds(col, row) then
        return 0
    end
    return ground_map[map_index(col, row)]
end

local function set_tile(col, row, tile_id)
    if in_bounds(col, row) then
        ground_map[map_index(col, row)] = tile_id
    end
end

local function count_stars()
    local n = 0
    for i = 1, #ground_map do
        if ground_map[i] == TILE_STAR then
            n = n + 1
        end
    end
    return n
end

local function build_level_map()
    local m = {}
    for i = 1, MAP_COLS * MAP_ROWS do
        m[i] = 0
    end

    local function put(col, row, tile_id)
        if in_bounds(col, row) then
            m[map_index(col, row)] = tile_id
        end
    end

    local function fill(col0, row0, w, h, tile_id)
        for row = row0, row0 + h - 1 do
            for col = col0, col0 + w - 1 do
                put(col, row, tile_id)
            end
        end
    end

    -- 下段の地面（穴と水場あり）
    for col = 0, MAP_COLS - 1 do
        if col >= 18 and col <= 21 then
            put(col, MAP_ROWS - 1, TILE_WATER)
            put(col, MAP_ROWS - 2, TILE_WATER)
        elseif col >= 34 and col <= 37 then
            put(col, MAP_ROWS - 1, TILE_WATER)
            put(col, MAP_ROWS - 2, TILE_WATER)
        elseif col >= 50 and col <= 53 then
            put(col, MAP_ROWS - 1, TILE_WATER)
            put(col, MAP_ROWS - 2, TILE_WATER)
        else
            local ground = (col % 5 == 0) and TILE_GRASS_DOT or TILE_GRASS
            put(col, MAP_ROWS - 1, ground)
            put(col, MAP_ROWS - 2, TILE_DIRT)
        end
    end

    local platforms = {
        { 6, 10, 5, TILE_BRICK },
        { 14, 8, 4, TILE_BRICK },
        { 24, 9, 5, TILE_BRICK },
        { 30, 7, 4, TILE_BRICK },
        { 40, 8, 6, TILE_BRICK },
        { 48, 10, 4, TILE_BRICK },
        { 56, 9, 5, TILE_BRICK },
    }
    for _, p in ipairs(platforms) do
        fill(p[1], p[2], p[3], 1, p[4])
    end

    local stars = {
        { 8, 9 }, { 16, 7 }, { 26, 8 }, { 32, 6 },
        { 43, 7 }, { 50, 9 }, { 58, 8 }, { 4, 9 },
        { 22, 9 }, { 38, 7 }, { 46, 7 }, { 60, 8 },
    }
    for _, s in ipairs(stars) do
        put(s[1], s[2], TILE_STAR)
    end

    return m
end

local function build_cloud_map()
    local m = {}
    for row = 0, MAP_ROWS - 1 do
        for col = 0, MAP_COLS - 1 do
            local idx = map_index(col, row)
            if row <= 2 and col % 7 == 2 then
                m[idx] = 5
            elseif row == 3 and col % 11 == 5 then
                m[idx] = 5
            elseif row >= 4 and row <= 7 and col % 9 == 1 then
                m[idx] = 6
            else
                m[idx] = 0
            end
        end
    end
    return m
end

local function foreach_overlap_tiles(px, py, pw, ph, fn)
    local left = math.floor(px / TILE)
    local right = math.floor((px + pw - 1) / TILE)
    local top = math.floor(py / TILE)
    local bottom = math.floor((py + ph - 1) / TILE)
    for row = top, bottom do
        for col = left, right do
            fn(col, row, get_tile(col, row))
        end
    end
end

local function hits_solid(px, py, pw, ph)
    local blocked = false
    foreach_overlap_tiles(px, py, pw, ph, function(_, _, tile_id)
        if SOLID[tile_id] then
            blocked = true
        end
    end)
    return blocked
end

local function touches_water(px, py, pw, ph)
    local wet = false
    foreach_overlap_tiles(px, py, pw, ph, function(_, _, tile_id)
        if tile_id == TILE_WATER then
            wet = true
        end
    end)
    if wet then
        return true
    end

    -- 足元直下の水タイル（落下・歩行の境界）
    local feet_row = math.floor((py + ph) / TILE)
    local left = math.floor(px / TILE)
    local right = math.floor((px + pw - 1) / TILE)
    for col = left, right do
        if get_tile(col, feet_row) == TILE_WATER then
            return true
        end
    end
    return false
end

local function try_move_axis(axis, amount)
    local next_x = player_x
    local next_y = player_y
    if axis == "x" then
        next_x = player_x + amount
    else
        next_y = player_y + amount
    end

    if not hits_solid(next_x, next_y, PLAYER_W, PLAYER_H) then
        player_x = next_x
        player_y = next_y
        return true
    end
    return false
end

local function collect_stars()
    local left = math.floor(player_x / TILE)
    local right = math.floor((player_x + PLAYER_W - 1) / TILE)
    local top = math.floor(player_y / TILE)
    local bottom = math.floor((player_y + PLAYER_H - 1) / TILE)
    local dirty = false
    for row = top, bottom do
        for col = left, right do
            if get_tile(col, row) == TILE_STAR then
                set_tile(col, row, 0)
                stars_left = stars_left - 1
                dirty = true
            end
        end
    end
    if dirty then
        machine.set_layer_tiles(0, ground_map)
    end
end

local function update_camera()
    local max_scroll = MAP_COLS * TILE - W
    if max_scroll < 0 then
        max_scroll = 0
    end
    scroll_x = player_x + PLAYER_W / 2 - W / 2
    if scroll_x < 0 then
        scroll_x = 0
    end
    if scroll_x > max_scroll then
        scroll_x = max_scroll
    end
end

local function reset_game()
    ground_map = build_level_map()
    stars_total = count_stars()
    stars_left = stars_total

    player_x = 32.0
    player_y = (MAP_ROWS - 4) * TILE - PLAYER_H
    vel_x = 0.0
    vel_y = 0.0
    scroll_x = 0.0
    on_ground = false
    state = "play"

    machine.set_layer_tiles(0, ground_map)
end

function game_init()
    machine.set_draw_mode("layers")
    machine.set_layer_backdrop(machine.rgb(25, 35, 70))

    sheet_id = machine.load_sprite("tiles/tiles.bin", 128, 128)
    if not sheet_id then
        print("tile_test: load tiles/tiles.bin failed")
        return
    end

    machine.set_layer(0, {
        tileset = sheet_id,
        tile_w = TILE,
        tile_h = TILE,
        sheet_cols = SHEET_COLS,
        map_cols = MAP_COLS,
        map_rows = MAP_ROWS,
        map_x = 0,
        map_y = 0,
        scroll_x = 0,
        scroll_y = 0,
        enabled = true,
    })

    machine.set_layer(1, {
        tileset = sheet_id,
        tile_w = TILE,
        tile_h = TILE,
        sheet_cols = SHEET_COLS,
        map_cols = MAP_COLS,
        map_rows = MAP_ROWS,
        map_x = 0,
        map_y = 0,
        scroll_x = 0,
        scroll_y = 0,
        enabled = true,
        transparent = true,
    })
    machine.set_layer_tiles(1, build_cloud_map())

    player_id = machine.load_sprite("tiles/player.bin", PLAYER_W, PLAYER_H)
    if not player_id then
        print("tile_test: load tiles/player.bin failed")
    end

    -- タイトル表示用にマップだけ構築（プレイ開始まで state=title）
    ground_map = build_level_map()
    machine.set_layer_tiles(0, ground_map)
    stars_total = count_stars()
    stars_left = stars_total
    state = "title"
    print("tile_test: init OK")
end

function game_update(dt)
    if state == "title" then
        if machine.jump_pressed() then
            reset_game()
        end
        return false
    end

    if state ~= "play" then
        if machine.jump_pressed() then
            reset_game()
        end
        return false
    end

    if machine.pressed(0) then
        vel_x = MOVE_SPEED
    elseif machine.pressed(2) then
        vel_x = -MOVE_SPEED
    else
        vel_x = vel_x * 0.75
        if math.abs(vel_x) < 0.01 then
            vel_x = 0.0
        end
    end

    if (machine.pressed(1) or machine.pressed(7)) and on_ground then
        vel_y = JUMP_SPEED
        on_ground = false
    end

    vel_y = vel_y + GRAVITY * dt
    if vel_y > MAX_FALL then
        vel_y = MAX_FALL
    end

    try_move_axis("x", vel_x * dt)

    on_ground = false
    if vel_y >= 0 then
        if try_move_axis("y", vel_y * dt) then
            -- moved
        else
            vel_y = 0
            on_ground = true
        end
    else
        if try_move_axis("y", vel_y * dt) then
            -- moved
        else
            vel_y = 0
        end
    end

    if player_y + PLAYER_H > MAP_ROWS * TILE then
        state = "dead"
    end

    if touches_water(player_x, player_y, PLAYER_W, PLAYER_H) then
        state = "dead"
    end

    collect_stars()
    update_camera()

    machine.set_layer_scroll(0, math.floor(scroll_x), 0)
    machine.set_layer_scroll(1, math.floor(scroll_x * 0.45), 0)

    if stars_left <= 0 then
        state = "win"
    end

    return false
end

function game_draw()
    if state == "title" then
        draw_title_screen()
        return
    end

    if player_id then
        local sx = math.floor(player_x - scroll_x)
        local sy = math.floor(player_y)
        machine.draw_sprite_keyed(player_id, sx, sy)
    end

    local bg = machine.rgb(25, 35, 70)
    local fg = machine.rgb(255, 255, 255)
    local sub = machine.rgb(200, 210, 230)

    machine.text(4, 4, "STAR HOP", fg, bg)
    machine.text(4, 14, "STAR " .. (stars_total - stars_left) .. "/" .. stars_total, sub, bg)

    if state == "win" then
        machine.text(W // 2 - 44, H // 2 - 8, "CLEAR!", machine.rgb(255, 220, 60), bg)
        machine.text(W // 2 - 56, H // 2 + 8, "JUMP=RETRY", sub, bg)
    elseif state == "dead" then
        machine.text(W // 2 - 52, H // 2 - 8, "GAME OVER", machine.rgb(255, 80, 80), bg)
        machine.text(W // 2 - 56, H // 2 + 8, "JUMP=RETRY", sub, bg)
    else
        machine.text(4, H - 12, "L/R MOVE UP/NEAR JUMP", sub, bg)
    end
end
