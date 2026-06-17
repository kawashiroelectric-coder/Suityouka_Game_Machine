// ============================================
// ファイル: sd_service.cpp
// SD カードの存在確認・FatFS マウント・ルート一覧
// ============================================

#include "sd_service.hpp"

#include <cstdint>
#include <cstdio>

#include "pico/stdlib.h"

extern "C" {
#include "f_util.h"
#include "ff.h"
#include "hw_config.h"
#include "sd_card.h"
}

namespace {

FATFS g_sd_fs;
bool g_sd_mounted = false;

}  // namespace

bool SdService::isCardPresent() {
    sd_card_t* card = sd_get_by_num(0);
    if (!card || !card->sd_test_com) {
        return false;
    }
    return card->sd_test_com(card);
}

bool SdService::isMounted() { return g_sd_mounted; }

bool SdService::mount() {
    FRESULT fr = f_mount(&g_sd_fs, "", 1);
    if (fr != FR_OK) {
        printf("f_mount failed: %s (%d)\n", FRESULT_str(fr), fr);
        g_sd_mounted = false;
        return false;
    }
    g_sd_mounted = true;

    sd_card_t* card = sd_get_by_num(0);
    if (card) {
        const uint32_t sectors = card->get_num_sectors ? card->get_num_sectors(card) : 0;
        const unsigned long mib =
            sectors ? static_cast<unsigned long>((static_cast<uint64_t>(sectors) * 512ULL) / (1024ULL * 1024ULL))
                    : 0UL;
        printf("SD mounted: %s, %lu sectors (~%lu MiB)\n",
               card->state.card_type == SDCARD_V2HC   ? "SDHC/SDXC"
               : card->state.card_type == SDCARD_V2 ? "SDSC v2"
               : card->state.card_type == SDCARD_V1 ? "SDSC v1"
                                                    : "SD",
               static_cast<unsigned long>(sectors), mib);
    }
    return true;
}

void SdService::unmount() {
    if (g_sd_mounted) {
        f_unmount("");
        g_sd_mounted = false;
    }
}

bool SdService::remount() {
    const bool was_mounted = g_sd_mounted;
    unmount();
    if (!was_mounted) {
        return false;
    }
    sleep_ms(20);
    return mount();
}

void SdService::listRoot() {
    DIR dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, "/");
    if (fr != FR_OK) {
        printf("f_opendir failed: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    while (true) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) {
            break;
        }
        printf("  %s\t%lu\n", fno.fname, static_cast<unsigned long>(fno.fsize));
    }
    f_closedir(&dir);
}
