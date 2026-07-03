#!/usr/bin/env python3
"""
Test_Lua 用 RGB565 タイルセット / スプライトを生成する。
実行: python generate_assets.py
出力: tile_test/tiles/tiles.bin (128x128), tile_test/tiles/player.bin (16x16)
"""

from __future__ import annotations

import struct
from pathlib import Path

TILE = 16
SHEET_COLS = 8
SHEET_W = TILE * SHEET_COLS
SHEET_H = TILE * SHEET_COLS

# Lua machine.set_layer(..., transparent = true) と同じキー色
KEY = 0xF81F


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


COL = {
    "key": KEY,
    "grass": rgb565(34, 139, 34),
    "grass_hi": rgb565(50, 180, 50),
    "grass_lo": rgb565(24, 100, 24),
    "dirt": rgb565(139, 90, 43),
    "dirt_hi": rgb565(160, 110, 60),
    "water": rgb565(30, 100, 200),
    "water_hi": rgb565(80, 150, 240),
    "cloud": rgb565(240, 240, 255),
    "bush": rgb565(20, 120, 40),
    "flower": rgb565(255, 80, 120),
    "player": rgb565(255, 200, 40),
    "player_eye": rgb565(40, 40, 40),
}


def blank_tile() -> list[list[int]]:
    return [[COL["key"] for _ in range(TILE)] for _ in range(TILE)]


def fill_rect(tile: list[list[int]], x0: int, y0: int, w: int, h: int, color: int) -> None:
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            if 0 <= x < TILE and 0 <= y < TILE:
                tile[y][x] = color


def tile_grass() -> list[list[int]]:
    t = blank_tile()
    for y in range(TILE):
        for x in range(TILE):
            t[y][x] = COL["grass_hi"] if (x + y) % 4 == 0 else COL["grass"]
    return t


def tile_grass_dots() -> list[list[int]]:
    t = tile_grass()
    for y in range(2, TILE, 4):
        for x in range(2, TILE, 4):
            t[y][x] = COL["grass_lo"]
    return t


def tile_dirt() -> list[list[int]]:
    t = blank_tile()
    for y in range(TILE):
        for x in range(TILE):
            t[y][x] = COL["dirt_hi"] if (x * 3 + y) % 5 == 0 else COL["dirt"]
    return t


def tile_water() -> list[list[int]]:
    t = blank_tile()
    for y in range(TILE):
        for x in range(TILE):
            t[y][x] = COL["water_hi"] if (x + y) % 3 == 0 else COL["water"]
    return t


def tile_cloud() -> list[list[int]]:
    t = blank_tile()
    fill_rect(t, 2, 6, 12, 6, COL["cloud"])
    fill_rect(t, 4, 4, 8, 4, COL["cloud"])
    fill_rect(t, 6, 8, 6, 4, COL["cloud"])
    return t


def tile_bush() -> list[list[int]]:
    t = blank_tile()
    fill_rect(t, 3, 8, 10, 7, COL["bush"])
    fill_rect(t, 1, 10, 14, 5, COL["bush"])
    fill_rect(t, 5, 5, 6, 5, COL["bush"])
    fill_rect(t, 7, 12, 2, 2, COL["flower"])
    return t


def tile_brick() -> list[list[int]]:
    t = blank_tile()
    brick = rgb565(120, 60, 40)
    mortar = rgb565(180, 170, 150)
    for y in range(TILE):
        for x in range(TILE):
            t[y][x] = mortar
    for row in range(4):
        off = 0 if row % 2 == 0 else 4
        y0 = row * 4
        for col in range(3):
            x0 = off + col * 6
            if x0 + 5 < TILE:
                fill_rect(t, x0, y0, 5, 3, brick)
    return t


def tile_star() -> list[list[int]]:
    t = blank_tile()
    c = rgb565(255, 220, 60)
    cx, cy = 8, 8
    for y in range(TILE):
        for x in range(TILE):
            dx, dy = abs(x - cx), abs(y - cy)
            if dx + dy <= 6 or (dx <= 2 and dy <= 6) or (dy <= 2 and dx <= 6):
                t[y][x] = c
    return t


TILE_BUILDERS = [
    tile_grass,       # index 0 -> Lua tile 1
    tile_grass_dots,  # 2
    tile_dirt,        # 3
    tile_water,       # 4
    tile_cloud,       # 5 (透明)
    tile_bush,        # 6 (透明)
    tile_brick,       # 7
    tile_star,        # 8
]


def tile_player() -> list[list[int]]:
    t = blank_tile()
    fill_rect(t, 4, 2, 8, 12, COL["player"])
    fill_rect(t, 3, 10, 10, 4, COL["player"])
    fill_rect(t, 6, 5, 2, 2, COL["player_eye"])
    fill_rect(t, 9, 5, 2, 2, COL["player_eye"])
    return t


def tile_to_bytes(tile: list[list[int]]) -> bytes:
    out = bytearray(TILE * TILE * 2)
    i = 0
    for y in range(TILE):
        for x in range(TILE):
            struct.pack_into("<H", out, i, tile[y][x])
            i += 2
    return bytes(out)


def sheet_to_bytes(tiles: list[list[list[int]]]) -> bytes:
    sheet = bytearray(SHEET_W * SHEET_H * 2)
    for idx, tile in enumerate(tiles):
        col = idx % SHEET_COLS
        row = idx // SHEET_COLS
        base_x = col * TILE
        base_y = row * TILE
        for y in range(TILE):
            for x in range(TILE):
                px = base_x + x
                py = base_y + y
                off = (py * SHEET_W + px) * 2
                struct.pack_into("<H", sheet, off, tile[y][x])
    return bytes(sheet)


def main() -> None:
    out_dir = Path(__file__).resolve().parent / "tile_test" / "tiles"
    out_dir.mkdir(parents=True, exist_ok=True)

    tiles = [fn() for fn in TILE_BUILDERS]
    while len(tiles) < 16:
        t = blank_tile()
        fill_rect(t, 4, 4, 8, 8, rgb565(80, 80, 80))
        tiles.append(t)

    sheet_path = out_dir / "tiles.bin"
    sheet_path.write_bytes(sheet_to_bytes(tiles))
    print(f"Wrote {sheet_path} ({SHEET_W}x{SHEET_H}, {sheet_path.stat().st_size} bytes)")

    player_path = out_dir / "player.bin"
    player_path.write_bytes(tile_to_bytes(tile_player()))
    print(f"Wrote {player_path} (16x16, {player_path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
