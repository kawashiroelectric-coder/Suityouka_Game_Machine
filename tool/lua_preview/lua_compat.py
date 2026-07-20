"""Lua ソースを lupa 実行環境向けに前処理する。

lupa が Lua 5.4+（`//` あり）のときは変換不要。
LuaJIT 等の古い環境向けにだけ `//` → `math.floor` へ置換する。
"""

from __future__ import annotations

import re
from typing import Any


def runtime_supports_idiv(lua_runtime: Any) -> bool:
    """実行中の Lua が整数除算 `//` をネイティブに持てるか。"""
    try:
        fn = lua_runtime.eval(
            """
            function()
                local ok, r = pcall(function() return 7 // 2 end)
                return ok and r == 3
            end
            """
        )
        return bool(fn())
    except Exception:
        return False


# 文字列リテラル外の a // b を math.floor(a/b) に置換
# 左辺は関数呼び出し machine.time_ms() なども許可。空の () 単独は除外。
_IDIV_PATTERN = re.compile(
    r"""
    (?P<left>
        (?:[\w\.]+(?:\([^()]*\))?)+
        | \([^()]+\)
        | "[^"\\]*(?:\\.[^"\\]*)*"
        | '[^'\\]*(?:\\.[^'\\]*)*'
        | \d+
    )
    \s*//\s*
    (?P<right>
        (?:[\w\.]+(?:\([^()]*\))?)+
        | \([^()]+\)
        | "[^"\\]*(?:\\.[^"\\]*)*"
        | '[^'\\]*(?:\\.[^'\\]*)*'
        | \d+
    )
    """,
    re.VERBOSE,
)


def _strip_comments_and_strings(line: str) -> str:
    """行内コメントと文字列を空白化（// 検出の誤爆を減らす）。"""
    out: list[str] = []
    i = 0
    n = len(line)
    while i < n:
        ch = line[i]
        if ch == "-" and i + 1 < n and line[i + 1] == "-":
            out.append(" " * (n - i))
            break
        if ch in "\"'":
            quote = ch
            out.append(" ")
            i += 1
            while i < n:
                if line[i] == "\\" and i + 1 < n:
                    out.extend([" ", " "])
                    i += 2
                    continue
                if line[i] == quote:
                    out.append(" ")
                    i += 1
                    break
                out.append(" ")
                i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def preprocess_lua54(source: str) -> str:
    """floor 除算 (//) を math.floor(a/b) に変換する（LuaJIT 向け）。"""
    lines = source.splitlines(keepends=True)
    changed = False
    for _pass_no in range(64):
        any_change = False
        new_lines: list[str] = []
        for line in lines:
            masked = _strip_comments_and_strings(line)
            if "//" not in masked:
                new_lines.append(line)
                continue

            def repl(m: re.Match[str]) -> str:
                left = m.group("left")
                right = m.group("right")
                if left.startswith(('"', "'")) or right.startswith(('"', "'")):
                    return m.group(0)
                # 空の () だけを左辺にしない（誤変換防止）
                if left.strip() == "()":
                    return m.group(0)
                return f"math.floor({left}/{right})"

            new_line = _IDIV_PATTERN.sub(repl, line, count=1)
            if new_line != line:
                any_change = True
                changed = True
            new_lines.append(new_line)
        lines = new_lines
        if not any_change:
            break
    if changed and "math.floor" in "".join(lines):
        header = "local __preview_math = math\n"
        if not lines or not lines[0].startswith("local __preview_math"):
            lines.insert(0, header)
    return "".join(lines)


def prepare_lua_source(source: str, lua_runtime: Any | None = None) -> str:
    """実行環境に合わせて必要なら // を前処理する。"""
    if lua_runtime is not None and runtime_supports_idiv(lua_runtime):
        return source
    return preprocess_lua54(source)
