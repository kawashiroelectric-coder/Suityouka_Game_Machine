-- セーブ／ロード API テストゲーム
-- SD: /save_test/save_test.lua
-- 機能: コイン在庫 / セーブ位置ゴースト / スロット A・B / version マイグレーション

local W = machine.width()
local H = machine.height()

local SAVE_VERSION = 2
local SAVE_PATHS = { "save_a.dat", "save_b.dat" }

local BTN_LEFT = 2
local BTN_RIGHT = 0
local BTN_UP = 1
local BTN_DOWN = 3
local BTN_OP_LEFT = 4
local BTN_OP_RIGHT = 5
local BTN_FAR = 6

local COL_BG = machine.rgb(16, 24, 48)
local COL_PLAYER = machine.rgb(80, 220, 120)
local COL_GHOST = machine.rgb(40, 100, 70)
local COL_COIN = machine.rgb(255, 210, 60)
local COL_TEXT = machine.rgb(220, 230, 255)
local COL_HINT = machine.rgb(140, 150, 180)
local COL_OK = machine.rgb(100, 255, 140)
local COL_ERR = machine.rgb(255, 100, 100)
local COL_SLOT = machine.rgb(120, 180, 255)

local PLAYER_SIZE = 16
local COIN_SIZE = 10
local MOVE_SPEED = 0.12
local GHOST_BLINK_MS = 280

-- 固定配置のコイン（id はセーブの coins_taken キー）
local COIN_DEFS = {
    { id = 1, x = 36, y = 72 },
    { id = 2, x = 150, y = 56 },
    { id = 3, x = 260, y = 90 },
    { id = 4, x = 80, y = 160 },
    { id = 5, x = 220, y = 170 },
    { id = 6, x = 160, y = 120 },
}

local player_x = 0
local player_y = 0
local score = 0
local save_count = 0
local inventory = { coins = 0 }
local coins_taken = {}
local active_slot = 1
local ghost = { active = false, x = 0, y = 0 }

local status_msg = ""
local status_color = COL_TEXT
local status_until = 0
local prev_op_left = false
local prev_op_right = false
local prev_far = false

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

local function set_status(msg, color, now_ms)
    status_msg = msg
    status_color = color or COL_TEXT
    status_until = now_ms + 2000
end

local function slot_name()
    return (active_slot == 1) and "A" or "B"
end

local function save_path()
    return SAVE_PATHS[active_slot]
end

local function set_ghost(x, y)
    ghost.active = true
    ghost.x = math.floor(x + 0.5)
    ghost.y = math.floor(y + 0.5)
end

local function coin_is_taken(id)
    return coins_taken[id] == true or coins_taken[tostring(id)] == true
end

local function mark_coin_taken(id)
    coins_taken[id] = true
end

-- v1（version 無し）→ v2 への補完
local function migrate_save_data(data)
    local version = tonumber(data.version) or 1
    if version < 1 then
        version = 1
    end

    if type(data.inventory) ~= "table" then
        data.inventory = { coins = 0 }
    else
        data.inventory.coins = tonumber(data.inventory.coins) or 0
    end

    if type(data.coins_taken) ~= "table" then
        data.coins_taken = {}
    end

    -- 旧セーブで inventory が無い場合、taken 数と coins を揃える
    if version < 2 then
        local taken_n = 0
        for k, v in pairs(data.coins_taken) do
            if v then
                taken_n = taken_n + 1
            end
            -- 文字列キーを数値キーへ寄せる
            local id = tonumber(k)
            if id then
                data.coins_taken[id] = true
            end
        end
        if data.inventory.coins == 0 and taken_n > 0 then
            data.inventory.coins = taken_n
        end
        data.version = SAVE_VERSION
    end

    return data
end

local function apply_save_data(data)
    if type(data) ~= "table" then
        return false
    end
    data = migrate_save_data(data)

    player_x = clamp(tonumber(data.x) or player_x, 0, W - PLAYER_SIZE)
    player_y = clamp(tonumber(data.y) or player_y, 0, H - PLAYER_SIZE)
    score = tonumber(data.score) or score
    save_count = tonumber(data.save_count) or save_count

    inventory.coins = tonumber(data.inventory.coins) or 0
    coins_taken = {}
    for k, v in pairs(data.coins_taken) do
        if v then
            local id = tonumber(k) or k
            coins_taken[id] = true
        end
    end

    set_ghost(player_x, player_y)
    return true
end

local function build_save_table(next_count)
    local taken = {}
    for i = 1, #COIN_DEFS do
        local id = COIN_DEFS[i].id
        if coin_is_taken(id) then
            taken[id] = true
        end
    end
    return {
        version = SAVE_VERSION,
        score = score,
        x = math.floor(player_x + 0.5),
        y = math.floor(player_y + 0.5),
        save_count = next_count,
        inventory = { coins = inventory.coins },
        coins_taken = taken,
    }
end

local function try_load(now_ms)
    local path = save_path()
    if not machine.file_exists(path) then
        set_status("slot " .. slot_name() .. " empty", COL_ERR, now_ms)
        return
    end
    local data, err = machine.load_data(path)
    if not data then
        set_status("load failed", COL_ERR, now_ms)
        print("load failed:", err)
        return
    end
    apply_save_data(data)
    set_status("loaded " .. slot_name() .. "!", COL_OK, now_ms)
    print("loaded slot", slot_name(), "score=", score, "coins=", inventory.coins)
end

local function try_save(now_ms)
    local path = save_path()
    local next_count = save_count + 1
    local ok, err = machine.save_data(path, build_save_table(next_count))
    if not ok then
        set_status("save failed", COL_ERR, now_ms)
        print("save failed:", err)
        return
    end
    save_count = next_count
    set_ghost(player_x, player_y)
    set_status("saved " .. slot_name() .. "!", COL_OK, now_ms)
    print("saved to", machine.resolve_path(path))
end

local function try_toggle_slot(now_ms)
    active_slot = (active_slot == 1) and 2 or 1
    local exists = machine.file_exists(save_path())
    local tag = exists and "has data" or "empty"
    set_status("slot " .. slot_name() .. " (" .. tag .. ")", COL_SLOT, now_ms)
end

local function rects_overlap(ax, ay, aw, ah, bx, by, bw, bh)
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by
end

local function update_coin_pickup()
    for i = 1, #COIN_DEFS do
        local c = COIN_DEFS[i]
        if not coin_is_taken(c.id) then
            if rects_overlap(player_x, player_y, PLAYER_SIZE, PLAYER_SIZE, c.x, c.y, COIN_SIZE, COIN_SIZE) then
                mark_coin_taken(c.id)
                inventory.coins = inventory.coins + 1
                score = score + 50
            end
        end
    end
end

function game_init()
    player_x = (W - PLAYER_SIZE) // 2
    player_y = (H - PLAYER_SIZE) // 2
    score = 0
    save_count = 0
    inventory = { coins = 0 }
    coins_taken = {}
    active_slot = 1
    ghost.active = false
    status_msg = "FAR=slot OP_L/R=save/load"
    status_color = COL_HINT
    status_until = machine.time_ms() + 3000
    prev_op_left = false
    prev_op_right = false
    prev_far = false

    -- 起動時は A 優先、無ければ B を自動ロード
    if machine.file_exists(SAVE_PATHS[1]) then
        active_slot = 1
        try_load(machine.time_ms())
        status_msg = "auto-loaded A"
        status_color = COL_HINT
        status_until = machine.time_ms() + 2500
    elseif machine.file_exists(SAVE_PATHS[2]) then
        active_slot = 2
        try_load(machine.time_ms())
        status_msg = "auto-loaded B"
        status_color = COL_HINT
        status_until = machine.time_ms() + 2500
    end
end

function game_update(dt)
    local now = machine.time_ms()

    score = score + dt // 16

    local dx, dy = 0, 0
    if machine.pressed(BTN_LEFT) then dx = dx - 1 end
    if machine.pressed(BTN_RIGHT) then dx = dx + 1 end
    if machine.pressed(BTN_UP) then dy = dy - 1 end
    if machine.pressed(BTN_DOWN) then dy = dy + 1 end

    if dx ~= 0 or dy ~= 0 then
        local len = math.sqrt(dx * dx + dy * dy)
        player_x = player_x + (dx / len) * MOVE_SPEED * dt
        player_y = player_y + (dy / len) * MOVE_SPEED * dt
    end

    player_x = clamp(player_x, 0, W - PLAYER_SIZE)
    player_y = clamp(player_y, 0, H - PLAYER_SIZE)

    update_coin_pickup()

    local op_left = machine.pressed(BTN_OP_LEFT)
    local op_right = machine.pressed(BTN_OP_RIGHT)
    local far = machine.pressed(BTN_FAR)
    if op_left and not prev_op_left then
        try_save(now)
    end
    if op_right and not prev_op_right then
        try_load(now)
    end
    if far and not prev_far then
        try_toggle_slot(now)
    end
    prev_op_left = op_left
    prev_op_right = op_right
    prev_far = far

    return false
end

function game_draw()
    machine.clear(COL_BG)

    -- セーブ位置ゴースト（点滅）
    if ghost.active then
        local blink_on = (machine.time_ms() // GHOST_BLINK_MS) % 2 == 0
        if blink_on then
            machine.fill_rect(ghost.x, ghost.y, PLAYER_SIZE, PLAYER_SIZE, COL_GHOST)
        end
    end

    for i = 1, #COIN_DEFS do
        local c = COIN_DEFS[i]
        if not coin_is_taken(c.id) then
            machine.fill_rect(c.x, c.y, COIN_SIZE, COIN_SIZE, COL_COIN)
        end
    end

    machine.fill_rect(math.floor(player_x), math.floor(player_y), PLAYER_SIZE, PLAYER_SIZE, COL_PLAYER)

    local a_mark = machine.file_exists(SAVE_PATHS[1]) and "*" or "-"
    local b_mark = machine.file_exists(SAVE_PATHS[2]) and "*" or "-"
    local slot_line = "slot " .. slot_name() .. "  A" .. a_mark .. " B" .. b_mark

    machine.text(4, 4, "Save/Load Test", COL_TEXT)
    machine.text(4, 16, "score:" .. tostring(score) .. " coin:" .. tostring(inventory.coins), COL_TEXT)
    machine.text(4, 28, slot_line .. " saves:" .. tostring(save_count), COL_SLOT)
    machine.text(4, H - 36, "OP_L save  OP_R load  FAR slot", COL_HINT)
    machine.text(4, H - 24, "D-pad move  get gold coins", COL_HINT)

    if status_msg ~= "" and machine.time_ms() < status_until then
        machine.text(4, 44, status_msg, status_color)
    end
end
