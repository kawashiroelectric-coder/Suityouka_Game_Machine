// ============================================
// ファイル: sd_service.cpp
// SD カードの存在確認・FatFS マウント・ルート一覧
// ============================================

#include "sd_service.hpp"

#include <cstdio>

extern "C" {
#include "f_util.h"
#include "ff.h"
#include "hw_config.h"
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
    return true;
}

void SdService::unmount() {
    if (g_sd_mounted) {
        f_unmount("");
        g_sd_mounted = false;
    }
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
