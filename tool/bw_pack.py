"""1 ビット白黒フレーム列の単一パックファイル（BWPK）読み書き。"""

from __future__ import annotations

import struct
from pathlib import Path

BWPK_MAGIC = b"BWPK"
BWPK_VERSION = 1
HEADER_SIZE = 12
INDEX_ENTRY_SIZE = 8


def pack_header(frame_count: int) -> bytes:
    if frame_count <= 0:
        raise ValueError("frame_count must be > 0")
    return struct.pack("<4sB3xI", BWPK_MAGIC, BWPK_VERSION, frame_count)


def data_base(frame_count: int) -> int:
    return HEADER_SIZE + frame_count * INDEX_ENTRY_SIZE


def write_pack(path: Path, frames: list[bytes]) -> None:
    """フレーム blob 列を 1 ファイルに書き出す。"""
    if not frames:
        raise ValueError("frames is empty")
    index: list[tuple[int, int]] = []
    offset = 0
    for blob in frames:
        index.append((offset, len(blob)))
        offset += len(blob)

    out = bytearray()
    out.extend(pack_header(len(frames)))
    for off, size in index:
        out.extend(struct.pack("<II", off, size))
    for blob in frames:
        out.extend(blob)
    path.write_bytes(bytes(out))


def read_header(data: bytes) -> tuple[int, int]:
    if len(data) < HEADER_SIZE:
        raise ValueError("truncated BWPK header")
    magic, version, frame_count = struct.unpack_from("<4sB3xI", data, 0)
    if magic != BWPK_MAGIC:
        raise ValueError("invalid BWPK magic")
    if version != BWPK_VERSION:
        raise ValueError(f"unsupported BWPK version {version}")
    if frame_count == 0:
        raise ValueError("empty BWPK")
    return frame_count, data_base(frame_count)


def read_frame_blob(pack_path: Path, frame_index_1based: int) -> bytes:
    """1 始まりのフレーム番号で blob を返す。"""
    with pack_path.open("rb") as f:
        header = f.read(HEADER_SIZE)
        frame_count, base = read_header(header)
        if frame_index_1based < 1 or frame_index_1based > frame_count:
            raise IndexError(frame_index_1based)
        f.seek(HEADER_SIZE + (frame_index_1based - 1) * INDEX_ENTRY_SIZE)
        offset, size = struct.unpack("<II", f.read(INDEX_ENTRY_SIZE))
        f.seek(base + offset)
        return f.read(size)
