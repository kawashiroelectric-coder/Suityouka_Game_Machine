/**
 * SDカード初期化の段階別デバッグ
 * no-OS-FatFS-SD-SDIO-SPI-RPi-Pico の公開APIのみ使用
 */
#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hw_config.h"
#include "sd_pins.h"
#include "SPI/sd_card_spi.h"
#include "SPI/my_spi.h"
#include "sd_card_constants.h"
#include "diskio.h"
#include "f_util.h"
#include "ff.h"
#include "sd_debug.h"

#define R1_IDLE_STATE   (1u << 0)
#define R1_NO_RESPONSE  0xFFu

/** カード種別を文字列化 */
static const char* card_type_str(card_type_t t) {
    switch (t) {
        case SDCARD_NONE: return "NONE";
        case SDCARD_V1: return "V1 (SDSC)";
        case SDCARD_V2: return "V2 (pre-HCS)";
        case SDCARD_V2HC: return "V2HC (SDHC/SDXC)";
        case CARD_UNKNOWN: return "UNKNOWN";
        default: return "?";
    }
}

/** disk_initialize 等の DSTATUS を分解表示 */
static void print_dstatus(const char* label, DSTATUS st) {
    printf("[SD DBG] %s DSTATUS=0x%02X", label, st);
    if (st & STA_NOINIT) printf(" NOINIT");
    if (st & STA_NODISK) printf(" NODISK");
    if (st & STA_PROTECT) printf(" PROTECT");
    if (st == 0) printf(" OK");
    printf("\n");
}

/** SD コマンド応答 R1 を分解表示 */
static void print_r1(const char* step, uint32_t r1) {
    printf("[SD DBG] %s R1=0x%02lX", step, (unsigned long)r1);
    if (r1 == R1_NO_RESPONSE) {
        printf(" (NO_RESPONSE)");
    } else {
        if (r1 & R1_IDLE_STATE) printf(" IDLE");
        if (r1 & (1u << 1)) printf(" ERASE_RESET");
        if (r1 & (1u << 2)) printf(" ILLEGAL_CMD");
        if (r1 & (1u << 3)) printf(" COM_CRC_ERR");
        if (r1 & (1u << 4)) printf(" ERASE_SEQ_ERR");
        if (r1 & (1u << 5)) printf(" ADDR_ERR");
        if (r1 & (1u << 6)) printf(" PARAM_ERR");
    }
    printf("\n");
}

void sd_debug_run_diagnostics(void) {
    printf("\n========== SD DEBUG START ==========\n");

    printf("[SD DBG] Power GP%d=%d (schematic: LOW=ON via Pch FET)\n",
           SD_PIN_POWER, gpio_get(SD_PIN_POWER));
    printf("[SD DBG] SPI pins: SCK=%d MOSI=%d MISO=%d CS=%d\n",
           SD_PIN_CLK, SD_PIN_MOSI, SD_PIN_MISO, SD_PIN_CS);
    printf("[SD DBG] Insert detect GP%d=%d (active level depends on socket)\n",
           0, gpio_get(0));

    /* --- Step 1: driver --- */
    printf("[SD DBG] Step 1: sd_init_driver()\n");
    bool drv_ok = sd_init_driver();
    printf("[SD DBG] sd_init_driver() => %s\n", drv_ok ? "true" : "false");

    sd_card_t* card = sd_get_by_num(0);
    if (!card) {
        printf("[SD DBG] ERROR: sd_get_by_num(0) returned NULL\n");
        printf("========== SD DEBUG END ==========\n\n");
        return;
    }

    spi_t* spi = card->spi_if_p->spi;
    printf("[SD DBG] SPI: inst=%p baud_cfg=%lu Hz spi_mode=%u\n",
           (void*)spi->hw_inst,
           (unsigned long)spi->baud_rate,
           (unsigned)spi->spi_mode);
    printf("[SD DBG] card_type(after driver)=%s m_Status=0x%02X\n",
           card_type_str(card->state.card_type), card->state.m_Status);

    /* --- Step 2: card detect --- */
    printf("[SD DBG] Step 2: sd_card_detect()\n");
    bool detected = sd_card_detect(card);
    printf("[SD DBG] sd_card_detect() => %s\n", detected ? "true" : "false");
    print_dstatus("after detect", card->state.m_Status);

    /* --- Step 3: CMD0 alone --- */
    printf("[SD DBG] Step 3: sd_go_idle_state() [CMD0]\n");
    uint32_t r1 = sd_go_idle_state(card);
    print_r1("CMD0", r1);
    if (r1 != R1_IDLE_STATE) {
        printf("[SD DBG] >>> FAIL at CMD0 (power/SPI wiring/CS)\n");
    }

    /* --- Step 4: full card init (sd_init_medium inside) --- */
    printf("[SD DBG] Step 4: card->init() [CMD8/CMD58/ACMD41/...]\n");
    printf("[SD DBG]   (library EMSG/DGB lines may appear below)\n");
    if (card->init) {
        DSTATUS st = card->init(card);
        print_dstatus("after card->init()", st);
        printf("[SD DBG] card_type(after init)=%s sectors=%lu\n",
               card_type_str(card->state.card_type),
               (unsigned long)card->state.sectors);
        if (st & STA_NOINIT) {
            printf("[SD DBG] >>> FAIL in sd_init_medium or not SDHC (need SDCARD_V2HC)\n");
        } else if (card->state.card_type != SDCARD_V2HC) {
            printf("[SD DBG] >>> WARN: init OK but type is not V2HC (library requires SDHC)\n");
        }
    }

    /* --- Step 5: disk layer --- */
    printf("[SD DBG] Step 5: disk_initialize(0)\n");
    DSTATUS di = disk_initialize(0);
    print_dstatus("disk_initialize", di);

    /* --- Step 6: FatFs mount --- */
    printf("[SD DBG] Step 6: f_mount(\"\", 1)\n");
    FATFS fs;
    FRESULT fr = f_mount(&fs, "", 1);
    printf("[SD DBG] f_mount => %d (%s)\n", fr, FRESULT_str(fr));
    if (fr == FR_OK) {
        printf("[SD DBG] >>> MOUNT OK - listing root:\n");
        DIR dir;
        FILINFO fno;
        if (f_opendir(&dir, "/") == FR_OK) {
            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
                printf("         %s  %lu bytes\n", fno.fname,
                       (unsigned long)fno.fsize);
            }
            f_closedir(&dir);
        }
        f_unmount("");
    } else if (fr == FR_NOT_READY) {
        printf("[SD DBG] >>> FAIL: physical drive not ready (see steps 3-4)\n");
    } else if (fr == FR_NO_FILESYSTEM) {
        printf("[SD DBG] >>> FAIL: no FAT volume (format as FAT32 on PC)\n");
    }

    /* --- Step 7: comm test --- */
    printf("[SD DBG] Step 7: sd_test_com()\n");
    if (card->sd_test_com) {
        bool com = card->sd_test_com(card);
        printf("[SD DBG] sd_test_com() => %s\n", com ? "true" : "false");
    }

    printf("========== SD DEBUG END ==========\n\n");
}
