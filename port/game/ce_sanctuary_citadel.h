// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_sanctuary_citadel.h: Open-Air Terrain-Backed Sanctuary Museum Citadel,
// shimmering hexagonal geodesic Archive Shield Dome, impact-ripple FX ring buffer,
// sealed north Cinema pavilion containment, and live In-World Archive Pedestal bridge.
// Pure C++14 header with no engine or D3D headers so host tests verify all math and state.
#ifndef CE_SANCTUARY_CITADEL_H
#define CE_SANCTUARY_CITADEL_H

#define CE_SANDBOX_HAS_CITADEL 1

#include "ce_sandbox_world.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace CeSanctuaryCitadel {

// Shared Living Sandbox Sanctuary plateau geometry (native world metres, Z points DOWN).
constexpr float kCenterX = 112.0f;
constexpr float kCenterY = 392.0f;
constexpr float kFloorZ  = -34.0f;
constexpr float kDomeRadius = 56.0f;
constexpr float kDomeSkirtDepth = 2.0f; // dome seals down to floorZ + 2.0f (-32.0f)

// Visual shield dome shell: clears the 4 outer retaining bastion / brass cornice corners
// (hypot(39.7, 39.7) = 56.14 m) by ~21 cm and extends underground across all 360 deg
// (floorZ + 24.5 m = -9.5 m, >= 6.6 m below the lowest valley trough around the cliff).
constexpr float kDomeVisualRadius = 56.35f;
constexpr float kDomeVisualSkirtMaxZ = 24.5f;
constexpr float kDomeVisualSkirtNzMax = 0.46f;

constexpr float kTerraceMinX = 72.0f;
constexpr float kTerraceMaxX = 152.0f;
constexpr float kTerraceMinY = 352.0f;
constexpr float kTerraceMaxY = 432.0f;

// In-World Archive Pedestal mounted atop the stepped octagonal rotunda dais.
constexpr float kPedestalX = 112.0f;
constexpr float kPedestalY = 392.0f;
constexpr float kPedestalTopZ = -35.05f;
constexpr float kPedestalHeight = kFloorZ - kPedestalTopZ; // 1.05 m above floor

// North Sealed Cinema Pavilion enclosure and outdoor-draw-skip volume.
constexpr float kCinemaWallMinX = 98.0f;
constexpr float kCinemaWallMaxX = 126.0f;
constexpr float kCinemaWallMinY = 410.0f;
constexpr float kCinemaWallMaxY = 428.0f;
constexpr float kCinemaDoorMinX = 109.5f;
constexpr float kCinemaDoorMaxX = 114.5f;
constexpr float kCinemaMinX = 98.5f;
constexpr float kCinemaMaxX = 125.5f;
constexpr float kCinemaMinY = 411.5f;
constexpr float kCinemaMaxY = 427.5f;
constexpr float kCinemaCeilingHeight = 10.0f; // Z in [floorZ - 10.0f, floorZ + 2.0f] => [-44.0f, -32.0f]

// Cinema roof slab (the coffered roof with its cornice overhang). Heights are
// metres above the terrace floor: the soffit at 9.00, the walkable top at 10.65.
constexpr float kCinemaRoofMinX = 97.4f;
constexpr float kCinemaRoofMaxX = 126.6f;
constexpr float kCinemaRoofMinY = 409.4f;
constexpr float kCinemaRoofMaxY = 428.6f;
constexpr float kCinemaRoofBottomH = 9.00f;
constexpr float kCinemaRoofTopH = 10.65f;

// Forseti signal mast on the south-east bastion corner: the one tall, off-axis
// silhouette that marks the Citadel as a frontline fortress from the valley.
constexpr float kSignalMastX = 146.0f;
constexpr float kSignalMastY = 358.0f;
constexpr float kSignalMastHeight = 25.9f;   // inside the dome (28.7 m of headroom at this corner)
constexpr float kSignalMastBaseHalf = 1.20f;

// Landmark positions for alcove cues and pedestal local-frame conversion.
constexpr float kForsetiAlcoveX = 88.5f;
constexpr float kForsetiAlcoveY = 392.0f;
constexpr float kMuspellAlcoveX = 135.5f;
constexpr float kMuspellAlcoveY = 392.0f;
constexpr float kAlcoveCueRadius = 7.5f;

// Grand Museum world entrance origin (matches CeMuseumWorld::kEntranceX/Y for pedestal pose conversion).
constexpr float kMuseumEntranceX = 256.0f;
constexpr float kMuseumEntranceY = 240.0f;

// DirectInput key code handled in addition to CeArchivePedestal's built-in shortcuts.
constexpr unsigned kDikR = 0x13u;

// Shield Dome impact-ripple FX ring buffer parameters and beam clamp safety margin.
constexpr unsigned kMaxRipples = 16u;
constexpr float kRippleDuration = 0.65f;
constexpr float kRippleMaxDt = 0.25f;
constexpr float kBeamPullback = 0.05f;

inline bool IsFinite3(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

// Queries terrain ground Z (native Z-down) along the visual shield dome perimeter at
// radius kDomeVisualRadius around (kCenterX, kCenterY).
inline float DomePerimeterGroundZ(float azimuthRad, float floorZ = kFloorZ) {
    const float baseZ = std::isfinite(floorZ) ? floorZ : kFloorZ;
    if (!std::isfinite(azimuthRad)) return baseZ;
    const float wx = kCenterX + std::cos(azimuthRad) * kDomeVisualRadius;
    const float wy = kCenterY + std::sin(azimuthRad) * kDomeVisualRadius;
    const float gz = CeSandboxWorld::HeightAt(wx, wy) + (baseZ - kFloorZ);
    const float minZ = baseZ - 8.0f;
    const float maxZ = baseZ + kDomeVisualSkirtMaxZ - 2.0f;
    if (gz < minZ) return minZ;
    if (gz > maxZ) return maxZ;
    return gz;
}

// Tangent-continuous (C^1 at nz = 0) mapping from unit sphere direction (nx, ny, nz) to
// dome-local coordinates relative to (kCenterX, kCenterY, floorZ). Upper hemisphere
// (nz <= 0, native Z-down) maps spherically at kDomeVisualRadius; lower skirt (nz > 0)
// maps via arc-length-preserving cylindrical projection down to kDomeVisualSkirtMaxZ.
inline void ProjectDomeVisualPoint(float nx, float ny, float nz,
                                   float *outX, float *outY, float *outZ) {
    float x = nx * kDomeVisualRadius;
    float y = ny * kDomeVisualRadius;
    float z = nz * kDomeVisualRadius;
    if (nz > 0.0f) {
        const float hr = std::hypot(nx, ny);
        if (hr > 1e-4f) {
            x = (nx / hr) * kDomeVisualRadius;
            y = (ny / hr) * kDomeVisualRadius;
        }
        const float clampedNz = nz > 1.0f ? 1.0f : nz;
        z = kDomeVisualRadius * std::asin(clampedNz);
        if (z > kDomeVisualSkirtMaxZ) z = kDomeVisualSkirtMaxZ;
    }
    if (outX) *outX = x;
    if (outY) *outY = y;
    if (outZ) *outZ = z;
}

// Containment predicate for the Archive Shield Dome hemisphere + 2 m terrain-sealing skirt.
// Native engine Z points DOWN: upper hemisphere arches upward to floorZ - kDomeRadius (-90 m),
// with a short cylindrical skirt sealing floorZ .. floorZ + kDomeSkirtDepth (-34 m .. -32 m).
inline bool IsInsideDome(float x, float y, float z, float floorZ = kFloorZ) {
    if (!IsFinite3(x, y, z) || !std::isfinite(floorZ)) return false;
    const float dx = x - kCenterX;
    const float dy = y - kCenterY;
    const float dz = z - floorZ;
    if (dz < -kDomeRadius || dz > kDomeSkirtDepth) return false;
    const float horiz2 = dx * dx + dy * dy;
    const float r2 = kDomeRadius * kDomeRadius;
    if (dz >= 0.0f) return horiz2 <= r2;
    return (horiz2 + dz * dz) <= r2;
}

// Containment predicate for the North Sealed Cinema Pavilion outdoor-draw-skip volume.
inline bool IsInsideCinema(float x, float y, float z, float floorZ = kFloorZ) {
    if (!IsFinite3(x, y, z) || !std::isfinite(floorZ)) return false;
    return x >= kCinemaMinX && x <= kCinemaMaxX &&
           y >= kCinemaMinY && y <= kCinemaMaxY &&
           z >= (floorZ - kCinemaCeilingHeight) && z <= (floorZ + kDomeSkirtDepth);
}

// Vertical rule for the Cinema roof. A roof is not a wall: a horizontal push-out
// box would shove a landing Battle Engine off sideways. Instead the height is
// clamped: a body whose feet are inside the slab (it came down onto the roof)
// is stood on top of it, and a body under the soffit whose head has risen into
// the slab is held just below it. `ioWorldZ` is the body's origin (Z points
// down), `cofgHeight` its origin's height above its feet. `ioVz`, when given,
// loses the component that was driving it into the slab.
enum RoofClamp {
    kRoofClampNone = 0,
    kRoofClampLanded,      // feet were inside the slab: now resting on the roof
    kRoofClampUnderSoffit, // head was inside the slab: now just under the ceiling
};

inline RoofClamp ResolveCinemaRoofHeight(float x, float y, float *ioWorldZ, float floorZ,
                                         float unitHeight, float cofgHeight = 0.0f,
                                         float *ioVz = NULL) {
    if (!ioWorldZ || !IsFinite3(x, y, *ioWorldZ) || !std::isfinite(floorZ) ||
        !(unitHeight > 0.0f) || !std::isfinite(cofgHeight))
        return kRoofClampNone;
    if (x < kCinemaRoofMinX || x > kCinemaRoofMaxX || y < kCinemaRoofMinY || y > kCinemaRoofMaxY)
        return kRoofClampNone;
    const float cofg = cofgHeight > 0.0f ? cofgHeight : 0.0f;
    const float feetH = (floorZ - *ioWorldZ) - cofg;
    const float headH = feetH + unitHeight;
    const float kEps = 0.01f;
    if (feetH >= kCinemaRoofBottomH && feetH < kCinemaRoofTopH) {
        *ioWorldZ = floorZ - (kCinemaRoofTopH + kEps + cofg);
        if (ioVz && *ioVz > 0.0f) *ioVz = 0.0f;   // Z points down: positive is falling
        return kRoofClampLanded;
    }
    if (feetH < kCinemaRoofBottomH && headH > kCinemaRoofBottomH) {
        float newFeet = kCinemaRoofBottomH - unitHeight - kEps;
        if (newFeet < 0.0f) newFeet = 0.0f;
        if (newFeet < feetH) *ioWorldZ = floorZ - (newFeet + cofg);
        if (ioVz && *ioVz < 0.0f) *ioVz = 0.0f;   // negative is rising
        return kRoofClampUnderSoffit;
    }
    return kRoofClampNone;
}

// How far inside the dome the viewer stands: 0 well outside, 1 well inside,
// smooth across the shell so the dome never pops as the camera passes through it.
inline float DomeViewerInsideBlend(float eyeX, float eyeY, float eyeZ, float floorZ = kFloorZ) {
    if (!IsFinite3(eyeX, eyeY, eyeZ) || !std::isfinite(floorZ)) return 0.0f;
    const float dx = eyeX - kCenterX;
    const float dy = eyeY - kCenterY;
    float dz = eyeZ - floorZ;
    if (dz > 0.0f) dz = 0.0f;             // the skirt is a cylinder below the floor line
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float t = (kDomeRadius + 1.5f - dist) / 4.0f;   // 0 at R + 1.5, 1 at R - 2.5
    const float u = t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : t);
    return u * u * (3.0f - 2.0f * u);
}

// Brightness kept by the dome's glass and by its lattice ribs for a viewer
// inside it, by elevation (0 at the horizon and skirt, 1 at the zenith). Seen
// from within, the shell overhead all but vanishes and the ribs drop to 40%, so
// neither crosses the reticle or the HUD rings; the golden band at the horizon
// keeps its full strength, and so do impact ripples (they are not scaled here).
inline float DomeHorizonBand(float elev01) {
    const float e = elev01 <= 0.0f ? 0.0f : (elev01 >= 1.0f ? 1.0f : elev01);
    const float t = (e - 0.18f) / (0.42f - 0.18f);
    const float u = t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : t);
    return 1.0f - u * u * (3.0f - 2.0f * u);
}

inline float DomeShellGain(float elev01, float insideBlend) {
    const float b = insideBlend <= 0.0f ? 0.0f : (insideBlend >= 1.0f ? 1.0f : insideBlend);
    const float interior = 0.05f + 0.95f * DomeHorizonBand(elev01);
    return 1.0f + (interior - 1.0f) * b;
}

inline float DomeRibGain(float elev01, float insideBlend) {
    const float b = insideBlend <= 0.0f ? 0.0f : (insideBlend >= 1.0f ? 1.0f : insideBlend);
    const float interior = 0.40f + 0.60f * DomeHorizonBand(elev01);
    return 1.0f + (interior - 1.0f) * b;
}

// The Cinema disarms weapons when either the player's machine or the camera is
// inside it: a third-person camera trails the machine through the doorway, and
// the machine must not fire into the hall during the frames it has not followed.
inline bool CinemaDisarmsWeapons(bool playerInside, bool cameraInside) {
    return playerInside || cameraInside;
}

inline bool IsOnTerrace(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    return x >= kTerraceMinX && x <= kTerraceMaxX &&
           y >= kTerraceMinY && y <= kTerraceMaxY;
}

// Clips interval [*ioT0, *ioT1] against lo <= p0 + t * dp <= hi.
inline bool ClipIntervalSlab(double p0, double dp, double lo, double hi,
                             double *ioT0, double *ioT1) {
    if (!ioT0 || !ioT1 || *ioT0 > *ioT1) return false;
    if (std::fabs(dp) <= 1e-12) {
        return p0 >= lo && p0 <= hi;
    }
    double a = (lo - p0) / dp;
    double b = (hi - p0) / dp;
    if (a > b) {
        const double tmp = a;
        a = b;
        b = tmp;
    }
    if (a > *ioT0) *ioT0 = a;
    if (b < *ioT1) *ioT1 = b;
    return *ioT0 <= *ioT1;
}

// Clips interval [*ioT0, *ioT1] against a*t^2 + b*t + c <= 0 (with a >= 0).
inline bool ClipIntervalQuadLeq0(double a, double b, double c,
                                 double *ioT0, double *ioT1) {
    if (!ioT0 || !ioT1 || *ioT0 > *ioT1) return false;
    if (a <= 1e-18) {
        return c <= 0.0;
    }
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return false;
    const double root = std::sqrt(disc);
    const double inv2a = 0.5 / a;
    const double r0 = (-b - root) * inv2a;
    const double r1 = (-b + root) * inv2a;
    if (r0 > *ioT0) *ioT0 = r0;
    if (r1 < *ioT1) *ioT1 = r1;
    return *ioT0 <= *ioT1;
}

// Exact analytical entry intersection of line segment P0 -> P1 (t in [0, 1]) against
// the closed Archive Shield Dome volume (upper hemisphere union 2 m base skirt).
// Detects both landing inside the dome and high-speed pass-through tunneling across the dome.
inline bool SegmentEntryDome(float x0, float y0, float z0,
                             float x1, float y1, float z1,
                             float floorZ,
                             float *outT,
                             float *outHitX, float *outHitY, float *outHitZ) {
    if (!IsFinite3(x0, y0, z0) || !IsFinite3(x1, y1, z1) || !std::isfinite(floorZ))
        return false;

    const double ox = double(x0) - double(kCenterX);
    const double oy = double(y0) - double(kCenterY);
    const double oz = double(z0) - double(floorZ);
    const double dx = double(x1) - double(x0);
    const double dy = double(y1) - double(y0);
    const double dz = double(z1) - double(z0);
    const double r2 = double(kDomeRadius) * double(kDomeRadius);

    bool found = false;
    double bestT = 2.0;

    // 1. Upper hemisphere: ox^2 + oy^2 + oz^2 <= R^2 AND oz in [-R, 0].
    {
        double t0 = 0.0, t1 = 1.0;
        const double a = dx * dx + dy * dy + dz * dz;
        const double b = 2.0 * (ox * dx + oy * dy + oz * dz);
        const double c = (ox * ox + oy * oy + oz * oz) - r2;
        if (ClipIntervalQuadLeq0(a, b, c, &t0, &t1) &&
            ClipIntervalSlab(oz, dz, -double(kDomeRadius), 0.0, &t0, &t1)) {
            found = true;
            bestT = t0;
        }
    }

    // 2. Cylindrical base skirt: ox^2 + oy^2 <= R^2 AND oz in [0, kDomeSkirtDepth].
    {
        double t0 = 0.0, t1 = 1.0;
        const double a = dx * dx + dy * dy;
        const double b = 2.0 * (ox * dx + oy * dy);
        const double c = (ox * ox + oy * oy) - r2;
        if (ClipIntervalQuadLeq0(a, b, c, &t0, &t1) &&
            ClipIntervalSlab(oz, dz, 0.0, double(kDomeSkirtDepth), &t0, &t1)) {
            if (!found || t0 < bestT) {
                found = true;
                bestT = t0;
            }
        }
    }

    if (!found) return false;
    if (bestT < 0.0) bestT = 0.0;
    if (bestT > 1.0) bestT = 1.0;

    if (outT) *outT = static_cast<float>(bestT);
    if (outHitX) *outHitX = static_cast<float>(double(x0) + bestT * dx);
    if (outHitY) *outHitY = static_cast<float>(double(y0) + bestT * dy);
    if (outHitZ) *outHitZ = static_cast<float>(double(z0) + bestT * dz);
    return true;
}

// Ballistic / missile shield dome interception:
// - Outgoing fire starting inside the dome passes cleanly outward (returns false).
// - All incoming fire starting outside the dome that touches or crosses the dome is
//   stopped at the exact exterior entry point (returns true, writes hit point).
inline bool InterceptIncomingSegment(float prevX, float prevY, float prevZ,
                                     float curX, float curY, float curZ,
                                     float floorZ,
                                     float *outHitX, float *outHitY, float *outHitZ) {
    if (!IsFinite3(prevX, prevY, prevZ) || !IsFinite3(curX, curY, curZ) || !std::isfinite(floorZ))
        return false;
    if (IsInsideDome(prevX, prevY, prevZ, floorZ))
        return false;
    return SegmentEntryDome(prevX, prevY, prevZ, curX, curY, curZ, floorZ,
                            NULL, outHitX, outHitY, outHitZ);
}

// Hitscan beam shield dome clamping:
// - Outgoing beams starting inside the dome pass cleanly outward (returns false).
// - All incoming beams starting outside the dome that intersect the dome are shortened
//   so start + clampedVel stops just outside the shield surface (returns true, writes exact
//   dome surface impact coordinates to outHitX/Y/Z for ripple FX).
inline bool ClampIncomingBeam(float startX, float startY, float startZ,
                              float *ioVelX, float *ioVelY, float *ioVelZ,
                              float floorZ,
                              float *outHitX, float *outHitY, float *outHitZ) {
    if (!ioVelX || !ioVelY || !ioVelZ) return false;
    const float vx = *ioVelX, vy = *ioVelY, vz = *ioVelZ;
    if (!IsFinite3(startX, startY, startZ) || !IsFinite3(vx, vy, vz) || !std::isfinite(floorZ))
        return false;
    if (IsInsideDome(startX, startY, startZ, floorZ))
        return false;

    float tHit = 0.0f;
    float hx = 0.0f, hy = 0.0f, hz = 0.0f;
    if (!SegmentEntryDome(startX, startY, startZ,
                          startX + vx, startY + vy, startZ + vz,
                          floorZ, &tHit, &hx, &hy, &hz)) {
        return false;
    }

    if (outHitX) *outHitX = hx;
    if (outHitY) *outHitY = hy;
    if (outHitZ) *outHitZ = hz;

    const float segLen = std::sqrt(vx * vx + vy * vy + vz * vz);
    float tSafe = segLen > 1e-5f ? (tHit - kBeamPullback / segLen) : 0.0f;
    if (tSafe < 0.0f) tSafe = 0.0f;
    *ioVelX = vx * tSafe;
    *ioVelY = vy * tSafe;
    *ioVelZ = vz * tSafe;
    return true;
}

// -----------------------------------------------------------------------------
// Impact-Ripple FX Ring Buffer (capacity 16, 0.65 s smooth expansion & decay)
// -----------------------------------------------------------------------------
struct ImpactRipple {
    float x, y, z;
    float age;
    float intensity;
    bool active;
};

struct RippleBuffer {
    ImpactRipple slots[kMaxRipples];
    unsigned nextWrite;
    unsigned totalRecorded;
};

inline void ResetRipples(RippleBuffer *rb) {
    if (!rb) return;
    std::memset(rb, 0, sizeof(*rb));
}

inline void RecordRipple(RippleBuffer *rb, float x, float y, float z, float intensity = 1.0f) {
    if (!rb || !IsFinite3(x, y, z) || !std::isfinite(intensity)) return;
    const float clamped = intensity < 0.1f ? 0.1f : (intensity > 2.0f ? 2.0f : intensity);
    ImpactRipple &slot = rb->slots[rb->nextWrite % kMaxRipples];
    slot.x = x;
    slot.y = y;
    slot.z = z;
    slot.age = 0.0f;
    slot.intensity = clamped;
    slot.active = true;
    rb->nextWrite = (rb->nextWrite + 1u) % kMaxRipples;
    ++rb->totalRecorded;
}

inline void StepRipples(RippleBuffer *rb, float dtSeconds, bool paused) {
    if (!rb || paused || !(dtSeconds > 0.0f) || !std::isfinite(dtSeconds)) return;
    const float dt = dtSeconds > kRippleMaxDt ? kRippleMaxDt : dtSeconds;
    for (unsigned i = 0; i < kMaxRipples; ++i) {
        if (!rb->slots[i].active) continue;
        rb->slots[i].age += dt;
        if (rb->slots[i].age >= kRippleDuration) {
            rb->slots[i].active = false;
            rb->slots[i].age = kRippleDuration;
        }
    }
}

inline unsigned ActiveRippleCount(const RippleBuffer &rb) {
    unsigned n = 0;
    for (unsigned i = 0; i < kMaxRipples; ++i)
        if (rb.slots[i].active) ++n;
    return n;
}

// -----------------------------------------------------------------------------
// Archive Pedestal Coordinate Conversion (world Z-down <-> visitor-local Z-up)
// -----------------------------------------------------------------------------
inline void WorldToPedestalLocal(float worldX, float worldY, float worldZ, float floorZ,
                                 float *outLocalX, float *outLocalY, float *outLocalZ) {
    if (outLocalX) *outLocalX = worldX - kMuseumEntranceX;
    if (outLocalY) *outLocalY = worldY - kMuseumEntranceY;
    if (outLocalZ) *outLocalZ = floorZ - worldZ;
}

inline void WorldAimToPedestalLocal(float aimX, float aimY, float aimZ,
                                    float *outLocalAimX, float *outLocalAimY, float *outLocalAimZ) {
    if (outLocalAimX) *outLocalAimX = aimX;
    if (outLocalAimY) *outLocalAimY = aimY;
    if (outLocalAimZ) *outLocalAimZ = -aimZ;
}

inline float PedestalPlinthHeight(float floorZ = kFloorZ) {
    const float h = floorZ - kPedestalTopZ;
    return h > 0.1f ? h : kPedestalHeight;
}

// -----------------------------------------------------------------------------
// Interior Solid-Obstacle Walkability Collision (piers, Cinema walls, pedestal)
// -----------------------------------------------------------------------------
constexpr unsigned kMaxSolidBoxes = 64u;
constexpr float kBattleEngineCollisionRadius = 1.35f;
constexpr float kBattleEngineCollisionHeight = 3.20f;
constexpr float kCollisionPushEps = 1e-3f;

struct SolidBox {
    float minX, minY;
    float maxX, maxY;
    float minH, maxH; // height above floorZ (positive UP, i.e. floorZ - worldZ)
};

struct SolidLayout {
    SolidBox boxes[kMaxSolidBoxes];
    unsigned count;
};

inline void AddSolidBox(SolidLayout *layout,
                        float minX, float minY, float maxX, float maxY,
                        float minH, float maxH) {
    if (!layout || layout->count >= kMaxSolidBoxes) return;
    SolidBox &b = layout->boxes[layout->count++];
    b.minX = minX; b.minY = minY;
    b.maxX = maxX; b.maxY = maxY;
    b.minH = minH; b.maxH = maxH;
}

// Builds the fixed-size AABB keep-out set matching all walkable-height interior
// structures in BuildCitadelArchitecture(): central Archive Pedestal dais, rotunda
// emitter pylons, colonnade & portal piers, Forseti/Muspell memorial alcoves, twin
// reflecting pools, sealed North Cinema pavilion walls/seating/stage, and overlook
// balustrades (split cleanly around the 17 m SE access ramp and 5 m Cinema portal).
inline SolidLayout MakeInteriorSolidLayout() {
    SolidLayout out = {};

    // 1. Central Archive Pedestal stepped dais & hologram assembly.
    AddSolidBox(&out, kCenterX - 2.06f, kCenterY - 2.06f,
                      kCenterX + 2.06f, kCenterY + 2.06f, 0.0f, 4.20f);

    // 2. Four Rotunda Shield-Dome Emitter Pylons.
    const float emX[2] = {103.5f, 120.5f};
    const float emY[2] = {383.5f, 400.5f};
    for (unsigned ix = 0; ix < 2u; ++ix) {
        for (unsigned iy = 0; iy < 2u; ++iy) {
            AddSolidBox(&out, emX[ix] - 0.68f, emY[iy] - 0.68f,
                              emX[ix] + 0.68f, emY[iy] + 0.68f, 0.0f, 8.65f);
        }
    }

    // 3. Twelve Flank Colonnade Piers (West X=82, East X=142) + 2 South Portal Pylons.
    const float colY[6] = {374.0f, 382.0f, 390.0f, 398.0f, 406.0f, 414.0f};
    for (unsigned i = 0; i < 6u; ++i) {
        AddSolidBox(&out,  82.0f - 0.66f, colY[i] - 0.66f,  82.0f + 0.66f, colY[i] + 0.66f, 0.0f, 10.15f);
        AddSolidBox(&out, 142.0f - 0.66f, colY[i] - 0.66f, 142.0f + 0.66f, colY[i] + 0.66f, 0.0f, 10.15f);
    }
    AddSolidBox(&out,  95.0f - 0.74f, 375.5f - 0.74f,  95.0f + 0.74f, 375.5f + 0.74f, 0.0f, 8.60f);
    AddSolidBox(&out, 129.0f - 0.74f, 375.5f - 0.74f, 129.0f + 0.74f, 375.5f + 0.74f, 0.0f, 8.60f);

    // 4. West Forseti Memorial Alcove walls, honor plinth, and banner standards.
    AddSolidBox(&out, 82.2f, 383.6f, 84.42f, 400.4f, 0.0f, 9.15f);
    AddSolidBox(&out, 83.8f, 383.6f, 87.2f,  384.8f, 0.0f, 9.15f);
    AddSolidBox(&out, 83.8f, 399.2f, 87.2f,  400.4f, 0.0f, 9.15f);
    AddSolidBox(&out, kForsetiAlcoveX - 1.40f, kForsetiAlcoveY - 1.40f,
                      kForsetiAlcoveX + 1.40f, kForsetiAlcoveY + 1.40f, 0.0f, 1.80f);
    AddSolidBox(&out, 88.0f - 0.45f, 378.0f - 0.45f, 88.0f + 0.45f, 378.0f + 0.45f, 0.0f, 6.68f);
    AddSolidBox(&out, 88.0f - 0.45f, 405.5f - 0.45f, 88.0f + 0.45f, 405.5f + 0.45f, 0.0f, 6.68f);

    // 5. East Muspell Memorial Alcove walls, honor plinth, and banner standards.
    AddSolidBox(&out, 139.58f, 383.6f, 141.8f, 400.4f, 0.0f, 9.15f);
    AddSolidBox(&out, 136.8f,  383.6f, 140.2f, 384.8f, 0.0f, 9.15f);
    AddSolidBox(&out, 136.8f,  399.2f, 140.2f, 400.4f, 0.0f, 9.15f);
    AddSolidBox(&out, kMuspellAlcoveX - 1.40f, kMuspellAlcoveY - 1.40f,
                      kMuspellAlcoveX + 1.40f, kMuspellAlcoveY + 1.40f, 0.0f, 1.80f);
    AddSolidBox(&out, 136.0f - 0.45f, 378.0f - 0.45f, 136.0f + 0.45f, 378.0f + 0.45f, 0.0f, 6.68f);
    AddSolidBox(&out, 136.0f - 0.45f, 405.5f - 0.45f, 136.0f + 0.45f, 405.5f + 0.45f, 0.0f, 6.68f);

    // 6. Twin Reflecting Pools framing the south axial approach.
    AddSolidBox(&out,  95.95f, 364.95f, 102.05f, 377.05f, 0.0f, 1.00f);
    AddSolidBox(&out, 121.95f, 364.95f, 128.05f, 377.05f, 0.0f, 1.00f);

    // 7. North Sealed Cinema Pavilion enclosure (5 m clear South doorway [109.5..114.5],
    //    5.2 m clear doorway height), interior seating tiers, and proscenium stage.
    AddSolidBox(&out,  98.0f,  409.45f, 109.5f,  410.90f, 0.0f, 10.65f); // South-West wall & portal pier
    AddSolidBox(&out, 114.5f,  409.45f, 126.0f,  410.90f, 0.0f, 10.65f); // South-East wall & portal pier
    AddSolidBox(&out, 109.5f,  409.20f, 114.5f,  410.90f, 5.20f, 10.65f); // South doorway lintel
    AddSolidBox(&out,  98.0f,  410.55f,  98.85f, 428.0f,  0.0f, 10.65f); // West enclosure wall
    AddSolidBox(&out, 125.15f, 410.55f, 126.0f,  428.0f,  0.0f, 10.65f); // East enclosure wall
    AddSolidBox(&out,  98.0f,  427.15f, 126.0f,  428.0f,  0.0f, 10.65f); // North screen wall
    AddSolidBox(&out,  98.85f, 409.45f, 108.6f,  421.0f,  0.0f,  2.10f); // West seating tiers & rear alcove
    AddSolidBox(&out, 115.4f,  409.45f, 125.15f, 421.0f,  0.0f,  2.10f); // East seating tiers & rear alcove
    AddSolidBox(&out, 100.48f, 424.5f,  123.52f, 427.15f, 0.0f,  8.15f); // Proscenium stage & screen frame

    // 8. Perimeter Overlook Balustrades (split cleanly around SE access ramp [119.5..136.5]).
    AddSolidBox(&out,  74.6f, 354.1f, 119.5f, 354.9f, 0.0f, 1.66f);
    AddSolidBox(&out, 136.5f, 354.1f, 149.4f, 354.9f, 0.0f, 1.66f);
    AddSolidBox(&out,  74.6f, 354.5f,  75.4f, 428.0f, 0.0f, 1.66f);
    AddSolidBox(&out, 148.6f, 354.5f, 149.4f, 428.0f, 0.0f, 1.66f);

    // 9. Forseti signal mast plinth and shaft on the south-east bastion corner.
    AddSolidBox(&out, kSignalMastX - kSignalMastBaseHalf, kSignalMastY - kSignalMastBaseHalf,
                      kSignalMastX + kSignalMastBaseHalf, kSignalMastY + kSignalMastBaseHalf, 0.0f, 1.02f);
    AddSolidBox(&out, kSignalMastX - 0.80f, kSignalMastY - 0.80f,
                      kSignalMastX + 0.80f, kSignalMastY + 0.80f, 1.02f, kSignalMastHeight);

    return out;
}

inline bool IsInteriorPointClear(const SolidLayout &layout,
                                 float x, float y, float worldZ, float floorZ,
                                 float radius, float unitHeight = kBattleEngineCollisionHeight,
                                 float cofgHeight = 0.0f) {
    if (!IsFinite3(x, y, worldZ) || !std::isfinite(floorZ) ||
        !(radius > 0.0f) || !(unitHeight > 0.0f))
        return true;
    const float feetH = (floorZ - worldZ) - cofgHeight;
    const float headH = feetH + unitHeight;
    const float r2 = radius * radius;
    for (unsigned i = 0; i < layout.count; ++i) {
        const SolidBox &b = layout.boxes[i];
        if (!(headH > b.minH && feetH < b.maxH)) continue;
        const float qx = x < b.minX ? b.minX : (x > b.maxX ? b.maxX : x);
        const float qy = y < b.minY ? b.minY : (y > b.maxY ? b.maxY : y);
        const float dx = x - qx;
        const float dy = y - qy;
        if (dx * dx + dy * dy < r2)
            return false;
    }
    return true;
}

// Minimum-penetration circle-vs-AABB push-out against all vertically overlapping
// interior structures. Returns true if (*ioX, *ioY) was adjusted; optionally cancels
// the inward velocity component in (*ioVx, *ioVy) for smooth sliding along walls.
inline bool ResolveInteriorCollision(const SolidLayout &layout,
                                     float *ioX, float *ioY, float worldZ, float floorZ,
                                     float radius, float unitHeight = kBattleEngineCollisionHeight,
                                     float cofgHeight = 0.0f,
                                     float *ioVx = NULL, float *ioVy = NULL) {
    if (!ioX || !ioY || !IsFinite3(*ioX, *ioY, worldZ) || !std::isfinite(floorZ) ||
        !(radius > 0.0f) || !(unitHeight > 0.0f))
        return false;
    const float feetH = (floorZ - worldZ) - cofgHeight;
    const float headH = feetH + unitHeight;
    const float r2 = radius * radius;
    float x = *ioX;
    float y = *ioY;
    float vx = ioVx ? *ioVx : 0.0f;
    float vy = ioVy ? *ioVy : 0.0f;
    bool pushed = false;

    for (unsigned iter = 0; iter < 3u; ++iter) {
        bool any = false;
        for (unsigned i = 0; i < layout.count; ++i) {
            const SolidBox &b = layout.boxes[i];
            if (!(headH > b.minH && feetH < b.maxH)) continue;
            const float qx = x < b.minX ? b.minX : (x > b.maxX ? b.maxX : x);
            const float qy = y < b.minY ? b.minY : (y > b.maxY ? b.maxY : y);
            const float dx = x - qx;
            const float dy = y - qy;
            const float d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;

            float nx = 0.0f, ny = 0.0f, pen = 0.0f;
            if (d2 > 1e-8f) {
                const float d = std::sqrt(d2);
                nx = dx / d;
                ny = dy / d;
                pen = radius - d;
            } else {
                const float dLeft  = x - b.minX;
                const float dRight = b.maxX - x;
                const float dSouth = y - b.minY;
                const float dNorth = b.maxY - y;
                float best = dLeft;
                nx = -1.0f; ny = 0.0f;
                if (dRight < best) { best = dRight; nx =  1.0f; ny =  0.0f; }
                if (dSouth < best) { best = dSouth; nx =  0.0f; ny = -1.0f; }
                if (dNorth < best) { best = dNorth; nx =  0.0f; ny =  1.0f; }
                pen = best + radius;
            }

            x += nx * (pen + kCollisionPushEps);
            y += ny * (pen + kCollisionPushEps);
            if (ioVx && ioVy) {
                const float vn = vx * nx + vy * ny;
                if (vn < 0.0f) {
                    vx -= vn * nx;
                    vy -= vn * ny;
                }
            }
            pushed = true;
            any = true;
        }
        if (!any) break;
    }

    if (pushed) {
        *ioX = x;
        *ioY = y;
        if (ioVx) *ioVx = vx;
        if (ioVy) *ioVy = vy;
    }
    return pushed;
}

// -----------------------------------------------------------------------------
// Exported Runtime Seam
// -----------------------------------------------------------------------------
void RuntimeInit(float floorZ);
void RuntimeShutdown();
void RuntimeUpdate(float dtSeconds, bool paused,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ);
// Simulation step (the game's tick, never the renderer): interior walkability
// push-out, the Cinema roof height rule and the player's Cinema occupancy.
void RuntimeTickSim();
void RuntimeDrawWorld();
void RuntimeDrawDomeFx();
bool RuntimeHandleKey(unsigned dik, bool repeat);
bool RuntimeHandleFire(bool release, float value);
bool RuntimeIsInsideDome(float x, float y, float z);
bool RuntimeIsInsideCinema(float x, float y, float z);
bool RuntimeCue(float eyeX, float eyeY, char *title, unsigned titleCap,
                char *line, unsigned lineCap, char *note, unsigned noteCap);

} // namespace CeSanctuaryCitadel

extern "C" {
bool PortCeSanctuaryActive(void);
bool PortCeSanctuarySkipOutdoorWorld(void);
bool PortCeSanctuaryInterceptRound(float prevX, float prevY, float prevZ,
                                   float curX, float curY, float curZ,
                                   float *outHitX, float *outHitY, float *outHitZ);
bool PortCeSanctuaryClampBeam(float startX, float startY, float startZ,
                              float *ioVelX, float *ioVelY, float *ioVelZ);
void PortCeSanctuaryDrawWorld(void);
bool PortCeSanctuaryFire(bool release, float value);
void PortCeSanctuaryRenderCinemaPlayer(int viewpoint);
}

#endif
