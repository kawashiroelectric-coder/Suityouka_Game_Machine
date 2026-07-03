#!/usr/bin/env python3
"""
埋め込み PCM ヘッダー (.h) と WAV のサンプルレート整合性を確認する。

ファームウェア出力は AudioConfig::SAMPLE_RATE（既定 44100 Hz）固定。

例:
    python tool/verify_pcm_header.py assets/boot_chime.h assets/cursor_move.h
    python tool/verify_pcm_header.py my_sound.wav
    python tool/verify_pcm_header.py assets/*.h
"""

from __future__ import annotations

import argparse
import glob
import re
import sys
import wave
from pathlib import Path

TARGET_RATE = 44100


def parse_pcm_header(path: Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace")
    rate_m = re.search(r"_sample_rate\s*=\s*(\d+)", text)
    ch_m = re.search(r"_channels\s*=\s*(\d+)", text)
    frames_m = re.search(r"_frame_count\s*=\s*(\d+)", text)
    pcm_m = re.search(r"_pcm\[(\d+)\]", text)
    if not all((rate_m, ch_m, frames_m, pcm_m)):
        raise ValueError(f"{path}: 必須フィールドが見つかりません")

    rate = int(rate_m.group(1))
    channels = int(ch_m.group(1))
    frames = int(frames_m.group(1))
    pcm_len = int(pcm_m.group(1))
    expected_pcm = frames * channels
    return {
        "path": path,
        "rate": rate,
        "channels": channels,
        "frames": frames,
        "pcm_len": pcm_len,
        "duration_sec": frames / rate if rate else 0.0,
        "pcm_ok": pcm_len == expected_pcm,
    }


def parse_wav(path: Path) -> dict:
    with wave.open(str(path), "rb") as wf:
        channels = wf.getnchannels()
        sample_width = wf.getsampwidth()
        rate = wf.getframerate()
        frames = wf.getnframes()
    if sample_width != 2:
        raise ValueError(f"{path}: 16bit PCM のみ対応 (sampwidth={sample_width})")
    if channels not in (1, 2):
        raise ValueError(f"{path}: mono/stereo のみ (channels={channels})")
    return {
        "path": path,
        "rate": rate,
        "channels": channels,
        "frames": frames,
        "duration_sec": frames / rate if rate else 0.0,
    }


def report_meta(meta: dict, kind: str, target_rate: int) -> list[str]:
    issues: list[str] = []
    p = meta["path"]
    rate = meta["rate"]
    print(f"{p.name} ({kind})")
    print(f"  レート     : {rate} Hz")
    print(f"  チャンネル : {meta['channels']}")
    print(f"  フレーム数 : {meta['frames']}")
    print(f"  長さ       : {meta['duration_sec']:.3f} s")

    if kind == "header":
        print(f"  PCM 配列長 : {meta['pcm_len']} (期待 {meta['frames'] * meta['channels']})")
        if not meta["pcm_ok"]:
            issues.append(f"{p}: _pcm[] 長さと frame_count×channels が不一致")

    if rate != target_rate:
        issues.append(
            f"{p}: レート {rate} Hz ≠ 出力固定 {target_rate} Hz "
            f"（--keep-rate で埋め込んだ場合、実行時リサンプルが走る）"
        )

    if rate == target_rate and kind == "wav":
        print(f"  → {target_rate} Hz: --keep-rate でそのまま変換可能")
    elif rate != target_rate and kind == "wav":
        print(
            f"  → 推奨: ffmpeg -i {p.name} -ar {target_rate} -ac 1 -sample_fmt s16 "
            f"out_{target_rate}.wav"
        )
        print(
            f"     または: python tool/wav_to_pcm_header.py {p.name}  "
            f"（既定で {target_rate} Hz へリサンプル）"
        )

    print()
    return issues


def resolve_inputs(patterns: list[str]) -> list[Path]:
    out: list[Path] = []
    seen: set[Path] = set()
    for pattern in patterns:
        expanded = glob.glob(pattern, recursive=True)
        paths = [Path(p) for p in expanded] if expanded else [Path(pattern)]
        for path in paths:
            resolved = path.resolve()
            if resolved not in seen and path.is_file():
                seen.add(resolved)
                out.append(path)
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description="PCM ヘッダー / WAV の 44.1kHz 整合性チェック")
    parser.add_argument("inputs", nargs="+", help=".h / .wav またはグロブ")
    parser.add_argument(
        "--target-rate",
        type=int,
        default=TARGET_RATE,
        help=f"ファームウェア出力レート (既定: {TARGET_RATE})",
    )
    args = parser.parse_args()
    target_rate = args.target_rate

    paths = resolve_inputs(args.inputs)
    if not paths:
        print("対象ファイルが見つかりません。", file=sys.stderr)
        sys.exit(2)

    all_issues: list[str] = []
    for path in paths:
        ext = path.suffix.lower()
        try:
            if ext == ".h":
                meta = parse_pcm_header(path)
                all_issues.extend(report_meta(meta, "header", target_rate))
            elif ext == ".wav":
                meta = parse_wav(path)
                all_issues.extend(report_meta(meta, "wav", target_rate))
            else:
                print(f"スキップ: {path} （.h / .wav のみ）\n")
        except (ValueError, wave.Error, OSError) as exc:
            all_issues.append(f"{path}: {exc}")
            print(f"エラー: {path}: {exc}\n")

    print(f"=== 出力固定レート: {target_rate} Hz (config.hpp CFG_AUDIO_SAMPLE_RATE) ===")
    if all_issues:
        print("問題:")
        for issue in all_issues:
            print(f"  - {issue}")
        sys.exit(1)
    print("問題は検出されませんでした。")


if __name__ == "__main__":
    main()
