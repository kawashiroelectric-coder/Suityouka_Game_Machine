#!/usr/bin/env python3
"""
ビジュアルノベル用のプレースホルダ背景・立ち絵 (RGB565 .bin) を生成する。

実行:
    cd Test_Lua/visual_novel
    python generate_images.py

自作画像を使う場合:
    pip install pillow
    python ../../tool/png_to_rgb565bin.py my_bg.png --resize 320x168 -o images/bg/my_bg.bin
    python ../../tool/png_to_rgb565bin.py my_char.png --resize 128x168 -o images/chars/my_char.bin
    立ち絵は背景をマゼンタ (#FF00FF) にすると透過されます。
"""

from __future__ import annotations

import struct
from pathlib import Path

BG_W, BG_H = 320, 168
CHAR_W, CHAR_H = 128, 168
KEY = 0xF81F  # マゼンタ透過キー（machine.draw_image_keyed 既定値）

OUT = Path(__file__).resolve().parent / "images"


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def write_bin(path: Path, w: int, h: int, get_pixel) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(w * h * 2)
    idx = 0
    for y in range(h):
        for x in range(w):
            struct.pack_into("<H", data, idx, get_pixel(x, y))
            idx += 2
    path.write_bytes(data)
    print(f"  {path.relative_to(OUT.parent)}  ({w}x{h}, {len(data)} bytes)")


def lerp(a: int, b: int, t: float) -> int:
    return int(a + (b - a) * t)


def bg_gradient(top, bottom, fn=None):
    def pixel(x, y):
        t = y / max(BG_H - 1, 1)
        r = lerp(top[0], bottom[0], t)
        g = lerp(top[1], bottom[1], t)
        b = lerp(top[2], bottom[2], t)
        if fn:
            r, g, b = fn(x, y, r, g, b)
        return rgb565(r, g, b)

    return pixel


def bg_starry(top, bottom, count=40, seed=1):
    stars = []
    for i in range(count):
        sx = (i * 7919 + seed * 13) % BG_W
        sy = (i * 3571 + seed * 7) % max(BG_H - 20, 1)
        stars.append((sx, sy, (i + seed) % 3))

    def pixel(x, y):
        t = y / max(BG_H - 1, 1)
        r = lerp(top[0], bottom[0], t)
        g = lerp(top[1], bottom[1], t)
        b = lerp(top[2], bottom[2], t)
        for sx, sy, br in stars:
            if abs(x - sx) <= 1 and abs(y - sy) <= 1:
                k = 180 + br * 25
                return rgb565(k, k, min(255, k + 30))
        return rgb565(r, g, b)

    return pixel


def draw_char_silhouette(style: str):
    """簡易シルエット（マゼンタ背景 + 不透明キャラ）"""

    def body_color():
        if style == "hero":
            return rgb565(80, 140, 220)
        if style == "mysterious":
            return rgb565(120, 80, 160)
        return rgb565(100, 110, 130)

    col = body_color()
    skin = rgb565(240, 200, 170)
    hair = rgb565(40, 30, 50) if style == "mysterious" else rgb565(60, 45, 35)

    def pixel(x, y):
        cx = CHAR_W // 2
        # 頭
        dx, dy = x - cx, y - 36
        if dx * dx + dy * dy <= 14 * 14:
            if style == "mysterious" and dy < -4:
                return hair
            return skin
        # 髪（ mysterious フード）
        if style == "mysterious":
            if -20 <= dy <= 8 and abs(dx) <= 22 and dy + abs(dx) * 0.3 < 10:
                return hair
        # 胴体
        if 48 <= y <= 130 and abs(x - cx) <= 28:
            return col
        # 足
        if 130 < y <= 165:
            leg = -18 if x < cx else 18
            if abs(x - (cx + leg)) <= 12:
                return col
        return KEY

    return pixel


def main() -> None:
    print("Generating VN placeholder images...")
    backgrounds = {
        "bg/title_night.bin": bg_starry((10, 15, 45), (25, 30, 70), count=50, seed=3),
        "bg/classroom.bin": bg_gradient((220, 210, 190), (180, 170, 150)),
        "bg/street.bin": bg_starry((20, 25, 55), (35, 40, 80), count=25, seed=5),
        "bg/star_path.bin": bg_starry((15, 20, 60), (40, 50, 100), count=60, seed=7),
        "bg/home.bin": bg_gradient((80, 60, 50), (120, 90, 70)),
        "bg/ending.bin": bg_starry((8, 12, 30), (20, 25, 50), count=35, seed=11),
    }
    for rel, fn in backgrounds.items():
        write_bin(OUT / rel, BG_W, BG_H, fn)

    for name, style in [
        ("chars/hero.bin", "hero"),
        ("chars/mysterious.bin", "mysterious"),
        ("chars/narrator.bin", "narrator"),
    ]:
        write_bin(OUT / name, CHAR_W, CHAR_H, draw_char_silhouette(style))

    print("Done. Copy images/ to SD:/visual_novel/images/")


if __name__ == "__main__":
    main()
