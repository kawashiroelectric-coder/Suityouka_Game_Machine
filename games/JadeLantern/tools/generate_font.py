#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
翠灯夜行 (JadeLantern) 用サブセットフォント（MISF v1, 12x12）を生成する。

元データ: games/visual_novel/fonts/PixelMplus-20130602/PixelMplus12-Regular.ttf
変換ロジック: games/visual_novel/generate_font.py を流用

使い方:
  python games/JadeLantern/tools/generate_font.py
  python games/JadeLantern/tools/generate_font.py --check   # 会話の行幅チェックのみ

出力:
  fonts/game_font.bin
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
GAME_DIR = TOOLS_DIR.parent
VN_DIR = GAME_DIR.parent / "visual_novel"
TTF_PATH = VN_DIR / "fonts" / "PixelMplus-20130602" / "PixelMplus12-Regular.ttf"
OUT_PATH = GAME_DIR / "fonts" / "game_font.bin"
SOURCES = [GAME_DIR / "JadeLantern.lua", GAME_DIR / "data.lua"]

sys.path.insert(0, str(VN_DIR))
import generate_font as vn_font  # noqa: E402

EXTRA = (
    " !\"#$%&'()*+,-./0123456789:;<=>?@"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
    "…、。「」！？・～★☆◆◇♪×"
)

# 会話 1 行の最大幅（px）。テキスト窓の内側幅に合わせる
MAX_LINE_PX = 200


def collect_codepoints() -> set[int]:
    needed: set[int] = set(vn_font.EXTRA_CODEPOINTS)
    for ch in EXTRA:
        needed.add(ord(ch))
    # コメントの文字は含めず、文字列リテラル内の文字だけを集める（RAM 節約）
    lit = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
    for path in SOURCES:
        if not path.exists():
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            code = line.split("--", 1)[0] if '"' not in line.split("--", 1)[0] else line
            for m in lit.finditer(code):
                for ch in m.group(1):
                    if ord(ch) >= 0x20:
                        needed.add(ord(ch))
    return needed


def text_width(s: str) -> int:
    w = 0
    for ch in s:
        w += 6 if ord(ch) < 0x80 else 12
    return w


def check_lines() -> int:
    """data.lua の会話文字列の行幅を検査する"""
    src = (GAME_DIR / "data.lua").read_text(encoding="utf-8")
    bad = 0
    for m in re.finditer(r'\{\s*"(\w+)",\s*"((?:[^"\\]|\\.)*)"\s*\}', src):
        kind, body = m.group(1), m.group(2)
        if kind in ("g", "t", "h", "s", "c"):
            continue
        lines = body.split("\\n")
        if len(lines) > 2:
            print("3 行以上:", body)
            bad += 1
        for ln in lines:
            if text_width(ln) > MAX_LINE_PX:
                print(f"幅超過 {text_width(ln)}px: {ln}")
                bad += 1
    print("line check:", "OK" if bad == 0 else f"{bad} issue(s)")
    return bad


def main() -> int:
    if "--check" in sys.argv:
        return 1 if check_lines() else 0
    check_lines()
    if not TTF_PATH.exists():
        print("Error: TTF not found:", TTF_PATH, file=sys.stderr)
        return 1
    _, gw, gh, rasterize = vn_font.parse_ttf(TTF_PATH, 12)
    needed = collect_codepoints()
    entries, missing = vn_font.build_entries(None, rasterize, needed)
    if missing:
        print(f"Warning: missing {len(missing)} glyphs: {''.join(missing[:60])}", file=sys.stderr)
    vn_font.write_misf(OUT_PATH, entries, gw, gh, gw)
    print("SD へコピー: fonts/game_font.bin → /games/JadeLantern/fonts/game_font.bin")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
