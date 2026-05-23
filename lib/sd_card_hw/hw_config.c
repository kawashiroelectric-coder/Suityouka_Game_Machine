/* hw_config.c - Suityouka Game Machine SD card (SPI1) */

#include "hw_config.h"
#include "sd_pins.h"

/* SPI1: SCK=10, MOSI=11, MISO=12, CS=13 */
static spi_t spi = {
    .hw_inst = spi1,
    .sck_gpio = SD_PIN_CLK,
    .mosi_gpio = SD_PIN_MOSI,
    .miso_gpio = SD_PIN_MISO,
    .baud_rate = 60 * 1000 * 1000,
    .spi_mode = 3,
};

static sd_spi_if_t spi_if = {
    .spi = &spi,
    .ss_gpio = SD_PIN_CS,
};

static sd_card_t sd_card = {
    .type = SD_IF_SPI,
    .spi_if_p = &spi_if,
    .use_card_detect = false,
};

size_t sd_get_num(void) { return 1; }

sd_card_t *sd_get_by_num(size_t num) {
    return (num == 0) ? &sd_card : NULL;
}
