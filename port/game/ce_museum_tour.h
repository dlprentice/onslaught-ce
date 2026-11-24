// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_tour.h: Guided tour sequence, wing metadata, and Atrium Directory
// Lectern quick-jump destinations for the Grand Museum complex. Plain arithmetic with no
// engine dependency so host tests verify every jump target against the complex walkability.
#ifndef CE_MUSEUM_TOUR_H
#define CE_MUSEUM_TOUR_H

#include <cmath>

namespace CeMuseumTour {

enum Wing {
    kWingCourtyard = 0,
    kWingAtrium,
    kWingForseti,
    kWingMuspell,
    kWingHangar,
    kWingConceptArt,
    kWingEarlyDesigns,
    kWingScreeningRoom,
    kWingCount
};

struct WingMeta {
    Wing wing;
    const char *title;
    const char *subtitle;
    // Visitor local arrival point and facing yaw (radians, 0 = +Y North, +PI/2 = -X West, -PI/2 = +X East)
    float jumpX;
    float jumpY;
    float jumpYaw;
};

// Strictly factual player-facing titles and cues (no build jargon, no internal filenames).
static const WingMeta kWings[kWingCount] = {
    {kWingCourtyard,     "Coastal Courtyard",
     "Walk past the Battle Engine to enter the Grand Atrium - hold Shift to sprint.",
     0.0f, 0.0f, 0.0f},
    {kWingAtrium,        "Grand Atrium",
     "Five galleries branch from the compass floor. Use the directory lectern to jump directly.",
     0.0f, 25.0f, 0.0f},
    {kWingForseti,       "Forseti Forces Wing",
     "Ground armour, walkers, and field defences of the Forseti Commonwealth.",
     -20.0f, 41.0f, 1.5707963f},
    {kWingMuspell,       "Muspell Dominion Wing",
     "Heavy mechs, siege armour, and frontline machines of the Muspell Dominion.",
     20.0f, 41.0f, -1.5707963f},
    {kWingHangar,        "Aviation and Naval Hangar",
     "Banked aircraft on flight pylons, naval vessels, and the relative scale wall.",
     0.0f, 56.5f, 0.0f},
    {kWingConceptArt,    "Concept Art Gallery",
     "Production paintings and unit studies leading to the Screening Room.",
     20.0f, 22.0f, -1.5707963f},
    {kWingEarlyDesigns,  "Early Designs Vault",
     "Archival design studies and early Battle Engine blueprints.",
     -20.0f, 22.0f, 1.5707963f},
    {kWingScreeningRoom, "Screening Room",
     "Use the projector console to watch game films.",
     47.5f, 14.5f, 0.0f},
};

inline const WingMeta &Meta(Wing w) {
    const unsigned idx = static_cast<unsigned>(w);
    return kWings[idx < static_cast<unsigned>(kWingCount) ? idx : 0u];
}

// Directory Console on the west entrance flank of the Grand Atrium, preserving the unobstructed central rotunda sightline.
constexpr float kDirectoryX = -5.4f;
constexpr float kDirectoryY = 26.8f;
constexpr float kDirectoryZ = 1.25f;
constexpr float kDirectoryMaxRange = 4.5f;

enum { kDirectoryStops = 6 };

struct DirectoryStop {
    Wing target;
    const char *label;
    float localOffsetX; // offset along the lectern rail
};

static const DirectoryStop kDirectory[kDirectoryStops] = {
    {kWingForseti,       "Forseti Wing",      -1.25f},
    {kWingEarlyDesigns,  "Design Vault",      -0.75f},
    {kWingHangar,        "Aviation Hangar",   -0.25f},
    {kWingMuspell,       "Muspell Wing",       0.25f},
    {kWingConceptArt,    "Concept Art",        0.75f},
    {kWingScreeningRoom, "Screening Room",     1.25f},
};

struct DirectoryHit {
    bool valid;
    int index; // 0..kDirectoryStops-1, or -1
};

// Ray-pick the brass selector plates mounted along the Atrium Directory Lectern.
// eye and aim are in visitor-local coordinates with Z positive upward.
inline DirectoryHit PickDirectory(float eyeX, float eyeY, float eyeZ,
                                  float aimX, float aimY, float aimZ) {
    DirectoryHit hit = {false, -1};
    const float dx0 = kDirectoryX - eyeX, dy0 = kDirectoryY - eyeY;
    if (dx0 * dx0 + dy0 * dy0 > kDirectoryMaxRange * kDirectoryMaxRange) return hit;
    const float alen = std::sqrt(aimX * aimX + aimY * aimY + aimZ * aimZ);
    if (!(alen > 1e-5f)) return hit;
    const float ux = aimX / alen, uy = aimY / alen, uz = aimZ / alen;
    float bestDist = 1e9f;
    for (int i = 0; i < kDirectoryStops; ++i) {
        const float px = kDirectoryX + kDirectory[i].localOffsetX;
        const float py = kDirectoryY - 0.18f;
        const float pz = kDirectoryZ;
        const float vx = px - eyeX, vy = py - eyeY, vz = pz - eyeZ;
        const float t = vx * ux + vy * uy + vz * uz;
        if (t <= 0.15f || t > kDirectoryMaxRange) continue;
        const float cx = eyeX + t * ux - px;
        const float cy = eyeY + t * uy - py;
        const float cz = eyeZ + t * uz - pz;
        const float d2 = cx * cx + cy * cy + cz * cz;
        if (d2 <= 0.24f * 0.24f && t < bestDist) {
            bestDist = t;
            hit.valid = true;
            hit.index = i;
        }
    }
    return hit;
}

// Guided tour sequence from Courtyard through every gallery and back.
inline Wing NextTourWing(Wing current) {
    const unsigned idx = static_cast<unsigned>(current);
    if (idx + 1u < static_cast<unsigned>(kWingCount)) return static_cast<Wing>(idx + 1u);
    return kWingCourtyard;
}

} // namespace CeMuseumTour

#endif
