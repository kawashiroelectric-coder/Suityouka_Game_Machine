#!/usr/bin/env python3
"""
Suityouka Game Machine — 対話型 Lua プレビューエミュレータ (320x240)

使い方（プロジェクトルートから）:
  python tool/lua_preview/preview.py Test_Lua/stg/stg.lua
  python tool/lua_preview/preview.py Test_Lua/tile_test/tile_test.lua --scale 3
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

# プロジェクトルートを import パスに追加
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
    sys.exit(1)

try:
    from lupa import LuaRuntime
except ImportError:
    print("lupa が必要です: pip install lupa", file=sys.stderr)
    sys.exit(1)

from lua_compat import preprocess_lua54
from machine_api import MachineHost
from framebuffer import SCREEN_WIDTH, SCREEN_HEIGHT


# ボタン index（config.hpp / LUA_API.md と一致）
KEY_MAP = {
    pygame.K_RIGHT: 0,
    pygame.K_UP: 1,
    pygame.K_LEFT: 2,
    pygame.K_DOWN: 3,
    pygame.K_z: 4,
    pygame.K_x: 5,
    pygame.K_c: 7,
    pygame.K_SPACE: 7,
    pygame.K_RETURN: 7,
}


def load_lua_chunk(lua: LuaRuntime, source: str, chunk_name: str) -> None:
    source = preprocess_lua54(source)
    loader = lua.eval(
        """
        function(src, name)
            local fn, err = load(src, name, "t")
            if not fn then error(err) end
            return fn()
        end
        """
    )
    loader(source, chunk_name)


def main() -> int:
    parser = argparse.ArgumentParser(description="Suityouka Game Machine Lua プレビュー")
    parser.add_argument("script", type=Path, help="ゲーム Lua スクリプト (例: Test_Lua/stg/stg.lua)")
    parser.add_argument(
        "--scale",
        type=int,
        default=2,
        help="画面拡大倍率 (既定 2 → 640x480)",
    )
    parser.add_argument(
        "--fps-limit",
        type=int,
        default=60,
        help="FPS 上限 (0 = 無制限)",
    )
    args = parser.parse_args()

    script_path = args.script
    if not script_path.is_file():
        script_path = ROOT / args.script
    if not script_path.is_file():
        print(f"スクリプトが見つかりません: {args.script}", file=sys.stderr)
        return 1

    game_dir = script_path.parent.resolve()
    scale = max(1, min(4, args.scale))

    pygame.init()
    pygame.display.set_caption(f"Lua Preview — {script_path.name}")
    window_w = SCREEN_WIDTH * scale
    window_h = SCREEN_HEIGHT * scale
    screen = pygame.display.set_mode((window_w, window_h + 32))
    clock = pygame.time.Clock()
    font_ui = pygame.font.SysFont("consolas", 14)

    lua = LuaRuntime(unpack_returned_tuples=True)
    host = MachineHost(game_dir, lua)

    def lua_print(*values: object) -> None:
        print("\t".join(str(v) for v in values))

    def lua_sleep_ms(ms: int) -> None:
        if ms > 0:
            time.sleep(min(int(ms), 100) / 1000.0)

    lua.globals().print = lua_print
    lua.globals().sleep_ms = lua_sleep_ms
    lua.globals().machine = host.build_machine_table()

    try:
        source = script_path.read_text(encoding="utf-8")
        load_lua_chunk(lua, source, f"@{script_path.name}")
    except Exception as exc:
        print(f"ゲーム読み込みエラー: {exc}", file=sys.stderr)
        return 1

    g = lua.globals()
    for name in ("game_init", "game_update", "game_draw"):
        fn = g[name]
        if fn is None or not callable(fn):
            print(f"必須関数 {name}() がありません", file=sys.stderr)
            return 1

    try:
        g.game_init()
    except Exception as exc:
        print(f"game_init エラー: {exc}", file=sys.stderr)
        return 1

    running = True
    last_ms = host.time_ms()
    status = "実行中"
    frame = pygame.Surface((SCREEN_WIDTH, SCREEN_HEIGHT))

    help_lines = [
        "←→↑↓: 移動  Z: OP_LEFT  X: OP_RIGHT  Space/C: NEAR",
        "Esc: 終了",
    ]

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                running = False

        host.buttons = [False] * 8
        keys = pygame.key.get_pressed()
        for key, idx in KEY_MAP.items():
            if keys[key]:
                host.buttons[idx] = True

        now_ms = host.time_ms()
        dt = now_ms - last_ms
        last_ms = now_ms
        if dt < 0:
            dt = 0
        if dt > 100:
            dt = 100

        try:
            should_exit = g.game_update(dt)
            if should_exit:
                status = "game_update が true を返した — 終了"
                running = False
        except Exception as exc:
            status = f"game_update エラー: {exc}"
            print(status, file=sys.stderr)
            running = False
            continue

        bands = host.fb.band_count()
        for band in range(bands):
            host.fb.begin_band(band)
            if host.draw_mode == "layers":
                host.compose_layers_for_band()
            try:
                g.game_draw()
            except Exception as exc:
                status = f"game_draw エラー: {exc}"
                print(status, file=sys.stderr)
                running = False
                break
            host.fb.end_band()

        if not running:
            break

        rgb = host.fb.to_rgb888_bytes()
        frame.lock()
        try:
            # pygame 1.x / 2.x 両対応
            if hasattr(pygame, "image"):
                img = pygame.image.frombuffer(rgb, (SCREEN_WIDTH, SCREEN_HEIGHT), "RGB")
            else:
                img = pygame.Image.frombuffer(rgb, (SCREEN_WIDTH, SCREEN_HEIGHT), "RGB")
        finally:
            frame.unlock()
        scaled = pygame.transform.scale(img, (window_w, window_h))
        screen.fill((20, 20, 30))
        screen.blit(scaled, (0, 0))
        y = window_h + 4
        for line in help_lines:
            surf = font_ui.render(line, True, (200, 200, 210))
            screen.blit(surf, (8, y))
            y += 16
        pygame.display.flip()

        if args.fps_limit > 0:
            clock.tick(args.fps_limit)

    pygame.quit()
    print(status)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
