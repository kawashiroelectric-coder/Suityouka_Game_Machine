# Third-Party Notices

本リポジトリ **Suityouka Game Machine** の **オリジナルソースコード**（`game_machine_main.cpp`、`lib/` 内の自前実装、`Game/` 内のサンプル Lua 等）は [LICENSE](LICENSE)（**MIT License**）に従います。

ファームウェアのビルド・配布には、以下のサードパーティソフトウェアが含まれます。  
各コンポーネントは **それぞれのライセンス** に従い、本ファイルはその帰属表示（attribution）をまとめたものです。

> **注意:** 本ファイルは法的助言ではありません。製品化・再配布の際は各ライセンス全文を確認してください。

---

## サマリー

| コンポーネント | 用途 | ライセンス | 同梱場所 / 取得元 |
|----------------|------|------------|-------------------|
| Raspberry Pi Pico SDK | MCU・ドライバ・ビルド基盤 | BSD-3-Clause | [pico-sdk](https://github.com/raspberrypi/pico-sdk)（ビルド時） |
| Lua 5.4 | ゲームスクリプト実行 | MIT | CMake FetchContent（`CMakeLists.txt`） |
| no-OS-FatFS-SD-SDIO-SPI-RPi-Pico | SD カード / FatFS | Apache-2.0 | `lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/` |
| FatFs | ファイルシステム（上記に同梱） | BSD-style（ChaN） | `lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/ff15/` |

---

## 1. Raspberry Pi Pico SDK

- **Copyright:** Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
- **License:** BSD 3-Clause License
- **URL:** https://github.com/raspberrypi/pico-sdk

```
Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this
   list of conditions and the following disclaimer in the documentation and/or
   other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## 2. Lua

- **Copyright:** Copyright (c) 1994–2024 Lua.org, PUC-Rio
- **License:** MIT License
- **Version:** 5.4.6（`CMakeLists.txt` の `GIT_TAG v5.4.6`）
- **URL:** https://www.lua.org/

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 3. no-OS-FatFS-SD-SDIO-SPI-RPi-Pico

- **Copyright:** Carl Kunde and contributors（`lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/LICENSE` を参照）
- **License:** Apache License 2.0
- **URL:** https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
- **Upstream commit (base):** `d5e453404cdbfaa55ab30d285b6ab0b730e84a05`

Apache License 2.0 の全文は同梱ファイルを参照してください:

- `lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/LICENSE`

または https://www.apache.org/licenses/LICENSE-2.0

### 改変について（Apache 2.0 明示）

本プロジェクトは upstream を **改変して** 利用しています。  
**詳細な変更一覧:** [`lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/MODIFICATIONS.md`](lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/MODIFICATIONS.md)

| 改変ファイル | 変更の概要 |
|--------------|------------|
| `src/CMakeLists.txt` | `include` ディレクトリを先に参照するよう include パス順を変更 |
| `src/ff15/source/ffconf.h` | テンプレート `!ffconf.h` を有効化。LFN(UTF-8)・LBA64・exFAT 等を設定 |
| `src/src/glue.c` | FatFs `LBA_t` と SD ドライバ `uint32_t` セクタの変換、ioctl 調整 |
| `src/sd_driver/SPI/sd_card_spi.c` | SDSC/SDHC アドレス変換、SPI 初期化順序・SDSC 対応 |
| `src/sd_driver/sd_regs.h` | CSD からのセクタ数計算の 64bit 化と上限クランプ |

改変ファイルの先頭には、Apache 2.0 に沿った **変更通知コメント** を付与しています。

**プロジェクト側の連携コード（ライブラリ本体の改変ではない）:**

| ファイル | 役割 |
|----------|------|
| `lib/sd_card_hw/hw_config.c` | SPI1 ピン・CS 設定（MIT、本リポジトリ） |
| `lib/sd_card_hw/sd_debug.c` | SD 診断（公開 API のみ使用） |

**Apache 2.0 配布時の要点（要約）:**

- ライセンス全文および著作権表示を保持すること
- 改変したファイルには変更通知を付けること（上記 MODIFICATIONS.md およびソース内コメント）
- 「Special Thanks」等の感謝文のみでは代替できない

---

## 4. FatFs（no-OS-FatFS 内に同梱）

- **Copyright:** Copyright (C) ChaN
- **License:** FatFs License（BSD-style。バイナリのみの再配布時はドキュメントへの記載不要、等の条件あり）
- **Path:** `lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/ff15/`
- **License file:** `lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src/ff15/LICENSE.txt`

FatFs はファームウェア内で SD カード上の FAT ファイルシステムアクセスに使用されます。

---

## 5. オプション: SD カード上のサンプルアセット（ファームウェア外）

以下は **SD カードに配置するサンプル** に含まれる場合があります。ファームウェア `.uf2` 本体には通常バンドルされませんが、再配布時は各ライセンスに従ってください。

| アセット | 用途 | ライセンス |
|----------|------|------------|
| PixelMplus12-Regular | サンプル VN 等のフォント元 | M+ FONT LICENSE（`Game/visual_novel/fonts/`） |
| 美咲ゴシック第 2（misaki） | フォント生成の代替元 | 各配布元のライセンスに従う |
| サンプル画像・BGM | 各 `Game/` サンプル | プロジェクト内 README を参照 |

---

## 6. 実行時表示（システム設定メニュー → About）

ファームウェアの **System Settings Menu** から **About / Code Ver** を選び、**NEAR** で About 画面を開けます（**FAR** で設定に戻る）。  
表示文字列は `lib/system_settings_menu/system_settings_menu.cpp` の **`kAboutLines`** で編集します。

- 実行時の数行表示は **補助的なクレジット** です
- **本ファイル（THIRD_PARTY_NOTICES.md）および各 LICENSE ファイルの代替にはなりません**
- 画面幅（320px・8×8 フォント）の都合上、短い表記を推奨します

例:

```cpp
constexpr const char* kAboutLines[] = {
    "Code Ver 1.0.0",
    "SD: Apache2.0 carlk3",
};
```

---

## 更新履歴

| 日付 | 内容 |
|------|------|
| 2026-07-04 | 初版（MIT + サードパーティ一覧） |
| 2026-07-04 | no-OS-FatFS 改変一覧を追記（MODIFICATIONS.md） |
