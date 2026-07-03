# Bad Apple!!（1 ビット白黒・差分ストリーム再生）

`bad-apple.mp4` を **差分圧縮した 1 ビット**フレーム列に変換し、実機で **30 FPS** 再生するサンプルです。

## 差分圧縮

Bad Apple は連続フレームが同じ、または数行だけ変わる場面が多いため、フレームごとに次のいずれかを出力します。

| 形式 | ファイルサイズ | 説明 |
|------|----------------|------|
| **SKIP** | 0 バイト | 前フレームと完全一致（SD 読み込みなし） |
| **DELTA** | `1 + 変更行数 × (1 + 40)` | 変わった行だけ送信（320×240 時） |
| **FULL** | 9,600 バイト | フルフレーム（差分より大きい場合や先頭フレーム） |

実機側は `machine.draw_bw_pack` が **frames.pack** 内のフレームを読み込み、形式を自動判別して RAM 上の現フレームに適用してからバンド描画します。**1 フレームあたり SD 読み込みは最大 1 回**、バンド描画は RAM から行います。

## フレーム形式（ビット列）

- 1 行 = `ceil(W/8)` バイト、**MSB = 左端**
- ビット `1` = 白、`0` = 黒

## 準備（PC）

```bash
cd Test_Lua/bad_apple
python convert_video.py
```

変換完了時に `skip` / `delta` / `full` の内訳と圧縮率が表示されます。出力は **frames.pack 1 ファイル**です（数千個の `.bin` を SD にコピーする必要はありません）。

古い `frames/f*.bin` を削除する場合:

```bash
python convert_video.py --clean-split
```

デバッグ用に個別 `.bin` も欲しい場合:

```bash
python convert_video.py --split-frames
```

## SD 配置

```
/games/bad_apple/
├── bad_apple.lua
├── bad_apple.bin      … ゲーム選択メニュー用プレビュー
└── frames.pack        … 全フレーム（BWPK 形式、約 60 MB）
```

## 操作

任意のボタンで終了し、ゲーム選択画面へ戻ります。
