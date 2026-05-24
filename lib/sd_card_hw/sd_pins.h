/* SD card SPI pin map (must match config.hpp SDConfig) */
#ifndef SD_PINS_H
#define SD_PINS_H

/** SPI1 SCK (GP10) */
#define SD_PIN_CLK   10
/** SPI1 MOSI (GP11) */
#define SD_PIN_MOSI  11
/** SPI1 MISO (GP12) */
#define SD_PIN_MISO  12
/** カード CS (GP13) */
#define SD_PIN_CS    13
/** カード電源制御（LOW=ON、Pch FET） */
#define SD_PIN_POWER 15

#endif
