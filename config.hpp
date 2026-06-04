// ============================================
// ファイル: config.hpp
// ピン配置とハードウェア設定
// 後から変更可能な設定を集約
// ============================================

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include "pico/stdlib.h"
#include "hardware/i2c.h"

// ============================================
// I2C設定
// ============================================
namespace I2CConfig {
    constexpr i2c_inst_t* PORT = i2c1;
    constexpr uint8_t SDA = 6;
    constexpr uint8_t SCL = 7;
    constexpr uint8_t INT = 8;      // 割り込みピン
    constexpr uint8_t RST = 9;      // リセットピン
    constexpr uint32_t BAUD_RATE = 100 * 1000;  // 100kHz（内蔵プルアップ使用時は低速推奨）
    
    // IOエキスパンダ（PCA9539）のI2Cアドレス
    constexpr uint8_t PCA9539_ADDR = 0x74;
    
    // ボタンマッピング（PCA9539 Port0のビット位置）
    constexpr uint8_t BUTTON_RIGHT = 0;  // P0.0
    constexpr uint8_t BUTTON_UP = 1;     // P0.1
    constexpr uint8_t BUTTON_LEFT = 2;   // P0.2
    constexpr uint8_t BUTTON_DOWN = 3;   // P0.3
    constexpr uint8_t BUTTON_OP_LEFT = 4; // P0.4
    constexpr uint8_t BUTTON_OP_RIGHT = 5; // P0.5
    constexpr uint8_t BUTTON_FAR = 6;     // P0.6
    constexpr uint8_t BUTTON_NEAR = 7;   // P0.7
}

// ============================================
// SDカード設定（SPI1）
// ============================================
namespace SDConfig {
    constexpr uint8_t PIN_CLK = 10;
    constexpr uint8_t PIN_MOSI = 11;
    constexpr uint8_t PIN_MISO = 12;
    constexpr uint8_t PIN_CS = 13;
    constexpr uint8_t PIN_INSERT = 0;  // SDカード検出ピン
    constexpr uint8_t PIN_SD_POWER = 15; // SD電源(Pch FET): LOW=ON, HIGH=OFF（回路図参照）
}

// ============================================
// LCD設定（ST7789 - SPI0）
// ============================================
namespace LCDConfig {
    constexpr uint8_t PIN_CS = 1;
    constexpr uint8_t PIN_SCK = 2;
    constexpr uint8_t PIN_MOSI = 3;
    constexpr uint8_t PIN_RST = 4;
    constexpr uint8_t PIN_DC = 5;
    constexpr uint8_t PIN_BLK = 14;  // バックライト
}

// ============================================
// 音声出力設定
// ============================================
namespace AudioConfig {
    constexpr uint8_t PIN_L_OUT = 21;    // 左チャンネル出力（PWM）
    constexpr uint8_t PIN_R_OUT = 20;    // 右チャンネル出力（PWM）
    constexpr uint8_t PIN_AUDIO_SD = 22;  // オーディオシャットダウン
    constexpr uint8_t PIN_ABD = 26;       // オーディオバイパス/デジタル
    
    // PWM設定
    constexpr uint32_t SAMPLE_RATE = 22050;  // サンプリングレート（Hz）
    constexpr uint16_t PWM_WRAP = 4095;       // PWM分解能（12bit）

    // PCM5102 I2S（PWM 出力 PIN_L_OUT / PIN_R_OUT と GPIO が重なるため排他）
    namespace I2S {
        constexpr uint8_t PIN_BCK = 21;   // ビットクロック（BCK）→ PCM5102 SCK
        constexpr uint8_t PIN_LRCK = 20; // ワードセレクト（LRCK）→ PCM5102 LCK
        constexpr uint8_t PIN_DATA = 19;  // データ（DIN）→ PCM5102 DIN
    }

}

// ============================================
// LED設定
// ============================================
namespace LEDConfig {
    constexpr uint8_t PIN_GREEN = 15;
    constexpr uint8_t PIN_RED = 18;
}

// ============================================
// バッテリーモニター設定
// ============================================
namespace BatteryConfig {
    constexpr uint8_t PIN_ADC = 28;  // ADC2
    constexpr uint8_t ADC_CHANNEL = 2;
}

// 電池残量表示 LED（PCA9539 Port1、同時に 1 灯のみ点灯）
namespace BatteryLedConfig {
    constexpr uint8_t LED_COUNT = 3;
    constexpr uint8_t PORT1_MASK = 0x07;  // P1.0 | P1.1 | P1.2

    constexpr uint8_t PIN_FULL = 0;  // P1.0 … 残量 MAX
    constexpr uint8_t PIN_MID = 1;   // P1.1 … 残量 中
    constexpr uint8_t PIN_LOW = 2;   // P1.2 … 残量 少

    constexpr uint8_t MASK_OFF = 0x00;
    constexpr uint8_t MASK_FULL = 0x01;  // P1.0
    constexpr uint8_t MASK_MID = 0x02;   // P1.1
    constexpr uint8_t MASK_LOW = 0x04;   // P1.2

    /** setBatteryLevel に渡す段階（0=消灯, 1=LOW灯, 2=MID灯, 3=FULL灯） */
    constexpr uint8_t LEVEL_EMPTY = 0;
    constexpr uint8_t LEVEL_LOW = 1;
    constexpr uint8_t LEVEL_MID = 2;
    constexpr uint8_t LEVEL_FULL = 3;
}

// ============================================
// ゲーム機設定
// ============================================
namespace GameConfig {
    constexpr const char* SD_ROOT = "";           // SDカードのルートパス
    constexpr const char* GAMES_DIR = "/games";      // ゲームプログラムのディレクトリ
    constexpr const char* IMAGES_DIR = "/images";   // 画像ファイルのディレクトリ
    constexpr const char* AUDIO_DIR = "/audio";     // 音声ファイルのディレクトリ
    
    constexpr uint16_t SCREEN_WIDTH = 320;
    constexpr uint16_t SCREEN_HEIGHT = 240;
    constexpr uint16_t BUFFER_WIDTH = SCREEN_WIDTH; // RGB565
    constexpr uint16_t BUFFER_HEIGHT = 20; // RGB565
    constexpr uint16_t MAX_HEIGHT_MATH = SCREEN_HEIGHT/BUFFER_HEIGHT; // 液晶を描写するのに必要な数
}

#endif // CONFIG_HPP
