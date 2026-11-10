// SPDX-License-Identifier: GPL-3.0-only
// Canonical script-visible pre-run flags: one byte per slot, in slot order, each exactly zero or one.
#include <cstdint>
#include <monocypher.h>

extern "C" int CE_NetPreRunFlagsDigest(const uint8_t *flags, uint32_t count, uint8_t *out) {
    if (!flags || !out || count != 256) return 0;
    for (uint32_t i=0;i<count;++i) if (flags[i]>1) return 0;
    crypto_blake2b(out, 32, flags, count);
    return 1;
}
