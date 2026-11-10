// SPDX-License-Identifier: GPL-3.0-only
#include "network.h"
#include "fixture_guard.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>

int main()
{
    if (!FixtureGuard()) return 2;
    uint8_t bytes[64]{};
    CE_NetWriter w;
    CE_NetWriterInit(&w, bytes, sizeof(bytes));
    CE_NetWriteU8(&w, 0xab); CE_NetWriteU16(&w, 0xcdef); CE_NetWriteU32(&w, 0x01234567);
    CE_NetWriteU64(&w, UINT64_C(0x89abcdef01234567)); CE_NetWriteF32(&w, 1.0f); CE_NetWriteF32(&w, -2.5f);
    const uint8_t expected[] = {0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0x01,0x23,0x45,0x67,
                                0x3f,0x80,0,0,0xc0,0x20,0,0};
    assert(!w.error && w.size == sizeof(expected) && std::memcmp(bytes, expected, sizeof(expected)) == 0);
    CE_NetReader r;
    CE_NetReaderInit(&r, bytes, w.size);
    assert(CE_NetReadU8(&r) == 0xab && CE_NetReadU16(&r) == 0xcdef);
    assert(CE_NetReadU32(&r) == 0x01234567 && CE_NetReadU64(&r) == UINT64_C(0x89abcdef01234567));
    assert(CE_NetReadF32(&r) == 1.0f && CE_NetReadF32(&r) == -2.5f && CE_NetReadDone(&r));
    CE_NetReadU8(&r); assert(r.error && !CE_NetReadDone(&r));
    for (uint32_t cut = 0; cut < sizeof(expected); ++cut) {
        CE_NetReaderInit(&r, bytes, cut);
        uint8_t copy[sizeof(expected)]; CE_NetReadBytes(&r, copy, sizeof(copy)); assert(r.error);
    }
    CE_NetWriterInit(&w, bytes, 1); CE_NetWriteU32(&w, 1); assert(w.error && w.size == 0);
    CE_NetWriterInit(&w, nullptr, 1); assert(w.error);
    CE_NetReaderInit(&r, nullptr, 1); assert(r.error);
    for (float x : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::quiet_NaN()}) {
        CE_NetWriterInit(&w, bytes, sizeof(bytes)); CE_NetWriteF32(&w, x); assert(w.error && w.size == 0);
    }
    for (uint32_t bits : {0x7f800000u, 0xff800000u, 0x7fc00000u}) {
        CE_NetWriterInit(&w, bytes, sizeof(bytes)); CE_NetWriteU32(&w, bits);
        CE_NetReaderInit(&r, bytes, w.size); assert(CE_NetReadF32(&r) == 0 && r.error);
    }
    const uint8_t trailing[] = {0,0};
    CE_NetReaderInit(&r, trailing, 2); CE_NetReadU8(&r); assert(!CE_NetReadDone(&r));
    std::puts("codec: golden endian/float bytes, every truncation, bounds, NaN/Inf and trailing data passed");
}
