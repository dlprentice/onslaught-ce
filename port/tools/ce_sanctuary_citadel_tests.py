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
    Need(IsInsideDome(112.0f + 55.0f, 392.0f, -33.0f), "inside 2 m base skirt is inside dome");
    Need(IsInsideDome(112.0f + 55.9f, 392.0f, -32.0f), "bottom edge of skirt Z=-32 is inside dome");
    Need(!IsInsideDome(112.0f, 392.0f, -31.8f), "below skirt Z=-31.8 is outside dome");
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

        // Low grazing shot into 2 m cylindrical base skirt (Z = -32.8).
        Need(InterceptIncomingSegment(190.0f, 392.0f, -32.8f, 112.0f, 392.0f, -32.8f, kFloorZ, &hx, &hy, &hz),
             "low skirt grazing round intercepted");
        Need(Near(hx, 168.0f, 1e-2f) && Near(hz, -32.8f, 1e-2f),
             "low skirt round stops at R=56 cylinder wall");

        // Upward shot from cliff below plateau aiming up through floor into Sanctuary.
        Need(InterceptIncomingSegment(112.0f, 392.0f, -15.0f, 112.0f, 392.0f, -45.0f, kFloorZ, &hx, &hy, &hz),
             "upward shot from beneath plateau intercepted at skirt floor");
        Need(Near(hz, -32.0f, 1e-2f), "upward shot stops at Z=-32 skirt seal");

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

    std::printf("ce_sanctuary_citadel_tests: all geometry, shield, beam, ripple, cinema & pedestal checks passed\n");
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

    # 1. Host ASan/UBSan test driver + 12 causal mutants (strictly under tempfile.gettempdir()).
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
    check("all 10 Runtime* and 5 extern C seam symbols declared and defined",
          all(sig in cit_h and sig in cit_cpp for sig in (
              "void RuntimeInit(",
              "void RuntimeShutdown()",
              "void RuntimeUpdate(",
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

    # 3. Patch 0379 scope, CE_VERSION guard, independence from 0256/0265/0355/0366, and faithful neutrality.
    files = re.findall(r"^--- a/(.+)$", patch_txt, re.M)
    check("patch 0379 touches only Round.cpp and DXEngine.cpp",
          files == ["Round.cpp", "DXEngine.cpp"], f"got {files}")
    check("patch 0379 has zero CE_NETWORK_OFF or ce_hook_* calls",
          "CE_NETWORK_OFF" not in patch_txt and "ce_hook_" not in patch_txt)
    check("patch 0379 disarms 4001/4000/DeclareOnGround (StartDieProcess BEFORE mUnknown120), clamps beams vs mPos+mBeamStart, and clears Cinema backbuffer",
          patch_txt.count("+\t\t\tStartDieProcess();\n+\t\t\tmUnknown120 = TRUE;\n+\t\t\tmVelocity = FVector(0, 0, 0);\n+\t\t\treturn;") == 3
          and "!(PortCeSanctuaryActive() && (mFlags & TF_DYING))" in patch_txt
          and "void CRound::DeclareOnGround()" in patch_txt
          and "PortCeSanctuaryClampBeam(mPos.X, mPos.Y, mPos.Z" in patch_txt
          and "PortCeSanctuaryClampBeam(mBeamStart.X, mBeamStart.Y, mBeamStart.Z" in patch_txt
          and "LT.D3D_Clear(0, NULL, D3DCLEAR_TARGET, 0x000a0c12, 1.0f, 0L);" in patch_txt)
    check("citadel splits South balustrade & retaining skirt around SE ramp, seals Cinema doorway, and resets dome depth bias",
          "EmitBalustradeX(354.5f,  75.0f, 119.5f" in cit_cpp
          and "EmitBalustradeX(354.5f, 136.5f, 149.0f" in cit_cpp
          and "kCinemaWallMinY - 0.20f" in cit_cpp
          and "const float yDoor = 410.65f;" in cit_cpp
          and "D3DRS_COLORWRITEENABLE, 15" in cit_cpp)

    sys.path.insert(0, str(PORT))
    sys.path.insert(0, str(PORT / "tools"))
    import build  # noqa: E402
    import ce_photo_tests  # noqa: E402

    # Check independence when skipping prior patches on DXEngine.cpp AND applying 0379 alone onto bare retail mirror.
    for skipped in (
        ("0256-ce-museum-hall-world.patch",),
        ("0265-ce-authored-exhibition-world.patch",),
        ("0355-ce-anisotropic-filtering.patch",),
        ("0366-ce-museum-visitor-hud.patch",),
    ):
        try:
            build.patched_text(["Round.cpp", "DXEngine.cpp"], skip=skipped)
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
    with_0379 = build.patched_text(["Round.cpp", "DXEngine.cpp"])
    without_0379 = build.patched_text(["Round.cpp", "DXEngine.cpp"], skip=("0379-ce-sanctuary-shield.patch",))
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
