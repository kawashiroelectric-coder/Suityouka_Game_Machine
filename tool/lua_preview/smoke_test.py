"""Headless smoke test for lua_preview (no pygame window)."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from lupa import LuaRuntime

from lua_compat import prepare_lua_source
from machine_api import MachineHost


def run_game(script: Path) -> None:
    lua = LuaRuntime(unpack_returned_tuples=True)
    host = MachineHost(script.parent, lua)
    lua.globals().print = lambda *_args: None
    lua.globals().sleep_ms = lambda _ms: None
    lua.globals().machine = host.build_machine_table()

    src = prepare_lua_source(script.read_text(encoding="utf-8"), lua)
    loader = lua.eval(
        """
        function(src, name)
            local fn, err = load(src, name, "t")
            if not fn then error(err) end
            return fn()
        end
        """
    )
    loader(src, f"@{script.name}")

    g = lua.globals()
    g.game_init()
    g.game_update(16)
    for b in range(host.fb.band_count()):
        host.fb.begin_band(b)
        if host.draw_mode == "layers":
            host.compose_layers_for_band()
        g.game_draw()
        host.fb.end_band()
    print(f"OK: {script.name} bands={host.fb.band_count()}")


def main() -> int:
    games = [
        ROOT / "games" / "stg" / "stg.lua",
        ROOT / "games" / "stg_fast" / "stg_fast.lua",
        ROOT / "games" / "tile_test" / "tile_test.lua",
        ROOT / "games" / "Run!Yamame" / "Run!Yamame.lua",
        ROOT / "games" / "Shogi" / "Shogi.lua",
        ROOT / "games" / "visual_novel" / "visual_novel.lua",
        ROOT / "games" / "sokoban" / "sokoban.lua",
        ROOT / "games" / "save_test" / "save_test.lua",
    ]
    for script in games:
        if not script.is_file():
            print(f"SKIP missing: {script}")
            continue
        run_game(script)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
