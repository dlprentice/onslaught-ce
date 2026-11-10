// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_STATE_DELTA_H
#define CE_STATE_DELTA_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CE_STATE_DELTA_MAX_IMAGE 65536u
#define CE_STATE_DELTA_HEADER 124u
#define CE_STATE_DELTA_DIGEST 32u

typedef struct CE_StateIdentity {
    uint8_t context[CE_STATE_DELTA_DIGEST]; // caller's source/build/schema/mission identity
    uint32_t epoch, sequence;             // nonzero; sequences do not wrap within an epoch
} CE_StateIdentity;
typedef struct CE_StateImage {
    CE_StateIdentity identity;
    const uint8_t *data;
    uint32_t size;
} CE_StateImage;
typedef struct CE_StateDeltaScratch { uint8_t data[CE_STATE_DELTA_MAX_IMAGE]; } CE_StateDeltaScratch;
enum CE_StateDeltaResult {
    CE_STATE_DELTA_OK = 0,
    CE_STATE_DELTA_INVALID,  // invalid argument, noncanonical wire or target digest mismatch
    CE_STATE_DELTA_BASIS,    // context/epoch/base sequence/size/digest does not identify the supplied basis
    CE_STATE_DELTA_STALE,    // valid target is not newer than last_applied_sequence
    CE_STATE_DELTA_CAPACITY, // caller's output buffer is too small
    CE_STATE_DELTA_NOT_FIT   // valid canonical encoding exceeds the existing 64KiB message bound
};

// Wire v1 (all integers except varints are big-endian):
// 0: "CED1", 4: version=1, 5: algorithm=1, 6: reserved u16=0, 8: context[32],
// 40: epoch, 44: base sequence, 48: target sequence, 52: base size, 56: target size,
// 60: base digest[32], 92: target digest[32]. At 124: zero or more (skip, count, literals).
// Skip/count are minimal unsigned LEB128, <=65536; count>0. Runs contain maximal contiguous changed bytes
// against the same offset in the base (zero beyond its size); later skips>0. Unchanged target tail is implicit.
// No alternative encodings, trailing zero runs or unchanged literals are accepted.

// These functions allocate nothing and never interpret float/clock/state fields. Zero-length images are valid.
// Image digests are BLAKE2b-256("CE-state-image-v1\0" || context || epochBE || sequenceBE || sizeBE || bytes).
int CE_StateImageDigest(const CE_StateImage *, uint8_t out[CE_STATE_DELTA_DIGEST]);
// Returns the canonical encoded size, including when that size exceeds the message bound.
int CE_StateDeltaMeasure(const CE_StateImage *basis, const CE_StateImage *target, uint32_t *required);

// The caller must pin an immutable, genuinely APPLICATION-ACKED basis. Transport enqueue/receipt is not that ACK.
// This stateless codec does not promote bases, apply game state, generate ACKs or choose recovery/keyframe policy.
int CE_StateDeltaEncode(const CE_StateImage *application_acked_basis, const CE_StateImage *target,
                        uint8_t *out, uint32_t capacity, uint32_t *written);
// last_applied_sequence must be >= basis.sequence. OK reconstructs bytes, not successful game application.
// The caller still must validate/apply those bytes before advancing its applied sequence or acknowledging them.
int CE_StateDeltaDecode(const CE_StateImage *pinned_basis, uint32_t last_applied_sequence,
                        const uint8_t *wire, uint32_t wire_size, CE_StateDeltaScratch *scratch,
                        uint8_t *out, uint32_t capacity, CE_StateIdentity *identity, uint32_t *size);

// All failure results preserve output bytes, sizes and identities. Decode scratch may change on refusal.
// Every mutable output range (digest/required/written/identity/size/output buffer) must be disjoint from all
// other outputs, scratch, immutable image descriptors, image data and wire. Scratch has the same restrictions.
// Output buffer range means its entire declared capacity; scratch means its entire fixed object. Numeric range
// overflow and any such overlap are refused before writes, including scratch writes. Immutable inputs may overlap
// each other if individually valid. Callers still must provide accessible objects for all declared ranges.
#ifdef __cplusplus
}
#endif
#endif
