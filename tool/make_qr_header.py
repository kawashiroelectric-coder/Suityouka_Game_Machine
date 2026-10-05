#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
URL の QR コードを C++ ヘッダ（ビット配列）にする。本体の System Menu の「GitHub QR」ページ用。

  pip install segno
  python tool/make_qr_header.py
  python tool/make_qr_header.py --url https://example.com/ --out lib/system_settings_menu/github_qr.hpp

本体側には QR の生成処理を入れず、ここで作ったモジュール（白黒のマス）をそのまま描く。
"""

from __future__ import annotations

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_URL = "https://github.com/kawashiroelectric-coder/Suityouka_Game_Machine"
DEFAULT_OUT = ROOT / "lib" / "system_settings_menu" / "github_qr.hpp"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default=DEFAULT_URL)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--error", default="m", help="誤り訂正レベル l/m/q/h（既定 m）")
    args = ap.parse_args()

    import segno

    qr = segno.make_qr(args.url, error=args.error, boost_error=False)
    rows = [list(r) for r in qr.matrix]   # 1 = 黒
    n = len(rows)
    row_bytes = (n + 7) // 8
    lines = []
    for r in rows:
        bs = []
        for i in range(row_bytes):
            v = 0
            for b in range(8):
                x = i * 8 + b
                if x < n and r[x]:
                    v |= 0x80 >> b
            bs.append(f"0x{v:02X}")
        lines.append("    " + ", ".join(bs) + ",")

    text = f"""// ============================================
// ファイル: github_qr.hpp（tool/make_qr_header.py で自動生成。手で編集しない）
// URL: {args.url}
// QR バージョン {qr.version} / 誤り訂正 {qr.error.upper()} / {n}x{n} モジュール（クワイエットゾーン含まず）
// ============================================

#ifndef GITHUB_QR_HPP
#define GITHUB_QR_HPP

#include <cstdint>

namespace GithubQr {{

constexpr const char* kUrl = "{args.url}";
constexpr int kSize = {n};
constexpr int kRowBytes = {row_bytes};

/** 1 行 kRowBytes バイト、MSB が左。ビット 1 = 黒モジュール */
constexpr uint8_t kModules[kSize * kRowBytes] = {{
{chr(10).join(lines)}
}};

/** (x, y) のモジュールが黒なら true */
constexpr bool isDark(int x, int y) {{
    return (kModules[y * kRowBytes + (x >> 3)] & (0x80 >> (x & 7))) != 0;
}}

}}  // namespace GithubQr

#endif  // GITHUB_QR_HPP
"""
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))
    print(f"{args.out}: version {qr.version}-{qr.error.upper()} {n}x{n}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
