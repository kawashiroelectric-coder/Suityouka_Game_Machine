# ゲーム機プロジェクト

Raspberry Pi Pico 2W を使用した携帯ゲーム機風デバイスのファームウェアです。  
SD カード上の **Lua スクリプト**をファイルエクスプローラから選んで実行します。

## 機能

- **ST7789 液晶**: 320×240 ピクセル、RGB565、ラインバッファ（バンド）描画 + DMA 転送
- **ボタン入力**: I2C IO エキスパンダ（PCA9539）経由で 8 ボタン
- **ロータリーエンコーダ**: クアッドエンコーダ入力 + **15 段階マスター音量**（回転で調整、左上に一時表示）
- **音声出力**: PCM5102 **I2S**（Core 1 + DMA）、BGM ストリーミング + SE 最大 8 系統
- **SD カード**: SPI 経由 FatFS、Lua スクリプト・画像（`.bin`）・WAV の読み込み
- **ファイルエクスプローラ**: 3×2 グリッド GUI（フォルダ / `.lua` / その他をアイコン表示）
- **Lua 5.4**: `game_init` / `game_update` / `game_draw` ループ、タイルレイヤー、UTF-8 フォント等
- **バッテリー監視**: ADC + IO エキスパンダ経由 LED 表示（Core 1）

## 起動フロー

1. ハードウェア初期化（I2C / SD / LCD / 音声 / エンコーダ）
2. SD 未マウント時 → **入力テスト画面**（ボタン・エンコーダ確認、SD 挿入待ち）
3. SD マウント後 → **ファイルエクスプローラ**
4. `.lua` を選択 → Lua ゲーム実行（終了後エクスプローラへ戻る）
5. SD 抜去検知 → 再び入力テスト画面へ

## ファイルエクスプローラ操作

| 操作 | ボタン |
|------|--------|
| カーソル移動 | UP / DOWN / LEFT / RIGHT（長押しリピートあり） |
| 決定（フォルダ进入 / `.lua` 実行） | OP_RIGHT または NEAR |
| 親フォルダへ戻る | OP_LEFT |
| 音量調整 | エンコーダ回転（全画面共通） |

## ハードウェア構成

ピン配置は **`config.hpp`** の `CFG_*` マクロで定義しています。ボード変更時はここを編集してください。

### I2C（IO エキスパンダ PCA9539）

| 信号 | GPIO |
|------|------|
| SDA | 6 |
| SCL | 7 |
| INT | 8 |
| RST | 9 |

### SD カード（SPI1）

| 信号 | GPIO |
|------|------|
| CLK | 10 |
| MOSI | 11 |
| MISO | 12 |
| CS | 13 |
| POWER | 15 |

### LCD（ST7789, SPI0）

| 信号 | GPIO |
|------|------|
| CS | 1 |
| SCK | 2 |
| MOSI | 3 |
| RST | 4 |
| DC | 5 |
| BLK | 14 |

論理画面サイズ: **320×240**（`GameConfig::SCREEN_WIDTH` / `SCREEN_HEIGHT`）

### ロータリーエンコーダ

| 信号 | GPIO |
|------|------|
| A | 17 |
| B | 16 |
| SW | 26 |

### 音声（PCM5102 I2S）

| 信号 | GPIO |
|------|------|
| BCK | 21 |
| LRCK | 20 |
| DATA | 19 |
| MUTE | 18 |
| SPMUTE | 22 |

サンプルレート: 22050 Hz（`AudioConfig::SAMPLE_RATE`）

### バッテリー ADC

| 信号 | GPIO |
|------|------|
| ADC | 28（ADC チャンネル 2） |

## SD カードの構成

FAT32 でフォーマットし、ルートまたは任意フォルダに **`.lua` とアセット**を配置します。  
固定の `games/` ディレクトリ構成は不要です。

### 配置例

```
/
├── visual_novel/
│   ├── visual_novel.lua
│   ├── scenario.lua
│   ├── fonts/
│   └── images/
├── tile_test/
│   ├── layers_test.lua
│   └── tiles/
│       └── *.bin
└── （その他 .lua / フォルダ）
```

サンプル詳細: [Test_Lua/README.md](Test_Lua/README.md)

### パス解決

実行中の `.lua` があるフォルダを基準に、相対パスが解決されます。

- Lua API: `machine.script_dir()` / `machine.resolve_path("assets/foo.bin")`
- 実装: `lib/sd_path_util/sd_path_util.hpp`

## ビルド方法

1. [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) をインストール（Pico VS Code 拡張でも可）
2. プロジェクトルートで CMake ビルド:

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

3. 生成された `build/Suityouka_Game_Machine.uf2` を BOOTSEL モードの Pico に書き込み

### Windows（キャッシュクリア付き）

```powershell
Remove-Item -Path "build/CMakeCache.txt" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "build/CMakeCache-*.txt" -Force -ErrorAction SilentlyContinue
Remove-Item -Path "build/CMakeFiles" -Recurse -Force -ErrorAction SilentlyContinue
cd build
& "C:\Users\adsad\.pico-sdk\cmake\v3.31.5\bin\cmake.exe" ..
& "C:\Users\adsad\.pico-sdk\cmake\v3.31.5\bin\cmake.exe" --build .
```

## 使用方法

1. SD カードを FAT32 でフォーマット
2. Lua ゲームとアセットをコピー（`Test_Lua/` 内のサンプルを SD ルートに合わせて配置）
3. ファームウェアを Pico 2W に書き込み、USB シリアルでログ確認（任意）
4. SD 挿入後、ファイルエクスプローラで `.lua` を選択して起動

## ソフトウェア構成

| パス | 説明 |
|------|------|
| `game_machine_main.cpp` | 起動・初期化・メインループ |
| `config.hpp` | ピン配置・画面サイズ・ヒープ予算等 |
| `lib/ST7789/` | ST7789 LCD ドライバ |
| `lib/button_input/` | I2C ボタン入力 |
| `lib/encoder_input/` | ロータリーエンコーダ |
| `lib/encoder_volume/` | エンコーダ音量（15 段階） |
| `lib/input_test_mode/` | SD 未挿時の入力テスト画面 |
| `lib/file_explorer/` | GUI ファイルエクスプローラ |
| `lib/sd_card_hw/` | SD ピン定義・`hw_config`・デバッグ |
| `lib/sd_service/` | FatFS マウント / アンマウント |
| `lib/sd_path_util/` | SD パス正規化（ヘッダのみ） |
| `lib/lua_interpreter/` | Lua 実行・`machine.*` API |
| `lib/lua_interpreter/lua_audio.cpp` | BGM / SE ミキシング |
| `lib/game_display/` | RGB565 フレームバッファ・描画 |
| `lib/font_renderer/` | UTF-8 サブセットフォント（MISF） |
| `lib/tile_layers/` | GBA 風タイル背景レイヤー |
| `lib/audio_output/` | 音声出力管理 |
| `lib/pcm5102_i2s/` | PCM5102 I2S PIO ドライバ |
| `lib/battery_monitor/` | バッテリー ADC（Core 1） |
| `lib/heap_budget/` | 動的ヒープ予算管理 |
| `lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/` | FatFS / SD ドライバ（サードパーティ） |
| `Test_Lua/` | Lua サンプルゲーム |
| `LUA_API.md` | Lua `machine.*` API リファレンス |

## 開発向けメモ

### 画像・音声フォーマット

- **画像**: RGB565 リトルエンディアン生データ（`.bin`）。Lua 側で幅・高さを指定して `machine.load_image` 等
- **背景ストリーム**: 大きな背景は `machine.draw_bg_stream` で SD から帯ごと読み込み（全枚 RAM 載せ不要）
- **音声**: WAV（PCM 16bit）。BGM は `machine.play_wav`、SE は `machine.play_se`

### デバッグ

- USB シリアル（stdio）で `printf` / Lua の `print` 出力
- SD 初期化失敗時は `[SD DBG]` ログを確認（`sd_debug_run_diagnostics`）
- Debug ビルド時は画面右上に FPS 表示（`GAME_MACHINE_DEBUG`）

### Lua API

ゲームスクリプトから使える API の一覧は [LUA_API.md](LUA_API.md) を参照してください。

## ライセンス

プロジェクト固有のライセンスに従います。  
`lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico` および FetchContent で取得する Lua 本体は、それぞれ同梱のライセンスに従います。
