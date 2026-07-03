"""Suityouka Game Machine の machine.* API モック。"""

from __future__ import annotations

import json
import os
import time
from array import array
from pathlib import Path
from typing import Any

from font_misf import MisfFont
from framebuffer import Framebuffer, rgb888_to_rgb565
from tile_layers import LAYER_COUNT, TileLayerSystem
from bw_stream import BwFrameBuffer


class ImageSlot:
    __slots__ = ("pixels", "width", "height")

    def __init__(self, pixels: array, width: int, height: int) -> None:
        self.pixels = pixels
        self.width = width
        self.height = height


class MachineHost:
    """Lua から呼ばれる machine テーブルの実装。"""

    JUMP_BUTTONS = (1, 5, 0, 3, 7)

    def __init__(self, game_dir: Path, lua_runtime: Any) -> None:
        self.game_dir = game_dir.resolve()
        self.lua = lua_runtime
        self.fb = Framebuffer()
        self.font = MisfFont()
        self.tile_layers = TileLayerSystem()
        self.draw_mode = "direct"
        self.images: dict[int, ImageSlot] = {}
        self._next_image_id = 1
        self.buttons = [False] * 8
        self.boot_ms = int(time.time() * 1000)
        self._save_root = self.game_dir / "_preview_save"
        self._save_root.mkdir(parents=True, exist_ok=True)
        self._vn_layers: dict[str, dict] = {}
        self._heap_used = 64 * 1024
        self._bw_stream = BwFrameBuffer()

    # --- 基本 ---

    def width(self) -> int:
        return self.fb.width

    def height(self) -> int:
        return self.fb.height

    def time_ms(self) -> int:
        return int(time.time() * 1000) - self.boot_ms

    def rgb(self, r: int, g: int, b: int) -> int:
        return rgb888_to_rgb565(int(r), int(g), int(b))

    def band_index(self) -> int:
        return self.fb.band_index

    def band_count(self) -> int:
        return self.fb.band_count()

    def band_top(self) -> int:
        return self.fb.band_top()

    def band_bottom(self) -> int:
        return self.fb.band_bottom()

    def band_height(self) -> int:
        return self.fb.buffer_height

    def rect_in_band(self, y: int, h: int) -> bool:
        return self.fb.rect_in_band(int(y), int(h))

    def pressed(self, index: int) -> bool:
        idx = int(index)
        if 0 <= idx < len(self.buttons):
            return self.buttons[idx]
        return False

    def jump_pressed(self) -> bool:
        return any(self.buttons[i] for i in self.JUMP_BUTTONS if i < len(self.buttons))

    def heap_used(self) -> int:
        return self._heap_used

    def heap_available(self) -> int:
        return 512 * 1024 - self._heap_used

    def set_present_mode(self, *_args: Any) -> None:
        pass

    def present(self, *_args: Any) -> None:
        pass

    # --- 描画 ---

    def clear(self, color: int = 0) -> None:
        self.fb.clear(int(color))

    def fill_rect(self, x: int, y: int, w: int, h: int, color: int) -> None:
        self.fb.fill_rect(int(x), int(y), int(w), int(h), int(color))

    def fill_rects(self, rects) -> None:
        n = len(rects)
        for i in range(1, n + 1):
            r = rects[i]
            if r is None:
                continue
            x = int(r["x"])
            y = int(r["y"])
            w = int(r["w"])
            h = int(r["h"])
            color = int(r["color"])
            self.fb.fill_rect(x, y, w, h, color)

    def draw_line(self, x0: int, y0: int, x1: int, y1: int, color: int) -> None:
        self.fb.draw_line(int(x0), int(y0), int(x1), int(y1), int(color))

    def draw_circle(self, cx: int, cy: int, r: int, color: int) -> None:
        self.fb.draw_circle(int(cx), int(cy), int(r), int(color))

    def fill_circle(self, cx: int, cy: int, r: int, color: int) -> None:
        self.fb.fill_circle(int(cx), int(cy), int(r), int(color))

    def text(
        self,
        x: int,
        y: int,
        text: str,
        fg: int = 0xFFFF,
        bg: int | None = None,
    ) -> None:
        fg_i = int(fg)
        use_bg = bg is not None
        bg_i = int(bg) if use_bg else 0
        if self.font.loaded:
            self.font.draw_text_bg(
                self.fb, int(x), int(y), str(text), fg_i, bg_i, use_bg
            )
            return
        cx = int(x)
        for ch in str(text):
            self.fb.draw_char_8x8(cx, int(y), ch, fg_i, bg_i, use_bg)
            cx += 8

    def _lua_text(self, x, y, text, *extra) -> None:
        """Lua からの可変引数: (x,y,str[,fg[,bg]]). bg 省略時は透明。"""
        fg = int(extra[0]) if len(extra) >= 1 else 0xFFFF
        bg = int(extra[1]) if len(extra) >= 2 else None
        self.text(int(x), int(y), str(text), fg, bg)

    # --- 画像 ---

    def _resolve(self, path: str) -> Path:
        p = Path(path)
        if p.is_absolute():
            return p
        return (self.game_dir / p).resolve()

    def script_dir(self) -> str:
        return str(self.game_dir).replace("\\", "/")

    def resolve_path(self, path: str) -> str:
        return str(self._resolve(path)).replace("\\", "/")

    def file_exists(self, path: str) -> bool:
        return self._resolve(path).is_file()

    def load_image(self, path: str, w: int, h: int) -> tuple[int | None, str | None]:
        full = self._resolve(path)
        try:
            pixels = Framebuffer.load_bin_pixels(str(full), int(w), int(h))
        except (OSError, ValueError) as exc:
            return None, str(exc)
        slot_id = self._next_image_id
        self._next_image_id += 1
        self.images[slot_id] = ImageSlot(pixels, int(w), int(h))
        self._heap_used += len(pixels) * 2
        return slot_id, None

    def _get_slot(self, image_id: int) -> ImageSlot | None:
        return self.images.get(int(image_id))

    def draw_image(self, image_id: int, dx: int, dy: int, *args: int) -> None:
        slot = self._get_slot(image_id)
        if not slot:
            return
        if len(args) == 4:
            sx, sy, sw, sh = map(int, args)
            self.fb.blit_rgb565(slot.pixels, slot.width, slot.height, int(dx), int(dy), sx, sy, sw, sh)
        else:
            self.fb.blit_rgb565(slot.pixels, slot.width, slot.height, int(dx), int(dy))

    def draw_image_keyed(self, image_id: int, dx: int, dy: int, key: int, *args: int) -> None:
        slot = self._get_slot(image_id)
        if not slot:
            return
        key_i = int(key)
        if len(args) == 4:
            sx, sy, sw, sh = map(int, args)
            self.fb.blit_rgb565(
                slot.pixels,
                slot.width,
                slot.height,
                int(dx),
                int(dy),
                sx,
                sy,
                sw,
                sh,
                key_i,
            )
        else:
            self.fb.blit_rgb565(
                slot.pixels, slot.width, slot.height, int(dx), int(dy), key_color=key_i
            )

    def free_image(self, image_id: int) -> None:
        slot = self.images.pop(int(image_id), None)
        if slot:
            self._heap_used -= len(slot.pixels) * 2

    def image_size(self, image_id: int) -> tuple[int, int]:
        slot = self._get_slot(image_id)
        if not slot:
            raise ValueError(f"invalid image id {image_id}")
        return slot.width, slot.height

    def load_sprite(self, path: str, w: int, h: int) -> tuple[int | None, str | None]:
        return self.load_image(path, w, h)

    def draw_sprite(self, image_id: int, dx: int, dy: int, *args: int) -> None:
        self.draw_image(image_id, dx, dy, *args)

    def draw_sprite_keyed(self, image_id: int, dx: int, dy: int, key: int, *args: int) -> None:
        self.draw_image_keyed(image_id, dx, dy, key, *args)

    def free_sprite(self, image_id: int) -> None:
        self.free_image(image_id)

    def draw_tilemap(
        self,
        image_id: int,
        map_x: int,
        map_y: int,
        tile_w: int,
        tile_h: int,
        sheet_cols: int,
        cols: int,
        rows: int,
        data,
    ) -> None:
        slot = self._get_slot(image_id)
        if not slot:
            return
        tw = int(tile_w)
        th = int(tile_h)
        for row in range(int(rows)):
            ty = int(map_y) + row * th
            if not self.fb.rect_in_band(ty, th):
                continue
            for col in range(int(cols)):
                idx = row * int(cols) + col + 1
                tile = data[idx]
                if tile is None or int(tile) < 0:
                    continue
                self.fb.draw_tile(
                    int(map_x) + col * tw,
                    ty,
                    tw,
                    th,
                    int(sheet_cols),
                    slot.pixels,
                    slot.width,
                    slot.height,
                    int(tile),
                )

    # --- ストリーム描画 ---

    def _draw_stream_layer(
        self,
        path: str,
        dx: int,
        dy: int,
        w: int,
        h: int,
        key: int | None,
        keyed: bool,
    ) -> bool:
        full = self._resolve(path)
        if not full.is_file():
            return False
        try:
            pixels = Framebuffer.load_bin_pixels(str(full), w, h)
        except (OSError, ValueError):
            return False
        key_color = key if keyed else None
        self.fb.blit_rgb565(pixels, w, h, dx, dy, key_color=key_color)
        return True

    def draw_bg_stream(self, path: str, dx: int, dy: int, w: int, h: int) -> bool:
        return self._draw_stream_layer(path, int(dx), int(dy), int(w), int(h), None, False)

    def draw_vn_stream(self, compose) -> bool:
        ok = False
        bg = compose["bg"] if compose["bg"] is not None else None
        if bg is not None:
            key = int(bg["key"]) if bg["key"] is not None else 0xF81F
            keyed = bool(bg["keyed"]) if bg["keyed"] is not None else True
            if self._draw_stream_layer(
                str(bg["path"]),
                int(bg["x"] or 0),
                int(bg["y"] or 0),
                int(bg["w"]),
                int(bg["h"]),
                key,
                keyed,
            ):
                ok = True
        chars = compose["chars"]
        if chars is not None:
            n = len(chars)
            for i in range(1, n + 1):
                ch = chars[i]
                if ch is None:
                    continue
                key = int(ch["key"]) if ch["key"] is not None else 0xF81F
                keyed = bool(ch["keyed"]) if ch["keyed"] is not None else True
                if self._draw_stream_layer(
                    str(ch["path"]),
                    int(ch["x"] or 0),
                    int(ch["y"] or 0),
                    int(ch["w"]),
                    int(ch["h"]),
                    key,
                    keyed,
                ):
                    ok = True
        return ok

    def draw_bw_stream(
        self,
        path: str,
        dx: int,
        dy: int,
        w: int,
        h: int,
        fg: int,
        bg: int,
    ) -> bool:
        width = int(w)
        height = int(h)
        if width <= 0 or height <= 0:
            return False

        fg_color = int(fg) & 0xFFFF
        bg_color = int(bg) & 0xFFFF
        full = self._resolve(path)
        norm = str(full)

        band_top = self.fb.band_top()
        band_bottom = self.fb.band_bottom()
        img_bottom = int(dy) + height
        top = int(dy) if int(dy) > band_top else band_top
        bottom = img_bottom if img_bottom < band_bottom else band_bottom
        if top >= bottom:
            return True

        bw = self._bw_stream
        frame_changed = (
            bw.path != norm
            or bw.width != width
            or bw.height != height
            or bw.fg != fg_color
            or bw.bg != bg_color
        )
        if frame_changed:
            if not bw.load_from_file(full, width, height):
                return False
            bw.path = norm
            bw.pack_path = None
            bw.pack_frame = 0
            bw.fg = fg_color
            bw.bg = bg_color

        return bw.blit_band(self.fb, int(dx), int(dy), width, height, fg_color, bg_color)

    def draw_bw_pack(
        self,
        path: str,
        frame_index: int,
        dx: int,
        dy: int,
        w: int,
        h: int,
        fg: int,
        bg: int,
    ) -> bool:
        width = int(w)
        height = int(h)
        frame_no = int(frame_index)
        if width <= 0 or height <= 0 or frame_no <= 0:
            return False

        fg_color = int(fg) & 0xFFFF
        bg_color = int(bg) & 0xFFFF
        full = self._resolve(path)
        norm = str(full)

        band_top = self.fb.band_top()
        band_bottom = self.fb.band_bottom()
        img_bottom = int(dy) + height
        top = int(dy) if int(dy) > band_top else band_top
        bottom = img_bottom if img_bottom < band_bottom else band_bottom
        if top >= bottom:
            return True

        bw = self._bw_stream
        frame_changed = (
            bw.pack_path != norm
            or bw.pack_frame != frame_no
            or bw.width != width
            or bw.height != height
            or bw.fg != fg_color
            or bw.bg != bg_color
        )
        if frame_changed:
            if bw.pack_path != norm or bw.width != width or bw.height != height:
                bw.buffer_frame = 0
            if not bw.sync_pack_frame(full, frame_no, width, height):
                return False
            bw.pack_path = norm
            bw.pack_frame = frame_no
            bw.path = None
            bw.fg = fg_color
            bw.bg = bg_color

        return bw.blit_band(self.fb, int(dx), int(dy), width, height, fg_color, bg_color)

    # --- レイヤー ---

    def set_draw_mode(self, mode: str) -> None:
        if mode not in ("direct", "layers"):
            raise ValueError('set_draw_mode: use "direct" or "layers"')
        self.draw_mode = mode

    def draw_mode_fn(self) -> str:
        return self.draw_mode

    def layer_count(self) -> int:
        return LAYER_COUNT

    def set_layer_backdrop(self, color: int) -> None:
        self.tile_layers.backdrop_color = int(color) & 0xFFFF

    def set_layer(self, layer_index: int, cfg) -> None:
        transparent = cfg["transparent"]
        key_enabled = False
        key_color = 0xF81F
        if transparent is True:
            key_enabled = True
        elif transparent is not None and transparent is not False:
            key_enabled = True
            key_color = int(transparent) & 0xFFFF
        ok = self.tile_layers.set_layer_config(
            int(layer_index),
            int(cfg["tileset"]),
            int(cfg["tile_w"]),
            int(cfg["tile_h"]),
            int(cfg["sheet_cols"]),
            int(cfg["map_cols"]),
            int(cfg["map_rows"]),
            int(cfg["map_x"] or 0),
            int(cfg["map_y"] or 0),
            int(cfg["scroll_x"] or 0),
            int(cfg["scroll_y"] or 0),
            bool(cfg["enabled"]) if cfg["enabled"] is not None else True,
            key_enabled,
            key_color,
        )
        if not ok:
            raise ValueError("set_layer: invalid config")

    def set_layer_scroll(self, layer_index: int, sx: int, sy: int) -> None:
        if not self.tile_layers.set_layer_scroll(int(layer_index), int(sx), int(sy)):
            raise ValueError("set_layer_scroll: invalid layer")

    def set_layer_tiles(self, layer_index: int, tiles) -> None:
        n = len(tiles)
        data = [int(tiles[i]) for i in range(1, n + 1)]
        if not self.tile_layers.set_layer_tiles(int(layer_index), data):
            raise ValueError("set_layer_tiles: size mismatch (call set_layer first)")

    def clear_layer(self, layer_index: int) -> None:
        self.tile_layers.clear_layer(int(layer_index))

    def clear_all_layers(self) -> None:
        backdrop = self.tile_layers.backdrop_color
        self.tile_layers.reset()
        self.tile_layers.backdrop_color = backdrop

    def compose_layers_for_band(self) -> None:
        def get_image(image_id: int):
            slot = self._get_slot(image_id)
            if not slot:
                return None
            return slot.pixels, slot.width, slot.height

        self.tile_layers.compose_band(self.fb, get_image)

    # --- フォント ---

    def load_font(self, path: str) -> bool:
        return self.font.load(str(self._resolve(path)))

    def font_loaded(self) -> bool:
        return self.font.loaded

    def font_height(self) -> int:
        return self.font.scaled_glyph_height() if self.font.loaded else 8

    def font_advance(self) -> int:
        return self.font.scaled_default_advance() if self.font.loaded else 8

    def set_font_scale(self, num: int, den: int = 1) -> None:
        num_i = int(num)
        den_i = int(den)
        if num_i <= 0 or den_i <= 0 or num_i > 255 or den_i > 255:
            raise ValueError("font scale must be 1..255 (num, den)")
        self.font.set_scale(num_i, den_i)

    # --- load_return / save ---

    def load_return(self, path: str):
        full = self._resolve(path)
        try:
            source = full.read_text(encoding="utf-8")
        except OSError as exc:
            return None, str(exc)
        chunk_name = f"@{path}"
        loader = self.lua.eval(
            """
            function(src, name)
                local fn, err = load(src, name, "t")
                if not fn then return nil, err end
                return fn()
            end
            """
        )
        result = loader(source, chunk_name)
        if isinstance(result, tuple) and len(result) == 2 and result[0] is None:
            return None, result[1]
        return result, None

    def _lua_value_to_lua(self, value: Any, indent: int = 0) -> str:
        pad = "  " * indent
        pad_in = "  " * (indent + 1)
        if value is None:
            return "nil"
        if isinstance(value, bool):
            return "true" if value else "false"
        if isinstance(value, (int, float)):
            if isinstance(value, float) and value.is_integer():
                return str(int(value))
            return str(value)
        if isinstance(value, str):
            return json.dumps(value, ensure_ascii=False)
        if hasattr(value, "items"):
            lines = ["{"]
            items = list(value.items())
            for i, (k, v) in enumerate(items):
                comma = "," if i + 1 < len(items) else ""
                if isinstance(k, str):
                    key_repr = f"[{json.dumps(k, ensure_ascii=False)}]"
                else:
                    key_repr = f"[{int(k)}]"
                lines.append(f"{pad_in}{key_repr} = {self._lua_value_to_lua(v, indent + 1)}{comma}")
            lines.append(f"{pad}}}")
            return "\n".join(lines)
        n = len(value)
        if n == 0:
            return "{}"
        lines = ["{"]
        for i in range(1, n + 1):
            comma = "," if i < n else ""
            lines.append(f"{pad_in}{self._lua_value_to_lua(value[i], indent + 1)}{comma}")
        lines.append(f"{pad}}}")
        return "\n".join(lines)

    def save_data(self, path: str, table) -> tuple[bool | None, str | None]:
        body = self._lua_value_to_lua(table)
        text = f"-- game_machine save v1\nreturn {body}\n"
        dest = self._save_root / Path(path).name
        try:
            dest.write_text(text, encoding="utf-8")
        except OSError as exc:
            return None, str(exc)
        return True, None

    def load_data(self, path: str):
        dest = self._save_root / Path(path).name
        if not dest.is_file():
            return None, "file not found"
        source = dest.read_text(encoding="utf-8")
        loader = self.lua.eval(
            """
            function(src, name)
                local fn, err = load(src, name, "t")
                if not fn then return nil, err end
                local ok, result = pcall(fn)
                if not ok then return nil, result end
                if type(result) ~= 'table' then return nil, 'save file must return a table' end
                return result
            end
            """
        )
        result = loader(source, str(dest))
        if isinstance(result, tuple) and len(result) == 2 and result[1] and result[0] is None:
            return None, str(result[1])
        return result, None

    # --- 音声（スタブ） ---

    def play_tone(self, *_args: Any) -> None:
        pass

    def play_wav(self, *_args: Any) -> bool:
        return False

    def play_se(self, *_args: Any) -> bool:
        return False

    def stop_sound(self) -> None:
        pass

    def set_volume(self, *_args: Any) -> None:
        pass

    def build_machine_table(self) -> Any:
        """lupa 用 machine テーブルを構築。"""
        lua = self.lua
        m = lua.table()

        def bind(name: str, fn: Any) -> None:
            m[name] = fn

        bind("width", self.width)
        bind("height", self.height)
        bind("time_ms", self.time_ms)
        bind("rgb", self.rgb)
        bind("clear", self.clear)
        bind("fill_rect", self.fill_rect)
        bind("fill_rects", self.fill_rects)
        bind("draw_line", self.draw_line)
        bind("draw_circle", self.draw_circle)
        bind("fill_circle", self.fill_circle)
        bind("text", self._lua_text)
        bind("pressed", self.pressed)
        bind("jump_pressed", self.jump_pressed)
        bind("band_index", self.band_index)
        bind("band_count", self.band_count)
        bind("band_top", self.band_top)
        bind("band_bottom", self.band_bottom)
        bind("band_height", self.band_height)
        bind("rect_in_band", self.rect_in_band)
        bind("set_present_mode", self.set_present_mode)
        bind("present", self.present)
        bind("load_image", self._lua_load_image)
        bind("draw_image", self.draw_image)
        bind("draw_image_keyed", self.draw_image_keyed)
        bind("free_image", self.free_image)
        bind("image_size", self._lua_image_size)
        bind("load_sprite", self._lua_load_image)
        bind("draw_sprite", self.draw_image)
        bind("draw_sprite_keyed", self.draw_image_keyed)
        bind("free_sprite", self.free_image)
        bind("draw_tilemap", self.draw_tilemap)
        bind("draw_bg_stream", self.draw_bg_stream)
        bind("draw_vn_stream", self.draw_vn_stream)
        bind("draw_bw_stream", self.draw_bw_stream)
        bind("draw_bw_pack", self.draw_bw_pack)
        bind("set_draw_mode", self.set_draw_mode)
        bind("draw_mode", self.draw_mode_fn)
        bind("layer_count", self.layer_count)
        bind("set_layer_backdrop", self.set_layer_backdrop)
        bind("set_layer", self.set_layer)
        bind("set_layer_scroll", self.set_layer_scroll)
        bind("set_layer_tiles", self.set_layer_tiles)
        bind("clear_layer", self.clear_layer)
        bind("clear_all_layers", self.clear_all_layers)
        bind("load_font", self.load_font)
        bind("font_loaded", self.font_loaded)
        bind("font_height", self.font_height)
        bind("font_advance", self.font_advance)
        bind("set_font_scale", self.set_font_scale)
        bind("load_return", self._lua_load_return)
        bind("script_dir", self.script_dir)
        bind("resolve_path", self.resolve_path)
        bind("file_exists", self.file_exists)
        bind("save_data", self._lua_save_data)
        bind("load_data", self._lua_load_data)
        bind("play_tone", self.play_tone)
        bind("play_wav", self.play_wav)
        bind("play_se", self.play_se)
        bind("stop_sound", self.stop_sound)
        bind("set_volume", self.set_volume)
        bind("heap_used", self.heap_used)
        bind("heap_available", self.heap_available)
        return m

    def _lua_load_image(self, path: str, w: int, h: int):
        slot_id, err = self.load_image(path, w, h)
        if slot_id is None:
            return None, err or "load_image failed"
        return slot_id

    def _lua_image_size(self, image_id: int):
        try:
            return self.image_size(image_id)
        except ValueError as exc:
            raise RuntimeError(str(exc))

    def _lua_load_return(self, path: str):
        value, err = self.load_return(path)
        if err:
            return None, err
        return value

    def _lua_save_data(self, path: str, table):
        ok, err = self.save_data(path, table)
        if ok:
            return True
        return None, err or "save failed"

    def _lua_load_data(self, path: str):
        value, err = self.load_data(path)
        if err:
            return None, err
        return value
