"""RGB565 リトルエンディアン変換の共通処理（tool 内スクリプト用）。"""

from __future__ import annotations

import struct
from pathlib import Path

from PIL import Image


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rgb565_to_rgb888(value: int) -> tuple[int, int, int]:
    r = ((value >> 11) & 0x1F) << 3
    g = ((value >> 5) & 0x3F) << 2
    b = (value & 0x1F) << 3
    return r, g, b


def parse_size(size: str) -> tuple[int, int]:
    parts = size.lower().split("x")
    if len(parts) != 2:
        raise ValueError(f"'{size}' は WxH 形式で指定してください (例: 320x168)")
    return int(parts[0]), int(parts[1])


def image_to_rgb565_bytes(image: Image.Image) -> tuple[int, int, bytes]:
    rgb = image.convert("RGB")
    width, height = rgb.size
    data = bytearray(width * height * 2)
    index = 0
    for y in range(height):
        for x in range(width):
            r, g, b = rgb.getpixel((x, y))
            struct.pack_into("<H", data, index, rgb888_to_rgb565(r, g, b))
            index += 2
    return width, height, bytes(data)


def rgba_image_to_rgb565_pixels(image: Image.Image) -> tuple[int, int, list[int]]:
    rgba = image.convert("RGBA")
    width, height = rgba.size
    pixels: list[int] = []
    for y in range(height):
        for x in range(width):
            r, g, b, a = rgba.getpixel((x, y))
            if a < 255:
                alpha = a / 255.0
                r = int(round(r * alpha))
                g = int(round(g * alpha))
                b = int(round(b * alpha))
            pixels.append(rgb888_to_rgb565(r, g, b))
    return width, height, pixels


def rgb565_bytes_to_image(data: bytes, width: int, height: int) -> Image.Image:
    expected = width * height * 2
    if len(data) != expected:
        raise ValueError(f"サイズ不一致: {len(data)} != {expected} ({width}x{height})")

    image = Image.new("RGB", (width, height))
    pixels = image.load()
    index = 0
    for y in range(height):
        for x in range(width):
            value = struct.unpack_from("<H", data, index)[0]
            pixels[x, y] = rgb565_to_rgb888(value)
            index += 2
    return image


def write_rgb565_bin(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def read_rgb565_bin(path: Path) -> bytes:
    return path.read_bytes()
