/*
 * M5Stack Tab5 — keyboard accessory proof-of-life + guided keymap wizard.
 * ADR 0052.
 *
 * Phase 1 (proven): bring up the I2C keyboard accessory, read its VERSION
 * register to prove the bus/address are right, then poll for real key
 * press/release events. Confirmed working on real hardware -- the row/col
 * numbers the physical matrix reports do NOT match this file's own
 * assumed visual layout order (that assumption was never verified against
 * anything), so this adds a guided wizard: prompts through all 70 keys in
 * the SAME order the dashboard mockup (ADR 0051) uses, and correlates each
 * prompt with whatever (row,col) the hardware actually reports when you
 * press it -- the same kind of empirical keymap discovery the Cardputer
 * ADV project needed for its own keyboard (see that project's ADR
 * 0017-0020 history).
 *
 * Deliberately standalone -- no WiFi, no mirror. Isolates the one new
 * variable (the keyboard accessory's I2C bus) the same way
 * main_serial_test.cpp isolated USB-CDC from M5Unified/WiFi in ADR 0049.
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
#include "tab5_keyboard.h"

namespace {

tab5kb::Tab5Keyboard kb;
uint32_t gPressCount = 0, gReleaseCount = 0;
tab5kb::KeyEvent gLast{false, 0, 0};
bool gHaveEvent = false;
bool gNeedRedraw = true;

// Same order as web/index.html's #tab5kb markup (ADR 0051) -- 5 rows x 14
// cols, 70 keys. Purely a PROMPT sequence; the physical matrix's own
// row/col numbering is independent of this order, which is the whole
// point of running the wizard.
const char* kExpected[] = {
    "esc","1","2","3","4","5","6","7","8","9","0","-","+","del",
    "`~","!?","@","#","$","%","^","&","*/","(<",")>","[{","]}","\\|",
    "tab","Q","W","E","R","T","Y","U","I","O","P",";","'","backspace",
    "sym","Aa","A","S","D","F","G","H","J","K","L","up-arrow","_=","enter",
    "ctrl","alt","Z","X","C","V","B","N","M","middle-dot","left-arrow","down-arrow","right-arrow","space",
};
constexpr int kExpectedCount = sizeof(kExpected) / sizeof(kExpected[0]);
int gWizardIndex = 0;

void restartWizard()
{
    gWizardIndex = 0;
    Serial.println("  --- keymap wizard restarted ---");
}

void onKeyEvent(const tab5kb::KeyEvent& ke)
{
    gLast = ke;
    gHaveEvent = true;
    gNeedRedraw = true;
    if (ke.pressed) ++gPressCount; else ++gReleaseCount;
    Serial.printf("  key %-8s row=%u col=%u\n", ke.pressed ? "PRESSED" : "released", ke.row, ke.col);

    // Advance the wizard only on PRESS, not release -- one prompt per
    // physical key-down, matching how you'd naturally press through them.
    if (ke.pressed && gWizardIndex < kExpectedCount) {
        Serial.printf("  WIZARD %2d/%2d  expected=%-12s -> row=%u col=%u\n",
                      gWizardIndex + 1, kExpectedCount, kExpected[gWizardIndex], ke.row, ke.col);
        ++gWizardIndex;
        if (gWizardIndex == kExpectedCount) {
            Serial.println("  --- keymap wizard complete -- scroll up for the full table ---");
        }
    }
}

void redraw()
{
    M5.Display.fillRect(0, 60, M5.Display.width(), 260, TFT_BLACK);
    M5.Display.setCursor(10, 60);
    M5.Display.setTextSize(3);
    if (gWizardIndex < kExpectedCount) {
        M5.Display.printf("Press: %s\n(%d/%d)\n", kExpected[gWizardIndex], gWizardIndex + 1, kExpectedCount);
    } else {
        M5.Display.println("WIZARD COMPLETE\nsee serial log");
    }
    M5.Display.setTextSize(2);
    if (gHaveEvent) {
        M5.Display.printf("\nlast: row=%u col=%u %s\n", gLast.row, gLast.col, gLast.pressed ? "PRESSED" : "released");
    }
    M5.Display.printf("presses=%lu releases=%lu\n",
                      (unsigned long)gPressCount, (unsigned long)gReleaseCount);
    M5.Display.println("\n(serial 'r' restarts wizard)");
}

} // namespace

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    for (uint32_t t = millis(); !Serial && millis() - t < 2500; ) delay(50);
    delay(400);

    M5.Display.setRotation(1);
    M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(10, 10);
    M5.Display.println("Tab5 keyboard proof - ADR 0052");

    Serial.println();
    Serial.println("===== M5Stack Tab5 keyboard proof (ADR 0052) =====");

    bool ok = kb.begin(onKeyEvent);
    Serial.printf("  keyboard begin=%d version=0x%02X\n", ok, kb.version());
    M5.Display.printf("kb begin=%d version=0x%02X\n", ok, kb.version());
    if (!ok) {
        M5.Display.println("FAILED - check I2C wiring/address");
    } else {
        Serial.printf("  Press keys in order -- wizard will prompt %d keys.\n", kExpectedCount);
    }

    redraw();
    Serial.println("===================================================");
}

void loop()
{
    M5.update();
    kb.poll();

    if (Serial.available() && Serial.read() == 'r') {
        restartWizard();
        gNeedRedraw = true;
    }

    if (gNeedRedraw) {
        gNeedRedraw = false;
        redraw();
    }
    delay(2);
}
