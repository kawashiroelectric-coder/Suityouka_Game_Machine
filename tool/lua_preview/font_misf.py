"""MISF v1 フォント読み込みと描画（numpy 版）。

文字列ごとに「塗り分け画像」（0=そのまま / 1=前景 / 2=背景）を作ってキャッシュし、
描画は numpy の一括代入 1〜2 回で済ませる。グリフを左から順に重ねる旧実装と
同じ結果になる（隣の文字の背景が前の文字に重なる場合も同じ）。
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import TYPE_CHECKING

import numpy as np

if TYPE_CHECKING:
    from framebuffer import Framebuffer


@dataclass
class IndexEntry:
    codepoint: int
    advance: int
    flags: int
    glyph_index: int


_TEXT_CACHE_MAX = 4096


class MisfFont:
    MAGIC = b"MISF"

    def __init__(self) -> None:
        self.glyph_w = 8
        self.glyph_h = 8
        self.default_advance = 8
        self.glyph_count = 0
        self.bytes_per_glyph = 8
        self.index: list[IndexEntry] = []
        self._by_cp: dict[int, IndexEntry] = {}
        self.glyph_data: bytes = b""
        self.scale_num = 1
        self.scale_den = 1
        self._glyph_masks: dict[tuple[int, int, int], np.ndarray] = {}
        self._text_cache: dict[tuple, tuple[np.ndarray, int, int]] = {}

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
            entry = IndexEntry(cp, adv, flags, gidx)
            self.index.append(entry)
            # 二分探索と同じく、同じ codepoint が複数あっても 1 つ目が見つかるとは限らないが
            # 実データは重複しない
            self._by_cp.setdefault(cp, entry)
            off += 8
        self.glyph_data = data[off : off + glyph_bytes]
        return True

    def _find_glyph(self, codepoint: int) -> IndexEntry | None:
        return self._by_cp.get(codepoint)

    def _bytes_per_row(self) -> int:
        return self.bytes_per_glyph // self.glyph_h if self.glyph_h else 0

    def _glyph_mask(self, gidx: int) -> np.ndarray | None:
        """拡大縮小済みグリフの bool マスク（旧 _draw_glyph と同じ最近傍サンプリング）"""
        key = (gidx, self.scale_num, self.scale_den)
        m = self._glyph_masks.get(key)
        if m is not None:
            return m
        out_w = self._scale(self.glyph_w)
        out_h = self._scale(self.glyph_h)
        if out_w <= 0 or out_h <= 0:
            return None
        bpr = self._bytes_per_row()
        glyph = self.glyph_data[gidx * self.bytes_per_glyph : (gidx + 1) * self.bytes_per_glyph]
        if bpr == 0:
            base = np.zeros((self.glyph_h, self.glyph_w), dtype=bool)
        else:
            raw = np.frombuffer(glyph, dtype=np.uint8)
            need = self.glyph_h * bpr
            if raw.size < need:
                raw = np.concatenate([raw, np.zeros(need - raw.size, dtype=np.uint8)])
            bits = np.unpackbits(raw[:need].reshape(self.glyph_h, bpr), axis=1)  # MSB 先頭
            if bits.shape[1] < self.glyph_w:
                bits = np.pad(bits, ((0, 0), (0, self.glyph_w - bits.shape[1])))
            base = bits[:, : self.glyph_w].astype(bool)
        ys = (np.arange(out_h) * self.glyph_h) // out_h
        xs = (np.arange(out_w) * self.glyph_w) // out_w
        m = base[ys][:, xs]
        self._glyph_masks[key] = m
        return m

    def _layout(self, text: str):
        """[(x, y, gidx or None)] と描画範囲を返す。x, y は文字列原点からの相対"""
        data = text.encode("utf-8") if isinstance(text, str) else bytes(text)
        items: list[tuple[int, int, int]] = []
        cx = 0
        y = 0
        i = 0
        n = len(data)
        gh = self.scaled_glyph_height()
        dadv = self.scaled_default_advance()
        while i < n:
            b = data[i]
            if b == 0x0A:  # '\n'（実機 FontRenderer と同じ改行）
                cx = 0
                y += gh
                i += 1
                continue
            if b < 0x80:
                cp = b
                i += 1
            elif (b & 0xE0) == 0xC0 and i + 1 < n:
                cp = ((b & 0x1F) << 6) | (data[i + 1] & 0x3F)
                i += 2
            elif (b & 0xF0) == 0xE0 and i + 2 < n:
                cp = ((b & 0x0F) << 12) | ((data[i + 1] & 0x3F) << 6) | (data[i + 2] & 0x3F)
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
            entry = self._by_cp.get(cp)
            if entry is None or entry.glyph_index >= self.glyph_count:
                cx += dadv
                continue
            items.append((cx, y, entry.glyph_index))
            adv = entry.advance if entry.advance else self.default_advance
            cx += self._scale(adv)
        return items

    def _paint(self, text: str, use_bg: bool):
        """(paint, ox, oy)。paint は 0/1/2 の uint8、(ox,oy) は原点からのずれ"""
        key = (text, use_bg, self.scale_num, self.scale_den)
        hit = self._text_cache.get(key)
        if hit is not None:
            return hit
        items = self._layout(text)
        out_w = self._scale(self.glyph_w)
        out_h = self._scale(self.glyph_h)
        if not items or out_w <= 0 or out_h <= 0:
            res = (None, 0, 0)
        else:
            min_x = min(it[0] for it in items)
            min_y = min(it[1] for it in items)
            max_x = max(it[0] for it in items) + out_w
            max_y = max(it[1] for it in items) + out_h
            paint = np.zeros((max_y - min_y, max_x - min_x), dtype=np.uint8)
            for gx, gy, gidx in items:
                m = self._glyph_mask(gidx)
                if m is None:
                    continue
                sub = paint[gy - min_y : gy - min_y + out_h, gx - min_x : gx - min_x + out_w]
                if use_bg:
                    sub[:, :] = np.where(m, 1, 2)
                else:
                    sub[m] = 1
            res = (paint, min_x, min_y)
        if len(self._text_cache) >= _TEXT_CACHE_MAX:
            self._text_cache.clear()
        self._text_cache[key] = res
        return res

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
        paint, ox, oy = self._paint(text, use_bg)
        if paint is None:
            return
        fb.draw_paint(paint, x + ox, y + oy, int(fg) & 0xFFFF, int(bg) & 0xFFFF)
