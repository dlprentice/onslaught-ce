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
// Exported Runtime Seam
// -----------------------------------------------------------------------------
void RuntimeInit(float floorZ);
void RuntimeShutdown();
void RuntimeUpdate(float dtSeconds, bool paused,
                   float eyeX, float eyeY, float eyeZ,
                   float aimX, float aimY, float aimZ);
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
