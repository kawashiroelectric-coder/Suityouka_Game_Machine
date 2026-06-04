#!/usr/bin/env python3
"""
PNG / JPG / BMP 画像を RGB565 リトルエンディアンのバイナリファイルに変換する。
生成された .bin は SD カードに置き、Lua の machine.load_image() で読み込める。

使い方:
    python png_to_rgb565bin.py sprite.png                  # → sprite.bin (元サイズ)
    python png_to_rgb565bin.py sprite.png -o sd/sprite.bin  # 出力先を指定
    python png_to_rgb565bin.py sprite.png --resize 64x64    # リサイズ
    python png_to_rgb565bin.py *.png -d sd/                 # 複数一括変換

必要パッケージ:
    pip install pillow
"""

from __future__ import annotations

import argparse
import glob
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Error: Pillow が必要です。  pip install pillow", file=sys.stderr)
    sys.exit(1)


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def convert(src: Path, dst: Path, resize: tuple[int, int] | None) -> None:
    with Image.open(src) as img:
        img = img.convert("RGB")
        if resize:
            img = img.resize(resize, Image.LANCZOS)
        w, h = img.size
        data = bytearray(w * h * 2)
        idx = 0
        for y in range(h):
            for x in range(w):
                r, g, b = img.getpixel((x, y))
                struct.pack_into("<H", data, idx, rgb888_to_rgb565(r, g, b))
                idx += 2
        dst.write_bytes(data)
        print(f"{src} -> {dst}  ({w}x{h}, {len(data)} bytes)")


def parse_resize(s: str) -> tuple[int, int]:
    parts = s.lower().split("x")
    if len(parts) != 2:
        raise argparse.ArgumentTypeError(f"'{s}' は WxH 形式で指定してください (例: 64x64)")
    return int(parts[0]), int(parts[1])


def resolve_inputs(patterns: list[str]) -> list[Path]:
    result: list[Path] = []
    seen: set[Path] = set()
    for pat in patterns:
        expanded = glob.glob(pat, recursive=True)
        paths = [Path(p) for p in expanded] if expanded else [Path(pat)]
        for p in paths:
            rp = p.resolve()
            if rp not in seen:
                seen.add(rp)
                result.append(p)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(
        description="画像を RGB565 バイナリ (.bin) に変換する (SD カード → machine.load_image 用)",
    )
    parser.add_argument("inputs", nargs="+", help="入力画像 (PNG/JPG/BMP) またはグロブパターン")
    parser.add_argument("-o", "--output", help="出力ファイル名 (単一入力時のみ)")
    parser.add_argument("-d", "--output-dir", help="出力ディレクトリ (複数入力時)")
    parser.add_argument("--resize", type=parse_resize, help="リサイズ (例: 64x64, 320x240)")
    args = parser.parse_args()

    inputs = resolve_inputs(args.inputs)
    valid_ext = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp"}
    inputs = [p for p in inputs if p.suffix.lower() in valid_ext]

    if not inputs:
        print("対象の画像ファイルが見つかりません。", file=sys.stderr)
        sys.exit(2)

    if args.output and len(inputs) == 1:
        dst = Path(args.output)
        dst.parent.mkdir(parents=True, exist_ok=True)
        convert(inputs[0], dst, args.resize)
        return

    out_dir = Path(args.output_dir) if args.output_dir else None
    if out_dir:
        out_dir.mkdir(parents=True, exist_ok=True)

    for src in inputs:
        dst = (out_dir / src.stem if out_dir else src.with_suffix("")).with_suffix(".bin")
        convert(src, dst, args.resize)


if __name__ == "__main__":
    main()
