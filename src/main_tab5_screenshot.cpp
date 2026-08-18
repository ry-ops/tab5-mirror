/*
 * M5Stack Tab5 — screenshot proof-of-life. ADR 0049.
 *
 * Smallest possible falsifiable claim: boot on real Tab5 hardware, draw a
 * known pattern across the full logical screen, read it back via M5GFX's
 * generic readRect(), and serve it as a BMP over HTTP. Either the browser's
 * download matches what's on the panel, or it doesn't.
 *
 * Deliberately standalone -- does not touch lib/CardputerMirror, menu.cpp,
 * keyinject.cpp, or wifi_manager*.cpp. See ADR 0049 for why.
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>
#include <memory>
#include <algorithm>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if __has_include("wifi_credentials.h")
  #include "wifi_credentials.h"
#else
  #error "Missing include/wifi_credentials.h - copy include/wifi_credentials.example.h to it and fill in your network."
#endif

namespace {

struct Profile { const char* ssid; const char* password; };
constexpr Profile kProfiles[] = { WIFI_PROFILES };

// Same placeholder trap as the Cardputer main.cpp: a REDACTED credentials
// file builds and flashes cleanly, then fails at runtime in a way
// indistinguishable from a sleeping AP. Catch it at compile time instead.
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

// ADR 0048/0049: arduino-esp32's default SDIO pins target the generic
// ESP32-P4 EvalBoard, not Tab5's wiring. Without this, WiFi.begin() fails
// with "H_SDIO_DRV: card init failed". CLK, CMD, D0, D1, D2, D3, RST.
constexpr int8_t kSdioClk = 12, kSdioCmd = 13, kSdioD0 = 11, kSdioD1 = 10,
                 kSdioD2 = 9,  kSdioD3 = 8,   kSdioRst = 15;

AsyncWebServer server(80);

void drawTestPattern(int w, int h)
{
    // Coarse checkerboard in the four corner-identifying colors already used
    // by ReadbackFrameSource::selfTest() on the Cardputer side, plus a
    // diagonal so orientation/mirroring bugs are visible at a glance instead
    // of only showing up as "wrong average color".
    static constexpr uint16_t kPat[4] = { 0xF800, 0x07E0, 0x001F, 0xFFFF }; // R,G,B,W
    const int cell = 40;
    M5.Display.startWrite();
    for (int y = 0; y < h; y += cell) {
        for (int x = 0; x < w; x += cell) {
            uint16_t c = kPat[((x / cell) + (y / cell)) & 3];
            M5.Display.fillRect(x, y, cell, cell, c);
        }
    }
    for (int i = 0; i < h && i < w; ++i) M5.Display.drawPixel(i, i, TFT_BLACK);
    M5.Display.endWrite();
    M5.Display.display();
}

// 24bpp BMP: 14B file header + 40B DIB header, bottom-up row order, BGR.
// One-shot only -- this is fine for a screenshot, would not survive a
// per-frame mirroring budget.
void putU16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
void putU32(uint8_t* p, uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF; }

// Returns nullptr on alloc failure. The shared_ptr's custom deleter calls
// free() (not delete[]) to match heap_caps_malloc, and keeps the buffer
// alive for exactly as long as ESPAsyncWebServer's streaming response needs
// it -- a raw pointer freed right after send() would be a use-after-free
// while the ~2.7 MB image is still being drained over multiple TCP writes.
std::shared_ptr<uint8_t> buildScreenshotBmp(size_t* outLen)
{
    const int w = M5.Display.width();
    const int h = M5.Display.height();
    const size_t rowSize = ((size_t)w * 3 + 3) & ~size_t(3); // 4-byte row alignment
    const size_t pixelBytes = rowSize * (size_t)h;
    const size_t fileSize = 54 + pixelBytes;

    // Read in row bands, not one readRect(0,0,w,h,...) call, and yield
    // between bands. A single full-1280x720 read + conversion pass ran long
    // enough with no yield to starve the hardware watchdog and reset the
    // whole chip (rst:0x7 HP_SYS_HP_WDT_RESET, seen on real hardware) --
    // this is a one-shot screenshot (ADR 0049), not a per-frame path, but it
    // still has to not take the system down to produce one image.
    constexpr int kBandRows = 32;
    uint16_t* rgb565 = (uint16_t*)heap_caps_malloc((size_t)w * kBandRows * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgb565) return nullptr;

    uint8_t* buf = (uint8_t*)heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { free(rgb565); return nullptr; }

    buf[0] = 'B'; buf[1] = 'M';
    putU32(buf + 2, (uint32_t)fileSize);
    putU32(buf + 6, 0);
    putU32(buf + 10, 54);           // pixel data offset
    putU32(buf + 14, 40);           // DIB header size
    putU32(buf + 18, (uint32_t)w);
    putU32(buf + 22, (uint32_t)h);  // positive height = bottom-up
    putU16(buf + 26, 1);            // planes
    putU16(buf + 28, 24);           // bits per pixel
    putU32(buf + 30, 0);            // BI_RGB, no compression
    putU32(buf + 34, (uint32_t)pixelBytes);
    putU32(buf + 38, 2835);         // ~72 DPI
    putU32(buf + 42, 2835);
    putU32(buf + 46, 0);
    putU32(buf + 50, 0);

    for (int bandTop = 0; bandTop < h; bandTop += kBandRows) {
        const int bandH = std::min(kBandRows, h - bandTop);
        // rgb565_t overload converts from the panel's read depth with no
        // byte swap -- see ReadbackFrameSource.cpp's note on the uint16_t*
        // trap. One band, not the whole screen, per readRect() call.
        M5.Display.readRect(0, bandTop, w, bandH, (lgfx::rgb565_t*)rgb565);

        for (int row = 0; row < bandH; ++row) {
            const int srcY = bandTop + row;
            // Bottom-up: file row 0 is the image's last row.
            const int dstY = h - 1 - srcY;
            const uint16_t* src = rgb565 + (size_t)row * w;
            uint8_t* dst = buf + 54 + (size_t)dstY * rowSize;
            for (int x = 0; x < w; ++x) {
                const lgfx::rgb565_t px(src[x]);
                dst[x * 3 + 0] = px.B8(); // BMP wants BGR
                dst[x * 3 + 1] = px.G8();
                dst[x * 3 + 2] = px.R8();
            }
        }
        vTaskDelay(1); // feed the watchdog between bands
    }

    free(rgb565);
    *outLen = fileSize;
    return std::shared_ptr<uint8_t>(buf, [](uint8_t* p) { free(p); });
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
    Serial.println("===== M5Stack Tab5 screenshot proof (ADR 0049) =====");
    Serial.printf("  Board width x height (post-rotation target): 1280x720 expected\n");

    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(10, 10);
    M5.Display.println("Tab5 screenshot proof - ADR 0049");

    Serial.printf("  Display reports: %dx%d\n", M5.Display.width(), M5.Display.height());
    Serial.printf("  PSRAM: %u B\n", (unsigned)ESP.getPsramSize());

    drawTestPattern(M5.Display.width(), M5.Display.height());

    WiFi.setPins(kSdioClk, kSdioCmd, kSdioD0, kSdioD1, kSdioD2, kSdioD3, kSdioRst);
    WiFi.mode(WIFI_STA);
    Serial.printf("  STA MAC: %s\n", WiFi.macAddress().c_str());

    // wifi_credentials.h's own comment (and the original Cardputer
    // wifi_manager.cpp:200) already document this: an empty password string
    // must become nullptr at the WiFi.begin() call site to select open
    // association. Passing "" directly (what an earlier version of this
    // sketch did) is NOT the same thing to the underlying driver and was the
    // likely cause of AUTH_FAIL against the open MDC(IoT) network.
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
    // Re-read here, not right after WiFi.mode(WIFI_STA) -- the hosted C6
    // co-processor link isn't fully up that early and macAddress() reads
    // back all-zeros until a connection attempt has actually run.
    Serial.printf("  STA MAC (post-attempt): %s\n", WiFi.macAddress().c_str());

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("  WiFi CONNECTED, ip=%s\n", WiFi.localIP().toString().c_str());
        M5.Display.printf("WiFi: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("  WiFi FAILED to connect within 20s.");
        M5.Display.println("WiFi: FAILED");
    }

    server.on("/screenshot.bmp", HTTP_GET, [](AsyncWebServerRequest* req) {
        size_t len = 0;
        std::shared_ptr<uint8_t> buf = buildScreenshotBmp(&len);
        if (!buf) { req->send(500, "text/plain", "screenshot alloc failed"); return; }
        // Streaming callback overload: buf is captured by value (shared_ptr,
        // ref-counted) so it stays alive until this fill callback has been
        // invoked for the last chunk, then frees itself. index/maxLen are a
        // byte-range request into the already-fully-built buffer -- the
        // "streaming" here is only about the HTTP transfer, not the BMP
        // build, which already happened synchronously above.
        AsyncWebServerResponse* resp = req->beginResponse(
            "image/bmp", len,
            [buf, len](uint8_t* dst, size_t maxLen, size_t index) -> size_t {
                if (index >= len) return 0;
                size_t n = std::min(maxLen, len - index);
                memcpy(dst, buf.get() + index, n);
                return n;
            });
        req->send(resp);
    });
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html",
            "<html><body style='margin:0;background:#111'>"
            "<img src='/screenshot.bmp' style='max-width:100%;height:auto;display:block'>"
            "</body></html>");
    });
    server.begin();
    Serial.println("  HTTP server up: GET /screenshot.bmp");
    Serial.println("=====================================================");
}

void loop()
{
    M5.update();
    delay(10);
}
