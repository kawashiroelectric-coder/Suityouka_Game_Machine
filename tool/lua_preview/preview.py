#!/usr/bin/env python3
"""
Suityouka Game Machine — 対話型 Lua プレビューエミュレータ (320x240)

使い方（プロジェクトルートから）:
  python tool/lua_preview/preview.py games/stg/stg.lua
  python tool/lua_preview/preview.py games/Shogi/Shogi.lua --scale 3
"""

from __future__ import annotations

import argparse
import faulthandler
import sys
import time
import traceback
from pathlib import Path

# ネイティブ落ちのスタックを残す（Windows の無言終了対策）
faulthandler.enable(all_threads=True)

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

from lua_compat import prepare_lua_source
from machine_api import MachineHost
from framebuffer import SCREEN_WIDTH, SCREEN_HEIGHT


# ボタン index（config.hpp / LUA_API.md と一致）
KEY_MAP = {
    pygame.K_RIGHT: 0,
    pygame.K_UP: 1,
    pygame.K_LEFT: 2,
    pygame.K_DOWN: 3,
    pygame.K_z: 4,  # OP_LEFT
    pygame.K_x: 5,  # OP_RIGHT
    pygame.K_s: 6,  # FAR
    pygame.K_a: 7,  # NEAR
}


class PreviewWatchdog:
    """重い Lua 実行中もウィンドウを生きたままにする。

    Windows は数秒イベント未処理だと「応答なし」扱いにし、プレビューごと
    落とすことがある。Lua の instruction hook から pygame をポンプする。
    """

    def __init__(self, abort_ms: int = 0) -> None:
        self.abort_ms = max(0, int(abort_ms))
        self.quit_requested = False
        self._slice_start = 0.0
        self._last_pump = 0.0
        self._warned = False
        self._suspended = False
        self.last_status = ""

    def suspend(self) -> None:
        """game_draw / フレーム変換中は SDL 再入を避けるためポンプ停止。"""
        self._suspended = True

    def resume(self) -> None:
        self._suspended = False

    def begin_slice(self) -> None:
        now = time.perf_counter()
        self._slice_start = now
        self._last_pump = now
        self._warned = False
        self.last_status = ""

    def pump(self) -> None:
        """Lua debug hook から呼ばれる。イベント処理・Esc 検出・任意の時間制限。"""
        if self._suspended:
            return
        now = time.perf_counter()
        # フック頻度が高いので、実時間で間引く
        if now - self._last_pump < 0.05:
            return
        self._last_pump = now

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                self.quit_requested = True
            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                self.quit_requested = True

        # OS への応答（Not Responding 防止）
        pygame.event.pump()

        # Esc / 閉じるはフラグのみ。例外にすると「予期しない終了」に見える。
        elapsed_ms = (now - self._slice_start) * 1000.0
        if elapsed_ms >= 1500 and not self._warned:
            self._warned = True
            self.last_status = f"重い処理中… {elapsed_ms:.0f}ms"
            print(f"[preview] {self.last_status}", file=sys.stderr)

        if self.abort_ms > 0 and elapsed_ms >= self.abort_ms:
            self.quit_requested = True
            self.last_status = f"preview watchdog: Lua が {self.abort_ms}ms を超えたため中断"
            print(f"[preview] {self.last_status}", file=sys.stderr)


def load_lua_chunk(lua: LuaRuntime, source: str, chunk_name: str) -> None:
    source = prepare_lua_source(source, lua)
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


def install_lua_watchdog(lua: LuaRuntime, watchdog: PreviewWatchdog) -> None:
    """Lua debug hook は lupa+pygame 環境でヒープ破損の原因になり得るため無効。

    イベントポンプはメインループ側の watchdog.pump() のみで行う。
    """
    _ = lua
    _ = watchdog
    return


def load_ui_font(size: int = 14) -> "pygame.font.Font":
    """日本語ヘルプ行を描けるフォントを優先して選ぶ。"""
    candidates = (
        "yu gothic ui",
        "meiryo",
        "ms gothic",
        "msgothic",
        "noto sans cjk jp",
        "consolas",
    )
    for name in candidates:
        try:
            font = pygame.font.SysFont(name, size)
            font.render("実行あ", True, (255, 255, 255))
            return font
        except Exception:
            continue
    return pygame.font.Font(None, size)


def frame_to_surface(rgb: bytes, size: tuple[int, int]) -> "pygame.Surface":
    """RGB888 バイト列から Surface を作る。

    frombuffer はバッファ寿命に依存して Windows で無言クラッシュすることがあるため、
    コピーする frombytes を優先する。
    """
    w, h = size
    if hasattr(pygame.image, "frombytes"):
        return pygame.image.frombytes(rgb, (w, h), "RGB")
    # 古い pygame 向けフォールバック（参照を呼び出し側で維持すること）
    return pygame.image.frombuffer(rgb, (w, h), "RGB")


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
    parser.add_argument(
        "--watchdog-ms",
        type=int,
        default=0,
        help="Lua 1 フレームの上限 ms（0=中断しない。Esc/閉じるは常に可）",
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
    try:
        screen = pygame.display.set_mode((window_w, window_h + 48))
    except pygame.error as exc:
        print(f"ウィンドウ作成に失敗: {exc}", file=sys.stderr)
        pygame.quit()
        return 1
    clock = pygame.time.Clock()
    font_ui = load_ui_font(14)

    lua = LuaRuntime(unpack_returned_tuples=True)
    host = MachineHost(game_dir, lua)
    watchdog = PreviewWatchdog(abort_ms=args.watchdog_ms)

    def lua_print(*values: object) -> None:
        print("\t".join(str(v) for v in values), flush=True)

    def lua_sleep_ms(ms: int) -> None:
        if ms > 0:
            # sleep 中も落ちないよう細かく分割してポンプ
            remain = min(int(ms), 500)
            end = time.perf_counter() + remain / 1000.0
            while time.perf_counter() < end:
                watchdog.pump()
                time.sleep(0.01)

    lua.globals().print = lua_print
    lua.globals().sleep_ms = lua_sleep_ms
    lua.globals().machine = host.build_machine_table()

    try:
        source = script_path.read_text(encoding="utf-8")
        load_lua_chunk(lua, source, f"@{script_path.name}")
    except Exception as exc:
        print(f"ゲーム読み込みエラー: {exc}", file=sys.stderr, flush=True)
        traceback.print_exc()
        pygame.quit()
        return 1

    g = lua.globals()
    for name in ("game_init", "game_update", "game_draw"):
        fn = g[name]
        if fn is None or not callable(fn):
            print(f"必須関数 {name}() がありません", file=sys.stderr, flush=True)
            pygame.quit()
            return 1

    try:
        install_lua_watchdog(lua, watchdog)
    except Exception as exc:
        print(f"[preview] watchdog 設定に失敗（継続）: {exc}", file=sys.stderr, flush=True)

    try:
        watchdog.begin_slice()
        g.game_init()
    except Exception as exc:
        print(f"game_init エラー: {exc}", file=sys.stderr, flush=True)
        traceback.print_exc()
        pygame.quit()
        return 1

    running = True
    last_ms = host.time_ms()
    status = "実行中"
    last_rgb: bytes | None = None
    consecutive_errors = 0

    help_lines = [
        "Arrows: move  Z: OP_LEFT  X: OP_RIGHT  A: NEAR  S: FAR",
        "Esc: quit (works during heavy AI too)",
    ]

    print(f"[preview] ready: {script_path}", flush=True)

    while running:
        # 通常フレームのイベント（hook と二重になっても問題ない）
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                running = False
        if watchdog.quit_requested:
            running = False
            status = "ユーザー中断"
            break

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

        update_ok = True
        try:
            watchdog.begin_slice()
            should_exit = g.game_update(dt)
            if should_exit:
                status = "game_update が true を返した — 終了"
                running = False
            consecutive_errors = 0
            if watchdog.last_status:
                status = watchdog.last_status
            else:
                status = "実行中"
        except Exception as exc:
            update_ok = False
            consecutive_errors += 1
            status = f"game_update エラー: {exc}"
            print(status, file=sys.stderr)
            traceback.print_exc()
            if watchdog.quit_requested:
                running = False
                status = "ユーザー中断"
                break
            # 連続で壊れ続けるときだけ終了（一時的な重さ/中断は継続）
            if consecutive_errors >= 8:
                print("[preview] エラーが続くため終了します", file=sys.stderr)
                running = False
                continue

        if not running:
            break

        if update_ok:
            bands = host.fb.band_count()
            draw_failed = False
            watchdog.suspend()
            try:
                for band in range(bands):
                    host.fb.begin_band(band)
                    if host.draw_mode == "layers":
                        host.compose_layers_for_band()
                    try:
                        g.game_draw()
                    except Exception as exc:
                        status = f"game_draw エラー: {exc}"
                        print(status, file=sys.stderr, flush=True)
                        traceback.print_exc()
                        draw_failed = True
                        consecutive_errors += 1
                        break
                    host.fb.end_band()
                if not draw_failed:
                    try:
                        last_rgb = host.fb.to_rgb888_bytes()
                        consecutive_errors = 0
                    except Exception as exc:
                        status = f"フレーム変換エラー: {exc}"
                        print(status, file=sys.stderr, flush=True)
                        consecutive_errors += 1
            finally:
                watchdog.resume()
            watchdog.pump()
            if watchdog.quit_requested:
                running = False
                status = "ユーザー中断"
                break

        # 直前の成功フレームを出し続け、落ちずに復帰できるようにする
        rgb = last_rgb
        if rgb is None:
            rgb = bytes(SCREEN_WIDTH * SCREEN_HEIGHT * 3)

        try:
            img = frame_to_surface(rgb, (SCREEN_WIDTH, SCREEN_HEIGHT))
            scaled = pygame.transform.scale(img, (window_w, window_h))
            screen.fill((20, 20, 30))
            screen.blit(scaled, (0, 0))
            y = window_h + 4
            for line in help_lines + [status[:72]]:
                try:
                    surf = font_ui.render(line, True, (200, 200, 210))
                except Exception:
                    surf = font_ui.render(line.encode("ascii", "replace").decode("ascii"), True, (200, 200, 210))
                screen.blit(surf, (8, y))
                y += 14
            pygame.display.flip()
        except Exception as exc:
            print(f"[preview] 表示エラー: {exc}", file=sys.stderr, flush=True)
            traceback.print_exc()

        if args.fps_limit > 0:
            clock.tick(args.fps_limit)

    pygame.quit()
    print(status, flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except BaseException as exc:
        print(f"[preview] 予期しない終了: {exc}", file=sys.stderr, flush=True)
        traceback.print_exc()
        try:
            pygame.quit()
        except Exception:
            pass
        raise SystemExit(1)
