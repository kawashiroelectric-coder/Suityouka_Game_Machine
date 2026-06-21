# 画像変換ツール (tool/)

ゲームマシン向け RGB565 画像の変換スクリプト群です。  
いずれも **プロジェクトルート** から実行してください。

## 要件

- Python 3
- Pillow: `pip install pillow`

## ファイル構成

| ファイル | 用途 |
|----------|------|
| `rgb565_codec.py` | 共通変換ロジック（直接実行しない） |
| `image_to_rgb565_bin.py` | 画像 → `.bin`（SD / Lua 用・**主に使うツール**） |
| `rgb565_bin_to_png.py` | `.bin` → PNG（プレビュー・デバッグ用） |
| `png_to_c_header.py` | PNG → C ヘッダー `.h`（ファームウェア埋め込み用） |
| `wav_to_pcm_header.py` | WAV → PCM C ヘッダー `.h`（ファームウェア音声埋め込み用） |
| `lua_preview/` | **対話型 Lua プレビュー**（320×240 / pygame）。詳細は `lua_preview/README.md` |

### フォーマット

- 色: RGB565 リトルエンディアン（`uint16_t` 1 ピクセル = 2 バイト）
- 並び: 左上から右へ、行ごとに下へ
- Lua からは `machine.load_image()` / `draw_bg_stream()` 等で読み込み

---

## image_to_rgb565_bin.py

PNG / JPG / BMP 等を `.bin` に変換します。

```bash
# 同じ名前で .bin を出力
python tool/image_to_rgb565_bin.py sprite.png

# 出力先を指定
python tool/image_to_rgb565_bin.py sprite.png -o Test_Lua/stg/img/player.bin

# リサイズして変換（メニュー用プレビューは 100x100 など）
python tool/image_to_rgb565_bin.py cover.png --resize 100x100 -o games/mygame/cover.bin

# 複数ファイルを一括変換
python tool/image_to_rgb565_bin.py Test_Lua/stg/img/*.png -d Test_Lua/stg/img/
```

### よく使うサイズ

| 用途 | 推奨サイズ |
|------|------------|
| ゲーム選択メニューのプレビュー | 100×100 |
| ビジュアルノベル背景 | 320×168 |
| 立ち絵 | 128×168 など |
| タイルセット | 128×128（16×16 タイル × 8 列など） |
| フルスクリーン | 320×240 |

---

## rgb565_bin_to_png.py

`.bin` を PNG に戻して内容を確認します。  
**幅・高さは変換時と同じ値を `--size` で指定**してください。

```bash
# タイルセットのプレビュー
python tool/rgb565_bin_to_png.py Test_Lua/tile_test/tiles/tiles.bin --size 128x128

# ビジュアルノベル背景を一括プレビュー
python tool/rgb565_bin_to_png.py Test_Lua/visual_novel/images/bg/*.bin --size 320x168
```

---

## png_to_c_header.py

PNG を C の `uint16_t` 配列ヘッダーに変換します（ファームウェア組み込み向け）。  
Lua ゲーム用の SD カードアセットには `image_to_rgb565_bin.py` を使ってください。

```bash
python tool/png_to_c_header.py assets/icon.png
python tool/png_to_c_header.py assets/icon.png -d assets -n my_icon
```

### オプション

- `-d`, `--output-dir` : 出力ディレクトリ（既定: `assets`）
- `-n`, `--name` : ヘッダー内の変数名ベース
- `--comment` : ヘッダー先頭コメント

---

## wav_to_pcm_header.py

16bit PCM WAV を C の `int16_t` 配列ヘッダーに変換します（flash 埋め込み・C++ 再生用）。  
Lua の SD 用 WAV とは別経路です。

```bash
# 短い SE（既定で 44100Hz にリサンプル、32KB 以下）
python tool/wav_to_pcm_header.py click.wav -d assets -n click

# 長い BGM を flash 埋め込み（サイズ警告のみ）
python tool/wav_to_pcm_header.py title.wav --bgm -d assets -n title_bgm

# 元のサンプルレートを維持
python tool/wav_to_pcm_header.py raw.wav --keep-rate -d assets
```

### 生成ヘッダーの例

```c
static const uint32_t click_sample_rate = 22050;
static const uint16_t click_channels = 1;
static const uint32_t click_frame_count = 2205;
static const int16_t click_pcm[2205] = { ... };
```

### C++ からの再生

```cpp
#include "click.h"

LuaAudio& audio = g_luaInterpreter.audioEngine();

// 短い SE（32KB 以下）
audio.playSeFromEmbedded(click_pcm, click_frame_count, click_channels, click_sample_rate);

// 長い BGM（ストリーム再生）
audio.playBgmFromEmbedded(title_bgm_pcm, title_bgm_frame_count,
                          title_bgm_channels, title_bgm_sample_rate);
```

BGM 再生中はメニュー等で `audio.pumpStream()` を定期的に呼んでください。

### オプション

- `-d`, `--output-dir` : 出力ディレクトリ（既定: `assets`）
- `-n`, `--name` : 変数名ベース
- `--rate` : 出力サンプルレート（既定: 44100）
- `--keep-rate` : WAV の元レートを維持（リサンプルしない）
- `--max-kb` : SE 向けサイズ上限 KB（既定: 32）
- `--bgm` : BGM 埋め込み向け（上限緩和）
