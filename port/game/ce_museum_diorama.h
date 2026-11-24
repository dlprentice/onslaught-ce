// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_diorama.h: Four glass-domed living tabletop battle dioramas staged
// symmetrically in the Grand Atrium. Pure C++14 specification, terrain relief math,
// animated miniature unit kinematics, 3D combat tracer trajectories, interactive brass
// pause/resume stud picking, and curator plaque focus — host-tested under ASan/UBSan by
// port/tools/ce_museum_diorama_tests.py.
#ifndef CE_MUSEUM_DIORAMA_H
#define CE_MUSEUM_DIORAMA_H

#include "ce_museum_tour.h"
#include <cmath>
#include <cstdint>

namespace CeMuseumDiorama {

enum {
    kDioramaCount = 4,
    kMaxUnitsPerDiorama = 10,
    kMaxTracersPerDiorama = 8
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
    kThemeArctic = 3
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
    kShapeIceberg = 5
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
    float arcHeight;        // 0 for laser/pulse, >0 for artillery/mortar arc
    float periodSec;        // fire cycle period
    float phaseSec;         // staggered firing phase
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

// Four symmetrically placed octagonal command tables in the 4 open quadrants of the Grand Atrium,
// completely clear of skylight columns (+/-8.2, 28.2/43.8), runners, portals, Directory & Pedestal.
static const DioramaSpec kDioramas[kDioramaCount] = {
    // 1. Southwest Atrium Quadrant — Coastal Beachhead Assault
    {
        kThemeCoastal,
        "Coastal Beachhead Assault",
        "Living Tabletop Diorama I - Forseti Landing",
        "Forseti warships and armour storm a fortified Muspell island ridge beneath circling F-24 cover.",
        -11.4f, 32.2f, 1.0f, 0.0f,
        9u,
        {
            {"fs_marshall",  true,  kShapeShip,     kMotionHoverShip,   -1.08f, -0.58f, 0.03f, 0.04f, 0.90f, 0.00f, 0.00f, 0.56f},
            {"flander",      true,  kShapeShip,     kMotionPatrol,      -0.68f, -0.22f, 0.03f, 0.14f, 0.55f, 0.40f, 0.00f, 0.34f},
            {"fpulsetank",   true,  kShapeTank,     kMotionPatrol,      -0.25f, -0.52f, 0.02f, 0.18f, 0.65f, 0.10f, 0.00f, 0.26f},
            {"f_battletank", true,  kShapeTank,     kMotionPatrol,      -0.18f,  0.05f, 0.02f, 0.16f, 0.58f, 1.80f, 0.00f, 0.28f},
            {"f_be1",        true,  kShapeWalker,   kMotionPatrol,      -0.02f,  0.48f, 0.02f, 0.18f, 0.78f, 2.50f, 0.00f, 0.32f},
            {"M_bunker",     false, kShapeFortress, kMotionStatic,       0.88f,  0.34f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.42f},
            {"mt_laser",     false, kShapeFortress, kMotionTurretSweep,  0.74f, -0.30f, 0.00f, 0.45f, 0.85f, 0.60f, 0.00f, 0.28f},
            {"mtank",        false, kShapeTank,     kMotionPatrol,       0.56f,  0.06f, 0.02f, 0.15f, 0.62f, 1.10f, 0.00f, 0.28f},
            {"fa_f24",       true,  kShapeJet,      kMotionOrbit,        0.00f,  0.00f, 1.05f, 1.05f, 1.15f, 0.00f, 0.42f, 0.30f},
        },
        6u,
        {
            {2u, 6u, 0xff4ee8ffu, 0.00f, 1.35f, 0.00f}, // Pulse Tank -> Laser Turret
            {3u, 7u, 0xff5ceeffu, 0.05f, 1.65f, 0.45f}, // Battle Tank -> Muspell Tank
            {4u, 5u, 0xff68f2ffu, 0.04f, 1.20f, 0.80f}, // Aquila -> Bunker
            {6u, 3u, 0xffff4438u, 0.00f, 1.50f, 0.20f}, // Laser Turret -> Battle Tank
            {7u, 2u, 0xffff5830u, 0.06f, 1.80f, 1.05f}, // Muspell Tank -> Pulse Tank
            {8u, 5u, 0xff76f6ffu, 0.00f, 1.45f, 0.60f}, // F-24 -> Bunker
        }
    },

    // 2. Southeast Atrium Quadrant — Volcanic Foundry Siege
    {
        kThemeVolcanic,
        "Volcanic Foundry Siege",
        "Living Tabletop Diorama II - Muspell Bastion",
        "Warspite and Dominion walkers hold a lava-split basalt caldera against Forseti air wings.",
        11.4f, 32.2f, -1.0f, 0.0f,
        8u,
        {
            {"BOSS-warspite",   false, kShapeFortress, kMotionStatic,       0.12f,  0.14f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.66f},
            {"gunwalker",       false, kShapeWalker,   kMotionPatrol,      -0.78f, -0.45f, 0.02f, 0.18f, 0.72f, 0.30f, 0.00f, 0.28f},
            {"guncrab",         false, kShapeWalker,   kMotionPatrol,       0.84f, -0.34f, 0.02f, 0.16f, 0.56f, 1.50f, 0.00f, 0.32f},
            {"MB_radarstation", false, kShapeFortress, kMotionTurretSweep, -0.68f,  0.66f, 0.00f, 0.80f, 0.50f, 0.00f, 0.00f, 0.38f},
            {"m_be1",           false, kShapeWalker,   kMotionPatrol,       0.62f,  0.64f, 0.02f, 0.16f, 0.82f, 2.20f, 0.00f, 0.32f},
            {"fa_xf24",         true,  kShapeJet,      kMotionOrbit,        0.00f,  0.00f, 1.12f, 1.12f, 1.32f, 0.50f, 0.46f, 0.30f},
            {"fa_heavy_bomber", true,  kShapeJet,      kMotionOrbit,        0.00f,  0.00f, 0.88f, 1.02f, -0.78f, 2.40f, 0.28f, 0.40f},
            {"mt_laser",        false, kShapeFortress, kMotionTurretSweep, -0.18f, -0.84f, 0.00f, 0.60f, 0.95f, 1.20f, 0.00f, 0.26f},
        },
        6u,
        {
            {5u, 0u, 0xff5ceeffu, 0.00f, 1.25f, 0.10f}, // XF-24 -> Warspite
            {6u, 0u, 0xff70f4ffu, 0.14f, 1.90f, 0.70f}, // Heavy Bomber -> Warspite
            {0u, 5u, 0xffff9a30u, 0.00f, 1.30f, 0.40f}, // Warspite flak -> XF-24
            {7u, 6u, 0xffff4e30u, 0.00f, 1.50f, 0.95f}, // Laser -> Heavy Bomber
            {2u, 6u, 0xffffaa3cu, 0.18f, 1.75f, 0.25f}, // Gun Crab flak -> Bomber
            {4u, 5u, 0xffff5c34u, 0.00f, 1.40f, 1.15f}, // Muspell BE -> XF-24
        }
    },

    // 3. Northwest Atrium Quadrant — Skycarrier Fleet Clash
    {
        kThemeSkycarrier,
        "Skycarrier Fleet Clash",
        "Living Tabletop Diorama III - High-Altitude Engagement",
        "Fenrir and heavy air transports duel amid cloud rings while interceptors bank in close dogfights.",
        -11.4f, 47.6f, 1.0f, 0.0f,
        7u,
        {
            {"boss-fenrir", false, kShapeShip, kMotionHoverShip,  0.34f,  0.18f, 0.68f, 0.06f, 0.70f, 0.00f, 0.00f, 0.68f},
            {"fs_cargo",    true,  kShapeShip, kMotionHoverShip, -0.86f, -0.34f, 0.12f, 0.04f, 0.85f, 1.20f, 0.00f, 0.48f},
            {"f_lifter",    true,  kShapeJet,  kMotionHoverShip, -0.58f,  0.54f, 0.62f, 0.06f, 1.05f, 0.50f, 0.00f, 0.36f},
            {"m_dropship",  false, kShapeJet,  kMotionHoverShip,  0.66f, -0.62f, 0.56f, 0.06f, 0.95f, 2.00f, 0.00f, 0.36f},
            {"fa_f24",      true,  kShapeJet,  kMotionOrbit,      0.00f,  0.00f, 1.14f, 1.08f, 1.38f, 0.00f, 0.48f, 0.30f},
            {"muspfite",    false, kShapeJet,  kMotionOrbit,      0.00f,  0.00f, 1.04f, 1.02f, 1.38f, 2.10f, 0.45f, 0.28f},
            {"m_atf",       false, kShapeJet,  kMotionOrbit,      0.00f,  0.00f, 0.92f, 1.15f, -1.12f, 4.10f, 0.42f, 0.30f},
        },
        6u,
        {
            {4u, 5u, 0xff52ecffu, 0.00f, 1.15f, 0.00f}, // F-24 -> Muspell Fighter
            {5u, 2u, 0xffff4a34u, 0.00f, 1.30f, 0.35f}, // Muspell Fighter -> Lifter
            {6u, 4u, 0xffff6838u, 0.00f, 1.25f, 0.70f}, // ATF -> F-24
            {0u, 1u, 0xffff5030u, 0.00f, 1.65f, 0.20f}, // Fenrir -> Cargo Ship
            {4u, 0u, 0xff6cf4ffu, 0.00f, 1.45f, 0.90f}, // F-24 -> Fenrir
            {2u, 3u, 0xff58e8ffu, 0.00f, 1.55f, 0.55f}, // Lifter -> Dropship
        }
    },

    // 4. Northeast Atrium Quadrant — Arctic Kraken Stronghold
    {
        kThemeArctic,
        "Arctic Kraken Stronghold",
        "Living Tabletop Diorama IV - Polar Sea Battle",
        "The Kraken super-weapon surfaces beside polar icebergs as capital ships trade arcing salvoes.",
        11.4f, 47.6f, -1.0f, 0.0f,
        7u,
        {
            {"boss_gill-m",      false, kShapeFortress, kMotionHoverShip,  0.14f,  0.08f, 0.08f, 0.03f, 0.65f, 0.00f, 0.00f, 0.64f},
            {"iceberg1",         false, kShapeIceberg,  kMotionStatic,    -0.88f,  0.64f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.48f},
            {"fs_marshall",      true,  kShapeShip,     kMotionHoverShip, -0.82f, -0.58f, 0.04f, 0.03f, 0.82f, 0.90f, 0.00f, 0.52f},
            {"m_battleship",     false, kShapeShip,     kMotionHoverShip,  0.88f, -0.52f, 0.04f, 0.03f, 0.76f, 2.20f, 0.00f, 0.54f},
            {"fb_control_tower", true,  kShapeFortress, kMotionStatic,     0.74f,  0.76f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.40f},
            {"m_bomber",         false, kShapeJet,      kMotionOrbit,      0.00f,  0.00f, 1.06f, 1.04f, -0.96f, 0.60f, 0.34f, 0.36f},
            {"fa_a15b",          true,  kShapeJet,      kMotionOrbit,      0.00f,  0.00f, 0.92f, 1.12f,  1.22f, 2.80f, 0.42f, 0.32f},
        },
        6u,
        {
            {2u, 0u, 0xff5ceeffu, 0.34f, 1.85f, 0.00f}, // Marshall arcing salvo -> Kraken
            {3u, 2u, 0xffffa63cu, 0.36f, 1.95f, 0.65f}, // Battleship arcing salvo -> Marshall
            {0u, 2u, 0xffff4234u, 0.00f, 1.55f, 0.35f}, // Kraken beam -> Marshall
            {6u, 0u, 0xff6af4ffu, 0.00f, 1.35f, 0.90f}, // A-15B strafe -> Kraken
            {5u, 4u, 0xffff6c34u, 0.16f, 1.75f, 0.25f}, // Muspell Bomber -> Control Tower
            {4u, 5u, 0xff56e6ffu, 0.00f, 1.45f, 1.15f}, // Control Tower AA -> Bomber
        }
    },
};

// Deterministic sculpted tabletop terrain height (metres above kTabletopZ, 0.00..0.38m).
inline float TerrainHeight(Theme theme, float rx, float ry) {
    const float r = std::sqrt(rx * rx + ry * ry);
    if (r >= kTerrainRadius) return 0.0f;
    const float rimFade = 1.0f - (r / kTerrainRadius) * (r / kTerrainRadius);
    const float fade = rimFade < 0.0f ? 0.0f : rimFade;
    switch (theme) {
    case kThemeCoastal: {
        // West/South bay water (0.0m) curving up into an emerald fortified East/North island ridge.
        const float shore = rx * 0.72f + ry * 0.42f + 0.18f * std::sin(ry * 2.4f);
        if (shore <= -0.15f) return 0.0f;
        const float rise = (shore + 0.15f) * 0.30f + 0.07f * std::sin(rx * 3.2f) * std::cos(ry * 2.8f);
        const float h = rise * (0.45f + 0.55f * fade);
        return h < 0.0f ? 0.0f : (h > 0.38f ? 0.38f : h);
    }
    case kThemeVolcanic: {
        // Obsidian basalt caldera with glowing central lava moat & raised volcanic peaks.
        const float peak = 0.24f * std::exp(-((rx - 0.12f) * (rx - 0.12f) + (ry - 0.14f) * (ry - 0.14f)) * 1.4f);
        const float ridge = 0.18f * (0.5f + 0.5f * std::sin(rx * 2.6f + 0.4f) * std::cos(ry * 2.4f));
        const float canyon = std::fabs(rx * 0.8f - ry * 0.6f + 0.25f * std::sin(ry * 2.5f));
        const float cut = canyon < 0.22f ? (canyon / 0.22f) : 1.0f;
        const float h = (0.04f + (peak + ridge) * cut) * (0.35f + 0.65f * fade);
        return h < 0.0f ? 0.0f : (h > 0.36f ? 0.36f : h);
    }
    case kThemeSkycarrier: {
        // Deep calm sapphire ocean below hovering skycarriers (flat sea with subtle swell).
        const float swell = 0.018f * (1.0f + std::sin(rx * 3.0f + ry * 2.2f));
        return swell * fade;
    }
    case kThemeArctic: {
        // Polar sea with sharp glaciated NE & NW ice shelves.
        const float nwShelf = std::exp(-((rx + 0.90f) * (rx + 0.90f) + (ry - 0.68f) * (ry - 0.68f)) * 2.2f);
        const float neShelf = std::exp(-((rx - 0.76f) * (rx - 0.76f) + (ry - 0.76f) * (ry - 0.76f)) * 2.4f);
        const float h = (0.32f * nwShelf + 0.28f * neShelf) * (0.4f + 0.6f * fade);
        return h < 0.0f ? 0.0f : (h > 0.36f ? 0.36f : h);
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
        dz = u.baseZ + 0.06f * std::sin(a * 2.0f);
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
        dz = floorH + u.baseZ + 0.025f * std::sin(a);
        yaw = u.phaseRad + 0.06f * std::sin(a * 0.6f);
        pitch = 0.045f * std::cos(a);
        roll = 0.035f * std::sin(a * 1.3f);
        break;
    }
    case kMotionTurretSweep: {
        const float a = simTime * u.speedRad + u.phaseRad;
        dz = TerrainHeight(theme, dx, dy) + u.baseZ;
        yaw = u.phaseRad + u.orbitRadius * std::sin(a);
        break;
    }
    }

    // Hard safety clamp so no miniature unit ever clips outside the geodesic crystal dome.
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
    float impactAlpha;      // 0..1 glowing impact flash when bolt hits target
    uint32_t colour;
};

inline void EvaluateTracer(const DioramaSpec &d, const TracerSpec &tr, float simTime, TracerSegment *out) {
    if (!out) return;
    out->active = false;
    out->impactAlpha = 0.0f;
    out->colour = tr.colour;
    if (tr.fromUnit >= d.unitCount || tr.toUnit >= d.unitCount || !(tr.periodSec > 0.1f)) return;

    UnitPose src = {}, dst = {};
    EvaluateUnitPose(d.theme, d.units[tr.fromUnit], simTime, &src);
    EvaluateUnitPose(d.theme, d.units[tr.toUnit], simTime, &dst);

    const float cyc = std::fmod(simTime + tr.phaseSec, tr.periodSec);
    const float u = (cyc < 0.0f ? cyc + tr.periodSec : cyc) / tr.periodSec; // 0..1
    const float zSrc = src.dz + 0.07f;
    const float zDst = dst.dz + 0.07f;

    // Flight occupies u in [0.00, 0.72]; impact burst flashes across [0.70, 0.92].
    if (u <= 0.72f) {
        const float head = u / 0.72f;
        const float tail = head > 0.24f ? (head - 0.24f) : 0.0f;
        auto sample = [&](float s, float *ox, float *oy, float *oz) {
            *ox = src.dx + (dst.dx - src.dx) * s;
            *oy = src.dy + (dst.dy - src.dy) * s;
            *oz = zSrc + (zDst - zSrc) * s + tr.arcHeight * 4.0f * s * (1.0f - s);
        };
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
