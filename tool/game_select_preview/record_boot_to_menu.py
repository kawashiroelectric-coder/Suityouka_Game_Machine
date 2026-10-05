#!/usr/bin/env python3
"""
起動スプラッシュ → ゲーム選択メニューまでをオフスクリーン描画し MP4 に書き出す。

使い方（プロジェクトルートから）:
  python tool/game_select_preview/record_boot_to_menu.py
  python tool/game_select_preview/record_boot_to_menu.py --seed 42 --scale 3
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

PREVIEW_DIR = Path(__file__).resolve().parent
if str(PREVIEW_DIR) not in sys.path:
    sys.path.insert(0, str(PREVIEW_DIR))

from PIL import Image

from tool.game_select_preview.game_catalog import load_entries
from tool.game_select_preview.menu_render import (
    MenuState,
    load_backgrounds,
    pick_random_menu_background,
    render_menu_frame,
    reset_bg_history,
)
from tool.lua_preview.framebuffer import SCREEN_HEIGHT, SCREEN_WIDTH
from tool.rgb565_codec import rgb565_to_rgb888


def resolve_games_dir(arg: str | None) -> Path:
    if arg:
        path = Path(arg)
        if not path.is_absolute():
            path = ROOT / path
        return path
    for candidate in (ROOT / "games", ROOT / "Test_Lua"):
        if candidate.is_dir():
            return candidate
    return ROOT / "games"

BOOT_SPLASH_MIN_MS = 2200
MENU_HOLD_MS = 2500
FPS = 30

VIDEO_DIR = PREVIEW_DIR / "video"
DEFAULT_OUTPUT = VIDEO_DIR / "boot_to_game_select.mp4"
GAMELOGO_H = ROOT / "assets" / "GameLogo.h"


def load_boot_splash_image() -> Image.Image:
    if not GAMELOGO_H.is_file():
        raise FileNotFoundError(f"起動ロゴが見つかりません: {GAMELOGO_H}")
    text = GAMELOGO_H.read_text(encoding="utf-8", errors="replace")
    m_w = re.search(r"GameLogo_width\s*=\s*(\d+)", text)
    m_h = re.search(r"GameLogo_height\s*=\s*(\d+)", text)
    if not m_w or not m_h:
        raise ValueError("GameLogo.h から width/height を読み取れません")
    width, height = int(m_w.group(1)), int(m_h.group(1))
    hex_vals = re.findall(r"0x[0-9A-Fa-f]+", text)
    # 配列先頭のピクセル値のみ（width/height は十進表記のため除外済み）
    expected = width * height
    if len(hex_vals) < expected:
        raise ValueError(f"GameLogo ピクセル数不足: {len(hex_vals)} < {expected}")
    pixels = [int(v, 16) for v in hex_vals[:expected]]
    img = Image.new("RGB", (width, height))
    px = img.load()
    i = 0
    for y in range(height):
        for x in range(width):
            px[x, y] = rgb565_to_rgb888(pixels[i])
            i += 1
    return img


def scale_frame(img: Image.Image, scale: int) -> Image.Image:
    if scale <= 1:
        return img
    w, h = img.size
    return img.resize((w * scale, h * scale), Image.Resampling.NEAREST)


def encode_mp4(frames: list[Image.Image], output: Path, fps: int) -> None:
    if not frames:
        raise ValueError("フレームがありません")
    output.parent.mkdir(parents=True, exist_ok=True)
    w, h = frames[0].size
    cmd = [
        "ffmpeg",
        "-y",
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb24",
        "-s",
        f"{w}x{h}",
        "-r",
        str(fps),
        "-i",
        "pipe:0",
        "-c:v",
        "libx264",
        "-pix_fmt",
        "yuv420p",
        "-movflags",
        "+faststart",
        str(output),
    ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    assert proc.stdin is not None
    try:
        for frame in frames:
            if frame.size != (w, h):
                frame = frame.resize((w, h), Image.Resampling.NEAREST)
            proc.stdin.write(frame.tobytes())
    finally:
        proc.stdin.close()
    stderr = proc.stderr.read().decode("utf-8", errors="replace") if proc.stderr else ""
    code = proc.wait()
    if code != 0:
        raise RuntimeError(f"ffmpeg 失敗 (code={code}):\n{stderr[-2000:]}")


def build_frames(
    splash: Image.Image,
    menu_rgb: Image.Image,
    *,
    splash_ms: int,
    menu_ms: int,
    fps: int,
    scale: int,
) -> list[Image.Image]:
    splash_frames = max(1, round(splash_ms * fps / 1000))
    menu_frames = max(1, round(menu_ms * fps / 1000))
    out: list[Image.Image] = []
    splash_s = scale_frame(splash, scale)
    menu_s = scale_frame(menu_rgb, scale)
    for _ in range(splash_frames):
        out.append(splash_s.copy())
    for _ in range(menu_frames):
        out.append(menu_s.copy())
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="起動画面→ゲーム選択の録画 MP4 生成")
    parser.add_argument("--games-dir", help="ゲーム一覧ルート（preview.py と同じ既定）")
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=ROOT / "assets",
        help="BG1〜4 PNG（既定: assets）",
    )
    parser.add_argument("--scale", type=int, default=2, help="出力の拡大倍率 (1-4, 既定 2)")
    parser.add_argument("--seed", type=int, help="BG 乱数シード")
    parser.add_argument("--splash-ms", type=int, default=BOOT_SPLASH_MIN_MS, help="スプラッシュ表示時間")
    parser.add_argument("--menu-ms", type=int, default=MENU_HOLD_MS, help="メニュー表示の追加時間")
    parser.add_argument("--fps", type=int, default=FPS, help="フレームレート")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"出力 MP4（既定: {DEFAULT_OUTPUT.relative_to(ROOT)}）",
    )
    args = parser.parse_args()

    games_dir = resolve_games_dir(args.games_dir)
    assets_dir = args.assets_dir
    if not assets_dir.is_absolute():
        assets_dir = ROOT / assets_dir
    scale = max(1, min(4, args.scale))
    output = args.output
    if not output.is_absolute():
        output = ROOT / output

    if args.seed is not None:
        import random

        random.seed(args.seed)

    try:
        backgrounds = load_backgrounds(assets_dir)
        splash = load_boot_splash_image()
    except (FileNotFoundError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 1

    reset_bg_history()
    games, truncated = load_entries(games_dir)
    state = MenuState(
        games=games,
        selected=0,
        truncated=truncated,
        bg_index=pick_random_menu_background(len(backgrounds)),
    )
    menu_fb = render_menu_frame(state, backgrounds)
    menu_rgb = Image.frombytes("RGB", (SCREEN_WIDTH, SCREEN_HEIGHT), menu_fb.to_rgb888_bytes())

    frames = build_frames(
        splash,
        menu_rgb,
        splash_ms=args.splash_ms,
        menu_ms=args.menu_ms,
        fps=args.fps,
        scale=scale,
    )

    print(
        f"Recording: splash {args.splash_ms}ms + menu {args.menu_ms}ms "
        f"@ {args.fps}fps → {len(frames)} frames, scale={scale}"
    )
    print(f"games={games_dir.name} count={len(games)} BG={state.bg_index + 1}")
    encode_mp4(frames, output, args.fps)
    print(f"Wrote {output} ({output.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
