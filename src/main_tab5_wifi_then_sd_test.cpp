/*
 * M5Stack Tab5 -- WiFi/ESP-Hosted bring-up followed by SPI-mode SD.begin(),
 * reproducing Launcher's own boot order in isolation.
 *
 * env:tab5-sdtest (SD alone, no WiFi) mounts the card reliably every time.
 * Both tab5-launcher builds (the original mirror-integrated fork AND a
 * brand new, unmodified-upstream-plus-one-line-fix sub project) fail to
 * mount the SAME card on the SAME hardware, every time -- despite using
 * the identical SD.begin(cs, SPI, 25000000) call. The one thing every
 * failing case shares that env:tab5-sdtest never does: Launcher's
 * _setup_gpio() always calls launcherWifiInitHostedSdioGuarded() (WiFi/
 * ESP-Hosted co-processor bring-up over its own SDIO link) BEFORE SD ever
 * gets touched. This sketch reproduces exactly that order -- WiFi/hosted
 * bring-up first, then SD.begin() -- to test whether the co-processor
 * bring-up itself is what breaks SD, independent of anything else in
 * Launcher's own code.
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
#include <WiFi.h>
#include <SD.h>
#include <SPI.h>

namespace {
// Same pins tab5-launcher's board profile uses for the ESP-Hosted SDIO link.
constexpr int8_t kSdioClk = 12, kSdioCmd = 13, kSdioD0 = 11, kSdioD1 = 10,
                 kSdioD2 = 9,  kSdioD3 = 8,   kSdioRst = 15;

// Same pins as env:tab5-sdtest / M5Stack's own documented Tab5 microSD example.
constexpr int kSdCs = 42, kSdSck = 43, kSdMosi = 44, kSdMiso = 39;

SPIClass sdSpi;
bool gWifiInitOk = false;
bool gSdOk = false;
uint8_t gCardType = 0;
unsigned long long gCardSizeMB = 0;

void printResult()
{
    Serial.println();
    Serial.println("===== WiFi/hosted bring-up, then SPI SD -- Launcher's own order =====");
    Serial.printf("  WiFi.mode(STA) after setPins -> %s\n", gWifiInitOk ? "no immediate failure" : "n/a");
    Serial.printf("  SD.begin(cs, SPI, 25000000) -> %s\n", gSdOk ? "true" : "FALSE");
    if (gSdOk) {
        const char* typeName = gCardType == CARD_NONE ? "NONE" :
                                gCardType == CARD_MMC  ? "MMC"  :
                                gCardType == CARD_SD   ? "SD"   :
                                gCardType == CARD_SDHC ? "SDHC" : "UNKNOWN";
        Serial.printf("  cardType=%d (%s) cardSize=%lluMB\n", (int)gCardType, typeName, gCardSizeMB);
    }
    Serial.println("=======================================================================");
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
    Serial.println("Step 1: WiFi/ESP-Hosted bring-up (matching Launcher's _setup_gpio() order)");
    WiFi.setPins(kSdioClk, kSdioCmd, kSdioD0, kSdioD1, kSdioD2, kSdioD3, kSdioRst);
    WiFi.mode(WIFI_MODE_STA);
    gWifiInitOk = true;
    delay(500);

    Serial.println("Step 2: SPI-mode SD.begin(cs, SPI, 25000000) -- the exact call that works alone");
    sdSpi.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    delay(10);
    gSdOk = SD.begin(kSdCs, sdSpi, 25000000);
    if (gSdOk) {
        gCardType = SD.cardType();
        if (gCardType != CARD_NONE) gCardSizeMB = SD.cardSize() / (1024 * 1024);
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
