#!/usr/bin/env python3
"""
Game Select Menu PC プレビュー（BG 乱数選択 + リスト表示）。

使い方（プロジェクトルートから）:
  python tool/game_select_preview/preview.py
  python tool/game_select_preview/preview.py --games-dir Test_Lua
  python tool/game_select_preview/preview.py --games-dir games --scale 3

操作:
  ↑/↓     選択移動
  R       BG を再抽選（ゲーム復帰時の outer loop 相当）
  Esc     終了
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

PREVIEW_DIR = Path(__file__).resolve().parent
if str(PREVIEW_DIR) not in sys.path:
    sys.path.insert(0, str(PREVIEW_DIR))

try:
    import pygame
except ImportError:
    print("pygame が必要です: pip install pygame", file=sys.stderr)
    raise SystemExit(1) from None

from tool.game_select_preview.game_catalog import load_entries
from tool.game_select_preview.menu_render import (
    MenuState,
    load_backgrounds,
    pick_random_menu_background,
    render_menu_frame,
    reset_bg_history,
)
from tool.lua_preview.framebuffer import SCREEN_HEIGHT, SCREEN_WIDTH


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


def main() -> int:
    parser = argparse.ArgumentParser(description="Game Select Menu PC プレビュー")
    parser.add_argument(
        "--games-dir",
        help="ゲーム一覧のルート（既定: games があれば games、なければ Test_Lua）",
    )
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=ROOT / "assets",
        help="BG1〜4 PNG があるディレクトリ（既定: assets）",
    )
    parser.add_argument("--scale", type=int, default=2, help="画面拡大倍率 (1-4, 既定 2)")
    parser.add_argument("--seed", type=int, help="BG 乱数のシード（再現用）")
    args = parser.parse_args()

    games_dir = resolve_games_dir(args.games_dir)
    assets_dir = args.assets_dir
    if not assets_dir.is_absolute():
        assets_dir = ROOT / assets_dir

    try:
        backgrounds = load_backgrounds(assets_dir)
    except FileNotFoundError as exc:
        print(exc, file=sys.stderr)
        return 1

    if args.seed is not None:
        import random
        random.seed(args.seed)

    reset_bg_history()
    games, truncated = load_entries(games_dir)
    state = MenuState(
        games=games,
        selected=0,
        truncated=truncated,
        bg_index=pick_random_menu_background(len(backgrounds)),
    )

    scale = max(1, min(4, args.scale))
    pygame.init()
    pygame.display.set_caption("Game Select Menu Preview")
    window_w = SCREEN_WIDTH * scale
    window_h = SCREEN_HEIGHT * scale + 40
    screen = pygame.display.set_mode((window_w, window_h))
    clock = pygame.time.Clock()
    font_ui = pygame.font.SysFont("consolas", 14)

    def redraw() -> pygame.Surface:
        fb = render_menu_frame(state, backgrounds)
        rgb = fb.to_rgb888_bytes()
        surface = pygame.image.frombuffer(rgb, (SCREEN_WIDTH, SCREEN_HEIGHT), "RGB")
        return surface

    frame = redraw()
    running = True
    status = f"games={games_dir.name}  count={len(games)}  BG={state.bg_index + 1}"

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN:
                if event.key == pygame.K_ESCAPE:
                    running = False
                elif event.key == pygame.K_r and len(backgrounds) > 0:
                    state.bg_index = pick_random_menu_background(len(backgrounds))
                    frame = redraw()
                    status = f"games={games_dir.name}  count={len(games)}  BG={state.bg_index + 1}"
                elif event.key == pygame.K_UP and state.games:
                    if state.selected > 0:
                        state.selected -= 1
                        frame = redraw()
                elif event.key == pygame.K_DOWN and state.games:
                    if state.selected + 1 < len(state.games):
                        state.selected += 1
                        frame = redraw()

        screen.fill((20, 20, 30))
        scaled = pygame.transform.scale(frame, (window_w, window_h - 40))
        screen.blit(scaled, (0, 0))
        help1 = font_ui.render("↑/↓: Select   R: New BG   Esc: Quit", True, (180, 220, 180))
        help2 = font_ui.render(status, True, (160, 200, 255))
        screen.blit(help1, (8, window_h - 36))
        screen.blit(help2, (8, window_h - 18))
        pygame.display.flip()
        clock.tick(60)

    pygame.quit()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
