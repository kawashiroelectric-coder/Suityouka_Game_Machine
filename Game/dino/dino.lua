-- Chrome Dino 風ランナー (Suityouka Game Machine)
-- SDカードルートに dino.lua として配置
-- ジャンプ: UP / RIGHT / OP_RIGHT / NEAR など (machine.jump_pressed)

local W = machine.width()
local H = machine.height()
local GROUND_Y = H - 48
local GRAVITY = 0.55
local JUMP_VEL = -11.5
local SCROLL_SPEED = 4

local COL_SKY = machine.rgb(247, 247, 247)
local COL_GROUND = machine.rgb(83, 83, 83)
local COL_DINO = machine.rgb(40, 40, 40)
local COL_CACTUS = machine.rgb(20, 120, 20)
local COL_TEXT = machine.rgb(60, 60, 60)

local player_x = 48
local player_y = 0
local player_vy = 0
local on_ground = true
local obstacles = {}
local spawn_cd = 0
local score = 0
local game_over = false
local blink = 0

local function reset_game()
    player_y = GROUND_Y - 36
    player_vy = 0
    on_ground = true
    obstacles = {}
    spawn_cd = 40
    score = 0
    game_over = false
    blink = 0
end

function game_init()
    reset_game()
end

local function rects_overlap(ax, ay, aw, ah, bx, by, bw, bh)
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by
end

local function spawn_obstacle()
    local h = 28 + (score % 3) * 8
    local w = 14 + (score % 2) * 6
    obstacles[#obstacles + 1] = { x = W + 10, w = w, h = h }
end

function game_update(dt)
    if game_over then
        blink = blink + dt
        if machine.jump_pressed() and blink > 400 then
            reset_game()
        end
        return false
    end

    score = score + dt

    if machine.jump_pressed() and on_ground then
        player_vy = JUMP_VEL
        on_ground = false
    end

    player_vy = player_vy + GRAVITY * dt / 16.0
    player_y = player_y + player_vy * dt / 16.0

    local floor = GROUND_Y - 36
    if player_y >= floor then
        player_y = floor
        player_vy = 0
        on_ground = true
    end

    spawn_cd = spawn_cd - dt
    if spawn_cd <= 0 then
        spawn_obstacle()
        spawn_cd = 900 - score * 0.3
        if spawn_cd < 350 then spawn_cd = 350 end
    end

    local px = player_x
    local py = player_y
    local pw = 32
    local ph = 36

    for i = #obstacles, 1, -1 do
        local o = obstacles[i]
        o.x = o.x - SCROLL_SPEED * dt / 16.0
        local ox = math.floor(o.x)
        local oy = GROUND_Y - o.h
        if rects_overlap(px, py, pw, ph, ox, oy, o.w, o.h) then
            game_over = true
            blink = 0
        end
        if o.x + o.w < -20 then
            table.remove(obstacles, i)
        end
    end

    return false
end

function game_draw()
    machine.clear(COL_SKY)
    machine.fill_rect(0, GROUND_Y, W, H - GROUND_Y, COL_GROUND)
    machine.fill_rect(0, GROUND_Y, W, 3, COL_TEXT)

    for i = 1, #obstacles do
        local o = obstacles[i]
        local ox = math.floor(o.x)
        local oy = GROUND_Y - o.h
        machine.fill_rect(ox, oy, o.w, o.h, COL_CACTUS)
    end

    machine.fill_rect(player_x, math.floor(player_y), 32, 36, COL_DINO)
    machine.fill_rect(player_x + 22, math.floor(player_y) + 6, 8, 8, COL_SKY)

    local sc = math.floor(score / 50)
    machine.text(8, 8, "SC:" .. sc, COL_TEXT, COL_SKY)

    if game_over then
        if (blink // 300) % 2 == 0 then
            machine.text(70, 100, "GAME OVER", COL_TEXT, COL_SKY)
            machine.text(40, 120, "Jump to retry", COL_TEXT, COL_SKY)
        end
    else
        machine.text(8, 22, "Jump=UP/OP", COL_TEXT, COL_SKY)
    end
end
