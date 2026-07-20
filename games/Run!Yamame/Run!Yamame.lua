-- Run! Yamame — 洞窟ランナー (Suityouka Game Machine)
-- SD: /games/Run!Yamame/
--   Run!Yamame.lua
--   run1.bin / run2.bin / run3.bin（各 40×40）
--   slide.bin（40×40・スライディング）
--   smoke.bin（35×35・走行／スライド煙）
--   cave.bin（320×240・左右ループ背景）
--   cave_rock.bin（31×44・地上障害物）
--   bat.bin（45×45・浮遊障害物）
--   spiderweb.bin（100×100・爆発時の巣）
--   hi_score.dat（自動生成・HI SCORE）
--
-- 操作:
--   タイトル … UP / OP_RIGHT / NEAR でスタート
--   ジャンプ … UP / OP_RIGHT / NEAR
--   スライド … DOWN
--   白い球 … FAR（画面中央やや先で爆発し、半径内の蝙蝠を消す）
--   ゲームオーバー … Jump でリトライ / OP_LEFT でタイトル

local W = machine.width()
local H = machine.height()
local GROUND_Y = H - 20
local GRAVITY = 0.55
local JUMP_VEL = -11.5
-- スクロール速度: 開始は遅く、表示スコア約 850 で従来速度に到達
local SCROLL_SPEED_MIN = 1.6
local SCROLL_SPEED_MAX = 4.0
local SPEED_RAMP_SCORE = 850
local TITLE_SCROLL = 1.2

local SAVE_PATH = "hi_score.dat"
local SAVE_VERSION = 1

local BTN_UP = 1
local BTN_DOWN = 3
local BTN_OP_LEFT = 4
local BTN_OP_RIGHT = 5
local BTN_FAR = 6
local BTN_NEAR = 7

local PLAYER_W = 40
local PLAYER_H = 40
local SLIDE_W = 40
local SLIDE_H = 40
local HIT_W = 28
local HIT_H = 34
local HIT_OX = 6
local HIT_OY = 4
local SLIDE_HIT_W = 32
local SLIDE_HIT_H = 18
local SLIDE_HIT_OX = 4
local SLIDE_HIT_OY = 20

local ROCK_W = 31
local ROCK_H = 44
local ROCK_HIT_W = 24
local ROCK_HIT_H = 38
local ROCK_HIT_OX = 3
local ROCK_HIT_OY = 4

local BAT_W = 45
local BAT_H = 45
local BAT_HIT_W = 34
local BAT_HIT_H = 28
local BAT_HIT_OX = 5
local BAT_HIT_OY = 8
local BAT_CLEARANCE = 22

local SMOKE_W = 35
local SMOKE_H = 35
local MAX_SLIDE_SMOKE = 5
local SLIDE_SMOKE_INTERVAL = 70

-- 白い球（FAR）: 画面中央やや先で爆発し、円形範囲内の蝙蝠を消す
local BALL_R = 5
local BALL_SPEED = 7.5
local BALL_EXPLODE_X = W // 2 + 28 -- 画面中央より少し先
local MAX_BALLS = 1
local BALL_CHARGE_MS = 5000 -- 満タンまで約 5 秒
local BOMB_RADIUS = 150
local BOMB_FLASH_MS = 450
local COL_BALL = machine.rgb(255, 255, 255)
local COL_BOMB = machine.rgb(140, 220, 255)
local COL_BOMB_INNER = machine.rgb(220, 250, 255)
local COL_GAUGE_BG = machine.rgb(40, 45, 55)
local COL_GAUGE_FG = machine.rgb(255, 255, 255)
local COL_GAUGE_READY = machine.rgb(180, 240, 255)

local WEB_W = 100
local WEB_H = 100

local BG_PATH = "cave.bin"
local BG_W = 320
local BG_H = 240

local COL_SKY = machine.rgb(247, 247, 247)
local COL_GROUND = machine.rgb(83, 83, 83)
local COL_DINO = machine.rgb(40, 40, 40)
local COL_CACTUS = machine.rgb(20, 120, 20)
local COL_TEXT = machine.rgb(60, 60, 60)
local COL_HUD = machine.rgb(240, 240, 245)
local COL_HUD_BG = machine.rgb(20, 20, 30)
local COL_TITLE = machine.rgb(255, 220, 90)
local COL_ACCENT = machine.rgb(180, 220, 140)
local COL_DIM = machine.rgb(160, 170, 180)

local RUN_CYCLE = { 1, 2, 3, 2 }
local RUN_FRAME_MS = 110
local RUN_SMOKE_EVERY_CYCLES = 2
local KIND_ROCK = 1
local KIND_BAT = 2

local mode = "title" -- title | play | gameover
local player_x = 48
local player_y = 0
local player_vy = 0
local on_ground = true
local sliding = false
local was_sliding = false
local obstacles = {}
local spawn_cd = 0
local score = 0
local hi_score = 0
local new_record = false
local blink = 0
local bg_scroll = 0
local bg_id = nil
local spawn_serial = 0

local run_ids = {}
local rock_id = nil
local bat_id = nil
local slide_id = nil
local smoke_id = nil
local web_id = nil
local anim_timer = 0
local anim_step = 1
local run_cycle_count = 0

local smokes = {}
local slide_smoke_cd = 0
local balls = {}
local bomb_fx = {}
local webs = {}
local ball_charge = BALL_CHARGE_MS -- 開始時満タン
local prev_op_left = false
local prev_far = false

local function display_score(raw)
    return math.floor(raw / 50)
end

local function current_scroll_speed()
    local sc = display_score(score)
    local t = sc / SPEED_RAMP_SCORE
    if t < 0 then
        t = 0
    elseif t > 1 then
        t = 1
    end
    return SCROLL_SPEED_MIN + (SCROLL_SPEED_MAX - SCROLL_SPEED_MIN) * t
end

local function jump_input()
    return machine.pressed(BTN_UP)
        or machine.pressed(BTN_OP_RIGHT)
        or machine.pressed(BTN_NEAR)
end

local function slide_input()
    return machine.pressed(BTN_DOWN)
end

local function confirm_input()
    return jump_input()
end

local function clear_balls()
    balls = {}
    bomb_fx = {}
    webs = {}
end

local function text_colors()
    if bg_id then
        return COL_HUD, COL_HUD_BG
    end
    return COL_TEXT, COL_SKY
end

local function draw_center_text(y, text, fg, bg)
    local tw = #text * 8
    local x = (W - tw) // 2
    if x < 0 then
        x = 0
    end
    if bg then
        machine.text(x, y, text, fg, bg)
    else
        machine.text(x, y, text, fg)
    end
end

local function load_hi_score()
    hi_score = 0
    if not machine.file_exists(SAVE_PATH) then
        return
    end
    local data = machine.load_data(SAVE_PATH)
    if type(data) ~= "table" then
        return
    end
    local hs = tonumber(data.hi_score)
    if hs then
        hi_score = math.max(0, math.floor(hs))
    end
end

local function save_hi_score()
    machine.save_data(SAVE_PATH, {
        version = SAVE_VERSION,
        hi_score = hi_score,
    })
end

local function try_record_hi_score()
    local sc = display_score(score)
    if sc > hi_score then
        hi_score = sc
        new_record = true
        save_hi_score()
        return true
    end
    new_record = false
    return false
end

local function clear_smokes()
    smokes = {}
    slide_smoke_cd = 0
    run_cycle_count = 0
end

local function reset_play()
    player_y = GROUND_Y - PLAYER_H
    player_vy = 0
    on_ground = true
    sliding = false
    was_sliding = false
    obstacles = {}
    spawn_cd = 900
    score = 0
    new_record = false
    blink = 0
    anim_timer = 0
    anim_step = 1
    bg_scroll = 0
    spawn_serial = 0
    clear_smokes()
    clear_balls()
    ball_charge = BALL_CHARGE_MS
end

local function enter_title()
    mode = "title"
    blink = 0
    sliding = false
    obstacles = {}
    clear_smokes()
    clear_balls()
    player_y = GROUND_Y - PLAYER_H
    on_ground = true
    anim_timer = 0
    anim_step = 1
end

local function enter_play()
    reset_play()
    mode = "play"
    prev_far = false
end

local function enter_gameover()
    mode = "gameover"
    blink = 0
    sliding = false
    try_record_hi_score()
end

function game_init()
    bg_id = machine.load_image(BG_PATH, BG_W, BG_H)
    if not bg_id then
        print("cave.bin load failed — plain sky background")
    end

    run_ids = {}
    for i = 1, 3 do
        local id = machine.load_image("run" .. i .. ".bin", PLAYER_W, PLAYER_H)
        if id then
            run_ids[i] = id
        else
            print("run" .. i .. ".bin load failed")
        end
    end

    rock_id = machine.load_image("cave_rock.bin", ROCK_W, ROCK_H)
    if not rock_id then
        print("cave_rock.bin load failed")
    end

    bat_id = machine.load_image("bat.bin", BAT_W, BAT_H)
    if not bat_id then
        print("bat.bin load failed")
    end

    slide_id = machine.load_image("slide.bin", SLIDE_W, SLIDE_H)
    if not slide_id then
        print("slide.bin load failed")
    end

    smoke_id = machine.load_image("smoke.bin", SMOKE_W, SMOKE_H)
    if not smoke_id then
        print("smoke.bin load failed")
    end

    web_id = machine.load_image("spiderweb.bin", WEB_W, WEB_H)
    if not web_id then
        print("spiderweb.bin load failed")
    end

    load_hi_score()
    enter_title()
    prev_op_left = false
    prev_far = false
end

local function rects_overlap(ax, ay, aw, ah, bx, by, bw, bh)
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by
end

local function spawn_obstacle()
    spawn_serial = spawn_serial + 1
    local kind = (spawn_serial % 2 == 0) and KIND_BAT or KIND_ROCK
    if kind == KIND_BAT then
        obstacles[#obstacles + 1] = {
            kind = KIND_BAT,
            x = W + 10,
            w = BAT_W,
            h = BAT_H,
            y = GROUND_Y - BAT_H - BAT_CLEARANCE,
        }
    else
        obstacles[#obstacles + 1] = {
            kind = KIND_ROCK,
            x = W + 10,
            w = ROCK_W,
            h = ROCK_H,
            y = GROUND_Y - ROCK_H,
        }
    end
end

local function spawn_ball()
    if ball_charge < BALL_CHARGE_MS then
        return false
    end
    if #balls >= MAX_BALLS then
        return false
    end
    local py = math.floor(player_y)
    local cy
    if sliding then
        cy = GROUND_Y - SLIDE_H + SLIDE_H * 0.55
    else
        cy = py + PLAYER_H * 0.45
    end
    balls[#balls + 1] = {
        x = player_x + PLAYER_W - 4,
        y = cy,
    }
    ball_charge = 0
    return true
end

local function clear_bats_in_radius(cx, cy, radius)
    local r2 = radius * radius
    for i = #obstacles, 1, -1 do
        local o = obstacles[i]
        if o.kind == KIND_BAT then
            local bx = o.x + o.w * 0.5
            local by = o.y + o.h * 0.5
            local dx = bx - cx
            local dy = by - cy
            if dx * dx + dy * dy <= r2 then
                table.remove(obstacles, i)
            end
        end
    end
end

local function detonate_at(cx, cy)
    clear_bats_in_radius(cx, cy, BOMB_RADIUS)
    bomb_fx[#bomb_fx + 1] = {
        x = cx,
        y = cy,
        flash = BOMB_FLASH_MS,
    }
    webs[#webs + 1] = {
        x = cx - WEB_W * 0.5,
        y = cy - WEB_H * 0.5,
    }
    if machine.play_tone then
        machine.play_tone(280, 90)
    end
end

local function update_balls(dt)
    local step = BALL_SPEED * dt / 16.0
    for i = #balls, 1, -1 do
        local b = balls[i]
        b.x = b.x + step
        if b.x >= BALL_EXPLODE_X then
            detonate_at(b.x, b.y)
            table.remove(balls, i)
        end
    end
end

local function update_bomb_fx(dt)
    for i = #bomb_fx, 1, -1 do
        local fx = bomb_fx[i]
        fx.flash = fx.flash - dt
        if fx.flash <= 0 then
            table.remove(bomb_fx, i)
        end
    end
end

local function update_webs(dt)
    local scroll = current_scroll_speed() * dt / 16.0
    for i = #webs, 1, -1 do
        local w = webs[i]
        w.x = w.x - scroll
        if w.x + WEB_W < -4 then
            table.remove(webs, i)
        end
    end
end

local function draw_balls()
    for i = 1, #balls do
        local b = balls[i]
        machine.fill_circle(math.floor(b.x), math.floor(b.y), BALL_R, COL_BALL)
    end
end

local function draw_bomb_fx()
    for i = 1, #bomb_fx do
        local fx = bomb_fx[i]
        local t = fx.flash / BOMB_FLASH_MS
        if t < 0 then
            t = 0
        end
        local cx = math.floor(fx.x)
        local cy = math.floor(fx.y)
        local r = math.floor((1.0 - t) * BOMB_RADIUS) + 8
        machine.draw_circle(cx, cy, r, COL_BOMB)
        machine.draw_circle(cx, cy, math.max(4, r - 12), COL_BOMB_INNER)
    end
end

local function draw_webs()
    if not web_id then
        return
    end
    for i = 1, #webs do
        local w = webs[i]
        if w.x < W and w.x + WEB_W > 0 then
            machine.draw_image_keyed(web_id, math.floor(w.x), math.floor(w.y), 0xF81F)
        end
    end
end

local function spawn_run_smoke()
    if not smoke_id then
        return
    end
    local py = math.floor(player_y)
    smokes[#smokes + 1] = {
        kind = "run",
        x = player_x - 10,
        y = py + PLAYER_H - SMOKE_H + 4,
    }
end

local function count_slide_smokes()
    local n = 0
    for i = 1, #smokes do
        if smokes[i].kind == "slide" then
            n = n + 1
        end
    end
    return n
end

local function remove_oldest_slide_smoke()
    for i = 1, #smokes do
        if smokes[i].kind == "slide" then
            table.remove(smokes, i)
            return
        end
    end
end

local function spawn_slide_smoke()
    if not smoke_id then
        return
    end
    while count_slide_smokes() >= MAX_SLIDE_SMOKE do
        remove_oldest_slide_smoke()
    end
    smokes[#smokes + 1] = {
        kind = "slide",
        x = player_x - 6,
        y = GROUND_Y - 22,
    }
end

local function update_run_anim(dt)
    -- プレイ中の空中／スライドでは固定姿勢。タイトルは常に走行ループ
    if mode == "play" and (sliding or not on_ground) then
        return
    end
    if mode == "gameover" then
        return
    end
    anim_timer = anim_timer + dt
    while anim_timer >= RUN_FRAME_MS do
        anim_timer = anim_timer - RUN_FRAME_MS
        anim_step = anim_step + 1
        if anim_step > #RUN_CYCLE then
            anim_step = 1
            if mode == "play" then
                run_cycle_count = run_cycle_count + 1
                if run_cycle_count >= RUN_SMOKE_EVERY_CYCLES then
                    spawn_run_smoke()
                    run_cycle_count = 0
                end
            end
        end
    end
end

local function current_run_frame()
    local frame = RUN_CYCLE[anim_step] or 1
    if mode == "play" and not on_ground then
        frame = 2
    end
    return frame
end

local function current_run_id()
    return run_ids[current_run_frame()]
end

local function update_smokes(dt)
    local scroll = current_scroll_speed() * dt / 16.0
    for i = #smokes, 1, -1 do
        local s = smokes[i]
        s.x = s.x - scroll
        if s.x + SMOKE_W * 2 < -8 then
            table.remove(smokes, i)
        end
    end

    if sliding then
        slide_smoke_cd = slide_smoke_cd - dt
        if slide_smoke_cd <= 0 then
            spawn_slide_smoke()
            slide_smoke_cd = SLIDE_SMOKE_INTERVAL
        end
    else
        slide_smoke_cd = 0
    end
end

local function draw_background()
    if bg_id then
        local ox = -(math.floor(bg_scroll) % BG_W)
        machine.draw_image(bg_id, ox, 0)
        machine.draw_image(bg_id, ox + BG_W, 0)
    else
        machine.clear(COL_SKY)
        machine.fill_rect(0, GROUND_Y, W, H - GROUND_Y, COL_GROUND)
        machine.fill_rect(0, GROUND_Y, W, 3, COL_TEXT)
    end
end

local function draw_smokes()
    if not smoke_id then
        return
    end
    local px_anchor = player_x + PLAYER_W * 0.5
    for i = 1, #smokes do
        local s = smokes[i]
        local sx = math.floor(s.x)
        local sy = math.floor(s.y)
        if s.kind == "slide" then
            -- draw_image_xform は lua_preview で不安定なため、アフィンを数値で直接渡す
            local dist = px_anchor - (s.x + SMOKE_W * 0.5)
            if dist < 0 then
                dist = 0
            end
            local t = dist / 90.0
            if t > 1 then
                t = 1
            end
            local sc = 0.35 + t * 1.15
            local ox = SMOKE_W * 0.5
            local oy = SMOKE_H * 0.5
            local cx = s.x + ox
            local cy = s.y + oy
            -- T(cx,cy) * S(sc) * T(-ox,-oy)
            local a = sc
            local e = sc
            local c = cx - a * ox
            local f = cy - e * oy
            machine.draw_image_affine(smoke_id, a, 0, c, 0, e, f, true)
        else
            machine.draw_image_keyed(smoke_id, sx, sy, 0xF81F)
        end
    end
end

local function draw_player()
    if sliding then
        local sy = GROUND_Y - SLIDE_H
        if slide_id then
            machine.draw_image_keyed(slide_id, player_x, sy, 0xF81F)
        elseif run_ids[1] then
            -- フォールバック: run1 の下半分（API: id,x,y,sx,sy,sw,sh,key）
            machine.draw_image_keyed(run_ids[1], player_x, sy, 0, PLAYER_H // 2, PLAYER_W, PLAYER_H // 2, 0xF81F)
        else
            machine.fill_rect(player_x, sy, SLIDE_W, SLIDE_H, COL_DINO)
        end
    else
        local py = math.floor(player_y)
        local id = current_run_id()
        if id then
            machine.draw_image_keyed(id, player_x, py, 0xF81F)
        else
            machine.fill_rect(player_x, py, PLAYER_W, PLAYER_H, COL_DINO)
        end
    end
end

local function draw_obstacles()
    for i = 1, #obstacles do
        local o = obstacles[i]
        local ox = math.floor(o.x)
        local oy = math.floor(o.y)
        if o.kind == KIND_BAT then
            if bat_id then
                machine.draw_image_keyed(bat_id, ox, oy, 0xF81F)
            else
                machine.fill_rect(ox, oy, o.w, o.h, machine.rgb(80, 60, 120))
            end
        else
            if rock_id then
                machine.draw_image_keyed(rock_id, ox, oy, 0xF81F)
            else
                machine.fill_rect(ox, oy, o.w, o.h, COL_CACTUS)
            end
        end
    end
end

local function player_hitbox()
    if sliding then
        local py = GROUND_Y - SLIDE_H
        return player_x + SLIDE_HIT_OX, py + SLIDE_HIT_OY, SLIDE_HIT_W, SLIDE_HIT_H
    end
    return player_x + HIT_OX, math.floor(player_y) + HIT_OY, HIT_W, HIT_H
end

local function draw_title()
    draw_background()
    draw_player()

    local fg, bg = text_colors()
    -- タイトル帯
    machine.fill_rect(0, 36, W, 52, COL_HUD_BG)
    draw_center_text(44, "Run!Yamame", COL_TITLE, COL_HUD_BG)
    draw_center_text(64, "CAVE RUNNER", COL_ACCENT, COL_HUD_BG)

    draw_center_text(108, "HI SCORE  " .. tostring(hi_score), COL_TITLE, bg)

    if (blink // 450) % 2 == 0 then
        draw_center_text(140, "PRESS TO START", fg, bg)
    end
    draw_center_text(168, "UP/NEAR: JUMP", COL_DIM, bg)
    draw_center_text(184, "DOWN: SLIDE  FAR: SHOT", COL_DIM, bg)
    draw_center_text(220, "2026 Kawashiro Electric", COL_DIM, bg)
end

local function draw_play_hud()
    local fg, bg = text_colors()
    local sc = display_score(score)

    -- 左上: 白い球チャージゲージ
    local gx, gy, gw, gh = 8, 6, 72, 8
    machine.fill_rect(gx - 1, gy - 1, gw + 2, gh + 2, COL_HUD_BG)
    machine.fill_rect(gx, gy, gw, gh, COL_GAUGE_BG)
    local fill = math.floor(gw * (ball_charge / BALL_CHARGE_MS))
    if fill < 0 then
        fill = 0
    elseif fill > gw then
        fill = gw
    end
    if fill > 0 then
        local col = (ball_charge >= BALL_CHARGE_MS) and COL_GAUGE_READY or COL_GAUGE_FG
        machine.fill_rect(gx, gy, fill, gh, col)
    end
    machine.text(gx + gw + 4, gy - 1, "WEB", fg, bg)

    machine.text(8, 20, "SC:" .. sc, fg, bg)
    machine.text(8, 34, "HI:" .. hi_score, fg, bg)
end

local function draw_gameover()
    draw_background()
    draw_smokes()
    draw_webs()
    draw_obstacles()
    draw_balls()
    draw_bomb_fx()
    draw_player()

    local fg, bg = text_colors()
    local sc = display_score(score)
    machine.text(8, 8, "SC:" .. sc, fg, bg)
    machine.text(8, 22, "HI:" .. hi_score, fg, bg)

    if (blink // 300) % 2 == 0 then
        draw_center_text(H // 2 - 28, "GAME OVER", fg, bg)
        if new_record then
            draw_center_text(H // 2 - 8, "NEW RECORD!", COL_TITLE, bg)
        end
        draw_center_text(H // 2 + 12, "Jump: retry", fg, bg)
        draw_center_text(H // 2 + 28, "OP_L: title", fg, bg)
    end
end

local function update_title(dt)
    blink = blink + dt
    bg_scroll = bg_scroll + TITLE_SCROLL * dt / 16.0
    update_run_anim(dt)

    if confirm_input() and blink > 350 then
        enter_play()
    end
end

local function update_gameover(dt)
    blink = blink + dt
    local op_left = machine.pressed(BTN_OP_LEFT)
    if op_left and not prev_op_left and blink > 300 then
        enter_title()
    elseif jump_input() and blink > 400 then
        enter_play()
    end
    prev_op_left = op_left
end

local function update_play(dt)
    score = score + dt
    local scroll_spd = current_scroll_speed()
    bg_scroll = bg_scroll + scroll_spd * dt / 16.0
    update_run_anim(dt)

    was_sliding = sliding
    if on_ground and slide_input() and not jump_input() then
        sliding = true
        player_y = GROUND_Y - SLIDE_H
        player_vy = 0
    else
        if sliding and on_ground then
            player_y = GROUND_Y - PLAYER_H
        end
        sliding = false
    end

    if jump_input() and on_ground and not slide_input() then
        player_vy = JUMP_VEL
        on_ground = false
        sliding = false
        player_y = GROUND_Y - PLAYER_H
    end

    if not sliding then
        player_vy = player_vy + GRAVITY * dt / 16.0
        player_y = player_y + player_vy * dt / 16.0

        local floor = GROUND_Y - PLAYER_H
        if player_y >= floor then
            player_y = floor
            player_vy = 0
            on_ground = true
        else
            on_ground = false
        end
    else
        on_ground = true
        player_vy = 0
        player_y = GROUND_Y - SLIDE_H
    end

    if sliding and not was_sliding then
        spawn_slide_smoke()
        slide_smoke_cd = SLIDE_SMOKE_INTERVAL
    end

    if ball_charge < BALL_CHARGE_MS then
        ball_charge = ball_charge + dt
        if ball_charge > BALL_CHARGE_MS then
            ball_charge = BALL_CHARGE_MS
        end
    end

    if machine.pressed(BTN_FAR) and not prev_far then
        spawn_ball()
    end

    update_smokes(dt)
    update_balls(dt)
    update_bomb_fx(dt)
    update_webs(dt)

    spawn_cd = spawn_cd - dt
    if spawn_cd <= 0 then
        spawn_obstacle()
        spawn_cd = 1500 - score * 0.12
        if spawn_cd < 900 then
            spawn_cd = 900
        end
    end

    local px, py, pw, ph = player_hitbox()

    for i = #obstacles, 1, -1 do
        local o = obstacles[i]
        o.x = o.x - scroll_spd * dt / 16.0
        local ox = math.floor(o.x)
        local oy = math.floor(o.y)
        local hit = false
        if o.kind == KIND_BAT then
            if not sliding then
                hit = rects_overlap(px, py, pw, ph,
                    ox + BAT_HIT_OX, oy + BAT_HIT_OY, BAT_HIT_W, BAT_HIT_H)
            end
        else
            hit = rects_overlap(px, py, pw, ph,
                ox + ROCK_HIT_OX, oy + ROCK_HIT_OY, ROCK_HIT_W, ROCK_HIT_H)
        end
        if hit then
            enter_gameover()
        end
        if o.x + o.w < -20 then
            table.remove(obstacles, i)
        end
    end
end

function game_update(dt)
    if mode == "title" then
        update_title(dt)
        prev_op_left = machine.pressed(BTN_OP_LEFT)
        prev_far = machine.pressed(BTN_FAR)
        return false
    end
    if mode == "gameover" then
        update_gameover(dt)
        prev_far = machine.pressed(BTN_FAR)
        return false
    end

    update_play(dt)
    prev_op_left = machine.pressed(BTN_OP_LEFT)
    prev_far = machine.pressed(BTN_FAR)
    return false
end

function game_draw()
    if mode == "title" then
        draw_title()
        return
    end
    if mode == "gameover" then
        draw_gameover()
        return
    end

    draw_background()
    draw_smokes()
    draw_webs()
    draw_obstacles()
    draw_balls()
    draw_bomb_fx()
    draw_player()
    draw_play_hud()
end
