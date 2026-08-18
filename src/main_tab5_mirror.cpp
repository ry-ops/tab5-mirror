/*
 * M5Stack Tab5 — continuous display mirror. Mirroring milestone, follows
 * ADR 0049's screenshot proof.
 *
 * Reuses the same boot/WiFi bring-up and checkerboard test pattern the
 * screenshot sketch proved correct on real hardware -- the content being
 * mirrored isn't the point of this milestone, continuous tile-budgeted
 * readback + WebSocket push is. lib/CardputerMirror's Mirror/
 * ReadbackFrameSource classes (ADR 0002/0038) are reused verbatim; only
 * CardputerMirror.h's geometry constants changed, for 1280x720 (ADR 0048).
 *
 * Deliberately standalone from main_tab5_screenshot.cpp -- see
 * platformio.ini's [env:tab5-mirror], which excludes it.
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
#include <WiFi.h>
#include "CardputerMirror.h"

#if __has_include("wifi_credentials.h")
  #include "wifi_credentials.h"
#else
  #error "Missing include/wifi_credentials.h - copy include/wifi_credentials.example.h to it and fill in your network."
#endif

namespace {

struct Profile { const char* ssid; const char* password; };
constexpr Profile kProfiles[] = { WIFI_PROFILES };

// Same placeholder trap as main_tab5_screenshot.cpp / the Cardputer main.cpp.
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

// ADR 0048/0049: Tab5's SDIO wiring differs from the generic ESP32-P4
// EvalBoard arduino-esp32 defaults to. CLK, CMD, D0, D1, D2, D3, RST.
constexpr int8_t kSdioClk = 12, kSdioCmd = 13, kSdioD0 = 11, kSdioD1 = 10,
                 kSdioD2 = 9,  kSdioD3 = 8,   kSdioRst = 15;

constexpr int kCheckerCell = 40;
constexpr uint16_t kCheckerPat[4] = { 0xF800, 0x07E0, 0x001F, 0xFFFF }; // R,G,B,W

uint16_t checkerColorAt(int x, int y)
{
    return kCheckerPat[((x / kCheckerCell) + (y / kCheckerCell)) & 3];
}

void drawTestPattern(int w, int h)
{
    M5.Display.startWrite();
    for (int y = 0; y < h; y += kCheckerCell) {
        for (int x = 0; x < w; x += kCheckerCell) {
            M5.Display.fillRect(x, y, kCheckerCell, kCheckerCell, checkerColorAt(x, y));
        }
    }
    for (int i = 0; i < h && i < w; ++i) M5.Display.drawPixel(i, i, TFT_BLACK);
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
    Serial.println("===== M5Stack Tab5 continuous mirror (mirroring milestone) =====");

    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK);
    Serial.printf("  Display reports: %dx%d\n", M5.Display.width(), M5.Display.height());
    Serial.printf("  PSRAM: %u B\n", (unsigned)ESP.getPsramSize());
    Serial.printf("  Mirror tile grid: %dx%d cols/rows, %dx%d px/tile, %d tiles\n",
                  cmirror::kTileCols, cmirror::kTileRows,
                  cmirror::kTileW, cmirror::kTileH, cmirror::kNumTiles);

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
    } else {
        Serial.println("  WiFi FAILED to connect. Mirror server will still start (SoftAP-less; see manageWifi=false).");
    }

    cmirror::Config mc;
    mc.manageWifi = false; // we already brought WiFi up above
    // Starting budget: unlike the Cardputer ADV's SPI readback (4.05ms/tile
    // measured over 16MHz 3-wire SIO, ADR 0002), Tab5's readRect() reads a
    // RAM line-buffer directly, no bus at all -- real per-tile cost is
    // unmeasured on this hardware yet. Deliberately conservative starting
    // point; watch the heartbeat's tiles/sec and raise if there's headroom.
    mc.budgetUs = 8000;
    CardputerMirror.begin(mc);

    Serial.printf("  Mirror server up at http://%s/\n", CardputerMirror.ipAddress().c_str());
    Serial.println("==================================================================");
}

static void heartbeat()
{
    static uint32_t last = 0;
    static uint32_t lastFrames = 0;
    if (millis() - last < 2000) return;
    const uint32_t frames = CardputerMirror.framesSent();
    Serial.printf("[%6lus] ip=%-15s clients=%d tiles_sent_total=%lu tiles/s=%lu heap=%u psram_free=%u\n",
                  millis() / 1000,
                  CardputerMirror.ipAddress().c_str(),
                  CardputerMirror.clientCount(),
                  (unsigned long)frames,
                  (unsigned long)((frames - lastFrames) / 2),
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram());
    lastFrames = frames;
    last = millis();
}

// A static screen only proves the one-time full-frame push on connect
// (WS_EVT_CONNECT -> forceFullFrame()). This exercises the actual dirty-tile
// path continuously -- a small bouncing box, small enough that each move
// only touches 1-2 tiles, so the CRC/change-detection in scanOneTile() has
// something real to detect rather than a wholesale redraw every frame.
static void animate()
{
    static uint32_t last = 0;
    static int x = 0, y = 0, dx = 6, dy = 5;
    if (millis() - last < 33) return; // ~30 fps content; mirror's own budget paces the network side
    last = millis();

    const int w = M5.Display.width(), h = M5.Display.height();
    constexpr int kBox = 24;
    M5.Display.startWrite();
    // Restore the checkerboard under the old box position pixel-by-pixel --
    // kBox (24) doesn't align with the checkerboard's cell size (40), so a
    // flat fillRect erase would paint over part of a differently-colored
    // cell rather than actually restoring the background.
    for (int py = y; py < y + kBox; ++py)
        for (int px = x; px < x + kBox; ++px)
            M5.Display.drawPixel(px, py, checkerColorAt(px, py));
    x += dx; y += dy;
    if (x <= 0 || x + kBox >= w) dx = -dx;
    if (y <= 0 || y + kBox >= h) dy = -dy;
    M5.Display.fillRect(x, y, kBox, kBox, TFT_WHITE);
    M5.Display.endWrite();
    M5.Display.display();
}

void loop()
{
    M5.update();
    heartbeat();
    animate();
    CardputerMirror.update();
    delay(2);
}
