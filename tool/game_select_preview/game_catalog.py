"""PC 上で /games 相当ディレクトリを走査し、ファームの GameCatalog と同規則で一覧化する。"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

MAX_ENTRIES = 256
AUXILIARY_LUA = frozenset(
    {
        "assets.lua",
        "scenario.lua",
        "config.lua",
        "data.lua",
        "constants.lua",
        "level_data.lua",
        "levels.lua",
    }
)
PRIORITY_LUA = ("game.lua", "main.lua", "boot.lua")
GAME_INIT_MARKERS = ("function game_init", "game_init =", "game_init=")


@dataclass
class GameEntry:
    title: str
    script_path: Path
    preview_path: Path | None
    script_size: int


def ends_with_lua(name: str) -> bool:
    return name.lower().endswith(".lua")


def to_title_from_filename(filename: str) -> str:
    stem = Path(filename).stem
    return stem.replace("_", " ")


def is_auxiliary_lua(filename: str) -> bool:
    return filename.lower() in AUXILIARY_LUA


def lua_source_declares_game_init(path: Path) -> bool:
    try:
        with path.open("rb") as f:
            data = f.read(24 * 1024)
    except OSError:
        return False
    text = data.decode("utf-8", errors="ignore")
    return any(marker in text for marker in GAME_INIT_MARKERS)


def pick_script_in_dir(game_dir: Path) -> tuple[Path, int] | None:
    for name in PRIORITY_LUA:
        candidate = game_dir / name
        if candidate.is_file():
            return candidate, candidate.stat().st_size

    dir_name = game_dir.name
    if dir_name:
        named = game_dir / f"{dir_name}.lua"
        if named.is_file():
            return named, named.stat().st_size

    candidates: list[tuple[Path, int, bool, bool]] = []
    for path in sorted(game_dir.glob("*.lua")):
        if not path.is_file():
            continue
        candidates.append(
            (
                path,
                path.stat().st_size,
                is_auxiliary_lua(path.name),
                lua_source_declares_game_init(path),
            )
        )

    for path, size, aux, has_init in candidates:
        if has_init and not aux:
            return path, size
    for path, size, aux, has_init in candidates:
        if has_init:
            return path, size
    for path, size, aux, has_init in candidates:
        if not aux:
            return path, size
    if candidates:
        path, size, _, _ = candidates[0]
        return path, size
    return None


def pick_preview_path(game_dir: Path | None, script_path: Path) -> Path | None:
    if game_dir is not None:
        for name in ("title.bin", "preview.bin"):
            candidate = game_dir / name
            if candidate.is_file():
                return candidate
    stem_bin = script_path.with_suffix(".bin")
    if stem_bin.is_file():
        return stem_bin
    return None


def is_game_catalog_candidate(games_dir: Path, entry: Path) -> bool:
    if entry.name.startswith("."):
        return False
    if entry.is_file():
        return ends_with_lua(entry.name)
    if entry.is_dir():
        return pick_script_in_dir(entry) is not None
    return False


def load_entries(games_dir: Path, max_entries: int = MAX_ENTRIES) -> tuple[list[GameEntry], bool]:
    if not games_dir.is_dir():
        return [], False

    entries: list[GameEntry] = []
    truncated = False

    for entry in sorted(games_dir.iterdir(), key=lambda p: p.name.lower()):
        if entry.name.startswith("."):
            continue

        if len(entries) >= max_entries:
            if is_game_catalog_candidate(games_dir, entry):
                truncated = True
            continue

        if entry.is_dir():
            picked = pick_script_in_dir(entry)
            if picked is None:
                continue
            script_path, script_size = picked
            preview = pick_preview_path(entry, script_path)
            entries.append(
                GameEntry(
                    title=entry.name,
                    script_path=script_path,
                    preview_path=preview,
                    script_size=script_size,
                )
            )
            continue

        if not ends_with_lua(entry.name):
            continue
        preview = pick_preview_path(games_dir, entry)
        entries.append(
            GameEntry(
                title=to_title_from_filename(entry.name),
                script_path=entry,
                preview_path=preview,
                script_size=entry.stat().st_size,
            )
        )

    return entries, truncated
