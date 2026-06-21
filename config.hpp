// ============================================
// ファイル: config.hpp
// ピン配置とハードウェア設定（ボード変更は CFG_* のみ編集）
// ============================================

#ifndef CONFIG_HPP
#define CONFIG_HPP

// ============================================================
// ボード GPIO / SPI — ここだけ編集（C / C++ 共通）
// ============================================================

// I2C1 + IO エキスパンダ (PCA9539)
#define CFG_I2C_SDA           6
#define CFG_I2C_SCL           7
#define CFG_I2C_INT           8
#define CFG_I2C_RST           9
#define CFG_I2C_BAUD_HZ       (100u * 1000u)
#define CFG_PCA9539_ADDR      0x74u

// SD カード (SPI1) — hw_config.c の .hw_inst は spi1 固定（SPI1 使用時）
#define CFG_SD_PIN_CLK        10
#define CFG_SD_PIN_MOSI       11
#define CFG_SD_PIN_MISO       12
#define CFG_SD_PIN_CS         13
#define CFG_SD_PIN_INSERT     0
#define CFG_SD_PIN_POWER      15
#define CFG_SD_SPI_BAUD_HZ    (60u * 1000u * 1000u)
#define CFG_SD_SPI_MODE       3

// LCD ST7789 (SPI0) — st7789_lcd の .hw_inst は spi0 固定（SPI0 使用時）
#define CFG_LCD_PIN_CS        1
#define CFG_LCD_PIN_SCK       2
#define CFG_LCD_PIN_MOSI      3
#define CFG_LCD_PIN_RST       4
#define CFG_LCD_PIN_DC        5
#define CFG_LCD_PIN_BLK       14
#define CFG_LCD_SPI_BAUD_HZ   (625u * 1000u * 1000u)
#define CFG_LCD_PHYSICAL_WIDTH   240
#define CFG_LCD_PHYSICAL_HEIGHT  320
#define CFG_LCD_DEFAULT_ROTATION 1

//エンコーダー関連ピン
#define CFG_ENCODER_PIN_A     17
#define CFG_ENCODER_PIN_B     16
#define CFG_ENCODER_PIN_SW    26

// 音声 (PCM5102 I2S)
#define CFG_AUDIO_SAMPLE_RATE     44100u
/** エンコーダ音量とは別の固定ゲイン（1.0=等倍）。小さい場合は 2.0〜3.0 程度に上げる */
#define CFG_AUDIO_CODE_VOLUME_GAIN  0.6f

/** I2S 信号 */
#define CFG_AUDIO_I2S_PIN_BCK     21
#define CFG_AUDIO_I2S_PIN_LRCK    20
#define CFG_AUDIO_I2S_PIN_DATA    19
/**
 * PCM5102A XSMT（ソフトミュート）を Pico GPIO で駆動する場合のピン番号。
 * XSMT=HIGH でアンミュート。基板で 3.3V 固定なら -1。
 * ※ DEMP/FLT/SCK/FMT とは別ピン。XSMT を GND のままにすると無音。
 */
#define CFG_AUDIO_I2S_PIN_XSMT    18
/** XSMT アンミュート時の GPIO レベル（1=HIGH=アンミュート, 0=LOW=アンミュートの基板向け） */
#define CFG_AUDIO_I2S_XSMT_UNMUTE_LEVEL  1
/** 旧基板用の第 2 ミュート線。未使用なら -1 */
#define CFG_AUDIO_I2S_PIN_SPMUTE  (-1)

// 起動スプラッシュ（BootSplash）
/** ロゴ最低表示時間 (ms)。BGM が短い場合もこの時間は表示を続ける */
#define CFG_BOOT_SPLASH_MIN_DISPLAY_MS   2200u
/** 1=ボタンでスキップ可能、0=不可 */
#define CFG_BOOT_SPLASH_SKIPPABLE          1
/** スキップ入力を有効にする最短表示時間 (ms) */
#define CFG_BOOT_SPLASH_SKIP_MIN_MS        300u


// バッテリー ADC
#define CFG_BATTERY_PIN_ADC   28
#define CFG_BATTERY_ADC_CH    2
/** 残量 LED パルス表示時間 (ms)。Pulse モード時、起動・残量変化後にこの時間だけ点灯 */
#define CFG_BATTERY_LED_PULSE_MS  1000u

// --- C 互換マクロ（lib/sd_card_hw/*.c） ---
#define SD_PIN_CLK    CFG_SD_PIN_CLK
#define SD_PIN_MOSI   CFG_SD_PIN_MOSI
#define SD_PIN_MISO   CFG_SD_PIN_MISO
#define SD_PIN_CS     CFG_SD_PIN_CS
#define SD_PIN_POWER  CFG_SD_PIN_POWER

#ifdef __cplusplus

#include <cstddef>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"

namespace I2CConfig {
    constexpr i2c_inst_t* PORT = i2c1;
    constexpr uint8_t SDA = CFG_I2C_SDA;
    constexpr uint8_t SCL = CFG_I2C_SCL;
    constexpr uint8_t INT = CFG_I2C_INT;
    constexpr uint8_t RST = CFG_I2C_RST;
    constexpr uint32_t BAUD_RATE = CFG_I2C_BAUD_HZ;
    constexpr uint8_t PCA9539_ADDR = CFG_PCA9539_ADDR;

    constexpr uint8_t BUTTON_RIGHT = 0;
    constexpr uint8_t BUTTON_UP = 1;
    constexpr uint8_t BUTTON_LEFT = 2;
    constexpr uint8_t BUTTON_DOWN = 3;
    constexpr uint8_t BUTTON_OP_LEFT = 4;
    constexpr uint8_t BUTTON_OP_RIGHT = 5;
    constexpr uint8_t BUTTON_FAR = 6;
    constexpr uint8_t BUTTON_NEAR = 7;
}

namespace SDConfig {
    constexpr uint8_t PIN_CLK = CFG_SD_PIN_CLK;
    constexpr uint8_t PIN_MOSI = CFG_SD_PIN_MOSI;
    constexpr uint8_t PIN_MISO = CFG_SD_PIN_MISO;
    constexpr uint8_t PIN_CS = CFG_SD_PIN_CS;
    constexpr uint8_t PIN_INSERT = CFG_SD_PIN_INSERT;
    constexpr uint8_t PIN_SD_POWER = CFG_SD_PIN_POWER;
    constexpr uint32_t SPI_BAUD_HZ = CFG_SD_SPI_BAUD_HZ;
    constexpr uint8_t SPI_MODE = CFG_SD_SPI_MODE;
    /** SPI インスタンス（hw_config.c の spi1 と一致） */
    inline spi_inst_t* spiHw() { return spi1; }
}

namespace LCDConfig {
    constexpr uint8_t PIN_CS = CFG_LCD_PIN_CS;
    constexpr uint8_t PIN_SCK = CFG_LCD_PIN_SCK;
    constexpr uint8_t PIN_MOSI = CFG_LCD_PIN_MOSI;
    constexpr uint8_t PIN_RST = CFG_LCD_PIN_RST;
    constexpr uint8_t PIN_DC = CFG_LCD_PIN_DC;
    constexpr uint8_t PIN_BLK = CFG_LCD_PIN_BLK;
    constexpr uint32_t SPI_BAUD_HZ = CFG_LCD_SPI_BAUD_HZ;
    constexpr uint16_t PHYSICAL_WIDTH = CFG_LCD_PHYSICAL_WIDTH;
    constexpr uint16_t PHYSICAL_HEIGHT = CFG_LCD_PHYSICAL_HEIGHT;
    constexpr uint8_t DEFAULT_ROTATION = CFG_LCD_DEFAULT_ROTATION;
    /** SPI インスタンス（st7789_lcd の spi_port と一致） */
    inline spi_inst_t* spiHw() { return spi0; }
}

namespace EncoderConfig {
    constexpr uint8_t PIN_A = CFG_ENCODER_PIN_A;
    constexpr uint8_t PIN_B = CFG_ENCODER_PIN_B;
    constexpr uint8_t PIN_SW = CFG_ENCODER_PIN_SW;
}

namespace AudioConfig {
    constexpr uint32_t SAMPLE_RATE = CFG_AUDIO_SAMPLE_RATE;
    /** エンコーダ / Lua set_volume とは独立。最終出力 = ユーザー音量 × この値 */
    constexpr float CODE_VOLUME_GAIN = CFG_AUDIO_CODE_VOLUME_GAIN;
    /** Core0 ストリーム / Core1 I2S DMA の 1 チャンクあたりフレーム数 */
    constexpr size_t STREAM_BUFFER_FRAMES = 256;
    /** SE 同時再生数（超過時は最も古い SE を上書き） */
    constexpr size_t SE_CHANNEL_COUNT = 8;
    /** 1 SE あたりの最大バイト数（SD から RAM 載せ / 埋め込み SE 共通） */
    constexpr size_t SE_MAX_BYTES = 96 * 1024;

    /** PCM5102 I2S（GP19=DIN, GP20=LRCK, GP21=BCK, GP18=XSMT 任意） */
    namespace I2S {
        constexpr uint8_t PIN_BCK = CFG_AUDIO_I2S_PIN_BCK;
        constexpr uint8_t PIN_LRCK = CFG_AUDIO_I2S_PIN_LRCK;
        constexpr uint8_t PIN_DATA = CFG_AUDIO_I2S_PIN_DATA;
        constexpr int8_t PIN_XSMT = CFG_AUDIO_I2S_PIN_XSMT;
        constexpr int8_t PIN_SPMUTE = CFG_AUDIO_I2S_PIN_SPMUTE;
        constexpr bool XSMT_UNMUTE_LEVEL = (CFG_AUDIO_I2S_XSMT_UNMUTE_LEVEL != 0);
    }
}


namespace BootSplashConfig {
    constexpr uint32_t MIN_DISPLAY_MS = CFG_BOOT_SPLASH_MIN_DISPLAY_MS;
    constexpr bool SKIPPABLE = (CFG_BOOT_SPLASH_SKIPPABLE != 0);
    constexpr uint32_t SKIP_MIN_MS = CFG_BOOT_SPLASH_SKIP_MIN_MS;
}

namespace BatteryConfig {
    constexpr uint8_t PIN_ADC = CFG_BATTERY_PIN_ADC;
    constexpr uint8_t ADC_CHANNEL = CFG_BATTERY_ADC_CH;
    /** ADC 基準電圧 (V) */
    constexpr float ADC_VREF = 3.3f;
    /** 12bit ADC 最大値 */
    constexpr uint16_t ADC_MAX = 4095;
    /** Core1 でのサンプリング周期 (ms) */
    constexpr uint32_t SAMPLE_INTERVAL_MS = 100;
    /** FULL 表示: V >= この値 */
    constexpr float THRESHOLD_FULL_V = 1.1f;
    /** MID 表示: THRESHOLD_MID_V <= V < THRESHOLD_FULL_V */
    constexpr float THRESHOLD_MID_V = 0.9f;
    /** Pulse モード: 起動・残量変化後に LED を点灯する時間 (ms) */
    constexpr uint32_t LED_PULSE_MS = CFG_BATTERY_LED_PULSE_MS;
}

namespace BatteryLedConfig {
    constexpr uint8_t LED_COUNT = 3;
    constexpr uint8_t PORT1_MASK = 0x07;

    constexpr uint8_t PIN_FULL = 0;
    constexpr uint8_t PIN_MID = 1;
    constexpr uint8_t PIN_LOW = 2;

    constexpr uint8_t MASK_OFF = 0x00;
    constexpr uint8_t MASK_FULL = 0x01;
    constexpr uint8_t MASK_MID = 0x02;
    constexpr uint8_t MASK_LOW = 0x04;

    constexpr uint8_t LEVEL_EMPTY = 0;
    constexpr uint8_t LEVEL_LOW = 1;
    constexpr uint8_t LEVEL_MID = 2;
    constexpr uint8_t LEVEL_FULL = 3;
}

/** 動的ヒープ（malloc / Lua）の全体予算。静的 RAM・スタックを除いた目安値 */
/*数値の目安表
BUDGET_BYTES	評価
256KB（現状）    安全側。実運用でクラッシュしにくい
280〜300KB      ゲーム専用なら試す価値あり。実機で machine.heap_used() と長時間プレイで確認が必要
320KB 以上      スタック・予算外・断片化で危険域
384KB           物理上限を超えやすく、現実的には不可に近い*/
namespace HeapConfig {
    constexpr size_t BUDGET_BYTES = 256 * 1024;
    /** malloc 失敗前に残しておく安全マージン */
    constexpr size_t RESERVE_BYTES = 8 * 1024;
}

namespace GameConfig {
    constexpr const char* SD_ROOT = "";
    constexpr const char* GAMES_DIR = "/games";
    constexpr const char* IMAGES_DIR = "/images";
    constexpr const char* AUDIO_DIR = "/audio";

    constexpr uint16_t SCREEN_WIDTH = 320;
    constexpr uint16_t SCREEN_HEIGHT = 240;
    constexpr uint16_t BUFFER_WIDTH = SCREEN_WIDTH;
    constexpr uint16_t BUFFER_HEIGHT = 20;
    constexpr uint16_t MAX_HEIGHT_MATH = SCREEN_HEIGHT / BUFFER_HEIGHT;

    /** GBA 風タイル背景レイヤー数（0=最背面） */
    constexpr size_t TILE_LAYER_COUNT = 4;
    /** 1 レイヤーあたりのタイルマップ最大セル数 */
    constexpr size_t TILEMAP_MAX_CELLS = 2048;
}

#endif  // __cplusplus

#endif  // CONFIG_HPP
