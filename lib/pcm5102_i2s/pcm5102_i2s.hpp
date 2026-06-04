// ============================================
// ファイル: pcm5102_i2s.hpp
// PCM5102 向け 32bit I2S PIO（main 未統合・PIO モジュールのみ）
// ============================================
//
// ビルド時に pcm5102_i2s.pio から pcm5102_i2s.pio.h が生成される。
// 初期化: pcm5102_i2s_32_program_init() / pcm5102_i2s_32_start()
//
// DMA 連携（未実装・参考）:
//   - ステレオ int32_t バッファを 32bit 幅で TX FIFO へ
//   - 1 フレーム = L32 + R32 の 2 ワード（64bit）を順に送る

#ifndef PCM5102_I2S_HPP
#define PCM5102_I2S_HPP

#include <cstdint>

#include "config.hpp"
#include "hardware/pio.h"
#include "pcm5102_i2s.pio.h"

/** config.hpp AudioConfig::I2S（PWM 出力ピンと GPIO 重複のため排他利用） */
namespace PCM5102I2S {
constexpr uint8_t PIN_DATA = AudioConfig::I2S::PIN_DATA;
constexpr uint8_t PIN_LRCK = AudioConfig::I2S::PIN_LRCK;
constexpr uint8_t PIN_BCK = AudioConfig::I2S::PIN_BCK;
/** sideset 連続ピンの基点（LRCK = base, BCK = base+1） */
constexpr uint8_t PIN_CLOCK_BASE = PIN_LRCK;

/** デフォルトサンプルレート（AudioConfig と揃えやすい値） */
constexpr uint32_t DEFAULT_SAMPLE_RATE_HZ = AudioConfig::SAMPLE_RATE;
}  // namespace PCM5102I2S

/**
 * PCM5102 用 32bit I2S PIO を 1 本の SM で起動するヘルパ。
 * pio_add_program 済みの offset を渡すこと。
 */
inline void pcm5102_i2s_init_sm(PIO pio, uint sm, uint offset,
                                uint32_t sample_rate_hz = PCM5102I2S::DEFAULT_SAMPLE_RATE_HZ) {
    pcm5102_i2s_32_program_init(pio, sm, offset, PCM5102I2S::PIN_DATA, PCM5102I2S::PIN_CLOCK_BASE,
                                sample_rate_hz);
}

#endif  // PCM5102_I2S_HPP
