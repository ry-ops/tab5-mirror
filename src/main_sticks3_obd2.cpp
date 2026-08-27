/*
 * M5Stack StickS3 -- BLE OBD2 adapter proof of life. See ADR 0058.
 *
 * Two phases in one sketch, gated on what gets found at connect time:
 *
 *   Phase A (always runs): scan for BLE peripherals, connect to the first
 *   one that either matches kTargetNameFilter (if set) or advertises one
 *   of the two known BLE-ELM327 GATT layouts, then enumerate and print
 *   every service/characteristic it exposes. Useful against *any* BLE OBD
 *   adapter, before its exact UUIDs are known -- this is how they get
 *   confirmed.
 *
 *   Phase B (only if a known layout was found): treat the notify/write
 *   characteristic pair as an ELM327 serial link -- init AT commands,
 *   then poll engine RPM (mode 01 PID 0C) once per second.
 *
 * Unlike Tab5 (ADR 0048/0056), the StickS3 has its own onboard ESP32-S3
 * radio -- no ESP-Hosted SDIO bridge, no WiFi.setPins() dance. Nothing
 * here is hardware-verified yet (ADR 0058): no StickS3 or adapter in hand
 * at the time this was written.
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include "obd2_parse.h"

namespace {

// Set to a substring (case-sensitive) of the adapter's advertised name to
// restrict which device gets connected to, e.g. "OBDLink". Empty = accept
// any device that either advertises a known OBD service UUID or is the
// strongest signal seen during the scan window.
constexpr const char* kTargetNameFilter = "";
constexpr uint32_t kScanSeconds = 8;

// Known BLE-ELM327 GATT layouts (ADR 0058) -- most cheap Chinese BLE
// ELM327 clones and OBDLink CX use pattern 1; some BLE-serial-wrapper
// adapters use Nordic UART Service (pattern 2).
const BLEUUID kUuidFff0Service("0000fff0-0000-1000-8000-00805f9b34fb");
const BLEUUID kUuidFff1Notify("0000fff1-0000-1000-8000-00805f9b34fb");
const BLEUUID kUuidFff2Write("0000fff2-0000-1000-8000-00805f9b34fb");

const BLEUUID kUuidNusService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
const BLEUUID kUuidNusRxNotify("6e400003-b5a3-f393-e0a9-e50e24dcca9e"); // adapter -> host
const BLEUUID kUuidNusTxWrite("6e400002-b5a3-f393-e0a9-e50e24dcca9e"); // host -> adapter

int gLine = 0;
constexpr int kLineH = 18;

// Set in setup() once a known ELM327 layout is confirmed; loop() polls
// through this directly rather than re-deriving it from the BLE client.
BLERemoteCharacteristic* gWriteChar = nullptr;

void printLine(const String& s)
{
    Serial.println(s);
    if (gLine * kLineH > M5.Display.height() - kLineH) {
        M5.Display.fillScreen(TFT_BLACK);
        gLine = 0;
    }
    M5.Display.setCursor(2, gLine * kLineH + 2);
    M5.Display.println(s);
    ++gLine;
}

BLEAdvertisedDevice* gTarget = nullptr;

class ScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override
    {
        String name = dev.haveName() ? dev.getName().c_str() : "(no name)";
        char line[128];
        snprintf(line, sizeof(line), "%s rssi=%d %s",
                 dev.getAddress().toString().c_str(), dev.getRSSI(), name.c_str());
        printLine(line);

        bool nameMatches = strlen(kTargetNameFilter) == 0 ||
                            (dev.haveName() && name.indexOf(kTargetNameFilter) >= 0);
        bool hasKnownService = dev.haveServiceUUID() &&
                                (dev.isAdvertisingService(kUuidFff0Service) ||
                                 dev.isAdvertisingService(kUuidNusService));

        if (!nameMatches) return;
        if (gTarget != nullptr && !hasKnownService) return; // keep first match unless a known-service device shows up
        if (gTarget != nullptr) delete gTarget;
        gTarget = new BLEAdvertisedDevice(dev);
    }
};

// Accumulates notified bytes until an ELM327 '>' prompt closes one response.
String gRxBuf;
volatile bool gRxHasResponse = false;

void onNotify(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool)
{
    for (size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(data[i]);
        if (c == '>') {
            gRxHasResponse = true;
        } else if (c != '\r') {
            gRxBuf += c;
        }
    }
}

bool sendCommand(BLERemoteCharacteristic* txChar, const char* cmd, uint32_t timeoutMs)
{
    gRxBuf = "";
    gRxHasResponse = false;
    String withCr = String(cmd) + "\r";
    txChar->writeValue(reinterpret_cast<uint8_t*>(const_cast<char*>(withCr.c_str())),
                        withCr.length(), false);
    uint32_t start = millis();
    while (!gRxHasResponse && millis() - start < timeoutMs) delay(20);
    return gRxHasResponse;
}

} // namespace

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    for (uint32_t t = millis(); !Serial && millis() - t < 2500;) delay(50);
    delay(400);

    M5.Display.setRotation(1);
    M5.Display.setTextSize(1);
    M5.Display.fillScreen(TFT_BLACK);

    Serial.println();
    Serial.println("===== M5StickS3 BLE OBD2 proof of life (ADR 0058) =====");
    printLine("Scanning for BLE OBD adapter...");

    BLEDevice::init("StickS3-OBD");
    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new ScanCB(), /*wantDuplicates=*/false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(kScanSeconds, false);
    scan->stop();

    if (gTarget == nullptr) {
        printLine("No matching device found.");
        printLine("Check kTargetNameFilter / power the adapter on.");
        return;
    }

    printLine("Connecting...");
    BLEClient* client = BLEDevice::createClient();
    if (!client->connect(gTarget)) {
        printLine("Connect failed.");
        return;
    }
    printLine("Connected. Enumerating services...");

    // Phase A: dump every service/characteristic this peripheral exposes,
    // regardless of whether it matched a known layout -- this is how the
    // real UUIDs for whatever adapter got bought get confirmed.
    auto* services = client->getServices();
    BLERemoteCharacteristic* notifyChar = nullptr;
    BLERemoteCharacteristic* writeChar = nullptr;
    for (auto& svcPair : *services) {
        BLERemoteService* svc = svcPair.second;
        printLine(String("svc ") + svc->getUUID().toString().c_str());
        auto* chars = svc->getCharacteristics();
        for (auto& chPair : *chars) {
            BLERemoteCharacteristic* ch = chPair.second;
            char props[64];
            snprintf(props, sizeof(props), "  ch %s [%s%s%s%s]",
                     ch->getUUID().toString().c_str(),
                     ch->canRead() ? "R" : "", ch->canWrite() ? "W" : "",
                     ch->canNotify() ? "N" : "", ch->canIndicate() ? "I" : "");
            printLine(props);

            if (svc->getUUID().equals(kUuidFff0Service)) {
                if (ch->getUUID().equals(kUuidFff1Notify)) notifyChar = ch;
                if (ch->getUUID().equals(kUuidFff2Write)) writeChar = ch;
            } else if (svc->getUUID().equals(kUuidNusService)) {
                if (ch->getUUID().equals(kUuidNusRxNotify)) notifyChar = ch;
                if (ch->getUUID().equals(kUuidNusTxWrite)) writeChar = ch;
            }
        }
    }

    if (notifyChar == nullptr || writeChar == nullptr) {
        printLine("No known ELM327 layout found.");
        printLine("Adapter uses a different UUID scheme --");
        printLine("read the dump above and update ADR 0058.");
        return;
    }

    // Phase B: known layout found -- treat it as an ELM327 serial link.
    printLine("Known layout found. Starting ELM327 init...");
    notifyChar->registerForNotify(onNotify);

    struct { const char* cmd; } initCmds[] = {{"ATZ"}, {"ATE0"}, {"ATSP0"}};
    for (auto& c : initCmds) {
        bool ok = sendCommand(writeChar, c.cmd, 3000);
        printLine(String(c.cmd) + " -> " + (ok ? gRxBuf : String("(timeout)")));
    }
    gWriteChar = writeChar; // only now, so loop() can tell "no known layout" from "ready"
}

void loop()
{
    if (gWriteChar == nullptr) {
        delay(1000);
        return;
    }
    bool ok = sendCommand(gWriteChar, "010C", 2000);
    if (ok) {
        int rpm = obd2::parseRpm(gRxBuf.c_str());
        printLine(rpm >= 0 ? String("RPM: ") + rpm : String("RPM: parse failed: ") + gRxBuf);
    } else {
        printLine("RPM query timed out.");
    }
    delay(1000);
}
