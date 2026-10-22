// SPDX-License-Identifier: GPL-3.0-only
// DXGetErrorString9A: the HRESULT names DxErr9 reports, for the game's log lines. The table (dxerr_names.inc) is
// generated from the retail oracle's output over every D3D-, Win32- and OLE-facility code; anything else is
// "Unknown", as retail answers (port/d3dx/README.md).
#include <dxerr9.h>

namespace {

struct Name
{
    unsigned int hr;
    const char *name;
};

const Name kNames[] = {
#include "dxerr_names.inc"
};

} // namespace

extern "C" const char *WINAPI DXGetErrorString9A(HRESULT hr)
{
    unsigned int key = (unsigned int)hr;
    size_t lo = 0, hi = sizeof(kNames) / sizeof(kNames[0]);
    while (lo < hi)
    {
        size_t mid = (lo + hi) / 2;
        if (kNames[mid].hr < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < sizeof(kNames) / sizeof(kNames[0]) && kNames[lo].hr == key ? kNames[lo].name : "Unknown";
}
