#!/usr/bin/env python3
"""
RGB565 リトルエンディアンの .bin を PNG に戻す（プレビュー・デバッグ用）。

例:
    python tool/rgb565_bin_to_png.py tiles.bin --size 128x128
    python tool/rgb565_bin_to_png.py hero.bin -o hero.png --size 128x168
    python tool/rgb565_bin_to_png.py Test_Lua/visual_novel/images/bg/*.bin --size 320x168

必要: pip install pillow
"""

from __future__ import annotations

import argparse
import glob
import sys
from pathlib import Path

from rgb565_codec import parse_size, read_rgb565_bin, rgb565_bytes_to_image

try:
    from PIL import Image  # noqa: F401
except ImportError:
    print("Error: Pillow が必要です。  pip install pillow", file=sys.stderr)
    sys.exit(1)


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


def convert(src: Path, dst: Path, size: tuple[int, int]) -> None:
    width, height = size
    data = read_rgb565_bin(src)
    image = rgb565_bytes_to_image(data, width, height)
    dst.parent.mkdir(parents=True, exist_ok=True)
    image.save(dst)
    print(f"{src} -> {dst}  ({width}x{height})")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="RGB565 .bin を PNG に変換（プレビュー用）",
    )
    parser.add_argument("inputs", nargs="+", help="入力 .bin またはグロブ")
    parser.add_argument("-o", "--output", help="出力 PNG（単一入力時。省略時は .bin と同じ場所）")
    parser.add_argument("-d", "--output-dir", help="出力ディレクトリ（複数入力時）")
    parser.add_argument(
        "--size",
        required=True,
        type=parse_size,
        help="画像サイズ WxH（例: 320x168, 128x128）",
    )
    args = parser.parse_args()

    inputs = [p for p in resolve_inputs(args.inputs) if p.suffix.lower() == ".bin"]
    if not inputs:
        print("対象の .bin ファイルが見つかりません。", file=sys.stderr)
        sys.exit(2)

    if args.output and len(inputs) == 1:
        convert(inputs[0], Path(args.output), args.size)
        return

    out_dir = Path(args.output_dir) if args.output_dir else None
    for src in inputs:
        if out_dir:
            dst = out_dir / f"{src.stem}.png"
        else:
            dst = src.with_suffix(".png")
        convert(src, dst, args.size)


if __name__ == "__main__":
    main()
