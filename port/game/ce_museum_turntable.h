// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_turntable.h: Rotating 3D turntable pedestals for the Grand Museum.
// Pure C++ state machine, curated Goodie manifest, wing-scoped residency budget, and
// ray-pick Pause/Resume switch logic — compiled both in-game (ce_museum_turntable.cpp)
// and in standalone host tests (port/tools/ce_museum_turntable_tests.py).
#ifndef CE_MUSEUM_TURNTABLE_H
#define CE_MUSEUM_TURNTABLE_H

#include "ce_museum_tour.h"
#include <cmath>

namespace CeMuseumTurntable {

constexpr float kDefaultRateRadPerSec = 12.0f * 3.14159265f / 180.0f; // ~12 deg/s (~30 s / rev)
constexpr float kStudRadius = 0.28f;
constexpr float kStudMaxRange = 5.5f;
// Cap simultaneously resident Goodie 3D meshes well inside 32-bit RAM budget.
enum { kMaxResidentGoodies = 10, kMaxTurntables = 24 };

enum StageStyle {
    kStageGround = 0,  // upright on plinth top
    kStageFlight,      // elevated & banked on a flight pylon
    kStageNaval,       // scaled vessel staged over a recessed water basin
    kStageHero,        // monumental courtyard centerpiece
    kStageScale        // relative scale alcove (stationary comparison, toggleable)
};

struct Spec {
    int goodieId;             // retail goodie_NN_res_PC.aya index
    CeMuseumTour::Wing wing;
    StageStyle style;
    const char *title;
    const char *category;
    const char *caption;
    float localX, localY;     // plinth centre in visitor-local metres
    float plinthTopZ;         // positive upward height of plinth top above floor
    float plinthHalf;         // plinth footprint half-side (for collision & stud placement)
    float targetSpan;         // desired visual bounding diameter (0 = natural 1:1 scale)
    float bankRad;            // flight bank roll (radians)
    float studDirX, studDirY; // unit direction toward aisle where brass control stud sits
    bool defaultPaused;
};

// Curated Grand Museum 3D exhibits loaded on demand from the player's installed retail Goodies.
enum { kManifestCount = 18 };
static const Spec kManifest[kManifestCount] = {
    // Courtyard Hero Monument (always resident while in Courtyard or Atrium sightline)
    {8,  CeMuseumTour::kWingCourtyard, kStageHero,
     "Battle Engine Aquila", "Forseti Dual-Mode Walker / Jet",
     "Full-scale dual-mode combat machine uniting armoured ground fire with aerial strike capability.",
     0.0f, 10.5f, 1.35f, 2.50f, 4.8f, 0.0f, 0.0f, -1.0f, false},

    // Forseti Forces Wing (West Apse featured rotating displays alongside the 6 native bay exhibits)
    {16, CeMuseumTour::kWingForseti, kStageGround,
     "Forseti Beam Tank", "Forseti Heavy Armour",
     "Tracked assault chassis fitted with a sustained high-energy beam emitter.",
     -40.8f, 37.8f, 0.90f, 1.45f, 2.8f, 0.0f, 1.0f, 0.0f, false},
    {17, CeMuseumTour::kWingForseti, kStageGround,
     "Forseti Plasma Tank", "Forseti Assault Armour",
     "Frontline armoured hull mounting a heavy plasma mortar for anti-fortification fire.",
     -40.8f, 44.2f, 0.90f, 1.45f, 2.8f, 0.0f, 1.0f, 0.0f, false},

    // Muspell Dominion Wing (6 symmetrical turntable pedestals)
    {47, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Gun Walker", "Muspell Light Mech",
     "Bipedal recon and skirmish walker fielded across Dominion beachheads.",
     22.0f, 47.8f, 0.90f, 1.85f, 3.0f, 0.0f, 0.0f, -1.0f, false},
    {48, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Heavy Gun Walker", "Muspell Assault Mech",
     "Up-armoured walker variant carrying dual heavy autocannons.",
     30.0f, 47.8f, 0.90f, 1.85f, 3.2f, 0.0f, 0.0f, -1.0f, false},
    {49, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Gun Crab", "Muspell Heavy Walker",
     "Quadrupedal siege platform built to anchor entrenched defensive lines.",
     38.0f, 47.8f, 0.90f, 1.85f, 3.3f, 0.0f, 0.0f, -1.0f, false},
    {51, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Arachnid", "Muspell Heavy Mech",
     "Multi-legged heavy assault walker designed for rough coastal terrain.",
     22.0f, 34.2f, 0.90f, 1.85f, 3.3f, 0.0f, 0.0f, 1.0f, false},
    {43, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Battle Tank", "Muspell Main Battle Tank",
     "Angular heavy tracked tank forming the backbone of Dominion armoured columns.",
     30.0f, 34.2f, 0.90f, 1.85f, 3.1f, 0.0f, 0.0f, 1.0f, false},
    {45, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Artillery", "Muspell Siege Vehicle",
     "Long-range mobile bombardment platform supporting amphibious assaults.",
     38.0f, 34.2f, 0.90f, 1.85f, 3.1f, 0.0f, 0.0f, 1.0f, false},

    // Aviation & Naval Hangar — West Aviation Pylons (banked in flight!)
    {19, CeMuseumTour::kWingHangar, kStageFlight,
     "F-24 Fighter", "Forseti Air Superiority",
     "Agile twin-engine interceptor assigned to fleet air defence.",
     -14.5f, 58.0f, 2.75f, 1.75f, 3.4f, 0.44f, 1.0f, 0.0f, false},
    {21, CeMuseumTour::kWingHangar, kStageFlight,
     "Forseti Heavy Bomber", "Forseti Strategic Aircraft",
     "Long-range strike airframe built for high-altitude anti-ship sorties.",
     -14.5f, 65.0f, 3.05f, 1.75f, 3.8f, 0.36f, 1.0f, 0.0f, false},
    {36, CeMuseumTour::kWingHangar, kStageFlight,
     "Muspell Fighter", "Muspell Interceptor",
     "Fast delta-wing Dominion fighter deployed from forward airbases.",
     -14.5f, 72.0f, 2.75f, 1.75f, 3.4f, 0.44f, 1.0f, 0.0f, false},

    // Aviation & Naval Hangar — East Naval Basin (staged over recessed dark water; brass studs sit on West basin rail!)
    {26, CeMuseumTour::kWingHangar, kStageNaval,
     "F.S. Marshall", "Forseti Naval Vessel",
     "Scale maritime display of a Forseti command and fleet support warship.",
     13.0f, 58.5f, 0.65f, 4.20f, 4.2f, 0.0f, -1.0f, 0.0f, false},
    {25, CeMuseumTour::kWingHangar, kStageNaval,
     "Forseti Amphibious Lander", "Forseti Transport Craft",
     "Armoured landing craft built to deliver ground forces onto contested beaches.",
     13.0f, 65.0f, 0.65f, 4.20f, 3.6f, 0.0f, -1.0f, 0.0f, false},
    {52, CeMuseumTour::kWingHangar, kStageNaval,
     "Muspell Battleship", "Muspell Capital Ship",
     "Scale maritime display of a heavy Dominion bombardment warship.",
     13.0f, 71.5f, 0.65f, 4.20f, 4.4f, 0.0f, -1.0f, 0.0f, false},

    // Aviation & Naval Hangar — North Relative Scale Alcove
    {50, CeMuseumTour::kWingHangar, kStageScale,
     "Muspell Gnat", "Relative Scale Alcove",
     "Compact scout unit staged beside heavier armour for direct size comparison.",
     -3.8f, 75.3f, 0.55f, 1.10f, 1.2f, 0.0f, 0.0f, -1.0f, true},
    {42, CeMuseumTour::kWingHangar, kStageScale,
     "Muspell Light Tank", "Relative Scale Alcove",
     "Medium armoured hull staged along the calibrated elevation wall.",
     0.0f, 75.3f, 0.55f, 1.25f, 2.4f, 0.0f, 0.0f, -1.0f, true},
    {15, CeMuseumTour::kWingHangar, kStageScale,
     "Forseti Battle Tank", "Relative Scale Alcove",
     "Main battle tank staged along the calibrated height bands for scale reference.",
     3.8f, 75.3f, 0.55f, 1.35f, 3.1f, 0.0f, 0.0f, -1.0f, true},
};

struct SlotState {
    float yawRad;
    bool paused;
    bool loaded;
    bool missing;
};

struct BankState {
    SlotState slots[kManifestCount];
    CeMuseumTour::Wing residentWing;
    bool initialized;
    bool fireHeld;
    bool fireCaptured;
    int hoveredIndex;
};

inline void Reset(BankState *s) {
    if (!s) return;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        s->slots[i].yawRad = 0.55f + 0.35f * float(i % 4u);
        s->slots[i].paused = kManifest[i].defaultPaused;
        s->slots[i].loaded = false;
        s->slots[i].missing = false;
    }
    s->residentWing = CeMuseumTour::kWingCourtyard;
    s->initialized = true;
    s->fireHeld = false;
    s->fireCaptured = false;
    s->hoveredIndex = -1;
}

// Which turntable indices should be resident for the visitor's current wing.
// Always keeps the Courtyard Hero Monument resident when outdoors or in the Atrium,
// plus only the active wing's exhibits — strictly <= kMaxResidentGoodies.
inline bool ShouldBeResident(unsigned index, CeMuseumTour::Wing currentWing) {
    if (index >= kManifestCount) return false;
    const CeMuseumTour::Wing w = kManifest[index].wing;
    if (w == currentWing) return true;
    if (w == CeMuseumTour::kWingCourtyard &&
        (currentWing == CeMuseumTour::kWingAtrium || currentWing == CeMuseumTour::kWingCourtyard))
        return true;
    return false;
}

inline unsigned CountWantedResident(CeMuseumTour::Wing currentWing) {
    unsigned n = 0;
    for (unsigned i = 0; i < kManifestCount; ++i)
        if (ShouldBeResident(i, currentWing)) ++n;
    return n;
}

inline void StepRotation(BankState *s, float dtSeconds, bool gamePaused) {
    if (!s || !s->initialized || gamePaused || !(dtSeconds > 0.0f)) return;
    const float clamped = dtSeconds > 0.25f ? 0.25f : dtSeconds;
    const float delta = kDefaultRateRadPerSec * clamped;
    const float twoPi = 6.283185307f;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (s->slots[i].paused) continue;
        float y = s->slots[i].yawRad + delta;
        if (y >= twoPi) y -= twoPi;
        s->slots[i].yawRad = y;
    }
}

inline void TogglePause(BankState *s, int index) {
    if (!s || index < 0 || static_cast<unsigned>(index) >= kManifestCount) return;
    s->slots[index].paused = !s->slots[index].paused;
}

// World-local coordinates of the pedestal's interactive brass control stud (Z positive upward).
inline void StudPosition(unsigned index, float *x, float *y, float *z) {
    const Spec &sp = kManifest[index];
    const float dist = sp.plinthHalf + 0.14f;
    if (x) *x = sp.localX + sp.studDirX * dist;
    if (y) *y = sp.localY + sp.studDirY * dist;
    if (z) *z = 1.05f;
}

// Ray-pick the closest turntable brass control stud (eye/aim in local coords, Z positive upward).
inline int PickStud(float eyeX, float eyeY, float eyeZ,
                    float aimX, float aimY, float aimZ,
                    CeMuseumTour::Wing currentWing) {
    const float alen = std::sqrt(aimX * aimX + aimY * aimY + aimZ * aimZ);
    if (!(alen > 1e-5f)) return -1;
    const float ux = aimX / alen, uy = aimY / alen, uz = aimZ / alen;
    int best = -1;
    float bestT = 1e9f;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (!ShouldBeResident(i, currentWing)) continue;
        float sx, sy, sz;
        StudPosition(i, &sx, &sy, &sz);
        const float vx = sx - eyeX, vy = sy - eyeY, vz = sz - eyeZ;
        const float t = vx * ux + vy * uy + vz * uz;
        if (t <= 0.15f || t > kStudMaxRange) continue;
        const float cx = eyeX + t * ux - sx;
        const float cy = eyeY + t * uy - sy;
        const float cz = eyeZ + t * uz - sz;
        if (cx * cx + cy * cy + cz * cz <= kStudRadius * kStudRadius && t < bestT) {
            bestT = t;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Nearest turntable exhibit the visitor is standing near and looking toward (for museum plaques).
inline int FocusExhibit(float visitorX, float visitorY, float forwardX, float forwardY,
                        CeMuseumTour::Wing currentWing) {
    const float flen = std::sqrt(forwardX * forwardX + forwardY * forwardY);
    if (!(flen > 1e-5f)) return -1;
    const float fx = forwardX / flen, fy = forwardY / flen;
    int best = -1;
    float bestScore = -1.0f;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (!ShouldBeResident(i, currentWing)) continue;
        const Spec &sp = kManifest[i];
        const float dx = sp.localX - visitorX, dy = sp.localY - visitorY;
        const float d = std::sqrt(dx * dx + dy * dy);
        const float maxD = sp.style == kStageHero ? 14.0f : 9.5f;
        if (d < 0.2f || d > maxD) continue;
        const float cosA = (dx * fx + dy * fy) / d;
        if (cosA < 0.86f) continue;
        const float score = cosA - 0.015f * d;
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Compute uniform scale factor to fit mesh bounding sphere/extents cleanly onto pedestal.
inline float ComputeScale(float meshSpan, float targetSpan) {
    if (!(meshSpan > 1e-3f)) return 1.0f;
    if (!(targetSpan > 0.0f)) return 1.0f;
    const float s = targetSpan / meshSpan;
    return s < 0.02f ? 0.02f : s > 6.0f ? 6.0f : s;
}

// Runtime adapter functions implemented in ce_museum_turntable.cpp.
void RuntimeInit();
void RuntimeShutdown();
void RuntimeUpdate(CeMuseumTour::Wing wing, float dtSeconds, bool paused,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ);
bool RuntimeHandleFire(bool release, float value);
bool RuntimeIsAimingControl();
bool RuntimeCue(float visitorX, float visitorY, float fwdX, float fwdY,
                CeMuseumTour::Wing wing,
                char *title, unsigned titleCap,
                char *line, unsigned lineCap,
                char *note, unsigned noteCap);
void RuntimeDraw(float floorZ);

} // namespace CeMuseumTurntable

#endif
