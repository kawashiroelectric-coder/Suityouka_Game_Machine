"""320x240 RGB565 フレームバッファとバンド描画。"""

from __future__ import annotations

import math

SCREEN_WIDTH = 320
SCREEN_HEIGHT = 240
BAND_HEIGHT = 20

# game_display.cpp と同じ 8x8 ASCII
FONT_8X8: tuple[tuple[int, ...], ...] = (
    (0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    (0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00),
    (0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    (0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00),
    (0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00),
    (0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00),
    (0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00),
    (0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00),
    (0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00),
    (0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00),
    (0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00),
    (0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00),
    (0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06),
    (0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00),
    (0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00),
    (0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00),
    (0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00),
    (0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00),
    (0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00),
    (0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00),
    (0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00),
    (0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00),
    (0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00),
    (0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00),
    (0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00),
    (0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00),
    (0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00),
    (0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06),
    (0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00),
    (0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00),
    (0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00),
    (0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00),
    (0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00),
    (0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00),
    (0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00),
    (0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00),
    (0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00),
    (0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00),
    (0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00),
    (0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00),
    (0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00),
    (0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00),
    (0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00),
    (0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00),
    (0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00),
    (0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00),
    (0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00),
    (0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00),
    (0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00),
    (0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00),
    (0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00),
    (0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00),
    (0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00),
    (0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00),
    (0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00),
    (0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00),
    (0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00),
    (0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00),
    (0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00),
    (0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00),
    (0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00),
    (0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00),
    (0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00),
    (0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF),
    (0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00),
    (0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00),
    (0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00),
    (0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00),
    (0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00),
    (0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00),
    (0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00),
    (0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F),
    (0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00),
    (0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00),
    (0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E),
    (0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00),
    (0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00),
    (0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00),
    (0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00),
    (0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00),
    (0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F),
    (0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78),
    (0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00),
    (0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00),
    (0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00),
    (0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00),
    (0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00),
    (0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00),
    (0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00),
    (0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F),
    (0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00),
    (0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00),
    (0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00),
    (0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00),
    (0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
)


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


class Framebuffer:
    """320x240 RGB565。バンド単位のクリップ描画をサポート。"""

    def __init__(self) -> None:
        self.width = SCREEN_WIDTH
        self.height = SCREEN_HEIGHT
        self.buffer_height = BAND_HEIGHT
        n = self.width * self.height
        # 互換用（プレビュー表示は _rgb888 のみ使う）
        self.pixels: list[int] = [0] * n
        self._rgb888 = bytearray(n * 3)
        self.band_index = 0
        self.band_y0 = 0
        self.band_rows = self.buffer_height
        self._band_buf: list[int] = [0] * (self.width * self.buffer_height)

    def band_count(self) -> int:
        return (self.height + self.buffer_height - 1) // self.buffer_height

    def begin_band(self, band: int) -> None:
        self.band_index = band
        self.band_y0 = band * self.buffer_height
        remaining = self.height - self.band_y0
        self.band_rows = min(self.buffer_height, max(0, remaining))

    def end_band(self) -> None:
        """バンドを表示用 RGB888 へ転送。array / C バッファは使わない。"""
        if self.band_rows <= 0:
            return
        w = self.width
        band = self._band_buf
        rgb = self._rgb888
        pix = self.pixels
        for row in range(self.band_rows):
            y = self.band_y0 + row
            band_off = row * w
            pix_off = y * w
            rgb_off = pix_off * 3
            for x in range(w):
                v = band[band_off + x] & 0xFFFF
                pix[pix_off + x] = v
                oi = rgb_off + x * 3
                rgb[oi] = ((v >> 11) & 0x1F) << 3
                rgb[oi + 1] = ((v >> 5) & 0x3F) << 2
                rgb[oi + 2] = (v & 0x1F) << 3

    def band_top(self) -> int:
        return self.band_y0

    def band_bottom(self) -> int:
        return self.band_y0 + self.band_rows

    def rect_in_band(self, y: int, h: int) -> bool:
        return (y + h) > self.band_top() and y < self.band_bottom()

    def clear(self, color: int) -> None:
        if self.band_rows <= 0:
            return
        n = self.width * self.band_rows
        c = int(color) & 0xFFFF
        # 毎回新規 array を作らず、既存バッファへ書き込む（断片化・破損リスク低減）
        buf = self._band_buf
        for i in range(n):
            buf[i] = c

    def _clip_rect(self, x: int, y: int, w: int, h: int) -> tuple[int, int, int, int] | None:
        if w <= 0 or h <= 0:
            return None
        x0 = max(0, x)
        y0 = max(self.band_top(), y)
        x1 = min(self.width, x + w)
        y1 = min(self.band_bottom(), y + h)
        if x0 >= x1 or y0 >= y1:
            return None
        return x0, y0, x1, y1

    def fill_rect(self, x: int, y: int, w: int, h: int, color: int) -> None:
        clipped = self._clip_rect(x, y, w, h)
        if not clipped:
            return
        x0, y0, x1, y1 = clipped
        color = int(color) & 0xFFFF
        for row in range(y0, y1):
            off = (row - self.band_y0) * self.width
            for x in range(x0, x1):
                self._band_buf[off + x] = color

    @staticmethod
    def _blend_rgb565(dst: int, src: int, alpha: int) -> int:
        if alpha <= 0:
            return dst & 0xFFFF
        if alpha >= 255:
            return src & 0xFFFF
        inv = 255 - alpha
        dr = (dst >> 11) & 0x1F
        dg = (dst >> 5) & 0x3F
        db = dst & 0x1F
        sr = (src >> 11) & 0x1F
        sg = (src >> 5) & 0x3F
        sb = src & 0x1F
        r = (dr * inv + sr * alpha) // 255
        g = (dg * inv + sg * alpha) // 255
        b = (db * inv + sb * alpha) // 255
        return (r << 11) | (g << 5) | b

    def fill_rect_alpha(self, x: int, y: int, w: int, h: int, color: int, alpha: int) -> None:
        """バンドバッファ上の既存色に RGB565 を alpha 合成する。alpha: 0..255。"""
        a = int(alpha)
        if a <= 0:
            return
        if a >= 255:
            self.fill_rect(x, y, w, h, color)
            return
        clipped = self._clip_rect(x, y, w, h)
        if not clipped:
            return
        x0, y0, x1, y1 = clipped
        src = int(color) & 0xFFFF
        for row in range(y0, y1):
            off = (row - self.band_y0) * self.width
            for col in range(x0, x1):
                idx = off + col
                self._band_buf[idx] = self._blend_rgb565(self._band_buf[idx], src, a)

    def draw_line(self, x0: int, y0: int, x1: int, y1: int, color: int) -> None:
        color &= 0xFFFF
        dx = abs(x1 - x0)
        dy = -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        x, y = x0, y0
        while True:
            if 0 <= x < self.width and self.band_top() <= y < self.band_bottom():
                self._band_buf[(y - self.band_y0) * self.width + x] = color
            if x == x1 and y == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x += sx
            if e2 <= dx:
                err += dx
                y += sy

    def fill_circle(self, cx: int, cy: int, r: int, color: int) -> None:
        color &= 0xFFFF
        for y in range(cy - r, cy + r + 1):
            if not self.rect_in_band(y, 1):
                continue
            dx = int(math.sqrt(max(0, r * r - (y - cy) ** 2)))
            self.fill_rect(cx - dx, y, dx * 2 + 1, 1, color)

    def draw_circle(self, cx: int, cy: int, r: int, color: int) -> None:
        color &= 0xFFFF
        x = r
        y = 0
        err = 0
        while x >= y:
            for px, py in (
                (cx + x, cy + y),
                (cx + y, cy + x),
                (cx - y, cy + x),
                (cx - x, cy + y),
                (cx - x, cy - y),
                (cx - y, cy - x),
                (cx + y, cy - x),
                (cx + x, cy - y),
            ):
                if 0 <= px < self.width and self.band_top() <= py < self.band_bottom():
                    self._band_buf[(py - self.band_y0) * self.width + px] = color
            y += 1
            err += 1 + 2 * y
            if 2 * (err - x) + 1 > 0:
                x -= 1
                err += 1 - 2 * x

    def blit_rgb565(
        self,
        src: list[int] | tuple[int, ...],
        src_w: int,
        src_h: int,
        dx: int,
        dy: int,
        sx: int = 0,
        sy: int = 0,
        sw: int | None = None,
        sh: int | None = None,
        key_color: int | None = None,
    ) -> None:
        if sw is None:
            sw = src_w - sx
        if sh is None:
            sh = src_h - sy
        src_len = len(src)
        band_limit = self.band_rows * self.width
        key = None if key_color is None else (int(key_color) & 0xFFFF)
        for row in range(sh):
            screen_y = dy + row
            if screen_y < self.band_top() or screen_y >= self.band_bottom():
                continue
            src_y = sy + row
            if src_y < 0 or src_y >= src_h:
                continue
            dst_off = (screen_y - self.band_y0) * self.width
            base = src_y * src_w
            for col in range(sw):
                screen_x = dx + col
                if screen_x < 0 or screen_x >= self.width:
                    continue
                src_x = sx + col
                if src_x < 0 or src_x >= src_w:
                    continue
                src_idx = base + src_x
                if src_idx < 0 or src_idx >= src_len:
                    continue
                try:
                    pix = int(src[src_idx]) & 0xFFFF
                except (TypeError, ValueError, IndexError):
                    continue
                if key is not None and pix == key:
                    continue
                idx = dst_off + screen_x
                if 0 <= idx < band_limit:
                    self._band_buf[idx] = pix

    def blit_rgb565_affine(
        self,
        src: list[int] | tuple[int, ...],
        src_w: int,
        src_h: int,
        a: float,
        b: float,
        c: float,
        d: float,
        e: float,
        f: float,
        sx: int = 0,
        sy: int = 0,
        sw: int | None = None,
        sh: int | None = None,
        key_color: int | None = None,
    ) -> None:
        """プレビュー用アフィン。list[int] だけ読む。

        対応:
          - 正の整数倍スケール（b=d=0, a=e>=1）… 背景 2x など
          - 180° 回転（a=e=-1, b=d=0）… 後手駒など
        それ以外は (c,f) を左上とした等倍 blit にフォールバック。
        """
        if sw is None:
            sw = src_w - sx
        if sh is None:
            sh = src_h - sy
        if sx < 0:
            sw += sx
            sx = 0
        if sy < 0:
            sh += sy
            sy = 0
        if sx + sw > src_w:
            sw = src_w - sx
        if sy + sh > src_h:
            sh = src_h - sy
        if sw <= 0 or sh <= 0:
            return

        need = src_w * src_h
        try:
            if isinstance(src, list) and len(src) >= need:
                pixels = src
            elif isinstance(src, tuple) and len(src) >= need:
                pixels = src
            else:
                # array 等は 1 回だけ list 化（以後は list 経路）
                pixels = [int(x) & 0xFFFF for x in src]
                if len(pixels) < need:
                    return
        except Exception:
            return

        key = None if key_color is None else (int(key_color) & 0xFFFF)
        band_limit = self.band_rows * self.width
        dst = self._band_buf

        def put(screen_x: int, screen_y: int, pix: int) -> None:
            if screen_x < 0 or screen_x >= self.width:
                return
            if screen_y < self.band_top() or screen_y >= self.band_bottom():
                return
            idx = (screen_y - self.band_y0) * self.width + screen_x
            if 0 <= idx < band_limit:
                dst[idx] = pix

        # --- 正の整数倍スケール ---
        if (
            abs(b) < 1e-8
            and abs(d) < 1e-8
            and abs(a - e) < 1e-8
            and abs(a - round(a)) < 1e-8
            and round(a) >= 1
        ):
            scale = int(round(a))
            ox = int(round(c))
            oy = int(round(f))
            for row in range(sh):
                src_y = sy + row
                base = src_y * src_w
                for col in range(sw):
                    pix = pixels[base + sx + col] & 0xFFFF
                    if key is not None and pix == key:
                        continue
                    bx = ox + col * scale
                    by = oy + row * scale
                    for yy in range(scale):
                        for xx in range(scale):
                            put(bx + xx, by + yy, pix)
            return

        # --- 180° 回転（後手駒）: x' = c - u, y' = f - v ---
        if (
            abs(b) < 1e-8
            and abs(d) < 1e-8
            and abs(a + 1) < 1e-8
            and abs(e + 1) < 1e-8
        ):
            ox = int(round(c))
            oy = int(round(f))
            for row in range(sh):
                src_y = sy + row
                base = src_y * src_w
                for col in range(sw):
                    pix = pixels[base + sx + col] & 0xFFFF
                    if key is not None and pix == key:
                        continue
                    put(ox - (sx + col), oy - (sy + row), pix)
            return

        # --- フォールバック: 等倍・左上 (c,f) ---
        ox = int(round(c))
        oy = int(round(f))
        for row in range(sh):
            src_y = sy + row
            base = src_y * src_w
            for col in range(sw):
                pix = pixels[base + sx + col] & 0xFFFF
                if key is not None and pix == key:
                    continue
                put(ox + col, oy + row, pix)

    def draw_tile(
        self,
        dx: int,
        dy: int,
        tile_w: int,
        tile_h: int,
        sheet_cols: int,
        sheet: list[int] | tuple[int, ...],
        sheet_w: int,
        sheet_h: int,
        tile_index: int,
        key_color: int | None = None,
    ) -> None:
        col = tile_index % sheet_cols
        row = tile_index // sheet_cols
        sx = col * tile_w
        sy = row * tile_h
        self.blit_rgb565(
            sheet, sheet_w, sheet_h, dx, dy, sx, sy, tile_w, tile_h, key_color
        )

    def draw_char_8x8(
        self, x: int, y: int, ch: str, fg: int, bg: int, use_bg: bool = True
    ) -> None:
        if len(ch) != 1:
            return
        c = ord(ch)
        if c < 32 or c > 127:
            return
        glyph = FONT_8X8[c - 32]
        fg &= 0xFFFF
        bg &= 0xFFFF
        for row in range(8):
            py = y + row
            if py < self.band_top() or py >= self.band_bottom():
                continue
            bits = glyph[row]
            dst_off = (py - self.band_y0) * self.width
            for col in range(8):
                px = x + col
                if px < 0 or px >= self.width:
                    continue
                if bits & (1 << col):
                    self._band_buf[dst_off + px] = fg
                elif use_bg:
                    self._band_buf[dst_off + px] = bg

    def to_rgb888_bytes(self) -> bytes:
        """表示用 RGB888 を返す。pixels は読まない（end_band で同期済み）。"""
        expected = self.width * self.height * 3
        rgb = self._rgb888
        if not isinstance(rgb, (bytearray, bytes)) or len(rgb) != expected:
            self._rgb888 = bytearray(expected)
            return bytes(expected)
        # bytearray のコピーを返す（呼び出し側が frombytes しても元バッファと独立）
        return bytes(rgb)

    @staticmethod
    def load_bin_pixels(path: str, width: int, height: int) -> list[int]:
        """RGB565 .bin を list[int] で返す（array の C バッファは使わない）。"""
        data = open(path, "rb").read()
        expected = width * height * 2
        if len(data) != expected:
            raise ValueError(f"{path}: size {len(data)} != {expected} ({width}x{height})")
        # ファイルはリトルエンディアン固定
        pixels: list[int] = []
        for i in range(0, expected, 2):
            pixels.append(data[i] | (data[i + 1] << 8))
        return pixels
