/*
 * Tab5Adapter implementation.
 * SPDX-License-Identifier: MIT
 */
#include "tab5_adapter.h"

namespace tab5adapter {

namespace {
Tab5InputSink* s_instance = nullptr;
}

void Tab5InputSink::begin()
{
    s_instance = this;
    _kb.begin(&Tab5InputSink::_trampoline);
}

void Tab5InputSink::_trampoline(const tab5kb::KeyEvent& ke)
{
    if (s_instance) s_instance->_onPhysical(ke);
}

void Tab5InputSink::_onPhysical(const tab5kb::KeyEvent& ke)
{
    _lastPhysical = ke;
    _havePhysical = true;
}

void Tab5InputSink::inject(const cmirror::RemoteKey& k)
{
    // Non-blocking, no display touch -- see the class comment. Just a
    // plain field write: single producer (AsyncTCP), single consumer
    // (loop() via takeRemote()), and a torn read here would at worst show
    // one stale/mixed key label for one frame, not a real hazard.
    _lastRemote = k;
    _haveRemote = true;
}

void Tab5InputSink::poll()
{
    _kb.poll();
}

bool Tab5InputSink::takeRemote(cmirror::RemoteKey* out)
{
    if (!_haveRemote) return false;
    _haveRemote = false;
    *out = _lastRemote;
    return true;
}

bool Tab5InputSink::takePhysical(tab5kb::KeyEvent* out)
{
    if (!_havePhysical) return false;
    _havePhysical = false;
    *out = _lastPhysical;
    return true;
}

}  // namespace tab5adapter
