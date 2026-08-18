/*
 * Minimal Serial/USB-CDC isolation test. No M5Unified, no WiFi, no display.
 * If this doesn't print, the problem is HWCDC/USB itself, not our
 * application code. See ADR 0049 addendum.
 * SPDX-License-Identifier: MIT
 */
#include <Arduino.h>

static uint32_t n = 0;

void setup()
{
    Serial.begin(115200);
}

void loop()
{
    Serial.printf("alive %lu\n", (unsigned long)n++);
    delay(500);
}
