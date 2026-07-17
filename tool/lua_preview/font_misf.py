"""MISF v1 フォント読み込みと描画。"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from framebuffer import Framebuffer


@dataclass
class IndexEntry:
    codepoint: int
    advance: int
    flags: int
    glyph_index: int


class MisfFont:
    MAGIC = b"MISF"

    def __init__(self) -> None:
        self.glyph_w = 8
        self.glyph_h = 8
        self.default_advance = 8
        self.glyph_count = 0
        self.bytes_per_glyph = 8
        self.index: list[IndexEntry] = []
        self.glyph_data: bytes = b""
        self.scale_num = 1
        self.scale_den = 1

    @property
    def loaded(self) -> bool:
        return bool(self.glyph_data)

    def unload(self) -> None:
        self.__init__()

    def set_scale(self, num: int, den: int) -> None:
        if num <= 0 or den <= 0:
            num, den = 1, 1
        self.scale_num = num
        self.scale_den = den

    def _scale(self, value: int) -> int:
        if self.scale_num == self.scale_den:
            return value
        return (value * self.scale_num) // self.scale_den

    def scaled_glyph_height(self) -> int:
        return self._scale(self.glyph_h)

    def scaled_default_advance(self) -> int:
        return self._scale(self.default_advance)

    def load(self, path: str) -> bool:
        self.unload()
        try:
            data = open(path, "rb").read()
        except OSError:
            return False
        if len(data) < 16 or data[:4] != self.MAGIC or data[4] != 1:
            return False
        self.glyph_w = data[5]
        self.glyph_h = data[6]
        self.default_advance = data[7]
        self.glyph_count = data[8] | (data[9] << 8)
        self.bytes_per_glyph = data[10] | (data[11] << 8)
        if self.glyph_w == 0 or self.glyph_h == 0 or self.bytes_per_glyph == 0:
            return False
        index_bytes = self.glyph_count * 8
        glyph_bytes = self.glyph_count * self.bytes_per_glyph
        if len(data) < 16 + index_bytes + glyph_bytes:
            return False
        off = 16
        for _ in range(self.glyph_count):
            cp, adv, flags, gidx = struct.unpack_from("<IBBH", data, off)
            self.index.append(IndexEntry(cp, adv, flags, gidx))
            off += 8
        self.glyph_data = data[off : off + glyph_bytes]
        return True

    def _find_glyph(self, codepoint: int) -> IndexEntry | None:
        lo, hi = 0, len(self.index) - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            cp = self.index[mid].codepoint
            if cp == codepoint:
                return self.index[mid]
            if cp < codepoint:
                lo = mid + 1
            else:
                hi = mid - 1
        return None

    def _bytes_per_row(self) -> int:
        return self.bytes_per_glyph // self.glyph_h if self.glyph_h else 0

    def _glyph_pixel(self, glyph: bytes, row: int, col: int) -> bool:
        bpr = self._bytes_per_row()
        if bpr == 0:
            return False
        bits = glyph[row * bpr + col // 8]
        mask = 0x80 >> (col % 8)
        return bool(bits & mask)

    def _draw_glyph(
        self,
        fb: Framebuffer,
        x: int,
        y: int,
        glyph: bytes,
        fg: int,
        bg: int,
        use_bg: bool,
    ) -> None:
        out_w = self._scale(self.glyph_w)
        out_h = self._scale(self.glyph_h)
        if out_w <= 0 or out_h <= 0:
            return
        for oy in range(out_h):
            py = y + oy
            if not fb.rect_in_band(py, 1):
                continue
            src_y = (oy * self.glyph_h) // out_h
            for ox in range(out_w):
                px = x + ox
                if px < 0 or px >= fb.width:
                    continue
                src_x = (ox * self.glyph_w) // out_w
                on = self._glyph_pixel(glyph, src_y, src_x)
                if on:
                    fb._band_buf[(py - fb.band_y0) * fb.width + px] = int(fg) & 0xFFFF
                elif use_bg:
                    fb._band_buf[(py - fb.band_y0) * fb.width + px] = int(bg) & 0xFFFF

    def draw_text_bg(
        self,
        fb: Framebuffer,
        x: int,
        y: int,
        text: str,
        fg: int,
        bg: int,
        use_bg: bool = True,
    ) -> None:
        if not self.loaded or not text:
            return
        # lupa は Lua 文字列を Python unicode として渡す。実機の C API は UTF-8
        # バイト列なので、同じデコード経路に乗せるため一旦 UTF-8 へ戻す。
        data = text.encode("utf-8") if isinstance(text, str) else bytes(text)
        cx = x
        i = 0
        n = len(data)
        while i < n:
            b = data[i]
            if b == 0x0A:  # '\n' — 実機 FontRenderer と同じ改行
                cx = x
                y += self.scaled_glyph_height()
                i += 1
                continue
            if b < 0x80:
                cp = b
                i += 1
            elif (b & 0xE0) == 0xC0 and i + 1 < n:
                cp = ((b & 0x1F) << 6) | (data[i + 1] & 0x3F)
                i += 2
            elif (b & 0xF0) == 0xE0 and i + 2 < n:
                cp = (
                    ((b & 0x0F) << 12)
                    | ((data[i + 1] & 0x3F) << 6)
                    | (data[i + 2] & 0x3F)
                )
                i += 3
            elif (b & 0xF8) == 0xF0 and i + 3 < n:
                cp = (
                    ((b & 0x07) << 18)
                    | ((data[i + 1] & 0x3F) << 12)
                    | ((data[i + 2] & 0x3F) << 6)
                    | (data[i + 3] & 0x3F)
                )
                i += 4
            else:
                i += 1
                continue

            entry = self._find_glyph(cp)
            if entry is None:
                cx += self.scaled_default_advance()
                continue
            gidx = entry.glyph_index
            if gidx >= self.glyph_count:
                cx += self.scaled_default_advance()
                continue
            glyph = self.glyph_data[
                gidx * self.bytes_per_glyph : (gidx + 1) * self.bytes_per_glyph
            ]
            self._draw_glyph(fb, cx, y, glyph, fg, bg, use_bg)
            adv = entry.advance if entry.advance else self.default_advance
            cx += self._scale(adv)
