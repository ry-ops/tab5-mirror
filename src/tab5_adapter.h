/*
 * Tab5Adapter — wires the Tab5 keyboard accessory (ADR 0052) into
 * lib/CardputerMirror's adapter architecture (ADR 0038), so remote
 * (browser) key clicks and physical key presses both flow through
 * IInputSink instead of the library's default no-input begin(cfg) path.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "CardputerMirror.h"
#include "tab5_keyboard.h"

namespace tab5adapter {

// IInputSink::inject()/injectBtn() run on the AsyncTCP task and MUST NOT
// block or touch the display (see IInputSink.h's own contract) -- this
// class only ever stores the latest event; the caller's loop() drains it
// via take*() and does the actual M5.Display drawing there.
class Tab5InputSink : public cmirror::IInputSink {
public:
    void begin() override;
    void inject(const cmirror::RemoteKey& k) override;
    void injectBtn(uint8_t btn, uint16_t ms) override {} // Tab5 has no G0/Reset-style top buttons
    void poll() override;

    // Non-blocking drain, called from loop(). Returns false (leaves *out
    // untouched) if nothing new arrived since the last call.
    bool takeRemote(cmirror::RemoteKey* out);
    bool takePhysical(tab5kb::KeyEvent* out);

    uint8_t keyboardVersion() const { return _kb.version(); }

private:
    static void _trampoline(const tab5kb::KeyEvent& ke);
    void _onPhysical(const tab5kb::KeyEvent& ke);

    tab5kb::Tab5Keyboard _kb;
    volatile bool _haveRemote = false;
    cmirror::RemoteKey _lastRemote{0, 0, false, false};
    volatile bool _havePhysical = false;
    tab5kb::KeyEvent _lastPhysical{false, 0, 0};
};

class Tab5Adapter : public cmirror::IHostAdapter {
public:
    void begin() override {} // no-op: the library calls sink.begin()/frameSource.begin() itself
    cmirror::IFrameSource& frameSource() override { return _src; }
    cmirror::IInputSink& inputSink() override { return _sink; }
    cmirror::PortMutex* busLock() override { return nullptr; } // no dedicated render task competes for the panel, same reasoning as the Cardputer ADV's own adapters

    Tab5InputSink& sink() { return _sink; }

private:
    cmirror::ReadbackFrameSource _src;
    Tab5InputSink _sink;
};

}  // namespace tab5adapter
