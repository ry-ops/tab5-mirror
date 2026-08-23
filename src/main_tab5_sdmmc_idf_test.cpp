/*
 * M5Stack Tab5 -- SD_MMC via raw ESP-IDF calls, bypassing arduino-esp32's
 * SD_MMC.cpp wrapper entirely.
 *
 * Every prior attempt going through Arduino's SD_MMC class (manual LDO,
 * SD_MMC.setPowerChannel(4), forcing BOARD_SDMMC_SLOT=0, switching to the
 * official maintained m5stack-tab5-p4 board profile, with/without WiFi)
 * failed identically: sdmmc_init_ocr send_op_cond returned 0x107
 * (ESP_ERR_TIMEOUT) on the very first card command. M5Stack's own official
 * UserDemo firmware reads this exact card on this exact hardware via a
 * near-identical call sequence, but goes through esp_vfs_fat_sdmmc_mount()
 * directly (no Arduino wrapper) with SDMMC_FREQ_HIGHSPEED and a 16KB
 * allocation unit -- both different from arduino-esp32's SD_MMC.cpp
 * defaults. This sketch reproduces the official BSP's bsp_sdcard_init()
 * call sequence as closely as possible, to isolate whether the Arduino
 * wrapper itself is the problem.
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
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
    Serial.println("===== M5Stack Tab5 SD_MMC via raw ESP-IDF esp_vfs_fat_sdmmc_mount =====");
    Serial.printf("  mount -> %s (err=0x%x %s)\n", gSdOk ? "true" : "FALSE", (int)gLastErr, esp_err_to_name(gLastErr));
    if (gSdOk && gCard) {
        Serial.printf("  name=%s sectors=%llu sector_size=%u -> %lluMB\n",
                       gCard->cid.name, (unsigned long long)gCard->csd.capacity,
                       (unsigned)gCard->csd.sector_size,
                       (unsigned long long)gCard->csd.capacity * gCard->csd.sector_size / (1024 * 1024));
    }
    Serial.println("=========================================================================");
}
} // namespace

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Power.setExtOutput(true);

    Serial.begin(115200);
    for (uint32_t t = millis(); !Serial && millis() - t < 2500; ) delay(50);
    delay(400);

    Serial.println();
    Serial.println("Step 0: gpio_set_drive_capability(GPIO_DRIVE_CAP_0) on SD pins -- exact BSP call");
    // hal_esp32.cpp's set_gpio_output_capability(), called during general board
    // bring-up (HalEsp32::init(), BEFORE any SD access), sets every SD pin plus
    // several other board pins to GPIO_DRIVE_CAP_0. Nothing in any prior test
    // sketch has ever called this -- untouched GPIOs default to whatever reset
    // state the pad happens to be in, which may not match what this specific
    // silicon needs for the SDMMC lines to work.
    {
        const gpio_num_t sdPins[] = { kSdClk, kSdCmd, kSdD0, kSdD1, kSdD2, kSdD3 };
        for (gpio_num_t p : sdPins) {
            esp_err_t e = gpio_set_drive_capability(p, GPIO_DRIVE_CAP_0);
            Serial.printf("  gpio_set_drive_capability(%d, CAP_0) -> %s\n", (int)p, e == ESP_OK ? "OK" : esp_err_to_name(e));
        }
    }

    Serial.println("Step 1: sd_pwr_ctrl_new_on_chip_ldo(chan=4) -- exact BSP call");
    sd_pwr_ctrl_ldo_config_t ldo_config = { .ldo_chan_id = kLdoChan };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = nullptr;
    esp_err_t ldoErr = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    Serial.printf("  -> %s (err=0x%x %s)\n", ldoErr == ESP_OK ? "OK" : "FAILED", (int)ldoErr, esp_err_to_name(ldoErr));
    if (ldoErr != ESP_OK) {
        printResult();
        return;
    }

    Serial.println("Step 2: SDMMC_HOST_DEFAULT() + slot 0 + explicit pins + HIGHSPEED -- exact BSP config");
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

    Serial.println("Step 3: esp_vfs_fat_sdmmc_mount() -- the actual call the official BSP makes");
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
