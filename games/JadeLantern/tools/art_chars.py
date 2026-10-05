#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
翠灯夜行 のキャラクター定義と描画（立ち絵バストアップ・ゲーム内ドット）。
すべてオリジナルデザイン。
"""

from __future__ import annotations

import math

from art_common import Canvas, shade, mix, star_points

SKIN = (255, 228, 210)
SKIN_SH = (236, 190, 176)
LINE = (40, 24, 40)

# ---------------------------------------------------------------------------
# キャラクター定義
# ---------------------------------------------------------------------------
CHARS = {
    # 自機: 灯守ほたる（ともり ほたる）— 灯籠守りの少女
    "hotaru": dict(
        hair=(46, 96, 72), hair_style="bob", eye=(250, 170, 40),
        cloth=(248, 244, 236), cloth2=(240, 140, 40), cloth_style="haori",
        acc=["lantern_pin"], expr="smile",
    ),
    # 1 面: 鬼火のちろり
    "chirori": dict(
        hair=(120, 190, 255), hair_style="twin", eye=(60, 110, 255),
        cloth=(70, 90, 200), cloth2=(200, 230, 255), cloth_style="dress",
        acc=["wisps"], expr="smug",
    ),
    # 2 面: 狐塚こはく
    "kohaku": dict(
        hair=(240, 170, 70), hair_style="long", eye=(200, 60, 40),
        cloth=(250, 250, 245), cloth2=(210, 40, 50), cloth_style="kimono",
        acc=["fox_ears"], expr="smug",
    ),
    # 3 面: 水無月しずく
    "shizuku": dict(
        hair=(60, 170, 190), hair_style="long", eye=(40, 120, 200),
        cloth=(40, 90, 170), cloth2=(170, 230, 240), cloth_style="kimono",
        acc=["water_pin"], expr="calm",
    ),
    # 4 面: 螺子巻ねじか
    "nejika": dict(
        hair=(200, 200, 215), hair_style="twin", eye=(170, 60, 200),
        cloth=(110, 50, 140), cloth2=(240, 200, 90), cloth_style="dress",
        acc=["gear_pin", "key"], expr="calm",
    ),
    # 5 面: 鳴神らいか
    "raika": dict(
        hair=(255, 220, 60), hair_style="spiky", eye=(60, 200, 255),
        cloth=(60, 60, 80), cloth2=(255, 220, 60), cloth_style="dress",
        acc=["horns", "bolts"], expr="angry",
    ),
    # 6 面: 夜翠ミコト（よすい みこと）— 翠月の姫
    "mikoto": dict(
        hair=(24, 40, 44), hair_style="hime", eye=(60, 230, 160),
        cloth=(20, 90, 70), cloth2=(200, 240, 220), cloth_style="kimono",
        acc=["crown", "moon"], expr="calm",
    ),
}

NAMES = {
    "hotaru": "灯守ほたる",
    "chirori": "鬼火のちろり",
    "kohaku": "狐塚こはく",
    "shizuku": "水無月しずく",
    "nejika": "螺子巻ねじか",
    "raika": "鳴神らいか",
    "mikoto": "夜翠ミコト",
}


# ---------------------------------------------------------------------------
# バストアップ立ち絵 (80x112)
# ---------------------------------------------------------------------------

def _back_hair(c: Canvas, p, cx, top):
    hair = p["hair"]
    hs = p["hair_style"]
    dark = shade(hair, 0.7)
    if hs in ("long", "hime"):
        c.poly([(cx - 22, top + 14), (cx + 22, top + 14), (cx + 28, 112), (cx - 28, 112)], dark)
    if hs == "hime":
        c.poly([(cx - 24, top + 18), (cx - 16, top + 18), (cx - 18, 112), (cx - 30, 112)], hair)
        c.poly([(cx + 24, top + 18), (cx + 16, top + 18), (cx + 18, 112), (cx + 30, 112)], hair)
    if hs == "twin":
        for sgn in (-1, 1):
            x = cx + sgn * 24
            c.poly([(x - 5, top + 12), (x + 5, top + 12), (x + sgn * 10, top + 60),
                    (x + sgn * 4, top + 84), (x - sgn * 2, top + 50)], hair)
            c.ellipse(x - 5, top + 8, x + 5, top + 18, shade(p["cloth2"], 1.0))
    c.ellipse(cx - 21, top - 2, cx + 21, top + 40, dark)


def _body(c: Canvas, p, cx):
    cl = p["cloth"]
    cl2 = p["cloth2"]
    st = p["cloth_style"]
    y0 = 76
    # 首
    c.rect(cx - 5, 62, cx + 5, 80, SKIN_SH)
    # 肩・胴
    c.poly([(cx - 12, y0), (cx + 12, y0), (cx + 30, y0 + 10), (cx + 36, 112),
            (cx - 36, 112), (cx - 30, y0 + 10)], cl)
    c.poly([(cx + 22, y0 + 8), (cx + 36, 112), (cx + 26, 112)], shade(cl, 0.8))
    c.poly([(cx - 22, y0 + 8), (cx - 36, 112), (cx - 26, 112)], shade(cl, 0.8))
    if st in ("kimono", "haori"):
        # 襟（V 字）
        c.poly([(cx - 12, y0), (cx - 4, y0), (cx + 6, 104), (cx, 112)], cl2)
        c.poly([(cx + 12, y0), (cx + 4, y0), (cx - 2, 98), (cx - 6, 92)], shade(cl2, 0.85))
        c.poly([(cx - 4, y0), (cx + 4, y0), (cx, y0 + 12)], SKIN)
        if st == "haori":
            c.poly([(cx - 30, y0 + 10), (cx - 18, y0 + 4), (cx - 14, 112), (cx - 36, 112)], cl2)
            c.poly([(cx + 30, y0 + 10), (cx + 18, y0 + 4), (cx + 14, 112), (cx + 36, 112)], cl2)
            # 羽織紐
            c.ellipse(cx - 3, 96, cx + 3, 102, (255, 230, 120))
        else:
            c.rect(cx - 30, 100, cx + 30, 106, shade(cl2, 0.9))
    else:
        # ドレス: 丸襟＋リボン
        c.ellipse(cx - 14, y0 - 4, cx + 14, y0 + 10, cl2)
        c.poly([(cx - 8, y0 + 4), (cx, y0 + 10), (cx - 8, y0 + 16)], shade(cl2, 0.8))
        c.poly([(cx + 8, y0 + 4), (cx, y0 + 10), (cx + 8, y0 + 16)], shade(cl2, 0.8))
        c.ellipse(cx - 3, y0 + 7, cx + 3, y0 + 13, cl2)
        c.poly([(cx - 36, 104), (cx + 36, 104), (cx + 36, 112), (cx - 36, 112)], shade(cl, 0.7))


def _face(c: Canvas, p, cx, top):
    # 顔の輪郭
    c.poly([(cx - 17, top + 16), (cx + 17, top + 16), (cx + 16, top + 34), (cx + 8, top + 44),
            (cx, top + 47), (cx - 8, top + 44), (cx - 16, top + 34)], SKIN)
    c.ellipse(cx - 17, top + 2, cx + 17, top + 38, SKIN)
    # 耳
    c.ellipse(cx - 20, top + 24, cx - 14, top + 33, SKIN_SH)
    c.ellipse(cx + 14, top + 24, cx + 20, top + 33, SKIN_SH)
    eye = p["eye"]
    expr = p["expr"]
    ey = top + 27
    for sgn in (-1, 1):
        ex = cx + sgn * 8
        # 白目
        c.ellipse(ex - 5, ey - 5, ex + 5, ey + 6, (255, 255, 255))
        # 瞳
        c.ellipse(ex - 4, ey - 4, ex + 4, ey + 6, eye)
        c.ellipse(ex - 3, ey - 1, ex + 3, ey + 5, shade(eye, 0.55))
        c.ellipse(ex - 3, ey + 2, ex + 3, ey + 6, shade(eye, 1.35))
        c.ellipse(ex - 3, ey - 3, ex, ey, (255, 255, 255))
        # まつげ（上）
        if expr == "angry":
            c.line([(ex - 6, ey - 4 + sgn * -2), (ex + 6, ey - 4 + sgn * 2)], LINE, 1.6)
        elif expr == "smug":
            c.line([(ex - 6, ey - 3), (ex + 6, ey - 4)], LINE, 1.8)
            c.rect(ex - 5, ey - 4, ex + 5, ey - 1, SKIN)
            c.line([(ex - 6, ey - 1), (ex + 6, ey - 1)], LINE, 1.4)
        else:
            c.arc(ex - 6, ey - 6, ex + 6, ey + 4, 200, 340, LINE, 1.6)
        # 眉
        by = top + 18
        if expr == "angry":
            c.line([(ex - 5 * sgn, by - 1), (ex + 4 * sgn, by + 2)], shade(p["hair"], 0.5), 1.2)
    # 頬
    c.ellipse(cx - 14, top + 33, cx - 8, top + 36, (255, 190, 190))
    c.ellipse(cx + 8, top + 33, cx + 14, top + 36, (255, 190, 190))
    # 口
    my = top + 39
    if expr == "smile":
        c.arc(cx - 3, my - 3, cx + 3, my + 2, 20, 160, (190, 70, 80), 1.0)
    elif expr == "smug":
        c.line([(cx - 3, my), (cx + 3, my - 1)], (190, 70, 80), 1.0)
    elif expr == "angry":
        c.ellipse(cx - 2, my - 1, cx + 2, my + 2, (170, 50, 60))
    else:
        c.line([(cx - 2, my), (cx + 2, my)], (190, 90, 90), 0.8)


def _front_hair(c: Canvas, p, cx, top):
    hair = p["hair"]
    hs = p["hair_style"]
    hl = shade(hair, 1.35)
    # 頭頂
    c.poly([(cx - 20, top + 20), (cx - 18, top + 6), (cx - 8, top - 2), (cx + 8, top - 2),
            (cx + 18, top + 6), (cx + 20, top + 20)], hair)
    c.ellipse(cx - 20, top - 3, cx + 20, top + 22, hair)
    # 前髪（とんがり束）
    tips = [(-17, 26), (-11, 22), (-5, 24), (1, 21), (7, 24), (13, 22), (18, 27)]
    if hs == "spiky":
        tips = [(-18, 28), (-12, 20), (-6, 26), (0, 18), (6, 26), (12, 20), (18, 28)]
    if hs == "hime":
        # ぱっつん
        c.rect(cx - 18, top + 6, cx + 18, top + 19, hair)
        c.rect(cx - 17, top + 17, cx + 17, top + 20, shade(hair, 0.8))
    else:
        base_y = top + 8
        prev = (cx - 20, base_y)
        for i, (dx, ty) in enumerate(tips):
            nx = cx - 20 + (i + 1) * (40 / len(tips))
            c.poly([prev, (cx + dx, top + ty), (nx, base_y)], hair)
            prev = (nx, base_y)
    # サイド
    side_len = {"bob": 42, "short": 36, "twin": 40, "long": 50, "hime": 46, "spiky": 38}[hs]
    for sgn in (-1, 1):
        x = cx + sgn * 19
        c.poly([(x - sgn * 1, top + 8), (x + sgn * 3, top + 12), (x + sgn * 2, top + side_len),
                (x - sgn * 4, top + side_len - 8), (x - sgn * 5, top + 20)], hair)
    if hs == "hime":
        for sgn in (-1, 1):
            x = cx + sgn * 16
            c.rect(min(x, x + sgn * 5) - 0, top + 16, max(x, x + sgn * 5), top + 50, hair)
    # ハイライト
    c.arc(cx - 14, top + 1, cx + 14, top + 14, 200, 340, hl, 1.2)
    if hs == "spiky":
        for sgn in (-1, 1):
            c.poly([(cx + sgn * 14, top + 2), (cx + sgn * 26, top - 6), (cx + sgn * 18, top + 8)], hair)


def _accessories(c: Canvas, p, cx, top):
    for a in p["acc"]:
        if a == "lantern_pin":
            x, y = cx + 14, top + 2
            c.line([(x + 4, y - 4), (x + 4, y)], (80, 60, 40), 1.0)
            c.ellipse(x - 1, y, x + 9, y + 12, (255, 170, 60))
            c.ellipse(x + 1, y + 2, x + 7, y + 10, (255, 230, 150))
            c.rect(x, y, x + 8, y + 1.5, (120, 40, 30))
            c.rect(x, y + 10.5, x + 8, y + 12, (120, 40, 30))
        elif a == "wisps":
            for (x, y, s) in ((6, 20, 1.0), (72, 30, 0.8), (70, 70, 1.1), (8, 64, 0.7)):
                c.ellipse(x - 5 * s, y - 2 * s, x + 5 * s, y + 8 * s, (90, 160, 255))
                c.poly([(x - 5 * s, y + 3 * s), (x, y - 10 * s), (x + 5 * s, y + 3 * s)], (90, 160, 255))
                c.ellipse(x - 3 * s, y + 1 * s, x + 3 * s, y + 7 * s, (210, 240, 255))
        elif a == "fox_ears":
            for sgn in (-1, 1):
                x = cx + sgn * 14
                c.poly([(x - 7, top + 4), (x + sgn * 6, top - 16), (x + 7, top + 4)], p["hair"])
                c.poly([(x - 3, top + 2), (x + sgn * 4, top - 10), (x + 3, top + 2)], (255, 230, 220))
        elif a == "water_pin":
            for i, (x, y) in enumerate(((cx - 18, top + 6), (cx - 13, top + 1), (cx - 20, top + 13))):
                c.ellipse(x - 3, y - 3, x + 3, y + 3, (150, 230, 255))
                c.ellipse(x - 1.5, y - 2, x, y - 0.5, (255, 255, 255))
        elif a == "gear_pin":
            x, y = cx + 15, top + 4
            for k in range(8):
                ang = k * math.pi / 4
                c.ellipse(x + math.cos(ang) * 6 - 2, y + math.sin(ang) * 6 - 2,
                          x + math.cos(ang) * 6 + 2, y + math.sin(ang) * 6 + 2, (230, 190, 80))
            c.ellipse(x - 6, y - 6, x + 6, y + 6, (230, 190, 80))
            c.ellipse(x - 2, y - 2, x + 2, y + 2, (120, 80, 40))
        elif a == "key":
            x, y = cx + 30, 86
            c.rect(x - 1.5, y - 18, x + 1.5, y + 4, (200, 170, 80))
            c.ellipse(x - 7, y - 26, x - 1, y - 16, (220, 190, 90))
            c.ellipse(x + 1, y - 26, x + 7, y - 16, (220, 190, 90))
        elif a == "horns":
            for sgn in (-1, 1):
                x = cx + sgn * 12
                c.poly([(x - 3, top + 2), (x + sgn * 4, top - 14), (x + 3, top + 2)], (250, 250, 230))
        elif a == "bolts":
            for (x, y) in ((6, 30), (72, 60)):
                c.poly([(x, y), (x + 6, y), (x + 1, y + 8), (x + 7, y + 8), (x - 3, y + 22),
                        (x + 1, y + 11), (x - 4, y + 11)], (255, 240, 90))
        elif a == "crown":
            c.poly([(cx - 12, top + 1), (cx - 12, top - 7), (cx - 6, top - 2), (cx, top - 10),
                    (cx + 6, top - 2), (cx + 12, top - 7), (cx + 12, top + 1)], (230, 210, 120))
            c.ellipse(cx - 3, top - 5, cx + 3, top + 1, (70, 240, 170))


def draw_portrait(key: str) -> "Image":
    p = CHARS[key]
    c = Canvas(80, 112, 4)
    cx, top = 40, 22
    if "moon" in p["acc"]:
        # 背後の翠月（切り抜きで三日月）
        c.ellipse(52, 2, 78, 28, (150, 255, 210))
        c.ellipse(58, -2, 84, 24, (0, 0, 0, 0))
    _back_hair(c, p, cx, top)
    _body(c, p, cx)
    _face(c, p, cx, top)
    _front_hair(c, p, cx, top)
    _accessories(c, p, cx, top)
    return c.finish()


# ---------------------------------------------------------------------------
# ゲーム内ドット（ちびキャラ）
# ---------------------------------------------------------------------------

def draw_chibi(key: str, w: int, h: int, frame: int = 0, back: bool = False):
    """全身ちびキャラ。w x h（例: 自機 16x24, ボス 32x40）。4 倍で描いて縮小"""
    p = CHARS[key]
    s = 8 if w <= 16 else 4
    # 基準 32x40 座標で描く
    c = Canvas(32, 40, s * w // 32 if w != 32 else s)
    c = Canvas(32, 40, 4)
    cx = 16
    hair = p["hair"]
    cl = p["cloth"]
    cl2 = p["cloth2"]
    bob = 1 if frame == 1 else 0
    # 足
    c.rect(cx - 5, 34, cx - 2, 39, (60, 40, 50))
    c.rect(cx + 2, 34, cx + 5, 39, (60, 40, 50))
    # スカート / 着物
    c.poly([(cx - 7, 20), (cx + 7, 20), (cx + 11 + bob, 35), (cx - 11 - bob, 35)], cl)
    c.poly([(cx - 11 - bob, 32), (cx + 11 + bob, 32), (cx + 11 + bob, 35), (cx - 11 - bob, 35)], cl2)
    # 胴
    c.rect(cx - 6, 18, cx + 6, 26, cl)
    c.rect(cx - 6, 24, cx + 6, 26, cl2)
    # 腕（袖）
    c.poly([(cx - 6, 18), (cx - 12, 26 - bob), (cx - 9, 29 - bob), (cx - 5, 23)], cl2 if p["cloth_style"] == "haori" else cl)
    c.poly([(cx + 6, 18), (cx + 12, 26 - bob), (cx + 9, 29 - bob), (cx + 5, 23)], cl2 if p["cloth_style"] == "haori" else cl)
    c.ellipse(cx - 12, 26 - bob, cx - 8, 30 - bob, SKIN)
    c.ellipse(cx + 8, 26 - bob, cx + 12, 30 - bob, SKIN)
    # 後ろ髪
    hs = p["hair_style"]
    if hs in ("long", "hime"):
        c.poly([(cx - 10, 6), (cx + 10, 6), (cx + 11, 26), (cx - 11, 26)], shade(hair, 0.75))
    if hs == "twin":
        c.poly([(cx - 10, 6), (cx - 15, 22 + bob), (cx - 11, 22 + bob)], hair)
        c.poly([(cx + 10, 6), (cx + 15, 22 + bob), (cx + 11, 22 + bob)], hair)
    # 頭
    c.ellipse(cx - 10, 1, cx + 10, 19, hair)
    if not back:
        c.ellipse(cx - 8, 6, cx + 8, 19, SKIN)
        c.poly([(cx - 9, 4), (cx + 9, 4), (cx + 9, 9), (cx + 4, 11), (cx, 8), (cx - 4, 11), (cx - 9, 9)], hair)
        # 目
        c.rect(cx - 5, 11, cx - 2, 15, p["eye"])
        c.rect(cx + 2, 11, cx + 5, 15, p["eye"])
        c.rect(cx - 5, 11, cx - 3, 12.5, (255, 255, 255))
        c.rect(cx + 2, 11, cx + 4, 12.5, (255, 255, 255))
    else:
        c.arc(cx - 8, 3, cx + 8, 12, 200, 340, shade(hair, 1.4), 1.0)
    # 装飾
    for a in p["acc"]:
        if a == "fox_ears":
            c.poly([(cx - 9, 5), (cx - 8, -3 + bob), (cx - 3, 3)], hair)
            c.poly([(cx + 9, 5), (cx + 8, -3 + bob), (cx + 3, 3)], hair)
        elif a == "horns":
            c.poly([(cx - 7, 3), (cx - 9, -3), (cx - 4, 2)], (250, 250, 230))
            c.poly([(cx + 7, 3), (cx + 9, -3), (cx + 4, 2)], (250, 250, 230))
        elif a == "crown":
            c.poly([(cx - 6, 3), (cx - 6, -1), (cx - 3, 1), (cx, -3), (cx + 3, 1), (cx + 6, -1), (cx + 6, 3)], (230, 210, 120))
        elif a == "lantern_pin":
            c.ellipse(cx + 6, 0, cx + 11, 6, (255, 170, 60))
        elif a == "gear_pin":
            c.ellipse(cx + 5, 0, cx + 11, 6, (230, 190, 80))
        elif a == "key":
            c.rect(cx + 11, 14, cx + 13, 22, (220, 190, 90))
            c.ellipse(cx + 10, 11, cx + 14, 15, (220, 190, 90))
        elif a == "water_pin":
            c.ellipse(cx - 11, 3, cx - 6, 8, (150, 230, 255))
    img = c.finish(outline=False)
    img = img.resize((w, h), 0) if (w, h) != (32, 40) else img
    from art_common import add_outline
    if (w, h) != (32, 40):
        # 小サイズは縮小し直し（LANCZOS）→ 輪郭
        img = c.img.resize((w, h), 1)  # LANCZOS=1
        px = img.load()
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                px[x, y] = (r, g, b, 255) if a >= 110 else (0, 0, 0, 0)
    add_outline(img)
    return img


# ---------------------------------------------------------------------------
# 雑魚: 妖精 (16x16, 2 フレーム) / 鬼火 (12x12) / 提灯お化け (16x16)
# ---------------------------------------------------------------------------
FAIRY_COLORS = [
    ((80, 150, 255), (200, 230, 255)),   # 青
    ((255, 90, 90), (255, 210, 200)),    # 赤
    ((90, 220, 120), (210, 255, 210)),   # 緑
    ((255, 210, 60), (255, 245, 190)),   # 黄
]


def draw_fairy(color_idx: int, frame: int):
    col, light = FAIRY_COLORS[color_idx]
    c = Canvas(16, 16, 8)
    wing = mix(light, (255, 255, 255), 0.3)
    if frame == 0:
        c.ellipse(0, 2, 7, 10, wing)
        c.ellipse(9, 2, 16, 10, wing)
    else:
        c.ellipse(1, 5, 7, 12, wing)
        c.ellipse(9, 5, 15, 12, wing)
    # ドレス
    c.poly([(5, 8), (11, 8), (13, 15), (3, 15)], col)
    c.rect(5, 13, 11, 15, shade(col, 0.7))
    # 頭
    c.ellipse(3.5, 0.5, 12.5, 9.5, shade(col, 0.55))
    c.ellipse(5, 3, 11, 9.5, SKIN)
    c.rect(4.5, 1.5, 11.5, 4, shade(col, 0.55))
    c.rect(6, 5.5, 7.4, 7.5, (40, 30, 60))
    c.rect(8.6, 5.5, 10, 7.5, (40, 30, 60))
    return c.finish()


def draw_wisp(frame: int, col=(90, 180, 255)):
    c = Canvas(12, 12, 8)
    sway = 1 if frame else -1
    c.poly([(2, 7), (6 + sway, 0), (10, 7)], col)
    c.ellipse(2, 3, 10, 11.5, col)
    c.ellipse(4, 5, 8, 10, (230, 250, 255))
    c.rect(4.8, 6, 5.8, 7.6, (30, 40, 80))
    c.rect(6.4, 6, 7.4, 7.6, (30, 40, 80))
    return c.finish()


def draw_lantern_ghost(frame: int):
    c = Canvas(16, 16, 8)
    c.rect(4, 0.5, 12, 2.5, (90, 40, 30))
    c.ellipse(2, 1.5, 14, 14.5, (255, 150, 60))
    for yy in (4, 7, 10):
        c.line([(3, yy), (13, yy)], (220, 110, 40), 0.5)
    c.rect(4, 13.5, 12, 15.5, (90, 40, 30))
    # 目と舌
    c.ellipse(4.5, 5, 7.5, 8.5, (255, 255, 255))
    c.ellipse(5.5, 6 + frame, 7, 8, (30, 20, 20))
    c.ellipse(9, 7, 11.5, 8.5, (30, 20, 20))
    c.poly([(7, 10), (10, 10), (9, 13 + frame)], (230, 60, 80))
    return c.finish()


def draw_option(frame: int):
    """自機オプション: 小さな灯籠 (8x8)"""
    c = Canvas(8, 8, 8)
    c.rect(2, 0, 6, 1.2, (110, 50, 30))
    c.ellipse(0.5, 0.8, 7.5, 7.2, (255, 170, 60) if frame == 0 else (255, 200, 90))
    c.ellipse(2, 2.2, 6, 5.8, (255, 240, 170))
    c.rect(2, 6.8, 6, 8, (110, 50, 30))
    return c.finish(alpha_cut=90)
