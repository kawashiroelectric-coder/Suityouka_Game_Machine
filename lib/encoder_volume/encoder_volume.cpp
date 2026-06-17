// ============================================
// ファイル: encoder_volume.cpp
// エンコーダによるマスター音量（15 段階）
// ============================================

#include "encoder_volume.hpp"

#include <cstdio>

#include "audio_output.hpp"
#include "device_settings.hpp"
#include "encoder_input.hpp"
#include "lua_audio.hpp"
#include "pico/stdlib.h"

namespace {

AudioOutput* g_audio = nullptr;
LuaAudio* g_lua_audio = nullptr;
EncoderInput g_encoder;
bool g_encoder_ready = false;

int g_volume_step = EncoderVolumeControl::kVolumeStepMax;
/** 1 デテント = 4 カウント（クアッドエンコーダ） */
constexpr int kEncoderCountsPerDetent = 4;
int32_t g_volume_encoder_base = 0;

}  // namespace

bool EncoderVolumeControl::init(AudioOutput* audio, LuaAudio* lua_audio) {
    g_audio = audio;
    g_lua_audio = lua_audio;
    g_volume_step = kVolumeStepMax;
    applyVolume();
    return audio != nullptr;
}

bool EncoderVolumeControl::initEncoder() {
    if (!g_encoder.initIrq()) {
        printf("EncoderVolume: initIrq failed\n");
        return false;
    }
    g_encoder.update();
    (void)g_encoder.consumeDelta();
    g_volume_encoder_base = g_encoder.position();
    g_encoder_ready = true;
    return true;
}

EncoderInput& EncoderVolumeControl::encoder() { return g_encoder; }

int EncoderVolumeControl::volumeStep() { return g_volume_step; }

float EncoderVolumeControl::volumeFloat() {
    if (kVolumeStepMax <= 0) {
        return 1.0f;
    }
    return static_cast<float>(g_volume_step) / static_cast<float>(kVolumeStepMax);
}

void EncoderVolumeControl::applyVolume() {
    const float vol = volumeFloat();
    if (g_lua_audio) {
        g_lua_audio->setVolume(vol);
    } else if (g_audio) {
        g_audio->setVolume(vol);
    }
}

void EncoderVolumeControl::onVolumeChanged() {
    applyVolume();
    DeviceSettings::setVolumeStep(g_volume_step);
    printf("Volume: step %d/%d (%.2f)\n", g_volume_step + 1, kVolumeSteps, volumeFloat());
}

void EncoderVolumeControl::restoreVolumeStep(int step) {
    if (step < 0) {
        step = 0;
    }
    if (step > kVolumeStepMax) {
        step = kVolumeStepMax;
    }
    g_volume_step = step;
    applyVolume();
}

void EncoderVolumeControl::service() {
    if (!g_encoder_ready) {
        return;
    }

    g_encoder.update();
    const int32_t pos = g_encoder.position();
    const int32_t moved = pos - g_volume_encoder_base;
    const int detents = static_cast<int>(moved / kEncoderCountsPerDetent);
    if (detents != 0) {
        g_volume_encoder_base += static_cast<int32_t>(detents * kEncoderCountsPerDetent);

        int next = g_volume_step + detents;
        if (next < 0) {
            next = 0;
        }
        if (next > kVolumeStepMax) {
            next = kVolumeStepMax;
        }
        if (next != g_volume_step) {
            g_volume_step = next;
            onVolumeChanged();
        }
    }

    DeviceSettings::service();
}
