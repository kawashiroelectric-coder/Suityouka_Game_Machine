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

/** encoder_pos 増加方向で音量 UP、15 段階 */
class EncoderVolumeControl {
public:
    static constexpr int kVolumeSteps = 15;
    static constexpr int kVolumeStepMax = kVolumeSteps - 1;

    static bool init(AudioOutput* audio, LuaAudio* lua_audio);
    static bool initEncoder();

    static EncoderInput& encoder();

    /** update + デルタ処理（各フレーム 1 回） */
    static void service();

    static int volumeStep();
    static float volumeFloat();
    /** 起動時復元用（フラッシュには書き込まない） */
    static void restoreVolumeStep(int step);

private:
    static void applyVolume();
    static void onVolumeChanged();
};

#endif  // ENCODER_VOLUME_HPP
