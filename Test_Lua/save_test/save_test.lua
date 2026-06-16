-- セーブ／ロード API テストゲーム
-- SD: /save_test/save_test.lua
-- OP_LEFT = セーブ / OP_RIGHT = ロード / 十字キー = 移動

local W = machine.width()
local H = machine.height()

local SAVE_PATH = "save.dat"

local BTN_LEFT = 2
local BTN_RIGHT = 0
local BTN_UP = 1
local BTN_DOWN = 3
local BTN_OP_LEFT = 4
local BTN_OP_RIGHT = 5

local COL_BG = machine.rgb(16, 24, 48)
local COL_PLAYER = machine.rgb(80, 220, 120)
local COL_TEXT = machine.rgb(220, 230, 255)
local COL_HINT = machine.rgb(140, 150, 180)
local COL_OK = machine.rgb(100, 255, 140)
local COL_ERR = machine.rgb(255, 100, 100)

local PLAYER_SIZE = 16
local MOVE_SPEED = 0.12

local player_x = 0
local player_y = 0
local score = 0
local save_count = 0
local status_msg = ""
local status_color = COL_TEXT
local status_until = 0
local prev_op_left = false
local prev_op_right = false

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

local function apply_save_data(data)
    if type(data) ~= "table" then
        return false
    end
    player_x = clamp(tonumber(data.x) or player_x, 0, W - PLAYER_SIZE)
    player_y = clamp(tonumber(data.y) or player_y, 0, H - PLAYER_SIZE)
    score = tonumber(data.score) or score
    save_count = tonumber(data.save_count) or save_count
    return true
end

local function try_load(now_ms)
    if not machine.file_exists(SAVE_PATH) then
        set_status("no save file", COL_ERR, now_ms)
        return
    end
    local data, err = machine.load_data(SAVE_PATH)
    if not data then
        set_status("load failed", COL_ERR, now_ms)
        print("load failed:", err)
        return
    end
    apply_save_data(data)
    set_status("loaded!", COL_OK, now_ms)
    print("loaded score=", score, "pos=", player_x, player_y)
end

local function try_save(now_ms)
    local next_count = save_count + 1
    local ok, err = machine.save_data(SAVE_PATH, {
        score = score,
        x = math.floor(player_x + 0.5),
        y = math.floor(player_y + 0.5),
        save_count = next_count,
    })
    if not ok then
        set_status("save failed", COL_ERR, now_ms)
        print("save failed:", err)
        return
    end
    save_count = next_count
    set_status("saved!", COL_OK, now_ms)
    print("saved to", machine.resolve_path(SAVE_PATH))
end

function game_init()
    player_x = (W - PLAYER_SIZE) // 2
    player_y = (H - PLAYER_SIZE) // 2
    score = 0
    save_count = 0
    status_msg = "OP_L=save OP_R=load"
    status_color = COL_HINT
    status_until = machine.time_ms() + 3000
    prev_op_left = false
    prev_op_right = false

    if machine.file_exists(SAVE_PATH) then
        try_load(machine.time_ms())
        status_msg = "auto-loaded (OP_L save)"
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

    local op_left = machine.pressed(BTN_OP_LEFT)
    local op_right = machine.pressed(BTN_OP_RIGHT)
    if op_left and not prev_op_left then
        try_save(now)
    end
    if op_right and not prev_op_right then
        try_load(now)
    end
    prev_op_left = op_left
    prev_op_right = op_right

    return false
end

function game_draw()
    machine.clear(COL_BG)

    machine.fill_rect(math.floor(player_x), math.floor(player_y), PLAYER_SIZE, PLAYER_SIZE, COL_PLAYER)

    machine.text(4, 4, "Save/Load Test", COL_TEXT)
    machine.text(4, 16, "score: " .. tostring(score), COL_TEXT)
    machine.text(4, 28, "saves: " .. tostring(save_count), COL_HINT)
    machine.text(4, H - 36, "OP_L save  OP_R load", COL_HINT)
    machine.text(4, H - 24, "D-pad move", COL_HINT)

    if status_msg ~= "" and machine.time_ms() < status_until then
        machine.text(4, 44, status_msg, status_color)
    end
end
