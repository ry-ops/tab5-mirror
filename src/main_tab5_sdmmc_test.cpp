/*
 * M5Stack Tab5 -- SD_MMC (SDIO) + on-chip LDO proof of life, standalone.
 *
 * Isolated, disposable test -- per this project's own "test the risky
 * change cheaply before touching the working repo" lesson. An earlier
 * attempt to switch tab5-launcher's Launcher fork to SD_MMC crashed the
 * device (H_SDIO_DRV register-read failure) with WiFi (ESP-Hosted, also
 * SDIO-based) already running. That attempt did NOT have the on-chip LDO
 * (ldo_chan_id=4) enabled first -- M5Stack's own official UserDemo BSP
 * (m5stack_tab5.c's bsp_sdcard_init()) calls sd_pwr_ctrl_new_on_chip_ldo()
 * before touching SD_MMC at all, and UserDemo's own SD access (SDMMC mode)
 * DOES work on this exact card/hardware, with WiFi also active (it runs
 * its own AP). This sketch reproduces that same order -- WiFi/ESP-Hosted
 * up first, then the LDO, then SD_MMC.begin() -- to find out whether the
 * earlier crash was actually the missing LDO (not a genuine WiFi/SDMMC
 * host-slot conflict as first assumed), before touching tab5-launcher's
 * real firmware again.
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
#include <WiFi.h>
#include <SD_MMC.h>
#include <sd_pwr_ctrl_by_on_chip_ldo.h>

namespace {
// ADR 0048/0049: Tab5's SDIO wiring for the WiFi/BT coprocessor.
constexpr int8_t kSdioClk = 12, kSdioCmd = 13, kSdioD0 = 11, kSdioD1 = 10,
                 kSdioD2 = 9,  kSdioD3 = 8,   kSdioRst = 15;

// microSD SDMMC pins, verified against M5Stack's own official BSP source
// (m5stack_tab5.c) -- same physical pins as the SPI-mode attempt, different
// signal roles.
constexpr int kSdClk = 43, kSdCmd = 44, kSdD0 = 39, kSdD1 = 40, kSdD2 = 41, kSdD3 = 42;

bool gLdoOk = false;
bool gWifiOk = false;
bool gSdOk = false;
uint8_t gCardType = 0;
unsigned long long gCardSizeMB = 0;

void printResult()
{
    Serial.println();
    Serial.println("===== M5Stack Tab5 SD_MMC + on-chip LDO proof of life =====");
    Serial.printf("  LDO enable -> %s\n", gLdoOk ? "OK" : "FAILED");
    Serial.printf("  WiFi (ESP-Hosted link) -> %s\n", gWifiOk ? "connected" : "not connected (link may still be up)");
    Serial.printf("  SD_MMC.begin() -> %s\n", gSdOk ? "true" : "FALSE");
    if (gSdOk) {
        const char* typeName = gCardType == CARD_NONE ? "NONE" :
                                gCardType == CARD_MMC  ? "MMC"  :
                                gCardType == CARD_SD   ? "SD"   :
                                gCardType == CARD_SDHC ? "SDHC" : "UNKNOWN";
        Serial.printf("  cardType=%d (%s) cardSize=%lluMB\n", (int)gCardType, typeName, gCardSizeMB);
    }
    Serial.println("=============================================================");
}
} // namespace

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    // M5Stack's own BSP (m5stack_tab5.c) drives the PI4IOE5V6416 IO expander's
    // EXT5V_EN bit high during board bring-up -- separate from the on-chip LDO
    // (which only sets the SDMMC bus's I/O signal voltage). If the SD slot's
    // actual VDD rail hangs off that same expander output, no I/O-voltage
    // config would matter -- the card would have zero power and never
    // respond to the very first command (matches the 0x107 timeout seen so
    // far). tab5-launcher's own interface.cpp already calls this (added for
    // an unrelated reason) but this isolated sketch never did.
    M5.Power.setExtOutput(true);

    Serial.begin(115200);
    for (uint32_t t = millis(); !Serial && millis() - t < 2500; ) delay(50);
    delay(400);

    Serial.println();
    Serial.println("Step 1: SKIPPED -- WiFi/ESP-Hosted intentionally not brought up this run.");
    Serial.println("  Testing whether ESP-Hosted's SDIO link contends with the SD_MMC host");
    Serial.println("  peripheral even when SD uses a different host slot -- every prior");
    Serial.println("  variation (LDO/power-channel/slot0/extOutput) failed identically with");
    Serial.println("  WiFi up; this isolates that one remaining variable.");
    gWifiOk = false;

    // Step 2: SD_MMC.cpp's own internal on-chip LDO handling (SOC_SDMMC_IO_POWER_EXTERNAL,
    // true on ESP32-P4) is gated on _power_channel != -1 -- which defaults to -1 and is
    // ONLY set via the dedicated setPowerChannel() call (SD_MMC.h:55). Without it,
    // begin() skips sd_pwr_ctrl_new_on_chip_ldo() entirely (SD_MMC.cpp:264's
    // "On-chip power channel specified, use external power" log on the -1 branch is
    // misleading -- it actually means NO on-chip channel is used) and the LDO only got
    // enabled as an unrelated side effect of this board profile's periman auto-LDO
    // pin-range feature, too late (logged AFTER sdmmc_init_ocr's 0x107 timeout, confirmed
    // via serial capture). Explicitly setting the power channel makes SD_MMC.begin()
    // acquire the LDO itself, before esp_vfs_fat_sdmmc_mount() ever toggles the bus --
    // matching M5Stack's own official BSP ordering (LDO up before any SD access).
    Serial.println("Step 2: SD_MMC.setPowerChannel(4) -- let begin() own LDO chan 4 acquisition");
    gLdoOk = SD_MMC.setPowerChannel(4);
    Serial.printf("  setPowerChannel(4) -> %s\n", gLdoOk ? "true" : "false");

    Serial.println("Step 3: SD_MMC.setPins() + begin() -- the step that crashed before");
    SD_MMC.setPins(kSdClk, kSdCmd, kSdD0, kSdD1, kSdD2, kSdD3);
    gSdOk = SD_MMC.begin("/sdcard", /*mode1bit=*/false, /*format_if_mount_failed=*/false);
    if (gSdOk) {
        gCardType = SD_MMC.cardType();
        if (gCardType != CARD_NONE) gCardSizeMB = SD_MMC.cardSize() / (1024 * 1024);
    }

    printResult();
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last > 3000) {
        last = millis();
        printResult();
    }
    delay(50);
}
