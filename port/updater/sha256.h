/* SPDX-License-Identifier: GPL-3.0-only */
/* port/updater/sha256.h: SHA-256 (FIPS 180-4) for the launcher's archive and game-data checks. */
#ifndef CE_SHA256_H
#define CE_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; size_t n; } SHA256_CTX;
void sha256_init(SHA256_CTX *c);
void sha256_update(SHA256_CTX *c, const void *data, size_t len);
void sha256_final(SHA256_CTX *c, uint8_t out[32]);
#endif
