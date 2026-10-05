#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FW_Test 用アセット生成（テスト WAV・テスト画像・フォント）

実行:  python test/FW_Test/generate_test_assets.py   （numpy / Pillow が必要）

出力:
  audio/t*.wav      … BGM 形式テスト用（4 秒。内容はすべて同じ）
  audio/long*.wav   … BGM 負荷測定用（12 秒）
  img/test16.bin    … 16x16 テスト画像（マゼンタ透過）
  fonts/game_font.bin … FW_Test.lua の文字だけを含む 12px フォント
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
VN_DIR = ROOT / "games" / "visual_novel"
TTF = VN_DIR / "fonts" / "PixelMplus-20130602" / "PixelMplus12-Regular.ttf"


def wav_bytes(pcm: np.ndarray, rate: int, extra_list=False, odd=False):
    """pcm: shape (n, ch) float -1..1"""
    ch = pcm.shape[1]
    data = (np.clip(pcm, -1, 1) * 30000).astype("<i2").tobytes()
    if odd:
        data += b"\x55"  # 端数 1 バイト（フレーム境界に満たない）
    fmt = b"fmt " + struct.pack("<IHHIIHH", 16, 1, ch, rate, rate * ch * 2, ch * 2, 16)
    lst = (b"LIST" + struct.pack("<I", 26) + b"INFOISFT" + struct.pack("<I", 14) + b"FW_Test gen\x00\x00\x00") if extra_list else b""
    body = b"WAVE" + fmt + lst + b"data" + struct.pack("<I", len(data)) + data
    if len(data) % 2:
        body += b"\x00"  # RIFF のパディング
    return b"RIFF" + struct.pack("<I", len(body)) + body


def test_signal(rate: int, ch: int, seconds=4.0):
    n = int(rate * seconds)
    t = np.arange(n) / rate
    tone = 0.35 * np.sin(2 * np.pi * 440 * t)
    left = np.zeros(n)
    right = np.zeros(n)
    a = t < 1.0
    b = (t >= 1.0) & (t < 2.0)
    c = (t >= 2.0) & (t < 3.8)
    left[a] = tone[a]
    right[b] = tone[b]
    left[c] = tone[c] * 0.6
    right[c] = tone[c] * 0.6
    # 0.5 秒ごとのクリック（1.5kHz 12ms）
    for k in range(8):
        s = int(k * 0.5 * rate)
        e = min(n, s + int(0.012 * rate))
        tt = np.arange(e - s) / rate
        click = 0.6 * np.sin(2 * np.pi * 1500 * tt) * np.linspace(1, 0, e - s)
        left[s:e] += click
        right[s:e] += click
    # 3.8〜4.0 秒: 終わりの合図 880Hz
    d = t >= 3.8
    hi = 0.35 * np.sin(2 * np.pi * 880 * t)
    left[d] = hi[d]
    right[d] = hi[d]
    if ch == 1:
        return ((left + right) * 0.5 * 1.6).reshape(-1, 1)
    return np.stack([left, right], 1)


def long_signal(rate: int, ch: int, seconds=12.0):
    n = int(rate * seconds)
    t = np.arange(n) / rate
    x = 0.25 * np.sin(2 * np.pi * 330 * t) + 0.1 * np.sin(2 * np.pi * 495 * t)
    if ch == 1:
        return x.reshape(-1, 1)
    return np.stack([x, 0.25 * np.sin(2 * np.pi * 262 * t)], 1)


def make_audio():
    out = HERE / "audio"
    out.mkdir(exist_ok=True)
    specs = [
        ("t44s", 44100, 2, {}), ("t44m", 44100, 1, {}), ("t22s", 22050, 2, {}), ("t22m", 22050, 1, {}),
        ("t11m", 11025, 1, {}), ("t48s", 48000, 2, {}), ("t32m", 32000, 1, {}), ("t8m", 8000, 1, {}),
        ("t44odd", 44100, 2, {"odd": True}), ("t22list", 22050, 2, {"extra_list": True}),
    ]
    for name, rate, ch, kw in specs:
        (out / f"{name}.wav").write_bytes(wav_bytes(test_signal(rate, ch), rate, **kw))
    # 途中で切れたファイル: data サイズは 4 秒分と宣言し、実データは 3.5 秒で打ち切り
    full = wav_bytes(test_signal(44100, 2), 44100)
    cut = len(full) - int(0.5 * 44100) * 4
    (out / "t44cut.wav").write_bytes(full[:cut])
    for name, rate, ch in (("long44s", 44100, 2), ("long22m", 22050, 1), ("long48s", 48000, 2)):
        (out / f"{name}.wav").write_bytes(wav_bytes(long_signal(rate, ch), rate))
    print("audio: done")


def make_image():
    key = 0xF81F
    px = []
    for y in range(16):
        for x in range(16):
            if (x - 7.5) ** 2 + (y - 7.5) ** 2 > 60:
                px.append(key)
            elif x < 8 and y < 8:
                px.append(0xF800)   # 赤
            elif x >= 8 and y < 8:
                px.append(0x07E0)   # 緑
            elif x < 8:
                px.append(0x001F)   # 青
            else:
                px.append(0xFFE0)   # 黄
    p = HERE / "img" / "test16.bin"
    p.parent.mkdir(exist_ok=True)
    p.write_bytes(struct.pack("<256H", *px))
    print("image: done")


def make_font():
    if not TTF.exists():
        print("font: skip (TTF not found)", TTF)
        return
    sys.path.insert(0, str(VN_DIR))
    import generate_font as vn  # noqa: E402
    needed = set(vn.EXTRA_CODEPOINTS)
    for cp in range(0x20, 0x7F):
        needed.add(cp)
    src = (HERE / "FW_Test.lua").read_text(encoding="utf-8")
    for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', src):
        for ch in m.group(1):
            needed.add(ord(ch))
    for ch in "▶秒%":
        needed.add(ord(ch))
    _, gw, gh, rast = vn.parse_ttf(TTF, 12)
    entries, missing = vn.build_entries(None, rast, needed)
    if missing:
        print("font: missing", "".join(missing))
    vn.write_misf(HERE / "fonts" / "game_font.bin", entries, gw, gh, gw)


if __name__ == "__main__":
    make_audio()
    make_image()
    make_font()
