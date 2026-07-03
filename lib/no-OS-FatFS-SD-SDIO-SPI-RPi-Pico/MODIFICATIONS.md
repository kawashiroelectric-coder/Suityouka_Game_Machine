# Modifications to no-OS-FatFS-SD-SDIO-SPI-RPi-Pico

This vendored copy is based on [carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico).

| Item | Value |
|------|-------|
| **Upstream commit** | `d5e453404cdbfaa55ab30d285b6ab0b730e84a05` |
| **Modified by** | Kawashiro Electric |
| **Project** | Suityouka Game Machine |
| **Date** | 2026 (integrated during firmware development) |

Original work: Copyright Carl John Kugler III and contributors.  
Licensed under the **Apache License 2.0** (see `LICENSE` in this directory).

The files listed below were changed from the upstream version for SD-card reliability on this hardware (SPI SD, SDSC/SDHC mixed support, FatFs LBA64/exFAT).

---

## Modified files

| File | Summary of changes |
|------|-------------------|
| `src/CMakeLists.txt` | Put `include` before `ff15/source` in `target_include_directories` so project `include/ffconf.h` / `hw_config.h` resolve correctly. |
| `src/ff15/source/ffconf.h` | **Activated** FatFs configuration (renamed from upstream template `!ffconf.h`). Enabled LFN (UTF-8), `FF_LBA64`, `FF_FS_EXFAT`, 512-byte sectors, file lock count 16, etc. |
| `src/src/glue.c` | Added `lba_to_sector32()` to map FatFs `LBA_t` to SD driver `uint32_t` sectors; updated `disk_read` / `disk_write` / `GET_SECTOR_COUNT` in `disk_ioctl`. |
| `src/sd_driver/SPI/sd_card_spi.c` | SDSC byte vs SDHC block addressing (`sd_sector_to_wire_addr`); SPI init order (OCR/CCS/CSD fallback for HC detection); allow SDSC cards; improved debug messages. |
| `src/sd_driver/sd_regs.h` | `CSD_sectors()` uses 64-bit intermediate math and clamps sector count to `UINT32_MAX` for SDXC-sized media. |

---

## Files not modified (project integration)

Hardware pinout and SPI instance are configured **outside** this library:

- `lib/sd_card_hw/hw_config.c` — Suityouka Game Machine (MIT License, project root)
- `lib/sd_card_hw/sd_debug.c` — diagnostics wrapper

These are **not** derivatives of no-OS-FatFS; they link against its public API only.

---

## How to audit changes

From this directory (nested git clone):

```bash
git diff d5e453404cdbfaa55ab30d285b6ab0b730e84a05 -- \
  src/CMakeLists.txt \
  src/ff15/source/ffconf.h \
  src/src/glue.c \
  src/sd_driver/SPI/sd_card_spi.c \
  src/sd_driver/sd_regs.h
```

Project-level summary: `THIRD_PARTY_NOTICES.md` at the repository root.
