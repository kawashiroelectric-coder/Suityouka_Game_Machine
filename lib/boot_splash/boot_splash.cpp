// ============================================
// ファイル: boot_splash.cpp
// 起動スプラッシュ（ロゴ・タイトル・boot ジングル）
// ============================================

#include "boot_splash.hpp"

#include <cstdio>

#include "GameLogo.h"
#include "boot_chime.h"
#include "button_input.hpp"
#include "config.hpp"
#include "game_display.hpp"
#include "lua_audio.hpp"
#include "pico/stdlib.h"
#include "st7789_lcd.hpp"

namespace {

/** 8 ボタンのいずれかが押下中なら true */
bool isAnyButtonHeld(ButtonInput* buttons) {
    if (!buttons) {
        return false;
    }
    for (int i = 0; i < 8; i++) {
        if (buttons->isPressed(static_cast<Button>(i))) {
            return true;
        }
    }
    return false;
}

/** GameLogo を 1 回の SPI/DMA 転送で描画する */
void drawSplashImage(const BootSplash::Config& config) {
    ST7789_LCD* lcd = config.lcd;
    if (!lcd) {
        return;
    }
    const uint16_t w = static_cast<uint16_t>(GameLogo_width);
    const uint16_t h = static_cast<uint16_t>(GameLogo_height);
    if (config.dma_channel >= 0 && config.dma_buffer && config.dma_buffer_size >= 2) {
        lcd->drawRawImageDMA(0, 0, w, h, GameLogo_pixels, config.dma_channel, config.dma_buffer,
                             config.dma_buffer_size);
        return;
    }
    lcd->drawRawImage(0, 0, w, h, GameLogo_pixels);
}

/** flash 埋め込み boot BGM（説明ウインドウが開く.mp3）をストリーム再生開始する */
void startBootBgm(LuaAudio* audio) {
    if (!audio) {
        return;
    }
    const bool ok = audio->playBgmFromEmbedded(boot_chime_pcm, boot_chime_frame_count, boot_chime_channels,
                                               boot_chime_sample_rate);
    if (!ok) {
        printf("BootSplash: boot BGM play failed\n");
    } else {
        printf("BootSplash: boot BGM started (%lu frames @ %lu Hz)\n",
               static_cast<unsigned long>(boot_chime_frame_count),
               static_cast<unsigned long>(boot_chime_sample_rate));
    }
}

/** 1 フレーム分の待機処理（入力・コールバック・点滅更新） */
void serviceFrame(const BootSplash::Config& config, uint32_t elapsed_ms, bool* skip_requested) {
    if (config.on_frame) {
        config.on_frame(config.user_data);
    } else if (config.audio) {
        config.audio->pumpStream();
    }

    if (config.skippable && config.buttons) {
        config.buttons->update();
        if (isAnyButtonHeld(config.buttons)) {
            *skip_requested = true;
        }
    }
}

}  // namespace

/** 起動スプラッシュを表示し、完了後にゲーム選択メニューへ進める */
void BootSplash::run(const Config& config) {
    if (!config.lcd || !config.display) {
        printf("BootSplash: missing lcd/display\n");
        return;
    }

    printf("BootSplash: start\n");
    config.display->releaseForDirectDraw();
    config.display->waitForTransferComplete();
    config.lcd->finishDrawRawImageDMA();

    drawSplashImage(config);

    startBootBgm(config.audio);

    const uint32_t start_ms = to_ms_since_boot(get_absolute_time());
    bool skip_requested = false;

    while (true) {
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        const uint32_t elapsed_ms = now_ms - start_ms;

        serviceFrame(config, elapsed_ms, &skip_requested);

        const bool min_elapsed = elapsed_ms >= config.min_display_ms;
        const bool audio_done = !config.audio || !config.audio->isAudioActive();
        const bool can_finish = min_elapsed && (audio_done || skip_requested);

        if (skip_requested && elapsed_ms >= config.skip_min_ms) {
            break;
        }
        if (can_finish) {
            break;
        }

        sleep_ms(33);
    }

    if (config.audio) {
        config.audio->stopBgm();
    }

    config.lcd->finishDrawRawImageDMA();
    printf("BootSplash: done\n");
}
