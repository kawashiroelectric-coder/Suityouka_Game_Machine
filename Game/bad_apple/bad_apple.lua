-- ============================================================================
-- Bad Apple!!（1 ビット白黒・差分ストリーム再生）
-- Suityouka Game Machine
--
-- SD: /games/bad_apple/bad_apple.lua + frames.pack
-- convert_video.py が差分圧縮した 1 ビットフレーム列を 1 ファイルにパックする。
--
-- フレーム形式（draw_bw_pack が自動判別）:
--   0 バイト     … 前フレームと同一（SKIP）
--   FRAME_BYTES … フルフレーム
--   それ以外    … 変更行のみの差分
--
-- 操作: 任意ボタンで終了
-- ============================================================================

-- VIDEO_META_BEGIN (auto-patched by convert_video.py)
local FRAME_W = 320
local FRAME_H = 240
local FRAME_FPS = 30
local FRAME_COUNT = 6572
local FRAME_BYTES = 9600
local FRAME_DELTA = true
-- VIDEO_META_END

local FRAME_MS = math.floor(1000 / FRAME_FPS + 0.5)
local FRAMES_PACK = "frames.pack"

local COL_WHITE = machine.rgb(255, 255, 255)
local COL_BLACK = machine.rgb(0, 0, 0)

local frame_idx = 1
local frame_acc = 0
local ready = false
local missing_hint = false

local function exit_pressed()
  return machine.jump_pressed() or machine.pressed(4) or machine.pressed(2)
end

function game_init()
  ready = machine.file_exists(FRAMES_PACK)
  missing_hint = not ready
  frame_idx = 1
  frame_acc = 0
  _G.__bad_apple_ready = ready
  _G.__bad_apple_missing = missing_hint
  _G.__bad_apple_frame = frame_idx
  _G.__bad_apple_frame_ms = FRAME_MS
  _G.__bad_apple_frame_count = FRAME_COUNT
end

function game_update(dt)
  if exit_pressed() then
    return true
  end

  if not ready then
    _G.__bad_apple_ready = ready
    _G.__bad_apple_missing = missing_hint
    _G.__bad_apple_frame = frame_idx
    return false
  end

  frame_acc = frame_acc + dt
  if frame_acc >= FRAME_MS then
    frame_acc = frame_acc - FRAME_MS
    frame_idx = frame_idx + 1
    if frame_idx > FRAME_COUNT then
      frame_idx = 1
    end
  end
  _G.__bad_apple_ready = ready
  _G.__bad_apple_missing = missing_hint
  _G.__bad_apple_frame = frame_idx
  return false
end

function game_draw()
  if missing_hint then
    machine.clear(COL_BLACK)
    if machine.band_index() == 0 then
      machine.text(8, 100, "NO FRAMES", machine.rgb(255, 80, 80), COL_BLACK)
      machine.text(8, 116, "Run convert_video.py", machine.rgb(200, 200, 200), COL_BLACK)
    end
    return
  end

  local ok = machine.draw_bw_pack(FRAMES_PACK, frame_idx, 0, 0, FRAME_W, FRAME_H, COL_WHITE, COL_BLACK)
  if not ok and machine.band_index() == 0 then
    machine.text(8, 120, "READ ERR", machine.rgb(255, 80, 80), COL_BLACK)
  end
end
