# ADR 0057 — SD_MMC (SDIO) dead end, kept as reference

**Status:** Rejected as a fix — not needed; sketches kept in-repo as
reference material only, not integrated into anything.
**Deciders:** firmware owner
**Related:** ADR 0055 (the actual working microSD proof, SPI mode);
[[tab5-launcher]]'s own ADR 0003 documents the full story of why this
investigation happened and how it was resolved (a leftover uncommitted
experiment in that repo, not a real gap here).

## Context

microSD mounting broke again on tab5-launcher's real firmware after
ADR 0002/0055's SPI fix had already been verified working. Before the real
cause was found (see tab5-launcher ADR 0003), this repo was used to
isolate whether Tab5's Launcher fork had a genuine SD_MMC (SDIO) gap —
matching M5Stack's own official BSP source
(`m5stack/M5Tab5-UserDemo`, `platforms/tab5/components/m5stack_tab5/m5stack_tab5.c`,
fetched directly via `gh api`) as closely as possible:

- On-chip LDO channel 4 (`sd_pwr_ctrl_new_on_chip_ldo`), both manually
  pre-enabled and via arduino-esp32's own `SD_MMC.setPowerChannel(4)`.
- ESP32-P4 SDMMC host slot 0 (fixed IOMUX pins 43/44/39-42, per
  `soc/sdmmc_pins.h`'s `SDMMC_SLOT0_IOMUX_PIN_NUM_*`) vs. the default
  slot 1 (GPIO-matrix routing) arduino-esp32 falls back to without an
  explicit `BOARD_SDMMC_SLOT` — both the generic `esp32-p4-evboard` board
  and the real maintained `m5stack-tab5-p4` board profile (with its own
  `variants/m5stack_tab5/pins_arduino.h` defining `BOARD_HAS_SDMMC` and
  `BOARD_SDMMC_SLOT 0` for real) were tried.
- `gpio_set_drive_capability(pin, GPIO_DRIVE_CAP_0)` on all six SD pins,
  matching the official BSP's `hal_esp32.cpp` `set_gpio_output_capability()`.
- `M5.Power.setExtOutput(true)`, matching the BSP's IO-expander `EXT5V_EN`
  bit.
- WiFi/ESP-Hosted brought up first vs. not brought up at all (rules out
  SDIO peripheral contention with the WiFi coprocessor).
- Bypassing arduino-esp32's `SD_MMC` Arduino wrapper entirely and calling
  `esp_vfs_fat_sdmmc_mount()` directly — a byte-for-byte replica of the
  official BSP's own call (host slot 0, exact pins, `SDMMC_FREQ_HIGHSPEED`,
  16KB allocation unit).
- M5Unified/M5GFX removed entirely (plain `Serial`, no `M5.begin()`), to
  rule out display/DMA resource contention.

**Every single variation failed identically**:
`sdmmc_init_ocr: send_op_cond (1) returned 0x107` (`ESP_ERR_TIMEOUT`) on
the very first card command, regardless of which of the above was changed.

One further attempt — forcing PlatformIO's production-silicon prebuilt
libs (`board_build.chip_variant = esp32p4`) instead of the Tab5 board
profile's default `esp32p4_es` ("ES pre rev.300" engineering-sample
silicon) libs — **crashed the device outright**
(`Guru Meditation Error: Illegal instruction` at boot). Reflashed back to
the last known-good test build immediately after; this is confirmed *not*
a safe drop-in override for this toolchain (likely a ROM/bootloader-stub
linkage mismatch), whatever the real relationship between ES and
production silicon turns out to be.

## Decision

**Not pursued further.** The actual cause of the "broke again" symptom was
unrelated to any of this (see tab5-launcher ADR 0003) — SPI mode
(ADR 0055) was never broken. These sketches are kept in this repo,
uncommitted-turned-tracked, purely as reference:

- `src/main_tab5_sdmmc_test.cpp` (`env:tab5-sdmmc-test`) — Arduino
  `SD_MMC` wrapper, various LDO/power-channel/board-profile permutations.
- `src/main_tab5_sdmmc_idf_test.cpp` (`env:tab5-sdmmc-idf-test`) — raw
  ESP-IDF `esp_vfs_fat_sdmmc_mount()`, byte-for-byte BSP replica.
- `src/main_tab5_sdmmc_nom5_test.cpp` (`env:tab5-sdmmc-nom5-test`,
  `env:tab5-sdmmc-prodsi-test`) — same raw call with M5Unified removed,
  and the chip-variant override that crashed the device.

If Tab5 ever genuinely needs SDMMC/SDIO (e.g. for higher throughput or
concurrent access alongside something else), the LDO channel, host slot,
and pin findings documented here and in the sketches' own comments remain
accurate groundwork — they were simply never the fix needed for the
original symptom.

## Consequences

**Positive**
- A real, externally-corroborated `0x107`/`ESP_ERR_TIMEOUT` failure
  signature and an exhaustive elimination list are preserved here instead
  of being re-discovered from scratch if SD_MMC is ever revisited.
- Confirms `esp32p4_es` vs `esp32p4` chip_variant is not freely
  interchangeable on this toolchain — worth remembering before touching
  that flag again for an unrelated reason.

**Negative / open**
- Whether the `0x107` timeout across every variation was a genuine
  pioarduino/arduino-esp32 ESP32-P4 SDMMC packaging gap (a community
  thread's working report used the official M5Stack Arduino board package,
  a different install path than pioarduino) was never conclusively
  determined — the investigation stopped once the real cause (unrelated to
  any of this) was found elsewhere. Left open rather than answered.
