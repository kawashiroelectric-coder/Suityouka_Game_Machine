/* hw_config.c - Suityouka Game Machine SD card (SPI1) */

#include "hw_config.h"
#include "config.hpp"
#include "sd_pins.h"

/** no-OS-FatFS 用 SPI1（.hw_inst=spi1 は SDConfig::SPI_HW と一致させる） */
static spi_t spi = {
    .hw_inst = spi1,
    .sck_gpio = SD_PIN_CLK,
    .mosi_gpio = SD_PIN_MOSI,
    .miso_gpio = SD_PIN_MISO,
    .baud_rate = CFG_SD_SPI_BAUD_HZ,
    .spi_mode = CFG_SD_SPI_MODE,
};

/** SPI バスと CS ピンの sd_spi_if */
static sd_spi_if_t spi_if = {
    .spi = &spi,
    .ss_gpio = SD_PIN_CS,
};

/** カード検出なしの SD カード記述子 */
static sd_card_t sd_card = {
    .type = SD_IF_SPI,
    .spi_if_p = &spi_if,
    .use_card_detect = false,
};

/** 接続 SD カード枚数（本機は 1 枚） */
size_t sd_get_num(void) { return 1; }

/** インデックス 0 の sd_card_t を返す（他は NULL） */
sd_card_t *sd_get_by_num(size_t num) {
    return (num == 0) ? &sd_card : NULL;
}
