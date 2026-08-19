/*
 * Tab5Keyboard implementation. ADR 0052.
 * SPDX-License-Identifier: MIT
 */
#include "tab5_keyboard.h"

namespace tab5kb {

// The GLOBAL Wire (hardware I2C peripheral 0), deliberately -- NOT a
// second TwoWire(1) instance. Real boot log evidence: M5Unified's own
// system I2C (display/touch/audio/IMU, SDA=31/SCL=32, ADR 0048) already
// claims peripheral 1 ("i2cInit(): ... num=1 sda=31 scl=32"), which is
// presumably deliberate on M5Unified's part specifically so the global
// Wire (peripheral 0) stays free for application code. A first attempt at
// TwoWire(1) here collided with that ("Bus already started in Master
// Mode", then a NULL TX buffer on every write) -- confirmed on real
// hardware, not theorised.
static TwoWire& s_wire = Wire;

bool Tab5Keyboard::_readReg(uint8_t reg, uint8_t* value)
{
    s_wire.beginTransmission(kAddr);
    s_wire.write(reg);
    if (s_wire.endTransmission(false) != 0) return false; // repeated start, no stop
    if (s_wire.requestFrom((int)kAddr, 1) != 1) return false;
    *value = s_wire.read();
    return true;
}

bool Tab5Keyboard::_writeReg(uint8_t reg, uint8_t value)
{
    s_wire.beginTransmission(kAddr);
    s_wire.write(reg);
    s_wire.write(value);
    return s_wire.endTransmission() == 0;
}

bool Tab5Keyboard::begin(void (*onEvent)(const KeyEvent&))
{
    _onEvent = onEvent;
    s_wire.begin(kPinSda, kPinScl, 100000);
    pinMode(kPinInt, INPUT); // not used yet (polling only, ADR 0052) -- reserved for hardware-interrupt mode later

    if (!_readReg(kRegVersion, &_version)) {
        _ok = false;
        return false;
    }
    _ok = _writeReg(kRegKeyboardMode, kModeNormal);
    return _ok;
}

void Tab5Keyboard::poll()
{
    if (!_ok) return;

    uint8_t status = 0;
    if (!_readReg(kRegIntSta, &status)) return;
    if (!(status & 0x01)) return; // no Normal-mode event pending

    uint8_t count = 0;
    if (!_readReg(kRegEventNum, &count)) return;

    while (count > 0) {
        uint8_t event = 0;
        if (_readReg(kRegKeyEvent, &event) && event != 0xFF) {
            KeyEvent ke;
            ke.pressed = (event & 0x80) != 0;
            ke.row     = (event >> 4) & 0x07;
            ke.col     =  event       & 0x0F;
            if (_onEvent) _onEvent(ke);
        }
        --count;
        if (count > 32) break; // safety, matches the official driver's own guard
    }
    // Explicitly clear EVENT_NUM too, not just INT_STA. The official
    // driver only clears INT_STA here, assuming each KEY_EVENT read
    // auto-decrements the device's own count -- on real hardware that
    // assumption produced a continuous, well-formed press/release stream
    // at a single fixed (row,col) with nothing touching the keyboard,
    // consistent with the count never actually reaching zero and the same
    // queued entry being re-read every poll(). This clear is defensive:
    // safe if the count really did auto-decrement (writing 0 to an
    // already-0 register), and fixes it if it didn't.
    _writeReg(kRegEventNum, 0x00);
    _writeReg(kRegIntSta, 0x00); // clear
}

const char* legendFor(uint8_t row, uint8_t col)
{
    // Same order as web/index.html's #tab5kb markup (ADR 0051), confirmed
    // 1:1 against real hardware (ADR 0052 addendum).
    static const char* const kLegend[5][14] = {
        {"esc","1","2","3","4","5","6","7","8","9","0","-","+","del"},
        {"`~","!?","@","#","$","%","^","&","*/","(<",")>","[{","]}","\\|"},
        {"tab","Q","W","E","R","T","Y","U","I","O","P",";","'","backspace"},
        {"sym","Aa","A","S","D","F","G","H","J","K","L","up","_=","enter"},
        {"ctrl","alt","Z","X","C","V","B","N","M","middle-dot","left","down","right","space"},
    };
    if (row >= 5 || col >= 14) return "?";
    return kLegend[row][col];
}

}  // namespace tab5kb
