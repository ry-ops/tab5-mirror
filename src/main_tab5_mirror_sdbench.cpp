/*
 * M5Stack Tab5 -- mirror + SD concurrent-load profiling harness (ADR 0058).
 *
 * Every other env in this tree keeps SD (ADR 0055, env:tab5-sdtest) and the
 * web mirror (ADR 0050, env:tab5-mirror) in separate firmware images -- SD
 * and the mirror have never actually run at the same time on this hardware
 * before this file. That means the reported "SD + streaming don't perform
 * well concurrently" symptom has no code path to reproduce it in yet. This
 * sketch is that path: same WiFi/mirror bring-up as main_tab5_mirror.cpp,
 * plus a continuous SD write+read benchmark (ADR 0055's proven SPI config,
 * pins/clock unchanged), both running in the same loop().
 *
 * Deliberately NOT a bus-arbitration exercise. Display is MIPI-DSI (a RAM
 * line-buffer read, no SPI at all -- see CardputerMirror.h's header
 * comment); SD is on its own dedicated SPI pins; WiFi is a separate SDIO
 * link to the ESP32-C6. None of the three share electrical bus lines, so
 * there is no lock to take here (see Tab5Adapter::busLock() returning
 * nullptr, unchanged) -- if the two workloads contend, it is over shared
 * CPU cycles / memory-bus bandwidth, not a bus one of them has to wait for.
 * That is exactly what this harness measures instead of assumes.
 *
 * Two SD I/O modes, chosen by platformio.ini's build_flags:
 *   default              -- SD write+read run inline in loop(), so a
 *                            blocking SD SPI transaction directly delays
 *                            the next tile scan/encode/publish call. Worst
 *                            case, and what a naive integration would do.
 *   -DSD_BENCH_TASK=1    -- SD write+read run on their own FreeRTOS task,
 *                            pinned to the core loop() does NOT run on.
 *                            Tests whether decoupling SD off the mirror's
 *                            own call stack helps, without needing true
 *                            DMA/async SD support from the Arduino SD lib
 *                            (which doesn't offer one).
 *
 * Every ~15s the SD workload toggles OFF/ON (kSdPhaseMs) so one boot
 * captures both "mirror alone" and "mirror + SD" phases directly comparable
 * in the same log, without a reflash. CardputerMirror's per-stage profiling
 * (CMIRROR_PROFILE, see CardputerMirror.h/.cpp) resets at each phase edge.
 *
 * NOTE: this was written and built for real Tab5 hardware but not flashed
 * from this session -- no physical device was available here. The
 * instrumentation and both SD modes are ready to flash; ADR 0058 states
 * that explicitly rather than presenting invented numbers.
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#ifdef SD_BENCH_TASK
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "CardputerMirror.h"
#include "tab5_adapter.h"

#if __has_include("wifi_credentials.h")
  #include "wifi_credentials.h"
#else
  #error "Missing include/wifi_credentials.h - copy include/wifi_credentials.example.h to it and fill in your network."
#endif

namespace {

struct Profile { const char* ssid; const char* password; };
constexpr Profile kProfiles[] = { WIFI_PROFILES };

constexpr bool cstarts(const char* h, const char* n)
{
    return *n == '\0' ? true : (*h != *n ? false : cstarts(h + 1, n + 1));
}
constexpr bool cfind(const char* h, const char* n)
{
    return cstarts(h, n) ? true : (*h == '\0' ? false : cfind(h + 1, n));
}
constexpr bool isPlaceholder(const char* s)
{
    return cfind(s, "REDACTED") || cfind(s, "PUT_ROTATED")
        || cfind(s, "your-network") || cfind(s, "your-password")
        || cfind(s, "backup-network") || cfind(s, "backup-password");
}
constexpr size_t kNumProfiles = sizeof(kProfiles) / sizeof(kProfiles[0]);
constexpr bool anyPlaceholder(size_t i = 0)
{
    return i >= kNumProfiles ? false
         : (isPlaceholder(kProfiles[i].ssid) || isPlaceholder(kProfiles[i].password)
            ? true : anyPlaceholder(i + 1));
}
static_assert(!anyPlaceholder(),
    "wifi_credentials.h still holds a REDACTED/PUT_ROTATED/example placeholder "
    "in one of the WIFI_PROFILES entries.");

constexpr int8_t kSdioClk = 12, kSdioCmd = 13, kSdioD0 = 11, kSdioD1 = 10,
                 kSdioD2 = 9,  kSdioD3 = 8,   kSdioRst = 15;

// ADR 0055's exact pins. kSdClockHz is deliberately a named constant, not
// inlined at the call site -- part of this bench's job is to find out
// whether 25MHz is actually the ceiling or just the first value M5Stack's
// own docs happened to use. Bump this and reflash to compare; nothing else
// in this file needs to change.
constexpr int kSdCs = 42, kSdSck = 43, kSdMosi = 44, kSdMiso = 39;
constexpr uint32_t kSdClockHz = 25000000;

tab5adapter::Tab5Adapter gAdapter;

// ---------------------------------------------------------------- SD bench

constexpr size_t   kSdChunkBytes = 64 * 1024;  // one op's worth: a plausible
                                                // "save a captured frame /
                                                // log chunk" unit, not a
                                                // full-card sweep.
constexpr uint32_t kSdPhaseMs    = 15000;      // OFF/ON toggle period.
constexpr const char* kSdBenchPath = "/bench.bin";

struct SdStats {
    uint32_t writeUsTotal = 0, writeUsMax = 0, writeSamples = 0, writeFails = 0;
    uint32_t readUsTotal  = 0, readUsMax  = 0, readSamples  = 0, readFails  = 0;
};

volatile bool gSdOk = false;
uint8_t* gSdBuf = nullptr;

SdStats gSdStats; // single writer (the SD task in SD_BENCH_TASK mode, or
                   // loop() itself otherwise), read by heartbeat() for
                   // printing only -- same not-volatile-payload pattern
                   // Tab5InputSink uses for _lastRemote/_lastPhysical
                   // (tab5_adapter.h): worst case a torn read prints one
                   // stale/mixed field for one heartbeat line, never a
                   // real hazard, and not worth a lock for.
#ifdef SD_BENCH_TASK
bool gSdPhaseActive = false;
TaskHandle_t gSdTaskHandle = nullptr;
#endif

bool sdBenchInit()
{
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    delay(10);
    if (!SD.begin(kSdCs, SPI, kSdClockHz)) return false;
    if (SD.cardType() == CARD_NONE) return false;

    gSdBuf = (uint8_t*)heap_caps_malloc(kSdChunkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gSdBuf) gSdBuf = (uint8_t*)malloc(kSdChunkBytes);
    if (!gSdBuf) return false;
    for (size_t i = 0; i < kSdChunkBytes; ++i) gSdBuf[i] = (uint8_t)(i & 0xFF);

    SD.remove(kSdBenchPath); // start clean; each write below is a full kSdChunkBytes write
    return true;
}

// One write+read cycle. Updates `stats` in place. Free function so both the
// inline (loop()) and task (SD_BENCH_TASK) callers share identical timing
// logic; only who calls it, and on which stack, differs. Both callers pass
// a plain (non-volatile) SdStats -- the task-mode caller copies out of its
// `volatile` shared struct first, mutates the plain copy here, then writes
// the whole thing back in one shot (see sdBenchTask()).
void sdBenchCycle(SdStats& stats)
{
    SD.remove(kSdBenchPath);
    uint32_t t0 = micros();
    File wf = SD.open(kSdBenchPath, FILE_WRITE);
    bool wOk = false;
    if (wf) {
        wOk = (wf.write(gSdBuf, kSdChunkBytes) == kSdChunkBytes);
        wf.close();
    }
    uint32_t writeUs = micros() - t0;
    if (wOk) {
        stats.writeUsTotal += writeUs;
        if (writeUs > stats.writeUsMax) stats.writeUsMax = writeUs;
        ++stats.writeSamples;
    } else {
        ++stats.writeFails;
    }

    t0 = micros();
    File rf = SD.open(kSdBenchPath, FILE_READ);
    bool rOk = false;
    if (rf) {
        rOk = (rf.read(gSdBuf, kSdChunkBytes) == (int)kSdChunkBytes);
        rf.close();
    }
    uint32_t readUs = micros() - t0;
    if (rOk) {
        stats.readUsTotal += readUs;
        if (readUs > stats.readUsMax) stats.readUsMax = readUs;
        ++stats.readSamples;
    } else {
        ++stats.readFails;
    }
}

#ifdef SD_BENCH_TASK
void sdBenchTask(void*)
{
    for (;;) {
        if (gSdOk && gSdPhaseActive) {
            sdBenchCycle(gSdStats);
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        vTaskDelay(1); // yield -- see the file header: this must not starve
                       // other tasks on its core just because SD has no
                       // native async/DMA path in the Arduino wrapper.
    }
}
#endif

// -------------------------------------------------------------- test pattern

constexpr int kCheckerCell = 40;
constexpr uint16_t kCheckerPat[4] = { 0xF800, 0x07E0, 0x001F, 0xFFFF };

uint16_t checkerColorAt(int x, int y)
{
    return kCheckerPat[((x / kCheckerCell) + (y / kCheckerCell)) & 3];
}

void drawTestPattern(int w, int h)
{
    M5.Display.startWrite();
    for (int y = 0; y < h; y += kCheckerCell)
        for (int x = 0; x < w; x += kCheckerCell)
            M5.Display.fillRect(x, y, kCheckerCell, kCheckerCell, checkerColorAt(x, y));
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(4, 4);
    M5.Display.print("SD+MIRROR BENCH");
    M5.Display.endWrite();
    M5.Display.display();
}

} // namespace

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    for (uint32_t t = millis(); !Serial && millis() - t < 2500; ) delay(50);
    delay(400);

    Serial.println();
    Serial.println("===== M5Stack Tab5 mirror+SD concurrent-load bench (ADR 0058) =====");
#ifdef SD_BENCH_TASK
    Serial.println("  SD mode: TASK (own FreeRTOS task, off loop()'s core)");
#else
    Serial.println("  SD mode: INLINE (runs directly in loop(), blocking)");
#endif

    M5.Display.setRotation(3);
    M5.Display.fillScreen(TFT_BLACK);
    drawTestPattern(M5.Display.width(), M5.Display.height());

    WiFi.setPins(kSdioClk, kSdioCmd, kSdioD0, kSdioD1, kSdioD2, kSdioD3, kSdioRst);
    WiFi.mode(WIFI_STA);
    for (size_t i = 0; i < kNumProfiles; ++i) {
        const char* pw = (kProfiles[i].password && *kProfiles[i].password)
                        ? kProfiles[i].password : nullptr;
        Serial.printf("  Connecting to '%s'", kProfiles[i].ssid);
        WiFi.begin(kProfiles[i].ssid, pw);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            delay(250);
            Serial.print('.');
        }
        Serial.println();
        if (WiFi.status() == WL_CONNECTED) break;
        WiFi.disconnect(true);
        delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("  WiFi CONNECTED, ip=%s\n", WiFi.localIP().toString().c_str());
        if (MDNS.begin("tab5")) MDNS.addService("http", "tcp", 80);
    } else {
        Serial.println("  WiFi FAILED to connect.");
    }

    gSdOk = sdBenchInit();
    Serial.printf("  SD bench init: %s\n", gSdOk ? "OK" : "FAILED (SD.begin/card/alloc)");

    cmirror::Config mc;
    mc.manageWifi = false;
    mc.budgetUs = 8000;
    CardputerMirror.begin(mc, gAdapter);
    Serial.printf("  Mirror server up at http://%s/  (or http://tab5.local/)\n", CardputerMirror.ipAddress().c_str());

#ifdef SD_BENCH_TASK
    if (gSdOk) {
        // loop() runs pinned to APP_CPU (core 1) by arduino-esp32's own
        // startup code; pin this to PRO_CPU (core 0) so the two workloads
        // get real parallel hardware time instead of just OS-level
        // time-slicing on one core.
        xTaskCreatePinnedToCore(sdBenchTask, "sdbench", 8192, nullptr, 1, &gSdTaskHandle, 0);
    }
#endif

    Serial.println("=====================================================================");
}

static void heartbeat()
{
    static uint32_t last = 0;
    static uint32_t lastFrames = 0;
    if (millis() - last < 2000) return;

    const uint32_t frames = CardputerMirror.framesSent();
    const uint32_t elapsedInPhase = millis() % kSdPhaseMs;
    const bool sdPhaseActive = (millis() / kSdPhaseMs) % 2 == 1;

    Serial.printf("[%6lus] SD-phase=%-6s (t+%lus) clients=%d tiles/s=%lu heap=%u psram_free=%u\n",
                  millis() / 1000,
                  sdPhaseActive ? "ACTIVE" : "idle",
                  elapsedInPhase / 1000,
                  CardputerMirror.clientCount(),
                  (unsigned long)((frames - lastFrames) / 2),
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram());
    lastFrames = frames;

#ifdef CMIRROR_PROFILE
    auto p = CardputerMirror.profileStats();
    Serial.printf("           mirror: capture avg=%luus max=%luus (n=%lu) | "
                  "encode avg=%luus max=%luus (n=%lu) | "
                  "publish avg=%luus max=%luus (n=%lu)\n",
                  p.captureSamples ? (unsigned long)(p.captureUsTotal / p.captureSamples) : 0,
                  (unsigned long)p.captureUsMax, (unsigned long)p.captureSamples,
                  p.encodeSamples ? (unsigned long)(p.encodeUsTotal / p.encodeSamples) : 0,
                  (unsigned long)p.encodeUsMax, (unsigned long)p.encodeSamples,
                  p.publishSamples ? (unsigned long)(p.publishUsTotal / p.publishSamples) : 0,
                  (unsigned long)p.publishUsMax, (unsigned long)p.publishSamples);
#endif

    if (gSdOk) {
        SdStats s = gSdStats;
        const double writeKBs = s.writeSamples
            ? (kSdChunkBytes / 1024.0) / (s.writeUsTotal / (double)s.writeSamples / 1e6) : 0;
        const double readKBs = s.readSamples
            ? (kSdChunkBytes / 1024.0) / (s.readUsTotal / (double)s.readSamples / 1e6) : 0;
        Serial.printf("           sd: write avg=%lums max=%lums (n=%lu fail=%lu, %.0fKB/s) | "
                      "read avg=%lums max=%lums (n=%lu fail=%lu, %.0fKB/s)\n",
                      s.writeSamples ? (unsigned long)(s.writeUsTotal / s.writeSamples / 1000) : 0,
                      (unsigned long)(s.writeUsMax / 1000), (unsigned long)s.writeSamples, (unsigned long)s.writeFails, writeKBs,
                      s.readSamples ? (unsigned long)(s.readUsTotal / s.readSamples / 1000) : 0,
                      (unsigned long)(s.readUsMax / 1000), (unsigned long)s.readSamples, (unsigned long)s.readFails, readKBs);
    }
    last = millis();
}

// Phase edges reset both mirror and SD stats so each printed window reflects
// only the phase it's labeled with, not a blend with the previous one.
static void handlePhaseEdge()
{
    static bool lastPhase = false;
    const bool sdPhaseActive = (millis() / kSdPhaseMs) % 2 == 1;
    if (sdPhaseActive == lastPhase) return;
    lastPhase = sdPhaseActive;

#ifdef CMIRROR_PROFILE
    CardputerMirror.profileReset();
#endif
    gSdStats = SdStats{};
#ifdef SD_BENCH_TASK
    gSdPhaseActive = sdPhaseActive;
#endif
    Serial.printf("  -- SD phase edge: now %s --\n", sdPhaseActive ? "ACTIVE" : "idle");
}

void loop()
{
    M5.update();
    handlePhaseEdge();
    heartbeat();

    gAdapter.sink().poll();
    tab5kb::KeyEvent phys;
    gAdapter.sink().takePhysical(&phys);
    cmirror::RemoteKey remote;
    gAdapter.sink().takeRemote(&remote);

#ifndef SD_BENCH_TASK
    // Inline mode: run directly on loop()'s own call stack, so a blocking
    // SD SPI transaction here delays the very next CardputerMirror.update()
    // call below by exactly its own duration. This is the "worst case"
    // measurement -- see the file header.
    if (gSdOk) {
        const bool sdPhaseActive = (millis() / kSdPhaseMs) % 2 == 1;
        if (sdPhaseActive) sdBenchCycle(gSdStats);
    }
#endif

    CardputerMirror.update();
    delay(2);
}
