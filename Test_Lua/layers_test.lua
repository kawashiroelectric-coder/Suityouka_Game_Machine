-- layers モード表示テスト (Suityouka Game Machine)
-- SD: /tile_test/layers_test.lua + /tile_test/tiles/*.bin
-- 相対パスは layers_test.lua があるフォルダ基準
-- （tile_test/layers_test.lua と同一）

local W = machine.width()
local H = machine.height()

local TILE = 16
local MAP_COLS = 40
local MAP_ROWS = 15
local SHEET_COLS = 8

local sheet_id = nil
local player_id = nil

local scroll_ground = 0
local scroll_cloud = 0
local player_x = 80
local player_y = 0
local elapsed = 0

-- player.bin の RGB565 サイズと一致させる（7200 bytes = 60×60×2）
local PLAYER_W = 60
local PLAYER_H = 60

local function build_ground_map()
    local m = {}
    for row = 0, MAP_ROWS - 1 do
        for col = 0, MAP_COLS - 1 do
            local idx = row * MAP_COLS + col + 1
            if row >= MAP_ROWS - 2 then
                m[idx] = 4
            elseif row >= MAP_ROWS - 4 and col % 7 == 0 then
                m[idx] = 3
            elseif (col + row) % 11 == 0 then
                m[idx] = 2
            elseif row >= MAP_ROWS - 5 and col % 5 == 0 then
                m[idx] = 7
            else
                m[idx] = 1
            end
        end
    end
    return m
end

local function build_cloud_map()
    local m = {}
    for row = 0, MAP_ROWS - 1 do
        for col = 0, MAP_COLS - 1 do
            local idx = row * MAP_COLS + col + 1
            if row <= 2 and col % 6 == 1 then
                m[idx] = 5
            elseif row == 3 and col % 9 == 4 then
                m[idx] = 5
            elseif row >= 5 and row <= 8 and col % 8 == 3 then
                m[idx] = 6
            else
                m[idx] = 0
            end
        end
    end
    return m
end

function game_init()
    machine.set_draw_mode("layers")
    machine.set_layer_backdrop(machine.rgb(25, 35, 70))

    sheet_id = machine.load_sprite("tiles/tiles.bin", 128, 128)
    if not sheet_id then
        print("layers_test: load tiles/tiles.bin failed")
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
    machine.set_layer_tiles(0, build_ground_map())

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
        print("layers_test: load tiles/player.bin failed (check W×H vs file size)")
    end

    player_y = H - PLAYER_H
    scroll_ground = 0
    scroll_cloud = 0
    elapsed = 0

    print("layers_test: init OK (layers mode)")
end

function game_update(dt)
    elapsed = elapsed + dt

    scroll_ground = scroll_ground + dt / 8
    scroll_cloud = scroll_cloud + dt / 3
    if scroll_ground > MAP_COLS * TILE then
        scroll_ground = scroll_ground - MAP_COLS * TILE
    end
    if scroll_cloud > MAP_COLS * TILE then
        scroll_cloud = scroll_cloud - MAP_COLS * TILE
    end

    machine.set_layer_scroll(0, math.floor(scroll_ground), 0)
    machine.set_layer_scroll(1, math.floor(scroll_cloud), 0)

    if machine.pressed(0) then
        player_x = player_x + dt / 8
    end
    if machine.pressed(2) then
        player_x = player_x - dt / 8
    end
    if machine.pressed(3) then
        player_y = player_y + dt / 8
    end
    if machine.pressed(1) then
        player_y = player_y - dt / 8
    end

    if player_x < 0 then player_x = 0 end
    if player_x > W - PLAYER_W then player_x = W - PLAYER_W end
    if player_y < 0 then player_y = 0 end
    if player_y > H - PLAYER_H then player_y = H - PLAYER_H end

    if machine.jump_pressed() then
        --return true
    end
    return false
end

function game_draw()
    if player_id then
        machine.draw_sprite_keyed(player_id, math.floor(player_x), math.floor(player_y))
    end

    machine.text(4, 4, "LAYERS TEST",
        machine.rgb(255, 255, 255), machine.rgb(25, 35, 70))
    machine.text(4, 14, "R:RIGHT L:LEFT U/D:MOVE",
        machine.rgb(200, 200, 220), machine.rgb(25, 35, 70))
    machine.text(4, H - 12, "JUMP=EXIT",
        machine.rgb(200, 200, 220), machine.rgb(25, 35, 70))
end
