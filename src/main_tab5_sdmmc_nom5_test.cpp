/*
 * M5Stack Tab5 -- SD_MMC via raw ESP-IDF calls, WITHOUT M5Unified/M5GFX at all.
 *
 * Every prior attempt, including a byte-for-byte replica of the official
 * BSP's bsp_sdcard_init() via raw esp_vfs_fat_sdmmc_mount() (LDO chan 4,
 * host slot 0, exact pins, SDMMC_FREQ_HIGHSPEED, 16KB allocation unit,
 * GPIO_DRIVE_CAP_0 on the SD pins), failed identically with
 * sdmmc_init_ocr send_op_cond 0x107 (ESP_ERR_TIMEOUT). All of those
 * attempts called M5.begin() first, which drives M5GFX's Tab5 autodetect --
 * heavy I2C probing, DSI display bring-up, touch controller detection. The
 * official BSP's own display path (LVGL + their own display driver, not
 * M5GFX) is completely different code. This sketch skips M5Unified/M5GFX
 * entirely -- plain Arduino Serial only -- to test whether display/DMA
 * resource contention from M5GFX's bring-up is somehow starving the SDMMC
 * peripheral, independent of every pin/power/slot config already verified
 * correct.
 *
 * SPDX-License-Identifier: MIT
 */
#include <Arduino.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdmmc_host.h>
#include <driver/gpio.h>
#include <sd_pwr_ctrl_by_on_chip_ldo.h>

namespace {
constexpr gpio_num_t kSdClk = GPIO_NUM_43, kSdCmd = GPIO_NUM_44, kSdD0 = GPIO_NUM_39,
                     kSdD1 = GPIO_NUM_40, kSdD2 = GPIO_NUM_41, kSdD3 = GPIO_NUM_42;
constexpr int kLdoChan = 4;

bool gSdOk = false;
esp_err_t gLastErr = ESP_OK;
sdmmc_card_t* gCard = nullptr;

void printResult()
{
    Serial.println();
    Serial.println("===== Tab5 SD_MMC, no M5Unified/M5GFX at all =====");
    Serial.printf("  mount -> %s (err=0x%x %s)\n", gSdOk ? "true" : "FALSE", (int)gLastErr, esp_err_to_name(gLastErr));
    if (gSdOk && gCard) {
        Serial.printf("  name=%s -> %lluMB\n", gCard->cid.name,
                       (unsigned long long)gCard->csd.capacity * gCard->csd.sector_size / (1024 * 1024));
    }
    Serial.println("===================================================");
}
} // namespace

void setup()
{
    Serial.begin(115200);
    for (uint32_t t = millis(); !Serial && millis() - t < 2500; ) delay(50);
    delay(400);

    Serial.println();
    Serial.println("No M5.begin() this run -- plain Serial + raw ESP-IDF SDMMC only.");

    {
        const gpio_num_t sdPins[] = { kSdClk, kSdCmd, kSdD0, kSdD1, kSdD2, kSdD3 };
        for (gpio_num_t p : sdPins) {
            gpio_set_drive_capability(p, GPIO_DRIVE_CAP_0);
        }
    }

    sd_pwr_ctrl_ldo_config_t ldo_config = { .ldo_chan_id = kLdoChan };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = nullptr;
    esp_err_t ldoErr = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    Serial.printf("LDO chan 4 -> %s (err=0x%x)\n", ldoErr == ESP_OK ? "OK" : "FAILED", (int)ldoErr);
    if (ldoErr != ESP_OK) {
        printResult();
        return;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = kSdClk;
    slot_config.cmd = kSdCmd;
    slot_config.d0 = kSdD0;
    slot_config.d1 = kSdD1;
    slot_config.d2 = kSdD2;
    slot_config.d3 = kSdD3;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    Serial.println("esp_vfs_fat_sdmmc_mount() ...");
    gLastErr = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &gCard);
    gSdOk = (gLastErr == ESP_OK);

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
