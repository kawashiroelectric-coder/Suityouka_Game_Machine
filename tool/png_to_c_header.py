#!/usr/bin/env python3
"""
PNG を ST7789 向け RGB565 の C ヘッダー (.h) に変換する。

ファームウェアに画像を埋め込む場合向け。Lua ゲームの SD 用 .bin には image_to_rgb565_bin.py を使う。

例:
    python tool/png_to_c_header.py assets/icon.png
    python tool/png_to_c_header.py assets/icon.png -d assets -n my_icon

必要: pip install pillow
"""

from __future__ import annotations

import argparse
import glob
import re
import sys
from pathlib import Path

try:
    from PIL import Image  # noqa: F401
except ImportError:
    print("Error: Pillow が必要です。  pip install pillow", file=sys.stderr)
    sys.exit(1)

from rgb565_codec import rgba_image_to_rgb565_pixels


def sanitize_identifier(name: str) -> str:
    sanitized = re.sub(r"[^0-9A-Za-z_]", "_", name)
    if re.match(r"^[0-9]", sanitized):
        sanitized = "img_" + sanitized
    return sanitized


def generate_header(name: str, width: int, height: int, pixels: list[int], comment: str | None) -> str:
    guard = sanitize_identifier(name).upper() + "_H"
    lines: list[str] = []
    if comment:
        lines.append(f"// {comment}")
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    safe = sanitize_identifier(name)
    lines.append(f"static const int {safe}_width = {width};")
    lines.append(f"static const int {safe}_height = {height};")
    lines.append(f"static const uint16_t {safe}_pixels[{width} * {height}] = {{")

    values_per_line = 12
    for row in range(height):
        row_values = pixels[row * width : (row + 1) * width]
        if width <= values_per_line:
            line = ",".join(f"0x{val:04X}" for val in row_values)
            lines.append(f"    {line},")
        else:
            for i in range(0, len(row_values), values_per_line):
                chunk = row_values[i : i + values_per_line]
                line = ",".join(f"0x{val:04X}" for val in chunk)
                lines.append(f"    {line},")
    lines.append("};")
    lines.append("")
    lines.append(f"#endif // {guard}")
    return "\n".join(lines)


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
        description="PNG を RGB565 C ヘッダー (.h) に変換",
    )
    parser.add_argument("inputs", nargs="+", help="入力 PNG またはグロブ")
    parser.add_argument("-d", "--output-dir", default="assets", help="出力ディレクトリ")
    parser.add_argument("-n", "--name", help="ヘッダー内の変数名ベース")
    parser.add_argument("--comment", help="ヘッダー先頭コメント")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    inputs = resolve_inputs(args.inputs)
    if not inputs:
        print("入力ファイルが見つかりません。", file=sys.stderr)
        sys.exit(2)

    from PIL import Image

    for input_path in inputs:
        if input_path.suffix.lower() not in {".png", ".apng"}:
            print(f"スキップ (PNG 以外): {input_path}", file=sys.stderr)
            continue

        with Image.open(input_path) as img:
            width, height, pixels = rgba_image_to_rgb565_pixels(img)

        base_name = args.name or input_path.stem
        header_name = sanitize_identifier(base_name)
        output_path = output_dir / f"{input_path.stem}.h"
        header_text = generate_header(
            header_name,
            width,
            height,
            pixels,
            comment=args.comment or f"{input_path.stem}: {width}x{height} RGB565",
        )
        output_path.write_text(header_text, encoding="utf-8")
        print(f"Generated {output_path}")


if __name__ == "__main__":
    main()
