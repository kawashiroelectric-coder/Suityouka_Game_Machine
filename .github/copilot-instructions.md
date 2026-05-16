<!--
リポジトリ固有の AI エージェント向け簡潔ガイド。
20〜50行に収めて要点だけ記載してください。
-->
# Copilot / AI エージェント用指示

目的: Raspberry Pi Pico（RP2040）向けファームウェアリポジトリで、AI が素早く作業開始できるように要点をまとめる。

- **全体像**: このプロジェクトは Pico クラスの RP2040 ボード用ファームウェアです。主要コンポーネント:
  - `main.cpp`: エントリ、初期化（I2C/ADC/DMA/LCD/SD）、アニメーションループ、パフォーマンステスト。
  - `st7789_lcd.cpp/.hpp`: ST7789 LCD ドライバ（RGB565、DMA ヘルパ、`drawRawImageDMA`、ウィンドウ設定）。
  - `sd_card.*` と `lib/src`: SD カード / FatFS 関連ロジックと埋め込みビットマップ。
  - `hw_config.c/h`: ボード固有のピン定義や設定。
  - `blink.pio` と生成済みヘッダ `build/blink.pio.h`: PIO の使用例。

- **構成方針（理由）**: SPI/I2C/DMA/PIO といった低レイヤの HAL はデバイス近傍の C/C++ コードに実装し、上位のアプリロジックは `main.cpp` に集約している。大きな画像は `sd_card.cpp` に埋め込み済み。SD/FatFS は `lib/src` を vendoring して利用している。

- **ビルド & 書き込みワークフロー（具体例）**:
  - 推奨: VS Code のタスクを使う（ワークスペースに定義済み）:
    - `Compile Project` — `build/` を対象に Ninja を実行（`.uf2` と `.dis` が生成される）。
    - `Run Project` — `picotool` を使ってバイナリを書き込み（`picotool load <path> -fx`）。
    - `Flash` — `openocd` + CMSIS-DAP で書き込み（タスク `Flash`）。
  - CLI（Windows PowerShell）例:
    ```powershell
    mkdir build; cd build
    cmake -G Ninja ..
    ninja -C .
    # 必要に応じてパスを調整して picotool で書き込み
    $picotool = "$env:USERPROFILE/.pico-sdk/picotool/2.2.0-a4/picotool/picotool.exe"
    & $picotool load "${PWD}/Suityouka_Game_Machine.uf2" -fx
    ```

- **プロジェクト固有のパターン & 規約**:
  - デバイス操作は pico SDK の `hardware_*` API を使用（例: `hardware/spi.h`, `hardware/dma.h`）。DMA チャネルの確保、`spi_get_dreq` による DREQ 設定など既存の慣習に従う。
  - C/C++ 混在: `hw_config.c` のような C ソースがあるため、C++ 側でのヘッダ参照は extern "C" を意識する。
  - グラフィックス: 画面バッファは `framebuffer[240*320]` を用い、差分更新（dirty-rect）ロジックを優先。大容量転送は `drawRawImageDMA` / DMA ヘルパを使う。
  - SD/FatFS: `lib/src` をサブディレクトリとして追加（`CMakeLists.txt` 内）。`#include "ff.h"` と `#include "sd_card.h"` で利用可能。既存の `sd_get_by_num` 等のラッパを再利用する。

- **統合ポイント / 注意点**:
  - Raspberry Pi Pico SDK を利用しているため、開発環境に `~/.pico-sdk`（`pico_sdk_import.cmake`）があることを前提とする。VS Code タスクはこの配置を期待している。
  - `.pio` を編集した場合は `build/generated/pio/...` のヘッダ再生成が必要。
  - 大きな静的リソース（ビットマップ）は `sd_card.cpp` に埋め込まれているため、変更はバイナリサイズとビルド時間に影響する。
  - `CMakeLists.txt` で `pico_enable_stdio_usb(...)` が有効になっているため、`printf` は USB 経由で出力される。

- **参照するべき箇所（実例）**:
  - DMA と LCD 転送: `main.cpp` の `initDMA`, `flushDirtyRects`, `fillScreenDMA` と `st7789_lcd.*`。
  - I2C エキスパンダ: `main.cpp` の PCA9539 ヘルパ (`pca9539_write_register`, `pca9539_read_register`)。
  - SD / FatFS: `lib/src` と `sd_card.cpp`。
  - アフィン変換: `AffineTransform2D.*` (基本行列演算)、`framebuffer_affine.hpp` (フレームバッファ特化)、`framebuffer_affine_example.cpp` (使用例)。

- **PR / 修正時の注意**:
  - 変更は小さく保ち、ハードウェアに影響するピン定義（`hw_config.*`）の変更は慎重に。
  - 既存のヘルパ（`st7789_lcd` の DMA ラッパや `lib/src` の SD ラッパ）を再利用する。

不明点や詳細化したいワークフロー（CI、テスト、シミュレータ、配線図など）があれば指示ください。追記します。
