// ============================================
// ファイル: encoder_volume.hpp
// エンコーダによるマスター音量（15 段階）とデバッグ表示
// ============================================

#ifndef ENCODER_VOLUME_HPP
#define ENCODER_VOLUME_HPP

#include <cstdint>

class AudioOutput;
class LuaAudio;
class ST7789_LCD;
class EncoderInput;

/** encoder_pos 増加方向で音量 UP、15 段階、調整後 600ms 左上表示 */
class EncoderVolumeControl {
public:
    static constexpr int kVolumeSteps = 15;
    static constexpr int kVolumeStepMax = kVolumeSteps - 1;
    static constexpr uint32_t kOverlayMs = 600;

    static bool init(AudioOutput* audio, LuaAudio* lua_audio);
    static bool initEncoder();
    static void setDisplay(ST7789_LCD* lcd);

    static EncoderInput& encoder();

    /** update + デルタ処理 + オーバーレイ描画（各フレーム 1 回） */
    static void service();

    static int volumeStep();
    static float volumeFloat();

private:
    static void applyVolume();
    static void onVolumeChanged();
    static void drawOverlayIfNeeded();
};

#endif  // ENCODER_VOLUME_HPP
