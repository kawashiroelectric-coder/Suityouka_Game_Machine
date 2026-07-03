-- ============================================================================
-- Stack Blocks（落ち物パズル）
-- Suityouka Game Machine 用サンプル
--
-- SD 配置: /games/stack_blocks/stack_blocks.lua
--
-- 操作:
--   LEFT / RIGHT … 左右移動
--   DOWN         … ソフトドロップ
--   NEAR / FAR … 回転
--   ゲームオーバー後 … ジャンプ系ボタンでリトライ
-- ============================================================================

local W = machine.width()
local H = machine.height()

local COLS = 10
local ROWS = 20
local CELL = 10
local BOARD_W = COLS * CELL
local BOARD_H = ROWS * CELL
local BOARD_X = (W - BOARD_W) // 2
local BOARD_Y = 8

local COL_BG = machine.rgb(12, 16, 28)
local COL_GRID = machine.rgb(28, 36, 56)
local COL_BORDER = machine.rgb(90, 110, 150)
local COL_TEXT = machine.rgb(220, 230, 255)
local COL_GHOST = machine.rgb(60, 70, 90)

-- ピース種別ごとの色（一般名のみ・商標名は使用しない）
local PIECE_COLORS = {
    machine.rgb(0, 220, 220),   -- 1: 棒形
    machine.rgb(220, 220, 0),   -- 2: 正方形
    machine.rgb(160, 0, 220),   -- 3: 三又形
    machine.rgb(0, 220, 80),    -- 4: 右階段形
    machine.rgb(220, 40, 40),   -- 5: 左階段形
    machine.rgb(40, 80, 220),   -- 6: 左フック形
    machine.rgb(220, 140, 0),   -- 7: 右フック形
}

local function copy_cells(cells)
    local out = {}
    for i = 1, #cells do
        out[i] = { cells[i][1], cells[i][2] }
    end
    return out
end

local function normalize_cells(cells)
    local min_x, min_y = 99, 99
    for i = 1, #cells do
        if cells[i][1] < min_x then
            min_x = cells[i][1]
        end
        if cells[i][2] < min_y then
            min_y = cells[i][2]
        end
    end
    local out = {}
    for i = 1, #cells do
        out[i] = { cells[i][1] - min_x, cells[i][2] - min_y }
    end
    return out
end

local function rotate_cells_cw(cells)
    local out = {}
    for i = 1, #cells do
        local x = cells[i][1]
        local y = cells[i][2]
        out[i] = { -y, x }
    end
    return normalize_cells(out)
end

local function cells_equal(a, b)
    if #a ~= #b then
        return false
    end
    local used = {}
    for i = 1, #a do
        local found = false
        for j = 1, #b do
            if not used[j] and a[i][1] == b[j][1] and a[i][2] == b[j][2] then
                used[j] = true
                found = true
                break
            end
        end
        if not found then
            return false
        end
    end
    return true
end

local function generate_rotations(base)
    local rotations = { normalize_cells(copy_cells(base)) }
    local current = rotations[1]
    for _ = 1, 3 do
        current = rotate_cells_cw(current)
        local duplicate = false
        for _, existing in ipairs(rotations) do
            if cells_equal(current, existing) then
                duplicate = true
                break
            end
        end
        if not duplicate then
            rotations[#rotations + 1] = copy_cells(current)
        end
    end
    return rotations
end

-- 各ピースの基本形から 90° 回転状態を自動生成
local PIECE_BASES = {
    { {0, 0}, {1, 0}, {2, 0}, {3, 0} },
    { {0, 0}, {1, 0}, {0, 1}, {1, 1} },
    { {1, 0}, {0, 1}, {1, 1}, {2, 1} },
    { {1, 0}, {2, 0}, {0, 1}, {1, 1} },
    { {0, 0}, {1, 0}, {1, 1}, {2, 1} },
    { {0, 0}, {0, 1}, {1, 1}, {2, 1} },
    { {2, 0}, {0, 1}, {1, 1}, {2, 1} },
}

local PIECES = {}
for i = 1, #PIECE_BASES do
    PIECES[i] = generate_rotations(PIECE_BASES[i])
end

local BTN_LEFT = 2
local BTN_RIGHT = 0
local BTN_DOWN = 3
local BTN_ROTATE = {6, 7}

local board = {}
local cur_type = 1
local cur_rot = 1
local cur_x = 0
local cur_y = 0
local next_type = 1
local score = 0
local lines_cleared = 0
local level = 1
local drop_ms = 550
local drop_acc = 0
local move_repeat_ms = 0
local game_over = false
local blink = 0
local spawn_count = 0
local prev_pressed = {}

local function reset_board()
    board = {}
    for y = 1, ROWS do
        board[y] = {}
        for x = 1, COLS do
            board[y][x] = 0
        end
    end
end

local function random_piece_type()
    spawn_count = spawn_count + 1
    return 1 + (spawn_count * 31 + lines_cleared * 7) % 7
end

local function piece_cells(piece_type, rotation)
    local def = PIECES[piece_type]
    local rot_count = #def
    local rot_idx = ((rotation - 1) % rot_count) + 1
    return def[rot_idx], PIECE_COLORS[piece_type]
end

local function can_place(piece_type, rotation, px, py)
    local cells = piece_cells(piece_type, rotation)
    for i = 1, #cells do
        local gx = px + cells[i][1]
        local gy = py + cells[i][2]
        if gx < 1 or gx > COLS or gy > ROWS then
            return false
        end
        if gy >= 1 and board[gy][gx] ~= 0 then
            return false
        end
    end
    return true
end

local function lock_piece()
    local cells, color = piece_cells(cur_type, cur_rot)
    for i = 1, #cells do
        local gx = cur_x + cells[i][1]
        local gy = cur_y + cells[i][2]
        if gy >= 1 and gy <= ROWS and gx >= 1 and gx <= COLS then
            board[gy][gx] = color
        end
    end
end

local function clear_full_lines()
    local cleared = 0
    local y = ROWS
    while y >= 1 do
        local full = true
        for x = 1, COLS do
            if board[y][x] == 0 then
                full = false
                break
            end
        end
        if full then
            table.remove(board, y)
            local row = {}
            for x = 1, COLS do
                row[x] = 0
            end
            table.insert(board, 1, row)
            cleared = cleared + 1
        else
            y = y - 1
        end
    end
    if cleared > 0 then
        local table_score = {0, 100, 300, 500, 800}
        score = score + (table_score[cleared + 1] or 800) * level
        lines_cleared = lines_cleared + cleared
        level = 1 + lines_cleared // 10
        drop_ms = 550 - (level - 1) * 40
        if drop_ms < 120 then
            drop_ms = 120
        end
    end
end

local function spawn_piece()
    cur_type = next_type
    next_type = random_piece_type()
    cur_rot = 1
    cur_x = 4
    cur_y = 1
    if not can_place(cur_type, cur_rot, cur_x, cur_y) then
        game_over = true
        blink = 0
        return false
    end
    return true
end

local function reset_game()
    reset_board()
    score = 0
    lines_cleared = 0
    level = 1
    drop_ms = 550
    drop_acc = 0
    move_repeat_ms = 0
    game_over = false
    blink = 0
    prev_pressed = {}
    spawn_count = 0
    next_type = random_piece_type()
    spawn_piece()
end

function game_init()
    reset_game()
end

local function just_pressed(btn)
    local now = machine.pressed(btn)
    local was = prev_pressed[btn]
    prev_pressed[btn] = now
    return now and not was
end

local function try_move(dx, dy)
    if can_place(cur_type, cur_rot, cur_x + dx, cur_y + dy) then
        cur_x = cur_x + dx
        cur_y = cur_y + dy
        return true
    end
    return false
end

local function try_rotate()
    local rot_count = #PIECES[cur_type]
    local next_rot = cur_rot + 1
    if next_rot > rot_count then
        next_rot = 1
    end
    local kicks = {
        {0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-2, 0}, {2, 0}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    }
    for i = 1, #kicks do
        local kx = kicks[i][1]
        local ky = kicks[i][2]
        if can_place(cur_type, next_rot, cur_x + kx, cur_y + ky) then
            cur_rot = next_rot
            cur_x = cur_x + kx
            cur_y = cur_y + ky
            return true
        end
    end
    return false
end

local function hard_drop()
    while try_move(0, 1) do
        score = score + 1
    end
    lock_piece()
    clear_full_lines()
    spawn_piece()
end

local function step_down()
    if try_move(0, 1) then
        return true
    end
    lock_piece()
    clear_full_lines()
    spawn_piece()
    return false
end

local function update_input(dt)
    move_repeat_ms = move_repeat_ms - dt

    local function move_once(dx)
        if try_move(dx, 0) then
            move_repeat_ms = 140
        else
            move_repeat_ms = 200
        end
    end

    if just_pressed(BTN_LEFT) then
        move_once(-1)
    elseif machine.pressed(BTN_LEFT) and move_repeat_ms <= 0 then
        move_once(-1)
    end

    if just_pressed(BTN_RIGHT) then
        move_once(1)
    elseif machine.pressed(BTN_RIGHT) and move_repeat_ms <= 0 then
        move_once(1)
    end

    if just_pressed(BTN_DOWN) then
        if step_down() then
            score = score + 1
        end
    elseif machine.pressed(BTN_DOWN) then
        drop_acc = drop_acc + dt * 2
    end

    for i = 1, #BTN_ROTATE do
        if just_pressed(BTN_ROTATE[i]) then
            try_rotate()
            break
        end
    end

    if just_pressed(4) then
        hard_drop()
    end
end

function game_update(dt)
    if game_over then
        blink = blink + dt
        if machine.jump_pressed() and blink > 400 then
            reset_game()
        end
        return false
    end

    update_input(dt)

    drop_acc = drop_acc + dt
    while drop_acc >= drop_ms do
        drop_acc = drop_acc - drop_ms
        step_down()
        if game_over then
            break
        end
    end

    return false
end

local function draw_cell(px, py, color)
    machine.fill_rect(px + 1, py + 1, CELL - 2, CELL - 2, color)
end

local function draw_piece_cells(cells, px, py, color, ghost)
    for i = 1, #cells do
        local gx = px + cells[i][1]
        local gy = py + cells[i][2]
        if gy >= 1 then
            local sx = BOARD_X + (gx - 1) * CELL
            local sy = BOARD_Y + (gy - 1) * CELL
            draw_cell(sx, sy, ghost and COL_GHOST or color)
        end
    end
end

local function ghost_y()
    local gy = cur_y
    while can_place(cur_type, cur_rot, cur_x, gy + 1) do
        gy = gy + 1
    end
    return gy
end

local function draw_board()
    machine.fill_rect(BOARD_X - 2, BOARD_Y - 2, BOARD_W + 4, BOARD_H + 4, COL_BORDER)
    machine.fill_rect(BOARD_X, BOARD_Y, BOARD_W, BOARD_H, COL_BG)

    for y = 1, ROWS do
        for x = 1, COLS do
            local c = board[y][x]
            if c ~= 0 then
                draw_cell(BOARD_X + (x - 1) * CELL, BOARD_Y + (y - 1) * CELL, c)
            end
        end
    end

    for x = 0, COLS do
        local lx = BOARD_X + x * CELL
        machine.fill_rect(lx, BOARD_Y, 1, BOARD_H, COL_GRID)
    end
    for y = 0, ROWS do
        local ly = BOARD_Y + y * CELL
        machine.fill_rect(BOARD_X, ly, BOARD_W, 1, COL_GRID)
    end
end

local function draw_active_piece()
    local cells, color = piece_cells(cur_type, cur_rot)
    local gy = ghost_y()
    if gy > cur_y then
        draw_piece_cells(cells, cur_x, gy, color, true)
    end
    draw_piece_cells(cells, cur_x, cur_y, color, false)
end

local function draw_next_preview()
    local panel_x = BOARD_X + BOARD_W + 12
    local panel_y = BOARD_Y + 4
    machine.text(panel_x, panel_y, "NEXT", COL_TEXT, COL_BG)
    machine.fill_rect(panel_x, panel_y + 12, 44, 44, COL_GRID)
    local cells, color = piece_cells(next_type, 1)
    local min_x, max_x = 99, -99
    local min_y, max_y = 99, -99
    for i = 1, #cells do
        local cx = cells[i][1]
        local cy = cells[i][2]
        if cx < min_x then min_x = cx end
        if cx > max_x then max_x = cx end
        if cy < min_y then min_y = cy end
        if cy > max_y then max_y = cy end
    end
    local pw = (max_x - min_x + 1) * 8
    local ph = (max_y - min_y + 1) * 8
    local ox = panel_x + (44 - pw) // 2 - min_x * 8
    local oy = panel_y + 12 + (44 - ph) // 2 - min_y * 8
    for i = 1, #cells do
        local sx = ox + cells[i][1] * 8
        local sy = oy + cells[i][2] * 8
        machine.fill_rect(sx, sy, 7, 7, color)
    end
end

local function draw_hud()
    local panel_x = BOARD_X + BOARD_W + 12
    machine.text(panel_x, BOARD_Y + 70, "SCORE", COL_TEXT, COL_BG)
    machine.text(panel_x, BOARD_Y + 82, tostring(score), COL_TEXT, COL_BG)
    machine.text(panel_x, BOARD_Y + 104, "LINES", COL_TEXT, COL_BG)
    machine.text(panel_x, BOARD_Y + 116, tostring(lines_cleared), COL_TEXT, COL_BG)
    machine.text(panel_x, BOARD_Y + 138, "LV " .. level, COL_TEXT, COL_BG)
    machine.text(BOARD_X, BOARD_Y + BOARD_H + 6, "Stack Blocks", COL_TEXT, COL_BG)
end

function game_draw()
    machine.clear(COL_BG)
    draw_board()
    if not game_over then
        draw_active_piece()
    end
    draw_next_preview()
    draw_hud()

    if game_over then
        if (blink // 300) % 2 == 0 then
            machine.text(BOARD_X + 8, BOARD_Y + 80, "GAME OVER", COL_TEXT, COL_BG)
            machine.text(BOARD_X + 4, BOARD_Y + 96, "Jump=retry", COL_TEXT, COL_BG)
        end
    end
end
