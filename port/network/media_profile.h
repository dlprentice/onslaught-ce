// SPDX-License-Identifier: GPL-3.0-only
#ifndef CE_MEDIA_PROFILE_H
#define CE_MEDIA_PROFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CE_MEDIA_IDENTITY_BYTES = 32,
    CE_MEDIA_PROFILE_DESCRIPTOR_BYTES = 4,
    CE_MEDIA_LEGACY_CHANNELS = 4,
    CE_MEDIA_TEXT_CHANNELS = 5,
    CE_MEDIA_TEXT_KIND = 23,
    CE_MEDIA_TEXT_CHANNEL = 4,
    // Reservations only: no accepted profile enables voice.
    CE_MEDIA_VOICE_RESERVED_SCHEMA = 1,
    CE_MEDIA_VOICE_RESERVED_KIND = 24,
    CE_MEDIA_VOICE_RESERVED_CHANNEL = 5,
    CE_MEDIA_VOICE_RESERVED_CHANNELS = 6
};

typedef struct CE_MediaProfile {
    uint8_t profile_version;
    uint8_t text_schema;
    uint8_t voice_schema;
    uint8_t reserved;
} CE_MediaProfile;

typedef enum CE_MediaProfileStatus {
    CE_MEDIA_PROFILE_DISABLED = 0,
    CE_MEDIA_PROFILE_TEXT = 1,
    CE_MEDIA_PROFILE_UNSUPPORTED = 2
} CE_MediaProfileStatus;

// Only {0,0,0,0} and {1,1,0,0} are accepted. This is a local contract,
// not peer negotiation, authentication, or permission to send a packet.
CE_MediaProfileStatus CE_MediaProfileValidate(const CE_MediaProfile *profile);

// A disabled profile copies the base identity exactly. An enabled text profile
// hashes a domain label, the base identity and four explicitly serialized bytes.
// Returns UNSUPPORTED for an invalid argument/profile and clears a nonnull out.
// The output may be the same buffer as base. No other overlap is supported.
CE_MediaProfileStatus CE_MediaProfileDeriveIdentity(
    uint8_t out[CE_MEDIA_IDENTITY_BYTES],
    const uint8_t base[CE_MEDIA_IDENTITY_BYTES],
    const CE_MediaProfile *profile);

// Returns zero for an unsupported profile. These proposed channel counts do
// not create channels or change any existing transport configuration.
unsigned CE_MediaProfileChannelCount(const CE_MediaProfile *profile);

#ifdef __cplusplus
}
#endif
#endif
