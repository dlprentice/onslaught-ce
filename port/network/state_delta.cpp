// SPDX-License-Identifier: GPL-3.0-only
#include "state_delta.h"
#include "network.h"
#include <monocypher.h>
#include <cstddef>
#include <cstring>
#include <stdint.h>

static_assert(CE_STATE_DELTA_MAX_IMAGE == CE_NET_MAX_MESSAGE, "delta must retain the transport message bound");
namespace {
const uint8_t kMagic[4] = {'C', 'E', 'D', '1'};
const uint8_t kDomain[] = "CE-state-image-v1"; // sizeof includes the domain's terminating zero

void Put32(uint8_t *p, uint32_t v)
{ p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v); }
uint32_t Get32(const uint8_t *p)
{ return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
bool Range(const void *p, size_t n)
{ return !n || (p && n <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(p)); }
bool Overlap(const void *a, size_t an, const void *b, size_t bn)
{
    if (!an || !bn) return false;
    const uintptr_t ap = reinterpret_cast<uintptr_t>(a), bp = reinterpret_cast<uintptr_t>(b);
    if (!Range(a, an) || !Range(b, bn)) return true;
    return ap < bp + bn && bp < ap + an;
}
bool Valid(const CE_StateImage *image)
{
    return Range(image, sizeof(*image)) && image->identity.epoch && image->identity.sequence &&
           image->size <= CE_STATE_DELTA_MAX_IMAGE && Range(image->data, image->size);
}
bool Pair(const CE_StateImage *basis, const CE_StateImage *target)
{
    return Valid(basis) && Valid(target) && basis->identity.epoch == target->identity.epoch &&
           basis->identity.sequence < target->identity.sequence &&
           crypto_verify32(basis->identity.context, target->identity.context) == 0;
}
bool OutsideImage(const void *p, size_t n, const CE_StateImage &image)
{
    return !Overlap(p, n, &image, sizeof(image)) && !Overlap(p, n, image.data, image.size);
}
void Digest(const CE_StateImage &image, uint8_t out[32])
{
    uint8_t numbers[12];
    Put32(numbers, image.identity.epoch); Put32(numbers + 4, image.identity.sequence); Put32(numbers + 8, image.size);
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, 32);
    crypto_blake2b_update(&ctx, kDomain, sizeof(kDomain));
    crypto_blake2b_update(&ctx, image.identity.context, 32);
    crypto_blake2b_update(&ctx, numbers, sizeof(numbers));
    if (image.size) crypto_blake2b_update(&ctx, image.data, image.size);
    crypto_blake2b_final(&ctx, out);
}
uint8_t Reference(const CE_StateImage &basis, uint32_t i) { return i < basis.size ? basis.data[i] : 0; }
uint32_t VarSize(uint32_t n) { return n < 128 ? 1 : n < 16384 ? 2 : 3; }
void PutVar(uint8_t *out, uint32_t &at, uint32_t n)
{
    do { uint8_t b = uint8_t(n & 127); n >>= 7; out[at++] = uint8_t(b | (n ? 128 : 0)); } while (n);
}
bool GetVar(const uint8_t *wire, uint32_t size, uint32_t &at, uint32_t &out)
{
    const uint32_t begin = at;
    uint32_t n = 0;
    for (uint32_t i = 0; i != 3; ++i) {
        if (at == size) return false;
        const uint8_t b = wire[at++];
        n |= uint32_t(b & 127) << (7 * i);
        if (!(b & 128)) {
            if (n > CE_STATE_DELTA_MAX_IMAGE || at - begin != VarSize(n)) return false;
            out = n; return true;
        }
    }
    return false;
}
uint32_t Measure(const CE_StateImage &basis, const CE_StateImage &target)
{
    uint32_t i = 0, previous_end = 0, bytes = CE_STATE_DELTA_HEADER;
    while (i < target.size) {
        if (target.data[i] == Reference(basis, i)) { ++i; continue; }
        const uint32_t begin = i;
        do { ++i; } while (i < target.size && target.data[i] != Reference(basis, i));
        bytes += VarSize(begin - previous_end) + VarSize(i - begin) + i - begin;
        previous_end = i;
    }
    return bytes; // <= header + 3*65536: bounded independently of caller capacity
}
} // namespace

int CE_StateImageDigest(const CE_StateImage *image, uint8_t out[32])
{
    if (!Valid(image) || !Range(out, 32) || !OutsideImage(out, 32, *image)) return CE_STATE_DELTA_INVALID;
    uint8_t digest[32]; Digest(*image, digest); std::memcpy(out, digest, 32);
    return CE_STATE_DELTA_OK;
}
int CE_StateDeltaMeasure(const CE_StateImage *basis, const CE_StateImage *target, uint32_t *required)
{
    if (!Pair(basis, target) || !Range(required, sizeof(*required)) ||
        !OutsideImage(required, sizeof(*required), *basis) || !OutsideImage(required, sizeof(*required), *target))
        return CE_STATE_DELTA_INVALID;
    *required = Measure(*basis, *target); return CE_STATE_DELTA_OK;
}
int CE_StateDeltaEncode(const CE_StateImage *basis, const CE_StateImage *target,
                        uint8_t *out, uint32_t capacity, uint32_t *written)
{
    if (!Pair(basis, target) || !Range(written, sizeof(*written)) || !Range(out, capacity) ||
        !OutsideImage(written, sizeof(*written), *basis) || !OutsideImage(written, sizeof(*written), *target) ||
        !OutsideImage(out, capacity, *basis) || !OutsideImage(out, capacity, *target) ||
        Overlap(written, sizeof(*written), out, capacity)) return CE_STATE_DELTA_INVALID;
    const uint32_t required = Measure(*basis, *target);
    if (required > CE_NET_MAX_MESSAGE) return CE_STATE_DELTA_NOT_FIT;
    if (capacity < required) return CE_STATE_DELTA_CAPACITY;
    uint8_t base_digest[32], target_digest[32]; Digest(*basis, base_digest); Digest(*target, target_digest);
    std::memcpy(out, kMagic, 4); out[4] = 1; out[5] = 1; out[6] = out[7] = 0;
    std::memcpy(out + 8, basis->identity.context, 32);
    Put32(out + 40, basis->identity.epoch); Put32(out + 44, basis->identity.sequence);
    Put32(out + 48, target->identity.sequence); Put32(out + 52, basis->size); Put32(out + 56, target->size);
    std::memcpy(out + 60, base_digest, 32); std::memcpy(out + 92, target_digest, 32);
    uint32_t i = 0, previous_end = 0, at = CE_STATE_DELTA_HEADER;
    while (i < target->size) {
        if (target->data[i] == Reference(*basis, i)) { ++i; continue; }
        const uint32_t begin = i;
        do { ++i; } while (i < target->size && target->data[i] != Reference(*basis, i));
        PutVar(out, at, begin - previous_end); PutVar(out, at, i - begin);
        std::memcpy(out + at, target->data + begin, i - begin); at += i - begin;
        previous_end = i;
    }
    *written = at; return CE_STATE_DELTA_OK;
}
int CE_StateDeltaDecode(const CE_StateImage *basis, uint32_t last_applied_sequence,
                        const uint8_t *wire, uint32_t wire_size, CE_StateDeltaScratch *scratch,
                        uint8_t *out, uint32_t capacity, CE_StateIdentity *identity, uint32_t *size)
{
    if (!Valid(basis) || last_applied_sequence < basis->identity.sequence ||
        wire_size < CE_STATE_DELTA_HEADER || wire_size > CE_NET_MAX_MESSAGE || !Range(wire, wire_size) ||
        !Range(scratch, sizeof(*scratch)) || !Range(identity, sizeof(*identity)) || !Range(size, sizeof(*size)) ||
        !Range(out, capacity)) return CE_STATE_DELTA_INVALID;
    // Check every mutable range before reading the wire or using scratch: a descriptor can live in aliased storage.
    if (!OutsideImage(out, capacity, *basis) || !OutsideImage(scratch, sizeof(*scratch), *basis) ||
        !OutsideImage(identity, sizeof(*identity), *basis) || !OutsideImage(size, sizeof(*size), *basis) ||
        Overlap(out, capacity, wire, wire_size) || Overlap(scratch, sizeof(*scratch), wire, wire_size) ||
        Overlap(identity, sizeof(*identity), wire, wire_size) || Overlap(size, sizeof(*size), wire, wire_size) ||
        Overlap(out, capacity, scratch, sizeof(*scratch)) || Overlap(out, capacity, identity, sizeof(*identity)) ||
        Overlap(out, capacity, size, sizeof(*size)) || Overlap(scratch, sizeof(*scratch), identity, sizeof(*identity)) ||
        Overlap(scratch, sizeof(*scratch), size, sizeof(*size)) || Overlap(identity, sizeof(*identity), size, sizeof(*size)))
        return CE_STATE_DELTA_INVALID;
    if (std::memcmp(wire, kMagic, 4) || wire[4] != 1 || wire[5] != 1 || wire[6] || wire[7])
        return CE_STATE_DELTA_INVALID;
    if (crypto_verify32(wire + 8, basis->identity.context) || Get32(wire + 40) != basis->identity.epoch ||
        Get32(wire + 44) != basis->identity.sequence || Get32(wire + 52) != basis->size)
        return CE_STATE_DELTA_BASIS;
    CE_StateIdentity target_identity = basis->identity;
    target_identity.sequence = Get32(wire + 48);
    const uint32_t target_size = Get32(wire + 56);
    if (target_identity.sequence <= basis->identity.sequence || target_size > CE_STATE_DELTA_MAX_IMAGE)
        return CE_STATE_DELTA_INVALID;
    if (capacity < target_size) return CE_STATE_DELTA_CAPACITY;
    uint8_t digest[32]; Digest(*basis, digest);
    if (crypto_verify32(wire + 60, digest)) return CE_STATE_DELTA_BASIS;
    const uint32_t common = target_size < basis->size ? target_size : basis->size;
    if (common) std::memcpy(scratch->data, basis->data, common);
    if (target_size > common) std::memset(scratch->data + common, 0, target_size - common);
    uint32_t at = CE_STATE_DELTA_HEADER, end = 0;
    bool first = true;
    while (at < wire_size) {
        uint32_t skip = 0, count = 0;
        if (!GetVar(wire, wire_size, at, skip) || !GetVar(wire, wire_size, at, count) || !count ||
            (!first && !skip) || skip > target_size - end) return CE_STATE_DELTA_INVALID;
        const uint32_t begin = end + skip;
        if (count > target_size - begin || count > wire_size - at) return CE_STATE_DELTA_INVALID;
        for (uint32_t i = 0; i != count; ++i)
            if (wire[at + i] == Reference(*basis, begin + i)) return CE_STATE_DELTA_INVALID;
        // A run must include every adjacent changed byte. Positive skips and changed-only literals make it maximal.
        std::memcpy(scratch->data + begin, wire + at, count); at += count;
        end = begin + count; first = false;
    }
    const CE_StateImage target = {target_identity, scratch->data, target_size}; Digest(target, digest);
    if (crypto_verify32(wire + 92, digest)) return CE_STATE_DELTA_INVALID;
    if (target_identity.sequence <= last_applied_sequence) return CE_STATE_DELTA_STALE;
    if (target_size) std::memcpy(out, scratch->data, target_size);
    *identity = target_identity; *size = target_size;
    return CE_STATE_DELTA_OK;
}
