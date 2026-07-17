# save_test — セーブ／ロード API テスト

`machine.save_data` / `machine.load_data` / `machine.file_exists` の動作確認用ミニゲームです。

コイン収集・セーブ位置ゴースト・スロット A/B・`version` マイグレーションを含み、実ゲーム向けのセーブパターンを一通り触れます。

## SD 配置

```
/save_test/save_test.lua
```

起動後、SD ブラウザで `save_test` フォルダに入り `save_test.lua` を実行してください。

セーブファイルは同フォルダに作成されます。

| スロット | ファイル |
|---------|----------|
| A | `save_a.dat` |
| B | `save_b.dat` |

（旧 `save.dat` は読みません。必要なら手動でコピー／削除してください。）

## 操作

| 入力 | 動作 |
|------|------|
| 十字キー | 緑の四角を移動 |
| 接触 | 黄色いコインを取得（在庫・スコア加算） |
| FAR | セーブスロット A ↔ B 切替 |
| OP_LEFT | 現在スロットへセーブ |
| OP_RIGHT | 現在スロットからロード |

起動時はスロット A を優先し、無ければ B を自動ロードします。

セーブ／ロード後、最後に記録した位置に暗い緑のゴーストが点滅表示されます（チェックポイント可視化）。

## セーブ内容（version 2）

```lua
{
  version = 2,
  score = ...,
  x = ...,
  y = ...,
  save_count = ...,
  inventory = { coins = ... },
  coins_taken = { [1] = true, ... },  -- 取得済みコイン id
}
```

`version` が無い／1 の旧形式もロード時に補完して v2 扱いにします。

## 関連ドキュメント

- [LUA_API.md](../../LUA_API.md) — `machine.save_data` 等の API 詳細
