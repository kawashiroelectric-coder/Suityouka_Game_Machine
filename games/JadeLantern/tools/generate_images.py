#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
翠灯夜行 (JadeLantern) の画像アセットを生成する。

実行（どこからでも可）:
  python games/JadeLantern/tools/generate_images.py

出力（JadeLantern/ 直下）:
  img/player.bin        40x25   自機 2 フレーム (20x25)
  img/fairy.bin         128x16  妖精 4 色 x 2 フレーム (16x16)
  img/mob.bin           56x16   鬼火 12x12 x2 / 提灯お化け 16x16 x2
  img/boss_<名>.bin     64x40   ボス 2 フレーム (32x40)
  img/face_<名>.bin     80x112  会話用立ち絵
  img/bullets.bin       128x40  敵弾 (小6/中10/大16/星8) x 8 色
  img/misc.bin          64x18   自機弾・アイテム・オプション・当たり判定
  title.bin             100x100 ゲーム選択メニュー用プレビュー
  img/*.png             上の各画像の PNG 版（透過あり。編集・確認用で SD には不要）

PNG を編集したら tool/BinPngConverter で .bin に戻せます。

透過色はマゼンタ 0xF81F（machine.draw_image_keyed 既定）。
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

from art_common import Canvas, write_bin, shade, mix, star_points, add_outline  # noqa: E402
from art_chars import (  # noqa: E402
    CHARS, draw_portrait, draw_chibi, draw_fairy, draw_wisp, draw_lantern_ghost, draw_option,
)

GAME_DIR = Path(__file__).resolve().parent.parent
IMG_DIR = GAME_DIR / "img"
PNG_DIR = IMG_DIR  # PNG 版は .bin と同じ img/ に置く（title.png も含む）

BULLET_COLORS = [
    (255, 60, 60),    # 0 赤
    (255, 150, 40),   # 1 橙
    (255, 230, 60),   # 2 黄
    (80, 230, 90),    # 3 緑
    (60, 220, 230),   # 4 水
    (70, 110, 255),   # 5 青
    (190, 80, 255),   # 6 紫
    (235, 235, 235),  # 7 白
]
# 種類: (サイズ, シート上 y)
BULLET_ROWS = [(6, 0), (10, 6), (16, 16), (8, 32)]


def blank(w, h):
    return Image.new("RGBA", (w, h), (0, 0, 0, 0))


def sheet_h(images):
    w = sum(i.width for i in images)
    h = max(i.height for i in images)
    out = blank(w, h)
    x = 0
    for im in images:
        out.paste(im, (x, 0), im)
        x += im.width
    return out


def draw_round_bullet(size, col, ring=False):
    c = Canvas(size, size, 8)
    m = 0.2
    c.ellipse(m, m, size - m, size - m, shade(col, 0.75))
    if ring:
        c.ellipse(size * 0.15, size * 0.15, size * 0.85, size * 0.85, col)
        c.ellipse(size * 0.3, size * 0.3, size * 0.7, size * 0.7, (255, 255, 255))
    else:
        c.ellipse(size * 0.12, size * 0.12, size * 0.88, size * 0.88, col)
        c.ellipse(size * 0.3, size * 0.3, size * 0.7, size * 0.7, mix(col, (255, 255, 255), 0.75))
    return c.finish(outline=False, alpha_cut=100)


def draw_star_bullet(size, col):
    c = Canvas(size, size, 8)
    h = size / 2
    c.poly(star_points(h, h, h, h * 0.45), shade(col, 0.8))
    c.poly(star_points(h, h, h * 0.75, h * 0.35), mix(col, (255, 255, 255), 0.4))
    return c.finish(outline=False, alpha_cut=100)


def make_bullets():
    out = blank(128, 40)
    for ci, col in enumerate(BULLET_COLORS):
        s0, y0 = BULLET_ROWS[0]
        out.paste(im := draw_round_bullet(s0, col), (ci * s0, y0), im)
        s1, y1 = BULLET_ROWS[1]
        out.paste(im := draw_round_bullet(s1, col, ring=True), (ci * s1, y1), im)
        s2, y2 = BULLET_ROWS[2]
        out.paste(im := draw_round_bullet(s2, col, ring=True), (ci * s2, y2), im)
        s3, y3 = BULLET_ROWS[3]
        out.paste(im := draw_star_bullet(s3, col), (ci * s3, y3), im)
    return out


def draw_item(label_col, glyph):
    c = Canvas(10, 10, 8)
    c.rect(0.5, 0.5, 9.5, 9.5, shade(label_col, 0.6))
    c.rect(1.2, 1.2, 8.8, 8.8, label_col)
    im = c.finish(outline=False)
    d = ImageDraw.Draw(im)
    white = (255, 255, 255, 255)
    for (x, y) in glyph:
        d.point((x, y), fill=white)
    return im


GLYPH_P = [(3, 2), (4, 2), (5, 2), (3, 3), (6, 3), (3, 4), (4, 4), (5, 4), (3, 5), (3, 6), (3, 7)]
GLYPH_POINT = [(4, 2), (5, 2), (4, 3), (5, 3), (2, 5), (7, 5), (2, 6), (7, 6), (3, 7), (4, 7), (5, 7), (6, 7)]
GLYPH_B = [(3, 2), (4, 2), (5, 2), (3, 3), (6, 3), (3, 4), (4, 4), (5, 4), (3, 5), (6, 5), (3, 6), (6, 6), (3, 7), (4, 7), (5, 7)]
GLYPH_1UP = [(2, 3), (3, 2), (4, 3), (5, 3), (6, 2), (7, 3), (2, 4), (7, 4), (3, 5), (6, 5), (4, 6), (5, 6)]


def make_misc():
    out = blank(64, 18)
    # 自機メイン弾: 護符（お札）6x14
    c = Canvas(6, 14, 8)
    c.rect(0.5, 0, 5.5, 14, (255, 250, 230))
    c.rect(1.5, 2, 4.5, 12, (255, 140, 60))
    c.rect(2.3, 4, 3.7, 10, (255, 250, 230))
    tal = c.finish(outline=False)
    out.paste(tal, (0, 0), tal)
    # オプション弾: 小さな灯火 6x8
    c = Canvas(6, 8, 8)
    c.poly([(0.5, 5), (3, 0), (5.5, 5)], (255, 170, 60))
    c.ellipse(0.5, 2.5, 5.5, 8, (255, 170, 60))
    c.ellipse(1.8, 4, 4.2, 7, (255, 245, 200))
    fl = c.finish(outline=False)
    out.paste(fl, (6, 0), fl)
    # アイテム 10x10
    for i, (col, g) in enumerate((((230, 50, 50), GLYPH_P), ((50, 90, 230), GLYPH_POINT),
                                  ((40, 170, 70), GLYPH_B), ((230, 80, 170), GLYPH_1UP))):
        im = draw_item(col, g)
        out.paste(im, (12 + i * 10, 0), im)
    # 星（弾消し得点）6x6
    c = Canvas(6, 6, 8)
    c.poly(star_points(3, 3, 3, 1.4), (255, 240, 120))
    st = c.finish(outline=False, alpha_cut=90)
    out.paste(st, (52, 0), st)
    # オプション 8x8 x2
    for f in range(2):
        im = draw_option(f)
        out.paste(im, (12 + f * 8, 10), im)
    # 当たり判定マーカー 5x5
    c = Canvas(5, 5, 8)
    c.ellipse(0, 0, 5, 5, (255, 60, 60))
    c.ellipse(1, 1, 4, 4, (255, 255, 255))
    hb = c.finish(outline=False, alpha_cut=90)
    out.paste(hb, (28, 10), hb)
    return out


def make_title_preview():
    """ゲーム選択メニュー用 100x100。立ち絵 img/face_hotaru.png（差し替え版）があればそれを等倍で使う"""
    img = Image.new("RGB", (100, 100), (10, 14, 34))
    d = ImageDraw.Draw(img)
    for y in range(100):
        t = y / 99
        d.line([(0, y), (99, y)], fill=mix((8, 10, 30), (20, 60, 60), t))
    # 翠月
    moon = Image.new("L", (100, 100), 0)
    md = ImageDraw.Draw(moon)
    md.ellipse([52, 6, 92, 46], fill=255)
    md.ellipse([60, 2, 100, 42], fill=0)
    img.paste((150, 255, 210), (0, 0), moon)
    # 灯籠の光
    for i in range(14):
        x = (i * 37) % 96 + 2
        y = 40 + (i * 23) % 56
        d.ellipse([x, y, x + 4, y + 5], fill=(255, 170, 60))
        d.point((x + 2, y + 2), fill=(255, 240, 180))
    face_png = IMG_DIR / "face_hotaru.png"
    if face_png.exists():
        # 差し替え立ち絵（80x112）: 縮小すると線がつぶれるので等倍で頭〜胸元を見せる
        face = Image.open(face_png).convert("RGBA")
        ring_cx, ring_cy, ring_r = 78, 62, 16
        face_pos = (-4, -2)
    else:
        face = draw_portrait("hotaru").resize((64, 90), Image.NEAREST)
        ring_cx, ring_cy, ring_r = 72, 66, 18
        face_pos = (2, 14)
    # 弾幕（立ち絵の後ろ）
    for i in range(12):
        a = i * math.pi / 6
        x = ring_cx + math.cos(a) * ring_r
        y = ring_cy + math.sin(a) * ring_r
        d.ellipse([x - 3, y - 3, x + 3, y + 3], fill=(70, 230, 160), outline=(20, 80, 60))
    img.paste(face, face_pos, face)
    return img


FONT_TTF = GAME_DIR.parent / "visual_novel" / "fonts" / "PixelMplus-20130602" / "PixelMplus12-Regular.ttf"


def make_logo():
    """タイトルロゴ「翠灯夜行」144x40（PixelMplus12 を 3 倍ドット拡大＋縁取り）"""
    from PIL import ImageFont
    out = blank(144, 40)
    if not FONT_TTF.exists():
        return out
    font = ImageFont.truetype(str(FONT_TTF), 12)
    mask = Image.new("1", (48, 12), 0)
    ImageDraw.Draw(mask).text((0, 0), "翠灯夜行", font=font, fill=1)
    big = mask.resize((144, 36), Image.NEAREST)
    mp = big.load()
    px = out.load()
    # 縁取り（2px 暗色）
    for y in range(36):
        for x in range(144):
            if mp[x, y]:
                for dy in range(-2, 3):
                    for dx in range(-2, 3):
                        nx, ny = x + dx, y + dy + 2
                        if 0 <= nx < 144 and 0 <= ny < 40 and px[nx, ny][3] == 0:
                            px[nx, ny] = (10, 40, 34, 255)
    # 本体（上から翠→白のグラデーション）
    for y in range(36):
        t = y / 35
        col = mix((230, 255, 245), (60, 210, 150), t)
        for x in range(144):
            if mp[x, y]:
                px[x, y + 2] = col + (255,)
    return out


def main() -> int:
    IMG_DIR.mkdir(parents=True, exist_ok=True)
    outputs = {}

    outputs["img/player.bin"] = sheet_h([draw_chibi("hotaru", 20, 25, 0), draw_chibi("hotaru", 20, 25, 1)])
    fairies = []
    for ci in range(4):
        for f in range(2):
            fairies.append(draw_fairy(ci, f))
    outputs["img/fairy.bin"] = sheet_h(fairies)
    mob = blank(56, 16)
    for f in range(2):
        w = draw_wisp(f)
        mob.paste(w, (f * 12, 2), w)
        g = draw_lantern_ghost(f)
        mob.paste(g, (24 + f * 16, 0), g)
    outputs["img/mob.bin"] = mob
    for key in CHARS:
        outputs[f"img/face_{key}.bin"] = draw_portrait(key)
        if key != "hotaru":
            outputs[f"img/boss_{key}.bin"] = sheet_h([draw_chibi(key, 32, 40, 0), draw_chibi(key, 32, 40, 1)])
    outputs["img/bullets.bin"] = make_bullets()
    outputs["img/misc.bin"] = make_misc()
    outputs["img/logo.bin"] = make_logo()
    outputs["title.bin"] = make_title_preview()

    for rel, im in outputs.items():
        write_bin(GAME_DIR / rel, im)
        im.save(PNG_DIR / (Path(rel).stem + ".png"))
        print(f"{rel:24s} {im.width}x{im.height}")
    print("done")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
