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

/** 音声出力と Lua 音声エンジンを登録し、初期音量を適用する（main 起動時） */
bool EncoderVolumeControl::init(AudioOutput* audio, LuaAudio* lua_audio) {
    g_audio = audio;
    g_lua_audio = lua_audio;
    g_volume_step = kVolumeStepMax;
    applyVolume();
    return audio != nullptr;
}

/** エンコーダを IRQ モードで初期化し、現在位置を基準点にする（main 起動時） */
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

/** 共有 EncoderInput インスタンスへの参照を返す */
EncoderInput& EncoderVolumeControl::encoder() { return g_encoder; }

/** 現在の音量ステップ（0 始まり）を返す */
int EncoderVolumeControl::volumeStep() { return g_volume_step; }

/** 現在の音量を 0.0〜1.0 の float で返す */
float EncoderVolumeControl::volumeFloat() {
    if (kVolumeStepMax <= 0) {
        return 1.0f;
    }
    return static_cast<float>(g_volume_step) / static_cast<float>(kVolumeStepMax);
}

/** 登録済み音声出力へ現在の音量を反映する */
void EncoderVolumeControl::applyVolume() {
    const float vol = volumeFloat();
    if (g_lua_audio) {
        g_lua_audio->setVolume(vol);
    } else if (g_audio) {
        g_audio->setVolume(vol);
    }
}

/** 音量変更時に音声反映・フラッシュ保存予約・シリアルログを行う */
void EncoderVolumeControl::onVolumeChanged() {
    applyVolume();
    DeviceSettings::setVolumeStep(g_volume_step);
    printf("Volume: step %d/%d (%.2f)\n", g_volume_step + 1, kVolumeSteps, volumeFloat());
}

/** 起動時にフラッシュから読んだ音量ステップを復元する（フラッシュには書かない） */
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

/** エンコーダ回転を監視し音量変更と DeviceSettings 保存を行う（各フレーム 1 回） */
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
