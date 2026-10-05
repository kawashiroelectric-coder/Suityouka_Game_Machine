#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
翠灯夜行 (JadeLantern) の BGM / 効果音を合成して WAV を書き出す（チップチューン風）。

実行:
  pip install numpy
  python games/JadeLantern/tools/generate_audio.py

出力:
  bgm/*.wav         22050Hz / 16bit / モノラル（machine.play_wav でストリーミング再生）
  bgm/bgm_len.lua   曲の長さ(ms)。play_wav はループしないので Lua 側で再スタートに使う
  se/*.wav          11025Hz / 16bit / モノラル（machine.play_se、1 ファイル 32KB 以下）

曲はすべてこのスクリプト内の MML 風データとコード進行から生成するオリジナル。
"""

from __future__ import annotations

import struct
from pathlib import Path

import numpy as np

GAME_DIR = Path(__file__).resolve().parent.parent
BGM_DIR = GAME_DIR / "bgm"
SE_DIR = GAME_DIR / "se"
SR = 22050
SE_SR = 11025


def write_wav(path: Path, data: np.ndarray, sr: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = np.clip(data, -1.0, 1.0)
    pcm = (pcm * 32000).astype("<i2").tobytes()
    header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, sr, sr * 2, 2, 16)
    header += b"data" + struct.pack("<I", len(pcm))
    path.write_bytes(header + pcm)


# ---------------------------------------------------------------------------
# 音源
# ---------------------------------------------------------------------------
NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi_freq(m: float) -> float:
    return 440.0 * 2 ** ((m - 69) / 12)


def env_adsr(n: int, sr: int, a=0.005, d=0.08, s=0.6, r=0.05) -> np.ndarray:
    t = np.arange(n) / sr
    e = np.ones(n) * s
    na = max(1, int(a * sr))
    nd = max(1, int(d * sr))
    e[:na] = np.linspace(0, 1, na, endpoint=False)[: min(na, n)] if n >= na else np.linspace(0, 1, n)
    if n > na:
        k = min(nd, n - na)
        e[na:na + k] = np.linspace(1, s, nd)[:k]
    nr = min(int(r * sr), n)
    if nr > 0:
        e[n - nr:] *= np.linspace(1, 0, nr)
    return e


def osc(kind: str, freq: float, n: int, sr: int, vib: float = 0.0) -> np.ndarray:
    t = np.arange(n) / sr
    if vib > 0:
        # 少し遅れてかかるビブラート
        depth = vib * np.clip((t - 0.12) * 4, 0, 1)
        phase = np.cumsum(freq * (1 + depth * np.sin(2 * np.pi * 5.5 * t))) / sr
    else:
        phase = freq * t
    ph = phase % 1.0
    if kind == "pulse25":
        return np.where(ph < 0.25, 1.0, -1.0)
    if kind == "pulse12":
        return np.where(ph < 0.125, 1.0, -1.0)
    if kind == "square":
        return np.where(ph < 0.5, 1.0, -1.0)
    if kind == "tri":
        return 4 * np.abs(ph - 0.5) - 1
    if kind == "saw":
        return 2 * ph - 1
    if kind == "sine":
        return np.sin(2 * np.pi * ph)
    raise ValueError(kind)


def drum(kind: str, sr: int) -> np.ndarray:
    rng = np.random.default_rng(1)
    if kind == "k":
        n = int(0.14 * sr)
        t = np.arange(n) / sr
        f = 150 * np.exp(-t * 30) + 45
        ph = np.cumsum(f) / sr
        return np.sin(2 * np.pi * ph) * np.exp(-t * 18) * 1.0
    if kind == "s":
        n = int(0.14 * sr)
        t = np.arange(n) / sr
        return (rng.uniform(-1, 1, n) * 0.7 + np.sin(2 * np.pi * 190 * t) * 0.4) * np.exp(-t * 22)
    if kind == "h":
        n = int(0.04 * sr)
        t = np.arange(n) / sr
        x = rng.uniform(-1, 1, n)
        x = np.diff(np.concatenate([[0], x]))  # 高域寄り
        return x * np.exp(-t * 90) * 0.5
    if kind == "o":
        n = int(0.16 * sr)
        t = np.arange(n) / sr
        x = rng.uniform(-1, 1, n)
        x = np.diff(np.concatenate([[0], x]))
        return x * np.exp(-t * 20) * 0.35
    raise ValueError(kind)


# ---------------------------------------------------------------------------
# 作曲データ
#   コード: "Am", "F", "G", "Em", "Dm", "C", "E", "Bb" など（ルート + m/7 等）
#   メロディ: 16 分音符グリッドの文字列。1 小節 = 16 文字
#     数字/記号: スケール度数（1..7、+/- でオクターブ）  '.' 延ばし  '_' 休符
# ---------------------------------------------------------------------------
CHORD_Q = {"": [0, 4, 7], "m": [0, 3, 7], "7": [0, 4, 7, 10], "m7": [0, 3, 7, 10],
           "M7": [0, 4, 7, 11], "sus4": [0, 5, 7], "dim": [0, 3, 6], "add9": [0, 4, 7, 14]}


def parse_chord(c: str):
    root = NOTE[c[0]]
    rest = c[1:]
    if rest.startswith("#"):
        root += 1
        rest = rest[1:]
    elif rest.startswith("b"):
        root -= 1
        rest = rest[1:]
    return root % 12, CHORD_Q.get(rest, [0, 4, 7])


SCALES = {
    "minor": [0, 2, 3, 5, 7, 8, 10],
    "harm": [0, 2, 3, 5, 7, 8, 11],
    "major": [0, 2, 4, 5, 7, 9, 11],
    "dorian": [0, 2, 3, 5, 7, 9, 10],
    "miyako": [0, 1, 3, 5, 7, 8, 10],  # フリジアン（都節音階寄りの和風）
}


def degree_to_midi(deg: int, octv: int, key: int, scale) -> int:
    d = deg - 1
    o = d // 7
    return 60 + key + scale[d % 7] + 12 * (o + octv)


def parse_melody(s: str, key: int, scale, base_oct=0):
    """戻り値: [(開始step, 長さstep, midi)]"""
    notes = []
    s = s.replace(" ", "").replace("|", "")
    i = 0
    step = 0
    cur = None
    octv = base_oct
    sharp = 0
    while i < len(s):
        ch = s[i]
        if ch == "#":
            sharp = 1
            i += 1
            continue
        if ch == "+":
            octv += 1
            i += 1
            continue
        if ch == "-":
            octv -= 1
            i += 1
            continue
        if ch.isdigit():
            if cur:
                notes.append(cur)
            cur = [step, 1, degree_to_midi(int(ch), octv, key, scale) + sharp]
            octv = base_oct
            sharp = 0
        elif ch == ".":
            if cur:
                cur[1] += 1
        elif ch == "_":
            if cur:
                notes.append(cur)
            cur = None
        step += 1
        i += 1
    if cur:
        notes.append(cur)
    return notes, step


def normalize_bar(bar: str) -> str:
    """1 小節を 16 ステップにそろえる（不足は '.' で延ばし、超過は切り捨て）"""
    toks = []
    mod = ""
    for c in bar.replace(" ", ""):
        if c in "+-#":
            mod += c
        else:
            toks.append(mod + c)
            mod = ""
    toks = toks[:16]
    while len(toks) < 16:
        toks.append(".")
    return "".join(toks)


class Song:
    def __init__(self, name, bpm, key, scale, chords, melody, drums="rock",
                 lead="pulse25", arp=True, bass="tri", swing=0.0, lead_oct=0):
        self.name = name
        self.bpm = bpm
        self.key = NOTE[key[0]] + (1 if "#" in key else 0) - (1 if key.endswith("b") and len(key) > 1 else 0)
        self.scale = SCALES[scale]
        self.chords = chords          # 1 小節 1 コード（リスト）
        self.melody = melody          # 小節ごとの文字列リスト
        self.drums = drums
        self.lead = lead
        self.arp = arp
        self.bass = bass
        self.lead_oct = lead_oct

    def render(self, sr=SR) -> np.ndarray:
        step_sec = 60.0 / self.bpm / 4
        # 2 周目はリードの音色を変えて繰り返す（曲を長く）
        self.chords = self.chords + self.chords
        self.melody = self.melody + self.melody
        bars = len(self.chords)
        total_steps = bars * 16
        n = int(total_steps * step_sec * sr)
        out = np.zeros(n + sr)

        def put(start_step, length_step, sig):
            a = int(start_step * step_sec * sr)
            b = a + len(sig)
            if b > len(out):
                sig = sig[: len(out) - a]
                b = len(out)
            out[a:b] += sig

        # メロディ（各小節を 16 ステップにそろえる）
        mel = "".join(normalize_bar(b) for b in self.melody)
        notes, _ = parse_melody(mel, self.key, self.scale, self.lead_oct)
        half = len(self.chords) * 8
        alt = {"square": "pulse25", "pulse25": "square", "tri": "square", "saw": "pulse25"}
        for st, ln, m in notes:
            dur = ln * step_sec
            k = int(dur * sr)
            kind = self.lead if st < half else alt.get(self.lead, self.lead)
            sig = osc(kind, midi_freq(m + 12), k, sr, vib=0.006 if ln >= 4 else 0.0)
            sig *= env_adsr(k, sr, 0.004, 0.1, 0.65, min(0.05, dur / 3)) * 0.20
            put(st, ln, sig)
            # 薄いディレイ（エコー）
            put(st + 3, ln, sig * 0.28)

        for bar, ch in enumerate(self.chords):
            if ch == "-":
                continue
            root, q = parse_chord(ch)
            base = 36 + ((root - 0) % 12)
            if base < 40:
                base += 12
            # ベース: 8 分でルート／オクターブ
            for k in range(8):
                m = base + (12 if k % 2 == 1 and self.bass == "oct" else 0)
                if self.bass == "walk" and k % 2 == 1:
                    m = base + q[(k // 2) % len(q)]
                ln = 2
                kk = int(ln * step_sec * sr)
                sig = osc("tri", midi_freq(m), kk, sr) * env_adsr(kk, sr, 0.003, 0.05, 0.8, 0.02) * 0.30
                put(bar * 16 + k * 2, ln, sig)
            # アルペジオ（16 分）
            if self.arp:
                tones = [60 + root + i for i in q] + [72 + root + q[0]]
                for k in range(16):
                    m = tones[k % len(tones)] if (k // 4) % 2 == 0 else tones[-1 - (k % len(tones))]
                    kk = int(step_sec * sr)
                    sig = osc("pulse12", midi_freq(m), kk, sr) * env_adsr(kk, sr, 0.002, 0.04, 0.3, 0.01) * 0.055
                    put(bar * 16 + k, 1, sig)
            # ドラム
            pat = DRUMS[self.drums]
            for k, ch2 in enumerate(pat):
                if ch2 == ".":
                    continue
                for dch in ch2:
                    put(bar * 16 + k, 1, drum(dch, sr) * 0.30)

        out = out[:n]
        # 簡易マスタリング
        peak = np.max(np.abs(out)) + 1e-6
        out = np.tanh(out / peak * 1.3) * 0.85
        # ループの継ぎ目を少しだけなめらかに
        f = int(0.01 * sr)
        out[:f] *= np.linspace(0, 1, f)
        out[-f:] *= np.linspace(1, 0, f)
        return out


DRUMS = {
    #          1...2...3...4...
    "rock": ["k", ".", "h", ".", "s", ".", "h", ".", "k", ".", "k", "h", "s", ".", "h", "h"],
    "fast": ["k", "h", "h", "h", "s", "h", "k", "h", "k", "h", "h", "h", "s", "h", "h", "o"],
    "calm": ["k", ".", ".", ".", "h", ".", ".", ".", "k", ".", "k", ".", "h", ".", ".", "."],
    "waltz": ["k", ".", ".", ".", "h", ".", "h", ".", "s", ".", ".", ".", "h", ".", "h", "."],
    "none": ["."] * 16,
    "boss": ["k", "h", "k", "h", "s", "h", "k", "h", "k", "h", "k", "h", "s", "h", "s", "s"],
}


def rep(lst, n):
    return [x for _ in range(n) for x in lst]


SONGS = [
    # タイトル「翠灯夜行」
    Song("title", 112, "D", "minor",
         ["Dm", "Bb", "C", "Am", "Dm", "Bb", "Gm", "A",
          "Bb", "C", "Am", "Dm", "Gm", "C", "F", "A"],
         ["5...3...4...5...", "6...5...4...3...", "4...3...2...3...", "1.......-5......",
          "5...3...4...5...", "6...+1...7...6...", "5...4...3...2...", "#3..............",
          "4...5...6...+1..", "+2...+1...7...6..", "5...3...1...3...", "2.......1.......",
          "4...5...6...5...", "+1...7...6...5..", "6...5...4...3...", "2...............",
          ], drums="calm", lead="square", arp=True),
    # 1 面「宵待ちの畦道」
    Song("st1", 150, "A", "minor",
         rep(["Am", "F", "G", "Em"], 2) + ["F", "G", "Am", "Am", "F", "G", "E", "E"],
         ["1.3.5...6.5.3...", "4.3.2.1.2...3...", "5.5.6.7.+1...7.6.", "5.......3.......",
          "1.3.5...6.5.3...", "4.3.2.1.2...3...", "5.6.7.+1.+2...+1.7.", "+1..............",
          "6...+1...6...5...", "4...5...7...+2..", "+1.7.6.5.3.5.6.", "6...............",
          "6...+1...+3...+2..", "+1...7...+2...+1..", "7.6.5.#4.#5...7...", "#5..............",
          ], drums="rock"),
    # 2 面「千本鳥居の狐火」
    Song("st2", 160, "E", "miyako",
         rep(["Em", "C", "Am", "B7"], 2) + ["C", "D", "Em", "Em", "Am", "C", "B7", "B7"],
         ["1.2.3...5.3.2.1.", "3...2...1...-5...", "1.2.3...5.6.5.3.", "2...............",
          "1.2.3...5.3.2.1.", "3...2...1...3...", "4...3...2...1...", "1...............",
          "5.6.+1...+2.+1.6.", "5...6...5...3...", "5.6.+1...+2.+1.6.", "5...............",
          "+1...6...5...3...", "5...6...+1...+2..", "+1...6...5...3...", "2...............",
          ], drums="fast", lead="pulse25"),
    # 3 面「白糸の滝に霧は立つ」
    Song("st3", 132, "C", "dorian",
         rep(["Cm", "Bb", "Ab", "Bb"], 2) + ["Fm", "G", "Cm", "Cm", "Ab", "Bb", "G", "G"],
         ["5.......3...4...", "5...4...2.......", "3.......1...2...", "4...5...2.......",
          "5.......3...4...", "5...6...7.......", "+1...7...6...4..", "5...............",
          "4...5...6.......", "7...+1...+2.....", "+1...7...6...5..", "3...............",
          "6...5...4...3...", "4...5...6...7...", "+2...+1...7...+1.", "7...............",
          ], drums="calm", lead="tri", arp=True),
    # 4 面「螺子仕掛けの夜想曲」
    Song("st4", 144, "G", "harm",
         rep(["Gm", "Cm", "D7", "Gm"], 2) + ["Eb", "F", "Bb", "Gm", "Cm", "Eb", "D7", "D7"],
         ["1.5.3.5.1.5.3.5.", "4.+1.6.+1.4.+1.6.+1.", "7.5.2.5.7.5.2.5.", "1.......5.......",
          "1.5.3.5.1.5.3.5.", "4.+1.6.+1.4.+1.6.+1.", "7...6...5...4...", "5...............",
          "3...5...+1...+3..", "+2...+1...7...6...", "+1...6...4...6...", "5...............",
          "4...6...+1...+3..", "+2...+1...7...6...", "5...#4...5...6...", "7...............",
          ], drums="waltz", lead="square"),
    # 5 面「雷雲を越えて」
    Song("st5", 170, "B", "minor",
         rep(["Bm", "G", "A", "F#m"], 2) + ["G", "A", "Bm", "Bm", "Em", "G", "F#", "F#"],
         ["1.1.3.5.1.1.3.5.", "6.5.3.2.1...-7...", "1.1.3.5.6.5.+1.7.", "5...............",
          "1.1.3.5.1.1.3.5.", "6.5.3.2.1...3...", "4.5.6.7.+1.+2.+1.7.", "+1..............",
          "6...+1...+3...+2..", "+1...7...5...6...", "7...+1...+2...+3..", "+2..............",
          "+4...+3...+2...+1..", "7...6...5...6...", "7.+1.+2.+1.7.6.#5.7.", "#5..............",
          ], drums="fast", lead="pulse25"),
    # 6 面「翠月の天守閣」
    Song("st6", 150, "F#", "minor",
         rep(["F#m", "D", "E", "C#m"], 2) + ["D", "E", "F#m", "F#m", "Bm", "D", "C#", "C#"],
         ["5...4...3...1...", "2...3...4...5...", "6...5...4...3...", "5...............",
          "5...4...3...1...", "2...3...4...5...", "6...7...+1...+2..", "+1..............",
          "+3...+2...+1...7..", "6...7...+1...+2..", "+3...+2...+1...+2..", "+3..............",
          "+4...+3...+2...+1..", "7...+1...+2...+3..", "+2...+1...7...+1..", "7...............",
          ], drums="rock", lead="square"),
    # ボス「化かし化かされ夜祭り」ほか（1〜5 面共通）
    Song("boss", 165, "C", "minor",
         rep(["Cm", "Ab", "Bb", "G"], 2) + ["Ab", "Bb", "Cm", "Cm", "Fm", "Ab", "G", "G"],
         ["1.3.5.+1.7.5.3.5.", "6.5.4.3.4.5.6...", "7.5.2.5.7.+1.+2.7.", "5...............",
          "1.3.5.+1.7.5.3.5.", "6.5.4.3.4.5.6...", "7.6.5.4.3.2.1.-7.", "1...............",
          "+1...+3...+2...+1..", "7...+1...+2...7...", "+1...+3...+5...+3..", "+1..............",
          "+4...+3...+1...6...", "+1...+3...+4...+3..", "+2...7...+2...+4..", "#+2..............",
          ], drums="boss", lead="pulse25"),
    # ラスボス「明けない夜の翠灯」
    Song("final", 175, "E", "harm",
         rep(["Em", "C", "D", "B7"], 2) + ["C", "D", "Em", "Em", "Am", "C", "B7", "B7"],
         ["1.5.+1.5.3.5.+1.5.", "6.5.3.1.3.5.6...", "7.+2.7.5.7.+2.+4.+2.", "+1.7.6.5.7.......",
          "1.5.+1.5.3.5.+1.5.", "6.5.3.1.3.5.6...", "7.6.5.4.5.6.7.+1.", "+1..............",
          "+3...+2...+1...7...", "+1...+2...+3...+4..", "+5...+4...+3...+2..", "+3..............",
          "+1...+3...+4...+5..", "+6...+5...+4...+3..", "+2...+4...+3...+2..", "+2..............",
          ], drums="boss", lead="saw"),
    # エンディング
    Song("ending", 96, "F", "major",
         ["F", "C", "Dm", "Bb", "F", "C", "Bb", "C",
          "Dm", "Am", "Bb", "F", "Gm", "C", "F", "F"],
         ["3...5...6...5...", "5...3...2.......", "3...5...6...+1..", "+1...7...6.......",
          "3...5...6...5...", "5...3...2...3...", "4...3...2...1...", "2...............",
          "6...5...6...+1..", "7...6...5.......", "4...5...6...5...", "3...............",
          "4...5...6...+1..", "+2...+1...7...+2..", "+1..............", "+1..............",
          ], drums="calm", lead="tri", arp=True),
]


# ---------------------------------------------------------------------------
# 効果音
# ---------------------------------------------------------------------------
def se_tone_sweep(f0, f1, dur, kind="square", vol=0.6, sr=SE_SR):
    n = int(dur * sr)
    t = np.arange(n) / sr
    f = f0 * (f1 / f0) ** (t / dur)
    ph = np.cumsum(f) / sr
    if kind == "square":
        x = np.where(ph % 1 < 0.5, 1.0, -1.0)
    elif kind == "tri":
        x = 4 * np.abs(ph % 1 - 0.5) - 1
    else:
        x = np.sin(2 * np.pi * ph)
    return x * np.linspace(1, 0, n) * vol


def se_noise(dur, decay, vol=0.7, sr=SE_SR, lp=0.5):
    rng = np.random.default_rng(3)
    n = int(dur * sr)
    t = np.arange(n) / sr
    x = rng.uniform(-1, 1, n)
    y = np.zeros(n)
    acc = 0.0
    for i in range(n):
        acc = acc + lp * (x[i] - acc)
        y[i] = acc
    return y * np.exp(-t * decay) * vol


def make_se():
    se = {}
    se["kill"] = se_noise(0.12, 30, 0.6, lp=0.6) + se_tone_sweep(600, 200, 0.12, "square", 0.25)
    se["death"] = np.concatenate([se_tone_sweep(1400, 200, 0.18, "square", 0.5),
                                  se_tone_sweep(900, 100, 0.25, "tri", 0.5)])
    se["bomb"] = se_noise(0.6, 5, 0.7, lp=0.25) + se_tone_sweep(120, 40, 0.6, "sine", 0.6)
    se["spell"] = np.concatenate([se_tone_sweep(400, 1600, 0.25, "square", 0.35),
                                  se_tone_sweep(1600, 1600, 0.15, "tri", 0.3)])
    se["bossdie"] = se_noise(0.9, 4, 0.8, lp=0.35) + se_tone_sweep(200, 30, 0.9, "sine", 0.6)
    notes = [523, 659, 784, 1047]
    se["extend"] = np.concatenate([se_tone_sweep(f, f, 0.08, "square", 0.35) for f in notes])
    se["power"] = np.concatenate([se_tone_sweep(f, f, 0.05, "tri", 0.5) for f in (660, 880, 1320)])
    se["select"] = se_tone_sweep(1200, 1200, 0.04, "square", 0.25)
    return se


def main() -> int:
    lens = {}
    for song in SONGS:
        data = song.render()
        write_wav(BGM_DIR / f"{song.name}.wav", data, SR)
        ms = int(len(data) / SR * 1000)
        lens[song.name] = ms
        print(f"bgm/{song.name}.wav  {ms / 1000:.1f}s  {len(data) * 2 // 1024}KB")
    with (BGM_DIR / "bgm_len.lua").open("w", encoding="utf-8") as f:
        f.write("-- generate_audio.py が自動生成（曲の長さ ms）\nreturn {\n")
        for k, v in lens.items():
            f.write(f"  {k} = {v},\n")
        f.write("}\n")
    for name, data in make_se().items():
        write_wav(SE_DIR / f"{name}.wav", data, SE_SR)
        print(f"se/{name}.wav  {len(data) * 2} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
