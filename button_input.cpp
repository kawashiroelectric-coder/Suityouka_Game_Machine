// ============================================
// ファイル: button_input.cpp
// ボタン入力管理クラスの実装
// ============================================

#include "button_input.hpp"
#include <cstdio>

ButtonInput::ButtonInput(i2c_inst_t* port, uint8_t addr)
    : i2c_port(port), i2c_addr(addr), last_state(0xFF), current_state(0xFF), irq_enabled(false) {
}

bool ButtonInput::writeRegister(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    int result = i2c_write_blocking(i2c_port, i2c_addr, buf, 2, false);
    return result == 2;
}

bool ButtonInput::readRegister(uint8_t reg, uint8_t* value) {
    int result = i2c_write_blocking(i2c_port, i2c_addr, &reg, 1, true);
    if (result != 1) return false;
    
    result = i2c_read_blocking(i2c_port, i2c_addr, value, 1, false);
    return result == 1;
}

bool ButtonInput::init() {
    // Port0を入力に設定（1=入力, 0=出力）
    if (!writeRegister(REG_CONFIG_PORT0, 0xFF)) {
        printf("ButtonInput: Port0設定失敗\n");
        return false;
    }
    
    // Port1を出力に設定し、出力をLOWに
    if (!writeRegister(REG_CONFIG_PORT1, 0x00)) {
        printf("ButtonInput: Port1設定失敗\n");
        return false;
    }
    if (!writeRegister(REG_OUTPUT_PORT1, 0x00)) {
        printf("ButtonInput: Port1出力設定失敗\n");
        return false;
    }
    
    // Polarityはport0のみ反転（アクティブローなので）
    writeRegister(REG_POLARITY_PORT0, 0xFF);
    writeRegister(REG_POLARITY_PORT1, 0x00);
    
    // 初期状態を読み取る
    update();
    last_state = current_state;
    
    printf("ButtonInput: 初期化完了\n");
    return true;
}

void ButtonInput::update() {
    uint8_t port0_value;
    if (readRegister(REG_INPUT_PORT0, &port0_value)) {
        last_state = current_state;
        current_state = port0_value;
    }
}

bool ButtonInput::isPressed(Button button) const {
    uint8_t bit = static_cast<uint8_t>(button);
    return !(current_state & (1 << bit));  // アクティブローなので反転
}

bool ButtonInput::wasReleased(Button button) const {
    uint8_t bit = static_cast<uint8_t>(button);
    bool was_pressed = !(last_state & (1 << bit));
    bool is_pressed = !(current_state & (1 << bit));
    return was_pressed && !is_pressed;
}

bool ButtonInput::wasPressed(Button button) const {
    uint8_t bit = static_cast<uint8_t>(button);
    bool was_pressed = !(last_state & (1 << bit));
    bool is_pressed = !(current_state & (1 << bit));
    return !was_pressed && is_pressed;
}
