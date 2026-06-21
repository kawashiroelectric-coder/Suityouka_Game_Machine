"""GBA 風タイルレイヤー合成（最大 4 層）。"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable

from framebuffer import Framebuffer

LAYER_COUNT = 4
MAX_CELLS = 2048


def floor_div(a: int, b: int) -> int:
    if b == 0:
        return 0
    q = a // b
    r = a % b
    if r != 0 and ((r > 0) != (b > 0)):
        q -= 1
    return q


@dataclass
class Layer:
    configured: bool = False
    enabled: bool = True
    tileset_id: int = -1
    tile_w: int = 0
    tile_h: int = 0
    sheet_cols: int = 0
    map_cols: int = 0
    map_rows: int = 0
    map_x: int = 0
    map_y: int = 0
    scroll_x: int = 0
    scroll_y: int = 0
    key_enabled: bool = False
    key_color: int = 0xF81F
    tiles: list[int] = field(default_factory=list)


class TileLayerSystem:
    def __init__(self) -> None:
        self.backdrop_color = 0
        self.layers = [Layer() for _ in range(LAYER_COUNT)]

    def reset(self) -> None:
        self.backdrop_color = 0
        self.layers = [Layer() for _ in range(LAYER_COUNT)]

    def set_layer_config(
        self,
        layer_index: int,
        tileset_id: int,
        tile_w: int,
        tile_h: int,
        sheet_cols: int,
        map_cols: int,
        map_rows: int,
        map_x: int,
        map_y: int,
        scroll_x: int,
        scroll_y: int,
        enabled: bool,
        key_enabled: bool,
        key_color: int,
    ) -> bool:
        if layer_index < 0 or layer_index >= LAYER_COUNT:
            return False
        if tile_w <= 0 or tile_h <= 0 or sheet_cols <= 0 or map_cols <= 0 or map_rows <= 0:
            return False
        cells = map_cols * map_rows
        if cells <= 0 or cells > MAX_CELLS:
            return False
        layer = self.layers[layer_index]
        layer.configured = True
        layer.enabled = enabled
        layer.tileset_id = tileset_id
        layer.tile_w = tile_w
        layer.tile_h = tile_h
        layer.sheet_cols = sheet_cols
        layer.map_cols = map_cols
        layer.map_rows = map_rows
        layer.map_x = map_x
        layer.map_y = map_y
        layer.scroll_x = scroll_x
        layer.scroll_y = scroll_y
        layer.key_enabled = key_enabled
        layer.key_color = key_color
        if len(layer.tiles) != cells:
            layer.tiles = [0] * cells
        return True

    def set_layer_scroll(self, layer_index: int, scroll_x: int, scroll_y: int) -> bool:
        if layer_index < 0 or layer_index >= LAYER_COUNT:
            return False
        self.layers[layer_index].scroll_x = scroll_x
        self.layers[layer_index].scroll_y = scroll_y
        return True

    def set_layer_tiles(self, layer_index: int, data: list[int]) -> bool:
        if layer_index < 0 or layer_index >= LAYER_COUNT:
            return False
        layer = self.layers[layer_index]
        if not layer.configured or len(layer.tiles) == 0:
            return False
        if len(data) != len(layer.tiles):
            return False
        layer.tiles = list(data)
        return True

    def clear_layer(self, layer_index: int) -> None:
        if 0 <= layer_index < LAYER_COUNT:
            self.layers[layer_index] = Layer()

    def compose_band(
        self,
        fb: Framebuffer,
        get_image: Callable[[int], tuple | None],
    ) -> None:
        fb.clear(self.backdrop_color)
        band_top = fb.band_top()
        band_bottom = fb.band_bottom()
        screen_w = fb.width

        for layer in self.layers:
            if not layer.configured or not layer.enabled or not layer.tiles:
                continue
            if layer.tileset_id < 0:
                continue
            slot = get_image(layer.tileset_id)
            if not slot:
                continue
            sheet, sheet_w, sheet_h = slot
            tile_w = layer.tile_w
            tile_h = layer.tile_h
            start_row = floor_div(band_top - layer.map_y + layer.scroll_y, tile_h)
            end_row = floor_div(band_bottom - 1 - layer.map_y + layer.scroll_y, tile_h)
            key = layer.key_color if layer.key_enabled else None

            for tile_row in range(start_row, end_row + 1):
                ty = layer.map_y + tile_row * tile_h - layer.scroll_y
                if not fb.rect_in_band(ty, tile_h):
                    continue
                start_col = floor_div(0 - layer.map_x + layer.scroll_x, tile_w)
                end_col = floor_div(screen_w - 1 - layer.map_x + layer.scroll_x, tile_w)
                for tile_col in range(start_col, end_col + 1):
                    if (
                        tile_col < 0
                        or tile_col >= layer.map_cols
                        or tile_row < 0
                        or tile_row >= layer.map_rows
                    ):
                        continue
                    idx = tile_row * layer.map_cols + tile_col
                    raw = layer.tiles[idx]
                    if raw <= 0:
                        continue
                    tile_index = raw - 1
                    tx = layer.map_x + tile_col * tile_w - layer.scroll_x
                    fb.draw_tile(
                        tx,
                        ty,
                        tile_w,
                        tile_h,
                        layer.sheet_cols,
                        sheet,
                        sheet_w,
                        sheet_h,
                        tile_index,
                        key,
                    )
