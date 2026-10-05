"""320x240 RGB565 フレームバッファとバンド描画（numpy 版）。

描画先は numpy の uint16 配列。旧実装（1 ピクセルずつ Python ループ）と
同じ結果になるよう、クリップ・合成式・描画順を揃えている。

描画モード:
  - 全画面（実機の「録画」パスと同じ）… begin_full() 後に game_draw を 1 回
  - バンド（実機のフォールバック / layers モード）… begin_band(i) / end_band()
"""

from __future__ import annotations

import math

import numpy as np

from font8x8 import FONT_8X8

SCREEN_WIDTH = 320
SCREEN_HEIGHT = 240
BAND_HEIGHT = 20


def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def _build_font8x8_masks() -> list[np.ndarray]:
    masks = []
    for glyph in FONT_8X8:
        m = np.zeros((8, 8), dtype=bool)
        for row in range(8):
            bits = glyph[row]
            for col in range(8):
                if bits & (1 << col):
                    m[row, col] = True
        masks.append(m)
    return masks


_FONT8_MASKS = _build_font8x8_masks()

# RGB565 → RGB888（下位ビットは 0。旧実装と同じ）
_LUT_R = (((np.arange(65536, dtype=np.uint32) >> 11) & 0x1F) << 3).astype(np.uint8)
_LUT_G = (((np.arange(65536, dtype=np.uint32) >> 5) & 0x3F) << 2).astype(np.uint8)
_LUT_B = ((np.arange(65536, dtype=np.uint32) & 0x1F) << 3).astype(np.uint8)


def as_image_array(src, src_w: int, src_h: int) -> np.ndarray:
    """list / tuple / ndarray を (h, w) uint16 配列にする（足りない分は 0）。"""
    if isinstance(src, np.ndarray) and src.ndim == 2 and src.shape == (src_h, src_w):
        return src
    flat = np.asarray(src, dtype=np.int64).ravel() & 0xFFFF
    need = src_w * src_h
    if flat.size < need:
        flat = np.concatenate([flat, np.zeros(need - flat.size, dtype=np.int64)])
    return flat[:need].astype(np.uint16).reshape(src_h, src_w)


class Framebuffer:
    """320x240 RGB565。現在の描画先（全画面 or 1 バンド）にクリップして描く。"""

    def __init__(self) -> None:
        self.width = SCREEN_WIDTH
        self.height = SCREEN_HEIGHT
        self.buffer_height = BAND_HEIGHT
        # LCD に相当する表示内容
        self.screen = np.zeros((self.height, self.width), dtype=np.uint16)
        # バンドバッファ（実機の 20 行バッファ。バンド間で使い回す）
        self._band_buf = np.zeros((self.buffer_height, self.width), dtype=np.uint16)
        self.band_index = 0
        self.band_y0 = 0
        self.band_rows = self.buffer_height
        self._target = self._band_buf   # 描画先配列
        self._full = False
        self._text8_cache: dict = {}

    # ------------------------------------------------------------------
    # 描画先の切り替え
    # ------------------------------------------------------------------
    def band_count(self) -> int:
        return (self.height + self.buffer_height - 1) // self.buffer_height

    def begin_full(self) -> None:
        """全画面に直接描く（実機の録画→再生パス相当）。"""
        self._full = True
        self.band_index = 0
        self.band_y0 = 0
        self.band_rows = self.height
        self._target = self.screen

    def end_full(self) -> None:
        self._full = False
        self._target = self._band_buf

    def begin_band(self, band: int) -> None:
        self._full = False
        self._target = self._band_buf
        self.band_index = band
        self.band_y0 = band * self.buffer_height
        remaining = self.height - self.band_y0
        self.band_rows = min(self.buffer_height, max(0, remaining))

    def end_band(self) -> None:
        if self._full or self.band_rows <= 0:
            return
        y0 = self.band_y0
        self.screen[y0 : y0 + self.band_rows] = self._band_buf[: self.band_rows]

    def band_top(self) -> int:
        return self.band_y0

    def band_bottom(self) -> int:
        return self.band_y0 + self.band_rows

    def rect_in_band(self, y: int, h: int) -> bool:
        return (y + h) > self.band_y0 and y < self.band_y0 + self.band_rows

    # 描画先のスライス（画面座標 y0..y1 → 配列の行）
    def _rows(self, y0: int, y1: int):
        return self._target[y0 - self.band_y0 : y1 - self.band_y0]

    # ------------------------------------------------------------------
    # 基本図形
    # ------------------------------------------------------------------
    def clear(self, color: int) -> None:
        if self.band_rows <= 0:
            return
        self._target[: self.band_rows] = int(color) & 0xFFFF

    def _clip_rect(self, x: int, y: int, w: int, h: int):
        if w <= 0 or h <= 0:
            return None
        bt = self.band_y0
        bb = bt + self.band_rows
        x0 = x if x > 0 else 0
        y0 = y if y > bt else bt
        x1 = x + w
        if x1 > self.width:
            x1 = self.width
        y1 = y + h
        if y1 > bb:
            y1 = bb
        if x0 >= x1 or y0 >= y1:
            return None
        return x0, y0, x1, y1

    def fill_rect(self, x: int, y: int, w: int, h: int, color: int) -> None:
        c = self._clip_rect(x, y, w, h)
        if c is None:
            return
        x0, y0, x1, y1 = c
        self._rows(y0, y1)[:, x0:x1] = int(color) & 0xFFFF

    @staticmethod
    def _blend_rgb565(dst: int, src: int, alpha: int) -> int:
        if alpha <= 0:
            return dst & 0xFFFF
        if alpha >= 255:
            return src & 0xFFFF
        inv = 255 - alpha
        r = (((dst >> 11) & 0x1F) * inv + ((src >> 11) & 0x1F) * alpha) // 255
        g = (((dst >> 5) & 0x3F) * inv + ((src >> 5) & 0x3F) * alpha) // 255
        b = ((dst & 0x1F) * inv + (src & 0x1F) * alpha) // 255
        return (r << 11) | (g << 5) | b

    def fill_rect_alpha(self, x: int, y: int, w: int, h: int, color: int, alpha: int) -> None:
        """既存色に RGB565 を alpha 合成する。alpha: 0..255。"""
        a = int(alpha)
        if a <= 0:
            return
        if a >= 255:
            self.fill_rect(x, y, w, h, color)
            return
        c = self._clip_rect(x, y, w, h)
        if c is None:
            return
        x0, y0, x1, y1 = c
        region = self._rows(y0, y1)[:, x0:x1]
        d = region.astype(np.uint32)
        src = int(color) & 0xFFFF
        inv = 255 - a
        sr = ((src >> 11) & 0x1F) * a
        sg = ((src >> 5) & 0x3F) * a
        sb = (src & 0x1F) * a
        r = (((d >> 11) & 0x1F) * inv + sr) // 255
        g = (((d >> 5) & 0x3F) * inv + sg) // 255
        b = ((d & 0x1F) * inv + sb) // 255
        region[:, :] = ((r << 11) | (g << 5) | b).astype(np.uint16)

    def _plot_points(self, xs: list[int], ys: list[int], color: int) -> None:
        if not xs:
            return
        xa = np.fromiter(xs, dtype=np.int32, count=len(xs))
        ya = np.fromiter(ys, dtype=np.int32, count=len(ys))
        bt = self.band_y0
        ok = (xa >= 0) & (xa < self.width) & (ya >= bt) & (ya < bt + self.band_rows)
        if not ok.any():
            return
        self._target[ya[ok] - bt, xa[ok]] = color

    def draw_line(self, x0: int, y0: int, x1: int, y1: int, color: int) -> None:
        color &= 0xFFFF
        # 縦線・横線は矩形で（結果は Bresenham と同じ）
        if y0 == y1:
            xa, xb = (x0, x1) if x0 <= x1 else (x1, x0)
            self.fill_rect(xa, y0, xb - xa + 1, 1, color)
            return
        if x0 == x1:
            ya, yb = (y0, y1) if y0 <= y1 else (y1, y0)
            self.fill_rect(x0, ya, 1, yb - ya + 1, color)
            return
        # バンドと交差しない線は省略
        bt = self.band_y0
        if max(y0, y1) < bt or min(y0, y1) >= bt + self.band_rows:
            return
        dx = abs(x1 - x0)
        dy = -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        x, y = x0, y0
        xs: list[int] = []
        ys: list[int] = []
        while True:
            xs.append(x)
            ys.append(y)
            if x == x1 and y == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x += sx
            if e2 <= dx:
                err += dx
                y += sy
        self._plot_points(xs, ys, color)

    def fill_circle(self, cx: int, cy: int, r: int, color: int) -> None:
        color &= 0xFFFF
        bt = self.band_y0
        bb = bt + self.band_rows
        ya = max(cy - r, bt)
        yb = min(cy + r, bb - 1)
        for y in range(ya, yb + 1):
            dx = int(math.sqrt(max(0, r * r - (y - cy) ** 2)))
            self.fill_rect(cx - dx, y, dx * 2 + 1, 1, color)

    def draw_circle(self, cx: int, cy: int, r: int, color: int) -> None:
        color &= 0xFFFF
        bt = self.band_y0
        if cy + r < bt or cy - r >= bt + self.band_rows:
            return
        x = r
        y = 0
        err = 0
        xs: list[int] = []
        ys: list[int] = []
        while x >= y:
            xs += (cx + x, cx + y, cx - y, cx - x, cx - x, cx - y, cx + y, cx + x)
            ys += (cy + y, cy + x, cy + x, cy + y, cy - y, cy - x, cy - x, cy - y)
            y += 1
            err += 1 + 2 * y
            if 2 * (err - x) + 1 > 0:
                x -= 1
                err += 1 - 2 * x
        self._plot_points(xs, ys, color)

    # ------------------------------------------------------------------
    # 画像
    # ------------------------------------------------------------------
    def _blit_array(self, region: np.ndarray, dx: int, dy: int, key: int | None) -> None:
        """region（h,w の uint16）を画面座標 (dx,dy) に置く。key 色は描かない。"""
        h, w = region.shape
        c = self._clip_rect(dx, dy, w, h)
        if c is None:
            return
        x0, y0, x1, y1 = c
        src = region[y0 - dy : y1 - dy, x0 - dx : x1 - dx]
        dst = self._rows(y0, y1)[:, x0:x1]
        if key is None:
            dst[:, :] = src
        else:
            m = src != key
            dst[m] = src[m]

    def blit_rgb565(
        self,
        src,
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
        img = as_image_array(src, src_w, src_h)
        if sw is None:
            sw = src_w - sx
        if sh is None:
            sh = src_h - sy
        if sw <= 0 or sh <= 0:
            return
        # 画像外の部分は描かない（旧実装と同じ）
        cx0 = max(sx, 0)
        cy0 = max(sy, 0)
        cx1 = min(sx + sw, src_w)
        cy1 = min(sy + sh, src_h)
        if cx0 >= cx1 or cy0 >= cy1:
            return
        key = None if key_color is None else (int(key_color) & 0xFFFF)
        self._blit_array(img[cy0:cy1, cx0:cx1], dx + (cx0 - sx), dy + (cy0 - sy), key)

    def blit_rgb565_affine(
        self,
        src,
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
        """プレビュー用アフィン（旧実装と同じ近似）。

        対応:
          - 正の整数倍スケール（b=d=0, a=e>=1）
          - 180° 回転（a=e=-1, b=d=0）
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
        try:
            img = as_image_array(src, src_w, src_h)
        except Exception:
            return
        region = img[sy : sy + sh, sx : sx + sw]
        key = None if key_color is None else (int(key_color) & 0xFFFF)

        if (
            abs(b) < 1e-8
            and abs(d) < 1e-8
            and abs(a - e) < 1e-8
            and abs(a - round(a)) < 1e-8
            and round(a) >= 1
        ):
            scale = int(round(a))
            if scale > 1:
                region = np.repeat(np.repeat(region, scale, axis=0), scale, axis=1)
            self._blit_array(region, int(round(c)), int(round(f)), key)
            return

        if abs(b) < 1e-8 and abs(d) < 1e-8 and abs(a + 1) < 1e-8 and abs(e + 1) < 1e-8:
            # x' = c - u, y' = f - v
            ox = int(round(c))
            oy = int(round(f))
            flipped = region[::-1, ::-1]
            self._blit_array(flipped, ox - (sx + sw - 1), oy - (sy + sh - 1), key)
            return

        self._blit_array(region, int(round(c)), int(round(f)), key)

    def draw_tile(
        self,
        dx: int,
        dy: int,
        tile_w: int,
        tile_h: int,
        sheet_cols: int,
        sheet,
        sheet_w: int,
        sheet_h: int,
        tile_index: int,
        key_color: int | None = None,
    ) -> None:
        col = tile_index % sheet_cols
        row = tile_index // sheet_cols
        self.blit_rgb565(
            sheet, sheet_w, sheet_h, dx, dy, col * tile_w, row * tile_h, tile_w, tile_h, key_color
        )

    # ------------------------------------------------------------------
    # 文字（フォント未読込時の 8x8）
    # ------------------------------------------------------------------
    def draw_mask(self, mask: np.ndarray, x: int, y: int, fg: int, bg: int, use_bg: bool) -> None:
        """bool マスクを置く。True=fg、False=bg（use_bg 時のみ）。"""
        h, w = mask.shape
        c = self._clip_rect(x, y, w, h)
        if c is None:
            return
        x0, y0, x1, y1 = c
        m = mask[y0 - y : y1 - y, x0 - x : x1 - x]
        dst = self._rows(y0, y1)[:, x0:x1]
        if use_bg:
            dst[:, :] = np.where(m, fg & 0xFFFF, bg & 0xFFFF)
        else:
            dst[m] = fg & 0xFFFF

    def draw_paint(self, paint: np.ndarray, x: int, y: int, fg: int, bg: int) -> None:
        """paint（0=そのまま、1=fg、2=bg）を置く。"""
        h, w = paint.shape
        c = self._clip_rect(x, y, w, h)
        if c is None:
            return
        x0, y0, x1, y1 = c
        p = paint[y0 - y : y1 - y, x0 - x : x1 - x]
        dst = self._rows(y0, y1)[:, x0:x1]
        dst[p == 1] = fg & 0xFFFF
        dst[p == 2] = bg & 0xFFFF

    def draw_char_8x8(
        self, x: int, y: int, ch: str, fg: int, bg: int, use_bg: bool = True
    ) -> None:
        if len(ch) != 1:
            return
        c = ord(ch)
        if c < 32 or c > 127:
            return
        self.draw_mask(_FONT8_MASKS[c - 32], x, y, fg, bg, use_bg)

    def draw_text_8x8(self, x: int, y: int, text: str, fg: int, bg: int, use_bg: bool) -> None:
        """8x8 フォントの文字列（1 文字 8px 送り。範囲外の文字は送りだけ）"""
        key = (text, use_bg)
        paint = self._text8_cache.get(key)
        if paint is None:
            n = len(text)
            paint = np.zeros((8, max(1, n) * 8), dtype=np.uint8)
            for i, ch in enumerate(text):
                c = ord(ch)
                if 32 <= c <= 127:
                    m = _FONT8_MASKS[c - 32]
                    paint[:, i * 8 : i * 8 + 8] = np.where(m, 1, 2 if use_bg else 0)
            if len(self._text8_cache) > 2048:
                self._text8_cache.clear()
            self._text8_cache[key] = paint
        self.draw_paint(paint, x, y, fg, bg)

    # ------------------------------------------------------------------
    # 表示
    # ------------------------------------------------------------------
    def to_rgb888_array(self) -> np.ndarray:
        """(h, w, 3) uint8"""
        s = self.screen
        out = np.empty((self.height, self.width, 3), dtype=np.uint8)
        out[:, :, 0] = _LUT_R[s]
        out[:, :, 1] = _LUT_G[s]
        out[:, :, 2] = _LUT_B[s]
        return out

    def to_rgb888_bytes(self) -> bytes:
        return self.to_rgb888_array().tobytes()

    @staticmethod
    def load_bin_pixels(path: str, width: int, height: int) -> np.ndarray:
        """RGB565 .bin（リトルエンディアン）を (height, width) の uint16 配列で返す。"""
        data = open(path, "rb").read()
        expected = width * height * 2
        if len(data) != expected:
            raise ValueError(f"{path}: size {len(data)} != {expected} ({width}x{height})")
        return np.frombuffer(data, dtype="<u2").astype(np.uint16).reshape(height, width)
