#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
scenario.lua ⇔ 人間向け scenario.vnscript の相互変換。

使い方:
  python scenario_convert.py to-text scenario.lua scenario.vnscript
  python scenario_convert.py to-lua  scenario.vnscript scenario.lua

.vnscript 形式:
  @chapter  … 章見出し（Lua の -- コメントに対応）
  === scene:id ===
  bg: r, g, b
  bg_image: ...
  name: ...
  character: id, pos          （pos または x=数値）
  characters: id@pos; id@x123
  decor_stars: true/false
  next: scene_id

  [line]                      … シーン既定の話者
  本文（複数行可）

  [line speaker=八雲]         … 話者上書き
  [line speaker=にとり character=hero pos=left]
  [line speaker=にとり characters=hero@left;mysterious@right]
  [line speaker=にとり bg_image=home]

  [choices]
  - ラベル -> next_id
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_LUA = SCRIPT_DIR / "scenario.lua"
DEFAULT_TEXT = SCRIPT_DIR / "scenario.vnscript"

LUA_HEADER = """\
-- ============================================================================
-- ファイル: scenario.lua
-- ビジュアルノベル「{title}」のシナリオデータ
-- {subtitle}
-- ============================================================================
--
-- 【編集のしかた】
--   人間向け: scenario.vnscript を編集 → python scenario_convert.py to-lua
--   またはこのファイルを直接編集しても可。
--   lines の各要素は 1 ブロック（話者変更・立ち絵変更・演出の「間」で区切る）。
--   同じ話者の連続文は 1 要素にまとめ、折り返し・ページ送りはエンジン任せ。
--   素材追加時は PRODUCTION_ASSETS.md を参照。
--   編集後: python generate_font.py → SD に scenario.lua と game_font.bin をコピー。
--   game_font.bin は PixelMplus12-Regular から生成（拡大なし 12x12）。

return {{
"""

TEXT_HEADER = """\
# =============================================================================
# VN Scenario（人間向け編集用）— {title}
# {subtitle}
# =============================================================================
#
# 変換:
#   python scenario_convert.py to-lua  scenario.vnscript scenario.lua
#   python scenario_convert.py to-text scenario.lua scenario.vnscript
#
# 記法:
#   @chapter 章タイトル
#   === scene:シーンID ===
#   [line] / [line speaker=名前]  … 本文ブロック（空行まで）
#   [choices] と - ラベル -> next_id
#
# =============================================================================

"""


# ---------------------------------------------------------------------------
# Lua サブセットパーサ（scenario.lua 専用）
# ---------------------------------------------------------------------------


class LuaParseError(Exception):
    pass


class LuaLexer:
    def __init__(self, text: str) -> None:
        self.text = text
        self.pos = 0

    def eof(self) -> bool:
        return self.pos >= len(self.text)

    def peek(self, n: int = 0) -> str:
        return self.text[self.pos + n] if self.pos + n < len(self.text) else ""

    def advance(self, n: int = 1) -> None:
        self.pos += n

    def skip_ws(self) -> None:
        while not self.eof():
            c = self.peek()
            if c in " \t\r\n":
                self.advance()
                continue
            if c == "-" and self.peek(1) == "-":
                while not self.eof() and self.peek() != "\n":
                    self.advance()
                continue
            break

    def consume(self, literal: str) -> None:
        self.skip_ws()
        if not self.text.startswith(literal, self.pos):
            ctx = self.text[self.pos : self.pos + 40]
            raise LuaParseError(f"expected {literal!r} at {self.pos}, got {ctx!r}")
        self.pos += len(literal)

    def parse_ident(self) -> str:
        self.skip_ws()
        start = self.pos
        if not (self.peek().isalpha() or self.peek() == "_"):
            raise LuaParseError(f"expected identifier at {self.pos}")
        while not self.eof() and (self.peek().isalnum() or self.peek() == "_"):
            self.advance()
        return self.text[start : self.pos]

    def parse_string(self) -> str:
        self.skip_ws()
        if self.peek() not in "\"'":
            raise LuaParseError(f"expected string at {self.pos}")
        quote = self.peek()
        self.advance()
        out: list[str] = []
        while not self.eof():
            c = self.peek()
            if c == quote:
                self.advance()
                return "".join(out)
            if c == "\\":
                self.advance()
                esc = self.peek()
                mapping = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", quote: quote}
                if esc in mapping:
                    out.append(mapping[esc])
                    self.advance()
                elif esc.isdigit():
                    num = esc
                    self.advance()
                    for _ in range(2):
                        if self.peek().isdigit():
                            num += self.peek()
                            self.advance()
                    out.append(chr(int(num, 10) % 256))
                else:
                    out.append(esc)
                    self.advance()
                continue
            out.append(c)
            self.advance()
        raise LuaParseError("unterminated string")

    def parse_number(self) -> int:
        self.skip_ws()
        start = self.pos
        while not self.eof() and self.peek() in "0123456789":
            self.advance()
        if start == self.pos:
            raise LuaParseError(f"expected number at {self.pos}")
        return int(self.text[start : self.pos])

    def parse_value(self) -> Any:
        self.skip_ws()
        if self.eof():
            raise LuaParseError("unexpected end of input")
        c = self.peek()
        if c in "\"'":
            return self.parse_string()
        if c.isdigit():
            return self.parse_number()
        if c == "{":
            return self.parse_table()
        ident = self.parse_ident()
        if ident == "true":
            return True
        if ident == "false":
            return False
        raise LuaParseError(f"unknown literal {ident!r} at {self.pos}")

    def parse_table(self) -> dict[str, Any] | list[Any]:
        self.consume("{")
        self.skip_ws()
        if self.peek() == "}":
            self.advance()
            return {}

        # 配列形式 { "a", "b" } か { key = val } か判定
        saved = self.pos
        try:
            first = self.parse_value()
        except LuaParseError:
            first = None
        self.skip_ws()
        if self.peek() == "=":
            # key = value 形式 → dict として再解析
            self.pos = saved
            return self._parse_dict_table()
        if self.peek() == "}":
            self.advance()
            return [first] if first is not None else []
        if self.peek() != ",":
            raise LuaParseError(f"expected , or }} at {self.pos}")
        # 配列
        self.pos = saved
        return self._parse_array_table()

    def _parse_dict_table(self) -> dict[str, Any]:
        result: dict[str, Any] = {}
        while True:
            self.skip_ws()
            if self.peek() == "}":
                self.advance()
                return result
            key = self.parse_ident()
            self.consume("=")
            result[key] = self.parse_value()
            self.skip_ws()
            if self.peek() == ",":
                self.advance()
                continue
            if self.peek() == "}":
                self.advance()
                return result
            raise LuaParseError(f"expected , or }} at {self.pos}")

    def _parse_array_table(self) -> list[Any]:
        result: list[Any] = []
        while True:
            self.skip_ws()
            if self.peek() == "}":
                self.advance()
                return result
            result.append(self.parse_value())
            self.skip_ws()
            if self.peek() == ",":
                self.advance()
                continue
            if self.peek() == "}":
                self.advance()
                return result
            raise LuaParseError(f"expected , or }} at {self.pos}")


def strip_lua_comments_preserve_strings(text: str) -> str:
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                if text[i] == "\\":
                    out.append(text[i : i + 2])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "-" and i + 1 < n and text[i + 1] == "-":
            while i < n and text[i] != "\n":
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def parse_scenario_lua(path: Path) -> tuple[dict[str, str], list[dict[str, Any]]]:
    raw = path.read_text(encoding="utf-8")
    text = strip_lua_comments_preserve_strings(raw)

    title = "星降る夜に"
    subtitle = "二次創作：河城にとり / 八雲紫（東方Project）"
    m = re.search(r"ビジュアルノベル「(.+?)」", raw)
    if m:
        title = m.group(1)
    m = re.search(r"^--\s*(二次創作.+)$", raw, re.M)
    if m:
        subtitle = m.group(1).strip()

    chapters: list[tuple[str, str]] = []
    for m in re.finditer(
        r"--\s*-+\s*\n\s*--\s*(.+?)\s*\n\s*--\s*-+",
        raw,
    ):
        chapters.append((m.group(1).strip(), m.group(0)))

    return_pos = text.find("return")
    if return_pos < 0:
        raise LuaParseError("return not found")
    brace = text.find("{", return_pos)
    if brace < 0:
        raise LuaParseError("table not found after return")

    lexer = LuaLexer(text[brace:])
    scenes_table = lexer.parse_table()
    if not isinstance(scenes_table, list):
        raise LuaParseError("scenario root must be an array of scenes")

    meta = {"title": title, "subtitle": subtitle}
    return meta, scenes_table


# ---------------------------------------------------------------------------
# Lua 生成
# ---------------------------------------------------------------------------


def lua_string(s: str) -> str:
    escaped = (
        s.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )
    return f'"{escaped}"'


def emit_character_table(char: dict[str, Any], indent: str) -> list[str]:
    lines: list[str] = []
    if "x" in char:
        lines.append(f'{indent}character = {{ id = {lua_string(char["id"])}, x = {char["x"]} }},')
    else:
        pos = char.get("pos", "center")
        lines.append(
            f'{indent}character = {{ id = {lua_string(char["id"])}, pos = {lua_string(pos)} }},'
        )
    return lines


def emit_characters_array(chars: list[dict[str, Any]], indent: str) -> list[str]:
    out = [f"{indent}characters = {{"]
    for c in chars:
        if "x" in c:
            out.append(
                f'{indent}  {{ id = {lua_string(c["id"])}, x = {c["x"]} }},'
            )
        else:
            out.append(
                f'{indent}  {{ id = {lua_string(c["id"])}, pos = {lua_string(c.get("pos", "center"))} }},'
            )
    out.append(f"{indent}}},")
    return out


def emit_line_item(item: Any, indent: str) -> list[str]:
    if isinstance(item, str):
        return [f"{indent}{lua_string(item)},"]
    if not isinstance(item, dict):
        raise ValueError(f"unsupported line item: {item!r}")
    inner = f"{indent}  "
    parts = [f"{indent}{{"]
    text = item.get("text") or item.get("line") or ""
    parts.append(f'{inner}text = {lua_string(text)},')
    if "name" in item:
        parts.append(f'{inner}name = {lua_string(item["name"])},')
    if "bg_image" in item:
        parts.append(f'{inner}bg_image = {lua_string(item["bg_image"])},')
    if "character" in item and isinstance(item["character"], dict):
        c = item["character"]
        if "x" in c:
            parts.append(
                f'{inner}character = {{ id = {lua_string(c["id"])}, x = {c["x"]} }},'
            )
        else:
            parts.append(
                f'{inner}character = {{ id = {lua_string(c["id"])}, pos = {lua_string(c.get("pos", "center"))} }},'
            )
    if "characters" in item and isinstance(item["characters"], list):
        parts.append(f"{inner}characters = {{")
        for c in item["characters"]:
            if "x" in c:
                parts.append(
                    f'{inner}  {{ id = {lua_string(c["id"])}, x = {c["x"]} }},'
                )
            else:
                parts.append(
                    f'{inner}  {{ id = {lua_string(c["id"])}, pos = {lua_string(c.get("pos", "center"))} }},'
                )
        parts.append(f"{inner}}},")
    parts.append(f"{indent}}},")
    return parts


def emit_scene_lua(scene: dict[str, Any], indent: str = "  ") -> list[str]:
    out = [f"{indent}{{"]
    inner = indent + "  "
    out.append(f'{inner}id = {lua_string(scene["id"])},')

    bg = scene.get("bg")
    if isinstance(bg, dict):
        out.append(f'{inner}bg = {{ r = {bg["r"]}, g = {bg["g"]}, b = {bg["b"]} }},')

    for key in ("bg_image", "name", "next"):
        if key in scene:
            out.append(f"{inner}{key} = {lua_string(scene[key])},")

    if scene.get("decor_stars") is False:
        out.append(f"{inner}decor_stars = false,")
    elif scene.get("decor_stars") is True:
        out.append(f"{inner}decor_stars = true,")

    if "character" in scene and isinstance(scene["character"], dict):
        out.extend(emit_character_table(scene["character"], inner))
    if "characters" in scene and isinstance(scene["characters"], list):
        out.extend(emit_characters_array(scene["characters"], inner))

    lines = scene.get("lines")
    if isinstance(lines, list) and lines:
        out.append(f"{inner}lines = {{")
        line_inner = inner + "  "
        for item in lines:
            out.extend(emit_line_item(item, line_inner))
        out.append(f"{inner}}},")

    choices = scene.get("choices")
    if isinstance(choices, list) and choices:
        out.append(f"{inner}choices = {{")
        for ch in choices:
            out.append(
                f'{inner}  {{ label = {lua_string(ch["label"])}, next = {lua_string(ch["next"])} }},'
            )
        out.append(f"{inner}}},")

    out.append(f"{indent}}},")
    return out


def scenes_to_lua(meta: dict[str, str], scenes: list[dict[str, Any]], chapters: list[str | None]) -> str:
    parts = [LUA_HEADER.format(title=meta.get("title", "星降る夜に"), subtitle=meta.get("subtitle", ""))]
    prev_chapter: str | None = None
    for i, scene in enumerate(scenes):
        chapter = chapters[i] if i < len(chapters) else None
        if chapter and chapter != prev_chapter:
            parts.append("")
            parts.append("  -- --------------------------------------------------------------------------")
            parts.append(f"  -- {chapter}")
            parts.append("  -- --------------------------------------------------------------------------")
        if chapter:
            prev_chapter = chapter
        parts.extend(emit_scene_lua(scene))
    parts.append("")
    parts.append("}")
    parts.append("")
    return "\n".join(parts)


# ---------------------------------------------------------------------------
# .vnscript 変換
# ---------------------------------------------------------------------------


def format_character_spec(char: dict[str, Any]) -> str:
    if "x" in char:
        return f'{char["id"]}, x={char["x"]}'
    return f'{char["id"]}, {char.get("pos", "center")}'


def format_characters_list(chars: list[dict[str, Any]]) -> str:
    parts = []
    for c in chars:
        if "x" in c:
            parts.append(f'{c["id"]}@x{c["x"]}')
        else:
            parts.append(f'{c["id"]}@{c.get("pos", "center")}')
    return "; ".join(parts)


def parse_character_spec(text: str) -> dict[str, Any]:
    text = text.strip()
    if "@x" in text:
        cid, xpart = text.split("@x", 1)
        return {"id": cid.strip(), "x": int(xpart.strip())}
    if "@" in text:
        cid, pos = text.split("@", 1)
        return {"id": cid.strip(), "pos": pos.strip()}
    if ", x=" in text:
        cid, xval = text.split(", x=", 1)
        return {"id": cid.strip(), "x": int(xval.strip())}
    if "," in text:
        cid, pos = text.split(",", 1)
        return {"id": cid.strip(), "pos": pos.strip()}
    return {"id": text, "pos": "center"}


def parse_characters_spec(text: str) -> list[dict[str, Any]]:
    return [parse_character_spec(part) for part in text.split(";") if part.strip()]


def parse_line_header(line: str) -> dict[str, str]:
    m = re.match(r"\[line(?:\s+(.+))?\]", line.strip(), re.I)
    if not m:
        return {}
    attrs: dict[str, str] = {}
    if m.group(1):
        for token in re.finditer(r'(\w+)=(".*?"|[^\s]+)', m.group(1)):
            val = token.group(2)
            if val.startswith('"') and val.endswith('"'):
                val = val[1:-1]
            attrs[token.group(1)] = val
    return attrs


def scene_to_text(scene: dict[str, Any]) -> list[str]:
    out: list[str] = []
    sid = scene.get("id", "")
    out.append(f"=== scene:{sid} ===")

    bg = scene.get("bg")
    if isinstance(bg, dict):
        out.append(f'bg: {bg["r"]}, {bg["g"]}, {bg["b"]}')
    for key in ("bg_image", "name", "next"):
        if key in scene:
            out.append(f"{key}: {scene[key]}")

    if scene.get("decor_stars") is False:
        out.append("decor_stars: false")
    elif scene.get("decor_stars") is True:
        out.append("decor_stars: true")

    if "character" in scene and isinstance(scene["character"], dict):
        out.append(f"character: {format_character_spec(scene['character'])}")
    if "characters" in scene and isinstance(scene["characters"], list):
        out.append(f"characters: {format_characters_list(scene['characters'])}")

    out.append("")

    lines = scene.get("lines") or []
    for item in lines:
        if isinstance(item, str):
            out.append("[line]")
        elif isinstance(item, dict):
            attrs: list[str] = []
            if item.get("name"):
                attrs.append(f'speaker={item["name"]}')
            if item.get("bg_image"):
                attrs.append(f'bg_image={item["bg_image"]}')
            if item.get("character") and isinstance(item["character"], dict):
                c = item["character"]
                attrs.append(f'character={c["id"]}')
                if "x" in c:
                    attrs.append(f'x={c["x"]}')
                elif c.get("pos"):
                    attrs.append(f'pos={c["pos"]}')
            if item.get("characters") and isinstance(item["characters"], list):
                attrs.append(f'characters={format_characters_list(item["characters"])}')
            header = "[line " + " ".join(attrs) + "]" if attrs else "[line]"
            out.append(header)
            text = item.get("text") or item.get("line") or ""
        else:
            continue
        if isinstance(item, str):
            text = item
        out.append(text)
        out.append("")

    choices = scene.get("choices")
    if isinstance(choices, list) and choices:
        out.append("[choices]")
        for ch in choices:
            out.append(f'- {ch["label"]} -> {ch["next"]}')
        out.append("")

    return out


def extract_chapters_from_lua(raw: str) -> list[str | None]:
    """各シーン直前の -- 章コメント（なければ None）。"""
    chapters: list[str | None] = []
    pattern = re.compile(
        r"(?:--\s*-+\s*\n\s*--\s*(.+?)\s*\n\s*--\s*-+\s*\n)?"
        r"\s*\{\s*\n\s*id\s*=\s*\"([^\"]+)\"",
        re.M,
    )
    for m in pattern.finditer(raw):
        ch = m.group(1).strip() if m.group(1) else None
        chapters.append(ch)
    return chapters


def lua_to_text(path: Path, out_path: Path) -> None:
    raw = path.read_text(encoding="utf-8")
    meta, scenes = parse_scenario_lua(path)
    chapters = extract_chapters_from_lua(raw)

    parts = [
        TEXT_HEADER.format(
            title=meta.get("title", "星降る夜に"),
            subtitle=meta.get("subtitle", ""),
        )
    ]
    prev_chapter: str | None = None
    for i, scene in enumerate(scenes):
        chapter = chapters[i] if i < len(chapters) else None
        if chapter and chapter != prev_chapter:
            parts.append(f"@chapter {chapter}")
            parts.append("")
        if chapter:
            prev_chapter = chapter
        parts.extend(scene_to_text(scene))

    out_path.write_text("\n".join(parts).rstrip() + "\n", encoding="utf-8")
    print(f"Wrote {out_path} ({len(scenes)} scenes)")


def parse_vnscript(path: Path) -> tuple[dict[str, str], list[dict[str, Any]], list[str | None]]:
    text = path.read_text(encoding="utf-8")
    title = "星降る夜に"
    subtitle = "二次創作：河城にとり / 八雲紫（東方Project）"
    m = re.search(r"—\s*(.+?)（", text)
    if m:
        title = m.group(1).strip()
    m = re.search(r"^#\s*(二次創作.+)$", text, re.M)
    if m:
        subtitle = m.group(1).strip()

    meta = {"title": title, "subtitle": subtitle}
    scenes: list[dict[str, Any]] = []
    chapters: list[str | None] = []
    current_chapter: str | None = None

    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if stripped.startswith("@chapter "):
            current_chapter = stripped[len("@chapter ") :].strip()
            i += 1
            continue

        if stripped.startswith("=== scene:") and stripped.endswith("==="):
            if current_chapter is not None:
                chapters.append(current_chapter)
                current_chapter = None
            else:
                chapters.append(None)

            sid = stripped[len("=== scene:") : -len(" ===")].strip()
            scene: dict[str, Any] = {"id": sid, "lines": []}
            i += 1

            while i < len(lines):
                row = lines[i].strip()
                if row.startswith("@chapter ") or row.startswith("=== scene:"):
                    break
                if row.startswith("[line"):
                    break
                if row == "[choices]":
                    break
                if not row or row.startswith("#"):
                    i += 1
                    continue

                if ":" in row:
                    key, val = row.split(":", 1)
                    key = key.strip()
                    val = val.strip()
                    if key == "bg":
                        r, g, b = [int(x.strip()) for x in val.split(",")]
                        scene["bg"] = {"r": r, "g": g, "b": b}
                    elif key == "decor_stars":
                        scene["decor_stars"] = val.lower() == "true"
                    elif key == "character":
                        scene["character"] = parse_character_spec(val)
                    elif key == "characters":
                        scene["characters"] = parse_characters_spec(val)
                    elif key in ("bg_image", "name", "next"):
                        scene[key] = val
                i += 1

            while i < len(lines):
                row = lines[i].strip()
                if row.startswith("@chapter ") or row.startswith("=== scene:"):
                    break
                if row == "[choices]":
                    i += 1
                    scene["choices"] = []
                    while i < len(lines):
                        crow = lines[i].strip()
                        if not crow or crow.startswith("@chapter") or crow.startswith("=== scene:"):
                            break
                        if crow.startswith("[line"):
                            break
                        m = re.match(r"-\s*(.+?)\s*->\s*(.+)", crow)
                        if m:
                            scene["choices"].append({"label": m.group(1).strip(), "next": m.group(2).strip()})
                        i += 1
                    continue
                if not row.startswith("[line"):
                    i += 1
                    continue

                attrs = parse_line_header(row)
                i += 1
                body_lines: list[str] = []
                while i < len(lines):
                    body = lines[i]
                    bstrip = body.strip()
                    if bstrip.startswith("[line") or bstrip.startswith("=== scene:") or bstrip.startswith("@chapter"):
                        break
                    if bstrip == "[choices]":
                        break
                    if bstrip.startswith("#") and not body_lines:
                        i += 1
                        continue
                    if bstrip == "" and body_lines:
                        i += 1
                        break
                    body_lines.append(body.rstrip("\n"))
                    i += 1

                body = "\n".join(body_lines).strip("\n")
                if not attrs:
                    scene["lines"].append(body)
                else:
                    item: dict[str, Any] = {"text": body}
                    if "speaker" in attrs:
                        item["name"] = attrs["speaker"]
                    if "bg_image" in attrs:
                        item["bg_image"] = attrs["bg_image"]
                    if "characters" in attrs:
                        item["characters"] = parse_characters_spec(attrs["characters"])
                    elif "character" in attrs:
                        c: dict[str, Any] = {"id": attrs["character"]}
                        if "x" in attrs:
                            c["x"] = int(attrs["x"])
                        elif "pos" in attrs:
                            c["pos"] = attrs["pos"]
                        item["character"] = c
                    scene["lines"].append(item)

            if not scene.get("lines"):
                scene.pop("lines", None)
            scenes.append(scene)
            continue

        i += 1

    return meta, scenes, chapters


def text_to_lua(path: Path, out_path: Path) -> None:
    meta, scenes, chapters = parse_vnscript(path)
    lua = scenes_to_lua(meta, scenes, chapters)
    out_path.write_text(lua, encoding="utf-8")
    print(f"Wrote {out_path} ({len(scenes)} scenes)")


def main() -> int:
    parser = argparse.ArgumentParser(description="scenario.lua ⇔ scenario.vnscript converter")
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_export = sub.add_parser("to-text", help="scenario.lua → .vnscript")
    p_export.add_argument("input", type=Path, nargs="?", default=DEFAULT_LUA)
    p_export.add_argument("output", type=Path, nargs="?", default=DEFAULT_TEXT)

    p_import = sub.add_parser("to-lua", help=".vnscript → scenario.lua")
    p_import.add_argument("input", type=Path, nargs="?", default=DEFAULT_TEXT)
    p_import.add_argument("output", type=Path, nargs="?", default=DEFAULT_LUA)

    args = parser.parse_args()
    try:
        if args.cmd == "to-text":
            if not args.input.exists():
                print(f"Error: not found: {args.input}", file=sys.stderr)
                return 1
            lua_to_text(args.input, args.output)
        else:
            if not args.input.exists():
                print(f"Error: not found: {args.input}", file=sys.stderr)
                return 1
            text_to_lua(args.input, args.output)
    except (LuaParseError, ValueError, KeyError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
