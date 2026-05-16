// ============================================
// ファイル: button_input.hpp
// ボタン入力管理クラス（IOエキスパンダ経由）
// ============================================

#ifndef BUTTON_INPUT_HPP
#define BUTTON_INPUT_HPP

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "config.hpp"

// ボタン定義
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

// ボタン入力管理クラス
class ButtonInput {
private:
    i2c_inst_t* i2c_port;
    uint8_t i2c_addr;
    uint8_t last_state;
    uint8_t current_state;
    bool irq_enabled;
    
    // PCA9539レジスタアドレス
    static constexpr uint8_t REG_INPUT_PORT0 = 0x00;
    static constexpr uint8_t REG_INPUT_PORT1 = 0x01;
    static constexpr uint8_t REG_OUTPUT_PORT0 = 0x02;
    static constexpr uint8_t REG_OUTPUT_PORT1 = 0x03;
    static constexpr uint8_t REG_POLARITY_PORT0 = 0x04;
    static constexpr uint8_t REG_POLARITY_PORT1 = 0x05;
    static constexpr uint8_t REG_CONFIG_PORT0 = 0x06;
    static constexpr uint8_t REG_CONFIG_PORT1 = 0x07;
    
    // レジスタ読み書き
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegister(uint8_t reg, uint8_t* value);
    
public:
    ButtonInput(i2c_inst_t* port, uint8_t addr);
    
    // 初期化
    bool init();
    
    // ボタン状態の更新（定期的に呼び出す）
    void update();
    
    // ボタンが押されているか
    bool isPressed(Button button) const;
    
    // ボタンが離されたか（エッジ検出）
    bool wasReleased(Button button) const;
    
    // ボタンが押されたか（エッジ検出）
    bool wasPressed(Button button) const;
    
    // 全ボタンの状態を取得（ビットマスク）
    uint8_t getAllButtons() const { return current_state; }
    
    // 前回の状態を取得
    uint8_t getLastState() const { return last_state; }
};

#endif // BUTTON_INPUT_HPP
