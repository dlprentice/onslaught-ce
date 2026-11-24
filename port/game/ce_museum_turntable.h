// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_turntable.h: Rotating 3D turntable pedestals for the Grand Museum.
// Pure C++ state machine, ALL 45/45 (100%) retail GT_MESH Goodie 3D exhibits resident
// simultaneously across all wings (zero doorway pop-in), and wing-scoped ray-pick
// Pause/Resume switch logic — compiled both in-game (ce_museum_turntable.cpp) and in host
// tests (port/tools/ce_museum_turntable_tests.py).
#ifndef CE_MUSEUM_TURNTABLE_H
#define CE_MUSEUM_TURNTABLE_H

#include "ce_museum_tour.h"
#include <cmath>

namespace CeMuseumTurntable {

constexpr float kDefaultRateRadPerSec = 12.0f * 3.14159265f / 180.0f; // ~12 deg/s (~30 s / rev)
constexpr float kStudRadius = 0.28f;
constexpr float kStudMaxRange = 5.5f;
// All 45/45 retail GT_MESH Goodie 3D exhibits stay resident simultaneously so sightlines across wings never pop in.
enum { kMaxResidentGoodies = 48, kMaxTurntables = 48 };

enum StageStyle {
    kStageGround = 0,  // upright on plinth top
    kStageFlight,      // elevated & banked on a flight pylon
    kStageNaval,       // scaled vessel staged over a recessed water basin
    kStageHero,        // monumental courtyard centerpiece
    kStageScale        // relative scale alcove (stationary comparison, toggleable)
};

struct Spec {
    int goodieId;             // retail goodie_NN_res_PC.aya index (verified GT_MESH in FEPGoodies.cpp)
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

// Complete 45/45 (100%) retail GT_MESH Goodie 3D exhibits loaded once at Museum load from installed game files.
enum { kManifestCount = 45 };
static const Spec kManifest[kManifestCount] = {
    // ---- Coastal Courtyard (5 exhibits: Hero Aquila + 2 Aquila Heritage configs + 2 flanking coastal flight pylons) ----
    {8,  CeMuseumTour::kWingCourtyard, kStageHero,
     "Battle Engine Aquila (Unit-00)", "Forseti Dual-Mode Walker / Jet",
     "Full-scale RI-04 dual-mode machine uniting armoured ground fire with aerial strike.",
     0.0f, 10.5f, 1.35f, 2.50f, 4.8f, 0.0f, 0.0f, -1.0f, false},
    {9,  CeMuseumTour::kWingCourtyard, kStageGround,
     "BE:A Unit-01 'Pulsar'", "Aquila Standard Configuration",
     "Balanced upgrade adding a Plasma Grenade Launcher and expanded munition stores.",
     -5.8f, -1.6f, 0.90f, 1.15f, 2.5f, 0.0f, 1.0f, 0.0f, false},
    {10, CeMuseumTour::kWingCourtyard, kStageGround,
     "BE:A Unit-03 'Lancer'", "Aquila Precision Configuration",
     "Long-range energy loadout pairing an MBC-1 Stream Laser with guided Flux missiles.",
     5.8f, -1.6f, 0.90f, 1.15f, 2.5f, 0.0f, -1.0f, 0.0f, false},
    {20, CeMuseumTour::kWingCourtyard, kStageFlight,
     "XF-28 Venom", "Forseti Advanced Interceptor",
     "Mach 2 successor to the F-24 Valkyrie banked on the western coastal showcase pylon.",
     -10.2f, 0.5f, 2.35f, 1.40f, 3.2f, 0.34f, 1.0f, 0.0f, false},
    {37, CeMuseumTour::kWingCourtyard, kStageFlight,
     "ASX-9 Storm", "Muspell Advanced Tactical Fighter",
     "Mach 2.5 jet interceptor built from stolen XF-28 data, banked on the eastern pylon.",
     10.2f, 0.5f, 2.35f, 1.40f, 3.2f, -0.34f, -1.0f, 0.0f, false},

    // ---- Grand Atrium North Heritage Alcoves (2 exhibits: Aquila Stealth Config + Lost Toys Panorama Monument) ----
    {11, CeMuseumTour::kWingAtrium, kStageGround,
     "BE:A Unit-04S 'Sniper'", "Aquila Stealth Configuration",
     "Cloaked machine built from downed Vilifier stealth tech and armed with a Rail Gun.",
     -5.4f, 48.0f, 0.95f, 1.25f, 2.7f, 0.0f, 0.0f, -1.0f, false},
    {76, CeMuseumTour::kWingAtrium, kStageGround,
     "Lost Toys Panorama Monument", "Commemorative Studio Archive",
     "Panoramic studio commemorative sculpture celebrating the creators of Battle Engine Aquila.",
     5.4f, 48.0f, 0.95f, 1.25f, 2.8f, 0.0f, 0.0f, -1.0f, false},

    // ---- Forseti Forces Wing (9 Goodie 3D exhibits alongside the 6 native bay displays — all 5 Forseti Turrets!) ----
    {16, CeMuseumTour::kWingForseti, kStageGround,
     "AV-18 Bardiche Assault Tank", "Forseti Heavy Armour",
     "Modified Sabre hull mounting a stationary-fire MBC-1 Beam Cannon and heat-sink pods.",
     -40.8f, 38.0f, 0.90f, 1.30f, 2.6f, 0.0f, 1.0f, 0.0f, false},
    {17, CeMuseumTour::kWingForseti, kStageGround,
     "M-112 Archer Mobile Artillery", "Forseti Mobile Artillery",
     "Lightly armoured hover chassis deploying stability pods to fire the MPC-3 Plasma Cannon.",
     -40.8f, 44.0f, 0.90f, 1.30f, 2.6f, 0.0f, 1.0f, 0.0f, false},
    {14, CeMuseumTour::kWingForseti, kStageGround,
     "AV-14 Sabre Light Tank", "Forseti Light Armour",
     "Hovering IS-2 pulse-cannon tank forming the backbone of Forseti armoured divisions.",
     -25.25f, 48.4f, 0.85f, 1.10f, 2.3f, 0.0f, 0.0f, -1.0f, false},
    {18, CeMuseumTour::kWingForseti, kStageGround,
     "Forseti Supply Truck", "Forseti Logistics Vehicle",
     "Unarmed multi-purpose Scaena F250 hover truck hauling troops and munitions.",
     -32.75f, 48.4f, 0.85f, 1.10f, 2.3f, 0.0f, 0.0f, -1.0f, false},
    {28, CeMuseumTour::kWingForseti, kStageGround,
     "Forseti SAT Turret", "Forseti Air Defence",
     "Fixed Surface-to-Air Torpedo battery firing seeking plasma warheads at aircraft.",
     -40.8f, 49.0f, 0.85f, 1.05f, 2.0f, 0.0f, 0.0f, -1.0f, false},
    {29, CeMuseumTour::kWingForseti, kStageGround,
     "Forseti Beam Turret", "Forseti Base Defence",
     "Fixed 360-degree MBC-1 Beam Cannon emplacement defending Forseti coastal bases.",
     -25.25f, 33.6f, 0.85f, 1.10f, 2.1f, 0.0f, 0.0f, 1.0f, false},
    {30, CeMuseumTour::kWingForseti, kStageGround,
     "Forseti Pulse Turret", "Forseti Base Defence",
     "Fixed IS-2 Pulse Cannon turret defending Forseti perimeters against enemy armour.",
     -40.8f, 33.0f, 0.85f, 1.05f, 2.0f, 0.0f, 0.0f, 1.0f, false},
    {31, CeMuseumTour::kWingForseti, kStageGround,
     "Forseti Artillery Emplacement", "Forseti Coastal Defence",
     "Long-range fixed MPC-33 Plasma Cannon emplacement firing three-round bursts.",
     -32.75f, 33.6f, 0.85f, 1.10f, 2.1f, 0.0f, 0.0f, 1.0f, false},
    {32, CeMuseumTour::kWingForseti, kStageGround,
     "Forseti Blaster Turret", "Forseti Heavy Defence",
     "Reliable M-6 Blaster emplacement tasked with infantry suppression and light AA fire.",
     -18.0f, 48.6f, 0.85f, 1.05f, 2.0f, 0.0f, 0.0f, -1.0f, false},

    // ---- Muspell Wing (13 exhibits: 2 rows of 4 + 2 East Apse + 3 corner turret alcoves - all 5 Muspell Turrets!) ----
    {47, CeMuseumTour::kWingMuspell, kStageGround,
     "W-8 Cerberus Walker", "Muspell Recon / Tank Hunter",
     "Early Muspell bipedal tank hunter armed with twin 110mm and 88mm cannons.",
     21.8f, 48.0f, 0.90f, 1.50f, 2.9f, 0.0f, 0.0f, -1.0f, false},
    {48, CeMuseumTour::kWingMuspell, kStageGround,
     "W-11 Marauder Walker", "Muspell Assault Walker",
     "Up-armoured multi-role walker mounting Twin Machine Guns and a 110mm or CB-130 gun.",
     27.0f, 48.0f, 0.90f, 1.50f, 3.0f, 0.0f, 0.0f, -1.0f, false},
    {49, CeMuseumTour::kWingMuspell, kStageGround,
     "WX-15 Dominator 'Crab Walker'", "Muspell Heavy Walker",
     "Six-legged assault walker mounting Twin 130mm Cannons across rough island terrain.",
     32.2f, 48.0f, 0.90f, 1.50f, 3.1f, 0.0f, 0.0f, -1.0f, false},
    {51, CeMuseumTour::kWingMuspell, kStageGround,
     "D-9 Arachnodrone Walker", "Muspell Swarm Drone",
     "Automated three-metre Needle Laser walker deployed in rapid overwhelming swarms.",
     37.4f, 48.0f, 0.90f, 1.50f, 3.1f, 0.0f, 0.0f, -1.0f, false},
    {43, CeMuseumTour::kWingMuspell, kStageGround,
     "T-92 Growler Main Battle Tank", "Muspell Main Battle Tank",
     "Heavy 60mm Steelam tank mounting Twin 110mm Cannons and close-range weaponry.",
     21.8f, 34.0f, 0.90f, 1.50f, 3.0f, 0.0f, 0.0f, 1.0f, false},
    {45, CeMuseumTour::kWingMuspell, kStageGround,
     "A-3 Steel Rain Mobile Artillery", "Muspell Siege Artillery",
     "Massive Steelam hover carriage mounting a 360-degree G-6 Siege Cannon turret.",
     27.0f, 34.0f, 0.90f, 1.50f, 3.0f, 0.0f, 0.0f, 1.0f, false},
    {44, CeMuseumTour::kWingMuspell, kStageGround,
     "ML-16 Vengeance Rocket Launcher", "Muspell Rocket Artillery",
     "Tracked-hover fire-support unit launching recoilless multi-warhead rocket barrages.",
     32.2f, 34.0f, 0.90f, 1.50f, 2.9f, 0.0f, 0.0f, 1.0f, false},
    {46, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Supply Truck", "Muspell Logistics Vehicle",
     "Armoured GRUFF G2 hover truck adapted from civilian haulage for military duty.",
     37.4f, 34.0f, 0.90f, 1.50f, 2.8f, 0.0f, 0.0f, 1.0f, false},
    {53, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell SAM Site", "Muspell Air Defence",
     "Automated anti-aircraft missile battery firing long-range SA-15 Granite missiles.",
     18.4f, 48.2f, 0.85f, 1.05f, 2.0f, 0.0f, 0.0f, -1.0f, false},
    {54, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Cannon Turret", "Muspell Base Defence",
     "Armoured GRUFF E-1 turret mounting the 88mm Cannon from the T-85 Land Raider.",
     41.2f, 38.0f, 0.90f, 1.25f, 2.4f, 0.0f, -1.0f, 0.0f, false},
    {55, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Machine Gun Turret", "Muspell Perimeter Defence",
     "Compact bunker turret firing a Quad 20mm Machine Gun against infantry and light craft.",
     41.2f, 48.8f, 0.85f, 1.05f, 2.0f, 0.0f, 0.0f, -1.0f, false},
    {56, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Artillery Emplacement", "Muspell Siege Defence",
     "Well-armoured bunker housing a long-range 150mm Cannon lobbing explosive shells.",
     41.2f, 44.0f, 0.90f, 1.25f, 2.4f, 0.0f, -1.0f, 0.0f, false},
    {57, CeMuseumTour::kWingMuspell, kStageGround,
     "Muspell Flak Turret", "Muspell Anti-Air Defence",
     "Agile quad-20mm anti-aircraft turret filling island skies with bursting shrapnel.",
     41.2f, 33.2f, 0.85f, 1.05f, 2.0f, 0.0f, 0.0f, 1.0f, false},

    // ---- Aviation & Naval Hangar: West Aviation Staggered Double-Row Flight Pylons (9 aircraft — 100% of all aircraft!) ----
    // Outer West Row (x = -15.6, 5 aircraft)
    {19, CeMuseumTour::kWingHangar, kStageFlight,
     "F-24 Valkyrie Fighter", "Forseti Air Superiority",
     "Mach 1.8 Mag-Thruster fighter armed with Micro Missiles and a P-141 Pulse Laser.",
     -15.6f, 55.0f, 2.65f, 1.35f, 3.1f, 0.42f, 1.0f, 0.0f, false},
    {21, CeMuseumTour::kWingHangar, kStageFlight,
     "B-7F Monitor Bomber", "Forseti Heavy Bomber",
     "Venerable Mag-Thruster heavy bomber delivering Leveller Spread Bombs on anti-ship runs.",
     -15.6f, 60.0f, 3.10f, 1.35f, 3.4f, 0.32f, 1.0f, 0.0f, false},
    {22, CeMuseumTour::kWingHangar, kStageFlight,
     "A-15 Penetrator Ground Attack", "Forseti Close Air Support",
     "Armoured 400 mph strike craft pairing a twin TL-C Energy Torpedo Launcher with a laser.",
     -15.6f, 65.0f, 2.75f, 1.35f, 3.2f, 0.38f, 1.0f, 0.0f, false},
    {38, CeMuseumTour::kWingHangar, kStageFlight,
     "B-4 Sky Tyrant Heavy Bomber", "Muspell Heavy Bomber",
     "Heavily armoured jet bomber dropping Redemption Iron Bombs across multiple passes.",
     -15.6f, 70.0f, 3.10f, 1.35f, 3.4f, 0.34f, 1.0f, 0.0f, false},
    {41, CeMuseumTour::kWingHangar, kStageFlight,
     "UD-5H Quell Heavy Lifter", "Muspell Assault Transport",
     "VTOL folding-wing assault transport deploying entire Muspell invasion forces.",
     -15.6f, 75.0f, 2.85f, 1.35f, 3.3f, 0.30f, 1.0f, 0.0f, false},
    // Inner Staggered West Row (x = -9.8, 4 aircraft framing the open y = 65.0 cross-aisle)
    {23, CeMuseumTour::kWingHangar, kStageFlight,
     "U-17 Highside Transporter", "Forseti VTOL Transport",
     "Quad Mag-Thruster VTOL utility craft ferrying cargo and supplies between islands.",
     -9.8f, 58.4f, 2.95f, 1.20f, 3.0f, 0.28f, 1.0f, 0.0f, false},
    {36, CeMuseumTour::kWingHangar, kStageFlight,
     "ASF-7 Lightning Interceptor", "Muspell Interceptor",
     "Mach 2.2 cross-wing interceptor armed with Twin Vulcan Cannons and Rapier missiles.",
     -9.8f, 62.2f, 2.55f, 1.20f, 2.8f, 0.44f, 1.0f, 0.0f, false},
    {39, CeMuseumTour::kWingHangar, kStageFlight,
     "A-19 Enforcer Ground Attack", "Muspell Close Air Support",
     "Heavy forward-armoured strike aircraft chewing through armour with a 30mm Gattling.",
     -9.8f, 68.4f, 2.65f, 1.20f, 2.9f, 0.36f, 1.0f, 0.0f, false},
    {40, CeMuseumTour::kWingHangar, kStageFlight,
     "UD-3 Thunder Dropship", "Muspell Troop Transport",
     "High-altitude jet dropship deploying jetpack-equipped Muspell Commandos.",
     -9.8f, 72.8f, 3.05f, 1.20f, 3.0f, 0.30f, 1.0f, 0.0f, false},

    // ---- Aviation & Naval Hangar: East Naval Basin (4 vessels over recessed dark water) ----
    {26, CeMuseumTour::kWingHangar, kStageNaval,
     "FSV-6 Marshall Frigate", "Forseti Heavy Frigate",
     "Heavy escort frigate carrying Quad Plasma Cannons, M-4N blasters, and a Repair Pad.",
     13.0f, 58.2f, 0.65f, 4.20f, 4.0f, 0.0f, -1.0f, 0.0f, false},
    {25, CeMuseumTour::kWingHangar, kStageNaval,
     "NAV-3 Turtle Landing Craft", "Forseti Assault Lander",
     "Heavily armoured amphibious assault vessel with M605 Plasma Grenade Launchers.",
     13.0f, 62.7f, 0.65f, 4.20f, 3.4f, 0.0f, -1.0f, 0.0f, false},
    {27, CeMuseumTour::kWingHangar, kStageNaval,
     "FG-8 Packhorse Supply Frigate", "Forseti Fleet Logistics",
     "Unarmed 40-knot Nascon-powered supply vessel sustaining Forseti island lifelines.",
     13.0f, 67.2f, 0.65f, 4.20f, 3.8f, 0.0f, -1.0f, 0.0f, false},
    {52, CeMuseumTour::kWingHangar, kStageNaval,
     "BB-2 Manticore Battleship", "Muspell Capital Ship",
     "40-knot battleship mounting four Triple 150mm Cannons plus Vulcan and Flak turrets.",
     13.0f, 71.7f, 0.65f, 4.20f, 4.2f, 0.0f, -1.0f, 0.0f, false},

    // ---- Aviation & Naval Hangar: North Relative Scale Alcove (3 calibrated units) ----
    {50, CeMuseumTour::kWingHangar, kStageScale,
     "G-1 Gnat Attack Drone", "Relative Scale Alcove",
     "Solar-celled flying attack drone opening its wings to power a small Beam Laser.",
     -3.8f, 75.3f, 0.55f, 1.48f, 1.2f, 0.0f, 0.0f, -1.0f, true},
    {42, CeMuseumTour::kWingHangar, kStageScale,
     "T-85 Land Raider Tank", "Relative Scale Alcove",
     "Reliable 45mm Steelam assault tank mounting an 88mm Cannon along the scale wall.",
     0.0f, 75.3f, 0.55f, 1.48f, 2.4f, 0.0f, 0.0f, -1.0f, true},
    {15, CeMuseumTour::kWingHangar, kStageScale,
     "M-1 Broadsword Main Battle Tank", "Relative Scale Alcove",
     "Heavy 80mm Plasteel battle tank with Twin IS-4 Pulse Cannons on the scale wall.",
     3.8f, 75.3f, 0.55f, 1.48f, 3.1f, 0.0f, 0.0f, -1.0f, true},
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

// Keep all 45/45 retail 3D Goodie exhibits resident simultaneously across every wing so
// sightlines through Atrium doorways and galleries show exhibits with zero pop-in.
inline bool ShouldBeResident(unsigned index, CeMuseumTour::Wing /*currentWing*/) {
    return index < kManifestCount;
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

// Ray-pick the closest turntable brass control stud in the visitor's current wing.
inline int PickStud(float eyeX, float eyeY, float eyeZ,
                    float aimX, float aimY, float aimZ,
                    CeMuseumTour::Wing currentWing) {
    const float alen = std::sqrt(aimX * aimX + aimY * aimY + aimZ * aimZ);
    if (!(alen > 1e-5f)) return -1;
    const float ux = aimX / alen, uy = aimY / alen, uz = aimZ / alen;
    int best = -1;
    float bestT = 1e9f;
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (kManifest[i].wing != currentWing) continue;
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
        if (kManifest[i].wing != currentWing) continue;
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
