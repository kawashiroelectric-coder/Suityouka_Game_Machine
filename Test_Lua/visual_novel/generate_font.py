#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
美咲ゴシック第2 (BDF) からビジュアルノベル用サブセット MISF フォントを生成する。

使い方:
  1. 美咲フォント BDF を入手（フリー）
     http://littlelimit.net/misaki.htm
     → misaki_bdf_2021-05-05.zip 内の misaki_gothic_2nd.bdf
  2. fonts/misaki_gothic_2nd.bdf に配置
  3. python generate_font.py

出力:
  fonts/game_font.bin  … Pico 向け MISF v1 バイナリ
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
BDF_PATH = SCRIPT_DIR / "fonts" / "misaki_gothic_2nd.bdf"
HPP_PATH = SCRIPT_DIR / "fonts" / "misaki.hpp"
OUT_PATH = SCRIPT_DIR / "fonts" / "game_font.bin"
SCENARIO_FILES = [
    SCRIPT_DIR / "visual_novel.lua",
    SCRIPT_DIR / "scenario.lua",
]

# シナリオ外でも使う記号・ASCII
EXTRA_CODEPOINTS = [
    0x20,
    0x3F,  # ?
    0x2014,  # —
    0x2026,  # …
    0x3001,  # 、
    0x3002,  # 。
    0x300C,  # 「
    0x300D,  # 」
    0xFF01,  # ！
    0xFF1F,  # ？
]


def parse_bdf(path: Path) -> dict[int, tuple[int, bytes]]:
    """BDF を読み、codepoint -> (advance, 8x8 bitmap) に変換。"""
    text = path.read_text(encoding="utf-8", errors="replace")
    glyphs: dict[int, tuple[int, bytes]] = {}

    for block in text.split("STARTCHAR")[1:]:
        name_end = block.find("\n")
        if name_end < 0:
            continue
        body = block[name_end + 1 :]

        enc_match = re.search(r"ENCODING\s+(-?\d+)", body)
        if not enc_match:
            continue
        cp = int(enc_match.group(1))
        if cp < 0:
            continue

        dw_match = re.search(r"DWIDTH\s+(-?\d+)\s+(-?\d+)", body)
        advance = int(dw_match.group(1)) if dw_match else 8
        if advance <= 0:
            advance = 8

        bbx_match = re.search(r"BBX\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", body)
        if not bbx_match:
            continue
        bbx_w = int(bbx_match.group(1))
        bbx_h = int(bbx_match.group(2))

        bm_start = body.find("BITMAP")
        if bm_start < 0:
            continue
        bm_start = body.find("\n", bm_start) + 1
        bm_end = body.find("ENDCHAR", bm_start)
        bitmap_lines = body[bm_start:bm_end].strip().splitlines()

        rows = []
        for line in bitmap_lines:
            line = line.strip()
            if not line:
                continue
            value = int(line, 16)
            if bbx_w <= 8:
                rows.append(value & 0xFF)
            else:
                rows.append((value >> 8) & 0xFF)
            if len(rows) >= 8:
                break

        while len(rows) < 8:
            rows.append(0)
        rows = rows[:8]

        # 全角は 16px 送り、半角は 8px（美咲 BDF の DWIDTH に従う）
        if advance > 16:
            advance = 16
        glyphs[cp] = (advance, bytes(rows))

    return glyphs


def parse_misaki_hpp(path: Path) -> dict[int, tuple[int, bytes]]:
    """aloseed/misaki の misaki.hpp から glyph を読む（美咲ゴシック第2 同梱データ）。"""
    text = path.read_text(encoding="utf-8", errors="replace")

    uni_match = re.search(r"const uint16_t misaki_uni\[\] = \{([^;]+)\};", text, re.S)
    data_match = re.search(r"const uint8_t misaki_data\[\]\[8\] = \{([^;]+)\};", text, re.S)
    if not uni_match or not data_match:
        raise ValueError("misaki.hpp: misaki_uni / misaki_data not found")

    uni_vals = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", uni_match.group(1))]
    if len(uni_vals) < 65536:
        raise ValueError(f"misaki.hpp: misaki_uni too short ({len(uni_vals)})")

    glyphs: dict[int, tuple[int, bytes]] = {}
    rows = re.findall(r"\{([^}]+)\}", data_match.group(1))
    data_rows: list[bytes] = []
    for row in rows:
        nums = [int(x.strip(), 16) for x in row.split(",") if "0x" in x.lower()]
        if len(nums) >= 8:
            data_rows.append(bytes(nums[:8]))

    for cp in range(len(uni_vals)):
        idx = uni_vals[cp]
        if idx == 0xFFFF or idx >= len(data_rows):
            continue
        bitmap = data_rows[idx]
        if cp <= 0x7E or (0xFF61 <= cp <= 0xFF9F):
            advance = 8
        elif cp >= 0xFF01:
            advance = 16
        else:
            advance = 16
        glyphs[cp] = (advance, bitmap)

    return glyphs


def advance_for_codepoint(cp: int) -> int:
    if cp <= 0x7E or (0xFF61 <= cp <= 0xFF9F):
        return 8
    if cp >= 0xFF01:
        return 16
    return 16


def collect_codepoints() -> set[int]:
    chars: set[int] = set(EXTRA_CODEPOINTS)
    for cp in range(0x21, 0x7F):
        chars.add(cp)

    for path in SCENARIO_FILES:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        for ch in text:
            if ch in "\r\n\t":
                continue
            chars.add(ord(ch))

    return chars


def find_name_only_codepoints() -> list[str]:
    """name = \"...\" 専用で、セリフ本文に出てこない文字（フォント更新漏れで ? になりやすい）。"""
    from collections import Counter

    body_counts: Counter[int] = Counter()
    name_counts: Counter[int] = Counter()

    name_re = re.compile(r'name\s*=\s*"([^"]*)"')

    for path in SCENARIO_FILES:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        for m in name_re.finditer(text):
            for ch in m.group(1):
                name_counts[ord(ch)] += 1

        stripped = name_re.sub("", text)
        for ch in stripped:
            if ch in "\r\n\t":
                continue
            body_counts[ord(ch)] += 1

    only_in_name: list[str] = []
    for cp, _ in name_counts.items():
        if cp not in body_counts and cp >= 0x80:
            only_in_name.append(chr(cp))

    only_in_name.sort(key=lambda s: ord(s))
    return only_in_name


def write_misf(path: Path, entries: list[tuple[int, int, bytes]]) -> None:
    """MISF v1: header 16 + index 8*N + glyph 8*N"""
    entries.sort(key=lambda e: e[0])
    glyph_w = 8
    glyph_h = 8
    default_advance = 8
    count = len(entries)

    header = struct.pack(
        "<4sBBBBHHxxxx",
        b"MISF",
        1,
        glyph_w,
        glyph_h,
        default_advance,
        count,
        8,
    )

    index_blob = bytearray()
    glyph_blob = bytearray()
    for i, (cp, advance, bitmap) in enumerate(entries):
        index_blob += struct.pack("<IBBH", cp, advance, 0, i)
        glyph_blob += bitmap

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + index_blob + glyph_blob)
    print(f"Wrote {path} ({count} glyphs, {path.stat().st_size} bytes)")


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description="Generate MISF subset font for Pico")
    parser.add_argument(
        "--from-hpp",
        type=Path,
        default=HPP_PATH,
        help="misaki.hpp path (aloseed/misaki, 美咲ゴシック第2)",
    )
    parser.add_argument(
        "--from-bdf",
        type=Path,
        default=BDF_PATH,
        help="misaki_gothic_2nd.bdf path (official BDF)",
    )
    args = parser.parse_args()

    source_glyphs: dict[int, tuple[int, bytes]] | None = None
    source_name = ""

    if args.from_hpp.exists():
        print("Parsing misaki.hpp:", args.from_hpp)
        source_glyphs = parse_misaki_hpp(args.from_hpp)
        source_name = "misaki.hpp"
    elif args.from_bdf.exists():
        print("Parsing BDF:", args.from_bdf)
        source_glyphs = parse_bdf(args.from_bdf)
        source_name = "BDF"
    else:
        print("Error: font source not found.", file=sys.stderr)
        print("Option A: place misaki.hpp at", HPP_PATH, file=sys.stderr)
        print("  (clone https://github.com/aloseed/misaki and copy src/misaki.hpp)", file=sys.stderr)
        print("Option B: place misaki_gothic_2nd.bdf at", BDF_PATH, file=sys.stderr)
        print("  (download from http://littlelimit.net/misaki.htm)", file=sys.stderr)
        return 1

    needed = collect_codepoints()

    entries: list[tuple[int, int, bytes]] = []
    missing: list[str] = []

    for cp in sorted(needed):
        if cp in source_glyphs:
            advance, bitmap = source_glyphs[cp]
            entries.append((cp, advance, bitmap))
        else:
            missing.append(chr(cp) if cp >= 32 else f"U+{cp:04X}")

    if missing:
        print(f"Warning ({source_name}): missing {len(missing)} glyphs:", file=sys.stderr)
        print("  " + "".join(missing[:60]), file=sys.stderr)

    if not entries:
        print("Error: no glyphs generated", file=sys.stderr)
        return 1

    write_misf(OUT_PATH, entries)

    name_only = find_name_only_codepoints()
    if name_only:
        print("")
        print("【注意】名前 (name=) 専用の文字:", "".join(name_only))
        print("  → セリフには無いため、名前を変えたあと game_font.bin の SD コピーを忘れると ? になります。")

    print("")
    print("SD へコピー: fonts/game_font.bin → /visual_novel/fonts/game_font.bin")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
