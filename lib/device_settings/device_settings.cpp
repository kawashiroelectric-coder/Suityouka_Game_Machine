// ============================================
// ファイル: device_settings.cpp
// 本体フラッシュ ROM への音量・輝度の永続化
// ============================================

#include "device_settings.hpp"

#include <cstdio>
#include <cstring>

#include "encoder_volume.hpp"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"
#include "pico/stdlib.h"
#include "st7789_lcd.hpp"

namespace {

constexpr uint32_t kMagic = 0x474D5331u;  // "GSM1"
constexpr uint16_t kVersion = 1;
constexpr uint32_t kFlashSettingsOffset =
    static_cast<uint32_t>(PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
constexpr uint32_t kSaveDebounceMs = 250;

struct StoredSettings {
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t crc16 = 0;
    uint8_t volume_step = 0;
    uint8_t brightness_percent = 0;
    uint8_t reserved[2] = {};
};

struct FlashWriteContext {
    uint32_t offset = 0;
    const uint8_t* data = nullptr;
    size_t length = 0;
};

int g_volume_step = EncoderVolumeControl::kVolumeStepMax;
int g_brightness_percent = DeviceSettings::kDefaultBrightnessPercent;
bool g_loaded = false;
bool g_dirty = false;
uint32_t g_dirty_since_ms = 0;

uint16_t calcCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000u) != 0) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021u);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

uint16_t payloadCrc(const StoredSettings& stored) {
    uint8_t payload[4] = {stored.volume_step, stored.brightness_percent, stored.reserved[0],
                          stored.reserved[1]};
    uint8_t header[2] = {static_cast<uint8_t>(stored.version & 0xFFu),
                         static_cast<uint8_t>((stored.version >> 8) & 0xFFu)};
    uint8_t block[6];
    std::memcpy(block, header, sizeof(header));
    std::memcpy(block + sizeof(header), payload, sizeof(payload));
    return calcCrc16(block, sizeof(block));
}

int clampVolumeStep(int step) {
    if (step < 0) {
        return 0;
    }
    if (step > EncoderVolumeControl::kVolumeStepMax) {
        return EncoderVolumeControl::kVolumeStepMax;
    }
    return step;
}

int clampBrightnessPercent(int percent) {
    if (percent < ST7789_LCD::kBacklightMinPercent) {
        return ST7789_LCD::kBacklightMinPercent;
    }
    if (percent > ST7789_LCD::kBacklightMaxPercent) {
        return ST7789_LCD::kBacklightMaxPercent;
    }
    return percent;
}

bool readStored(StoredSettings& out) {
    const auto* flash =
        reinterpret_cast<const StoredSettings*>(XIP_BASE + kFlashSettingsOffset);
    out = *flash;
    if (out.magic != kMagic || out.version != kVersion) {
        return false;
    }
    return out.crc16 == payloadCrc(out);
}

void __no_inline_not_in_flash_func(flashWriteWorker)(FlashWriteContext* ctx) {
    flash_range_erase(ctx->offset, FLASH_SECTOR_SIZE);
    flash_range_program(ctx->offset, ctx->data, ctx->length);
}

void writeStoredToFlash(const StoredSettings& stored) {
    alignas(4) static uint8_t sector[FLASH_SECTOR_SIZE];
    std::memset(sector, 0xFF, sizeof(sector));
    std::memcpy(sector, &stored, sizeof(stored));

    FlashWriteContext ctx = {};
    ctx.offset = kFlashSettingsOffset;
    ctx.data = sector;
    ctx.length = FLASH_SECTOR_SIZE;

    const uint32_t ints = save_and_disable_interrupts();
    flashWriteWorker(&ctx);
    restore_interrupts(ints);
}

void markDirty() {
    g_dirty = true;
    g_dirty_since_ms = to_ms_since_boot(get_absolute_time());
}

void flushToFlashIfReady() {
    if (!g_dirty) {
        return;
    }
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - g_dirty_since_ms < kSaveDebounceMs) {
        return;
    }

    StoredSettings stored = {};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.volume_step = static_cast<uint8_t>(g_volume_step);
    stored.brightness_percent = static_cast<uint8_t>(g_brightness_percent);
    stored.crc16 = payloadCrc(stored);

    printf("DeviceSettings: saving volume=%d brightness=%d%%\n", g_volume_step + 1,
           g_brightness_percent);
    writeStoredToFlash(stored);
    g_dirty = false;
    printf("DeviceSettings: save complete\n");
}

}  // namespace

void DeviceSettings::load() {
    StoredSettings stored = {};
    if (readStored(stored)) {
        g_volume_step = clampVolumeStep(stored.volume_step);
        g_brightness_percent = clampBrightnessPercent(stored.brightness_percent);
        printf("DeviceSettings: loaded volume=%d/%d brightness=%d%%\n", g_volume_step + 1,
               EncoderVolumeControl::kVolumeSteps, g_brightness_percent);
    } else {
        g_volume_step = EncoderVolumeControl::kVolumeStepMax;
        g_brightness_percent = kDefaultBrightnessPercent;
        printf("DeviceSettings: using defaults volume=%d/%d brightness=%d%%\n", g_volume_step + 1,
               EncoderVolumeControl::kVolumeSteps, g_brightness_percent);
    }
    g_loaded = true;
    g_dirty = false;
}

void DeviceSettings::service() {
    if (!g_loaded) {
        load();
    }
    flushToFlashIfReady();
}

int DeviceSettings::volumeStep() {
    if (!g_loaded) {
        load();
    }
    return g_volume_step;
}

int DeviceSettings::brightnessPercent() {
    if (!g_loaded) {
        load();
    }
    return g_brightness_percent;
}

void DeviceSettings::setVolumeStep(int step) {
    if (!g_loaded) {
        load();
    }
    const int next = clampVolumeStep(step);
    if (next == g_volume_step) {
        return;
    }
    g_volume_step = next;
    markDirty();
}

void DeviceSettings::setBrightnessPercent(int percent) {
    if (!g_loaded) {
        load();
    }
    const int next = clampBrightnessPercent(percent);
    if (next == g_brightness_percent) {
        return;
    }
    g_brightness_percent = next;
    markDirty();
}
