#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host tests, property sweeps, causal mutants, and patch independence checks for the
Sanctuary Museum Citadel, Archive Shield Dome, Sealed Cinema Pavilion, and Archive
Pedestal bridge (port/game/ce_sanctuary_citadel.{h,cpp}, patch 0379)."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
GAME = PORT / "game"
CITADEL_H = GAME / "ce_sanctuary_citadel.h"
CITADEL_CPP = GAME / "ce_sanctuary_citadel.cpp"
PATCH = PORT / "patches/0379-ce-sanctuary-shield.patch"

DRIVER = r"""
#include "ce_sanctuary_citadel.h"
#include "ce_archive_pedestal.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace CeSanctuaryCitadel;

static void Need(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(2);
    }
}

static bool Near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

static float DistToCenter3D(float x, float y, float z, float fz = kFloorZ) {
    const float dx = x - kCenterX;
    const float dy = y - kCenterY;
    const float dz = z - fz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static uint32_t NextRng(uint32_t &s) {
    s = s * 1664525u + 1013904223u;
    return s;
}

static float RandUnit(uint32_t &s) {
    return float(NextRng(s) >> 8) * (1.0f / 16777216.0f);
}

static float RandRange(uint32_t &s, float lo, float hi) {
    return lo + (hi - lo) * RandUnit(s);
}

int main() {
    // 1. Exact shared coordinate constants contract.
    Need(Near(kCenterX, 112.0f) && Near(kCenterY, 392.0f) && Near(kFloorZ, -34.0f),
         "plateau center and floor elevation match contract");
    Need(Near(kDomeRadius, 56.0f), "shield dome radius is 56 m");
    Need(Near(kPedestalX, 112.0f) && Near(kPedestalY, 392.0f) && Near(kPedestalTopZ, -35.05f),
         "pedestal world pose matches contract");
    Need(Near(kCinemaMinX, 98.5f) && Near(kCinemaMaxX, 125.5f) &&
         Near(kCinemaMinY, 411.5f) && Near(kCinemaMaxY, 427.5f),
         "sealed cinema bounds match contract");

    // 2. Dome containment across upper hemisphere, apex, skirt, and exterior (Z points DOWN).
    Need(IsInsideDome(112.0f, 392.0f, -34.0f), "center on floor is inside dome");
    Need(IsInsideDome(112.0f, 392.0f, -89.9f), "just below apex is inside dome");
    Need(IsInsideDome(112.0f, 392.0f, -90.0f), "exact apex Z=-90 is on dome shell");
    Need(!IsInsideDome(112.0f, 392.0f, -90.2f), "above apex Z=-90.2 is outside dome");
    Need(IsInsideDome(112.0f + 55.0f, 392.0f, -33.0f), "inside base skirt is inside dome");
    Need(IsInsideDome(112.0f + 55.9f, 392.0f, -9.5f), "bottom edge of 24.5 m skirt Z=-9.5 is inside dome");
    Need(IsInsideDome(132.8f, 341.2f, -30.54f), "SE Access Ramp inside R=56 is protected by 24.5 m dome skirt");
    Need(!IsInsideDome(112.0f, 392.0f, -9.3f), "below 24.5 m skirt Z=-9.3 is outside dome");
    Need(!IsInsideDome(112.0f + 56.5f, 392.0f, -34.0f), "outside radius on floor is outside dome");
    Need(!IsInsideDome(std::nanf(""), 392.0f, -34.0f), "NaN coordinate rejected");

    // 3. Ballistic & missile interception: zero penetration, exact surface hit, outgoing fire passes.
    {
        float hx = 0.0f, hy = 0.0f, hz = 0.0f;
        // Cardinal horizontal shot from East (+X) aimed at center.
        Need(InterceptIncomingSegment(212.0f, 392.0f, -34.0f, 112.0f, 392.0f, -34.0f, kFloorZ, &hx, &hy, &hz),
             "east incoming round intercepted");
        Need(Near(hx, 168.0f, 1e-2f) && Near(hy, 392.0f, 1e-2f) && Near(hz, -34.0f, 1e-2f),
             "east incoming round stops on dome outer shell");

        // High-altitude vertical drop straight onto apex (Z = -180 -> -34).
        Need(InterceptIncomingSegment(112.0f, 392.0f, -180.0f, 112.0f, 392.0f, -34.0f, kFloorZ, &hx, &hy, &hz),
             "vertical airstrike round intercepted at apex");
        Need(Near(hx, 112.0f, 1e-2f) && Near(hy, 392.0f, 1e-2f) && Near(hz, -90.0f, 1e-2f),
             "vertical airstrike stops at apex Z=-90");

        // High-speed single-tick pass-through tunneling shot (starts West outside, ends East outside!).
        Need(InterceptIncomingSegment(-20.0f, 392.0f, -50.0f, 260.0f, 392.0f, -50.0f, kFloorZ, &hx, &hy, &hz),
             "fast pass-through shell cannot tunnel through dome");
        Need(hx < kCenterX && Near(DistToCenter3D(hx, hy, hz), kDomeRadius, 2e-2f),
             "pass-through shell stops on near (entry) face of dome, not exit face");

        // Low grazing shot into 24.5 m cylindrical base skirt (Z = -32.8) and down onto SE Access Ramp (Z = -30.54).
        Need(InterceptIncomingSegment(190.0f, 392.0f, -32.8f, 112.0f, 392.0f, -32.8f, kFloorZ, &hx, &hy, &hz),
             "low skirt grazing round intercepted");
        Need(Near(hx, 168.0f, 1e-2f) && Near(hz, -32.8f, 1e-2f),
             "low skirt round stops at R=56 cylinder wall");
        Need(InterceptIncomingSegment(180.0f, 300.0f, -25.0f, 132.8f, 341.2f, -30.54f, kFloorZ, &hx, &hy, &hz),
             "valley shot aimed at SE Access Ramp intercepted by 24.5 m skirt");
        Need(Near(std::hypot(hx - kCenterX, hy - kCenterY), kDomeRadius, 2e-2f),
             "SE Access Ramp shot stops on outer R=56 dome skirt");

        // Upward shot from cliff below 24.5 m skirt aiming up through floor into Sanctuary.
        Need(InterceptIncomingSegment(112.0f, 392.0f, -5.0f, 112.0f, 392.0f, -45.0f, kFloorZ, &hx, &hy, &hz),
             "upward shot from beneath 24.5 m skirt intercepted at skirt floor");
        Need(Near(hz, -9.5f, 1e-2f), "upward shot stops at Z=-9.5 skirt seal");

        // Clear exterior miss in the valley battle.
        Need(!InterceptIncomingSegment(250.0f, 200.0f, -20.0f, 310.0f, 180.0f, -18.0f, kFloorZ, &hx, &hy, &hz),
             "unrelated valley battlefield round passes freely");
        // Near-miss grazing outside R = 56.5 m.
        Need(!InterceptIncomingSegment(112.0f + 56.5f, 300.0f, -34.0f, 112.0f + 56.5f, 480.0f, -34.0f, kFloorZ, &hx, &hy, &hz),
             "exterior near-miss passes freely");

        // Outgoing fire originating inside Sanctuary passes cleanly outward!
        Need(!InterceptIncomingSegment(112.0f, 366.0f, -36.0f, 260.0f, 210.0f, -15.0f, kFloorZ, &hx, &hy, &hz),
             "player outgoing fire from overlook pad passes cleanly outward");
        Need(!InterceptIncomingSegment(112.0f, 392.0f, -36.0f, 112.0f, 392.0f, -130.0f, kFloorZ, &hx, &hy, &hz),
             "player outgoing AA fire straight through dome apex passes cleanly");
    }

    // 4. Randomized property sweep: 2,000 incoming ballistic shots -> ZERO penetrations!
    {
        uint32_t rng = 0x5A4C7019u;
        for (unsigned i = 0; i < 2000u; ++i) {
            const float az = RandRange(rng, 0.0f, 6.2831853f);
            const float el = RandRange(rng, -0.05f, 1.48f);
            const float rOut = RandRange(rng, kDomeRadius + 1.0f, 240.0f);
            const float px = kCenterX + std::cos(el) * std::cos(az) * rOut;
            const float py = kCenterY + std::cos(el) * std::sin(az) * rOut;
            const float pz = kFloorZ - std::sin(el) * rOut;
            if (IsInsideDome(px, py, pz)) continue;

            // Aim either into interior (guaranteed inside R=56 hemisphere: 30*sqrt(3)=51.96 < 56) OR across to far exterior.
            const float tx = kCenterX + RandRange(rng, -30.0f, 30.0f);
            const float ty = kCenterY + RandRange(rng, -30.0f, 30.0f);
            const float tz = kFloorZ - RandRange(rng, 0.0f, 30.0f);
            Need(IsInsideDome(tx, ty, tz), "fuzz target point is strictly inside dome");
            const float mult = (i & 1u) ? 1.0f : 3.2f; // half land inside, half tunnel all the way across!
            const float cx = px + (tx - px) * mult;
            const float cy = py + (ty - py) * mult;
            const float cz = pz + (tz - pz) * mult;

            float hx = 0.0f, hy = 0.0f, hz = 0.0f;
            const bool hit = InterceptIncomingSegment(px, py, pz, cx, cy, cz, kFloorZ, &hx, &hy, &hz);
            Need(hit, "randomized incoming ballistic round must be intercepted");
            Need(IsFinite3(hx, hy, hz), "intercept hit point is finite");
            // Verify hit point is on the dome boundary (not strictly deep inside).
            const float d3 = DistToCenter3D(hx, hy, hz);
            const float dh = std::hypot(hx - kCenterX, hy - kCenterY);
            const bool onHemi = (hz <= kFloorZ + 0.05f && Near(d3, kDomeRadius, 0.15f));
            const bool onSkirtWall = (hz >= kFloorZ - 0.05f && hz <= kFloorZ + kDomeSkirtDepth + 0.05f && Near(dh, kDomeRadius, 0.15f));
            const bool onSkirtBot = (Near(hz, kFloorZ + kDomeSkirtDepth, 0.05f) && dh <= kDomeRadius + 0.15f);
            Need(onHemi || onSkirtWall || onSkirtBot, "ballistic hit lies on dome exterior shell");
        }
    }

    // 5. Hitscan laser/beam clamping: zero penetration, exact pullback outside dome, outgoing beams pass.
    {
        float vx = -220.0f, vy = 0.0f, vz = 0.0f;
        float hx = 0.0f, hy = 0.0f, hz = 0.0f;
        Need(ClampIncomingBeam(212.0f, 392.0f, -40.0f, &vx, &vy, &vz, kFloorZ, &hx, &hy, &hz),
             "incoming hitscan beam clamped by dome");
        Need(Near(DistToCenter3D(hx, hy, hz), kDomeRadius, 2e-2f), "beam ripple lands on dome surface");
        const float endX = 212.0f + vx, endY = 392.0f + vy, endZ = -40.0f + vz;
        Need(!IsInsideDome(endX, endY, endZ), "clamped beam endpoint is strictly outside dome");
        Need(vx < 0.0f && endX > hx && Near(endX - hx, kBeamPullback, 1e-2f),
             "beam pulled back cleanly by kBeamPullback along incoming ray");

        // Close-range beam starting 0.01 m outside dome firing inward.
        float cvx = -50.0f, cvy = 0.0f, cvz = 0.0f;
        Need(ClampIncomingBeam(168.01f, 392.0f, -34.0f, &cvx, &cvy, &cvz, kFloorZ, &hx, &hy, &hz),
             "point-blank exterior beam clamped");
        Need(!IsInsideDome(168.01f + cvx, 392.0f + cvy, -34.0f + cvz),
             "point-blank clamped beam stays strictly outside dome");

        // Outgoing player laser beam from inside Sanctuary passes untouched.
        float ovx = 180.0f, ovy = -120.0f, ovz = 25.0f;
        Need(!ClampIncomingBeam(112.0f, 366.0f, -36.0f, &ovx, &ovy, &ovz, kFloorZ, &hx, &hy, &hz),
             "outgoing beam from inside dome passes untouched");
        Need(Near(ovx, 180.0f) && Near(ovy, -120.0f) && Near(ovz, 25.0f),
             "outgoing beam velocity preserved");
    }

    // 6. Randomized property sweep: 1,500 incoming hitscan beams -> ZERO penetrations!
    {
        uint32_t rng = 0xC0DEC0DEu;
        for (unsigned i = 0; i < 1500u; ++i) {
            const float az = RandRange(rng, 0.0f, 6.2831853f);
            const float el = RandRange(rng, 0.0f, 1.50f);
            const float rOut = RandRange(rng, kDomeRadius + 0.25f, 220.0f);
            const float sx = kCenterX + std::cos(el) * std::cos(az) * rOut;
            const float sy = kCenterY + std::cos(el) * std::sin(az) * rOut;
            const float sz = kFloorZ - std::sin(el) * rOut;
            if (IsInsideDome(sx, sy, sz)) continue;

            const float tx = kCenterX + RandRange(rng, -30.0f, 30.0f);
            const float ty = kCenterY + RandRange(rng, -30.0f, 30.0f);
            const float tz = kFloorZ - RandRange(rng, 0.0f, 30.0f);
            float vx = (tx - sx) * 2.5f;
            float vy = (ty - sy) * 2.5f;
            float vz = (tz - sz) * 2.5f;
            float hx = 0.0f, hy = 0.0f, hz = 0.0f;
            Need(ClampIncomingBeam(sx, sy, sz, &vx, &vy, &vz, kFloorZ, &hx, &hy, &hz),
                 "randomized incoming beam must be clamped");
            // Sample 8 points along clamped segment [0..1]: NONE may lie inside dome!
            for (unsigned s = 0; s <= 8u; ++s) {
                const float f = float(s) / 8.0f;
                Need(!IsInsideDome(sx + vx * f, sy + vy * f, sz + vz * f),
                     "clamped beam segment never penetrates dome");
            }
        }
    }

    // 7. Impact-ripple ring buffer (capacity 16, wrap, pause freeze, 0.65 s decay).
    {
        RippleBuffer rb = {};
        ResetRipples(&rb);
        Need(ActiveRippleCount(rb) == 0u && rb.totalRecorded == 0u, "fresh ripple buffer empty");
        for (unsigned i = 0; i < 20u; ++i) {
            RecordRipple(&rb, 168.0f, 392.0f, -34.0f - float(i) * 0.5f, 1.0f);
        }
        Need(ActiveRippleCount(rb) == kMaxRipples, "ring buffer saturates at 16 active ripples");
        Need(rb.totalRecorded == 20u && rb.nextWrite == 4u, "ring buffer wraps write cursor cleanly");

        // Paused ticks do not advance ripple age.
        StepRipples(&rb, 0.50f, true);
        Need(ActiveRippleCount(rb) == kMaxRipples && Near(rb.slots[0].age, 0.0f),
             "paused simulation holds ripple age steady");

        // Large single-frame dt clamped to 0.25 s.
        StepRipples(&rb, 5.0f, false);
        Need(Near(rb.slots[0].age, kRippleMaxDt) && ActiveRippleCount(rb) == kMaxRipples,
             "large dt clamped to 0.25 s");

        // Advance past 0.65 s -> all ripples expire cleanly.
        StepRipples(&rb, 0.25f, false);
        Need(ActiveRippleCount(rb) == kMaxRipples, "ripples still active at 0.50 s");
        StepRipples(&rb, 0.20f, false);
        Need(ActiveRippleCount(rb) == 0u, "all ripples expire cleanly at 0.65 s");
    }

    // 8. Sealed Cinema Pavilion outdoor-draw-skip bounds.
    {
        Need(IsInsideCinema(112.0f, 418.0f, -36.0f), "cinema center is inside skip volume");
        Need(IsInsideCinema(98.5f, 411.5f, -34.0f), "cinema min corner is inside");
        Need(IsInsideCinema(125.5f, 427.5f, -44.0f), "cinema max corner / ceiling is inside");
        Need(IsInsideCinema(112.0f, 420.0f, -32.0f), "cinema floor skirt bound Z=-32 is inside");
        Need(!IsInsideCinema(98.4f, 418.0f, -36.0f), "outside west cinema wall returns false");
        Need(!IsInsideCinema(125.6f, 418.0f, -36.0f), "outside east cinema wall returns false");
        Need(!IsInsideCinema(112.0f, 411.4f, -36.0f), "south doorway threshold returns false");
        Need(!IsInsideCinema(112.0f, 427.6f, -36.0f), "behind north screen wall returns false");
        Need(!IsInsideCinema(112.0f, 418.0f, -44.2f), "above cinema roof returns false");
        Need(!IsInsideCinema(112.0f, 418.0f, -31.8f), "below cinema floor returns false");
        Need(!IsInsideCinema(112.0f, 366.0f, -36.0f), "overlook launch pad does NOT skip outdoor world");
        Need(!IsInsideCinema(112.0f, 392.0f, -36.0f), "rotunda pedestal does NOT skip outdoor world");
    }

    // 9. Archive Pedestal pose wiring & world-to-local ray-picking roundtrip!
    {
        CeArchivePedestal::BasePose pose = {};
        pose.localX = kPedestalX - kMuseumEntranceX;
        pose.localY = kPedestalY - kMuseumEntranceY;
        pose.plinthTopZ = PedestalPlinthHeight(kFloorZ);
        pose.plinthHalf = CeArchivePedestal::kPedestalPlinthHalf;
        pose.customWorldPose = true;

        Need(Near(pose.plinthTopZ, 1.05f), "pedestal plinth height is exactly 1.05 m");
        Need(Near(kFloorZ - pose.plinthTopZ, kPedestalTopZ), "pedestal topZ sits flush at -35.05 m");

        // Visitor standing 2.0 m south of the brass control rail in world coordinates (Z down!).
        const float railWorldY = kPedestalY + CeArchivePedestal::kControlOffsetY;
        const float btnWorldZ = kFloorZ - CeArchivePedestal::kControlLocalZ;
        for (unsigned b = 0; b < static_cast<unsigned>(CeArchivePedestal::kBtnCount); ++b) {
            const float bx = kPedestalX + CeArchivePedestal::kButtons[b].offsetX;
            const float eyeX = bx;
            const float eyeY = railWorldY - 2.0f;
            const float eyeZ = kFloorZ - 1.65f; // 1.65 m eye height above floorZ = -34.0
            const float aimX = bx - eyeX;
            const float aimY = railWorldY - eyeY;
            const float aimZ = btnWorldZ - eyeZ; // positive in native Z-down (looking slightly down!)

            float lEx, lEy, lEz, lAx, lAy, lAz;
            WorldToPedestalLocal(eyeX, eyeY, eyeZ, kFloorZ, &lEx, &lEy, &lEz);
            WorldAimToPedestalLocal(aimX, aimY, aimZ, &lAx, &lAy, &lAz);

            Need(CeArchivePedestal::IsVisitorNearPedestal(pose, lEx, lEy),
                 "visitor in front of Sanctuary pedestal is within keyboard range");
            Need(CeArchivePedestal::PickButton(pose, lEx, lEy, lEz, lAx, lAy, lAz) == int(b),
                 "world Z-down aim picks exact brass button on Archive Pedestal");
        }
        // Visitor far away in the valley is outside pedestal keyboard range.
        float farLx, farLy, farLz;
        WorldToPedestalLocal(280.0f, 180.0f, -12.0f, kFloorZ, &farLx, &farLy, &farLz);
        Need(!CeArchivePedestal::IsVisitorNearPedestal(pose, farLx, farLy),
             "valley player is outside Archive Pedestal keyboard range");
    }

    // 10. 360-degree visual dome ground-seal property sweep (720 rays x 0.5 deg), bastion
    //     cornice clearance, C^1 cylindrical projection, and DomePerimeterGroundZ bounds.
    {
        // Visual radius clears all 4 brass bastion cornice corners (hypot(39.7, 39.7) = 56.144 m)
        // while physical kDomeRadius (56.0 m) keeps its contract and kDomeSkirtDepth (24.5 m) matches visual skirt.
        const float bastionCornerR = std::hypot(39.7f, 39.7f);
        Need(kDomeVisualRadius > 56.14f && (kDomeVisualRadius - bastionCornerR) >= 0.20f,
             "visual dome radius clears all 4 retaining bastion/brass cornice corners by >= 20 cm");
        Need(Near(kDomeRadius, 56.0f) && Near(kDomeSkirtDepth, 24.5f),
             "physical dome radius stays 56 m and skirt matches 24.5 m visual skirt");
        Need(kDomeVisualSkirtMaxZ >= 24.0f, "visual skirt reaches >= 24 m below floorZ");

        // C^1 continuity and cylindrical arc-length preservation across equator nz = 0.
        {
            float hx0 = 0.0f, hy0 = 0.0f, hz0 = 0.0f;
            float hx1 = 0.0f, hy1 = 0.0f, hz1 = 0.0f;
            const float eps = 1e-3f;
            const float cxEps = std::sqrt(1.0f - eps * eps);
            ProjectDomeVisualPoint(cxEps, 0.0f, -eps, &hx0, &hy0, &hz0);
            ProjectDomeVisualPoint(cxEps, 0.0f,  eps, &hx1, &hy1, &hz1);
            Need(Near(hx0, kDomeVisualRadius, 1e-3f) && Near(hx1, kDomeVisualRadius, 1e-3f),
                 "C^1 dome projection radius is continuous across equator");
            Need(Near(hz1 - hz0, 2.0f * kDomeVisualRadius * eps, 2e-3f),
                 "C^1 dome projection preserves vertical arc-length slope across equator");

            // Deep skirt vertex stays strictly cylindrical at radius kDomeVisualRadius (never curves inward!).
            float sx = 0.0f, sy = 0.0f, sz = 0.0f;
            const float nzSkirt = 0.35f;
            const float hrUnit = std::sqrt(1.0f - nzSkirt * nzSkirt);
            ProjectDomeVisualPoint(hrUnit * 0.6f, hrUnit * 0.8f, nzSkirt, &sx, &sy, &sz);
            Need(Near(std::hypot(sx, sy), kDomeVisualRadius, 1e-3f),
                 "cylindrical skirt projection maintains full kDomeVisualRadius without curving inward");
            Need(Near(sz, kDomeVisualRadius * std::asin(nzSkirt), 1e-3f),
                 "cylindrical skirt Z matches arc-length-preserving R * asin(nz)");
        }

        // 360 deg x 0.5 deg (720-ray) terrain-seal sweep around (112, 392) at R = kDomeVisualRadius.
        float maxGroundZ = -1e9f;
        float minGroundZ =  1e9f;
        for (unsigned i = 0; i < 720u; ++i) {
            const float a = static_cast<float>(i) * (6.283185307f / 720.0f);
            const float wx = kCenterX + std::cos(a) * kDomeVisualRadius;
            const float wy = kCenterY + std::sin(a) * kDomeVisualRadius;
            const float rawGz = CeSandboxWorld::HeightAt(wx, wy);
            const float gz = DomePerimeterGroundZ(a, kFloorZ);

            Need(std::isfinite(gz), "DomePerimeterGroundZ returns finite height");
            Need(Near(gz, rawGz, 1e-3f), "DomePerimeterGroundZ matches CeSandboxWorld::HeightAt at kDomeVisualRadius");
            Need(gz >= kFloorZ - 8.0f && gz <= kFloorZ + kDomeVisualSkirtMaxZ - 2.0f,
                 "DomePerimeterGroundZ stays strictly inside documented bounds");
            if (rawGz > maxGroundZ) maxGroundZ = rawGz;
            if (rawGz < minGroundZ) minGroundZ = rawGz;

            // Project full skirt depth (nz = kDomeVisualSkirtNzMax) and verify >= 4.0 m underground everywhere!
            float px = 0.0f, py = 0.0f, pz = 0.0f;
            const float hUnit = std::sqrt(1.0f - kDomeVisualSkirtNzMax * kDomeVisualSkirtNzMax);
            ProjectDomeVisualPoint(std::cos(a) * hUnit, std::sin(a) * hUnit, kDomeVisualSkirtNzMax, &px, &py, &pz);
            Need(Near(std::hypot(px, py), kDomeVisualRadius, 1e-3f),
                 "720-ray skirt perimeter stays on kDomeVisualRadius cylinder");
            const float skirtWorldZ = kFloorZ + pz;
            Need((skirtWorldZ - rawGz) >= 4.0f,
                 "visual dome skirt extends >= 4.0 m underground below HeightAt at every 0.5 deg azimuth");
        }
        // Verify we actually traversed both the raised parapet berm and deep hillside drop!
        Need(minGroundZ <= -37.5f && maxGroundZ >= -19.5f,
             "720-ray sweep exercises both raised parapet berm and deep cliff hillside");
        // NaN azimuth guard & custom floorZ shift.
        Need(Near(DomePerimeterGroundZ(std::nanf(""), kFloorZ), kFloorZ),
             "DomePerimeterGroundZ rejects NaN azimuth safely");
        Need(Near(DomePerimeterGroundZ(0.0f, kFloorZ - 2.0f), DomePerimeterGroundZ(0.0f, kFloorZ) - 2.0f, 1e-3f),
             "DomePerimeterGroundZ shifts cleanly with custom floorZ");
    }

    // 11. Interior SolidBox walkability collision (piers, Cinema walls, Archive Pedestal,
    //     alcoves, pools, balustrades) with doorway/ramp/pad clearance & Jet flyover.
    {
        const SolidLayout solids = MakeInteriorSolidLayout();
        Need(solids.count >= 40u && solids.count <= kMaxSolidBoxes,
             "interior solid layout populates all citadel structures within fixed capacity");

        const float walkZ = kFloorZ - 1.9f; // ground Walker with COfGHeight = 1.9 m
        const float r = kBattleEngineCollisionRadius;

        // Key walkable zones remain 100% clear for Battle Engine walking.
        Need(IsInteriorPointClear(solids, kCenterX, 366.0f, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
             "South Overlook Launch Pad is clear");
        Need(IsInteriorPointClear(solids, 128.0f, 354.5f, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
             "SE Grand Access Ramp opening is clear");
        Need(IsInteriorPointClear(solids, kCenterX, 410.0f, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
             "North Cinema 5 m South doorway is clear for Walker entry");
        Need(IsInteriorPointClear(solids, kCenterX, 418.0f, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
             "North Cinema center aisle is clear");
        Need(IsInteriorPointClear(solids, kCenterX, 388.2f, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
             "visitor stance in front of Archive Pedestal control rail is clear");

        // Walking into Archive Pedestal dais pushes Battle Engine smoothly south & cancels inward vy.
        {
            float x = kCenterX, y = 389.4f, vx = 2.0f, vy = 5.0f;
            Need(ResolveInteriorCollision(solids, &x, &y, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f, &vx, &vy),
                 "pedestal dais blocks Battle Engine clipping");
            Need(y <= 389.94f - r + 1e-3f && Near(vx, 2.0f) && Near(vy, 0.0f),
                 "pedestal push-out stops cleanly outside dais while preserving lateral slide");
            Need(IsInteriorPointClear(solids, x, y, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "resolved pedestal position is strictly clear");
        }

        // Walking into Cinema South-West & East enclosure walls pushes out cleanly.
        {
            float x = 104.0f, y = 410.2f, vx = 0.0f, vy = 4.0f;
            Need(ResolveInteriorCollision(solids, &x, &y, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f, &vx, &vy),
                 "Cinema South-West wall blocks exterior pass-through");
            Need(IsInteriorPointClear(solids, x, y, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "resolved Cinema South-West wall position is strictly clear");

            float ex = 125.5f, ey = 418.0f, evx = 4.0f, evy = 1.0f;
            Need(ResolveInteriorCollision(solids, &ex, &ey, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f, &evx, &evy),
                 "Cinema East enclosure wall blocks clipping");
            Need(IsInteriorPointClear(solids, ex, ey, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "resolved Cinema East wall position is strictly clear");
        }

        // Walking into a Rotunda Emitter Pylon and a Flank Colonnade Pier pushes out cleanly.
        {
            float px = 103.5f, py = 383.5f;
            Need(ResolveInteriorCollision(solids, &px, &py, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "Rotunda Emitter Pylon blocks clipping");
            Need(IsInteriorPointClear(solids, px, py, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "resolved Rotunda Emitter Pylon position is clear");

            float cx = 82.0f, cy = 398.0f;
            Need(ResolveInteriorCollision(solids, &cx, &cy, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "Flank Colonnade Pier blocks clipping");
            Need(IsInteriorPointClear(solids, cx, cy, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "resolved Flank Colonnade Pier position is clear");
        }

        // Jet mode flying 6 m above terrace clears low pedestal/pools/balustrades but still respects 9.6 m Cinema walls!
        {
            const float midJetZ = kFloorZ - 1.9f - 6.0f;
            Need(IsInteriorPointClear(solids, kCenterX, kCenterY, midJetZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "Jet mode flying 6 m up clears central Archive Pedestal");
            Need(IsInteriorPointClear(solids, 99.0f, 371.0f, midJetZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "Jet mode flying 6 m up clears reflecting pools");
            Need(!IsInteriorPointClear(solids, 104.0f, 410.2f, midJetZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "Jet mode flying 6 m up still collides with 10.65 m Cinema enclosure wall");

            const float highJetZ = kFloorZ - 1.9f - 12.0f;
            Need(IsInteriorPointClear(solids, 104.0f, 410.2f, highJetZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
                 "Jet mode flying 12 m up clears Cinema roof cleanly");
        }
    }

    // 12. Cinema roof height rule (Z points DOWN; heights are above the terrace floor).
    {
        const float h = kBattleEngineCollisionHeight;
        const float cofg = 1.9f;
        auto worldZForFeet = [&](float feetH) { return kFloorZ - (feetH + cofg); };
        auto feetOf = [&](float worldZ) { return (kFloorZ - worldZ) - cofg; };
        Need(Near(kCinemaRoofMinX, 97.4f) && Near(kCinemaRoofMaxX, 126.6f) &&
             Near(kCinemaRoofMinY, 409.4f) && Near(kCinemaRoofMaxY, 428.6f) &&
             Near(kCinemaRoofBottomH, 9.0f) && Near(kCinemaRoofTopH, 10.65f),
             "Cinema roof slab bounds match contract");

        // A Jet descending onto the roof: feet inside the slab are stood on its top; falling speed is cancelled.
        {
            float z = worldZForFeet(9.8f), vz = 6.0f;
            Need(ResolveCinemaRoofHeight(112.0f, 419.0f, &z, kFloorZ, h, cofg, &vz) == kRoofClampLanded,
                 "feet inside the roof slab land on the roof");
            Need(feetOf(z) >= kCinemaRoofTopH && feetOf(z) <= kCinemaRoofTopH + 0.05f && Near(vz, 0.0f),
                 "landing rests just above the slab top with no falling speed");
            // Resting there is stable: the next tick changes nothing, and gravity sinking it a little re-lands it.
            float z2 = z, vz2 = 0.0f;
            Need(ResolveCinemaRoofHeight(112.0f, 419.0f, &z2, kFloorZ, h, cofg, &vz2) == kRoofClampNone && Near(z2, z),
                 "a body resting on the roof is left alone");
            float z3 = z + 0.4f, vz3 = 2.0f;
            Need(ResolveCinemaRoofHeight(112.0f, 419.0f, &z3, kFloorZ, h, cofg, &vz3) == kRoofClampLanded &&
                 Near(z3, z) && Near(vz3, 0.0f), "gravity sag is put back on the slab top");
            // Taking off again is never held down.
            float z4 = z, vz4 = -8.0f;
            Need(ResolveCinemaRoofHeight(112.0f, 419.0f, &z4, kFloorZ, h, cofg, &vz4) == kRoofClampNone && Near(vz4, -8.0f),
                 "lifting off the roof keeps its climb speed");
        }
        // Every part of the slab holds, across the footprint and the slab's whole thickness.
        for (float x = kCinemaRoofMinX + 0.1f; x < kCinemaRoofMaxX; x += 2.9f) {
            for (float y = kCinemaRoofMinY + 0.1f; y < kCinemaRoofMaxY; y += 2.7f) {
                for (float feet = kCinemaRoofBottomH; feet < kCinemaRoofTopH; feet += 0.33f) {
                    float z = worldZForFeet(feet), vz = 4.0f;
                    Need(ResolveCinemaRoofHeight(x, y, &z, kFloorZ, h, cofg, &vz) == kRoofClampLanded &&
                         feetOf(z) >= kCinemaRoofTopH && vz == 0.0f, "roof holds everywhere on the slab");
                }
            }
        }
        // On the roof a body is clear of every wall box beneath it: no sideways shove.
        {
            const SolidLayout solids = MakeInteriorSolidLayout();
            float z = worldZForFeet(10.2f);
            Need(ResolveCinemaRoofHeight(99.0f, 419.0f, &z, kFloorZ, h, cofg) == kRoofClampLanded, "lands over the West wall");
            Need(IsInteriorPointClear(solids, 99.0f, 419.0f, z, kFloorZ, kBattleEngineCollisionRadius, h, cofg),
                 "a body standing on the roof is clear of the wall boxes below");
            float x = 99.0f, y = 419.0f;
            Need(!ResolveInteriorCollision(solids, &x, &y, z, kFloorZ, kBattleEngineCollisionRadius, h, cofg),
                 "no horizontal push for a body standing on the roof");
        }
        // Inside the hall: a walker is untouched; a Jet rising into the soffit is held just under it.
        {
            float z = worldZForFeet(0.0f), vz = 0.0f;
            Need(ResolveCinemaRoofHeight(112.0f, 419.0f, &z, kFloorZ, h, cofg, &vz) == kRoofClampNone &&
                 Near(z, worldZForFeet(0.0f)), "a walker on the Cinema floor is untouched");
            float zj = worldZForFeet(7.0f), vzj = -5.0f;
            Need(ResolveCinemaRoofHeight(112.0f, 419.0f, &zj, kFloorZ, h, cofg, &vzj) == kRoofClampUnderSoffit,
                 "a head inside the slab from below is caught by the soffit");
            Need(feetOf(zj) + h <= kCinemaRoofBottomH && feetOf(zj) + h > kCinemaRoofBottomH - 0.05f && Near(vzj, 0.0f),
                 "the body is held just under the ceiling with no climb speed");
            Need(IsInsideCinema(112.0f, 419.0f, zj), "a body held under the soffit is still inside the Cinema");
        }
        // Outside the footprint, above the roof and bad input: nothing happens.
        {
            float z = worldZForFeet(9.8f);
            Need(ResolveCinemaRoofHeight(96.9f, 419.0f, &z, kFloorZ, h, cofg) == kRoofClampNone, "West of the slab: no clamp");
            Need(ResolveCinemaRoofHeight(112.0f, 408.9f, &z, kFloorZ, h, cofg) == kRoofClampNone, "South of the slab: no clamp");
            Need(ResolveCinemaRoofHeight(112.0f, 429.1f, &z, kFloorZ, h, cofg) == kRoofClampNone, "North of the slab: no clamp");
            Need(Near(z, worldZForFeet(9.8f)), "a miss leaves the height alone");
            float zh = worldZForFeet(14.0f);
            Need(ResolveCinemaRoofHeight(112.0f, 419.0f, &zh, kFloorZ, h, cofg) == kRoofClampNone, "flying over the roof: no clamp");
            float zn = std::nanf("");
            Need(ResolveCinemaRoofHeight(112.0f, 419.0f, &zn, kFloorZ, h, cofg) == kRoofClampNone, "NaN height refused");
            Need(ResolveCinemaRoofHeight(112.0f, 419.0f, NULL, kFloorZ, h, cofg) == kRoofClampNone, "null height refused");
            float zz = worldZForFeet(9.8f);
            Need(ResolveCinemaRoofHeight(112.0f, 419.0f, &zz, kFloorZ, 0.0f, cofg) == kRoofClampNone, "zero body height refused");
        }
    }

    // 13. Cinema weapon disarm: the machine OR the camera inside the hall is enough.
    Need(!CinemaDisarmsWeapons(false, false), "armed outside the Cinema");
    Need(CinemaDisarmsWeapons(true, false), "machine inside, camera still trailing outside: disarmed");
    Need(CinemaDisarmsWeapons(false, true), "camera inside, machine at the door: disarmed");
    Need(CinemaDisarmsWeapons(true, true), "both inside: disarmed");

    // 14. Dome seen from inside: overhead glass nearly gone, ribs at 40%, horizon band at full strength.
    {
        Need(Near(DomeViewerInsideBlend(kCenterX, kCenterY, kFloorZ - 2.0f), 1.0f), "viewer at the rotunda is fully inside");
        Need(Near(DomeViewerInsideBlend(kCenterX + 80.0f, kCenterY, kFloorZ - 2.0f), 0.0f), "viewer in the valley is outside");
        Need(DomeViewerInsideBlend(std::nanf(""), kCenterY, kFloorZ) == 0.0f, "NaN eye counts as outside");
        float last = 2.0f;
        for (float d = kDomeRadius - 6.0f; d <= kDomeRadius + 6.0f; d += 0.25f) {
            const float b = DomeViewerInsideBlend(kCenterX + d, kCenterY, kFloorZ - 2.0f);
            Need(b >= 0.0f && b <= 1.0f && b <= last + 1e-6f, "inside blend falls smoothly across the shell");
            last = b;
        }
        for (float e = 0.0f; e <= 1.0f; e += 0.05f) {
            Need(Near(DomeShellGain(e, 0.0f), 1.0f) && Near(DomeRibGain(e, 0.0f), 1.0f),
                 "from outside the dome is drawn at full strength");
            Need(DomeShellGain(e, 1.0f) <= DomeRibGain(e, 1.0f) + 1e-6f, "glass fades at least as much as the ribs");
            Need(DomeShellGain(e, 0.5f) <= 1.0f && DomeShellGain(e, 0.5f) >= DomeShellGain(e, 1.0f) - 1e-6f,
                 "half-way through the shell sits between the two looks");
        }
        Need(DomeShellGain(1.0f, 1.0f) <= 0.06f && DomeShellGain(0.6f, 1.0f) <= 0.06f, "overhead glass is near zero from inside");
        Need(Near(DomeRibGain(1.0f, 1.0f), 0.40f, 0.005f) && Near(DomeRibGain(0.6f, 1.0f), 0.40f, 0.005f),
             "overhead ribs are toned to 40% from inside");
        Need(Near(DomeShellGain(0.0f, 1.0f), 1.0f) && Near(DomeRibGain(0.0f, 1.0f), 1.0f) &&
             DomeShellGain(0.15f, 1.0f) > 0.95f && DomeRibGain(0.15f, 1.0f) > 0.95f,
             "the golden horizon band keeps its full strength from inside");
    }

    // 15. Forseti signal mast: one tall off-axis landmark, solid at its base, inside the dome, clear of the routes.
    {
        const SolidLayout solids = MakeInteriorSolidLayout();
        const float walkZ = kFloorZ - 1.9f;
        const float r = kBattleEngineCollisionRadius;
        Need(kSignalMastHeight >= 22.0f, "the mast stands well above the 9-10 m colonnades");
        Need(std::fabs(kSignalMastX - kCenterX) > 20.0f, "the mast is off the Citadel's axis");
        Need(IsOnTerrace(kSignalMastX, kSignalMastY), "the mast stands on the terrace");
        Need(IsInsideDome(kSignalMastX, kSignalMastY, kFloorZ - kSignalMastHeight - 1.0f),
             "the mast tip is inside the shield dome");
        Need(!IsInteriorPointClear(solids, kSignalMastX, kSignalMastY, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
             "the mast base is solid");
        Need(!IsInteriorPointClear(solids, kSignalMastX - (kSignalMastBaseHalf + r - 0.15f), kSignalMastY, walkZ, kFloorZ, r,
                                   kBattleEngineCollisionHeight, 1.9f),
             "the mast's wide plinth, not just its shaft, stops a walker");
        float mx = kSignalMastX - 0.4f, my = kSignalMastY + 0.3f;
        Need(ResolveInteriorCollision(solids, &mx, &my, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f) &&
             IsInteriorPointClear(solids, mx, my, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
             "walking into the mast pushes out cleanly");
        Need(!IsInteriorPointClear(solids, kSignalMastX, kSignalMastY, kFloorZ - 1.9f - 14.0f, kFloorZ, r,
                                   kBattleEngineCollisionHeight, 1.9f), "a Jet cannot fly through the mast shaft");
        Need(IsInteriorPointClear(solids, 128.0f, 354.5f, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f) &&
             IsInteriorPointClear(solids, 128.0f, 358.0f, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f) &&
             IsInteriorPointClear(solids, kCenterX, 366.0f, walkZ, kFloorZ, r, kBattleEngineCollisionHeight, 1.9f),
             "the mast leaves the ramp gateway and the launch pad clear");
        Need(solids.count <= kMaxSolidBoxes - 8u, "solid layout keeps headroom");
    }

    std::printf("ce_sanctuary_citadel_tests: all geometry, shield, beam, ripple, cinema, pedestal, 360-deg dome seal, interior collision, roof height, dome interior & signal mast checks passed\n");
    return 0;
}
"""

MUTANTS = [
    ("dome-wrong-z-sign",
     "if (dz < -kDomeRadius || dz > kDomeSkirtDepth) return false;",
     "if (dz > kDomeRadius || dz < -kDomeSkirtDepth) return false;"),
    ("dome-skip-skirt",
     "if (dz >= 0.0f) return horiz2 <= r2;",
     "if (dz >= 0.0f) return false;"),
    ("segment-skip-hemisphere",
     "if (ClipIntervalQuadLeq0(a, b, c, &t0, &t1) &&\n            ClipIntervalSlab(oz, dz, -double(kDomeRadius), 0.0, &t0, &t1)) {",
     "if (false && ClipIntervalQuadLeq0(a, b, c, &t0, &t1) &&\n            ClipIntervalSlab(oz, dz, -double(kDomeRadius), 0.0, &t0, &t1)) {"),
    ("segment-skip-skirt",
     "if (ClipIntervalQuadLeq0(a, b, c, &t0, &t1) &&\n            ClipIntervalSlab(oz, dz, 0.0, double(kDomeSkirtDepth), &t0, &t1)) {",
     "if (false && ClipIntervalQuadLeq0(a, b, c, &t0, &t1) &&\n            ClipIntervalSlab(oz, dz, 0.0, double(kDomeSkirtDepth), &t0, &t1)) {"),
    ("segment-pick-exit-t",
     "found = true;\n            bestT = t0;",
     "found = true;\n            bestT = t1;"),
    ("intercept-blocks-outgoing",
     "if (IsInsideDome(prevX, prevY, prevZ, floorZ))\n        return false;",
     "if (false && IsInsideDome(prevX, prevY, prevZ, floorZ))\n        return false;"),
    ("beam-no-pullback",
     "float tSafe = segLen > 1e-5f ? (tHit - kBeamPullback / segLen) : 0.0f;",
     "float tSafe = segLen > 1e-5f ? (tHit + 0.5f / segLen) : 0.0f;"),
    ("beam-no-clamp-write",
     "*ioVelX = vx * tSafe;\n    *ioVelY = vy * tSafe;\n    *ioVelZ = vz * tSafe;",
     "(void)tSafe;"),
    ("ripple-no-expire",
     "if (rb->slots[i].age >= kRippleDuration) {\n            rb->slots[i].active = false;",
     "if (false && rb->slots[i].age >= kRippleDuration) {\n            rb->slots[i].active = false;"),
    ("ripple-ignores-pause",
     "if (!rb || paused || !(dtSeconds > 0.0f) || !std::isfinite(dtSeconds)) return;",
     "if (!rb || !(dtSeconds > 0.0f) || !std::isfinite(dtSeconds)) return;"),
    ("cinema-leaks-south",
     "y >= kCinemaMinY && y <= kCinemaMaxY &&",
     "y >= 360.0f && y <= kCinemaMaxY &&"),
    ("pedestal-wrong-aim-z",
     "if (outLocalAimZ) *outLocalAimZ = -aimZ;",
     "if (outLocalAimZ) *outLocalAimZ = aimZ;"),
    ("visual-radius-clips-bastion",
     "constexpr float kDomeVisualRadius = 56.35f;",
     "constexpr float kDomeVisualRadius = 55.90f;"),
    ("visual-skirt-too-shallow",
     "constexpr float kDomeVisualSkirtMaxZ = 24.5f;",
     "constexpr float kDomeVisualSkirtMaxZ = 14.0f;"),
    ("visual-skirt-curves-inward",
     "if (nz > 0.0f) {",
     "if (false && nz > 0.0f) {"),
    ("collision-skips-pedestal",
     "AddSolidBox(&out, kCenterX - 2.06f, kCenterY - 2.06f,",
     "if (false) AddSolidBox(&out, kCenterX - 2.06f, kCenterY - 2.06f,"),
    ("collision-blocks-cinema-door",
     "AddSolidBox(&out, 109.5f,  409.20f, 114.5f,  410.90f, 5.20f, 10.65f);",
     "AddSolidBox(&out, 109.5f,  409.20f, 114.5f,  410.90f, 0.0f, 10.65f);"),
    ("collision-no-push-out",
     "x += nx * (pen + kCollisionPushEps);",
     "(void)pen;"),
    ("roof-falls-through",
     "if (feetH >= kCinemaRoofBottomH && feetH < kCinemaRoofTopH) {",
     "if (false && feetH >= kCinemaRoofBottomH && feetH < kCinemaRoofTopH) {"),
    ("roof-lands-inside-slab",
     "*ioWorldZ = floorZ - (kCinemaRoofTopH + kEps + cofg);",
     "*ioWorldZ = floorZ - (kCinemaRoofBottomH + kEps + cofg);"),
    ("roof-keeps-falling-speed",
     "if (ioVz && *ioVz > 0.0f) *ioVz = 0.0f;   // Z points down: positive is falling",
     "(void)ioVz;"),
    ("roof-ignores-footprint",
     "if (x < kCinemaRoofMinX || x > kCinemaRoofMaxX || y < kCinemaRoofMinY || y > kCinemaRoofMaxY)\n        return kRoofClampNone;",
     "(void)x; (void)y;"),
    ("roof-no-soffit",
     "if (feetH < kCinemaRoofBottomH && headH > kCinemaRoofBottomH) {",
     "if (false && feetH < kCinemaRoofBottomH && headH > kCinemaRoofBottomH) {"),
    ("disarm-camera-only",
     "return playerInside || cameraInside;",
     "return cameraInside;"),
    ("dome-ribs-full-inside",
     "const float interior = 0.40f + 0.60f * DomeHorizonBand(elev01);",
     "const float interior = 1.0f + 0.0f * DomeHorizonBand(elev01);"),
    ("dome-glass-full-inside",
     "const float interior = 0.05f + 0.95f * DomeHorizonBand(elev01);",
     "const float interior = 1.0f + 0.0f * DomeHorizonBand(elev01);"),
    ("dome-horizon-band-dimmed",
     "return 1.0f - u * u * (3.0f - 2.0f * u);\n}\n\ninline float DomeShellGain(",
     "return 0.0f * u;\n}\n\ninline float DomeShellGain("),
    ("mast-not-solid",
     "AddSolidBox(&out, kSignalMastX - kSignalMastBaseHalf, kSignalMastY - kSignalMastBaseHalf,",
     "if (false) AddSolidBox(&out, kSignalMastX - kSignalMastBaseHalf, kSignalMastY - kSignalMastBaseHalf,"),
    ("mast-too-short",
     "constexpr float kSignalMastHeight = 25.9f;",
     "constexpr float kSignalMastHeight = 9.0f;"),
    ("dome-skirt-too-shallow",
     "constexpr float kDomeSkirtDepth = 24.5f;",
     "constexpr float kDomeSkirtDepth = 2.0f;"),
]


def run(cmd, expect=0):
    res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return res.returncode == expect, res.stdout + res.stderr


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def main():
    failures = []

    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + ("" if ok else ("\n" + detail if detail else "")))
        if not ok:
            failures.append(name)

    # 1. Host ASan/UBSan test driver + causal mutants (strictly under tempfile.gettempdir()).
    with tempfile.TemporaryDirectory(prefix="ce-sanctuary-citadel-", dir=tempfile.gettempdir()) as tmp:
        d = Path(tmp)
        (d / "driver.cpp").write_text(DRIVER)
        asan_flags = ["c++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                      "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        ok, detail = run(asan_flags + ["-I" + str(GAME), str(d / "driver.cpp"), "-o", str(d / "driver")])
        check("sanctuary citadel driver builds with ASan/UBSan", ok, detail)
        if ok:
            ok, detail = run([str(d / "driver")])
            check("shield dome, 3500-shot fuzz sweeps, beam clamp, ripples, cinema & pedestal pose pass", ok, detail)

        hdr_src = CITADEL_H.read_text()
        for label, old, new in MUTANTS:
            if hdr_src.count(old) != 1:
                check("mutant anchor " + label, False, "not found once: " + old)
                continue
            m = d / ("mut_" + label)
            m.mkdir()
            (m / CITADEL_H.name).write_text(hdr_src.replace(old, new, 1))
            built, detail = run(["c++", "-std=c++14", "-O1", "-I" + str(m), "-I" + str(GAME),
                                 str(d / "driver.cpp"), "-o", str(m / "driver")])
            if not built:
                check("mutant " + label + " caught", False, detail)
                continue
            caught, _ = run([str(m / "driver")], 2)
            check("mutant " + label + " caught", caught)

    # 2. Exported seam, D3D9 state safety, heap isolation, SPDX & hygiene checks.
    cit_h = CITADEL_H.read_text()
    cit_cpp = CITADEL_CPP.read_text()
    patch_txt = PATCH.read_text(encoding="latin-1")

    check("exported seam defines CE_SANDBOX_HAS_CITADEL 1",
          "#define CE_SANDBOX_HAS_CITADEL 1" in cit_h)
    check("all 10 Runtime* and 7 extern C seam symbols declared and defined",
          all(sig in cit_h and sig in cit_cpp for sig in (
              "void RuntimeInit(",
              "void RuntimeShutdown()",
              "void RuntimeUpdate(",
              "void RuntimeTickSim()",
              "void RuntimeDrawWorld()",
              "void RuntimeDrawDomeFx()",
              "bool RuntimeHandleKey(",
              "bool RuntimeHandleFire(",
              "bool RuntimeIsInsideDome(",
              "bool RuntimeIsInsideCinema(",
              "bool RuntimeCue(",
              "PortCeSanctuaryActive(",
              "PortCeSanctuarySkipOutdoorWorld(",
              "PortCeSanctuaryInterceptRound(",
              "PortCeSanctuaryClampBeam(",
              "PortCeSanctuaryDrawWorld(",
              "PortCeSanctuaryFire(",
              "PortCeSanctuaryRenderCinemaPlayer(",
          )))
    check("citadel reuses CeMuseumTextures (all 13 textures) and wires CeArchivePedestal pose",
          "CeMuseumTextures::LoadAll(" in cit_cpp
          and "CeMuseumTextures::Get(" in cit_cpp
          and "CeMuseumTextures::ReleaseAll()" in cit_cpp
          and "CeArchivePedestal::RuntimeInit()" in cit_cpp
          and "CeArchivePedestal::RuntimeSetBasePose(" in cit_cpp
          and "CeArchivePedestal::RuntimeDraw(" in cit_cpp)
    check("citadel guards D3D9 state with StateBlock + LT.InvalidateAllStates + RENDERINFO dirty flags",
          "CreateStateBlock(D3DSBT_ALL" in cit_cpp
          and "LT.InvalidateAllStates();" in cit_cpp
          and "RENDERINFO.mWorldChanged = true;" in cit_cpp
          and "RENDERINFO.mViewChanged = true;" in cit_cpp
          and "RENDERINFO.mProjectionChanged = true;" in cit_cpp
          and "D3DRS_ZWRITEENABLE, FALSE" in cit_cpp)
    check("zero retail heap operator new / STL / fopen / BEA_* leaks in citadel modules",
          all("BEA_" not in txt for txt in (cit_h, cit_cpp))
          and re.search(r"\b(new\s+|delete\s+|std::vector|std::string|fopen\s*\(|fclose\s*\()",
                        strip_comments(cit_cpp)) is None)
    check("SPDX GPL-3.0-only present on all sanctuary citadel files",
          all("SPDX-License-Identifier: GPL-3.0-only" in txt for txt in (cit_h, cit_cpp, Path(__file__).read_text())))
    check("zero forbidden jargon or internal terms in citadel sources",
          not re.search(r"\b(prototype|experimental|ce\.ini|wave\s*[a-z]|agent|claude|anthropic|opus|sonnet)\b",
                        cit_h + "\n" + cit_cpp, re.I))

    # 3. Patch 0379 + 0394 scope, CE_VERSION guard, independence, and faithful neutrality.
    patch_0394_txt = (PORT / "patches/0394-ce-hook-projectile-intercept.patch").read_text(encoding="latin-1")
    files = re.findall(r"^--- a/(.+)$", patch_txt, re.M)
    check("patch 0379 touches only DXEngine.cpp (Round.cpp retired into 0394-ce-hook-projectile-intercept)",
          files == ["DXEngine.cpp"], f"got {files}")
    check("patch 0379 has zero CE_NETWORK_OFF or ce_hook_* calls",
          "CE_NETWORK_OFF" not in patch_txt and "ce_hook_" not in patch_txt)
    check("patch 0394 disarms 4001/4000/DeclareOnGround (StartDieProcess BEFORE mUnknown120), clamps beam [mBeamStart, mPos+mVelocity], and 0379 clears Cinema backbuffer",
          patch_0394_txt.count("+\t\t\tStartDieProcess();\n+\t\t\tmUnknown120 = TRUE;\n+\t\t\tmVelocity = FVector(0, 0, 0);\n+\t\t\treturn;") == 3
          and "!ceFlakEv.intercepted" in patch_0394_txt
          and "void CRound::DeclareOnGround()" in patch_0394_txt
          and "FVector ceBeamSeg = (mPos + mVelocity) - mBeamStart;" in patch_0394_txt
          and "CE_PROJ_BEAM_CLAMP" in patch_0394_txt
          and "PortCeSanctuaryProjectileIntercept(" in cit_cpp
          and "LT.D3D_Clear(0, NULL, D3DCLEAR_TARGET, 0x000a0c12, 1.0f, 0L);" in patch_txt)
    check("citadel splits South balustrade & retaining skirt around SE ramp, keeps South Cinema portal open, guards SkipOutdoorWorld & keys, and renders 3P player in Cinema",
          "EmitBalustradeX(354.5f,  75.0f, 119.5f" in cit_cpp
          and "EmitBalustradeX(354.5f, 136.5f, 149.0f" in cit_cpp
          and "kCinemaWallMinY - 0.20f" in cit_cpp
          and "yDoor = 410.65f" not in cit_cpp
          and "PortCePhotoOn()" in cit_cpp
          and "PortCeCmdMapOpen()" in cit_cpp
          and "kDikSpace" not in cit_cpp
          and "CeArchivePedestal::kDikTab && !CeArchivePedestal::RuntimeIsAimingControl()" in cit_cpp
          and "PortCeSanctuaryRenderCinemaPlayer(viewpoint);" in patch_txt
          and "D3DRS_COLORWRITEENABLE, 15" in cit_cpp)
    check("citadel uses managed GPU VB/IB buffers for static geometry, shares StateBlock with ArchivePedestal, drops bastion skirt to 0.00f, and loads retail faction emblem murals",
          "CreateVertexBuffer(" in cit_cpp
          and "CreateIndexBuffer(" in cit_cpp
          and "D3DPOOL_MANAGED" in cit_cpp
          and "ReleaseGpuBuffers();" in cit_cpp
          and "dev->DrawIndexedPrimitive(" in cit_cpp
          and "CeArchivePedestal::RuntimeDraw(gCitadel->floorZ, false);" in cit_cpp
          and "EmitBox(72.5f, 352.5f, -10.0f, 119.5f, 354.2f, 0.00f" in cit_cpp
          and re.search(r"-10\.0f[^;\n]*0\.03f", cit_cpp) is None
          and "goodies_F_Bakgrnd.tga" in cit_cpp
          and "goodies_M_Bakgrnd.tga" in cit_cpp
          and "PortCeAssetCatalogFindTexture(" in cit_cpp
          and "ReleaseFactionEmblemTextures();" in cit_cpp)
    inlay_match = re.search(r"const bool isInlay = \([^;]+\);", cit_cpp)
    check("citadel excludes 3D kMatBrass frames from isInlay depth bias and resets shaders/texstage with 6cm mural standoff",
          inlay_match is not None
          and "kMatBrass" not in inlay_match.group(0)
          and "kMatRunner" in inlay_match.group(0)
          and "dev->SetVertexShader(NULL);" in cit_cpp[cit_cpp.index("DrawFactionEmblemMurals("):cit_cpp.index("void RuntimeInit(")]
          and "dev->SetPixelShader(NULL);" in cit_cpp[cit_cpp.index("DrawFactionEmblemMurals("):cit_cpp.index("void RuntimeInit(")]
          and "dev->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);" in cit_cpp[cit_cpp.index("DrawFactionEmblemMurals("):cit_cpp.index("void RuntimeInit(")]
          and "dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);" in cit_cpp[cit_cpp.index("DrawFactionEmblemMurals("):cit_cpp.index("void RuntimeInit(")]
          and "{kEmblemForseti,  84.48f," in cit_cpp
          and "{kEmblemMuspell, 139.52f," in cit_cpp)
    draw_dome_fn = cit_cpp[cit_cpp.index("void RuntimeDrawDomeFx()"):cit_cpp.index("} // namespace CeSanctuaryCitadel")]
    check("citadel shield dome uses C^1 geodesic skirt, precomputed 128-seg 3-row ground collar, and skirt-conforming ripples",
          '#include "ce_sandbox_world.h"' in cit_h
          and "ProjectDomeVisualPoint(" in cit_cpp
          and "kDomeVisualSkirtNzMax" in cit_cpp
          and "kCollarSegs = 128" in cit_cpp
          and "BuildDomeGroundCollar();" in cit_cpp
          and "DomePerimeterGroundZ(" not in draw_dome_fn
          and "HeightAt(" not in draw_dome_fn
          and "zBot = gz + bury;" in draw_dome_fn
          and "zMid = gz - 0.15f;" in draw_dome_fn
          and "zTop = gz - 3.20f;" in draw_dome_fn
          and "const bool onSkirt = (rip.z > cz);" in draw_dome_fn)
    check("citadel emits North kMatBrass cornice trim, enforces interior SolidBox walkability collision, and suppresses Cinema weapon fire",
          "EmitBox(72.7f, 431.3f, -0.45f, 151.3f, 431.7f, -0.05f, kMatBrass, kBrassCol);" in cit_cpp
          and "gCitadel->solids = MakeInteriorSolidLayout();" in cit_cpp
          and "EnforceInteriorUnitCollisions();" in cit_cpp
          and "ResolveInteriorCollision(gCitadel->solids," in cit_cpp
          and "if (CinemaDisarmsWeapons(gCitadel->playerInsideCinema, gCitadel->insideCinema)) {\n        ClearBattleEngineWeaponChargeAndSlowMovement();\n        return true;\n    }" in cit_cpp)

    # 2b. Collision is simulation, occupancy is read fresh, the roof is a height rule, the terrace is lit for night.
    def body(src, start, end):
        a = src.index(start)
        return src[a:src.index(end, a)]
    update_fn = body(cit_cpp, "void RuntimeUpdate(float dtSeconds, bool paused,", "void RuntimeTickSim()")
    tick_fn = body(cit_cpp, "void RuntimeTickSim() {", "bool RuntimeHandleKey(")
    draw_fn = body(cit_cpp, "void RuntimeDrawWorld() {", "void RuntimeDrawDomeFx() {")
    fire_fn = body(cit_cpp, "bool RuntimeHandleFire(bool release, float value) {", "bool RuntimeIsInsideDome(")
    skip_fn = body(cit_cpp, 'extern "C" bool PortCeSanctuarySkipOutdoorWorld(void) {', 'extern "C" bool PortCeSanctuaryInterceptRound(')
    enforce_fn = body(cit_cpp, "static void EnforceInteriorUnitCollisions() {", "static void ClearBattleEngineWeaponChargeAndSlowMovement() {")
    cam_occ_fn = body(cit_cpp, "static void RefreshCinemaCameraOccupancy() {", "static void RefreshCinemaOccupancy() {")
    sandbox_cpp = (GAME / "ce_sandbox_director.cpp").read_text()
    sb_tick = body(sandbox_cpp, 'extern "C" void PortCeLivingSandboxTickPre(void) {', 'extern "C" void PortCeLivingSandboxDrawWorld(')
    sb_draw = body(sandbox_cpp, 'extern "C" void PortCeLivingSandboxDrawWorld(', 'extern "C" bool PortCeLivingSandboxKey(')
    check("interior unit collision runs in the sim tick, never in a render pass",
          "EnforceInteriorUnitCollisions();" in tick_fn
          and "EnforceInteriorUnitCollisions" not in update_fn
          and "EnforceInteriorUnitCollisions" not in draw_fn
          and "CeSanctuaryCitadel::RuntimeTickSim();" in sb_tick
          and "RuntimeTickSim" not in sb_draw
          and sb_tick.index("GAME.IsPaused()") < sb_tick.index("CeSanctuaryCitadel::RuntimeTickSim();"))
    check("Cinema occupancy is read fresh at sim/trigger and camera-only memoized in outdoor-draw skip",
          "RefreshCinemaOccupancy();" in tick_fn
          and skip_fn.index("RefreshCinemaCameraOccupancy();") < skip_fn.index("gCitadel->insideCinema")
          and "RefreshCinemaOccupancy(" not in skip_fn
          and "WORLD.GetUnitNB" not in cam_occ_fn
          and "cameraOccupancyFrame" in cam_occ_fn
          and fire_fn.index("RefreshCinemaOccupancy();") < fire_fn.index("CinemaDisarmsWeapons(")
          and "ENGINE.GetCameraForViewpoint(0)" in cit_cpp
          and "gCitadel->playerInsideCinema = playerInside;" in cit_cpp)
    occupancy_fn = body(cit_cpp, "static void RefreshCinemaOccupancy() {", "} // namespace")
    check("the player's machine for the Cinema disarm is the unit they pilot, else their Battle Engine",
          "PortCePlayUnitIsDriven(u)" in occupancy_fn
          and occupancy_fn.index("if (driven) {") < occupancy_fn.index("pl->GetBattleEngine()")
          and "driven->GetPos()" in occupancy_fn)
    check("Cinema roof is a height clamp applied before the horizontal push-out, with no ceiling SolidBox",
          enforce_fn.index("ResolveCinemaRoofHeight(") < enforce_fn.index("ResolveInteriorCollision(")
          and "t->MoveTo(FVector(x, y, z));" in enforce_fn
          and re.search(r"AddSolidBox\(&out,[^;]*kCinemaRoof", cit_h) is None)
    rig_fn = body(cit_cpp, "static void SetupCitadelLightRig(IDirect3DDevice9 *dev) {", "static void RestoreEngineRenderState(")
    amb = re.search(r"D3DRS_AMBIENT, D3DCOLOR_XRGB\((\d+), (\d+), (\d+)\)", rig_fn)
    key = re.search(r"setDirLight\(0,[^;]*?\n\s*([0-9.]+)f, ([0-9.]+)f, ([0-9.]+)f,", rig_fn)
    check("citadel is lit for its starfield sky: low cool ambient, moon key bluer than it is red, baked lantern pools",
          amb is not None and key is not None
          and max(int(v) for v in amb.groups()) <= 40 and int(amb.group(3)) > int(amb.group(1))
          and float(key.group(3)) > float(key.group(1)) + 0.2
          and "kLightPools[]" in cit_cpp
          and "AddLightPools(wx, wy, lz, isFloor, isVertical, &lightR, &lightG, &lightB);" in cit_cpp
          and "{kSignalMastX, kSignalMastY," in cit_cpp)
    dome_fn = body(cit_cpp, "void RuntimeDrawDomeFx() {", "} // namespace CeSanctuaryCitadel")
    ripple_at = dome_fn.index("Pass 2B")
    check("dome seen from inside: glass and ribs take the interior gains, impact ripples do not",
          "DomeViewerInsideBlend(gCitadel->eyeX, gCitadel->eyeY, gCitadel->eyeZ, cz)" in dome_fn
          and "DomeShellGain(elev, insideBlend)" in dome_fn[:ripple_at]
          and "DomeRibGain(elev, insideBlend)" in dome_fn[:ripple_at]
          and "insideBlend" not in dome_fn[ripple_at:])
    check("signal mast is built with its own contact shadow, AO footprint, inset cross-arm endcaps, and lantern",
          "const float mx = kSignalMastX, my = kSignalMastY;" in cit_cpp
          and "RegisterAoBox(kSignalMastX - 1.3f" in cit_cpp
          and "mx + 0.50f" in cit_cpp
          and "my + 0.28f" in cit_cpp
          and "kSignalMastHeight, kMatForsetiGlow" in cit_cpp)

    sys.path.insert(0, str(PORT))
    sys.path.insert(0, str(PORT / "tools"))
    import build  # noqa: E402
    import ce_photo_tests  # noqa: E402

    # Check independence when skipping prior patches on DXEngine.cpp (testing queue up through 0379)
    # AND applying 0379 alone onto bare retail mirror.
    later_patches = tuple(p.name for p in sorted((PORT / "patches").glob("*.patch")) if p.name > PATCH.name)
    for skipped in (
        ("0256-ce-museum-hall-world.patch",),
        ("0265-ce-authored-exhibition-world.patch",),
        ("0355-ce-anisotropic-filtering.patch",),
        ("0366-ce-museum-visitor-hud.patch",),
    ):
        try:
            build.patched_text(["Round.cpp", "DXEngine.cpp"], skip=skipped + later_patches)
            check("patch 0379 applies cleanly without " + "+".join(skipped), True)
        except RuntimeError as err:
            check("patch 0379 applies cleanly without " + "+".join(skipped), False, str(err))

    # Prove 0379 anchors purely on untouched retail lines: applies at fuzz 0 with ALL other patches skipped!
    all_others = tuple(p.name for p in sorted((PORT / "patches").glob("*.patch")) if p.name != PATCH.name)
    try:
        build.patched_text(["Round.cpp", "DXEngine.cpp"], skip=all_others)
        check("patch 0379 applies at fuzz 0 directly to bare retail Round.cpp and DXEngine.cpp", True)
    except RuntimeError as err:
        check("patch 0379 applies at fuzz 0 directly to bare retail Round.cpp and DXEngine.cpp", False, str(err))

    # Faithful build neutrality: with CE_VERSION undefined, patch 0379 leaves Round.cpp and DXEngine.cpp identical!
    with_0379 = build.patched_text(["Round.cpp", "DXEngine.cpp"], skip=later_patches)
    without_0379 = build.patched_text(["Round.cpp", "DXEngine.cpp"], skip=("0379-ce-sanctuary-shield.patch",) + later_patches)
    for fname in ("Round.cpp", "DXEngine.cpp"):
        f_with = ce_photo_tests.faithful(build.network_view(with_0379[fname], off=True))
        f_without = ce_photo_tests.faithful(build.network_view(without_0379[fname], off=True))
        check(f"patch 0379 is 100% neutral in faithful build for {fname}", f_with == f_without)

    try:
        ce_photo_tests.check_structure(build.patched_text(["DXEngine.cpp", "Hud.cpp", "ltshell.cpp", "game.cpp"]))
        check("ce_photo_tests.check_structure passes with patch 0379 applied", True)
    except AssertionError as err:
        check("ce_photo_tests.check_structure passes with patch 0379 applied", False, str(err))

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
