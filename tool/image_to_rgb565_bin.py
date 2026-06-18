#!/usr/bin/env python3
"""
画像 (PNG/JPG/BMP 等) を RGB565 リトルエンディアンの .bin に変換する。

SD カード用アセットおよび Lua の machine.load_image / draw_bg_stream 向け。

例:
    python tool/image_to_rgb565_bin.py sprite.png
    python tool/image_to_rgb565_bin.py sprite.png -o sd/sprite.bin
    python tool/image_to_rgb565_bin.py sprite.png --resize 100x100
    python tool/image_to_rgb565_bin.py *.png -d output/

必要: pip install pillow
"""

from __future__ import annotations

import argparse
import glob
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Error: Pillow が必要です。  pip install pillow", file=sys.stderr)
    sys.exit(1)

from rgb565_codec import image_to_rgb565_bytes, parse_size, write_rgb565_bin

VALID_EXT = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp"}


def convert(src: Path, dst: Path, resize: tuple[int, int] | None) -> None:
    with Image.open(src) as img:
        if resize:
            img = img.resize(resize, Image.LANCZOS)
        width, height, data = image_to_rgb565_bytes(img)
    write_rgb565_bin(dst, data)
    print(f"{src} -> {dst}  ({width}x{height}, {len(data)} bytes)")


def resolve_inputs(patterns: list[str]) -> list[Path]:
    result: list[Path] = []
    seen: set[Path] = set()
    for pattern in patterns:
        expanded = glob.glob(pattern, recursive=True)
        paths = [Path(p) for p in expanded] if expanded else [Path(pattern)]
        for path in paths:
            resolved = path.resolve()
            if resolved not in seen:
                seen.add(resolved)
                result.append(path)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(
        description="画像を RGB565 .bin に変換 (SD / Lua 用)",
    )
    parser.add_argument("inputs", nargs="+", help="入力画像またはグロブ")
    parser.add_argument("-o", "--output", help="出力 .bin（単一入力時）")
    parser.add_argument("-d", "--output-dir", help="出力ディレクトリ（複数入力時）")
    parser.add_argument("--resize", type=parse_size, help="リサイズ (例: 64x64, 320x168)")
    args = parser.parse_args()

    inputs = [p for p in resolve_inputs(args.inputs) if p.suffix.lower() in VALID_EXT]
    if not inputs:
        print("対象の画像ファイルが見つかりません。", file=sys.stderr)
        sys.exit(2)

    if args.output and len(inputs) == 1:
        convert(inputs[0], Path(args.output), args.resize)
        return

    out_dir = Path(args.output_dir) if args.output_dir else None
    if out_dir:
        out_dir.mkdir(parents=True, exist_ok=True)

    for src in inputs:
        dst = (out_dir / src.stem if out_dir else src.with_suffix("")).with_suffix(".bin")
        convert(src, dst, args.resize)


if __name__ == "__main__":
    main()
