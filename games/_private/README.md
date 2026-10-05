# games/_private — 作成中ゲーム（非公開）

このフォルダの中身は `.gitignore` で除外されていて、GitHub には上がりません（この README だけ公開されます）。
完成して公開するときは、ゲームのフォルダを `games/` の直下へ移動してください。

## 置き方

```
games/_private/
└── MyGame/
    ├── MyGame.lua      … フォルダ名と同じ名前の Lua が起動スクリプト
    ├── img/
    └── ...
```

## 試し方

- PC: `python tool/lua_preview/preview.py games/_private/MyGame/MyGame.lua`
- 実機: SD カードの `/games/` 直下に **`MyGame` フォルダごと** コピーします。
  本体のゲーム一覧は `/games/` の直下しか見ないので、`/games/_private/` のまま入れてもメニューには出ません。

## 注意

- 非公開にしたいファイルが、すでに一度コミットされている場合は `.gitignore` に書くだけでは消えません。
  `git rm -r --cached games/_private/MyGame` で追跡をやめてからコミットしてください（PC 上のファイルは残ります）。
- GitHub に上がらないので、バックアップは別に取ってください（OneDrive 上にあれば OneDrive には残ります）。
