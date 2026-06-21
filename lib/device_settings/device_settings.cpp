// ============================================
// ファイル: device_settings.cpp
// 本体フラッシュ ROM への音量・輝度の永続化
// ============================================

#include "device_settings.hpp"

#include <cstdio>
#include <cstring>

#include "encoder_volume.hpp"
#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "st7789_lcd.hpp"

namespace {

constexpr uint32_t kMagic = 0x474D5331u;  // "GSM1"
constexpr uint16_t kVersion = 1;
constexpr uint32_t kFlashSettingsOffset =
    static_cast<uint32_t>(PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE);
constexpr uint32_t kSaveDebounceMs = 2000;
constexpr uint32_t kFlashSafeTimeoutMs = 1000;

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
uint8_t g_battery_led_mode = static_cast<uint8_t>(DeviceSettings::BatteryLedMode::AlwaysOn);
bool g_loaded = false;
bool g_dirty = false;
uint32_t g_dirty_since_ms = 0;

/** CRC-16/CCITT-FALSE で data のチェックサムを計算する */
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

/** StoredSettings のペイロード部分の CRC を計算する */
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

/** 音量ステップを 0〜最大の範囲に収める */
int clampVolumeStep(int step) {
    if (step < 0) {
        return 0;
    }
    if (step > EncoderVolumeControl::kVolumeStepMax) {
        return EncoderVolumeControl::kVolumeStepMax;
    }
    return step;
}

/** 輝度パーセントを LCD 許容範囲に収める */
int clampBrightnessPercent(int percent) {
    if (percent < ST7789_LCD::kBacklightMinPercent) {
        return ST7789_LCD::kBacklightMinPercent;
    }
    if (percent > ST7789_LCD::kBacklightMaxPercent) {
        return ST7789_LCD::kBacklightMaxPercent;
    }
    return percent;
}

DeviceSettings::BatteryLedMode clampBatteryLedMode(uint8_t raw) {
    if (raw == static_cast<uint8_t>(DeviceSettings::BatteryLedMode::PulseOnChange)) {
        return DeviceSettings::BatteryLedMode::PulseOnChange;
    }
    return DeviceSettings::BatteryLedMode::AlwaysOn;
}

/** フラッシュ末尾から設定を読み込み、マジック・CRC を検証する */
bool readStored(StoredSettings& out) {
    const auto* flash =
        reinterpret_cast<const StoredSettings*>(XIP_BASE + kFlashSettingsOffset);
    out = *flash;
    if (out.magic != kMagic || out.version != kVersion) {
        return false;
    }
    return out.crc16 == payloadCrc(out);
}

/** RAM 上で実行: フラッシュセクタを消去して設定を書き込む */
void __no_inline_not_in_flash_func(flashWriteWorker)(void* param) {
    auto* ctx = static_cast<FlashWriteContext*>(param);
    flash_range_erase(ctx->offset, FLASH_SECTOR_SIZE);
    flash_range_program(ctx->offset, ctx->data, ctx->length);
}

/** 設定構造体をフラッシュ末尾 1 セクタへ書き込む（Core1 ロックアウト付き） */
bool writeStoredToFlash(const StoredSettings& stored) {
    alignas(4) static uint8_t sector[FLASH_SECTOR_SIZE];
    std::memset(sector, 0xFF, sizeof(sector));
    std::memcpy(sector, &stored, sizeof(stored));

    FlashWriteContext ctx = {};
    ctx.offset = kFlashSettingsOffset;
    ctx.data = sector;
    ctx.length = FLASH_SECTOR_SIZE;

    const int rc = flash_safe_execute(flashWriteWorker, &ctx, kFlashSafeTimeoutMs);
    return rc == PICO_OK;
}

/** 設定変更を記録し、デバウンス用タイムスタンプを更新する */
void markDirty() {
    g_dirty = true;
    g_dirty_since_ms = to_ms_since_boot(get_absolute_time());
}

/** デバウンス経過後に未保存の変更をフラッシュへ書き込む */
void flushToFlashIfReady(bool force) {
    if (!g_dirty) {
        return;
    }
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!force && now - g_dirty_since_ms < kSaveDebounceMs) {
        return;
    }

    StoredSettings stored = {};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.volume_step = static_cast<uint8_t>(g_volume_step);
    stored.brightness_percent = static_cast<uint8_t>(g_brightness_percent);
    stored.reserved[0] = g_battery_led_mode;
    stored.crc16 = payloadCrc(stored);

    if (!writeStoredToFlash(stored)) {
        printf("DeviceSettings: flash save failed\n");
        return;
    }
    g_dirty = false;
}

}  // namespace

/** 起動時にフラッシュから音量・輝度を読み込む（失敗時はデフォルト値） */
void DeviceSettings::load() {
    StoredSettings stored = {};
    if (readStored(stored)) {
        g_volume_step = clampVolumeStep(stored.volume_step);
        g_brightness_percent = clampBrightnessPercent(stored.brightness_percent);
        g_battery_led_mode =
            static_cast<uint8_t>(clampBatteryLedMode(stored.reserved[0]));
        printf("DeviceSettings: loaded volume=%d/%d brightness=%d%% batt_led=%u\n",
               g_volume_step + 1, EncoderVolumeControl::kVolumeSteps, g_brightness_percent,
               static_cast<unsigned>(g_battery_led_mode));
    } else {
        g_volume_step = EncoderVolumeControl::kVolumeStepMax;
        g_brightness_percent = kDefaultBrightnessPercent;
        g_battery_led_mode = static_cast<uint8_t>(DeviceSettings::BatteryLedMode::AlwaysOn);
        printf("DeviceSettings: using defaults volume=%d/%d brightness=%d%% batt_led=%u\n",
               g_volume_step + 1, EncoderVolumeControl::kVolumeSteps, g_brightness_percent,
               static_cast<unsigned>(g_battery_led_mode));
    }
    g_loaded = true;
    g_dirty = false;
}

/** メインループから定期的に呼び、遅延フラッシュ書き込みを実行する */
void DeviceSettings::service() {
    if (!g_loaded) {
        load();
    }
    flushToFlashIfReady(false);
}

/** 未保存の変更があればデバウンスを待たず即時フラッシュする（メニュー退場時など） */
void DeviceSettings::flushPending() {
    if (!g_loaded) {
        load();
    }
    flushToFlashIfReady(true);
}

/** 現在の音量ステップ（0 始まり）を返す。未読込なら load する */
int DeviceSettings::volumeStep() {
    if (!g_loaded) {
        load();
    }
    return g_volume_step;
}

/** 現在の LCD 輝度（%）を返す。未読込なら load する */
int DeviceSettings::brightnessPercent() {
    if (!g_loaded) {
        load();
    }
    return g_brightness_percent;
}

/** 音量ステップを設定し、遅延フラッシュ保存を予約する */
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

/** LCD 輝度（%）を設定し、遅延フラッシュ保存を予約する */
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

/** バッテリー LED 表示モードを返す */
DeviceSettings::BatteryLedMode DeviceSettings::batteryLedMode() {
    if (!g_loaded) {
        load();
    }
    return clampBatteryLedMode(g_battery_led_mode);
}

/** バッテリー LED 表示モードを設定し、遅延フラッシュ保存を予約する */
void DeviceSettings::setBatteryLedMode(BatteryLedMode mode) {
    if (!g_loaded) {
        load();
    }
    const uint8_t next = static_cast<uint8_t>(mode);
    if (next == g_battery_led_mode) {
        return;
    }
    g_battery_led_mode = next;
    markDirty();
}
