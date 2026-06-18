// ============================================
// ファイル: encoder_input.hpp
// ロータリーエンコーダ（A/B + SW）入力
// ============================================

#ifndef ENCODER_INPUT_HPP
#define ENCODER_INPUT_HPP

#include <cstdint>

/** GP16/17/26 のクアドラチャエンコーダ */
class EncoderInput {
public:
    /** ポーリング用（A/B は update() で読む） */
    bool init();
    /** A/B を GPIO 割り込みでデコード（テストモード向け） */
    bool initIrq();
    /** A/B の GPIO 割り込みを無効化する */
    void disableIrq();

    /** SW ポーリング（A/B は IRQ 有効時は割り込み側で処理） */
    void update();

    /** GPIO 共有 IRQ コールバックから呼ぶ（initIrq 使用時） */
    void serviceIrq(unsigned int gpio);

    /** 累積回転位置（カウント）を返す */
    int32_t position() const;
    /** 前回 consume 以降の累積デルタを返し、内部カウンタを 0 にする */
    int32_t consumeDelta();

    /** エンコーダ SW が押下中か（アクティブロー） */
    bool isSwitchPressed() const;
    /** 前回 update 以降に SW が押されたエッジがあれば true を返し消費する */
    bool wasSwitchPressed();
    /** SW 押下回数（デバッグ用）を返す */
    uint32_t switchPressCount() const { return switch_press_count_; }

    /** A 相ピンの現在レベルを返す（デバッグ表示用） */
    bool pinA() const;
    /** B 相ピンの現在レベルを返す（デバッグ表示用） */
    bool pinB() const;

private:
    /** A/B/SW ピン初期化とクアドラチャデコード */
    void initPins();
    /** グレイコード遷移から回転方向を 1 ステップ分デコードする */
    void decodeQuadratureStep();

    uint8_t last_ab_state_ = 0;
    volatile int32_t position_ = 0;
    volatile int32_t pending_delta_ = 0;
    bool switch_last_ = false;
    bool switch_pressed_edge_ = false;
    uint32_t switch_press_count_ = 0;
    bool irq_enabled_ = false;
};

#endif  // ENCODER_INPUT_HPP
