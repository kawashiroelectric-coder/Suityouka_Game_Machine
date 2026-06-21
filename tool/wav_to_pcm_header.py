#!/usr/bin/env python3
"""
WAV (16bit PCM) を C ヘッダー (.h) に変換する。

ファームウェア flash 埋め込み用。生成したヘッダーは LuaAudio::playSeFromEmbedded /
playBgmFromEmbedded から再生できる。

例:
    python tool/wav_to_pcm_header.py sound.wav
    python tool/wav_to_pcm_header.py sound.wav -d assets -n boot_chime
    python tool/wav_to_pcm_header.py sound.wav --rate 22050
    python tool/wav_to_pcm_header.py bgm.wav --bgm -d assets -n title_bgm
    python tool/wav_to_pcm_header.py your_jingle.wav -d assets -n boot_chime --bgm

SE 向けは PCM データ 32KB 以下（--max-kb 32 が既定）。
長い BGM は --bgm を付ける（サイズ警告のみ緩和）。

必要: Python 3 標準ライブラリのみ（wave）
"""

from __future__ import annotations

import argparse
import glob
import re
import struct
import sys
import wave
from pathlib import Path


DEFAULT_RATE = 44100
DEFAULT_MAX_KB = 32


def sanitize_identifier(name: str) -> str:
    sanitized = re.sub(r"[^0-9A-Za-z_]", "_", name)
    if re.match(r"^[0-9]", sanitized):
        sanitized = "snd_" + sanitized
    return sanitized


def linear_resample_mono(samples: list[int], src_rate: int, dst_rate: int) -> list[int]:
    if src_rate == dst_rate or not samples:
        return samples
    out_len = max(1, int(round(len(samples) * dst_rate / src_rate)))
    out: list[int] = []
    for i in range(out_len):
        src_pos = i * src_rate / dst_rate
        idx = int(src_pos)
        frac = src_pos - idx
        s0 = samples[min(idx, len(samples) - 1)]
        s1 = samples[min(idx + 1, len(samples) - 1)]
        out.append(int(round(s0 + (s1 - s0) * frac)))
    return out


def linear_resample_stereo(samples: list[tuple[int, int]], src_rate: int, dst_rate: int) -> list[tuple[int, int]]:
    if src_rate == dst_rate or not samples:
        return samples
    out_len = max(1, int(round(len(samples) * dst_rate / src_rate)))
    out: list[tuple[int, int]] = []
    for i in range(out_len):
        src_pos = i * src_rate / dst_rate
        idx = int(src_pos)
        frac = src_pos - idx
        l0, r0 = samples[min(idx, len(samples) - 1)]
        l1, r1 = samples[min(idx + 1, len(samples) - 1)]
        out.append((
            int(round(l0 + (l1 - l0) * frac)),
            int(round(r0 + (r1 - r0) * frac)),
        ))
    return out


def read_wav_pcm16(path: Path) -> tuple[list[int] | list[tuple[int, int]], int, int]:
    with wave.open(str(path), "rb") as wf:
        channels = wf.getnchannels()
        sample_width = wf.getsampwidth()
        sample_rate = wf.getframerate()
        frame_count = wf.getnframes()

        if sample_width != 2:
            raise ValueError(f"{path}: 16bit PCM のみ対応 (sampwidth={sample_width})")
        if channels not in (1, 2):
            raise ValueError(f"{path}: mono/stereo のみ対応 (channels={channels})")

        raw = wf.readframes(frame_count)
        if len(raw) != frame_count * channels * 2:
            raise ValueError(f"{path}: フレームサイズ不一致")

        if channels == 1:
            samples = list(struct.unpack(f"<{frame_count}h", raw))
            return samples, sample_rate, channels

        pairs = struct.unpack(f"<{frame_count * 2}h", raw)
        stereo = [(pairs[i], pairs[i + 1]) for i in range(0, len(pairs), 2)]
        return stereo, sample_rate, channels


def to_interleaved_pcm(samples: list[int] | list[tuple[int, int]], channels: int) -> list[int]:
    if channels == 1:
        assert isinstance(samples, list) and (not samples or isinstance(samples[0], int))
        return samples  # type: ignore[return-value]
    assert channels == 2
    out: list[int] = []
    for left, right in samples:  # type: ignore[misc]
        out.append(left)
        out.append(right)
    return out


def generate_header(
    name: str,
    pcm: list[int],
    sample_rate: int,
    channels: int,
    comment: str | None,
) -> str:
    safe = sanitize_identifier(name)
    guard = safe.upper() + "_H"
    frame_count = len(pcm) // channels
    byte_size = len(pcm) * 2

    lines: list[str] = []
    if comment:
        lines.append(f"// {comment}")
    lines.append("// C++: LuaAudio::playSeFromEmbedded / playBgmFromEmbedded")
    lines.append(f"// {sample_rate} Hz, {channels} ch, {frame_count} frames, {byte_size} bytes")
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"static const uint32_t {safe}_sample_rate = {sample_rate};")
    lines.append(f"static const uint16_t {safe}_channels = {channels};")
    lines.append(f"static const uint32_t {safe}_frame_count = {frame_count};")
    lines.append(f"static const int16_t {safe}_pcm[{len(pcm)}] = {{")

    values_per_line = 12
    for i in range(0, len(pcm), values_per_line):
        chunk = pcm[i : i + values_per_line]
        line = ",".join(str(v) for v in chunk)
        lines.append(f"    {line},")

    lines.append("};")
    lines.append("")
    lines.append(f"#endif // {guard}")
    return "\n".join(lines) + "\n"


def convert_file(
    src: Path,
    dst: Path,
    base_name: str | None,
    target_rate: int | None,
    max_bytes: int,
    allow_large: bool,
) -> None:
    samples, sample_rate, channels = read_wav_pcm16(src)

    if target_rate and target_rate != sample_rate:
        if channels == 1:
            samples = linear_resample_mono(samples, sample_rate, target_rate)  # type: ignore[assignment]
        else:
            samples = linear_resample_stereo(samples, sample_rate, target_rate)  # type: ignore[assignment]
        sample_rate = target_rate

    pcm = to_interleaved_pcm(samples, channels)
    byte_size = len(pcm) * 2

    if byte_size > max_bytes and not allow_large:
        raise ValueError(
            f"{src}: PCM {byte_size} bytes > SE 上限 {max_bytes} bytes。"
            f"短くするか --bgm（BGM 埋め込み向け）を使ってください。"
        )
    if byte_size > max_bytes and allow_large:
        print(f"警告: {src} は {byte_size} bytes（flash 容量に注意）", file=sys.stderr)

    stem = base_name or src.stem
    safe = sanitize_identifier(stem)
    header = generate_header(
        safe,
        pcm,
        sample_rate,
        channels,
        comment=f"{src.name}: embedded PCM from WAV",
    )
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(header, encoding="utf-8")
    frame_count = len(pcm) // channels
    print(f"{src} -> {dst}  ({sample_rate}Hz, {channels}ch, {frame_count} frames, {byte_size} bytes)")


def resolve_inputs(patterns: list[str]) -> list[Path]:
    result: list[Path] = []
    seen: set[Path] = set()
    for pattern in patterns:
        expanded = glob.glob(pattern, recursive=True)
        paths = [Path(p) for p in expanded] if expanded else [Path(pattern)]
        for path in paths:
            resolved = path.resolve()
            if resolved not in seen:
                seen.add(resolved)
                result.append(path)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description="WAV を埋め込み PCM C ヘッダー (.h) に変換")
    parser.add_argument("inputs", nargs="+", help="入力 WAV またはグロブ")
    parser.add_argument("-d", "--output-dir", default="assets", help="出力ディレクトリ")
    parser.add_argument("-n", "--name", help="ヘッダー内の変数名ベース")
    parser.add_argument(
        "--rate",
        type=int,
        default=DEFAULT_RATE,
        help=f"出力サンプルレート (既定: {DEFAULT_RATE}。元と同じならリサンプルしない)",
    )
    parser.add_argument(
        "--max-kb",
        type=int,
        default=DEFAULT_MAX_KB,
        help=f"SE 向け PCM サイズ上限 KB (既定: {DEFAULT_MAX_KB})",
    )
    parser.add_argument(
        "--bgm",
        action="store_true",
        help="BGM 埋め込み向け（サイズ上限を緩和して警告のみ）",
    )
    parser.add_argument(
        "--keep-rate",
        action="store_true",
        help="WAV の元サンプルレートを維持（リサンプルしない）",
    )
    args = parser.parse_args()

    inputs = [p for p in resolve_inputs(args.inputs) if p.suffix.lower() == ".wav"]
    if not inputs:
        print("対象の WAV ファイルが見つかりません。", file=sys.stderr)
        sys.exit(2)

    out_dir = Path(args.output_dir)
    max_bytes = args.max_kb * 1024
    if args.bgm:
        max_bytes = 8 * 1024 * 1024

    target_rate = None if args.keep_rate else args.rate

    for src in inputs:
        name = args.name if len(inputs) == 1 else None
        dst = out_dir / f"{(name or src.stem)}.h"
        convert_file(src, dst, name, target_rate, max_bytes, args.bgm)


if __name__ == "__main__":
    main()
