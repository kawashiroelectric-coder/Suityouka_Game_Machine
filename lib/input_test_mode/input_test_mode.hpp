// ============================================
// ファイル: input_test_mode.hpp
// SD 未検知時のエンコーダ・ボタン入力テスト画面
// ============================================

#ifndef INPUT_TEST_MODE_HPP
#define INPUT_TEST_MODE_HPP

#include <cstdint>

class ST7789_LCD;
class ButtonInput;

/** SD 利用可能になるまでブロックし、入力状態を LCD に表示する */
class InputTestMode {
public:
    using TryMountCallback = bool (*)(void* user_data);
    using FrameCallback = void (*)(void* user_data);

    struct Config {
        ST7789_LCD* lcd = nullptr;
        ButtonInput* buttons = nullptr;
        TryMountCallback try_mount = nullptr;
        FrameCallback on_frame = nullptr;
        void* user_data = nullptr;
        uint32_t mount_retry_ms = 2000;
        uint32_t frame_interval_ms = 50;
    };

    /** try_mount が true を返すまでループする */
    static void run(const Config& config);
};

#endif  // INPUT_TEST_MODE_HPP
