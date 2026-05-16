# PNG to Header ツール

このツールは PNG 画像を ST7789 LCD 向けの RGB565 C ヘッダーファイルに変換します。

## 要件

- Python 3
- Pillow (`pip install pillow`)

## 使い方

プロジェクトルートから実行します。

```bash
python tool/png_to_header.py assets/MyImage.png
```

既定では、生成された `.h` ファイルは `assets/` に出力されます。

### オプション

- `-d`, `--output-dir` : 生成される `.h` ファイルの出力先ディレクトリ（デフォルト: `assets`）
- `-n`, `--name` : 生成ヘッダー内で使う変数名のベース
- `--comment` : 生成ヘッダーの先頭に追加するコメント

## 例

```bash
python tool/png_to_header.py assets/icon.png -d assets
```
