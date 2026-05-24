-- 弾幕STG (Suityouka Game Machine)
-- SDカードルートに stg.lua として配置
-- 移動: 十字キー / 射撃: OP_RIGHT など (machine.jump_pressed)
-- リトライ: ゲームオーバー後ジャンプ

local W = machine.width()
local H = machine.height()

local COL_BG = machine.rgb(8, 12, 32)
local COL_STAR = machine.rgb(40, 50, 90)
local COL_PLAYER = machine.rgb(0, 220, 255)
local COL_PBULLET = machine.rgb(255, 240, 80)
local COL_EBULLET = machine.rgb(255, 60, 120)
local COL_ENEMY = machine.rgb(180, 60, 255)
local COL_TEXT = machine.rgb(200, 220, 255)

local PLAYER_W = 14
local PLAYER_H = 14
local PB_W = 4
local PB_H = 8
local EB_W = 5
local EB_H = 5

-- 弾速: dt はミリ秒（dino.lua と同じスケール）
local PB_SPEED = 0.42
local EB_SPEED = 0.10
local ENEMY_MOVE_SPEED = 0.08

local MAX_PBULLETS = 20
local MAX_EBULLETS = 40
local MAX_ENEMIES = 4

local player_x = 0
local player_y = 0
local pbullets = {}
local ebullets = {}
local enemies = {}
local shoot_cd = 0
local spawn_cd = 0
local score = 0
local game_over = false
local blink = 0
local next_enemy_id = 0

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function rects_overlap(ax, ay, aw, ah, bx, by, bw, bh)
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by
end

local function reset_game()
    player_x = (W - PLAYER_W) // 2
    player_y = H - PLAYER_H - 16
    pbullets = {}
    ebullets = {}
    enemies = {}
    shoot_cd = 0
    spawn_cd = 800
    score = 0
    game_over = false
    blink = 0
    next_enemy_id = 0
end

function game_init()
    reset_game()
end

local function spawn_enemy()
    if #enemies >= MAX_ENEMIES then
        return
    end
    next_enemy_id = next_enemy_id + 1
    local w = 24
    local h = 18
    enemies[#enemies + 1] = {
        x = 24 + (next_enemy_id * 61) % (W - w - 48),
        y = 28,
        w = w,
        h = h,
        dir = (next_enemy_id % 2 == 0) and 1 or -1,
        shoot_cd = 500,
        pattern = next_enemy_id % 2,
    }
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

local function fire_enemy_shot(e)
    local cx = math.floor(e.x + e.w // 2)
    local cy = math.floor(e.y + e.h)

    if e.pattern == 0 then
        if #ebullets < MAX_EBULLETS then
            ebullets[#ebullets + 1] = { x = cx - EB_W // 2, y = cy, vx = 0, vy = 1 }
        end
    else
        if #ebullets < MAX_EBULLETS - 2 then
            ebullets[#ebullets + 1] = { x = cx - EB_W // 2 - 8, y = cy, vx = -1, vy = 1 }
            ebullets[#ebullets + 1] = { x = cx - EB_W // 2, y = cy, vx = 0, vy = 1 }
            ebullets[#ebullets + 1] = { x = cx - EB_W // 2 + 8, y = cy, vx = 1, vy = 1 }
        end
    end
end

local function update_player(dt)
    local speed = 0.22 * dt
    if machine.pressed(2) then
        player_x = player_x - speed
    end
    if machine.pressed(0) then
        player_x = player_x + speed
    end
    if machine.pressed(1) then
        player_y = player_y - speed
    end
    if machine.pressed(3) then
        player_y = player_y + speed
    end

    player_x = clamp(player_x, 4, W - PLAYER_W - 4)
    player_y = clamp(player_y, H // 2, H - PLAYER_H - 8)

    shoot_cd = shoot_cd - dt
    if (machine.pressed(6)) and shoot_cd <= 0 then
        fire_player_bullet()
        shoot_cd = 120
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
        if b.y > H + 8 or b.x < -12 or b.x > W + 8 or b.y < -12 then
            table.remove(ebullets, i)
        end
    end
end

local function update_enemies(dt)
    local move = ENEMY_MOVE_SPEED * dt
    for i = #enemies, 1, -1 do
        local e = enemies[i]
        e.x = e.x + e.dir * move
        if e.x < 8 then
            e.x = 8
            e.dir = 1
        elseif e.x + e.w > W - 8 then
            e.x = W - 8 - e.w
            e.dir = -1
        end

        e.shoot_cd = e.shoot_cd - dt
        if e.shoot_cd <= 0 then
            fire_enemy_shot(e)
            e.shoot_cd = 900
        end
    end
end

local function check_player_hit()
    local px = math.floor(player_x)
    local py = math.floor(player_y)
    for i = 1, #ebullets do
        local b = ebullets[i]
        if rects_overlap(px, py, PLAYER_W, PLAYER_H, math.floor(b.x), math.floor(b.y), EB_W, EB_H) then
            return true
        end
    end
    for i = 1, #enemies do
        local e = enemies[i]
        if rects_overlap(px, py, PLAYER_W, PLAYER_H, math.floor(e.x), math.floor(e.y), e.w, e.h) then
            return true
        end
    end
    return false
end

local function check_shots_hit_enemies()
    for j = #enemies, 1, -1 do
        local e = enemies[j]
        local ex = math.floor(e.x)
        local ey = math.floor(e.y)
        for i = #pbullets, 1, -1 do
            local b = pbullets[i]
            local bx = math.floor(b.x)
            local by = math.floor(b.y)
            -- 当たり判定を少し広げる
            if rects_overlap(bx - 1, by - 1, PB_W + 2, PB_H + 2, ex - 1, ey - 1, e.w + 2, e.h + 2) then
                table.remove(pbullets, i)
                table.remove(enemies, j)
                score = score + 100
                break
            end
        end
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

    update_player(dt)
    update_pbullets(dt)
    update_enemies(dt)
    update_ebullets(dt)
    check_shots_hit_enemies()

    if check_player_hit() then
        game_over = true
        blink = 0
        return false
    end

    spawn_cd = spawn_cd - dt
    if spawn_cd <= 0 then
        spawn_enemy()
        spawn_cd = 1500 - score // 25
        if spawn_cd < 700 then
            spawn_cd = 700
        end
    end

    return false
end

function game_draw()
    -- dino.lua と同様: 毎フレーム全画面クリアしてから描画（残像防止）
    machine.clear(COL_BG)

    machine.fill_rect(40, 30, 2, 2, COL_STAR)
    machine.fill_rect(120, 55, 2, 2, COL_STAR)
    machine.fill_rect(200, 25, 2, 2, COL_STAR)
    machine.fill_rect(280, 70, 2, 2, COL_STAR)

    for i = 1, #ebullets do
        local b = ebullets[i]
        machine.fill_rect(math.floor(b.x), math.floor(b.y), EB_W, EB_H, COL_EBULLET)
    end

    for i = 1, #pbullets do
        local b = pbullets[i]
        machine.fill_rect(math.floor(b.x), math.floor(b.y), PB_W, PB_H, COL_PBULLET)
    end

    for i = 1, #enemies do
        local e = enemies[i]
        machine.fill_rect(math.floor(e.x), math.floor(e.y), e.w, e.h, COL_ENEMY)
    end

    machine.fill_rect(math.floor(player_x), math.floor(player_y), PLAYER_W, PLAYER_H, COL_PLAYER)

    local sc = math.floor(score / 100)
    machine.text(8, 8, "SC:" .. sc, COL_TEXT, COL_BG)

    if game_over then
        if (blink // 300) % 2 == 0 then
            machine.text(64, 100, "GAME OVER", COL_TEXT, COL_BG)
            machine.text(36, 118, "Jump to retry", COL_TEXT, COL_BG)
        end
    else
        machine.text(8, 20, "Move:D-pad Fire:OP", COL_TEXT, COL_BG)
    end
end
