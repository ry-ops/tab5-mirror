/*
 * M5Stack Tab5 -- BLE scanner proof of life, standalone.
 *
 * Tab5 has no onboard Bluetooth radio -- BLE, like WiFi, is routed through
 * the ESP32-C6 co-processor over the same SDIO ESP-Hosted link (ADR 0048).
 * Untested territory: WiFi.setPins()+WiFi.begin() is confirmed to bring the
 * hosted link up (ADR 0049), but whether the standard arduino-esp32
 * BLEDevice API works over that same hosted link on ESP32-P4, with no
 * WiFi connection active, is the actual question this sketch answers --
 * smallest possible proof before building anything on top of it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

namespace {
// ADR 0048/0049: Tab5's SDIO wiring differs from the generic ESP32-P4
// EvalBoard arduino-esp32 defaults to.
constexpr int8_t kSdioClk = 12, kSdioCmd = 13, kSdioD0 = 11, kSdioD1 = 10,
                 kSdioD2 = 9,  kSdioD3 = 8,   kSdioRst = 15;

int gLine = 0;
constexpr int kLineH = 26;

void printLine(const String& s)
{
    Serial.println(s);
    if (gLine * kLineH > M5.Display.height() - kLineH) {
        M5.Display.fillScreen(TFT_BLACK);
        gLine = 0;
    }
    M5.Display.setCursor(4, gLine * kLineH + 4);
    M5.Display.println(s);
    ++gLine;
}

class ScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override
    {
        String name = dev.haveName() ? dev.getName().c_str() : "(no name)";
        char line[128];
        snprintf(line, sizeof(line), "%s  rssi=%d  %s",
                 dev.getAddress().toString().c_str(), dev.getRSSI(), name.c_str());
        printLine(line);
    }
};
} // namespace

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    for (uint32_t t = millis(); !Serial && millis() - t < 2500; ) delay(50);
    delay(400);

    M5.Display.setRotation(3); // ADR 0054: 3, not 1, for correct physical orientation
    M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK);

    Serial.println();
    Serial.println("===== M5Stack Tab5 BLE scanner proof of life =====");
    printLine("Bringing up ESP-Hosted link...");

    // Bring the SDIO link to the C6 up via WiFi.setPins()/begin(), same as
    // every other tab5-stack sketch -- BLE and WiFi share this same
    // transport, so this is the prerequisite regardless of whether a WiFi
    // connection is actually wanted.
    WiFi.setPins(kSdioClk, kSdioCmd, kSdioD0, kSdioD1, kSdioD2, kSdioD3, kSdioRst);
    WiFi.mode(WIFI_MODE_NULL); // hosted link up, no STA/AP -- BLE only

    printLine("Starting BLEDevice...");
    BLEDevice::init("Tab5-BLEScan");
    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new ScanCB(), /*wantDuplicates=*/true);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);

    printLine("Scanning...");
}

void loop()
{
    BLEDevice::getScan()->start(5, false);
    BLEDevice::getScan()->clearResults();
    delay(500);
}
