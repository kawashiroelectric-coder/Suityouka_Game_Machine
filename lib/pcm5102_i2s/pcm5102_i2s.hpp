// ============================================
// ファイル: pcm5102_i2s.hpp
// PCM5102A 向け I2S PIO（BCK=64fs, 32bit スロット、Core 1 で使用）
// ============================================

#ifndef PCM5102_I2S_HPP
#define PCM5102_I2S_HPP

#include <cstdint>

#include "config.hpp"
#include "hardware/pio.h"
#include "pcm5102_i2s.pio.h"

namespace PCM5102I2S {
constexpr uint8_t PIN_DATA = AudioConfig::I2S::PIN_DATA;
constexpr uint8_t PIN_LRCK = AudioConfig::I2S::PIN_LRCK;
constexpr uint8_t PIN_BCK = AudioConfig::I2S::PIN_BCK;
constexpr uint8_t PIN_CLOCK_BASE = PIN_LRCK;
constexpr uint32_t DEFAULT_SAMPLE_RATE_HZ = AudioConfig::SAMPLE_RATE;
}  // namespace PCM5102I2S

inline void pcm5102_i2s_init_sm(PIO pio, uint sm, uint offset,
                                uint32_t sample_rate_hz = PCM5102I2S::DEFAULT_SAMPLE_RATE_HZ) {
    pcm5102_i2s_program_init(pio, sm, offset, PCM5102I2S::PIN_DATA, PCM5102I2S::PIN_CLOCK_BASE,
                             sample_rate_hz);
}

#endif  // PCM5102_I2S_HPP
