#!/usr/bin/env python3
"""プラン3ゲーム用 100x100 メニュープレビュー .bin を生成する。"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tool"))

from PIL import Image, ImageDraw

from rgb565_codec import image_to_rgb565_bytes, write_rgb565_bin


def rgb565(r: int, g: int, b: int) -> tuple[int, int, int]:
    return (r, g, b)


def save_preview(path: Path, draw_fn) -> None:
    img = Image.new("RGB", (100, 100), (8, 10, 22))
    draw = ImageDraw.Draw(img)
    draw_fn(draw, img)
    _, _, data = image_to_rgb565_bytes(img)
    write_rgb565_bin(path, data)
    print(f"wrote {path} ({len(data)} bytes)")


def echo_preview(draw: ImageDraw.ImageDraw, _img: Image.Image) -> None:
    draw.rectangle((10, 10, 90, 90), outline=(40, 55, 80), width=2)
    draw.rectangle((44, 44, 56, 56), fill=(60, 240, 200))
    draw.rectangle((28, 50, 38, 60), fill=(255, 70, 150))
    draw.rectangle((62, 38, 72, 48), fill=(120, 180, 255))
    draw.text((22, 78), "ECHO", fill=(200, 220, 255))


def sono_preview(draw: ImageDraw.ImageDraw, _img: Image.Image) -> None:
    for y in range(20, 85, 12):
        for x in range(15, 85, 12):
            draw.rectangle((x, y, x + 8, y + 8), fill=(18, 22, 38))
    draw.rectangle((44, 44, 52, 52), fill=(240, 240, 255))
    draw.ellipse((70, 60, 78, 68), fill=(220, 60, 50))
    draw.ellipse((80, 60, 88, 68), fill=(220, 60, 50))
    draw.text((14, 8), "SONO", fill=(180, 200, 230))


def twin_preview(draw: ImageDraw.ImageDraw, _img: Image.Image) -> None:
    for c in range(5):
        for r in range(4):
            x = 18 + c * 14
            y = 28 + r * 14
            draw.rectangle((x, y, x + 12, y + 12), fill=(30, 34, 52))
    draw.rectangle((46, 48, 54, 56), fill=(60, 240, 200))
    draw.rectangle((54, 48, 62, 56), fill=(255, 100, 180))
    draw.line((50, 20, 50, 88), fill=(255, 200, 80), width=1)
    draw.text((10, 6), "TWIN", fill=(200, 210, 240))


def main() -> int:
    save_preview(ROOT / "games" / "ECHO" / "ECHO.bin", echo_preview)
    save_preview(ROOT / "games" / "Sonograph" / "Sonograph.bin", sono_preview)
    save_preview(ROOT / "games" / "TwinSwitch" / "TwinSwitch.bin", twin_preview)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
