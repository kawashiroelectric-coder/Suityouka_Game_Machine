#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
差し替え用の立ち絵（img/face_<名>2.png など）を、ゲームの立ち絵 80x112 に変換する。

  python games/JadeLantern/tools/import_portraits.py

- 背景（画像の端とつながった単色部分）を透明にする
- 目の高さと頭の大きさが元の立ち絵とそろうように拡大縮小・切り抜き
- face_hotaru 以外は左右反転（画面右側に表示されるボス用）
- img/face_<名>.bin（RGB565・マゼンタ透過）と確認用 img/face_<名>.png を上書き

generate_images.py を実行すると立ち絵が生成画像に戻るので、そのあとにこのスクリプトを実行する。
"""

from __future__ import annotations

import sys
from collections import deque
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from art_common import write_bin  # noqa: E402

GAME_DIR = Path(__file__).resolve().parent.parent
IMG_DIR = GAME_DIR / "img"

OUT_W, OUT_H = 80, 112
EYE_Y = 50          # 出力での目の高さ
HEAD_TO_EYE = 30    # 出力での「髪の上端 → 目」の距離（頭の大きさの基準）
CENTER_X = 40       # 出力での顔の中心

# 元画像での 顔の中心 x / 髪の上端 y / 目の高さ y（ピクセル）
PORTRAITS = {
    # key: (元ファイル名, center_x, hair_top_y, eye_y, 反転)
    "hotaru":  ("face_hotaru2.png",  62, 14, 56, False),
    "chirori": ("face_chirori2.png", 71, 15, 56, True),
    "kohaku":  ("indexed(2).png",    69, 18, 57, True),
    "shizuku": ("face_shizuku2.png", 79, 16, 45, True),
    "nejika":  ("face_nejika2.png",  67, 16, 57, True),
    "raika":   ("face_raika2.png",   85, 24, 72, True),
    "mikoto":  ("face_mikoto2.png",  60, 18, 57, True),
}

BG_TOLERANCE = 40   # 背景色との色差（RGB 距離）
EDGE_TOLERANCE = 90  # 輪郭のすぐ外側で背景とみなす色差


def remove_background(im: Image.Image) -> Image.Image:
    """画像の端とつながった背景色の領域を透明にする"""
    rgb = im.convert("RGB")
    w, h = rgb.size
    px = rgb.load()
    corners = [px[0, 0], px[w - 1, 0], px[0, h - 1], px[w - 1, h - 1]]
    bg = max(set(corners), key=corners.count)

    def is_bg(c):
        return sum((a - b) ** 2 for a, b in zip(c, bg)) <= BG_TOLERANCE ** 2

    seen = [[False] * w for _ in range(h)]
    q = deque()
    for x in range(w):
        q.append((x, 0)); q.append((x, h - 1))
    for y in range(h):
        q.append((0, y)); q.append((w - 1, y))
    while q:
        x, y = q.popleft()
        if not (0 <= x < w and 0 <= y < h) or seen[y][x]:
            continue
        if not is_bg(px[x, y]):
            continue
        seen[y][x] = True
        q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    # 縁に残る背景寄りの中間色（アンチエイリアス）を 2 周ぶん削る
    def near_bg(c):
        return sum((a - b) ** 2 for a, b in zip(c, bg)) <= EDGE_TOLERANCE ** 2

    for _ in range(2):
        ring = []
        for y in range(h):
            for x in range(w):
                if seen[y][x] or not near_bg(px[x, y]):
                    continue
                if any(0 <= x + dx < w and 0 <= y + dy < h and seen[y + dy][x + dx]
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    ring.append((x, y))
        for x, y in ring:
            seen[y][x] = True
    out = rgb.convert("RGBA")
    op = out.load()
    for y in range(h):
        for x in range(w):
            if seen[y][x]:
                op[x, y] = (0, 0, 0, 0)
    return out


def fit(im: Image.Image, cx: float, top: float, eye: float) -> Image.Image:
    s = HEAD_TO_EYE / float(eye - top)
    new_w = max(1, round(im.width * s))
    new_h = max(1, round(im.height * s))
    # 縮小は面積平均、拡大は最近傍（ドット感を残す）
    resample = Image.BOX if s < 1 else Image.NEAREST
    # 透明部分の色がにじまないよう、乗算済みアルファで縮小
    scaled = im.convert("RGBa").resize((new_w, new_h), resample).convert("RGBA")
    out = Image.new("RGBA", (OUT_W, OUT_H), (0, 0, 0, 0))
    ox = round(CENTER_X - cx * s)
    oy = round(EYE_Y - eye * s)
    out.alpha_composite(scaled, (ox, oy)) if ox >= 0 and oy >= 0 else out.paste(scaled, (ox, oy), scaled)
    # アルファは 2 値に（本体は透過色 1 色のみ）
    a = out.getchannel("A").point(lambda v: 255 if v >= 128 else 0)
    out.putalpha(a)
    return out


def main() -> int:
    for key, (src, cx, top, eye, flip) in PORTRAITS.items():
        path = IMG_DIR / src
        if not path.exists():
            print(f"skip {key}: {src} がありません")
            continue
        im = remove_background(Image.open(path))
        out = fit(im, cx, top, eye)
        if flip:
            out = out.transpose(Image.FLIP_LEFT_RIGHT)
        write_bin(IMG_DIR / f"face_{key}.bin", out)
        out.save(IMG_DIR / f"face_{key}.png")
        print(f"img/face_{key}.bin  <- {src}{'（左右反転）' if flip else ''}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
