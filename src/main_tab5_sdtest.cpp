/*
 * M5Stack Tab5 -- microSD proof of life, standalone.
 *
 * Matches M5Stack's own official Tab5 microSD example exactly: SPI mode,
 * SD.begin(cs, SPI, 25000000) -- an EXPLICIT 25MHz clock. Worth testing
 * because Launcher's own SD.begin(cs, sdcardSPI) (no frequency argument)
 * fails to detect the card on real hardware (cardType() reads CARD_NONE),
 * and Launcher never specifies a clock speed at all -- this isolates
 * whether that's the actual gap, before reaching for SD_MMC (which hit a
 * separate SDIO/WiFi-coprocessor conflict and crashed the device).
 *
 * Pins verified against M5Stack's own docs (docs.m5stack.com/en/arduino/
 * m5tab5/microsd), matching what Launcher's board profile already uses.
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

namespace {
constexpr int kSdCs = 42, kSdSck = 43, kSdMosi = 44, kSdMiso = 39;
}

namespace {
bool gOk = false;
uint8_t gCardType = 0;
unsigned long long gCardSizeMB = 0;

void printResult()
{
    Serial.println();
    Serial.println("===== M5Stack Tab5 microSD proof of life =====");
    Serial.printf("  Pins: cs=%d sck=%d mosi=%d miso=%d\n", kSdCs, kSdSck, kSdMosi, kSdMiso);
    Serial.printf("  SD.begin(cs, SPI, 25000000) -> %s\n", gOk ? "true" : "FALSE");
    if (gOk) {
        const char* typeName = gCardType == CARD_NONE ? "NONE" :
                                gCardType == CARD_MMC  ? "MMC"  :
                                gCardType == CARD_SD   ? "SD"   :
                                gCardType == CARD_SDHC ? "SDHC" : "UNKNOWN";
        Serial.printf("  cardType=%d (%s) cardSize=%lluMB\n", (int)gCardType, typeName, gCardSizeMB);
    }
    Serial.println("==================================================");
}
} // namespace

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    for (uint32_t t = millis(); !Serial && millis() - t < 2500; ) delay(50);
    delay(400);

    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    delay(10);

    // The one deliberate difference from Launcher's own attempt: an
    // explicit 25MHz clock, matching M5Stack's own documented example.
    gOk = SD.begin(kSdCs, SPI, 25000000);
    if (gOk) {
        gCardType = SD.cardType();
        if (gCardType != CARD_NONE) gCardSizeMB = SD.cardSize() / (1024 * 1024);
    }

    if (gOk && gCardType != CARD_NONE) {
        File root = SD.open("/");
        Serial.println("  Root directory listing:");
        File f = root.openNextFile();
        int n = 0;
        while (f && n < 20) {
            Serial.printf("    %s%s (%u bytes)\n", f.isDirectory() ? "[DIR] " : "", f.name(), (unsigned)f.size());
            f = root.openNextFile();
            ++n;
        }
        if (n == 0) Serial.println("    (empty)");
    }

    printResult();
}

void loop()
{
    // Reprints every 3s -- a one-shot setup() print is a losing race
    // against a serial monitor that attaches even slightly late (this
    // hardware's native USB-CDC re-enumerates on every reset). See
    // tab5-stack's own established heartbeat pattern (main_tab5_mirror.cpp).
    static uint32_t last = 0;
    if (millis() - last > 3000) {
        last = millis();
        printResult();
    }
    delay(50);
}
