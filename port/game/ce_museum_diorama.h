// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_diorama.h: Four glass-domed living tabletop battle dioramas staged
// symmetrically in the Grand Atrium. Pure C++14 specification, terrain relief math,
// animated miniature unit kinematics, 3D combat tracer trajectories, interactive brass
// pause/resume lectern stud picking, and curator plaque focus - host-tested under
// ASan/UBSan by port/tools/ce_museum_diorama_tests.py.
#ifndef CE_MUSEUM_DIORAMA_H
#define CE_MUSEUM_DIORAMA_H

#include "ce_museum_tour.h"
#include <cmath>
#include <cstdint>

namespace CeMuseumDiorama {

enum {
    kDioramaCount = 4,
    kMaxUnitsPerDiorama = 18,
    kMaxTracersPerDiorama = 16
};

constexpr float kTableRadius = 2.05f;
constexpr float kTerrainRadius = 1.84f;
constexpr float kDomeRadius = 1.94f;
constexpr float kTabletopZ = 1.05f;
constexpr float kDomeTopZ = 3.05f;
constexpr float kStudRadius = 0.26f;
constexpr float kStudMaxRange = 5.5f;
constexpr float kFocusMaxRange = 9.5f;

enum Theme {
    kThemeCoastal = 0,
    kThemeVolcanic = 1,
    kThemeSkycarrier = 2,
    kThemeNautilus = 3
};

enum MotionKind {
    kMotionStatic = 0,
    kMotionPatrol = 1,
    kMotionOrbit = 2,
    kMotionHoverShip = 3,
    kMotionTurretSweep = 4
};

enum FallbackShape {
    kShapeTank = 0,
    kShapeJet = 1,
    kShapeShip = 2,
    kShapeWalker = 3,
    kShapeFortress = 4,
    kShapeSubmarine = 5
};

enum TracerStyle {
    kTracerPulse = 0,
    kTracerBeam = 1,
    kTracerArtillery = 2,
    kTracerFlak = 3
};

struct UnitSpec {
    const char *meshStem;   // retail data/resources/meshes/m_<stem>.msh.aya stem
    bool isForseti;
    FallbackShape fallback;
    MotionKind motion;
    float baseX, baseY;     // relative to table centre (metres)
    float baseZ;            // offset above terrain (ground) or tabletop (air/hover)
    float orbitRadius;      // patrol amplitude or orbit radius
    float speedRad;         // angular / cycle speed (rad/s)
    float phaseRad;         // initial phase
    float bankRad;          // roll bank for orbiting aircraft
    float targetSpan;       // miniature visual diameter (metres)
};

struct TracerSpec {
    unsigned fromUnit;
    unsigned toUnit;
    uint32_t colour;        // 0xAARRGGBB luminous bolt colour
    float arcHeight;        // 0 for laser/pulse, >0 for artillery/mortar/torpedo arc
    float periodSec;        // fire cycle period
    float phaseSec;         // staggered firing phase
    TracerStyle style;      // pulse salvo, sustained beam, artillery arc, or AA flak
};

struct DioramaSpec {
    Theme theme;
    const char *title;
    const char *subtitle;
    const char *caption;
    float localX, localY;   // Atrium centre in visitor-local metres
    float studDirX, studDirY;
    unsigned unitCount;
    UnitSpec units[kMaxUnitsPerDiorama];
    unsigned tracerCount;
    TracerSpec tracers[kMaxTracersPerDiorama];
};

// Four symmetrically placed circular command tables in the 4 open quadrants of the Grand Atrium,
// completely clear of skylight columns (+/-8.2, 28.2/43.8), runners, portals, Directory & Pedestal.
static const DioramaSpec kDioramas[kDioramaCount] = {
    // 1. Southwest Atrium Quadrant - Coastal Beachhead Assault (15 hero-scaled units)
    {
        kThemeCoastal,
        "Coastal Beachhead Assault",
        "Living Tabletop Diorama I - Forseti Landing",
        "Forseti warships and armour storm a fortified Muspell island ridge beneath circling F-24 cover.",
        -11.4f, 32.2f, 1.0f, 0.0f,
        15u,
        {
            // Hero flagship + amphibious landers
            {"fs_marshall",  true,  kShapeShip,     kMotionHoverShip,   -1.14f, -0.58f, 0.04f, 0.03f, 0.86f, 0.35f, 0.00f, 0.60f},
            {"flander",      true,  kShapeShip,     kMotionHoverShip,   -0.74f, -0.14f, 0.03f, 0.04f, 0.72f, 0.50f, 0.00f, 0.34f},
            {"flander",      true,  kShapeShip,     kMotionHoverShip,   -0.46f, -0.86f, 0.03f, 0.04f, 0.68f, 0.65f, 0.00f, 0.33f},
            // Frontline Forseti & Muspell armour + walkers
            {"fpulsetank",   true,  kShapeTank,     kMotionPatrol,       0.02f, -0.42f, 0.02f, 0.11f, 0.68f, 0.15f, 0.00f, 0.27f},
            {"f_battletank", true,  kShapeTank,     kMotionPatrol,      -0.18f,  0.22f, 0.02f, 0.11f, 0.58f, 1.70f, 0.00f, 0.30f},
            {"fbeamtank",    true,  kShapeTank,     kMotionPatrol,      -0.56f,  0.72f, 0.02f, 0.10f, 0.62f, 2.85f, 0.00f, 0.29f},
            {"f_be1",        true,  kShapeWalker,   kMotionPatrol,       0.08f,  0.62f, 0.02f, 0.11f, 0.76f, 2.40f, 0.00f, 0.34f},
            {"mtank",        false, kShapeTank,     kMotionPatrol,       0.54f,  0.04f, 0.02f, 0.10f, 0.64f, 1.15f, 0.00f, 0.30f},
            {"gunwalker",    false, kShapeWalker,   kMotionPatrol,       0.48f, -0.64f, 0.02f, 0.10f, 0.74f, 0.80f, 0.00f, 0.28f},
            // Compact coastal fortifications & ridge turrets
            {"M_bunker",     false, kShapeFortress, kMotionStatic,       0.94f,  0.30f, 0.00f, 0.00f, 0.00f, 3.30f, 0.00f, 0.24f},
            {"mt_laser",     false, kShapeFortress, kMotionTurretSweep,  0.82f, -0.28f, 0.00f, 0.48f, 0.88f, 2.80f, 0.00f, 0.22f},
            {"mt_artillery", false, kShapeFortress, kMotionTurretSweep,  0.50f,  0.84f, 0.00f, 0.52f, 0.76f, 3.70f, 0.00f, 0.22f},
            // Air cover on two staggered altitude bands (0.86m / 1.24m)
            {"fa_f24",       true,  kShapeJet,      kMotionOrbit,        0.00f,  0.00f, 0.86f, 1.12f, 1.18f, 0.20f, 0.42f, 0.29f},
            {"fa_f24",       true,  kShapeJet,      kMotionOrbit,        0.00f,  0.00f, 1.24f, 0.92f, 1.28f, 3.30f, 0.45f, 0.27f},
            {"muspfite",     false, kShapeJet,      kMotionOrbit,        0.00f,  0.00f, 1.06f, 1.04f, -1.32f, 1.70f, 0.44f, 0.28f},
        },
        13u,
        {
            {0u,  9u, 0xff5ceeffu, 0.34f, 1.60f, 0.00f, kTracerArtillery}, // Marshall salvo -> Bunker
            {0u, 11u, 0xff62f0ffu, 0.30f, 1.75f, 0.85f, kTracerArtillery}, // Marshall salvo -> Artillery Turret
            {3u, 10u, 0xff4ee8ffu, 0.00f, 1.05f, 0.10f, kTracerPulse},     // Sabre -> Laser Turret
            {4u,  7u, 0xff5ceeffu, 0.04f, 1.25f, 0.45f, kTracerPulse},     // Broadsword -> Growler
            {5u, 11u, 0xff7af6ffu, 0.00f, 1.40f, 0.25f, kTracerBeam},      // Bardiche beam -> Artillery
            {6u,  9u, 0xff68f2ffu, 0.03f, 1.10f, 0.72f, kTracerPulse},     // Aquila -> Bunker
            {7u,  4u, 0xffff5830u, 0.05f, 1.35f, 0.90f, kTracerPulse},     // Growler -> Broadsword
            {8u,  3u, 0xffff6432u, 0.00f, 1.15f, 0.35f, kTracerPulse},     // Cerberus -> Sabre
            {10u, 4u, 0xffff3c2eu, 0.00f, 1.30f, 0.20f, kTracerBeam},      // Laser Turret -> Broadsword
            {11u, 1u, 0xffff9e38u, 0.32f, 1.65f, 0.60f, kTracerArtillery}, // Artillery -> Lander
            {12u, 9u, 0xff78f6ffu, 0.00f, 1.15f, 0.55f, kTracerPulse},     // F-24 -> Bunker
            {13u,14u, 0xff64f0ffu, 0.00f, 1.00f, 0.15f, kTracerPulse},     // High F-24 -> Lightning
            {14u,12u, 0xffff5030u, 0.00f, 1.10f, 0.65f, kTracerPulse},     // Lightning -> Low F-24
        }
    },

    // 2. Southeast Atrium Quadrant - Volcanic Foundry Siege (15 hero-scaled units)
    {
        kThemeVolcanic,
        "Volcanic Foundry Siege",
        "Living Tabletop Diorama II - Muspell Bastion",
        "Warspite and Muspell walkers hold a lava-split basalt caldera against Forseti assault wings.",
        11.4f, 32.2f, -1.0f, 0.0f,
        15u,
        {
            // Central volcanic bastion hero
            {"BOSS-warspite",   false, kShapeFortress, kMotionStatic,       0.16f,  0.14f, 0.00f, 0.00f, 0.00f, 0.35f, 0.00f, 0.68f},
            // Frontline Muspell & Forseti ground forces across basalt ridges
            {"guncrab",         false, kShapeWalker,   kMotionPatrol,       0.96f, -0.34f, 0.02f, 0.10f, 0.56f, 1.50f, 0.00f, 0.35f},
            {"gunwalker2",      false, kShapeWalker,   kMotionPatrol,      -0.88f, -0.12f, 0.02f, 0.09f, 0.68f, 0.30f, 0.00f, 0.30f},
            {"gunwalker",       false, kShapeWalker,   kMotionPatrol,       0.84f,  0.66f, 0.02f, 0.10f, 0.74f, 2.10f, 0.00f, 0.28f},
            {"m_be1",           false, kShapeWalker,   kMotionPatrol,       0.36f,  0.86f, 0.02f, 0.10f, 0.80f, 2.60f, 0.00f, 0.33f},
            {"f_be1",           true,  kShapeWalker,   kMotionPatrol,      -1.02f,  0.34f, 0.02f, 0.10f, 0.78f, 0.60f, 0.00f, 0.34f},
            {"fbeamtank",       true,  kShapeTank,     kMotionPatrol,      -0.34f, -0.98f, 0.02f, 0.10f, 0.62f, 1.15f, 0.00f, 0.29f},
            {"fplasmatank",     true,  kShapeTank,     kMotionPatrol,      -1.18f, -0.52f, 0.02f, 0.10f, 0.56f, 0.20f, 0.00f, 0.28f},
            // Compact perimeter radar & bastion turrets
            {"MB_radarstation", false, kShapeFortress, kMotionTurretSweep, -0.62f,  1.02f, 0.00f, 0.85f, 0.50f, 0.00f, 0.00f, 0.24f},
            {"mt_laser",        false, kShapeFortress, kMotionTurretSweep,  0.26f, -0.78f, 0.00f, 0.58f, 0.92f, 1.80f, 0.00f, 0.21f},
            {"mt_flak",         false, kShapeFortress, kMotionTurretSweep,  1.02f,  0.24f, 0.00f, 0.65f, 1.04f, 0.40f, 0.00f, 0.21f},
            // Air wings on two staggered altitude bands (0.86..0.90m / 1.24..1.28m)
            {"fa_xf24",         true,  kShapeJet,      kMotionOrbit,        0.00f,  0.00f, 1.24f, 0.94f,  1.36f, 0.40f, 0.46f, 0.29f},
            {"fa_heavy_bomber", true,  kShapeJet,      kMotionOrbit,        0.00f,  0.00f, 0.86f, 1.12f, -0.82f, 2.30f, 0.28f, 0.36f},
            {"fa_a15b",         true,  kShapeJet,      kMotionOrbit,        0.00f,  0.00f, 0.90f, 1.04f,  1.14f, 4.20f, 0.40f, 0.30f},
            {"m_atf",           false, kShapeJet,      kMotionOrbit,        0.00f,  0.00f, 1.28f, 0.88f, -1.42f, 3.60f, 0.46f, 0.28f},
        },
        14u,
        {
            {11u, 0u, 0xff5ceeffu, 0.00f, 1.05f, 0.10f, kTracerPulse},     // Venom -> Warspite
            {12u, 0u, 0xff70f4ffu, 0.16f, 1.60f, 0.65f, kTracerArtillery}, // Monitor Bomber -> Warspite
            {13u, 1u, 0xff62f0ffu, 0.00f, 1.15f, 0.30f, kTracerPulse},     // Penetrator -> Dominator
            {5u,  2u, 0xff58ecffu, 0.00f, 1.10f, 0.45f, kTracerPulse},     // Aquila -> Marauder
            {6u,  0u, 0xff7cf8ffu, 0.00f, 1.35f, 0.20f, kTracerBeam},      // Bardiche beam -> Warspite
            {7u,  0u, 0xff66f2ffu, 0.34f, 1.70f, 0.85f, kTracerArtillery}, // Archer mortar -> Warspite
            {0u, 11u, 0xffff9a30u, 0.00f, 1.15f, 0.35f, kTracerFlak},      // Warspite flak -> Venom
            {0u,  5u, 0xffff422eu, 0.00f, 1.45f, 0.75f, kTracerBeam},      // Warspite beam -> Aquila
            {1u, 12u, 0xffffaa3cu, 0.18f, 1.55f, 0.25f, kTracerFlak},      // Dominator flak -> Bomber
            {2u,  5u, 0xffff5c34u, 0.00f, 1.20f, 0.55f, kTracerPulse},     // Marauder -> Aquila
            {4u, 13u, 0xffff6030u, 0.00f, 1.25f, 0.95f, kTracerPulse},     // Muspell BE -> Penetrator
            {9u,  6u, 0xffff4230u, 0.00f, 1.30f, 0.15f, kTracerBeam},      // Laser Turret -> Bardiche
            {10u,12u, 0xffffb040u, 0.00f, 1.10f, 0.50f, kTracerFlak},      // Flak Turret -> Bomber
            {14u,11u, 0xffff5434u, 0.00f, 1.05f, 0.40f, kTracerPulse},     // Storm -> Venom
        }
    },

    // 3. Northwest Atrium Quadrant - Skycarrier Fleet Clash (14 hero-scaled units)
    {
        kThemeSkycarrier,
        "Skycarrier Fleet Clash",
        "Living Tabletop Diorama III - High-Altitude Engagement",
        "Venturer and Fenrir trade broadsides above open seas as fighters bank through cloud decks.",
        -11.4f, 47.6f, 1.0f, 0.0f,
        14u,
        {
            // Two dueling airborne flagship heroes
            {"boss-fenrir", false, kShapeShip, kMotionHoverShip,  0.56f,  0.34f, 0.64f, 0.05f, 0.68f, 3.65f, 0.00f, 0.70f},
            {"f_ventura",   true,  kShapeShip, kMotionHoverShip, -0.58f, -0.32f, 0.58f, 0.05f, 0.74f, 0.52f, 0.00f, 0.66f},
            // Escort warships on the sea below & hovering support transports
            {"fs_marshall", true,  kShapeShip, kMotionHoverShip, -0.98f,  0.56f, 0.04f, 0.04f, 0.82f, 0.20f, 0.00f, 0.36f},
            {"m_battleship",false, kShapeShip, kMotionHoverShip,  0.96f, -0.58f, 0.04f, 0.04f, 0.76f, 3.30f, 0.00f, 0.36f},
            {"f_lifter",    true,  kShapeJet,  kMotionHoverShip, -0.92f, -0.78f, 0.44f, 0.05f, 1.02f, 0.70f, 0.00f, 0.32f},
            {"m_dropship",  false, kShapeJet,  kMotionHoverShip,  0.86f,  0.86f, 0.44f, 0.05f, 0.94f, 2.40f, 0.00f, 0.32f},
            {"fs_cargo",    true,  kShapeShip, kMotionHoverShip, -0.22f,  1.06f, 0.04f, 0.04f, 0.70f, 5.40f, 0.00f, 0.34f},
            // Lower air band (0.84..0.90m)
            {"m_groundat",  false, kShapeJet,  kMotionOrbit,      0.00f,  0.00f, 0.84f, 1.14f, -1.08f, 0.90f, 0.36f, 0.28f},
            {"fa_f24",      true,  kShapeJet,  kMotionOrbit,      0.00f,  0.00f, 0.88f, 1.16f,  1.34f, 0.00f, 0.46f, 0.29f},
            {"muspfite",    false, kShapeJet,  kMotionOrbit,      0.00f,  0.00f, 0.90f, 1.08f,  1.34f, 3.15f, 0.45f, 0.28f},
            {"muspfite",    false, kShapeJet,  kMotionOrbit,      0.00f,  0.00f, 0.84f, 1.02f, -1.28f, 4.40f, 0.44f, 0.27f},
            // Upper air band (1.22..1.28m)
            {"fa_f24",      true,  kShapeJet,  kMotionOrbit,      0.00f,  0.00f, 1.24f, 0.92f,  1.42f, 2.60f, 0.48f, 0.27f},
            {"fa_xf24",     true,  kShapeJet,  kMotionOrbit,      0.00f,  0.00f, 1.28f, 0.86f,  1.50f, 5.10f, 0.48f, 0.28f},
            {"m_atf",       false, kShapeJet,  kMotionOrbit,      0.00f,  0.00f, 1.22f, 0.96f, -1.44f, 1.20f, 0.48f, 0.28f},
        },
        14u,
        {
            {0u,  1u, 0xffff422cu, 0.00f, 1.45f, 0.10f, kTracerBeam},      // Fenrir ion beam -> Venturer
            {1u,  0u, 0xff5ceeffu, 0.00f, 1.35f, 0.55f, kTracerBeam},      // Venturer broadside -> Fenrir
            {0u,  4u, 0xffff8c30u, 0.00f, 1.30f, 0.85f, kTracerFlak},      // Fenrir AA -> Highside
            {1u,  5u, 0xff68f2ffu, 0.00f, 1.25f, 0.25f, kTracerPulse},     // Venturer AA -> Thunder
            {2u,  3u, 0xff5ceeffu, 0.30f, 1.75f, 0.00f, kTracerArtillery}, // Marshall -> Manticore
            {3u,  2u, 0xffffa63cu, 0.32f, 1.85f, 0.70f, kTracerArtillery}, // Manticore -> Marshall
            {2u,  0u, 0xff70f4ffu, 0.00f, 1.50f, 0.40f, kTracerFlak},      // Marshall AA -> Fenrir
            {8u,  9u, 0xff52ecffu, 0.00f, 1.00f, 0.00f, kTracerPulse},     // Low F-24 -> Lightning
            {9u,  1u, 0xffff4e34u, 0.00f, 1.15f, 0.35f, kTracerPulse},     // Lightning -> Venturer
            {10u, 8u, 0xffff6034u, 0.00f, 1.10f, 0.60f, kTracerPulse},     // Lightning 2 -> Low F-24
            {11u,13u, 0xff6cf4ffu, 0.00f, 1.05f, 0.20f, kTracerPulse},     // High F-24 -> Storm
            {12u, 0u, 0xff78f8ffu, 0.00f, 1.15f, 0.80f, kTracerPulse},     // Venom -> Fenrir
            {13u,12u, 0xffff5836u, 0.00f, 1.05f, 0.50f, kTracerPulse},     // Storm -> Venom
            {7u,  4u, 0xffff6c34u, 0.00f, 1.25f, 0.90f, kTracerPulse},     // Enforcer -> Highside
        }
    },

    // 4. Northeast Atrium Quadrant - Nautilus Gill-M Ambush (14 hero-scaled units, equatorial reef!)
    {
        kThemeNautilus,
        "Nautilus Gill-M Ambush",
        "Living Tabletop Diorama IV - Equatorial Reef Sea Battle",
        "Gill-M and a Thornback submarine ambush Forseti warships across equatorial reef coves.",
        11.4f, 47.6f, -1.0f, 0.0f,
        14u,
        {
            // Two surfacing sea-monster & submarine heroes
            {"boss_gill-m",      false, kShapeFortress,  kMotionHoverShip,  0.06f,  0.04f, 0.05f, 0.03f, 0.62f, 0.40f, 0.00f, 0.66f},
            {"M_submarine",      false, kShapeSubmarine, kMotionHoverShip,  0.82f, -0.46f, 0.03f, 0.04f, 0.58f, 2.45f, 0.00f, 0.60f},
            // Frontline warships & equatorial headland defenders
            {"fs_marshall",      true,  kShapeShip,      kMotionHoverShip, -0.86f, -0.58f, 0.04f, 0.04f, 0.82f, 0.60f, 0.00f, 0.46f},
            {"fs_marshall",      true,  kShapeShip,      kMotionHoverShip, -1.12f, -0.06f, 0.04f, 0.04f, 0.78f, 5.90f, 0.00f, 0.42f},
            {"m_battleship",     false, kShapeShip,      kMotionHoverShip,  0.04f, -1.04f, 0.04f, 0.04f, 0.74f, 1.90f, 0.00f, 0.44f},
            {"flander",          true,  kShapeShip,      kMotionHoverShip, -0.22f,  0.62f, 0.03f, 0.04f, 0.86f, 5.40f, 0.00f, 0.32f},
            {"fpulsetank",       true,  kShapeTank,      kMotionPatrol,    -0.88f,  0.96f, 0.02f, 0.09f, 0.64f, 0.80f, 0.00f, 0.26f},
            {"f_be1",            true,  kShapeWalker,    kMotionPatrol,     0.66f,  0.86f, 0.02f, 0.09f, 0.72f, 2.20f, 0.00f, 0.32f},
            // Compact coastal reef emplacements
            {"fb_control_tower", true,  kShapeFortress,  kMotionStatic,     1.14f,  0.56f, 0.00f, 0.00f, 0.00f, 3.80f, 0.00f, 0.24f},
            {"ft_beam",          true,  kShapeFortress,  kMotionTurretSweep,-1.22f, 0.66f, 0.00f, 0.55f, 0.88f, 5.60f, 0.00f, 0.21f},
            {"ft_pulse",         true,  kShapeFortress,  kMotionTurretSweep, 0.58f, 1.18f, 0.00f, 0.58f, 0.94f, 4.40f, 0.00f, 0.20f},
            // Torpedo & interceptor wings on two staggered altitude bands (0.86m / 1.24..1.28m)
            {"fa_a15b",          true,  kShapeJet,       kMotionOrbit,      0.00f,  0.00f, 0.86f, 1.12f,  1.22f, 2.80f, 0.42f, 0.31f},
            {"m_bomber",         false, kShapeJet,       kMotionOrbit,      0.00f,  0.00f, 1.24f, 0.94f, -0.98f, 0.60f, 0.34f, 0.34f},
            {"fa_f24",           true,  kShapeJet,       kMotionOrbit,      0.00f,  0.00f, 1.28f, 0.88f,  1.36f, 4.20f, 0.45f, 0.28f},
        },
        13u,
        {
            {2u,  0u, 0xff5ceeffu, 0.34f, 1.65f, 0.00f, kTracerArtillery}, // Marshall 1 salvo -> Gill-M
            {3u,  1u, 0xff64f0ffu, 0.32f, 1.75f, 0.55f, kTracerArtillery}, // Marshall 2 salvo -> Thornback
            {4u,  2u, 0xffffa63cu, 0.36f, 1.80f, 0.65f, kTracerArtillery}, // Manticore salvo -> Marshall 1
            {1u,  3u, 0xffff7a30u, 0.28f, 1.70f, 0.30f, kTracerArtillery}, // Thornback missile -> Marshall 2
            {0u,  2u, 0xffff4234u, 0.00f, 1.40f, 0.35f, kTracerBeam},      // Gill-M bio-beam -> Marshall 1
            {0u,  5u, 0xffff6438u, 0.00f, 1.50f, 0.95f, kTracerPulse},     // Gill-M burst -> Turtle
            {6u,  0u, 0xff50e8ffu, 0.00f, 1.15f, 0.20f, kTracerPulse},     // Sabre -> Gill-M
            {7u,  1u, 0xff6af4ffu, 0.04f, 1.20f, 0.70f, kTracerPulse},     // Aquila -> Thornback
            {9u,  0u, 0xff78f8ffu, 0.00f, 1.35f, 0.45f, kTracerBeam},      // Beam Turret -> Gill-M
            {10u,12u, 0xff58e8ffu, 0.00f, 1.15f, 0.15f, kTracerFlak},      // Pulse Turret AA -> Sky Tyrant
            {11u, 1u, 0xff6af4ffu, 0.10f, 1.25f, 0.85f, kTracerPulse},     // Penetrator torpedo run -> Thornback
            {12u, 8u, 0xffff6c34u, 0.20f, 1.60f, 0.25f, kTracerArtillery}, // Sky Tyrant -> Control Tower
            {13u,12u, 0xff62f0ffu, 0.00f, 1.05f, 0.50f, kTracerPulse},     // F-24 -> Sky Tyrant
        }
    },
};

// Shared lava-channel distance function for Diorama II (Volcanic Foundry Siege) so terrain
// carving, scorched-bank colouring, and the glowing molten magma ribbons stay 100% aligned.
inline float VolcanicLavaDistance(float rx, float ry) {
    // Primary S-curved lava canyon separating western approach ridges from Warspite's caldera.
    const float c0 = std::fabs(rx * 0.78f - ry * 0.62f + 0.22f * std::sin(ry * 2.4f) + 0.38f);
    // Eastern branching fissure draining toward the SE caldera rim.
    float c1 = 10.0f;
    if (rx > 0.24f) {
        c1 = std::fabs((rx - 0.30f) * 0.54f + (ry + 0.32f) * 0.84f + 0.10f * std::sin(rx * 3.2f));
        if (rx < 0.44f) c1 += (0.44f - rx) * 0.9f;
    }
    // Northern branching fissure draining toward the NW rim.
    float c2 = 10.0f;
    if (ry > 0.34f && rx < -0.10f) {
        c2 = std::fabs((rx + 0.24f) * 0.50f + (ry - 0.48f) * 0.86f - 0.08f * std::cos(rx * 3.0f));
        if (ry < 0.52f) c2 += (0.52f - ry) * 0.9f;
    }
    float d = c0 < c1 ? c0 : c1;
    if (c2 < d) d = c2;
    return d;
}

// Deterministic sculpted tabletop terrain height (metres above kTabletopZ, 0.00..0.52m).
inline float TerrainHeight(Theme theme, float rx, float ry) {
    const float r = std::sqrt(rx * rx + ry * ry);
    if (r >= kTerrainRadius) return 0.0f;
    const float rr = r / kTerrainRadius;
    // Keep steep geological relief out to ~88% of radius, then taper cleanly into the strata rim.
    const float rimFade = rr < 0.86f ? 1.0f : (1.0f - (rr - 0.86f) / 0.14f);
    const float fade = rimFade < 0.0f ? 0.0f : (rimFade * rimFade * (3.0f - 2.0f * rimFade));

    switch (theme) {
    case kThemeCoastal: {
        // SW turquoise bay (0.0m) curving through a golden sand landing cove up into terraced
        // emerald bluffs and a fortified eastern limestone ridge (up to 0.50m).
        const float shore = rx * 0.72f + ry * 0.42f + 0.16f * std::sin(ry * 2.3f);
        if (shore <= -0.36f) return 0.0f;
        const float t = (shore + 0.36f) / 1.95f;
        const float tc = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        // Stepped coastal headland terraces + natural ridge undulation.
        const float terraced = tc + 0.045f * std::sin(tc * 15.70796f);
        const float bluff = 0.44f * terraced + 0.075f * std::sin(rx * 3.4f + 0.5f) * std::cos(ry * 2.9f);
        const float peakNE = 0.14f * std::exp(-((rx - 0.88f) * (rx - 0.88f) + (ry - 0.62f) * (ry - 0.62f)) * 2.2f);
        const float h = (bluff + peakNE) * fade;
        return h < 0.0f ? 0.0f : (h > 0.51f ? 0.51f : h);
    }
    case kThemeVolcanic: {
        // Multi-tiered obsidian basalt caldera split by 3 branching molten lava channels,
        // central raised Warspite bastion mesa, and jagged outer caldera rim peaks (up to 0.52m).
        const float mesa = 0.30f * std::exp(-((rx - 0.16f) * (rx - 0.16f) + (ry - 0.14f) * (ry - 0.14f)) * 1.85f);
        const float rimRing = 0.24f * std::exp(-((r - 1.18f) * (r - 1.18f)) * 3.4f)
                            * (0.72f + 0.28f * std::sin(std::atan2(ry, rx) * 5.0f + 0.8f));
        const float crag = 0.12f * (0.5f + 0.5f * std::sin(rx * 3.6f + 0.3f) * std::cos(ry * 3.2f - 0.4f));
        const float raw = 0.09f + mesa + rimRing + crag;
        // Terraced basalt steps
        const float stepped = raw + 0.028f * std::sin(raw * 26.0f);
        const float lavaD = VolcanicLavaDistance(rx, ry);
        const float cut = lavaD < 0.17f ? (lavaD / 0.17f) : 1.0f;
        const float smoothCut = cut * cut * (3.0f - 2.0f * cut);
        const float h = (0.015f + stepped * smoothCut) * fade;
        return h < 0.0f ? 0.0f : (h > 0.52f ? 0.52f : h);
    }
    case kThemeSkycarrier: {
        // Deep calm sapphire ocean with layered multi-frequency sea swells below cloud decks.
        const float s1 = 0.018f * (1.0f + std::sin(rx * 3.4f + ry * 2.2f));
        const float s2 = 0.008f * (1.0f + std::cos(rx * 5.2f - ry * 4.1f));
        return (s1 + s2) * fade;
    }
    case kThemeNautilus: {
        // Equatorial Nautilus volcanic-reef coves framing a turquoise coral lagoon (NO ice!).
        const float nwD2 = (rx + 1.02f) * (rx + 1.02f) + (ry - 0.92f) * (ry - 0.92f);
        const float neD2 = (rx - 0.90f) * (rx - 0.90f) + (ry - 0.88f) * (ry - 0.88f);
        const float seD2 = (rx - 1.26f) * (rx - 1.26f) + (ry + 0.98f) * (ry + 0.98f);
        const float nwBluff = 0.48f * std::exp(-nwD2 * 3.50f);
        const float neBluff = 0.50f * std::exp(-neD2 * 3.15f);
        const float seReef  = 0.24f * std::exp(-seD2 * 4.40f);
        const float crag = 0.035f * std::sin(rx * 4.4f) * std::cos(ry * 4.0f);
        const float base = nwBluff + neBluff + seReef;
        if (base <= 0.032f) return 0.0f;
        const float lifted = (base - 0.032f) * 1.08f + crag * (base > 0.14f ? 1.0f : base / 0.14f);
        const float terraced = lifted + 0.022f * std::sin(lifted * 24.0f);
        const float h = terraced * fade;
        return h < 0.0f ? 0.0f : (h > 0.50f ? 0.50f : h);
    }
    default:
        return 0.0f;
    }
}

// Maximum safe horizontal radius inside the hemispherical crystal dome at height zAboveTabletop.
inline float DomeHorizontalRadius(float zAboveTabletop) {
    const float domeH = kDomeTopZ - kTabletopZ;
    const float u = zAboveTabletop / domeH;
    if (u <= 0.0f) return kDomeRadius;
    if (u >= 1.0f) return 0.0f;
    return kDomeRadius * std::sqrt(1.0f - u * u);
}

struct UnitPose {
    float dx, dy, dz;       // relative to table centre; dz is positive upward above kTabletopZ
    float yawRad;
    float pitchRad;
    float rollRad;
};

inline void EvaluateUnitPose(Theme theme, const UnitSpec &u, float simTime, UnitPose *out) {
    if (!out) return;
    float dx = u.baseX;
    float dy = u.baseY;
    float dz = u.baseZ;
    float yaw = u.phaseRad;
    float pitch = 0.0f;
    float roll = 0.0f;

    switch (u.motion) {
    case kMotionStatic:
        dz = TerrainHeight(theme, dx, dy) + u.baseZ;
        yaw = u.phaseRad;
        break;
    case kMotionPatrol: {
        const float a = simTime * u.speedRad + u.phaseRad;
        const float s = std::sin(a);
        const float c = std::cos(a);
        dx = u.baseX + u.orbitRadius * s;
        dy = u.baseY + u.orbitRadius * 0.45f * c;
        dz = TerrainHeight(theme, dx, dy) + u.baseZ;
        yaw = std::atan2(u.orbitRadius * 0.45f * (-s), u.orbitRadius * c);
        break;
    }
    case kMotionOrbit: {
        const float a = simTime * u.speedRad + u.phaseRad;
        dx = u.baseX + u.orbitRadius * std::cos(a);
        dy = u.baseY + u.orbitRadius * std::sin(a);
        dz = u.baseZ + 0.055f * std::sin(a * 2.0f);
        const float dir = u.speedRad >= 0.0f ? 1.0f : -1.0f;
        yaw = a + dir * 1.5707963f;
        roll = dir * u.bankRad;
        pitch = 0.06f * std::cos(a * 2.0f);
        break;
    }
    case kMotionHoverShip: {
        const float a = simTime * u.speedRad + u.phaseRad;
        dx = u.baseX + u.orbitRadius * std::sin(a * 0.7f);
        dy = u.baseY + u.orbitRadius * std::cos(a * 0.5f);
        const float floorH = (u.baseZ > 0.25f) ? 0.0f : TerrainHeight(theme, dx, dy);
        dz = floorH + u.baseZ + 0.022f * std::sin(a);
        yaw = u.phaseRad + 0.06f * std::sin(a * 0.6f);
        pitch = 0.042f * std::cos(a);
        roll = 0.032f * std::sin(a * 1.3f);
        break;
    }
    case kMotionTurretSweep: {
        const float a = simTime * u.speedRad + u.phaseRad;
        dz = TerrainHeight(theme, dx, dy) + u.baseZ;
        yaw = u.phaseRad + u.orbitRadius * std::sin(a);
        break;
    }
    }

    // Hard safety clamp so no miniature unit ever clips outside the crystal dome.
    const float maxR = DomeHorizontalRadius(dz + u.targetSpan * 0.35f) - u.targetSpan * 0.50f - 0.05f;
    const float safeR = maxR > 0.10f ? maxR : 0.10f;
    const float curR = std::sqrt(dx * dx + dy * dy);
    if (curR > safeR && curR > 1e-5f) {
        const float k = safeR / curR;
        dx *= k;
        dy *= k;
    }

    out->dx = dx;
    out->dy = dy;
    out->dz = dz;
    out->yawRad = yaw;
    out->pitchRad = pitch;
    out->rollRad = roll;
}

struct TracerSegment {
    bool active;
    float ax, ay, az;       // bolt tail (relative to table centre, Z up above kTabletopZ)
    float bx, by, bz;       // bolt head
    float impactX, impactY, impactZ;
    float impactAlpha;      // 0..1 glowing starburst flash when bolt hits target
    float smokeAlpha;       // 0..1 rising soft miniature smoke puff after impact
    float smokeRise;        // upward drift (metres) of trailing smoke puff
    uint32_t colour;
    TracerStyle style;
};

inline void EvaluateTracer(const DioramaSpec &d, const TracerSpec &tr, float simTime, TracerSegment *out) {
    if (!out) return;
    out->active = false;
    out->impactAlpha = 0.0f;
    out->smokeAlpha = 0.0f;
    out->smokeRise = 0.0f;
    out->colour = tr.colour;
    out->style = tr.style;
    if (tr.fromUnit >= d.unitCount || tr.toUnit >= d.unitCount || !(tr.periodSec > 0.1f)) return;

    UnitPose src = {}, dst = {};
    EvaluateUnitPose(d.theme, d.units[tr.fromUnit], simTime, &src);
    EvaluateUnitPose(d.theme, d.units[tr.toUnit], simTime, &dst);

    const float cyc = std::fmod(simTime + tr.phaseSec, tr.periodSec);
    const float u = (cyc < 0.0f ? cyc + tr.periodSec : cyc) / tr.periodSec; // 0..1
    const float zSrc = src.dz + 0.07f;
    const float zDst = dst.dz + 0.07f;

    auto sample = [&](float s, float *ox, float *oy, float *oz) {
        *ox = src.dx + (dst.dx - src.dx) * s;
        *oy = src.dy + (dst.dy - src.dy) * s;
        *oz = zSrc + (zDst - zSrc) * s + tr.arcHeight * 4.0f * s * (1.0f - s);
    };

    if (tr.style == kTracerBeam) {
        // Sustained luminous beam lance fires across [0.08, 0.72] with glowing target starburst.
        if (u >= 0.08f && u <= 0.72f) {
            const float head = u < 0.20f ? (u - 0.08f) / 0.12f : 1.0f;
            sample(0.0f, &out->ax, &out->ay, &out->az);
            sample(head, &out->bx, &out->by, &out->bz);
            out->active = true;
        }
        if (u >= 0.18f && u <= 0.86f) {
            const float f = u <= 0.70f ? 0.85f : (1.0f - (u - 0.70f) / 0.16f) * 0.85f;
            out->impactX = dst.dx;
            out->impactY = dst.dy;
            out->impactZ = zDst;
            out->impactAlpha = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        }
        if (u >= 0.62f && u <= 0.98f) {
            const float st = (u - 0.62f) / 0.36f;
            out->impactX = dst.dx;
            out->impactY = dst.dy;
            out->impactZ = zDst;
            out->smokeAlpha = (1.0f - st) * 0.55f;
            out->smokeRise = 0.03f + 0.14f * st;
        }
        return;
    }

    // Projectile / salvo / artillery flight occupies u in [0.00, 0.72]; starburst in [0.70, 0.94].
    if (u <= 0.72f) {
        const float head = u / 0.72f;
        const float segLen = (tr.style == kTracerArtillery) ? 0.22f : 0.26f;
        const float tail = head > segLen ? (head - segLen) : 0.0f;
        sample(tail, &out->ax, &out->ay, &out->az);
        sample(head, &out->bx, &out->by, &out->bz);
        out->active = true;
    }
    if (u >= 0.70f && u <= 0.94f) {
        const float f = 1.0f - (u - 0.70f) / 0.24f;
        out->impactX = dst.dx;
        out->impactY = dst.dy;
        out->impactZ = zDst;
        out->impactAlpha = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    }
    if (u >= 0.72f && u <= 0.99f) {
        const float st = (u - 0.72f) / 0.27f;
        out->impactX = dst.dx;
        out->impactY = dst.dy;
        out->impactZ = zDst;
        out->smokeAlpha = (1.0f - st) * (tr.style == kTracerArtillery ? 0.85f : 0.60f);
        out->smokeRise = 0.03f + 0.16f * st;
    }
}

struct DioramaState {
    float simTime[kDioramaCount];
    bool paused[kDioramaCount];
    bool initialized;
    bool fireHeld;
    bool fireCaptured;
    int hoveredIndex;
};

inline void Reset(DioramaState *s) {
    if (!s) return;
    for (unsigned i = 0; i < kDioramaCount; ++i) {
        s->simTime[i] = float(i) * 1.7f;
        s->paused[i] = false;
    }
    s->initialized = true;
    s->fireHeld = false;
    s->fireCaptured = false;
    s->hoveredIndex = -1;
}

inline void Step(DioramaState *s, float dtSeconds, bool gamePaused) {
    if (!s || !s->initialized || gamePaused || !(dtSeconds > 0.0f)) return;
    const float dt = dtSeconds > 0.25f ? 0.25f : dtSeconds;
    for (unsigned i = 0; i < kDioramaCount; ++i) {
        if (!s->paused[i]) s->simTime[i] += dt;
    }
}

inline void TogglePause(DioramaState *s, int index) {
    if (!s || !s->initialized || index < 0 || static_cast<unsigned>(index) >= kDioramaCount) return;
    s->paused[index] = !s->paused[index];
}

inline void StudPosition(unsigned index, float *x, float *y, float *z) {
    const unsigned i = index < kDioramaCount ? index : 0u;
    const DioramaSpec &d = kDioramas[i];
    const float dist = kTableRadius + 0.14f;
    if (x) *x = d.localX + d.studDirX * dist;
    if (y) *y = d.localY + d.studDirY * dist;
    if (z) *z = kTabletopZ;
}

inline int PickStud(float eyeX, float eyeY, float eyeZ,
                    float aimX, float aimY, float aimZ,
                    CeMuseumTour::Wing currentWing) {
    if (currentWing != CeMuseumTour::kWingAtrium) return -1;
    const float alen = std::sqrt(aimX * aimX + aimY * aimY + aimZ * aimZ);
    if (!(alen > 1e-5f)) return -1;
    const float ux = aimX / alen, uy = aimY / alen, uz = aimZ / alen;
    int best = -1;
    float bestT = 1e9f;
    for (unsigned i = 0; i < kDioramaCount; ++i) {
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
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

inline int FocusDiorama(float visitorX, float visitorY, float forwardX, float forwardY,
                        CeMuseumTour::Wing currentWing) {
    if (currentWing != CeMuseumTour::kWingAtrium) return -1;
    const float flen = std::sqrt(forwardX * forwardX + forwardY * forwardY);
    if (!(flen > 1e-5f)) return -1;
    const float fx = forwardX / flen, fy = forwardY / flen;
    int best = -1;
    float bestScore = -1.0f;
    for (unsigned i = 0; i < kDioramaCount; ++i) {
        const DioramaSpec &d = kDioramas[i];
        const float dx = d.localX - visitorX, dy = d.localY - visitorY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 0.25f || dist > kFocusMaxRange) continue;
        const float cosA = (dx * fx + dy * fy) / dist;
        if (cosA < 0.82f) continue;
        const float score = cosA - 0.015f * dist;
        if (score > bestScore) {
            bestScore = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Runtime adapter functions implemented in ce_museum_diorama.cpp.
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
void RuntimeDraw(float floorZ, bool ownStateBlock = true);

} // namespace CeMuseumDiorama

#endif
