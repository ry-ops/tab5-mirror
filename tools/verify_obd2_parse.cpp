// Host-buildable test for src/obd2_parse.h -- same split as
// tools/verify_codec.cpp / lib/CardputerMirror/Codec.h. See ADR 0058.
//
// SPDX-License-Identifier: MIT
#include "../src/obd2_parse.h"
#include <cstdio>
using namespace obd2;

struct Case { const char* name; const char* resp; int want; };

int main(){
  // 0x1A0F8... wait: A=0x1A=26, B=0xF8=248 -> (26*256+248)/4 = 6904/4 = 1726
  const Case cases[] = {
    {"spaced",                 "41 0C 1A F8",                         1726},
    {"unspaced",               "410C1AF8",                            1726},
    {"trailing prompt",        "41 0C 1A F8\r\r>",                    1726},
    {"echo not suppressed",    "010C\r41 0C 1A F8\r\r>",              1726},
    {"SEARCHING prefix",       "SEARCHING...\r41 0C 1A F8\r\r>",      1726},
    {"zero rpm",               "41 0C 00 00",                         0},
    {"NO DATA",                "NO DATA",                             -1},
    {"STOPPED",                "STOPPED",                             -1},
    {"unsupported cmd",        "?",                                   -1},
    {"empty",                  "",                                    -1},
    {"truncated after marker", "41 0C 1A",                            -1},
    {"garbage hex",            "41 0C GG F8",                         -1},
    {"null",                   nullptr,                               -1},
  };
  int fail = 0;
  for (const auto& c : cases) {
    int got = parseRpm(c.resp);
    bool ok = (got == c.want);
    printf("%-24s got=%-6d want=%-6d %s\n", c.name, got, c.want, ok ? "OK" : "FAIL");
    if (!ok) fail++;
  }
  printf(fail ? "%d case(s) FAILED\n" : "all cases OK\n", fail);
  return fail ? 1 : 0;
}
