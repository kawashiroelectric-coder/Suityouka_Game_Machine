"""Game Select Menu の描画（ファーム game_select_menu.cpp の定数・見た目に準拠）。"""

from __future__ import annotations

import random
from array import array
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

from tool.game_select_preview.game_catalog import GameEntry
from tool.lua_preview.framebuffer import Framebuffer, SCREEN_HEIGHT, SCREEN_WIDTH
from tool.rgb565_codec import rgb888_to_rgb565, rgba_image_to_rgb565_pixels

# --- layout (game_select_menu.cpp) ---
MENU_LIST_FIRST_ROW_Y = 26
MENU_ROW_PITCH = 18
MENU_ROW_BG_H = 8
MENU_LIST_X = 10
MENU_LIST_W = 180
MENU_LIST_TITLE_CHARS = 21
MENU_BG = rgb888_to_rgb565(8, 24, 45)
MENU_CHROME_BG = rgb888_to_rgb565(37, 61, 87)
MENU_DIVIDER = rgb888_to_rgb565(0, 0, 0)
MENU_SEL_BG = rgb888_to_rgb565(20, 70, 120)
MENU_ROW_BG = rgb888_to_rgb565(8, 24, 45)
MENU_TOP_CHROME_H = 24
MENU_BOTTOM_CHROME_H = 22
MENU_DIVIDER_Y_TOP = MENU_TOP_CHROME_H
MENU_CONTENT_Y0 = MENU_DIVIDER_Y_TOP + 1
MENU_DIVIDER_Y_BOTTOM = SCREEN_HEIGHT - MENU_BOTTOM_CHROME_H - 1
MENU_CONTENT_Y1 = MENU_DIVIDER_Y_BOTTOM - 1
MAX_VISIBLE_ROWS = (MENU_CONTENT_Y1 - MENU_LIST_FIRST_ROW_Y + 1) // MENU_ROW_PITCH
MENU_BOTTOM_CHROME_Y = MENU_DIVIDER_Y_BOTTOM + 1
MENU_CONTENT_H = MENU_CONTENT_Y1 - MENU_CONTENT_Y0 + 1
MENU_LIST_DIVIDER_X = MENU_LIST_X + MENU_LIST_W + 7
RIGHT_PANEL_CONTENT_X = MENU_LIST_DIVIDER_X + 1
RIGHT_PANEL_CONTENT_W = SCREEN_WIDTH - RIGHT_PANEL_CONTENT_X
PREVIEW_W = 100
PREVIEW_H = 100
RIGHT_PANEL_FRAME_W = PREVIEW_W + 2
RIGHT_PANEL_FRAME_H = PREVIEW_H + 2
RIGHT_TITLE_CHARS = 14
RIGHT_META_BLOCK_W = RIGHT_TITLE_CHARS * 8
RIGHT_BLOCK_H = RIGHT_PANEL_FRAME_H + 6 + 16 + 8
RIGHT_BLOCK_OFFSET_Y = (MENU_CONTENT_H - RIGHT_BLOCK_H) // 2
RIGHT_PANEL_Y = MENU_CONTENT_Y0 + RIGHT_BLOCK_OFFSET_Y
RIGHT_PANEL_X = RIGHT_PANEL_CONTENT_X + (RIGHT_PANEL_CONTENT_W - RIGHT_PANEL_FRAME_W) // 2
PREVIEW_IMAGE_X = RIGHT_PANEL_X + 1
PREVIEW_IMAGE_Y = RIGHT_PANEL_Y + 1
RIGHT_META_X = RIGHT_PANEL_CONTENT_X + (RIGHT_PANEL_CONTENT_W - RIGHT_META_BLOCK_W) // 2
RIGHT_TITLE_Y = RIGHT_PANEL_Y + RIGHT_PANEL_FRAME_H + 6
RIGHT_SIZE_Y = RIGHT_TITLE_Y + 16
MENU_HEADER_TITLE_Y = 10
MENU_FOOTER_HINT_Y = 226
PREVIEW_BG = rgb888_to_rgb565(20, 20, 40)

MENU_CHROME_OVERLAY_ALPHA = 200
MENU_CONTENT_OVERLAY_ALPHA = 170
MENU_ROW_SEL_OVERLAY_ALPHA = 210

COLOR_WHITE = 0xFFFF
COLOR_GREEN = 0x07E0
COLOR_GRAY = 0x8410
COLOR_YELLOW = 0xFFE0
COLOR_SEL_FG = rgb888_to_rgb565(255, 180, 70)
COLOR_CYAN = 0x07FF

_last_bg_index = -1


def pick_random_menu_background(bg_count: int) -> int:
    """ファーム pickRandomMenuBackground と同じ衝突回避付き乱数。"""
    global _last_bg_index
    idx = random.getrandbits(32) % bg_count
    if _last_bg_index >= 0 and idx == _last_bg_index:
        idx = (_last_bg_index + 1 + (random.getrandbits(32) % (bg_count - 1))) % bg_count
    _last_bg_index = idx
    return idx


def reset_bg_history() -> None:
    global _last_bg_index
    _last_bg_index = -1


def load_backgrounds(assets_dir: Path) -> list[tuple[int, int, array]]:
    backgrounds: list[tuple[int, int, array]] = []
    for i in range(1, 5):
        png_path = assets_dir / f"BG{i}.PNG"
        if not png_path.is_file():
            png_path = assets_dir / f"BG{i}.png"
        if not png_path.is_file():
            raise FileNotFoundError(f"背景が見つかりません: {assets_dir}/BG{i}.PNG")
        image = Image.open(png_path)
        w, h, pixels = rgba_image_to_rgb565_pixels(image)
        if w != SCREEN_WIDTH or h != SCREEN_HEIGHT:
            image = image.resize((SCREEN_WIDTH, SCREEN_HEIGHT), Image.Resampling.NEAREST)
            w, h, pixels = rgba_image_to_rgb565_pixels(image)
        buf = array("H", pixels)
        backgrounds.append((w, h, buf))
    return backgrounds


def blend_rgb565(dst: int, src: int, alpha: int) -> int:
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
    return ((r << 11) | (g << 5) | b) & 0xFFFF


@dataclass
class MenuState:
    games: list[GameEntry]
    selected: int = 0
    view_offset: int = 0
    truncated: bool = False
    bg_index: int = 0


def truncated_title(title: str, max_chars: int) -> str:
    if len(title) <= max_chars:
        return title
    return title[:max_chars]


def show_truncation_row(state: MenuState) -> bool:
    if not state.truncated or not state.games:
        return False
    game_rows = MAX_VISIBLE_ROWS - 1
    return state.view_offset + game_rows >= len(state.games)


def visible_game_row_count(state: MenuState) -> int:
    return MAX_VISIBLE_ROWS - 1 if show_truncation_row(state) else MAX_VISIBLE_ROWS


def sync_view_offset(state: MenuState) -> None:
    if not state.games:
        state.view_offset = 0
        return
    for _ in range(2):
        game_rows = visible_game_row_count(state)
        max_offset = max(0, len(state.games) - game_rows)
        if state.selected < state.view_offset:
            state.view_offset = state.selected
        elif state.selected >= state.view_offset + game_rows:
            state.view_offset = state.selected - game_rows + 1
        state.view_offset = max(0, min(state.view_offset, max_offset))


def text_width_px(text: str) -> int:
    return len(text) * 8


def draw_text(fb: Framebuffer, x: int, y: int, text: str, fg: int, bg: int | None = None) -> None:
    """前景のみ描画（bg=None はファームの drawText 相当・背景透過）。"""
    use_bg = bg is not None
    for i, ch in enumerate(text):
        fb.draw_char_8x8(x + i * 8, y, ch, fg, bg if bg is not None else 0, use_bg)


def draw_text_centered(fb: Framebuffer, y: int, text: str, fg: int, bg: int | None = None) -> None:
    x = (SCREEN_WIDTH - text_width_px(text)) // 2
    draw_text(fb, max(0, x), y, text, fg, bg)


def draw_text_centered_in_rect(
    fb: Framebuffer, rect_x: int, rect_y: int, rect_w: int, rect_h: int,
    text: str, fg: int, bg: int,
) -> None:
    tw = text_width_px(text)
    x = rect_x + (rect_w - tw) // 2
    y = rect_y + (rect_h - 8) // 2
    draw_text(fb, max(rect_x, x), y, text, fg, bg)


def fill_rect_translucent(
    fb: Framebuffer,
    bg_pixels: array,
    bg_w: int,
    x: int,
    y: int,
    w: int,
    h: int,
    color: int,
    alpha: int,
) -> None:
    clipped = fb._clip_rect(x, y, w, h)
    if not clipped:
        return
    x0, y0, x1, y1 = clipped
    for row in range(y0, y1):
        dst_off = (row - fb.band_y0) * fb.width
        row_base = row * bg_w
        for col in range(x0, x1):
            dst = bg_pixels[row_base + col]
            blended = blend_rgb565(dst, color, alpha)
            fb._band_buf[dst_off + col] = blended


def blit_background(fb: Framebuffer, bg_pixels: array, bg_w: int, bg_h: int) -> None:
    fb.blit_rgb565(bg_pixels, bg_w, bg_h, 0, 0)


def draw_outline_rect(fb: Framebuffer, x: int, y: int, w: int, h: int, color: int) -> None:
    fb.fill_rect(x, y, w, 1, color)
    fb.fill_rect(x, y + h - 1, w, 1, color)
    fb.fill_rect(x, y, 1, h, color)
    fb.fill_rect(x + w - 1, y, 1, h, color)


def draw_menu_static_chrome(fb: Framebuffer, bg_pixels: array, bg_w: int, bg_h: int) -> None:
    blit_background(fb, bg_pixels, bg_w, bg_h)
    fill_rect_translucent(
        fb, bg_pixels, bg_w, 0, 0, SCREEN_WIDTH, MENU_TOP_CHROME_H,
        MENU_CHROME_BG, MENU_CHROME_OVERLAY_ALPHA,
    )
    fill_rect_translucent(
        fb, bg_pixels, bg_w, 0, MENU_CONTENT_Y0, SCREEN_WIDTH, MENU_CONTENT_H,
        MENU_BG, MENU_CONTENT_OVERLAY_ALPHA,
    )
    fill_rect_translucent(
        fb, bg_pixels, bg_w, 0, MENU_BOTTOM_CHROME_Y, SCREEN_WIDTH, MENU_BOTTOM_CHROME_H,
        MENU_CHROME_BG, MENU_CHROME_OVERLAY_ALPHA,
    )
    fb.fill_rect(0, MENU_DIVIDER_Y_TOP, SCREEN_WIDTH, 1, MENU_DIVIDER)
    fb.fill_rect(0, MENU_DIVIDER_Y_BOTTOM, SCREEN_WIDTH, 1, MENU_DIVIDER)
    fb.fill_rect(MENU_LIST_DIVIDER_X, MENU_CONTENT_Y0, 1, MENU_CONTENT_H, MENU_DIVIDER)
    draw_text_centered(fb, MENU_HEADER_TITLE_Y, "GAME SELECT MENU", COLOR_WHITE)
    draw_text_centered(fb, MENU_FOOTER_HINT_Y, "[NEAR] Launch  [LEFT] Settings", COLOR_GREEN)


def draw_game_row(
    fb: Framebuffer,
    bg_pixels: array,
    bg_w: int,
    state: MenuState,
    visible_slot: int,
    game_index: int,
) -> None:
    row_top = MENU_LIST_FIRST_ROW_Y + visible_slot * MENU_ROW_PITCH
    bg_y = row_top + (MENU_ROW_PITCH - MENU_ROW_BG_H) // 2
    number = game_index + 1
    prefix = f"{number}."
    max_title_chars = max(1, MENU_LIST_TITLE_CHARS - len(prefix))
    title_part = truncated_title(state.games[game_index].title, max_title_chars)
    line = f"{prefix}{title_part}"
    selected = game_index == state.selected
    fg = COLOR_SEL_FG if selected else COLOR_WHITE
    row_bg = MENU_SEL_BG if selected else MENU_ROW_BG
    alpha = MENU_ROW_SEL_OVERLAY_ALPHA if selected else MENU_CONTENT_OVERLAY_ALPHA
    fill_rect_translucent(fb, bg_pixels, bg_w, MENU_LIST_X, bg_y, MENU_LIST_W, MENU_ROW_BG_H, row_bg, alpha)
    draw_text(fb, MENU_LIST_X + 2, bg_y, ">" if selected else " ", fg)
    draw_text(fb, MENU_LIST_X + 12, bg_y, line, fg)


def draw_truncation_row(fb: Framebuffer, bg_pixels: array, bg_w: int, visible_slot: int) -> None:
    row_top = MENU_LIST_FIRST_ROW_Y + visible_slot * MENU_ROW_PITCH
    bg_y = row_top + (MENU_ROW_PITCH - MENU_ROW_BG_H) // 2
    fill_rect_translucent(
        fb, bg_pixels, bg_w, MENU_LIST_X, bg_y, MENU_LIST_W, MENU_ROW_BG_H,
        MENU_ROW_BG, MENU_CONTENT_OVERLAY_ALPHA,
    )
    draw_text(fb, MENU_LIST_X + 12, bg_y, "Limit:256 max", COLOR_GRAY)


def draw_list_panel(fb: Framebuffer, bg_pixels: array, bg_w: int, state: MenuState) -> None:
    if not state.games:
        return
    game_rows = visible_game_row_count(state)
    for slot in range(game_rows):
        game_index = state.view_offset + slot
        if game_index < len(state.games):
            draw_game_row(fb, bg_pixels, bg_w, state, slot, game_index)
    if show_truncation_row(state):
        draw_truncation_row(fb, bg_pixels, bg_w, MAX_VISIBLE_ROWS - 1)


def draw_simple_right_panel(
    fb: Framebuffer, bg_pixels: array, bg_w: int, state: MenuState,
) -> None:
    """簡易プレビュー: 枠 + NO IMAGE + タイトル + サイズ。"""
    draw_outline_rect(fb, RIGHT_PANEL_X, RIGHT_PANEL_Y, RIGHT_PANEL_FRAME_W, RIGHT_PANEL_FRAME_H, COLOR_WHITE)
    fb.fill_rect(PREVIEW_IMAGE_X, PREVIEW_IMAGE_Y, PREVIEW_W, PREVIEW_H, PREVIEW_BG)
    draw_text_centered_in_rect(
        fb, PREVIEW_IMAGE_X, PREVIEW_IMAGE_Y, PREVIEW_W, PREVIEW_H,
        "NO IMAGE", COLOR_GRAY, PREVIEW_BG,
    )
    if state.games and 0 <= state.selected < len(state.games):
        entry = state.games[state.selected]
        title = truncated_title(entry.title, RIGHT_TITLE_CHARS)
        kb = (entry.script_size + 1023) // 1024
        size_line = f"Size : {kb} KB"
    else:
        title = "---"
        size_line = "Size : 0 KB"
    fill_rect_translucent(
        fb, bg_pixels, bg_w, RIGHT_META_X, RIGHT_TITLE_Y, RIGHT_META_BLOCK_W, 10,
        MENU_BG, MENU_CONTENT_OVERLAY_ALPHA,
    )
    draw_text(fb, RIGHT_META_X, RIGHT_TITLE_Y, title, COLOR_WHITE)
    fill_rect_translucent(
        fb, bg_pixels, bg_w, RIGHT_META_X, RIGHT_SIZE_Y, RIGHT_META_BLOCK_W, 10,
        MENU_BG, MENU_CONTENT_OVERLAY_ALPHA,
    )
    draw_text(fb, RIGHT_META_X, RIGHT_SIZE_Y, size_line, COLOR_WHITE)


def draw_empty_games(fb: Framebuffer, bg_pixels: array, bg_w: int, bg_h: int, sd_mounted: bool) -> None:
    draw_menu_static_chrome(fb, bg_pixels, bg_w, bg_h)
    msg_y = MENU_LIST_FIRST_ROW_Y + 24
    fill_rect_translucent(
        fb, bg_pixels, bg_w, MENU_LIST_X, MENU_LIST_FIRST_ROW_Y,
        MENU_LIST_W, MENU_CONTENT_Y1 - MENU_LIST_FIRST_ROW_Y + 1,
        MENU_BG, MENU_CONTENT_OVERLAY_ALPHA,
    )
    if not sd_mounted:
        draw_text(fb, MENU_LIST_X + 4, msg_y, "No SD card inserted", COLOR_YELLOW)
        draw_text(fb, MENU_LIST_X + 4, msg_y + 12, "Insert card to", COLOR_WHITE)
        draw_text(fb, MENU_LIST_X + 4, msg_y + 24, "load games", COLOR_WHITE)
    else:
        draw_text(fb, MENU_LIST_X + 4, msg_y, "No game in /games", COLOR_YELLOW)
        draw_text(fb, MENU_LIST_X + 4, msg_y + 12, "[NEAR] Refresh", COLOR_GRAY)
    draw_outline_rect(fb, RIGHT_PANEL_X, RIGHT_PANEL_Y, RIGHT_PANEL_FRAME_W, RIGHT_PANEL_FRAME_H, COLOR_WHITE)
    fb.fill_rect(PREVIEW_IMAGE_X, PREVIEW_IMAGE_Y, PREVIEW_W, PREVIEW_H, PREVIEW_BG)
    draw_text_centered_in_rect(
        fb, PREVIEW_IMAGE_X, PREVIEW_IMAGE_Y, PREVIEW_W, PREVIEW_H,
        "---", COLOR_GRAY, PREVIEW_BG,
    )


def render_menu_frame(state: MenuState, backgrounds: list[tuple[int, int, array]]) -> Framebuffer:
    bg_w, bg_h, bg_pixels = backgrounds[state.bg_index]
    fb = Framebuffer()
    if not state.games:
        for band in range(fb.band_count()):
            fb.begin_band(band)
            draw_empty_games(fb, bg_pixels, bg_w, bg_h, sd_mounted=True)
            fb.end_band()
        return fb

    sync_view_offset(state)
    for band in range(fb.band_count()):
        fb.begin_band(band)
        draw_menu_static_chrome(fb, bg_pixels, bg_w, bg_h)
        draw_list_panel(fb, bg_pixels, bg_w, state)
        draw_simple_right_panel(fb, bg_pixels, bg_w, state)
        fb.end_band()
    return fb
