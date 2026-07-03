"""1 ビット白黒フレーム（SKIP / DELTA / FULL）のプレビュー用処理。"""

from __future__ import annotations

from pathlib import Path


class BwFrameBuffer:
    """実機 bg_stream_util.cpp と同様のフレームバッファ。"""

    def __init__(self) -> None:
        self.frame: bytearray | None = None
        self.valid = False
        self.path: str | None = None
        self.pack_path: str | None = None
        self.pack_frame = 0
        self.buffer_frame = 0
        self.width = 0
        self.height = 0
        self.fg = 0
        self.bg = 0

    @staticmethod
    def frame_byte_size(width: int, height: int) -> int:
        row_bytes = (width + 7) // 8
        return row_bytes * height

    def ensure_size(self, width: int, height: int) -> int:
        need = self.frame_byte_size(width, height)
        if self.frame is None or len(self.frame) != need:
            self.frame = bytearray(need)
            self.valid = False
        self.width = width
        self.height = height
        return (width + 7) // 8

    def apply_blob(self, data: bytes, width: int, height: int) -> bool:
        row_bytes = self.ensure_size(width, height)
        frame_bytes = row_bytes * height
        file_size = len(data)

        if file_size == 0:
            return self.valid

        if file_size > frame_bytes:
            return False

        assert self.frame is not None

        if file_size == frame_bytes:
            self.frame[:] = data
            self.valid = True
            return True

        if not self.valid:
            return False

        row_count = data[0]
        expected = 1 + row_count * (1 + row_bytes)
        if file_size != expected or row_count == 0 or row_count > height:
            return False

        offset = 1
        for _ in range(row_count):
            y = data[offset]
            offset += 1
            if y >= height:
                return False
            row_off = y * row_bytes
            self.frame[row_off : row_off + row_bytes] = data[offset : offset + row_bytes]
            offset += row_bytes
        self.valid = True
        return True

    def load_from_file(self, full_path: Path, width: int, height: int) -> bool:
        if not full_path.is_file():
            return False
        return self.apply_blob(full_path.read_bytes(), width, height)

    def load_from_pack(self, pack_path: Path, frame_index_1based: int, width: int, height: int) -> bool:
        try:
            from tool.bw_pack import read_frame_blob
        except ImportError:
            return False
        if not pack_path.is_file():
            return False
        try:
            blob = read_frame_blob(pack_path, frame_index_1based)
        except (OSError, ValueError, IndexError):
            return False
        return self.apply_blob(blob, width, height)

    def sync_pack_frame(
        self, pack_path: Path, target_frame: int, width: int, height: int
    ) -> bool:
        try:
            from tool.bw_pack import read_frame_blob, read_header
        except ImportError:
            return False
        if not pack_path.is_file() or target_frame <= 0:
            return False

        with pack_path.open("rb") as f:
            header = f.read(12)
        try:
            frame_count, _data_base = read_header(header)
        except ValueError:
            return False
        if target_frame > frame_count:
            return False

        last = self.buffer_frame
        if last == target_frame and self.valid:
            return True

        if last <= 0 or not self.valid:
            for fno in range(1, target_frame + 1):
                try:
                    blob = read_frame_blob(pack_path, fno)
                except (OSError, ValueError, IndexError):
                    return False
                if not self.apply_blob(blob, width, height):
                    return False
            self.buffer_frame = target_frame
            return True

        if last == frame_count and target_frame == 1:
            try:
                blob = read_frame_blob(pack_path, 1)
            except (OSError, ValueError, IndexError):
                return False
            if not self.apply_blob(blob, width, height):
                return False
            self.buffer_frame = 1
            return True

        if last < target_frame:
            for fno in range(last + 1, target_frame + 1):
                try:
                    blob = read_frame_blob(pack_path, fno)
                except (OSError, ValueError, IndexError):
                    return False
                if not self.apply_blob(blob, width, height):
                    return False
            self.buffer_frame = target_frame
            return True

        for fno in range(1, target_frame + 1):
            try:
                blob = read_frame_blob(pack_path, fno)
            except (OSError, ValueError, IndexError):
                return False
            if not self.apply_blob(blob, width, height):
                return False
        self.buffer_frame = target_frame
        return True

    def blit_band(self, fb, dx: int, dy: int, width: int, height: int, fg: int, bg: int) -> bool:
        if not self.valid or self.frame is None:
            return False

        band_top = fb.band_top()
        band_bottom = fb.band_bottom()
        img_bottom = dy + height
        top = dy if dy > band_top else band_top
        bottom = img_bottom if img_bottom < band_bottom else band_bottom
        if top >= bottom:
            return True

        row_bytes = (width + 7) // 8
        src_y0 = top - dy
        fg &= 0xFFFF
        bg &= 0xFFFF

        for row in range(bottom - top):
            screen_y = top + row
            src_y = src_y0 + row
            line = self.frame[src_y * row_bytes : src_y * row_bytes + row_bytes]
            dst_off = (screen_y - fb.band_y0) * fb.width
            for x in range(width):
                screen_x = dx + x
                if screen_x < 0 or screen_x >= fb.width:
                    continue
                bit = 7 - (x % 8)
                on = (line[x // 8] >> bit) & 1
                fb._band_buf[dst_off + screen_x] = fg if on else bg
        return True
