-- ============================================================================
-- 画像アセット定義（visual_novel 用）
-- ============================================================================
--
-- scenario.lua では bg_image / character にここで定義した id を指定します。
-- 実体は images/ 以下の .bin（generate_images.py で生成、または PNG 変換）。
--
-- 背景: 320 x 168（テキストボックス上の領域）
-- 立ち絵: 128 x 168（マゼンタ 0xF81F を透過キー。draw_vn_stream / draw_image_keyed で使用）
--
return {
  bg = {
    title_night = { file = "bg/title_night.bin", w = 320, h = 168 },
    classroom   = { file = "bg/classroom.bin",   w = 320, h = 168 },
    street      = { file = "bg/street.bin",      w = 320, h = 168 },
    star_path   = { file = "bg/star_path.bin",   w = 320, h = 168 },
    home        = { file = "bg/home.bin",        w = 320, h = 168 },
    ending      = { file = "bg/ending.bin",      w = 320, h = 168 },
  },
  char = {
    hero       = { file = "chars/hero.bin",       w = 128, h = 168, keyed = true },
    mysterious = { file = "chars/mysterious.bin", w = 128, h = 168, keyed = true },
    narrator   = { file = "chars/narrator.bin",   w = 128, h = 168, keyed = true },
  },
}
