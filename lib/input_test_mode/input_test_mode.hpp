// ============================================
// ファイル: input_test_mode.hpp
// 設定画面から起動するエンコーダ・ボタン入力テスト画面
// ============================================

#ifndef INPUT_TEST_MODE_HPP
#define INPUT_TEST_MODE_HPP

#include <cstdint>

class ST7789_LCD;
class ButtonInput;
class EncoderInput;

/** 設定画面等から起動し、LEFT で呼び出し元へ戻る入力テスト画面 */
class InputTestMode {
public:
    using TryMountCallback = bool (*)(void* user_data);
    using FrameCallback = void (*)(void* user_data);

    struct Config {
        ST7789_LCD* lcd = nullptr;
        ButtonInput* buttons = nullptr;
        /** 共有エンコーダ（nullptr ならローカル生成） */
        EncoderInput* encoder = nullptr;
        /** 任意: SD 挿入時のバックグラウンドマウント試行 */
        TryMountCallback try_mount = nullptr;
        FrameCallback on_frame = nullptr;
        void* user_data = nullptr;
        uint32_t mount_retry_ms = 2000;
        uint32_t frame_interval_ms = 50;
    };

    /** 入力テスト画面を起動し LEFT+FAR で戻るまでループする。設定メニューから呼ぶ */
    static void run(const Config& config);
};

#endif  // INPUT_TEST_MODE_HPP
