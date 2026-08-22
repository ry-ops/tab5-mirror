# ADR 0056 — BLE scanner proof of life

**Status:** Accepted — implemented, verified on real hardware
(`env:tab5-ble`): real nearby BLE advertisers detected and printed
(address, RSSI, name where advertised — e.g. a Nespresso Vertuo machine, a
named speaker), with no WiFi connection active.
**Deciders:** firmware owner
**Related:** ADR 0048/0049 (WiFi.setPins()/begin() bring-up over the same
ESP-Hosted SDIO link this sketch also depends on)

## Context

Tab5 has no onboard Bluetooth radio — like WiFi, BLE is routed through the
ESP32-C6 co-processor over the same SDIO ESP-Hosted link (ADR 0048).
Untested before this sketch: whether the standard arduino-esp32
`BLEDevice`/`BLEScan` API works over that hosted link on ESP32-P4 at all,
and specifically whether it works with **no WiFi connection active** (only
the hosted transport itself brought up via `WiFi.setPins()` +
`WiFi.mode(WIFI_MODE_NULL)`, not a real STA/AP session).

## Decision

Standalone `src/main_tab5_ble.cpp` (`env:tab5-ble`), same isolation pattern
as every other proof-of-life sketch in this repo. Brings up the hosted SDIO
link the same way every other sketch does (`WiFi.setPins()`), but with
`WiFi.mode(WIFI_MODE_NULL)` instead of `WIFI_STA` — link up, no network
session — then calls the plain `BLEDevice::init()` / `BLEScan` API
unmodified.

**Result:** works immediately, no adaptation needed. Real advertisers
detected within the first scan cycle (~6s from boot), including named
devices (BLE name field parsed correctly) and RSSI. `CORE_DEBUG_LEVEL`
lowered to 1 for this env specifically — the library's own internal
`handleGAPEvent()` logging floods several lines per advertisement per scan
cycle at the base env's level (3), which is fine for a one-off diagnostic
but not for something meant to double as a usable on-device app.

## Consequences

**Positive**
- Confirms BLE (not just WiFi) works over Tab5's ESP-Hosted link, and
  works standalone (no WiFi connection prerequisite) — opens the door to
  any future BLE-only tooling on this hardware without needing a WiFi
  network to be present.
- Zero adaptation needed versus how `BLEDevice`/`BLEScan` are used on any
  other Arduino ESP32 board — the hosted-transport difference is entirely
  invisible at this API level.

**Negative / open**
- Only tested passive discovery (scanning/observing advertisements). GATT
  client/server roles (connecting to a peripheral, exposing services) are
  unverified on this hardware.
- This is a proof-of-life sketch, not a polished app — on-screen output is
  a simple scrolling list, no filtering/sorting/persistence.
