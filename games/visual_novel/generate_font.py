#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ビジュアルノベル用サブセット MISF フォントを生成する。

デフォルト: PixelMplus12-Regular.ttf → 12x12 ネイティブ（拡大なし）
代替: 美咲ゴシック第2 (BDF / misaki.hpp) → 8x8

使い方:
  python generate_font.py

  # 明示的に TTF を指定
  python generate_font.py --from-ttf fonts/PixelMplus-20130602/PixelMplus12-Regular.ttf

  # 美咲 8x8（従来）
  python generate_font.py --from-hpp fonts/misaki.hpp

出力:
  fonts/game_font.bin  … Pico 向け MISF v1 バイナリ
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
TTF_PATH = SCRIPT_DIR / "fonts" / "PixelMplus-20130602" / "PixelMplus12-Regular.ttf"
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


def bytes_per_row(glyph_w: int) -> int:
    return (glyph_w + 7) // 8


def bitmap_from_rows(rows: list[bytes], glyph_w: int, glyph_h: int) -> bytes:
    expected = bytes_per_row(glyph_w) * glyph_h
    blob = b"".join(rows)
    if len(blob) < expected:
        blob += b"\x00" * (expected - len(blob))
    return blob[:expected]


def parse_bdf(path: Path) -> tuple[dict[int, tuple[int, bytes]], int, int]:
    """BDF を読み、codepoint -> (advance, bitmap) に変換（8x8）。"""
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
            rows.append(bytes([value & 0xFF]))
            if len(rows) >= 8:
                break

        while len(rows) < 8:
            rows.append(b"\x00")
        rows = rows[:8]

        if advance > 16:
            advance = 16
        glyphs[cp] = (advance, bitmap_from_rows(rows, 8, 8))

    return glyphs, 8, 8


def parse_misaki_hpp(path: Path) -> tuple[dict[int, tuple[int, bytes]], int, int]:
    """aloseed/misaki の misaki.hpp から glyph を読む（8x8）。"""
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
        else:
            advance = 16
        glyphs[cp] = (advance, bitmap)

    return glyphs, 8, 8


def parse_ttf(path: Path, font_size: int) -> tuple[dict[int, tuple[int, bytes]], int, int]:
    """TrueType（PixelMplus 等）をラスタライズして 12x12 等の固定セルに格納。"""
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError as exc:
        raise RuntimeError("Pillow が必要です: pip install Pillow") from exc

    glyph_w = font_size
    glyph_h = font_size
    bpr = bytes_per_row(glyph_w)
    font = ImageFont.truetype(str(path), font_size)

    def rasterize(cp: int) -> tuple[int, bytes]:
        ch = chr(cp)
        advance_f = font.getlength(ch)
        advance = max(1, min(int(round(advance_f)), glyph_w))
        if cp <= 0x7E or cp == 0x20:
            advance = max(1, min(advance, glyph_w // 2 if glyph_w >= 12 else glyph_w))
        elif advance < glyph_w // 2:
            advance = glyph_w

        im = Image.new("1", (glyph_w, glyph_h), 0)
        draw = ImageDraw.Draw(im)
        draw.text((0, 0), ch, font=font, fill=1)

        row_bytes: list[bytes] = []
        for y in range(glyph_h):
            packed = 0
            for x in range(glyph_w):
                if im.getpixel((x, y)):
                    packed |= 1 << (7 - (x % 8))
                if x % 8 == 7 or x == glyph_w - 1:
                    row_bytes.append(bytes([packed & 0xFF]))
                    packed = 0
        return advance, bitmap_from_rows(row_bytes, glyph_w, glyph_h)

    # 必要 codepoint だけ生成（全 JIS は重い）
    glyphs: dict[int, tuple[int, bytes]] = {}
    return glyphs, glyph_w, glyph_h, rasterize


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
    """name = \"...\" 専用で、セリフ本文に出てこない文字。"""
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


def write_misf(
    path: Path,
    entries: list[tuple[int, int, bytes]],
    glyph_w: int,
    glyph_h: int,
    default_advance: int,
) -> None:
    """MISF v1: header 16 + index 8*N + glyph N*bytes_per_glyph"""
    entries.sort(key=lambda e: e[0])
    bytes_per_glyph = bytes_per_row(glyph_w) * glyph_h
    count = len(entries)

    header = struct.pack(
        "<4sBBBBHHxxxx",
        b"MISF",
        1,
        glyph_w,
        glyph_h,
        default_advance,
        count,
        bytes_per_glyph,
    )

    index_blob = bytearray()
    glyph_blob = bytearray()
    for i, (cp, advance, bitmap) in enumerate(entries):
        if advance > 255:
            advance = 255
        index_blob += struct.pack("<IBBH", cp, advance, 0, i)
        if len(bitmap) != bytes_per_glyph:
            raise ValueError(f"glyph U+{cp:04X}: expected {bytes_per_glyph} bytes, got {len(bitmap)}")
        glyph_blob += bitmap

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + index_blob + glyph_blob)
    print(
        f"Wrote {path} ({count} glyphs, {glyph_w}x{glyph_h}, "
        f"{path.stat().st_size} bytes)"
    )


def build_entries(
    source_glyphs: dict[int, tuple[int, bytes]] | None,
    rasterize,
    needed: set[int],
) -> tuple[list[tuple[int, int, bytes]], list[str]]:
    entries: list[tuple[int, int, bytes]] = []
    missing: list[str] = []

    for cp in sorted(needed):
        if source_glyphs and cp in source_glyphs:
            advance, bitmap = source_glyphs[cp]
            entries.append((cp, advance, bitmap))
        elif rasterize is not None:
            try:
                advance, bitmap = rasterize(cp)
                entries.append((cp, advance, bitmap))
            except (ValueError, OSError):
                missing.append(chr(cp) if cp >= 32 else f"U+{cp:04X}")
        else:
            missing.append(chr(cp) if cp >= 32 else f"U+{cp:04X}")

    return entries, missing


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description="Generate MISF subset font for Pico")
    parser.add_argument(
        "--from-ttf",
        type=Path,
        default=TTF_PATH,
        help="PixelMplus 等 TrueType フォント",
    )
    parser.add_argument(
        "--font-size",
        type=int,
        default=12,
        help="TTF ラスタライズサイズ（PixelMplus12 は 12）",
    )
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
    parser.add_argument(
        "--misaki",
        action="store_true",
        help="美咲 8x8 を強制（TTF より優先しない）",
    )
    args = parser.parse_args()

    source_glyphs: dict[int, tuple[int, bytes]] | None = None
    rasterize = None
    glyph_w = 8
    glyph_h = 8
    default_advance = 8
    source_name = ""

    if not args.misaki and args.from_ttf.exists():
        print("Rasterizing TTF:", args.from_ttf, f"({args.font_size}px)")
        source_glyphs, glyph_w, glyph_h, rasterize = parse_ttf(args.from_ttf, args.font_size)
        default_advance = glyph_w
        source_name = args.from_ttf.name
    elif args.from_hpp.exists():
        print("Parsing misaki.hpp:", args.from_hpp)
        source_glyphs, glyph_w, glyph_h = parse_misaki_hpp(args.from_hpp)
        default_advance = 8
        source_name = "misaki.hpp"
    elif args.from_bdf.exists():
        print("Parsing BDF:", args.from_bdf)
        source_glyphs, glyph_w, glyph_h = parse_bdf(args.from_bdf)
        default_advance = 8
        source_name = "BDF"
    else:
        print("Error: font source not found.", file=sys.stderr)
        print("Option A (default): place PixelMplus12 at", TTF_PATH, file=sys.stderr)
        print("Option B: place misaki.hpp at", HPP_PATH, file=sys.stderr)
        print("Option C: place misaki_gothic_2nd.bdf at", BDF_PATH, file=sys.stderr)
        return 1

    needed = collect_codepoints()
    entries, missing = build_entries(source_glyphs, rasterize, needed)

    if missing:
        print(f"Warning ({source_name}): missing {len(missing)} glyphs:", file=sys.stderr)
        print("  " + "".join(missing[:60]), file=sys.stderr)

    if not entries:
        print("Error: no glyphs generated", file=sys.stderr)
        return 1

    write_misf(OUT_PATH, entries, glyph_w, glyph_h, default_advance)

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
