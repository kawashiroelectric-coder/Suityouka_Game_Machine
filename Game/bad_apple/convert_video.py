#!/usr/bin/env python3
"""
MP4 を 30 FPS の 1 ビット白黒フレーム列（差分圧縮）に変換する。

出力は既定で frames.pack（全フレームを 1 ファイルに格納）です。
SD へのコピーが 1 ファイルで済むため、数千個の .bin を個別コピーする必要はありません。

各フレーム blob の形式（自動判別）:
  - 0 バイト          … SKIP（前フレームと同一、SD 読み込みなし）
  - FRAME_BYTES バイト … フルフレーム（MSB=左、1=白 0=黒）
  - それ以外          … 差分: [row_count:u8] + row_count × ([y:u8] + row_bytes)

実行例:
  cd Test_Lua/bad_apple
  python convert_video.py
  python convert_video.py --max-frames 300
  python convert_video.py --split-frames   # デバッグ用に個別 .bin も出力

必要: ffmpeg（PATH）、pip install pillow
"""

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO_TOOL = ROOT.parent.parent / "tool"
sys.path.insert(0, str(REPO_TOOL))

from bw_pack import write_pack  # noqa: E402
from rgb565_codec import parse_size, rgb888_to_rgb565, write_rgb565_bin  # noqa: E402

DEFAULT_MP4 = ROOT / "bad-apple.mp4"
DEFAULT_MP4_ALT = ROOT / "bad apple.mp4"
FRAMES_DIR = ROOT / "frames"
FRAMES_PACK = ROOT / "frames.pack"
LUA_FILE = ROOT / "bad_apple.lua"
META_BEGIN = "-- VIDEO_META_BEGIN (auto-patched by convert_video.py)"
META_END = "-- VIDEO_META_END"


def resolve_mp4(path: Path | None) -> Path:
    if path and path.is_file():
        return path
    if DEFAULT_MP4.is_file():
        return DEFAULT_MP4
    if DEFAULT_MP4_ALT.is_file():
        return DEFAULT_MP4_ALT
    raise FileNotFoundError(
        f"MP4 が見つかりません: {DEFAULT_MP4} または {DEFAULT_MP4_ALT}"
    )


def pack_frame_1bit(raw: bytes, width: int, height: int, threshold: int = 128) -> bytes:
    need = width * height * 3
    if len(raw) < need:
        return b""
    row_bytes = (width + 7) // 8
    out = bytearray(row_bytes * height)
    i = 0
    for _y in range(height):
        for x in range(width):
            r, g, b = raw[i], raw[i + 1], raw[i + 2]
            i += 3
            if (r + g + b) // 3 >= threshold:
                byte_idx = (_y * row_bytes) + (x // 8)
                bit = 7 - (x % 8)
                out[byte_idx] |= 1 << bit
    return bytes(out)


def encode_frame_delta(prev: bytes, curr: bytes, row_bytes: int, height: int) -> bytes:
    if prev == curr:
        return b""
    changed: list[tuple[int, bytes]] = []
    for y in range(height):
        off = y * row_bytes
        row = curr[off : off + row_bytes]
        if row != prev[off : off + row_bytes]:
            changed.append((y, row))
    frame_bytes = row_bytes * height
    delta_size = 1 + len(changed) * (1 + row_bytes)
    if delta_size >= frame_bytes:
        return curr
    out = bytearray([len(changed)])
    for y, row in changed:
        out.append(y)
        out.extend(row)
    return bytes(out)


def frame_path(out_dir: Path, index: int) -> Path:
    return out_dir / f"f{index:05d}.bin"


def patch_lua_meta(width: int, height: int, fps: int, frame_count: int) -> None:
    if not LUA_FILE.is_file():
        return
    row_bytes = (width + 7) // 8
    frame_bytes = row_bytes * height
    text = LUA_FILE.read_text(encoding="utf-8")
    block = (
        f"{META_BEGIN}\n"
        f"local FRAME_W = {width}\n"
        f"local FRAME_H = {height}\n"
        f"local FRAME_FPS = {fps}\n"
        f"local FRAME_COUNT = {frame_count}\n"
        f"local FRAME_BYTES = {frame_bytes}\n"
        f"local FRAME_DELTA = true\n"
        f"{META_END}"
    )
    pattern = re.compile(
        re.escape(META_BEGIN) + r"[\s\S]*?" + re.escape(META_END),
        re.MULTILINE,
    )
    if not pattern.search(text):
        print(f"警告: {LUA_FILE.name} に VIDEO_META ブロックがありません", file=sys.stderr)
        return
    LUA_FILE.write_text(pattern.sub(block, text, count=1), encoding="utf-8")
    print(f"Updated {LUA_FILE.name} (FRAME_COUNT={frame_count})")


def unpack_frame_1bit(data: bytes, width: int, height: int) -> list[list[bool]]:
    row_bytes = (width + 7) // 8
    rows: list[list[bool]] = []
    for y in range(height):
        line: list[bool] = []
        base = y * row_bytes
        for x in range(width):
            byte = data[base + x // 8]
            bit = 7 - (x % 8)
            line.append(bool((byte >> bit) & 1))
        rows.append(line)
    return rows


def write_preview(frame_data: bytes, preview_path: Path, width: int, height: int) -> None:
    try:
        from PIL import Image
    except ImportError:
        print("プレビュー生成をスキップ（Pillow 未インストール）", file=sys.stderr)
        return

    packed = frame_data
    if len(packed) != (width + 7) // 8 * height:
        print("プレビュー生成をスキップ（フレーム1がフル形式でない）", file=sys.stderr)
        return

    bits = unpack_frame_1bit(packed, width, height)
    img = Image.new("RGB", (width, height))
    for y in range(height):
        for x in range(width):
            img.putpixel((x, y), (255, 255, 255) if bits[y][x] else (0, 0, 0))
    preview = img.resize((100, 100), Image.LANCZOS)
    pw, ph = preview.size
    pdata = bytearray()
    for y in range(ph):
        for x in range(pw):
            r, g, b = preview.getpixel((x, y))
            pdata.extend(struct.pack("<H", rgb888_to_rgb565(r, g, b)))
    write_rgb565_bin(preview_path, bytes(pdata))
    print(f"Preview -> {preview_path} ({pw}x{ph})")


def remove_split_frames(out_dir: Path) -> None:
    if not out_dir.is_dir():
        return
    removed = 0
    for old in out_dir.glob("f*.bin"):
        old.unlink()
        removed += 1
    if removed:
        print(f"Removed {removed} old split frame files from {out_dir.name}/")
    try:
        out_dir.rmdir()
    except OSError:
        pass


def convert(
    mp4: Path,
    pack_path: Path,
    split_dir: Path | None,
    width: int,
    height: int,
    fps: int,
    max_frames: int | None,
    threshold: int,
    clean_split: bool,
) -> int:
    if split_dir is not None:
        split_dir.mkdir(parents=True, exist_ok=True)

    vf = (
        f"fps={fps},"
        f"scale={width}:{height}:force_original_aspect_ratio=decrease:flags=lanczos,"
        f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:black"
    )
    cmd = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(mp4),
        "-an",
        "-vf",
        vf,
    ]
    if max_frames is not None:
        cmd.extend(["-frames:v", str(max_frames)])
    cmd.extend(["-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"])
    print(f"ffmpeg: {' '.join(cmd)}")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    frame_bytes_rgb = width * height * 3
    row_bytes = (width + 7) // 8
    packed_bytes = row_bytes * height
    index = 0
    prev_packed = b""
    total_out = 0
    n_skip = 0
    n_delta = 0
    n_full = 0
    blobs: list[bytes] = []
    assert proc.stdout is not None
    while True:
        if max_frames is not None and index >= max_frames:
            break
        raw = proc.stdout.read(frame_bytes_rgb)
        if not raw or len(raw) < frame_bytes_rgb:
            break
        index += 1
        packed = pack_frame_1bit(raw, width, height, threshold)
        if len(packed) != packed_bytes:
            raise RuntimeError(f"pack failed at frame {index}")

        if index == 1:
            out_data = packed
            n_full += 1
        else:
            out_data = encode_frame_delta(prev_packed, packed, row_bytes, height)
            if len(out_data) == 0:
                n_skip += 1
            elif len(out_data) == packed_bytes:
                n_full += 1
            else:
                n_delta += 1

        blobs.append(out_data)
        if split_dir is not None:
            write_rgb565_bin(frame_path(split_dir, index), out_data)
        total_out += len(out_data)
        prev_packed = packed
        if index % 500 == 0 or index <= 3:
            print(f"  frame {index:5d} ({len(out_data)} bytes)")

    stderr = proc.stderr.read().decode("utf-8", errors="replace") if proc.stderr else ""
    proc.wait()
    if proc.returncode != 0 and index == 0:
        print(stderr, file=sys.stderr)
        raise RuntimeError(f"ffmpeg failed (code {proc.returncode})")

    if index == 0:
        raise RuntimeError("フレームが 1 枚も出力されませんでした")

    write_pack(pack_path, blobs)
    print(f"Pack -> {pack_path} ({pack_path.stat().st_size / (1024 * 1024):.1f} MB, {index} frames)")

    if split_dir is not None:
        for old in split_dir.glob("f*.bin"):
            try:
                n = int(old.stem[1:])
            except ValueError:
                continue
            if n > index:
                old.unlink()
    elif clean_split:
        remove_split_frames(FRAMES_DIR)

    patch_lua_meta(width, height, fps, index)
    write_preview(blobs[0], ROOT / "bad_apple.bin", width, height)

    raw_mb = packed_bytes * index / (1024 * 1024)
    out_mb = total_out / (1024 * 1024)
    ratio = (100.0 * total_out / (packed_bytes * index)) if index else 0
    print(
        f"Done: {index} frames @ {fps} FPS | "
        f"skip={n_skip} delta={n_delta} full={n_full} | "
        f"output {out_mb:.1f} MB (raw {raw_mb:.1f} MB, {ratio:.1f}%)"
    )
    return index


def main() -> None:
    parser = argparse.ArgumentParser(description="MP4 -> 1-bit 差分フレーム列（frames.pack）")
    parser.add_argument("mp4", nargs="?", type=Path, help="入力 MP4")
    parser.add_argument("-o", "--output", type=Path, default=FRAMES_PACK, help="出力 .pack ファイル")
    parser.add_argument("--split-frames", action="store_true", help="frames/ に個別 .bin も出力")
    parser.add_argument(
        "--clean-split",
        action="store_true",
        help="変換後に frames/ の古い個別 .bin を削除",
    )
    parser.add_argument("--size", type=parse_size, default=(320, 240))
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--threshold", type=int, default=128)
    parser.add_argument("--max-frames", type=int, help="変換する最大フレーム数（テスト用）")
    args = parser.parse_args()

    mp4 = resolve_mp4(args.mp4)
    width, height = args.size
    split_dir = FRAMES_DIR if args.split_frames else None
    convert(
        mp4,
        args.output,
        split_dir,
        width,
        height,
        args.fps,
        args.max_frames,
        args.threshold,
        args.clean_split,
    )
    print("SD 配置: /games/bad_apple/ に bad_apple.lua, frames.pack, bad_apple.bin をコピー")


if __name__ == "__main__":
    main()
