#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
翠灯夜行 (JadeLantern) 画像生成の共通処理。

- 4 倍解像度で PIL 描画 → 縮小 → マゼンタ透過 (0xF81F) の RGB565 .bin に変換
- すべてオリジナルキャラクター（パラメータで髪型・色・装飾を切り替え）
"""

from __future__ import annotations

import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw

KEY565 = 0xF81F
KEY_RGB = (255, 0, 255)


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def shade(c, k: float):
    """色を k 倍（>1 で明るく、<1 で暗く）"""
    if k >= 1.0:
        return tuple(int(v + (255 - v) * (k - 1.0)) for v in c[:3])
    return tuple(int(v * k) for v in c[:3])


def mix(a, b, t: float):
    return tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(3))


class Canvas:
    """1x 座標で指定し、内部では S 倍で描く RGBA キャンバス"""

    def __init__(self, w: int, h: int, s: int = 4) -> None:
        self.w, self.h, self.s = w, h, s
        self.img = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def _p(self, pts):
        s = self.s
        return [(x * s, y * s) for (x, y) in pts]

    def poly(self, pts, fill, outline=None, width=1):
        self.d.polygon(self._p(pts), fill=fill + (255,) if len(fill) == 3 else fill,
                       outline=outline)
        if outline is not None and width > 1:
            pp = self._p(pts)
            self.d.line(pp + [pp[0]], fill=outline, width=width * self.s // 2)

    def ellipse(self, x0, y0, x1, y1, fill, outline=None, width=1):
        s = self.s
        f = fill + (255,) if fill is not None and len(fill) == 3 else fill
        o = outline + (255,) if outline is not None and len(outline) == 3 else outline
        self.d.ellipse([x0 * s, y0 * s, x1 * s, y1 * s], fill=f, outline=o,
                       width=max(1, int(width * s)))

    def rect(self, x0, y0, x1, y1, fill):
        s = self.s
        self.d.rectangle([x0 * s, y0 * s, x1 * s - 1, y1 * s - 1], fill=fill + (255,))

    def line(self, pts, fill, width=1.0):
        self.d.line(self._p(pts), fill=fill + (255,), width=max(1, int(width * self.s)),
                    joint="curve")

    def arc(self, x0, y0, x1, y1, a0, a1, fill, width=1.0):
        s = self.s
        self.d.arc([x0 * s, y0 * s, x1 * s, y1 * s], a0, a1, fill=fill + (255,),
                   width=max(1, int(width * s)))

    def finish(self, outline_col=(24, 16, 32), outline=True, alpha_cut=110):
        """縮小して 1x 画像 (RGBA, alpha は 0/255) を返す"""
        small = self.img.resize((self.w, self.h), Image.LANCZOS)
        px = small.load()
        for y in range(self.h):
            for x in range(self.w):
                r, g, b, a = px[x, y]
                if a < alpha_cut:
                    px[x, y] = (0, 0, 0, 0)
                else:
                    # 半透明エッジは不透明化（色はそのまま）
                    px[x, y] = (r, g, b, 255)
        if outline:
            add_outline(small, outline_col)
        return small


def add_outline(img: Image.Image, col=(24, 16, 32)) -> None:
    """不透明領域の外周 1px を暗色で縁取る（シルエットの内側に描く）"""
    w, h = img.size
    px = img.load()
    edge = []
    for y in range(h):
        for x in range(w):
            if px[x, y][3] == 0:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if nx < 0 or ny < 0 or nx >= w or ny >= h or px[nx, ny][3] == 0:
                    edge.append((x, y))
                    break
    for x, y in edge:
        r, g, b, _ = px[x, y]
        px[x, y] = (int(r * 0.25 + col[0] * 0.75), int(g * 0.25 + col[1] * 0.75),
                    int(b * 0.25 + col[2] * 0.75), 255)


def to_key_rgb(img: Image.Image) -> Image.Image:
    """RGBA → RGB（透明部はマゼンタ）"""
    out = Image.new("RGB", img.size, KEY_RGB)
    out.paste(img, (0, 0), img)
    return out


def safe_color(c):
    """RGB565 でマゼンタキーと衝突しない色へ"""
    r, g, b = c
    if rgb565(r, g, b) == KEY565:
        g = 8
    return (r, g, b)


def write_bin(path: Path, img: Image.Image) -> None:
    """RGB / RGBA 画像を RGB565 LE .bin へ（RGBA の透明部はキー色）"""
    if img.mode == "RGBA":
        img = to_key_rgb(img)
    img = img.convert("RGB")
    w, h = img.size
    px = img.load()
    buf = bytearray(w * h * 2)
    i = 0
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if (r, g, b) == KEY_RGB:
                v = KEY565
            else:
                v = rgb565(*safe_color((r, g, b)))
            struct.pack_into("<H", buf, i, v)
            i += 2
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(buf))


def star_points(cx, cy, r_out, r_in, n=5, rot=-90.0):
    pts = []
    for i in range(n * 2):
        r = r_out if i % 2 == 0 else r_in
        a = math.radians(rot + i * 180.0 / n)
        pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    return pts
