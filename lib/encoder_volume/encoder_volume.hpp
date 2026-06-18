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

    /** 音声出力を登録し初期音量を適用する（main 起動時） */
    static bool init(AudioOutput* audio, LuaAudio* lua_audio);
    /** エンコーダを IRQ モードで初期化する（main 起動時） */
    static bool initEncoder();

    /** 共有 EncoderInput インスタンスへの参照を返す */
    static EncoderInput& encoder();

    /** エンコーダ回転監視・音量変更（各フレーム 1 回） */
    static void service();

    /** 現在の音量ステップ（0 始まり）を返す */
    static int volumeStep();
    /** 現在の音量を 0.0〜1.0 で返す */
    static float volumeFloat();
    /** 起動時復元用（フラッシュには書き込まない） */
    static void restoreVolumeStep(int step);

private:
    /** 登録済み音声出力へ音量を反映する */
    static void applyVolume();
    /** 音量変更時の後処理（保存・ログ） */
    static void onVolumeChanged();
};

#endif  // ENCODER_VOLUME_HPP
