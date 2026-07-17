#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
しょうぎ用サブセット MISF を、visual_novel の美咲フォント生データから生成する。

元データ:
  games/visual_novel/fonts/misaki.hpp

使い方:
  cd games/Shogi
  python generate_font.py

出力:
  fonts/game_font.bin
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
VN_DIR = SCRIPT_DIR.parent / "visual_novel"
MISAKI_HPP = VN_DIR / "fonts" / "misaki.hpp"
OUT_PATH = SCRIPT_DIR / "fonts" / "game_font.bin"
SHOGI_LUA = SCRIPT_DIR / "Shogi.lua"

# visual_novel の変換ロジックを流用
sys.path.insert(0, str(VN_DIR))
import generate_font as vn_font  # noqa: E402

# 盤面・UI で確実に入れたい文字（Lua から漏れても拾う）
EXTRA_CHARS = (
    " !"
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    ":+-/>?"
    "歩と香桂銀金角馬飛龍王玉成同打持駒杏圭全"
    "先後手番詰将棋上下難易度選終了妙案絶望待機"
    "勝ち負け引き分け河童もみじあなたのもちごま"
    "やさしいノーマルつよいふつうレベルをえらんでね"
    "盤上を選んでもう一度開始おわる指して"
    "考中対戦しようかかってこい"
    "つづきから記録新規"
    "…！？「」"
)


def collect_codepoints() -> set[int]:
    needed: set[int] = set(vn_font.EXTRA_CODEPOINTS)
    for cp in range(0x21, 0x7F):
        needed.add(cp)
    for ch in EXTRA_CHARS:
        needed.add(ord(ch))
    if SHOGI_LUA.exists():
        text = SHOGI_LUA.read_text(encoding="utf-8")
        for ch in text:
            if ch in "\r\n\t":
                continue
            o = ord(ch)
            if o >= 0x20:
                needed.add(o)
    return needed


def main() -> int:
    if not MISAKI_HPP.exists():
        print("Error: misaki.hpp not found:", MISAKI_HPP, file=sys.stderr)
        return 1

    print("Parsing misaki.hpp:", MISAKI_HPP)
    source_glyphs, glyph_w, glyph_h = vn_font.parse_misaki_hpp(MISAKI_HPP)
    needed = collect_codepoints()
    entries, missing = vn_font.build_entries(source_glyphs, None, needed)

    if missing:
        print(f"Warning: missing {len(missing)} glyphs:", file=sys.stderr)
        print("  " + "".join(missing[:80]), file=sys.stderr)

    if not entries:
        print("Error: no glyphs generated", file=sys.stderr)
        return 1

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    vn_font.write_misf(OUT_PATH, entries, glyph_w, glyph_h, 8)
    print("")
    print("SD へコピー: fonts/game_font.bin → /games/Shogi/fonts/game_font.bin")
    print("ライセンス: visual_novel/fonts/LICENSE.txt （美咲フォント）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
