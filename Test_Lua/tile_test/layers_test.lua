-- layers モード表示テスト (Suityouka Game Machine)
-- SD カードルートに本ファイルと tiles/ フォルダを配置して起動
-- 起動: layers_test.lua を game.lua にリネームするか、ホスト側でパス指定

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

    player_id = machine.load_sprite("tiles/player.bin", 16, 16)
    if not player_id then
        print("layers_test: load tiles/player.bin failed")
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

    player_y = H - TILE * 4
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
    if machine.pressed(3) then
        player_x = player_x - dt / 8
    end
    if player_x < 8 then player_x = 8 end
    if player_x > W - 24 then player_x = W - 24 end

    player_y = H - TILE * 4 + math.floor(math.sin(elapsed / 400) * 4)

    if machine.jump_pressed() then
        --return true
    end
    return false
end

function game_draw()
    if not player_id then
        return
    end
    machine.draw_sprite(player_id, math.floor(player_x), math.floor(player_y))

    machine.text(4, 4, "LAYERS TEST",
        machine.rgb(255, 255, 255), machine.rgb(25, 35, 70))
    machine.text(4, 14, "R:RIGHT D:LEFT",
        machine.rgb(200, 200, 220), machine.rgb(25, 35, 70))
    machine.text(4, H - 12, "JUMP=EXIT",
        machine.rgb(200, 200, 220), machine.rgb(25, 35, 70))
end
