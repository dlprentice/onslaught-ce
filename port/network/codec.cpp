// SPDX-License-Identifier: GPL-3.0-only
#include "network.h"
#include <cmath>
#include <cstring>
#include <limits>

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "network float requires IEEE binary32");

void CE_NetWriterInit(CE_NetWriter *w, uint8_t *p, uint32_t n) { *w = {p, n, 0, !p && n != 0}; }
void CE_NetWriteBytes(CE_NetWriter *w, const uint8_t *p, uint32_t n)
{
    if (w->error || w->size > w->capacity || n > w->capacity - w->size || (!p && n)) { w->error = 1; return; }
    if (n) std::memcpy(w->data + w->size, p, n);
    w->size += n;
}
void CE_NetWriteU8(CE_NetWriter *w, uint8_t x) { CE_NetWriteBytes(w, &x, 1); }
void CE_NetWriteU16(CE_NetWriter *w, uint16_t x)
{ uint8_t p[] = {uint8_t(x >> 8), uint8_t(x)}; CE_NetWriteBytes(w, p, 2); }
void CE_NetWriteU32(CE_NetWriter *w, uint32_t x)
{ uint8_t p[] = {uint8_t(x >> 24), uint8_t(x >> 16), uint8_t(x >> 8), uint8_t(x)}; CE_NetWriteBytes(w, p, 4); }
void CE_NetWriteU64(CE_NetWriter *w, uint64_t x) { CE_NetWriteU32(w, uint32_t(x >> 32)); CE_NetWriteU32(w, uint32_t(x)); }
void CE_NetWriteF32(CE_NetWriter *w, float x)
{
    if (!std::isfinite(x)) { w->error = 1; return; }
    uint32_t bits;
    std::memcpy(&bits, &x, 4);
    CE_NetWriteU32(w, bits);
}
void CE_NetReaderInit(CE_NetReader *r, const uint8_t *p, uint32_t n) { *r = {p, n, 0, !p && n != 0}; }
void CE_NetReadBytes(CE_NetReader *r, uint8_t *p, uint32_t n)
{
    if (r->error || r->position > r->size || n > r->size - r->position || (!p && n)) { r->error = 1; return; }
    if (n) std::memcpy(p, r->data + r->position, n);
    r->position += n;
}
uint8_t CE_NetReadU8(CE_NetReader *r) { uint8_t p = 0; CE_NetReadBytes(r, &p, 1); return p; }
uint16_t CE_NetReadU16(CE_NetReader *r) { uint16_t x = CE_NetReadU8(r); return uint16_t((x << 8) | CE_NetReadU8(r)); }
uint32_t CE_NetReadU32(CE_NetReader *r)
{ uint32_t x = CE_NetReadU16(r); return (x << 16) | CE_NetReadU16(r); }
uint64_t CE_NetReadU64(CE_NetReader *r)
{ uint64_t x = CE_NetReadU32(r); return (x << 32) | CE_NetReadU32(r); }
float CE_NetReadF32(CE_NetReader *r)
{
    uint32_t bits = CE_NetReadU32(r);
    float x = 0;
    std::memcpy(&x, &bits, 4);
    if (!std::isfinite(x)) { r->error = 1; return 0; }
    return x;
}
int CE_NetReadDone(const CE_NetReader *r) { return !r->error && r->position == r->size; }
