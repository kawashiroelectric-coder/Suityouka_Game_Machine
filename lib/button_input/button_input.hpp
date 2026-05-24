// ============================================
// ファイル: button_input.hpp
// ボタン入力管理クラス（IOエキスパンダ経由）
// ============================================

#ifndef BUTTON_INPUT_HPP
#define BUTTON_INPUT_HPP

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "config.hpp"

/** 8 ボタン（PCA9539 Port0、ビット 0～7、アクティブロー） */
enum class Button {
    RIGHT = 0,
    UP = 1,
    LEFT = 2,
    DOWN = 3,
    OP_LEFT = 4,
    OP_RIGHT = 5,
    FAR = 6,
    NEAR = 7
};

/** PCA9539 経由のボタン入力 */
class ButtonInput {
private:
    i2c_inst_t* i2c_port;
    uint8_t i2c_addr;
    uint8_t last_state;
    uint8_t current_state;
    bool irq_enabled;
    
    static constexpr uint8_t REG_INPUT_PORT0 = 0x00;
    static constexpr uint8_t REG_INPUT_PORT1 = 0x01;
    static constexpr uint8_t REG_OUTPUT_PORT0 = 0x02;
    static constexpr uint8_t REG_OUTPUT_PORT1 = 0x03;
    static constexpr uint8_t REG_POLARITY_PORT0 = 0x04;
    static constexpr uint8_t REG_POLARITY_PORT1 = 0x05;
    static constexpr uint8_t REG_CONFIG_PORT0 = 0x06;
    static constexpr uint8_t REG_CONFIG_PORT1 = 0x07;
    
    /** PCA9539 レジスタへ 1 バイト書き込む */
    bool writeRegister(uint8_t reg, uint8_t value);
    /** PCA9539 レジスタから 1 バイト読み込む */
    bool readRegister(uint8_t reg, uint8_t* value);
    
public:
    ButtonInput(i2c_inst_t* port, uint8_t addr);
    
    /** Port0 を入力、Port1 を出力に設定する */
    bool init();
    
    /** I2C から Port0 を読み、current_state / last_state を更新する */
    void update();
    
    /** 指定ボタンが押下中か（アクティブロー） */
    bool isPressed(Button button) const;
    
    /** 直前の update 以降に離されたか（エッジ検出） */
    bool wasReleased(Button button) const;
    
    /** 直前の update 以降に押されたか（エッジ検出） */
    bool wasPressed(Button button) const;
    
    /** Port0 の生ビットマスク（1=未押下） */
    uint8_t getAllButtons() const { return current_state; }
    
    /** 前回 update 時の Port0 状態 */
    uint8_t getLastState() const { return last_state; }
};

#endif // BUTTON_INPUT_HPP
