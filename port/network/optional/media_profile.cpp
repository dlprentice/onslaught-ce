// SPDX-License-Identifier: GPL-3.0-only
#include "../media_profile.h"

#include <monocypher.h>
#include <cstring>

namespace {
// No terminating zero belongs to this domain label.
const uint8_t kDomain[19] = {
    'C', 'E', '-', 'M', 'E', 'D', 'I', 'A', '-',
    'P', 'R', 'O', 'F', 'I', 'L', 'E', '-', 'V', '1'
};
const unsigned kDomainBytes = 19;
}

extern "C" CE_MediaProfileStatus CE_MediaProfileValidate(const CE_MediaProfile *profile) {
    if (!profile) return CE_MEDIA_PROFILE_UNSUPPORTED;
    if (profile->profile_version == 0 && profile->text_schema == 0 &&
        profile->voice_schema == 0 && profile->reserved == 0)
        return CE_MEDIA_PROFILE_DISABLED;
    if (profile->profile_version == 1 && profile->text_schema == 1 &&
        profile->voice_schema == 0 && profile->reserved == 0)
        return CE_MEDIA_PROFILE_TEXT;
    return CE_MEDIA_PROFILE_UNSUPPORTED;
}

extern "C" CE_MediaProfileStatus CE_MediaProfileDeriveIdentity(
    uint8_t out[CE_MEDIA_IDENTITY_BYTES],
    const uint8_t base[CE_MEDIA_IDENTITY_BYTES],
    const CE_MediaProfile *profile) {
    const CE_MediaProfileStatus status = CE_MediaProfileValidate(profile);
    if (!out) return CE_MEDIA_PROFILE_UNSUPPORTED;
    if (!base || status == CE_MEDIA_PROFILE_UNSUPPORTED) {
        std::memset(out, 0, CE_MEDIA_IDENTITY_BYTES);
        return CE_MEDIA_PROFILE_UNSUPPORTED;
    }
    if (status == CE_MEDIA_PROFILE_DISABLED) {
        std::memmove(out, base, CE_MEDIA_IDENTITY_BYTES);
        return status;
    }
    const uint8_t descriptor[CE_MEDIA_PROFILE_DESCRIPTOR_BYTES] = {
        profile->profile_version, profile->text_schema,
        profile->voice_schema, profile->reserved
    };
    uint8_t bound[kDomainBytes + CE_MEDIA_IDENTITY_BYTES + CE_MEDIA_PROFILE_DESCRIPTOR_BYTES];
    std::memcpy(bound, kDomain, kDomainBytes);
    std::memcpy(bound + kDomainBytes, base, CE_MEDIA_IDENTITY_BYTES);
    std::memcpy(bound + kDomainBytes + CE_MEDIA_IDENTITY_BYTES,
                descriptor, CE_MEDIA_PROFILE_DESCRIPTOR_BYTES);
    crypto_blake2b(out, CE_MEDIA_IDENTITY_BYTES, bound,
                  kDomainBytes + CE_MEDIA_IDENTITY_BYTES + CE_MEDIA_PROFILE_DESCRIPTOR_BYTES);
    return status;
}

extern "C" unsigned CE_MediaProfileChannelCount(const CE_MediaProfile *profile) {
    switch (CE_MediaProfileValidate(profile)) {
    case CE_MEDIA_PROFILE_DISABLED: return CE_MEDIA_LEGACY_CHANNELS;
    case CE_MEDIA_PROFILE_TEXT: return CE_MEDIA_TEXT_CHANNELS;
    default: return 0;
    }
}
