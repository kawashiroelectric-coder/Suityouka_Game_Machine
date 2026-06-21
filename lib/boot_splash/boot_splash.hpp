// ============================================
// ファイル: boot_splash.hpp
// 起動後・ゲーム選択メニュー前のロゴ＋音声スプラッシュ
// ============================================

#ifndef BOOT_SPLASH_HPP
#define BOOT_SPLASH_HPP

#include <cstddef>
#include <cstdint>

class ST7789_LCD;
class GameDisplay;
class ButtonInput;
class LuaAudio;

#include "config.hpp"

/** 起動スプラッシュ画面（ロゴ表示と boot BGM 再生） */
class BootSplash {
public:
    using FrameCallback = void (*)(void* user_data);

    struct Config {
        ST7789_LCD* lcd = nullptr;
        GameDisplay* display = nullptr;
        ButtonInput* buttons = nullptr;
        LuaAudio* audio = nullptr;
        /** 待機中に毎フレーム呼ぶ（音声 pump・エンコーダ音量等） */
        FrameCallback on_frame = nullptr;
        void* user_data = nullptr;
        /** 最低表示時間（ms）。`BootSplashConfig::MIN_DISPLAY_MS` が既定 */
        uint32_t min_display_ms = BootSplashConfig::MIN_DISPLAY_MS;
        /** ボタンでスキップ可能か */
        bool skippable = BootSplashConfig::SKIPPABLE;
        /** スキップを受け付ける最短時間（ms） */
        uint32_t skip_min_ms = BootSplashConfig::SKIP_MIN_MS;
        /** フルスクリーン画像の DMA 転送用（main の LCD SPI DMA を渡す） */
        int dma_channel = -1;
        uint8_t* dma_buffer = nullptr;
        size_t dma_buffer_size = 0;
    };

    /**
     * スプラッシュを表示し、音声終了またはスキップ後に戻る。
     * 呼び出し前に LCD / Audio / GameDisplay が初期化済みであること。
     */
    static void run(const Config& config);
};

#endif  // BOOT_SPLASH_HPP
