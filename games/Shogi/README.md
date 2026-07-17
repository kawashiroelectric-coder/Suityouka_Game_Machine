# 将棋 vs もみじ

もみじ（Momiji）AI と対局する将棋です。難易度は3段階です。

## SD 配置

```
/games/Shogi/
├── Shogi.lua
├── Shogi.bin                 # 任意（メニュー用 100×100）
├── generate_font.py
├── fonts/
│   ├── game_font.bin
│   └── LICENSE.txt
├── img/
│   ├── Momiji_thinking.bin / .png
│   ├── Momiji_idea.bin / .png
│   ├── Momiji_despair.bin / .png
│   ├── Koma.bin / Koma.png
│   ├── Shogi_BGs.bin         # 160×120 背景（load_image → 2 倍表示）
│   └── Shogi_BG.bin / .png   # 320×240（未使用・互換用）
├── sound/
│   ├── 将棋の駒を打つ.mp3    # 元データ
│   └── piece_drop.wav        # **必須**（16bit PCM・駒を置いたときの SE）
```

> `machine.play_se` は **WAV のみ** 対応です。mp3 だけではブザー音にフォールバックします。SD の `/games/Shogi/sound/piece_drop.wav` を置いてください。

## フォント

```bash
cd games/Shogi
python generate_font.py
```

## 画像

Momiji PNG を差し替えたら:

```bash
python ../../tool/image_to_rgb565_bin.py img/Momiji_thinking.png -o img/Momiji_thinking.bin
python ../../tool/image_to_rgb565_bin.py img/Momiji_idea.png -o img/Momiji_idea.bin
python ../../tool/image_to_rgb565_bin.py img/Momiji_despair.png -o img/Momiji_despair.bin
python ../../tool/image_to_rgb565_bin.py img/Koma.png -o img/Koma.bin
python ../../tool/image_to_rgb565_bin.py img/Shogi_BGs.png -o img/Shogi_BGs.bin
# （任意）フル解像度: python ../../tool/image_to_rgb565_bin.py img/Shogi_BG.png -o img/Shogi_BG.bin
```

背景は `Shogi_BGs.bin`（160×120）を `load_image` し、2 倍表示します。

## 操作

| 場面 | 操作 |
|------|------|
| タイトル | 上下で難易度／つづきからを選択、NEAR で決定、OP_LEFT で終了 |
| タイトル | 記録ありのとき末尾に **つづきから [難易度]** が追加される |
| 対局 | 十字でカーソル、NEAR で駒選択／移動／打ち |
| 対局 | FAR で持ち駒巡回 → 空きマスに NEAR で打つ（打ち SE） |
| 対局 | **OP_RIGHT で盤面記録**（自分の番のみ）、OP_LEFT でタイトルへ |
| 結果 | NEAR でもう一度、OP_LEFT でタイトル |

記録は `save.dat` に保存されます。対局終了や新規開始で無効化されます。

## 難易度

| レベル | AI |
|--------|-----|
| やさしい | 浅い探索＋ランダム寄り |
| ふつう | 中程度 |
| つよい | より深い探索 |

## 備考

- SE は `machine.play_se` 向け 16bit PCM WAV（`piece_drop.wav`）
- API は [LUA_API.md](../../LUA_API.md) に準拠
