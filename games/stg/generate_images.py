#!/usr/bin/env python3
"""
翠晶撃線 (stg) 用 RGB565 スプライトとメニュープレビューを生成する。

実行:
  cd Test_Lua/stg
  python generate_images.py

出力:
  img/*.bin   … ゲーム内スプライト（マゼンタ 0xF81F 透過）
  stg.bin     … ゲーム選択メニュー用 100x100 プレビュー
"""

from __future__ import annotations

import struct
from pathlib import Path

KEY = 0xF81F  # machine.draw_image_keyed 既定透過色


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def blank(w: int, h: int) -> list[list[int]]:
    return [[KEY for _ in range(w)] for _ in range(h)]


def fill(img: list[list[int]], x0: int, y0: int, w: int, h: int, color: int) -> None:
    ih, iw = len(img), len(img[0])
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            if 0 <= x < iw and 0 <= y < ih:
                img[y][x] = color


def write_bin(path: Path, img: list[list[int]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        for row in img:
            for px in row:
                f.write(struct.pack("<H", px))


def make_player() -> list[list[int]]:
    w, h = 16, 16
    img = blank(w, h)
    c1 = rgb565(80, 240, 255)
    c2 = rgb565(20, 140, 200)
    fill(img, 6, 2, 4, 10, c1)
    fill(img, 4, 10, 8, 4, c2)
    fill(img, 7, 3, 2, 4, rgb565(255, 255, 255))
    return img


def make_scout() -> list[list[int]]:
    w, h = 20, 16
    img = blank(w, h)
    c = rgb565(160, 90, 255)
    fill(img, 4, 3, 12, 8, c)
    fill(img, 2, 11, 16, 3, rgb565(100, 50, 180))
    return img


def make_fighter() -> list[list[int]]:
    w, h = 28, 20
    img = blank(w, h)
    c = rgb565(255, 100, 80)
    fill(img, 8, 3, 12, 12, c)
    fill(img, 2, 7, 6, 8, c)
    fill(img, 20, 7, 6, 8, c)
    return img


def make_diver() -> list[list[int]]:
    w, h = 22, 22
    img = blank(w, h)
    c = rgb565(100, 220, 120)
    fill(img, 8, 2, 6, 16, c)
    fill(img, 3, 8, 16, 5, rgb565(60, 180, 100))
    return img


def make_boss() -> list[list[int]]:
    w, h = 56, 44
    img = blank(w, h)
    c1 = rgb565(255, 60, 180)
    c2 = rgb565(160, 30, 100)
    fill(img, 12, 10, 32, 24, c1)
    fill(img, 6, 14, 8, 18, c2)
    fill(img, 42, 14, 8, 18, c2)
    fill(img, 22, 6, 12, 8, rgb565(255, 200, 220))
    return img


def make_bullet(w: int, h: int, color: tuple[int, int, int]) -> list[list[int]]:
    img = blank(w, h)
    fill(img, 0, 0, w, h, rgb565(*color))
    return img


def make_preview() -> list[list[int]]:
    w = h = 100
    img = blank(w, h)
    bg0 = rgb565(8, 12, 32)
    bg1 = rgb565(20, 40, 80)
    for y in range(h):
        c = bg0 if y < h // 2 else bg1
        for x in range(w):
            img[y][x] = c
    for i in range(30):
        sx = (i * 17) % w
        sy = (i * 23) % h
        fill(img, sx, sy, 2, 2, rgb565(120, 160, 220))
    # 自機シルエット
    fill(img, 46, 62, 8, 12, rgb565(80, 240, 255))
    fill(img, 42, 72, 16, 6, rgb565(20, 140, 200))
    # 敵弾
    fill(img, 30, 40, 3, 3, rgb565(255, 90, 150))
    fill(img, 68, 36, 3, 3, rgb565(255, 90, 150))
    fill(img, 50, 28, 3, 3, rgb565(255, 160, 60))
    return img


def main() -> None:
    root = Path(__file__).resolve().parent
    specs = {
        "img/player.bin": make_player(),
        "img/scout.bin": make_scout(),
        "img/fighter.bin": make_fighter(),
        "img/diver.bin": make_diver(),
        "img/boss.bin": make_boss(),
        "img/pbullet.bin": make_bullet(4, 10, (255, 255, 120)),
        "img/ebullet.bin": make_bullet(5, 5, (255, 90, 150)),
        "stg.bin": make_preview(),
    }
    for rel, pixels in specs.items():
        out = root / rel
        write_bin(out, pixels)
        h, w = len(pixels), len(pixels[0])
        print(f"Wrote {out.name} ({w}x{h})")
    print("Done. Copy stg/ folder to SD:/games/stg/")


if __name__ == "__main__":
    main()
