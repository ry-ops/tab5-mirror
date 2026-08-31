// Pure ELM327 response parsing -- no Arduino/BLE dependency, so it builds
// and runs identically on-device (src/main_sticks3_obd2.cpp) and on the
// host (tools/verify_obd2_parse.cpp). Same split as lib/CardputerMirror/
// Codec.h + tools/verify_codec.cpp. See ADR 0058.
//
// SPDX-License-Identifier: MIT
#pragma once
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace obd2 {

// Parses a mode 01 PID 0C (engine RPM) response, e.g. "41 0C 1A F8" or
// "410C1AF8", tolerating a preceding command echo and/or an ELM327
// "SEARCHING...\r" line before the real response (both are common when
// echo (ATE0) hasn't taken effect yet, or the adapter is still selecting
// a protocol after ATSP0). Returns -1 for anything that doesn't contain a
// well-formed "41 0C" reply -- including error strings like "NO DATA",
// "STOPPED", "?", or an empty/garbage buffer.
inline int parseRpm(const char* resp)
{
    if (resp == nullptr) return -1;
    const char* hit = strstr(resp, "41 0C");
    if (hit == nullptr) hit = strstr(resp, "410C");
    if (hit == nullptr) return -1;

    // Strip spaces from just the part starting at the match, so both
    // "41 0C 1A F8" and "410C1AF8" (and mixed spacing) land in the same
    // shape: "410C" followed by the two data-byte hex pairs.
    char packed[16];
    size_t n = 0;
    for (const char* p = hit; *p != '\0' && n < sizeof(packed) - 1; ++p) {
        if (*p != ' ') packed[n++] = *p;
    }
    packed[n] = '\0';
    if (n < 8) return -1; // "410C" + 4 hex digits

    char byteA[3] = {packed[4], packed[5], '\0'};
    char byteB[3] = {packed[6], packed[7], '\0'};
    if (!isxdigit(static_cast<unsigned char>(byteA[0])) || !isxdigit(static_cast<unsigned char>(byteA[1])) ||
        !isxdigit(static_cast<unsigned char>(byteB[0])) || !isxdigit(static_cast<unsigned char>(byteB[1]))) {
        return -1;
    }
    long a = strtol(byteA, nullptr, 16);
    long b = strtol(byteB, nullptr, 16);
    return static_cast<int>((a * 256 + b) / 4);
}

} // namespace obd2
