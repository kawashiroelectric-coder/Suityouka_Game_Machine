# ゲーム機プロジェクト

Raspberry Pi Pico 2Wを使用したゲーム機風デバイスのソフトウェアです。

## 機能

- **ST7789液晶ディスプレイ**: 240x320ピクセルのRGB565表示
- **ボタン入力**: I2C接続のIOエキスパンダ（PCA9539）経由で6個のボタン入力
- **音声出力**: PWMを使用したステレオスピーカ出力
- **SDカード**: SPI経由でSDカードからゲーム・画像・音声を読み込み

## ハードウェア構成

### ピン配置

ピン配置は `config.hpp` で定義されています。後から変更可能です。

#### I2C (IOエキスパンダ)
- SDA: GPIO 6
- SCL: GPIO 7
- INT: GPIO 5
- RST: GPIO 4

#### SDカード (SPI1)
- CLK: GPIO 10
- MOSI: GPIO 11
- MISO: GPIO 12
- CS: GPIO 13

#### LCD (ST7789, SPI0)
- CS: GPIO 1
- SCK: GPIO 2
- MOSI: GPIO 3
- RST: GPIO 8
- DC: GPIO 9
- BLK: GPIO 14

#### 音声出力
- L_OUT: GPIO 21 (PWM)
- R_OUT: GPIO 20 (PWM)
- AUDIO_SD: GPIO 22
- ABD: GPIO 26

## SDカードの構成

SDカードには以下のディレクトリ構造が必要です：

```
/
├── games/          # ゲームプログラム
│   └── [ゲーム名]/
│       ├── game.bin    # ゲームプログラム（バイナリ）
│       ├── icon.bmp    # ゲームアイコン（RGB565形式、240x320）
│       └── bgm.wav     # 背景音楽（WAV形式、16bitステレオ）
├── images/         # 画像ファイル
└── audio/          # 音声ファイル
```

## ビルド方法

1. Raspberry Pi Pico SDKをインストール
2. CMakeでビルド:
```bash
mkdir build
cd build
cmake ..
make
```

3. 生成された `.uf2` ファイルをPicoに書き込み

## 使用方法

1. SDカードをフォーマット（FAT32）
2. 上記のディレクトリ構造を作成
3. ゲームファイルを配置
4. Picoに書き込んで起動
5. ゲーム選択画面でUP/DOWNで選択、OP_RIGHTで開始

## ファイル構成

- `config.hpp`: ピン配置とハードウェア設定
- `button_input.hpp/cpp`: ボタン入力管理
- `audio_output.hpp/cpp`: 音声出力管理
- `game_loader.hpp/cpp`: SDカードからのゲーム・画像・音声読み込み
- `game_machine_main.cpp`: メインループ（ゲーム機としての動作）
- `st7789_lcd.hpp/cpp`: LCD表示制御
- `hw_config.c`: SDカードハードウェア設定

## 注意事項

- 画像ファイルは現在RGB565形式の生データのみ対応（将来的にBMP/PNG対応予定）
- 音声ファイルはWAV形式（16bitステレオ）のみ対応
- ゲームプログラムの実行は簡易実装のため、実際のゲームロジックは別途実装が必要

## ライセンス

プロジェクト固有のライセンスに従います。
