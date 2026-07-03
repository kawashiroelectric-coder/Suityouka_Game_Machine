# save_test — セーブ／ロード API テスト

`machine.save_data` / `machine.load_data` / `machine.file_exists` の動作確認用ミニゲームです。

## SD 配置

```
/save_test/save_test.lua
```

起動後、SD ブラウザで `save_test` フォルダに入り `save_test.lua` を実行してください。

セーブファイルは同フォルダに `save.dat` として作成されます（初回セーブ後）。

## 操作

| 入力 | 動作 |
|------|------|
| 十字キー | 緑の四角を移動 |
| OP_LEFT | 現在のスコア・位置をセーブ |
| OP_RIGHT | セーブデータをロード |

起動時に `save.dat` があれば自動ロードします。

## セーブ内容

```lua
{
  score = ...,
  x = ...,
  y = ...,
  save_count = ...,
}
```

## 関連ドキュメント

- [LUA_API.md](../../LUA_API.md) — `machine.save_data` 等の API 詳細
