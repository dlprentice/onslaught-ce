// SPDX-License-Identifier: GPL-3.0-only
#include "media_profile.h"

#include <cstdio>
#include <cstring>

namespace {
unsigned checks = 0;
unsigned failures = 0;

void Check(bool condition, const char *label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label);
    }
}

bool All(const uint8_t *bytes, unsigned count, uint8_t value) {
    for (unsigned i = 0; i < count; ++i)
        if (bytes[i] != value) return false;
    return true;
}

int Hex(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

bool Oracle(const char *text, uint8_t out[CE_MEDIA_IDENTITY_BYTES]) {
    if (std::strlen(text) != CE_MEDIA_IDENTITY_BYTES * 2) return false;
    for (unsigned i = 0; i < CE_MEDIA_IDENTITY_BYTES; ++i) {
        const int high = Hex(text[i * 2]);
        const int low = Hex(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = uint8_t(high * 16 + low);
    }
    return true;
}

void SetField(CE_MediaProfile &profile, unsigned field, uint8_t value) {
    switch (field) {
    case 0: profile.profile_version = value; break;
    case 1: profile.text_schema = value; break;
    case 2: profile.voice_schema = value; break;
    case 3: profile.reserved = value; break;
    }
}

void Legacy(const uint8_t base[CE_MEDIA_IDENTITY_BYTES]) {
    const CE_MediaProfile disabled = {0, 0, 0, 0};
    uint8_t guarded[CE_MEDIA_IDENTITY_BYTES + 2];
    std::memset(guarded, 97, sizeof guarded);
    Check(CE_MediaProfileValidate(&disabled) == CE_MEDIA_PROFILE_DISABLED,
          "disabled profile accepted explicitly");
    Check(CE_MediaProfileChannelCount(&disabled) == 4,
          "disabled proposal retains four channels");
    Check(CE_MediaProfileDeriveIdentity(guarded + 1, base, &disabled) == CE_MEDIA_PROFILE_DISABLED,
          "disabled derivation reports disabled");
    Check(std::memcmp(guarded + 1, base, CE_MEDIA_IDENTITY_BYTES) == 0,
          "disabled identity is exact base copy");
    Check(guarded[0] == 97 && guarded[CE_MEDIA_IDENTITY_BYTES + 1] == 97,
          "disabled derivation respects output bounds");
    Check(CE_MediaProfileDeriveIdentity(guarded + 1, guarded + 1, &disabled) == CE_MEDIA_PROFILE_DISABLED,
          "disabled in-place copy accepted");
    Check(std::memcmp(guarded + 1, base, CE_MEDIA_IDENTITY_BYTES) == 0,
          "disabled in-place copy retains identity");
}

void Text(const uint8_t base[CE_MEDIA_IDENTITY_BYTES],
          const uint8_t expected[CE_MEDIA_IDENTITY_BYTES]) {
    const CE_MediaProfile text = {1, 1, 0, 0};
    uint8_t guarded[CE_MEDIA_IDENTITY_BYTES + 2];
    std::memset(guarded, 113, sizeof guarded);
    Check(CE_MediaProfileValidate(&text) == CE_MEDIA_PROFILE_TEXT,
          "exact text profile accepted");
    Check(CE_MediaProfileChannelCount(&text) == 5,
          "text proposal adds only its fifth channel");
    Check(CE_MediaProfileDeriveIdentity(guarded + 1, base, &text) == CE_MEDIA_PROFILE_TEXT,
          "text derivation reports text");
    Check(std::memcmp(guarded + 1, expected, CE_MEDIA_IDENTITY_BYTES) == 0,
          "concrete enabled identity matches independent BLAKE2b oracle");
    Check(std::memcmp(guarded + 1, base, CE_MEDIA_IDENTITY_BYTES) != 0,
          "text identity differs from disabled identity");
    Check(guarded[0] == 113 && guarded[CE_MEDIA_IDENTITY_BYTES + 1] == 113,
          "text derivation respects output bounds");
    uint8_t in_place[CE_MEDIA_IDENTITY_BYTES];
    std::memcpy(in_place, base, CE_MEDIA_IDENTITY_BYTES);
    Check(CE_MediaProfileDeriveIdentity(in_place, in_place, &text) == CE_MEDIA_PROFILE_TEXT,
          "text in-place derivation accepted");
    Check(std::memcmp(in_place, expected, CE_MEDIA_IDENTITY_BYTES) == 0,
          "text in-place derivation binds original base");
    uint8_t changed[CE_MEDIA_IDENTITY_BYTES];
    std::memcpy(changed, base, CE_MEDIA_IDENTITY_BYTES);
    changed[CE_MEDIA_IDENTITY_BYTES - 1] ^= 1;
    Check(CE_MediaProfileDeriveIdentity(changed, changed, &text) == CE_MEDIA_PROFILE_TEXT,
          "changed base remains a supported text request");
    Check(std::memcmp(changed, expected, CE_MEDIA_IDENTITY_BYTES) != 0,
          "enabled identity binds the base identity");
}

void Refused(const uint8_t base[CE_MEDIA_IDENTITY_BYTES]) {
    const CE_MediaProfile accepted[] = {{0, 0, 0, 0}, {1, 1, 0, 0}};
    const uint8_t originals[][4] = {{0, 0, 0, 0}, {1, 1, 0, 0}};
    for (unsigned initial = 0; initial < 2; ++initial) {
        for (unsigned field = 0; field < 4; ++field) {
            for (unsigned value = 0; value < 256; ++value) {
                if (value == originals[initial][field]) continue;
                CE_MediaProfile profile = accepted[initial];
                SetField(profile, field, uint8_t(value));
                uint8_t guarded[CE_MEDIA_IDENTITY_BYTES + 2];
                std::memset(guarded, 127, sizeof guarded);
                Check(CE_MediaProfileValidate(&profile) == CE_MEDIA_PROFILE_UNSUPPORTED,
                      "every single-field departure from an accepted profile is refused");
                Check(CE_MediaProfileChannelCount(&profile) == 0,
                      "unsupported profile has no channel proposal");
                Check(CE_MediaProfileDeriveIdentity(guarded + 1, base, &profile) == CE_MEDIA_PROFILE_UNSUPPORTED,
                      "unsupported profile cannot derive an admitted identity");
                Check(All(guarded + 1, CE_MEDIA_IDENTITY_BYTES, 0),
                      "unsupported profile clears stale output");
                Check(guarded[0] == 127 && guarded[CE_MEDIA_IDENTITY_BYTES + 1] == 127,
                      "refusal respects output bounds");
            }
        }
    }
    const CE_MediaProfile voice[] = {{1, 0, 1, 0}, {1, 1, 1, 0}};
    for (const CE_MediaProfile &profile : voice) {
        Check(CE_MediaProfileValidate(&profile) == CE_MEDIA_PROFILE_UNSUPPORTED,
              "voice-only and text-plus-voice remain unavailable");
        Check(CE_MediaProfileChannelCount(&profile) == 0,
              "voice reservation never enables channels");
    }
}

void InvalidArguments(const uint8_t base[CE_MEDIA_IDENTITY_BYTES]) {
    const CE_MediaProfile text = {1, 1, 0, 0};
    uint8_t out[CE_MEDIA_IDENTITY_BYTES];
    std::memset(out, 157, sizeof out);
    Check(CE_MediaProfileValidate(nullptr) == CE_MEDIA_PROFILE_UNSUPPORTED,
          "null descriptor is not a disabled request");
    Check(CE_MediaProfileChannelCount(nullptr) == 0,
          "null descriptor cannot propose legacy fallback");
    Check(CE_MediaProfileDeriveIdentity(out, base, nullptr) == CE_MEDIA_PROFILE_UNSUPPORTED,
          "null descriptor derivation refused");
    Check(All(out, CE_MEDIA_IDENTITY_BYTES, 0), "null descriptor clears output");
    std::memset(out, 157, sizeof out);
    Check(CE_MediaProfileDeriveIdentity(out, nullptr, &text) == CE_MEDIA_PROFILE_UNSUPPORTED,
          "null base refused");
    Check(All(out, CE_MEDIA_IDENTITY_BYTES, 0), "null base clears output");
    Check(CE_MediaProfileDeriveIdentity(nullptr, base, &text) == CE_MEDIA_PROFILE_UNSUPPORTED,
          "null output refused");
}
}

int main(int argc, char **argv) {
    uint8_t expected[CE_MEDIA_IDENTITY_BYTES];
    if (argc != 2 || !Oracle(argv[1], expected)) {
        std::fprintf(stderr, "Supply the standalone runner's independent synthetic oracle.\n");
        return 2;
    }
    uint8_t base[CE_MEDIA_IDENTITY_BYTES];
    for (unsigned i = 0; i < CE_MEDIA_IDENTITY_BYTES; ++i) base[i] = uint8_t(i);
    Legacy(base);
    Text(base, expected);
    Refused(base);
    InvalidArguments(base);
    Check(CE_MEDIA_TEXT_KIND == 23 && CE_MEDIA_TEXT_CHANNEL == 4,
          "text allocation remains separate from legacy channels");
    Check(CE_MEDIA_VOICE_RESERVED_KIND == 24 && CE_MEDIA_VOICE_RESERVED_CHANNEL == 5 &&
          CE_MEDIA_VOICE_RESERVED_SCHEMA == 1 && CE_MEDIA_VOICE_RESERVED_CHANNELS == 6,
          "future voice reservations remain explicit");
    std::printf("media profile: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
