"""Headless smoke test for lua_preview (no pygame window)."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from lupa import LuaRuntime

from lua_compat import preprocess_lua54
from machine_api import MachineHost


def run_game(script: Path) -> None:
    lua = LuaRuntime(unpack_returned_tuples=True)
    host = MachineHost(script.parent, lua)
    lua.globals().print = lambda *_args: None
    lua.globals().sleep_ms = lambda _ms: None
    lua.globals().machine = host.build_machine_table()

    src = preprocess_lua54(script.read_text(encoding="utf-8"))
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
        ROOT / "Test_Lua" / "stg" / "stg.lua",
        ROOT / "Test_Lua" / "tile_test" / "tile_test.lua",
        ROOT / "Test_Lua" / "dino" / "dino.lua",
        ROOT / "Test_Lua" / "visual_novel" / "visual_novel.lua",
    ]
    for script in games:
        if not script.is_file():
            print(f"SKIP missing: {script}")
            continue
        run_game(script)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
