-- ============================================================================
-- しょうぎ vs もみじ（Momiji）
-- Suityouka Game Machine
--
-- SD: /games/Shogi/Shogi.lua + img/Momiji_*.bin + img/Koma.bin + img/Shogi_BGs.bin
--     + fonts/game_font.bin + sound/piece_drop.wav（駒を置いたときの SE）
--     + save.dat（対局中 OP_RIGHT で記録、タイトルから再開）
--
-- 操作:
--   タイトル … うえしたでレベル／つづきから、NEAR で決定、OP_LEFT で終了
--   対局 … 十字でカーソル、NEAR で選択/移動/打ち
--          FAR で持ち駒選択、OP_RIGHT で盤面記録、OP_LEFT でタイトル
-- ============================================================================

local W = machine.width()
local H = machine.height()

local BTN_RIGHT = 0
local BTN_UP = 1
local BTN_LEFT = 2
local BTN_DOWN = 3
local BTN_OP_LEFT = 4
local BTN_OP_RIGHT = 5
local BTN_FAR = 6
local BTN_NEAR = 7

local EMPTY = 0
local SENTE = 1 -- プレイヤー（下）
local GOTE = 2 -- もみじ AI（上）

-- 駒種（未成）
local P_FU, P_KY, P_KE, P_GI, P_KI, P_KA, P_HI, P_OU = 1, 2, 3, 4, 5, 6, 7, 8
-- 成りは type+8（金相当の動きなど）
local PROMOTE = 8

local BOARD_N = 9
local CELL = 20
local BOARD_PX = BOARD_N * CELL -- 180
local BOARD_X = 2
local BOARD_Y = 22
local KOMA_W, KOMA_H = 17, 17 -- img/Koma.bin 元画像サイズ

-- 右サイド: 上=もみじ画像、下=セリフ（縦積みで盤を広く取る）
-- draw_image_xform は描画録画を壊しやすいので等倍 keyed 描画にする
local MOMIJI_W, MOMIJI_H = 72, 96
-- 下のテキストボックス（LINE_X）とX位置を揃える
-- テキストボックスは [LINE_X, LINE_X + LINE_W) の中心で揃える
local MOMIJI_X = (BOARD_X + BOARD_PX + 6) + ((W - BOARD_PX - BOARD_X - 10) // 2) - (MOMIJI_W // 2)
local MOMIJI_Y = 22
local LINE_W = W - BOARD_PX - BOARD_X - 10 -- ~128
local LINE_H = 40
local LINE_X = BOARD_X + BOARD_PX + 6
local LINE_Y = MOMIJI_Y + MOMIJI_H + 4
-- 椛コメント欄の下（操作ヒント / リトライ案内）
local HINT_Y = LINE_Y + LINE_H + 6

-- 勝敗パネル（盤の中心に配置）
local RESULT_PANEL_W = 140
local RESULT_PANEL_H = 40
local RESULT_PANEL_X = BOARD_X + (BOARD_PX - RESULT_PANEL_W) // 2
local RESULT_PANEL_Y = BOARD_Y + (BOARD_PX - RESULT_PANEL_H) // 2

local COL_BG = machine.rgb(18, 22, 28)
local COL_BOARD = machine.rgb(180, 140, 80)
local COL_BOARD2 = machine.rgb(160, 120, 65)
local COL_GRID = machine.rgb(90, 60, 30)
local COL_SENTE = machine.rgb(245, 240, 230)
local COL_GOTE = machine.rgb(40, 45, 55)
local COL_SENTE_T = machine.rgb(30, 30, 40)
local COL_GOTE_T = machine.rgb(240, 240, 245)
local COL_CURSOR = machine.rgb(255, 230, 80)
local COL_SEL = machine.rgb(80, 200, 255)
local COL_HINT = machine.rgb(80, 200, 140)
local COL_HUD = machine.rgb(230, 240, 255)
local COL_DIM = machine.rgb(140, 160, 180)
local COL_ACCENT = machine.rgb(255, 210, 90)
local COL_BAD = machine.rgb(255, 120, 120)
local COL_PANEL = machine.rgb(20, 40, 50)
local COL_PANEL_EDGE = machine.rgb(60, 100, 120)

-- タイトル左カラム（椛コメント欄と同じ縁取りパネル）
-- 右端が LINE_X（コメント欄）に被らないよう余白を取る
local TITLE_BOX_X = 24
local TITLE_BOX_W = LINE_X - TITLE_BOX_X - 8 -- ~156
-- 選択ハイライトは文言幅に合わせて短め（右端を中央寄りに）
local TITLE_SEL_X = 26
local TITLE_SEL_W = 105
-- 操作ヒントは save の有無に関係なく画面下部に固定（幅は広め）
local TITLE_FOOTER_X = 24
local TITLE_FOOTER_W = W - TITLE_FOOTER_X - 6 -- ~290
local TITLE_FOOTER_Y = 186
local TITLE_FOOTER_H = 50

-- 椛コメントと同じ外観のパネル（縁 + 塗り）
local fn = {}

function fn.draw_panel_box(x, y, w, h)
    machine.fill_rect(x - 2, y - 2, w + 4, h + 4, COL_PANEL_EDGE)
    machine.fill_rect(x, y, w, h, COL_PANEL)
end

local PIECE_VAL = {
    [P_FU] = 100,
    [P_KY] = 300,
    [P_KE] = 300,
    [P_GI] = 450,
    [P_KI] = 500,
    [P_KA] = 700,
    [P_HI] = 800,
    [P_OU] = 20000,
    [P_FU + PROMOTE] = 450,
    [P_KY + PROMOTE] = 450,
    [P_KE + PROMOTE] = 450,
    [P_GI + PROMOTE] = 500,
    [P_KA + PROMOTE] = 900,
    [P_HI + PROMOTE] = 1000,
}

local PIECE_LABEL = {
    [P_FU] = "歩",
    [P_KY] = "香",
    [P_KE] = "桂",
    [P_GI] = "銀",
    [P_KI] = "金",
    [P_KA] = "角",
    [P_HI] = "飛",
    [P_OU] = "王",
    [P_FU + PROMOTE] = "と",
    [P_KY + PROMOTE] = "杏",
    [P_KE + PROMOTE] = "圭",
    [P_GI + PROMOTE] = "全",
    [P_KA + PROMOTE] = "馬",
    [P_HI + PROMOTE] = "龍",
}

local HAND_ORDER = { P_HI, P_KA, P_KI, P_GI, P_KE, P_KY, P_FU }

local LINES = {
    thinking = { "ふむふむ…", "計算中！", "どの手がいいかな？", "ちょっと待って…" },
    idea = { "これでどうだ！", "妙案！", "ここだ！", "ふふん！" },
    despair = { "ば、ばかぁ…", "まずいかも", "作戦ミス…", "うわーん…" },
    idle = { "あなたの番だよ", "さあ指して！", "待ってるよ", "どう出るかな？" },
    win = { "天狗の勝利！", "勝ちだ！", "やったぁ！" },
    lose = { "くっ…負けた", "次は負けん…", "完敗だ…" },
}

local DIFFICULTY = {
    {
        name_jp = "やさしい",
        name_en = "EASY",
        depth = 1,
        pick_top = 4,
        blunder_pct = 32,
        think_ms = { 220, 380 },
        search_ms = 200,
        king_w = 0.6,
        pst_w = 0.7,
        check_w = 0.7,
        hand_w = 0.8,
    },
    {
        name_jp = "ふつう",
        name_en = "NORMAL",
        depth = 1,
        pick_top = 1,
        blunder_pct = 0,
        think_ms = { 320, 480 },
        search_ms = 3500,
        king_w = 1.0,
        pst_w = 1.0,
        check_w = 1.0,
        hand_w = 1.0,
    },
    {
        name_jp = "つよい",
        name_en = "HARD",
        depth = 2,
        pick_top = 1,
        blunder_pct = 0,
        think_ms = { 420, 640 },
        search_ms = 5000,
        king_w = 1.6,
        pst_w = 1.25,
        check_w = 1.5,
        hand_w = 1.3,
    },
}

-- 歩の前進ボーナス（行 1=上 … 9=下）
local SENTE_FU_ADV = { 50, 45, 35, 25, 15, 8, 3, 0, 0 }
local GOTE_FU_ADV = { 0, 0, 3, 8, 15, 25, 35, 45, 50 }

local mode = "title"
local font_ok = false
local difficulty = 2
local title_sel = 2 -- タイトルのカーソル（1..#DIFFICULTY、記録ありなら +1 でつづきから）
local board = {}
local hand = { [SENTE] = {}, [GOTE] = {} }
local turn = SENTE
local cursor_r, cursor_c = 4, 3
local selected = nil -- {r,c}
local hand_sel = 0 -- 打ち駒種（0=なし）
local game_over = false
local winner = 0
local prev_pressed = {}
local blink = 0
local anim_t = 0
local has_save = false
local save_difficulty = 2 -- 記録時の難易度（つづきから行の表示用）
local SAVE_PATH = "save.dat"
local SAVE_VERSION = 1

local ai_phase = "idle"
local ai_timer = 0
local ai_move = nil
local ai_score = 0
local ai_search_deadline = 0
local emotion = "idle"
local line_text = "将棋を打とう！"
local line_idx = 1

local momiji_ids = {}
local momiji_fallback = false
local SE_PLACE = "sound/piece_drop.wav"
local koma_id = nil
-- 160×120 を RAM に載せ、2 倍ニアレストで全画面背景に使う（約 38KB）
local bg_id = nil
local BG_PATH = "img/Shogi_BGs.bin"
local BG_W, BG_H = 160, 120
local BG_SCALE = 2

-- 駒を盤に置いたとき（移動・打ち共通）に WAV SE を再生する
function fn.play_place_se()
    local ok, err = machine.play_se(SE_PLACE)
    if ok then
        return
    end
    if err then
        print("piece_drop SE fail:", err)
    end
    -- SD に WAV が無い／デコード失敗時のみフォールバック
    machine.play_tone(660, 60)
end

-- 合法手ハイライト用キャッシュ（毎フレーム／毎バンドの再計算を避ける）
local hl_moves = {}
local hl_kind = "none" -- none | piece | drop
local hl_sr, hl_sc = 0, 0
local hl_drop = 0

function fn.just_pressed(btn)
    local now = machine.pressed(btn)
    local was = prev_pressed[btn]
    prev_pressed[btn] = now
    return now and not was
end

function fn.pick_line(key)
    local list = LINES[key]
    if not list or #list == 0 then
        return ""
    end
    line_idx = (line_idx % #list) + 1
    return list[line_idx]
end

function fn.set_emotion(emo, force_line)
    emotion = emo
    if force_line then
        line_text = force_line
    else
        line_text = fn.pick_line(emo)
    end
end

function fn.diff_cfg()
    return DIFFICULTY[difficulty] or DIFFICULTY[2]
end

function fn.diff_label()
    local cfg = fn.diff_cfg()
    return font_ok and cfg.name_jp or cfg.name_en
end

function fn.pack(side, ptype)
    return side * 16 + ptype
end

function fn.side_of(p)
    if p == EMPTY then
        return 0
    end
    return p // 16
end

function fn.type_of(p)
    return p % 16
end

function fn.base_type(ptype)
    if ptype > PROMOTE then
        return ptype - PROMOTE
    end
    return ptype
end

function fn.demote_for_hand(ptype)
    return fn.base_type(ptype)
end

function fn.in_board(r, c)
    return r >= 1 and r <= BOARD_N and c >= 1 and c <= BOARD_N
end

function fn.forward(side)
    return side == SENTE and -1 or 1
end

function fn.opponent(side)
    return side == SENTE and GOTE or SENTE
end

function fn.clear_hand(side)
    hand[side] = {}
    for i = 1, 8 do
        hand[side][i] = 0
    end
end

function fn.add_hand(h, side, ptype)
    local t = fn.demote_for_hand(ptype)
    if t == P_OU then
        return
    end
    h[side][t] = (h[side][t] or 0) + 1
end

function fn.copy_board(src)
    local dst = {}
    for r = 1, BOARD_N do
        dst[r] = {}
        for c = 1, BOARD_N do
            dst[r][c] = src[r][c]
        end
    end
    return dst
end

function fn.copy_hand(src)
    local dst = { [SENTE] = {}, [GOTE] = {} }
    for s = SENTE, GOTE do
        for i = 1, 8 do
            dst[s][i] = src[s][i] or 0
        end
    end
    return dst
end

function fn.find_king(b, side)
    local want = fn.pack(side, P_OU)
    for r = 1, BOARD_N do
        for c = 1, BOARD_N do
            if b[r][c] == want then
                return r, c
            end
        end
    end
    return nil, nil
end

function fn.can_promote_raw(ptype)
    local t = fn.base_type(ptype)
    return t == P_FU or t == P_KY or t == P_KE or t == P_GI or t == P_KA or t == P_HI
end

function fn.in_promote_zone(side, r)
    if side == SENTE then
        return r <= 3
    end
    return r >= BOARD_N - 2
end

function fn.gold_deltas()
    return {
        { -1, -1 }, { 0, -1 }, { 1, -1 },
        { -1, 0 }, { 1, 0 },
        { 0, 1 },
    }
end

function fn.silver_deltas(side)
    local f = fn.forward(side)
    return {
        { -1, f }, { 0, f }, { 1, f },
        { -1, -f }, { 1, -f },
    }
end

function fn.king_deltas()
    return {
        { -1, -1 }, { 0, -1 }, { 1, -1 },
        { -1, 0 }, { 1, 0 },
        { -1, 1 }, { 0, 1 }, { 1, 1 },
    }
end

function fn.add_step_moves(b, side, r, c, deltas, moves)
    for i = 1, #deltas do
        local nr = r + deltas[i][2]
        local nc = c + deltas[i][1]
        if fn.in_board(nr, nc) then
            local t = b[nr][nc]
            if t == EMPTY or fn.side_of(t) ~= side then
                moves[#moves + 1] = { fr = r, fc = c, tr = nr, tc = nc, drop = 0 }
            end
        end
    end
end

function fn.add_slide_moves(b, side, r, c, dirs, moves)
    for i = 1, #dirs do
        local dr, dc = dirs[i][2], dirs[i][1]
        local nr, nc = r + dr, c + dc
        while fn.in_board(nr, nc) do
            local t = b[nr][nc]
            if t == EMPTY then
                moves[#moves + 1] = { fr = r, fc = c, tr = nr, tc = nc, drop = 0 }
            else
                if fn.side_of(t) ~= side then
                    moves[#moves + 1] = { fr = r, fc = c, tr = nr, tc = nc, drop = 0 }
                end
                break
            end
            nr = nr + dr
            nc = nc + dc
        end
    end
end

function fn.gen_pseudo_moves_from(b, side, r, c, moves)
    local p = b[r][c]
    if fn.side_of(p) ~= side then
        return
    end
    local ptype = fn.type_of(p)
    local f = fn.forward(side)

    if ptype == P_FU then
        fn.add_step_moves(b, side, r, c, { { 0, f } }, moves)
    elseif ptype == P_KY then
        fn.add_slide_moves(b, side, r, c, { { 0, f } }, moves)
    elseif ptype == P_KE then
        fn.add_step_moves(b, side, r, c, { { -1, f * 2 }, { 1, f * 2 } }, moves)
    elseif ptype == P_GI then
        fn.add_step_moves(b, side, r, c, fn.silver_deltas(side), moves)
    elseif ptype == P_KI or ptype == P_FU + PROMOTE or ptype == P_KY + PROMOTE
        or ptype == P_KE + PROMOTE or ptype == P_GI + PROMOTE then
        local d = fn.gold_deltas()
        -- 向きを side に合わせる（gold_deltas は SENTE 向き）
        if side == GOTE then
            for i = 1, #d do
                d[i] = { d[i][1], -d[i][2] }
            end
        end
        fn.add_step_moves(b, side, r, c, d, moves)
    elseif ptype == P_KA then
        fn.add_slide_moves(b, side, r, c, {
            { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 },
        }, moves)
    elseif ptype == P_HI then
        fn.add_slide_moves(b, side, r, c, {
            { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 },
        }, moves)
    elseif ptype == P_KA + PROMOTE then
        fn.add_slide_moves(b, side, r, c, {
            { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 },
        }, moves)
        fn.add_step_moves(b, side, r, c, fn.king_deltas(), moves)
    elseif ptype == P_HI + PROMOTE then
        fn.add_slide_moves(b, side, r, c, {
            { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 },
        }, moves)
        fn.add_step_moves(b, side, r, c, fn.king_deltas(), moves)
    elseif ptype == P_OU then
        fn.add_step_moves(b, side, r, c, fn.king_deltas(), moves)
    end
end

function fn.gen_pseudo_moves(b, h, side)
    local moves = {}
    for r = 1, BOARD_N do
        for c = 1, BOARD_N do
            if fn.side_of(b[r][c]) == side then
                fn.gen_pseudo_moves_from(b, side, r, c, moves)
            end
        end
    end
    -- 打ち
    for ti = 1, #HAND_ORDER do
        local t = HAND_ORDER[ti]
        if (h[side][t] or 0) > 0 then
            for r = 1, BOARD_N do
                for c = 1, BOARD_N do
                    if b[r][c] == EMPTY then
                        local ok = true
                        if t == P_FU then
                            for rr = 1, BOARD_N do
                                if fn.type_of(b[rr][c]) == P_FU and fn.side_of(b[rr][c]) == side then
                                    ok = false
                                    break
                                end
                            end
                            local edge = side == SENTE and 1 or BOARD_N
                            if r == edge then
                                ok = false
                            end
                        elseif t == P_KY then
                            local edge = side == SENTE and 1 or BOARD_N
                            if r == edge then
                                ok = false
                            end
                        elseif t == P_KE then
                            if side == SENTE and r <= 2 then
                                ok = false
                            elseif side == GOTE and r >= BOARD_N - 1 then
                                ok = false
                            end
                        end
                        if ok then
                            moves[#moves + 1] = { fr = 0, fc = 0, tr = r, tc = c, drop = t }
                        end
                    end
                end
            end
        end
    end
    return moves
end

-- 攻撃判定用（打ちを除く擬似手）
function fn.is_square_attacked(b, r, c, by_side)
    local moves = {}
    for rr = 1, BOARD_N do
        for cc = 1, BOARD_N do
            if fn.side_of(b[rr][cc]) == by_side then
                fn.gen_pseudo_moves_from(b, by_side, rr, cc, moves)
            end
        end
    end
    for i = 1, #moves do
        local m = moves[i]
        if m.tr == r and m.tc == c then
            return true
        end
    end
    return false
end

function fn.apply_move_state(b, h, side, m)
    local captured = EMPTY
    if m.drop ~= 0 then
        b[m.tr][m.tc] = fn.pack(side, m.drop)
        h[side][m.drop] = h[side][m.drop] - 1
    else
        local p = b[m.fr][m.fc]
        captured = b[m.tr][m.tc]
        b[m.fr][m.fc] = EMPTY
        local ptype = fn.type_of(p)
        local promo = false
        if fn.can_promote_raw(ptype) and ptype <= PROMOTE then
            if fn.in_promote_zone(side, m.fr) or fn.in_promote_zone(side, m.tr) then
                -- 歩・香・桂は強制、他は成る
                local bt = fn.base_type(ptype)
                if bt == P_FU or bt == P_KY or bt == P_KE or bt == P_KA or bt == P_HI or bt == P_GI then
                    promo = true
                end
            end
        end
        if promo then
            p = fn.pack(side, ptype + PROMOTE)
        end
        b[m.tr][m.tc] = p
        if captured ~= EMPTY then
            fn.add_hand(h, side, fn.type_of(captured))
        end
    end
    return captured
end

function fn.is_in_check(b, side)
    local kr, kc = fn.find_king(b, side)
    if not kr then
        return true
    end
    return fn.is_square_attacked(b, kr, kc, fn.opponent(side))
end

-- 疑似手を1手だけ合法判定（打ち歩詰め簡易チェック込み）
function fn.is_legal_move(b, h, side, m)
    local nb = fn.copy_board(b)
    local nh = fn.copy_hand(h)
    fn.apply_move_state(nb, nh, side, m)
    if fn.is_in_check(nb, side) then
        return false
    end
    if m.drop == P_FU and fn.is_in_check(nb, fn.opponent(side)) then
        -- 打ち歩詰め: 相手に1手でも逃げる合法手があれば OK
        local reply = fn.gen_pseudo_moves(nb, nh, fn.opponent(side))
        local has_reply = false
        for j = 1, #reply do
            local nb2 = fn.copy_board(nb)
            local nh2 = fn.copy_hand(nh)
            fn.apply_move_state(nb2, nh2, fn.opponent(side), reply[j])
            if not fn.is_in_check(nb2, fn.opponent(side)) then
                has_reply = true
                break
            end
        end
        if not has_reply then
            return false
        end
    end
    return true
end

-- 疑似手リストを合法手に絞る（打ち歩詰め簡易チェック込み）
function fn.filter_legal_moves(b, h, side, raw)
    local legal = {}
    for i = 1, #raw do
        local m = raw[i]
        if fn.is_legal_move(b, h, side, m) then
            legal[#legal + 1] = m
        end
    end
    return legal
end

function fn.gen_legal_moves(b, h, side)
    return fn.filter_legal_moves(b, h, side, fn.gen_pseudo_moves(b, h, side))
end

-- 詰み判定用: 合法手が1つでもあれば即 true（全列挙しない）
function fn.has_legal_move(b, h, side)
    -- 盤上の駒を先に見る（終盤でも打ちより早く見つかることが多い）
    for r = 1, BOARD_N do
        for c = 1, BOARD_N do
            if fn.side_of(b[r][c]) == side then
                local raw = {}
                fn.gen_pseudo_moves_from(b, side, r, c, raw)
                for i = 1, #raw do
                    if fn.is_legal_move(b, h, side, raw[i]) then
                        return true
                    end
                end
            end
        end
    end
    -- 打ち（歩は最後に回し、打ち歩詰めチェックを避ける）
    for ti = 1, #HAND_ORDER do
        local t = HAND_ORDER[ti]
        if t ~= P_FU and (h[side][t] or 0) > 0 then
            for r = 1, BOARD_N do
                for c = 1, BOARD_N do
                    if b[r][c] == EMPTY then
                        local ok = true
                        if t == P_KY then
                            local edge = side == SENTE and 1 or BOARD_N
                            if r == edge then
                                ok = false
                            end
                        elseif t == P_KE then
                            if side == SENTE and r <= 2 then
                                ok = false
                            elseif side == GOTE and r >= BOARD_N - 1 then
                                ok = false
                            end
                        end
                        if ok and fn.is_legal_move(b, h, side, { fr = 0, fc = 0, tr = r, tc = c, drop = t }) then
                            return true
                        end
                    end
                end
            end
        end
    end
    if (h[side][P_FU] or 0) > 0 then
        for r = 1, BOARD_N do
            for c = 1, BOARD_N do
                if b[r][c] == EMPTY then
                    local ok = true
                    for rr = 1, BOARD_N do
                        if fn.type_of(b[rr][c]) == P_FU and fn.side_of(b[rr][c]) == side then
                            ok = false
                            break
                        end
                    end
                    local edge = side == SENTE and 1 or BOARD_N
                    if r == edge then
                        ok = false
                    end
                    if ok and fn.is_legal_move(b, h, side, { fr = 0, fc = 0, tr = r, tc = c, drop = P_FU }) then
                        return true
                    end
                end
            end
        end
    end
    return false
end

-- 盤上の1マスからの合法手のみ
function fn.gen_legal_from_square(b, h, side, r, c)
    local raw = {}
    fn.gen_pseudo_moves_from(b, side, r, c, raw)
    return fn.filter_legal_moves(b, h, side, raw)
end

-- 指定持ち駒の打ち合法手のみ
function fn.gen_legal_drops(b, h, side, drop_type)
    local raw = {}
    if (h[side][drop_type] or 0) <= 0 then
        return raw
    end
    for r = 1, BOARD_N do
        for c = 1, BOARD_N do
            if b[r][c] == EMPTY then
                local ok = true
                if drop_type == P_FU then
                    for rr = 1, BOARD_N do
                        if fn.type_of(b[rr][c]) == P_FU and fn.side_of(b[rr][c]) == side then
                            ok = false
                            break
                        end
                    end
                    local edge = side == SENTE and 1 or BOARD_N
                    if r == edge then
                        ok = false
                    end
                elseif drop_type == P_KY then
                    local edge = side == SENTE and 1 or BOARD_N
                    if r == edge then
                        ok = false
                    end
                elseif drop_type == P_KE then
                    if side == SENTE and r <= 2 then
                        ok = false
                    elseif side == GOTE and r >= BOARD_N - 1 then
                        ok = false
                    end
                end
                if ok then
                    raw[#raw + 1] = { fr = 0, fc = 0, tr = r, tc = c, drop = drop_type }
                end
            end
        end
    end
    return fn.filter_legal_moves(b, h, side, raw)
end

function fn.invalidate_highlights()
    hl_moves = {}
    hl_kind = "none"
    hl_sr, hl_sc = 0, 0
    hl_drop = 0
end

function fn.refresh_highlights()
    if mode ~= "play" or turn ~= SENTE or game_over then
        fn.invalidate_highlights()
        return
    end
    if selected then
        hl_kind = "piece"
        hl_sr, hl_sc = selected.r, selected.c
        hl_drop = 0
        hl_moves = fn.gen_legal_from_square(board, hand, SENTE, selected.r, selected.c)
    elseif hand_sel ~= 0 then
        hl_kind = "drop"
        hl_sr, hl_sc = 0, 0
        hl_drop = hand_sel
        hl_moves = fn.gen_legal_drops(board, hand, SENTE, hand_sel)
    else
        fn.invalidate_highlights()
    end
end

function fn.enemy_zone_bonus(ks, r)
    if ks == SENTE then
        if r <= 3 then
            return 30
        end
        if r <= 5 then
            return 10
        end
    else
        if r >= 7 then
            return 30
        end
        if r >= 5 then
            return 10
        end
    end
    return 0
end

function fn.piece_pst_bonus(ks, r, c, ptype)
    local bt = fn.base_type(ptype)
    local bonus = 0
    if bt == P_FU then
        bonus = (ks == SENTE and SENTE_FU_ADV[r] or GOTE_FU_ADV[r])
    elseif bt == P_HI or bt == P_KA or ptype > PROMOTE then
        bonus = bonus + (5 - math.abs(c - 5)) * 4
        bonus = bonus + fn.enemy_zone_bonus(ks, r)
        if ptype == P_HI + PROMOTE or ptype == P_KA + PROMOTE then
            bonus = bonus + 35
        end
    elseif bt == P_GI or bt == P_KE then
        bonus = math.floor(fn.enemy_zone_bonus(ks, r) / 2)
    end
    return bonus
end

-- 玉周辺の守備・逃げ道（攻撃判定は使わず軽量に）
function fn.king_guard_bonus(b, ks)
    local kr, kc = fn.find_king(b, ks)
    if not kr then
        return -300
    end
    local guard = 0
    for dr = -1, 1 do
        for dc = -1, 1 do
            if dr ~= 0 or dc ~= 0 then
                local nr, nc = kr + dr, kc + dc
                if fn.in_board(nr, nc) then
                    local p = b[nr][nc]
                    if fn.side_of(p) == ks then
                        local bt = fn.base_type(fn.type_of(p))
                        if bt == P_KI then
                            guard = guard + 30
                        elseif bt == P_GI then
                            guard = guard + 22
                        elseif bt == P_KE or bt == P_KY then
                            guard = guard + 8
                        end
                    elseif fn.side_of(p) == fn.opponent(ks) then
                        guard = guard - 18
                    end
                end
            end
        end
    end
    for dr = -1, 1 do
        for dc = -1, 1 do
            if (dr ~= 0 or dc ~= 0) and fn.in_board(kr + dr, kc + dc) and b[kr + dr][kc + dc] == EMPTY then
                guard = guard + 5
            end
        end
    end
    return guard
end

function fn.hand_flex_bonus(h, ks)
    local n = 0
    for t = 1, 8 do
        n = n + (h[ks][t] or 0)
    end
    return n * 12
end

function fn.eval_pos(b, h, side)
    local cfg = fn.diff_cfg()
    local kw = cfg.king_w or 1.0
    local pw = cfg.pst_w or 1.0
    local cw = cfg.check_w or 1.0
    local hw = cfg.hand_w or 1.0
    local score = 0
    local mid = (BOARD_N + 1) / 2
    for r = 1, BOARD_N do
        for c = 1, BOARD_N do
            local p = b[r][c]
            if p ~= EMPTY then
                local s = fn.side_of(p)
                local t = fn.type_of(p)
                local v = PIECE_VAL[t] or 0
                local center = 4 - math.abs(r - mid) - math.abs(c - mid)
                v = v + center * 6 + fn.piece_pst_bonus(s, r, c, t) * pw
                if s == side then
                    score = score + v
                else
                    score = score - v
                end
            end
        end
    end
    for t = 1, 8 do
        local v = (PIECE_VAL[t] or 0) * 0.9
        score = score + (h[side][t] or 0) * v
        score = score - (h[fn.opponent(side)][t] or 0) * v
    end
    score = score + (fn.king_guard_bonus(b, side) - fn.king_guard_bonus(b, fn.opponent(side))) * kw
    score = score + (fn.hand_flex_bonus(h, side) - fn.hand_flex_bonus(h, fn.opponent(side))) * hw
    if fn.is_in_check(b, fn.opponent(side)) then
        score = score + 80 * cw
    end
    if fn.is_in_check(b, side) then
        score = score - 80 * cw
    end
    return score
end

-- 取りを先に並べると αβ の刈り込みが効きやすい
function fn.order_moves_for_search(b, moves)
    local caps, rest = {}, {}
    for i = 1, #moves do
        local m = moves[i]
        if m.drop == 0 and b[m.tr][m.tc] ~= EMPTY then
            local cap_v = PIECE_VAL[fn.type_of(b[m.tr][m.tc])] or 0
            caps[#caps + 1] = { m = m, v = cap_v }
        else
            rest[#rest + 1] = m
        end
    end
    table.sort(caps, function(a, bb)
        return a.v > bb.v
    end)
    local ordered = {}
    for i = 1, #caps do
        ordered[#ordered + 1] = caps[i].m
    end
    for i = 1, #rest do
        ordered[#ordered + 1] = rest[i]
    end
    return ordered
end

function fn.search_best(b, h, side, depth, maximizing_side, alpha, beta)
    if alpha == nil then
        alpha = -999999
    end
    if beta == nil then
        beta = 999999
    end
    -- 時間切れ: その局面の静的評価で打ち切り（最善手は上位で保持）
    if ai_search_deadline > 0 and machine.time_ms() >= ai_search_deadline then
        return fn.eval_pos(b, h, maximizing_side), nil
    end
    if depth == 0 then
        return fn.eval_pos(b, h, maximizing_side), nil
    end

    -- 全合法手を先に列挙せず、疑似手を順に合法性チェック（αβで途中打ち切りしやすい）
    local raw = fn.order_moves_for_search(b, fn.gen_pseudo_moves(b, h, side))
    local best_score = side == maximizing_side and -999999 or 999999
    local best_move = nil
    local any_legal = false
    for i = 1, #raw do
        if ai_search_deadline > 0 and machine.time_ms() >= ai_search_deadline then
            break
        end
        local m = raw[i]
        if fn.is_legal_move(b, h, side, m) then
            any_legal = true
            local nb = fn.copy_board(b)
            local nh = fn.copy_hand(h)
            fn.apply_move_state(nb, nh, side, m)
            local sc = select(1, fn.search_best(nb, nh, fn.opponent(side), depth - 1, maximizing_side, alpha, beta))
            if side == maximizing_side then
                if sc > best_score then
                    best_score = sc
                    best_move = m
                end
                if best_score > alpha then
                    alpha = best_score
                end
                if alpha >= beta then
                    break -- β刈り
                end
            else
                if sc < best_score then
                    best_score = sc
                    best_move = m
                end
                if best_score < beta then
                    beta = best_score
                end
                if alpha >= beta then
                    break -- α刈り
                end
            end
        end
    end
    if not any_legal then
        if fn.is_in_check(b, side) then
            return side == maximizing_side and -50000 or 50000, nil
        end
        return side == maximizing_side and -1000 or 1000, nil
    end
    return best_score, best_move
end

function fn.score_moves_shallow(b, h, side)
    local moves = fn.gen_legal_moves(b, h, side)
    local scored = {}
    for i = 1, #moves do
        local m = moves[i]
        local nb = fn.copy_board(b)
        local nh = fn.copy_hand(h)
        fn.apply_move_state(nb, nh, side, m)
        scored[#scored + 1] = { move = m, score = fn.eval_pos(nb, nh, side) }
    end
    table.sort(scored, function(a, bb)
        return a.score > bb.score
    end)
    return scored
end

function fn.rand_index(n)
    if n <= 1 then
        return 1
    end
    return 1 + (machine.time_ms() % n)
end

function fn.choose_ai_move()
    local cfg = fn.diff_cfg()
    if cfg.blunder_pct > 0 and (machine.time_ms() % 100) < cfg.blunder_pct then
        local raw = fn.gen_pseudo_moves(board, hand, GOTE)
        local legal = {}
        for i = 1, #raw do
            if fn.is_legal_move(board, hand, GOTE, raw[i]) then
                legal[#legal + 1] = raw[i]
                if #legal >= 8 then
                    break -- 凡ミス用に数手あれば足りる
                end
            end
        end
        if #legal == 0 then
            return nil, -99999
        end
        return legal[fn.rand_index(#legal)], 0
    end
    if cfg.pick_top and cfg.pick_top > 1 then
        local scored = fn.score_moves_shallow(board, hand, GOTE)
        if #scored == 0 then
            return nil, -99999
        end
        local n = math.min(cfg.pick_top, #scored)
        local pick = scored[fn.rand_index(n)]
        return pick.move, pick.score
    end
    ai_search_deadline = machine.time_ms() + (cfg.search_ms or 350)
    local score, move = fn.search_best(board, hand, GOTE, cfg.depth, GOTE)
    ai_search_deadline = 0
    if not move then
        -- 時間切れ等で手が無いとき: 最初の合法手を拾う
        local raw = fn.order_moves_for_search(board, fn.gen_pseudo_moves(board, hand, GOTE))
        for i = 1, #raw do
            if fn.is_legal_move(board, hand, GOTE, raw[i]) then
                move = raw[i]
                score = fn.eval_pos(board, hand, GOTE)
                break
            end
        end
    end
    return move, score or 0
end

function fn.ai_think_delay()
    local cfg = fn.diff_cfg()
    local lo, hi = cfg.think_ms[1], cfg.think_ms[2]
    return lo + (machine.time_ms() % math.max(1, hi - lo + 1))
end

function fn.title_menu_count()
    return #DIFFICULTY + (has_save and 1 or 0)
end

function fn.clamp_title_sel()
    local n = fn.title_menu_count()
    if title_sel < 1 then
        title_sel = 1
    elseif title_sel > n then
        title_sel = n
    end
end

function fn.refresh_has_save()
    -- file_exists に頼らない（プレビューは save が _preview_save/ にあり不一致になるため）
    local data = machine.load_data(SAVE_PATH)
    if type(data) ~= "table" then
        has_save = false
        save_difficulty = 2
        fn.clamp_title_sel()
        return
    end
    -- valid は true / 1 のどちらでも可
    local ok = data.valid == true or data.valid == 1
    has_save = ok and type(data.board) == "table"
    if has_save and data.difficulty and data.difficulty >= 1 and data.difficulty <= #DIFFICULTY then
        save_difficulty = data.difficulty
    else
        save_difficulty = 2
    end
    fn.clamp_title_sel()
end

function fn.build_save_table()
    local cells = {}
    for r = 1, BOARD_N do
        for c = 1, BOARD_N do
            cells[(r - 1) * BOARD_N + c] = board[r][c] or EMPTY
        end
    end
    local hs, hg = {}, {}
    for t = 1, 8 do
        hs[t] = hand[SENTE][t] or 0
        hg[t] = hand[GOTE][t] or 0
    end
    return {
        version = SAVE_VERSION,
        valid = true,
        difficulty = difficulty,
        turn = turn,
        cursor_r = cursor_r,
        cursor_c = cursor_c,
        board = cells,
        hand_sente = hs,
        hand_gote = hg,
    }
end

function fn.apply_save_table(data)
    if type(data) ~= "table" then
        return false
    end
    if not (data.valid == true or data.valid == 1) or type(data.board) ~= "table" then
        return false
    end
    local cells = data.board
    board = {}
    for r = 1, BOARD_N do
        board[r] = {}
        for c = 1, BOARD_N do
            board[r][c] = cells[(r - 1) * BOARD_N + c] or EMPTY
        end
    end
    fn.clear_hand(SENTE)
    fn.clear_hand(GOTE)
    local hs = data.hand_sente or {}
    local hg = data.hand_gote or {}
    for t = 1, 8 do
        hand[SENTE][t] = hs[t] or 0
        hand[GOTE][t] = hg[t] or 0
    end
    if data.difficulty and data.difficulty >= 1 and data.difficulty <= #DIFFICULTY then
        difficulty = data.difficulty
    end
    turn = (data.turn == GOTE) and GOTE or SENTE
    cursor_r = data.cursor_r or 7
    cursor_c = data.cursor_c or 5
    if cursor_r < 1 or cursor_r > BOARD_N then
        cursor_r = 7
    end
    if cursor_c < 1 or cursor_c > BOARD_N then
        cursor_c = 5
    end
    selected = nil
    hand_sel = 0
    game_over = false
    winner = 0
    ai_move = nil
    fn.invalidate_highlights()
    if turn == GOTE then
        ai_phase = "thinking"
        ai_timer = fn.ai_think_delay()
        fn.set_emotion("thinking", font_ok and "つづきから…" or "RESUME")
    else
        ai_phase = "idle"
        ai_timer = 0
        fn.set_emotion("idle", font_ok and "つづきだよ" or "RESUME")
    end
    return true
end

function fn.invalidate_save_file()
    machine.save_data(SAVE_PATH, { version = SAVE_VERSION, valid = false })
    has_save = false
end

function fn.try_save_game()
    -- AI思考中は盤が中途半端になりやすいのでプレイヤー待ちのときだけ
    if mode ~= "play" or game_over then
        return
    end
    if turn ~= SENTE or ai_phase ~= "idle" then
        fn.set_emotion("idle", font_ok and "自分の番で\n記録して下さい" or "SAVE ON YOUR TURN")
        machine.play_tone(220, 50)
        return
    end
    local ok, err = machine.save_data(SAVE_PATH, fn.build_save_table())
    if not ok then
        fn.set_emotion("despair", font_ok and "記録失敗…" or "SAVE FAIL")
        print("Shogi save failed:", err)
        machine.play_tone(180, 80)
        return
    end
    has_save = true
    fn.set_emotion("idea", font_ok and "盤面を\n記録しました" or "SAVED!")
    machine.play_tone(660, 60)
end

function fn.try_resume_game()
    local data, err = machine.load_data(SAVE_PATH)
    if type(data) ~= "table" or not (data.valid == true or data.valid == 1) then
        has_save = false
        if err then
            print("Shogi load failed:", err)
        end
        return false
    end
    if not fn.apply_save_table(data) then
        has_save = false
        return false
    end
    mode = "play"
    has_save = true
    machine.play_tone(520, 50)
    return true
end

function fn.reset_board()
    board = {}
    for r = 1, BOARD_N do
        board[r] = {}
        for c = 1, BOARD_N do
            board[r][c] = EMPTY
        end
    end
    -- 本将棋初期配置（下=先手プレイヤー、左=9筋）
    -- 後手（上）
    local back = { P_KY, P_KE, P_GI, P_KI, P_OU, P_KI, P_GI, P_KE, P_KY }
    for c = 1, 9 do
        board[1][c] = fn.pack(GOTE, back[c])
        board[9][c] = fn.pack(SENTE, back[c])
    end
    board[2][2] = fn.pack(GOTE, P_HI)
    board[2][8] = fn.pack(GOTE, P_KA)
    board[8][2] = fn.pack(SENTE, P_KA)
    board[8][8] = fn.pack(SENTE, P_HI)
    for c = 1, 9 do
        board[3][c] = fn.pack(GOTE, P_FU)
        board[7][c] = fn.pack(SENTE, P_FU)
    end

    fn.clear_hand(SENTE)
    fn.clear_hand(GOTE)
    turn = SENTE
    cursor_r, cursor_c = 7, 5
    selected = nil
    hand_sel = 0
    game_over = false
    winner = 0
    ai_phase = "idle"
    ai_timer = 0
    ai_move = nil
    fn.set_emotion("idle", "かかってこい！")
    fn.invalidate_highlights()
end

function fn.start_play()
    mode = "play"
    fn.reset_board()
    -- 新規対局は記録を無効化（つづきと混同しない）
    fn.invalidate_save_file()
end

function fn.finish_game(w)
    game_over = true
    winner = w
    mode = "result"
    fn.invalidate_save_file()
    if w == GOTE then
        fn.set_emotion("idea", fn.pick_line("win"))
    elseif w == SENTE then
        fn.set_emotion("despair", fn.pick_line("lose"))
    else
        fn.set_emotion("thinking", "引き分けか…")
    end
    machine.play_tone(w == SENTE and 520 or 880, 180)
end

function fn.after_move(side_moved)
    selected = nil
    hand_sel = 0
    fn.invalidate_highlights()
    local next_side = fn.opponent(side_moved)
    -- 詰み判定は全合法手列挙不要（1手見つかれば続行）
    if not fn.has_legal_move(board, hand, next_side) then
        fn.finish_game(side_moved)
        return
    end
    turn = next_side
    if next_side == GOTE then
        ai_phase = "thinking"
        ai_timer = fn.ai_think_delay()
        ai_move = nil
        fn.set_emotion("thinking")
    else
        ai_phase = "idle"
        fn.set_emotion("idle")
    end
end

function fn.try_player_action()
    if turn ~= SENTE or ai_phase ~= "idle" or game_over then
        return
    end

    if hand_sel ~= 0 then
        if hl_kind ~= "drop" or hl_drop ~= hand_sel then
            fn.refresh_highlights()
        end
        for i = 1, #hl_moves do
            local m = hl_moves[i]
            if m.tr == cursor_r and m.tc == cursor_c then
                fn.apply_move_state(board, hand, SENTE, m)
                fn.play_place_se()
                fn.invalidate_highlights()
                fn.after_move(SENTE)
                return
            end
        end
        machine.play_tone(220, 50)
        return
    end

    if selected then
        if hl_kind ~= "piece" or hl_sr ~= selected.r or hl_sc ~= selected.c then
            fn.refresh_highlights()
        end
        for i = 1, #hl_moves do
            local m = hl_moves[i]
            if m.tr == cursor_r and m.tc == cursor_c then
                fn.apply_move_state(board, hand, SENTE, m)
                fn.play_place_se()
                selected = nil
                fn.invalidate_highlights()
                fn.after_move(SENTE)
                return
            end
        end
        -- 自駒の再選択
        if fn.side_of(board[cursor_r][cursor_c]) == SENTE then
            selected = { r = cursor_r, c = cursor_c }
            machine.play_tone(440, 40)
            fn.refresh_highlights()
            return
        end
        selected = nil
        fn.invalidate_highlights()
        machine.play_tone(220, 50)
        return
    end

    if fn.side_of(board[cursor_r][cursor_c]) == SENTE then
        selected = { r = cursor_r, c = cursor_c }
        hand_sel = 0
        machine.play_tone(440, 40)
        fn.refresh_highlights()
    else
        machine.play_tone(220, 50)
    end
end

function fn.cycle_hand_sel()
    if turn ~= SENTE then
        return
    end
    selected = nil
    local opts = { 0 }
    for i = 1, #HAND_ORDER do
        local t = HAND_ORDER[i]
        if (hand[SENTE][t] or 0) > 0 then
            opts[#opts + 1] = t
        end
    end
    if #opts == 1 then
        hand_sel = 0
        fn.invalidate_highlights()
        fn.set_emotion("idle", font_ok and "持ち駒なし" or "NO HAND")
        return
    end
    local idx = 1
    for i = 1, #opts do
        if opts[i] == hand_sel then
            idx = i
            break
        end
    end
    idx = idx + 1
    if idx > #opts then
        idx = 1
    end
    hand_sel = opts[idx]
    fn.refresh_highlights()
    if hand_sel == 0 then
        fn.set_emotion("idle", "盤上を選んで")
    else
        fn.set_emotion("idle", (font_ok and "打:" or "DROP ") .. (PIECE_LABEL[hand_sel] or "?"))
    end
end

function fn.update_ai(dt)
    if turn ~= GOTE or game_over then
        return
    end
    if ai_phase == "thinking" then
        ai_timer = ai_timer - dt
        if ai_timer <= 0 then
            ai_move, ai_score = fn.choose_ai_move()
            if not ai_move then
                fn.finish_game(SENTE)
                return
            end
            if ai_score >= 400 then
                fn.set_emotion("idea")
            elseif ai_score <= -200 then
                fn.set_emotion("despair")
            else
                fn.set_emotion("idea")
            end
            ai_phase = "idea"
            ai_timer = 360
        end
    elseif ai_phase == "idea" then
        ai_timer = ai_timer - dt
        if ai_timer <= 0 and ai_move then
            -- 着手だけ先に反映し、赤ハイライトを消してから次フレームで詰み判定
            -- （after_move の重い処理中に絶望+赤のまま固まるのを防ぐ）
            fn.apply_move_state(board, hand, GOTE, ai_move)
            fn.play_place_se()
            ai_move = nil
            ai_phase = "resolve"
        end
    elseif ai_phase == "resolve" then
        fn.after_move(GOTE)
    end
end

function fn.update_title(dt)
    blink = blink + dt
    local n = fn.title_menu_count()
    if fn.just_pressed(BTN_UP) then
        title_sel = title_sel - 1
        if title_sel < 1 then
            title_sel = n
        end
        if title_sel <= #DIFFICULTY then
            difficulty = title_sel
        end
        machine.play_tone(520, 40)
    elseif fn.just_pressed(BTN_DOWN) then
        title_sel = title_sel + 1
        if title_sel > n then
            title_sel = 1
        end
        if title_sel <= #DIFFICULTY then
            difficulty = title_sel
        end
        machine.play_tone(480, 40)
    elseif fn.just_pressed(BTN_NEAR) or fn.just_pressed(BTN_OP_RIGHT) then
        if has_save and title_sel == #DIFFICULTY + 1 then
            if not fn.try_resume_game() then
                fn.set_emotion("despair", font_ok and "記録が読めない" or "NO SAVE")
                machine.play_tone(220, 60)
                fn.refresh_has_save()
            end
        else
            if title_sel >= 1 and title_sel <= #DIFFICULTY then
                difficulty = title_sel
            end
            fn.start_play()
        end
    elseif fn.just_pressed(BTN_OP_LEFT) then
        mode = "quit"
    end
end

function fn.update_play(dt)
    anim_t = anim_t + dt
    if fn.just_pressed(BTN_OP_LEFT) then
        mode = "title"
        fn.refresh_has_save()
        if has_save then
            title_sel = #DIFFICULTY + 1
        else
            title_sel = difficulty
            fn.clamp_title_sel()
        end
        fn.set_emotion("idle", "また来てね")
        return
    end
    if fn.just_pressed(BTN_OP_RIGHT) then
        fn.try_save_game()
        return
    end
    if turn == SENTE and ai_phase == "idle" then
        if fn.just_pressed(BTN_UP) and cursor_r > 1 then
            cursor_r = cursor_r - 1
        elseif fn.just_pressed(BTN_DOWN) and cursor_r < BOARD_N then
            cursor_r = cursor_r + 1
        elseif fn.just_pressed(BTN_LEFT) and cursor_c > 1 then
            cursor_c = cursor_c - 1
        elseif fn.just_pressed(BTN_RIGHT) and cursor_c < BOARD_N then
            cursor_c = cursor_c + 1
        elseif fn.just_pressed(BTN_FAR) then
            fn.cycle_hand_sel()
        elseif fn.just_pressed(BTN_NEAR) then
            fn.try_player_action()
        end
    end
    fn.update_ai(dt)
end

function fn.update_result(dt)
    blink = blink + dt
    if fn.just_pressed(BTN_NEAR) then
        fn.start_play()
    elseif fn.just_pressed(BTN_OP_LEFT) then
        mode = "title"
        fn.refresh_has_save()
        title_sel = difficulty
        fn.clamp_title_sel()
    end
end

function game_update(dt)
    if mode == "quit" then
        return true
    end
    if mode == "title" then
        fn.update_title(dt)
    elseif mode == "play" then
        fn.update_play(dt)
    elseif mode == "result" then
        fn.update_result(dt)
    end
    return false
end

function fn.piece_label(p)
    return PIECE_LABEL[fn.type_of(p)] or "?"
end

function fn.draw_piece(x, y, p)
    local s = fn.side_of(p)
    local bg = s == SENTE and COL_SENTE or COL_GOTE
    local fg = COL_SENTE_T
    -- 成り駒（P_* + PROMOTE）のときは文字色を赤にする
    if fn.type_of(p) > PROMOTE then
        fg = COL_BAD
    end
    local pad = 1 -- 20x20 のセル内で 17x17 を描画する
    if koma_id then
        -- 後手は 180 度回転して、先手と見分けやすくする
        if s == GOTE then
            machine.draw_image_affine(
                koma_id,
                -1, 0, x + pad + KOMA_W - 1,
                0, -1, y + pad + KOMA_H - 1,
                true
            )
        else
            -- Koma.bin は元画像のマゼンタ(0xF81F)をキーにして背景を抜く
            machine.draw_image_keyed(koma_id, x + pad, y + pad, 0xF81F)
        end
    else
        -- 画像がロードできないときのフォールバック（旧: 矩形）
        machine.fill_rect(x + pad, y + pad, KOMA_W, KOMA_H, bg)
        machine.draw_line(x + pad, y + pad, x + CELL - pad - 2, y + pad, COL_GRID)
    end
    local label = fn.piece_label(p)
    local tw = machine.font_advance()
    local th = machine.font_height()
    -- フォントは左上基準なので、現在のフォント寸法に合わせて中心へ寄せる
    machine.text(
        x + pad + (KOMA_W - tw) // 2,
        y + pad + (KOMA_H - th) // 2,
        label,
        fg
    )
end

function fn.draw_board()
    if not machine.rect_in_band(BOARD_Y - 2, BOARD_PX + 4) then
        return
    end
    machine.fill_rect(BOARD_X - 3, BOARD_Y - 3, BOARD_PX + 6, BOARD_PX + 6, COL_PANEL_EDGE)
    for r = 1, BOARD_N do
        for c = 1, BOARD_N do
            local x = BOARD_X + (c - 1) * CELL
            local y = BOARD_Y + (r - 1) * CELL
            local col = ((r + c) % 2 == 0) and COL_BOARD or COL_BOARD2
            machine.fill_rect(x, y, CELL - 1, CELL - 1, col)
            local p = board[r][c]
            if p ~= EMPTY then
                fn.draw_piece(x, y, p)
            end
        end
    end
    -- 合法ハイライト（キャッシュ済み・毎バンド再計算しない）
    if mode == "play" and turn == SENTE and #hl_moves > 0 then
        local col = (hl_kind == "drop") and COL_SEL or COL_HINT
        for i = 1, #hl_moves do
            local m = hl_moves[i]
            local x = BOARD_X + (m.tc - 1) * CELL
            local y = BOARD_Y + (m.tr - 1) * CELL
            machine.fill_rect(x + CELL // 2 - 2, y + CELL // 2 - 2, 4, 4, col)
        end
    end
    if selected then
        local x = BOARD_X + (selected.c - 1) * CELL
        local y = BOARD_Y + (selected.r - 1) * CELL
        machine.draw_line(x, y, x + CELL - 2, y, COL_SEL)
        machine.draw_line(x, y, x, y + CELL - 2, COL_SEL)
        machine.draw_line(x + CELL - 2, y, x + CELL - 2, y + CELL - 2, COL_SEL)
        machine.draw_line(x, y + CELL - 2, x + CELL - 2, y + CELL - 2, COL_SEL)
    end
    if mode == "play" and turn == SENTE then
        local x = BOARD_X + (cursor_c - 1) * CELL
        local y = BOARD_Y + (cursor_r - 1) * CELL
        if (anim_t // 200) % 2 == 0 then
            machine.draw_line(x, y, x + CELL - 2, y, COL_CURSOR)
            machine.draw_line(x, y, x, y + CELL - 2, COL_CURSOR)
            machine.draw_line(x + CELL - 2, y, x + CELL - 2, y + CELL - 2, COL_CURSOR)
            machine.draw_line(x, y + CELL - 2, x + CELL - 2, y + CELL - 2, COL_CURSOR)
        end
    end
    if ai_phase == "idea" and ai_move then
        local x = BOARD_X + (ai_move.tc - 1) * CELL
        local y = BOARD_Y + (ai_move.tr - 1) * CELL
        machine.fill_rect(x + 1, y + 1, CELL - 3, CELL - 3, machine.rgb(255, 120, 80))
    end
end

function fn.hand_str(side)
    local s = ""
    for i = 1, #HAND_ORDER do
        local t = HAND_ORDER[i]
        local n = hand[side][t] or 0
        if n > 0 then
            if s ~= "" then
                s = s .. " "
            end
            s = s .. (PIECE_LABEL[t] or "?") .. tostring(n)
        end
    end
    if s == "" then
        return "-"
    end
    return s
end

function fn.emotion_label()
    if emotion == "idea" then
        return font_ok and "妙案" or "IDEA"
    elseif emotion == "despair" then
        return font_ok and "絶望" or "DESPAIR"
    elseif emotion == "thinking" then
        return font_ok and "思考中" or "THINK"
    end
    return font_ok and "待機" or "WAIT"
end

function fn.draw_momiji()
    local id = momiji_ids[emotion] or momiji_ids.thinking or momiji_ids.idea or momiji_ids.despair
    if id then
        machine.draw_image_keyed(id, MOMIJI_X, MOMIJI_Y, 0xF81F)
    elseif momiji_fallback and machine.rect_in_band(MOMIJI_Y, MOMIJI_H) then
        machine.fill_rect(MOMIJI_X, MOMIJI_Y, MOMIJI_W, MOMIJI_H, machine.rgb(180, 80, 60))
    end
    if machine.rect_in_band(LINE_Y - 2, LINE_H + 4) then
        machine.fill_rect(LINE_X - 2, LINE_Y - 2, LINE_W + 4, LINE_H + 4, COL_PANEL_EDGE)
        machine.fill_rect(LINE_X, LINE_Y, LINE_W, LINE_H, COL_PANEL)
        -- 吹き出しの先端（上のもみじ側へ）
        machine.fill_rect(LINE_X + LINE_W // 2 - 3, LINE_Y - 6, 6, 6, COL_PANEL)
        machine.text(LINE_X + 4, LINE_Y + 4, fn.emotion_label(), COL_ACCENT)
        machine.text(LINE_X + 4, LINE_Y + 20, line_text, COL_HUD)
    end
end

function fn.draw_hud()
    if not machine.rect_in_band(0, 26) then
        return
    end
    machine.fill_rect(0, 0, W, 22, machine.rgb(8, 18, 24))
    machine.text(6, 4, font_ok and ("LV:" .. fn.diff_label()) or ("LV:" .. fn.diff_label()), COL_ACCENT)
    if mode == "play" then
        local t = turn == SENTE and (font_ok and "あなたの番" or "YOUR TURN")
            or (font_ok and "もみじの番" or "AI TURN")
        machine.text(110, 4, t, COL_HUD)
    end
end

-- UTF-8 文字数（MISF はだいたい等幅なので advance * 文字数で幅を見積もる）
function fn.utf8_len(s)
    local n = 0
    local i = 1
    local len = #s
    while i <= len do
        local b = string.byte(s, i)
        if b < 0x80 then
            i = i + 1
        elseif b < 0xE0 then
            i = i + 2
        elseif b < 0xF0 then
            i = i + 3
        else
            i = i + 4
        end
        n = n + 1
    end
    return n
end

-- MISF 美咲: ASCII/半角は default_advance、日本語などはその 2 倍（advance=16）
function fn.text_width(s)
    local adv = machine.font_advance()
    if not adv or adv <= 0 then
        adv = 8
    end
    local full = adv * 2
    local w = 0
    local i = 1
    local len = #s
    while i <= len do
        local b = string.byte(s, i)
        if b < 0x80 then
            if b ~= 10 then -- \n は幅に含めない
                w = w + adv
            end
            i = i + 1
        elseif b < 0xE0 then
            w = w + full
            i = i + 2
        elseif b < 0xF0 then
            w = w + full
            i = i + 3
        else
            w = w + full
            i = i + 4
        end
    end
    return w
end

function fn.draw_centered_text(cx, cy, s, color)
    local tw = fn.text_width(s)
    local th = machine.font_height()
    if not th or th <= 0 then
        th = 8
    end
    -- cx,cy は枠の中心。左上起点へ半幅・半高を引いて真の中央に置く
    machine.text(cx - tw // 2, cy - th // 2, s, color)
end

function fn.draw_hands()
    local y = BOARD_Y + BOARD_PX + 6
    if not machine.rect_in_band(y - 2, 32) then
        return
    end
    -- 盤幅(180)だと「飛2 角2 金4 銀4 桂4 香4 歩18」で枠をはみ出すため右端まで伸ばす
    local hand_w = W - BOARD_X - 4
    fn.draw_panel_box(BOARD_X, y, hand_w, 28)
    machine.text(BOARD_X + 2, y + 2, (font_ok and "持駒 " or "YOU ") .. fn.hand_str(SENTE), COL_HUD)
    machine.text(BOARD_X + 2, y + 14, (font_ok and "相手 " or "AI  ") .. fn.hand_str(GOTE), COL_DIM)
end

-- 椛コメント欄の下: 対局中は FAR/記録ヒント、勝敗後はリトライ案内
function fn.draw_side_hint()
    if not machine.rect_in_band(HINT_Y - 2, 32) then
        return
    end
    local msg
    local col = COL_DIM
    if mode == "result" then
        if (blink // 400) % 2 ~= 0 then
            return
        end
        msg = font_ok and "NEARでもう一度" or "NEAR: RETRY"
        col = COL_HINT
    elseif mode == "play" then
        if hand_sel ~= 0 then
            msg = (font_ok and "打:" or "DROP ") .. (PIECE_LABEL[hand_sel] or "?")
            col = COL_SEL
        else
            msg = font_ok and "FAR:持ち駒" or "FAR:HAND"
        end
        fn.draw_panel_box(LINE_X, HINT_Y, LINE_W, 28)
        machine.text(LINE_X + 2, HINT_Y + 2, msg, col)
        machine.text(LINE_X + 2, HINT_Y + 14, font_ok and "OP_R:記録" or "OP_R:SAVE", COL_DIM)
        return
    else
        return
    end
    fn.draw_panel_box(LINE_X, HINT_Y, LINE_W, 14)
    machine.text(LINE_X + 2, HINT_Y + 2, msg, col)
end

function fn.draw_backdrop()
    if bg_id then
        -- 純整数倍スケール（録画対応）。一般アフィンは使わない
        machine.draw_image_affine(bg_id, BG_SCALE, 0, 0, 0, BG_SCALE, 0)
    else
        machine.clear(COL_BG)
    end
end

function fn.draw_title()
    fn.draw_backdrop()
    fn.draw_momiji()

    -- タイトル（椛コメント同系パネル）
    if machine.rect_in_band(27, 24) then
        fn.draw_panel_box(TITLE_BOX_X, 29, TITLE_BOX_W, 18)
        machine.text(28, 33, font_ok and "将棋 vs もみじ" or "SHOGI vs MOMIJI", COL_ACCENT)
    end

    -- 難易度／つづきから（save で高さが変わる）
    local menu_top = 54
    local menu_h = 22 + #DIFFICULTY * 22
    if has_save then
        menu_h = menu_h + 34
    end
    if machine.rect_in_band(menu_top - 2, menu_h + 4) then
        fn.draw_panel_box(TITLE_BOX_X, menu_top, TITLE_BOX_W, menu_h)
        machine.text(28, 56, font_ok and "レベルをえらんでね" or "SELECT LEVEL", COL_HUD)
        for i = 1, #DIFFICULTY do
            local cfg = DIFFICULTY[i]
            local y = 78 + (i - 1) * 22
            local name = font_ok and cfg.name_jp or cfg.name_en
            if i == title_sel then
                machine.fill_rect(TITLE_SEL_X, y - 2, TITLE_SEL_W, 18, COL_PANEL_EDGE)
                machine.fill_rect(TITLE_SEL_X + 2, y, TITLE_SEL_W - 4, 14, COL_PANEL)
                machine.text(34, y + 2, "> " .. name, COL_ACCENT)
            else
                machine.text(40, y + 2, "  " .. name, COL_DIM)
            end
        end
        if has_save then
            local y = 78 + #DIFFICULTY * 22
            local scfg = DIFFICULTY[save_difficulty] or DIFFICULTY[2]
            local dname = font_ok and scfg.name_jp or scfg.name_en
            local title = font_ok and "つづきから" or "RESUME"
            local sub = "[" .. dname .. "]"
            if title_sel == #DIFFICULTY + 1 then
                machine.fill_rect(TITLE_SEL_X, y - 2, TITLE_SEL_W, 32, COL_PANEL_EDGE)
                machine.fill_rect(TITLE_SEL_X + 2, y, TITLE_SEL_W - 4, 28, COL_PANEL)
                machine.text(34, y + 2, "> " .. title, COL_ACCENT)
                machine.text(50, y + 16, sub, COL_HINT)
            else
                machine.text(40, y + 2, "  " .. title, COL_DIM)
                machine.text(56, y + 16, sub, COL_DIM)
            end
        end
    end

    -- 操作ヒント（下部固定・幅広パネル）
    if machine.rect_in_band(TITLE_FOOTER_Y - 4, TITLE_FOOTER_H) then
        fn.draw_panel_box(TITLE_FOOTER_X, TITLE_FOOTER_Y, TITLE_FOOTER_W, TITLE_FOOTER_H - 4)
        if (blink // 400) % 2 == 0 then
            machine.text(
                28,
                TITLE_FOOTER_Y + 4,
                font_ok and "NEAR で決定" or "PRESS NEAR",
                COL_HINT
            )
        end
        machine.text(
            28,
            TITLE_FOOTER_Y + 20,
            font_ok and "上下:えらぶ  OP_L:おわる" or "UP/DN  OP_L QUIT",
            COL_DIM
        )
        machine.text(
            28,
            TITLE_FOOTER_Y + 34,
            font_ok and "本将棋 9x9" or "9x9 SHOGI",
            COL_DIM
        )
    end
end

function fn.draw_play()
    fn.draw_backdrop()
    fn.draw_hud()
    fn.draw_board()
    fn.draw_hands()
    fn.draw_momiji()
    fn.draw_side_hint()
end

function fn.draw_result()
    fn.draw_backdrop()
    fn.draw_hud()
    fn.draw_board()
    fn.draw_hands()
    fn.draw_momiji()
    fn.draw_side_hint()
    if machine.rect_in_band(RESULT_PANEL_Y - 2, RESULT_PANEL_H + 4) then
        machine.fill_rect(
            RESULT_PANEL_X - 2,
            RESULT_PANEL_Y - 2,
            RESULT_PANEL_W + 4,
            RESULT_PANEL_H + 4,
            COL_PANEL_EDGE
        )
        machine.fill_rect(RESULT_PANEL_X, RESULT_PANEL_Y, RESULT_PANEL_W, RESULT_PANEL_H, COL_PANEL)
        local msg
        if winner == SENTE then
            msg = font_ok and "あなたの勝ち！" or "YOU WIN!"
        elseif winner == GOTE then
            msg = font_ok and "もみじの勝ち！" or "MOMIJI WINS!"
        else
            msg = font_ok and "引き分け" or "DRAW"
        end
        fn.draw_centered_text(
            RESULT_PANEL_X + RESULT_PANEL_W // 2,
            RESULT_PANEL_Y + RESULT_PANEL_H // 2,
            msg,
            COL_ACCENT
        )
    end
end

function game_draw()
    if mode == "title" then
        fn.draw_title()
    elseif mode == "play" then
        fn.draw_play()
    elseif mode == "result" then
        fn.draw_result()
    else
        machine.clear(COL_BG)
    end
end

function game_init()
    font_ok = machine.load_font("fonts/game_font.bin") == true
    local specs = {
        thinking = "img/Momiji_thinking.bin",
        idea = "img/Momiji_idea.bin",
        despair = "img/Momiji_despair.bin",
    }
    for k, path in pairs(specs) do
        local id = machine.load_image(path, MOMIJI_W, MOMIJI_H)
        if id then
            momiji_ids[k] = id
        else
            print("momiji load fail:", path)
            momiji_fallback = true
        end
    end
    -- img/Koma.bin（駒画像）をロード
    koma_id = machine.load_image("img/Koma.bin", KOMA_W, KOMA_H)
    if not koma_id then
        print("koma load fail: img/Koma.bin")
    end
    bg_id = machine.load_image(BG_PATH, BG_W, BG_H)
    if not bg_id then
        print("bg load fail:", BG_PATH)
    end
    fn.clear_hand(SENTE)
    fn.clear_hand(GOTE)
    mode = "title"
    blink = 0
    fn.refresh_has_save()
    title_sel = difficulty
    fn.clamp_title_sel()
    fn.set_emotion("thinking", font_ok and "将棋\n指しませんか？" or "Let's play!")
    prev_pressed = {}
end
