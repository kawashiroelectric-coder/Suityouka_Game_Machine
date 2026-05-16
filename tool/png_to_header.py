#!/usr/bin/env python3
"""Convert PNG images into RGB565 C header files for ST7789 LCD usage."""

from __future__ import annotations

import argparse
import glob
import os
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None  # type: ignore


def require_pillow() -> None:
    if Image is None:
        print(
            "Error: Pillow is required to run this tool. Install with `pip install pillow`.",
            file=sys.stderr,
        )
        sys.exit(1)


def sanitize_identifier(name: str) -> str:
    sanitized = re.sub(r"[^0-9A-Za-z_]", "_", name)
    if re.match(r"^[0-9]", sanitized):
        sanitized = "img_" + sanitized
    return sanitized


def rgba_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def image_to_rgb565_data(image_path: Path) -> tuple[int, int, list[int]]:
    require_pillow()
    with Image.open(image_path) as img:
        img = img.convert("RGBA")
        width, height = img.size
        pixels = []
        for y in range(height):
            for x in range(width):
                r, g, b, a = img.getpixel((x, y))
                if a < 255:
                    # Alpha compositing onto black background.
                    alpha = a / 255.0
                    r = int(round(r * alpha))
                    g = int(round(g * alpha))
                    b = int(round(b * alpha))
                pixels.append(rgba_to_rgb565(r, g, b))
        return width, height, pixels


def generate_header(name: str, width: int, height: int, pixels: list[int], comment: str | None = None) -> str:
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

    row_format = "    "
    values_per_line = 12
    for row in range(height):
        row_start = row * width
        row_values = pixels[row_start : row_start + width]
        if width <= values_per_line:
            line = ",".join(f"0x{val:04X}" for val in row_values)
            lines.append(f"    {line},")
        else:
            chunked = [row_values[i : i + values_per_line] for i in range(0, len(row_values), values_per_line)]
            for chunk in chunked:
                line = ",".join(f"0x{val:04X}" for val in chunk)
                lines.append(f"    {line},")
    lines.append("};")
    lines.append("")
    lines.append(f"#endif // {guard}")
    return "\n".join(lines)


def resolve_inputs(patterns: list[str]) -> list[Path]:
    result: list[Path] = []
    for pattern in patterns:
        expanded = glob.glob(pattern, recursive=True)
        if expanded:
            result.extend(Path(path) for path in expanded)
        else:
            candidate = Path(pattern)
            if candidate.exists():
                result.append(candidate)
    unique_paths = []
    seen = set()
    for path in result:
        resolved = path.resolve()
        if resolved not in seen:
            seen.add(resolved)
            unique_paths.append(path)
    return unique_paths


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert PNG files into RGB565 C header files for ST7789 display.",
    )
    parser.add_argument("inputs", nargs="+", help="Input PNG file(s) or glob pattern(s).")
    parser.add_argument("-d", "--output-dir", default="assets", help="Output directory for generated .h files.")
    parser.add_argument("-n", "--name", help="Base variable name to use in the generated header.")
    parser.add_argument("--comment", help="Optional comment to include at the top of the generated header.")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    input_paths = resolve_inputs(args.inputs)
    if not input_paths:
        print("No matching input files found.", file=sys.stderr)
        sys.exit(2)

    for input_path in input_paths:
        if input_path.suffix.lower() not in {".png", ".apng"}:
            print(f"Skipping non-PNG file: {input_path}", file=sys.stderr)
            continue

        width, height, pixels = image_to_rgb565_data(input_path)
        base_name = args.name or input_path.stem
        header_name = sanitize_identifier(base_name)
        output_path = output_dir / f"{input_path.stem}.h"
        header_text = generate_header(
            header_name,
            width,
            height,
            pixels,
            comment=args.comment or f"{input_path.stem}: {width}x{height} RGB565 image generated from PNG",
        )
        output_path.write_text(header_text, encoding="utf-8")
        print(f"Generated {output_path}")


if __name__ == "__main__":
    main()
