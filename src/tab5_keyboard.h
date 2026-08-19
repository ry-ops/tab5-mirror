/*
 * Tab5Keyboard — minimal driver for the M5Stack Tab5 keyboard accessory.
 * ADR 0052.
 *
 * Hand-written against the register map verified from M5Stack's own source
 * (github.com/m5stack/M5Tab5-Keyboard-UserDemo), not vendored -- see the
 * ADR for why. Normal mode only: row/col press/release events, nothing
 * else (HID/String/BLE modes, RGB LEDs) this repo doesn't need yet.
 *
 * KEYMAP (confirmed empirically on real hardware via a guided wizard, see
 * the ADR's addendum -- two complete, gap-free 70-key runs, zero
 * exceptions): row/col match web/index.html's #tab5kb key order (ADR
 * 0051) exactly, row = index/14, col = index%14 --
 *   row 0  esc 1 2 3 4 5 6 7 8 9 0 - + del
 *   row 1  ` ~  ! ?  @  #  $  %  ^  &  * /  ( <  ) >  [ {  ] }  \ |
 *   row 2  tab Q W E R T Y U I O P ; ' backspace
 *   row 3  sym Aa A S D F G H J K L up-arrow _= enter
 *   row 4  ctrl alt Z X C V B N M middle-dot left-arrow down-arrow right-arrow space
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace tab5kb {

constexpr uint8_t kAddr = 0x6D;
constexpr int kPinSda = 0, kPinScl = 1, kPinInt = 50;

// Register map, verified against real source -- see ADR 0052.
constexpr uint8_t kRegIntCfg      = 0x00;
constexpr uint8_t kRegIntSta      = 0x01;
constexpr uint8_t kRegEventNum    = 0x02;
constexpr uint8_t kRegKeyboardMode = 0x10;
constexpr uint8_t kRegKeyEvent    = 0x20;
constexpr uint8_t kRegVersion     = 0xFE;

constexpr uint8_t kModeNormal = 0;

struct KeyEvent {
    bool pressed;
    uint8_t row;  // 0-4
    uint8_t col;  // 0-13
};

class Tab5Keyboard {
public:
    // onEvent is called synchronously from poll(), on the caller's own
    // task -- same non-blocking-caller contract as IInputSink::inject().
    bool begin(void (*onEvent)(const KeyEvent&));
    // Drains all pending events via one I2C transaction round-trip per
    // event (matches the official driver's own drain loop). Call from
    // loop(); cheap no-op when INT_STA's bit0 isn't set.
    void poll();
    uint8_t version() const { return _version; }

private:
    bool _readReg(uint8_t reg, uint8_t* value);
    bool _writeReg(uint8_t reg, uint8_t value);

    void (*_onEvent)(const KeyEvent&) = nullptr;
    uint8_t _version = 0;
    bool _ok = false;
};

}  // namespace tab5kb
