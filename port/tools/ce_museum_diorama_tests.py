#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host tests and causal mutants for the 4 Grand Museum glass-domed living tabletop battle dioramas
(port/game/ce_museum_diorama.h/.cpp)."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
HEADER = GAME / "ce_museum_diorama.h"
CPP = GAME / "ce_museum_diorama.cpp"

DRIVER = r"""
#include "ce_museum_diorama.h"
#include "ce_museum_exhibition.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace CeMuseumDiorama;
namespace Grand = CeMuseumExhibition::Grand;

static void Need(bool ok, const char *why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(2);
    }
}

static bool Near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

static bool AsciiOnly(const char *s) {
    if (!s || !*s) return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p)
        if (*p < 0x20u || *p > 0x7eu) return false;
    return true;
}

int main() {
    const Grand::RoomGraph graph = Grand::MakeRoomGraph();
    Need(kDioramaCount == 4, "four symmetrically placed glass-domed dioramas");

    // 1. Validate all 4 diorama specs, Atrium quadrant symmetry, collision registration, and walkable clearance.
    for (unsigned i = 0; i < kDioramaCount; ++i) {
        const DioramaSpec &d = kDioramas[i];
        Need(d.theme == static_cast<Theme>(i), "distinct diorama themes 0..3");
        Need(AsciiOnly(d.title) && AsciiOnly(d.subtitle) && AsciiOnly(d.caption), "ASCII diorama copy");
        Need(std::strlen(d.title) <= 95u && std::strlen(d.subtitle) <= 95u && std::strlen(d.caption) <= 95u,
             "diorama title, subtitle and caption fit CeMuseumLabel kChars=96 without truncation");
        char noteCheck[128] = {};
        std::snprintf(noteCheck, sizeof(noteCheck), "%s - aim at lectern to resume.", d.subtitle);
        Need(std::strlen(noteCheck) <= 95u, "non-lectern diorama note fits kChars=96 without truncation");
        Need(d.unitCount >= 12u && d.unitCount <= 16u && d.unitCount <= unsigned(kMaxUnitsPerDiorama),
             "hero-scaled 12..16 miniature unit roster");
        Need(d.tracerCount >= 12u && d.tracerCount <= unsigned(kMaxTracersPerDiorama),
             "lively multi-salvo combat tracers");
        Need(Grand::RoomAt(graph, CeMuseumExhibition::Vec2{d.localX, d.localY}) == Grand::kRoomAtrium,
             "diorama sits cleanly inside Grand Atrium");
        // Table centre & rim are solid collision obstacles.
        Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{d.localX, d.localY}) == CeMuseumExhibition::kWalkBlocked,
             "diorama table blocks visitor");
        // Approach point 1.15 m in front of brass lectern stud is walkable.
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        StudPosition(i, &sx, &sy, &sz);
        Need(Near(sz, kTabletopZ), "stud sits at tabletop height");
        const float ax = sx + d.studDirX * 1.15f;
        const float ay = sy + d.studDirY * 1.15f;
        Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{ax, ay}) == CeMuseumExhibition::kWalkOpen,
             "visitor can stand comfortably in front of diorama lectern control");
        // Verify zero overlap with Atrium skylight columns (+/-8.2, 28.2/43.8), Archive Pedestal, Directory, and North plinths.
        const float colX[2] = {-8.2f, 8.2f}, colY[2] = {28.2f, 43.8f};
        for (unsigned cx = 0; cx < 2u; ++cx) {
            for (unsigned cy = 0; cy < 2u; ++cy) {
                const float dist = std::hypot(d.localX - colX[cx], d.localY - colY[cy]);
                Need(dist > kTableRadius + 0.55f + 1.0f, "diorama clears Atrium skylight columns with wide aisle");
            }
        }
        // Must clear the central Atrium runners (x in [-1.56..1.56], y in [39.74..42.26], y in [22.89..25.11]).
        Need(std::fabs(d.localX) - kTableRadius > 5.5f, "diorama clears Atrium N-S spine and Rotunda");
        Need(std::fabs(d.localY - 41.0f) - kTableRadius > 1.5f, "diorama clears Atrium E-W Forseti/Muspell runners");
        Need(d.localY - kTableRadius > 26.5f, "diorama clears Atrium SE/SW art runners and portals");
        Need(d.localY + kTableRadius < 51.0f, "diorama clears Atrium North wall murals");
        Need(std::fabs(d.localX) + kTableRadius < 15.0f, "diorama clears Atrium West/East perimeter walls");
        // Verify zero overlap between pairs of dioramas.
        for (unsigned j = i + 1u; j < kDioramaCount; ++j) {
            const float sep = std::hypot(d.localX - kDioramas[j].localX, d.localY - kDioramas[j].localY);
            Need(sep > 2.0f * kTableRadius + 4.0f, "diorama tables stay widely separated");
        }
        // Verify terrain height field across tabletop disk is finite, bounded in [0, 0.54], and dramatically sculpted.
        float maxH = 0.0f;
        for (float rx = -2.2f; rx <= 2.21f; rx += 0.11f) {
            for (float ry = -2.2f; ry <= 2.21f; ry += 0.11f) {
                const float h = TerrainHeight(d.theme, rx, ry);
                Need(std::isfinite(h) && h >= 0.0f && h <= 0.54f, "bounded finite terrain elevation");
                if (h > maxH) maxH = h;
                if (std::hypot(rx, ry) >= kTerrainRadius)
                    Need(Near(h, 0.0f), "terrain flattens outside rim");
            }
        }
        if (d.theme != kThemeSkycarrier) {
            Need(maxH >= 0.42f, "land/reef dioramas have dramatic 0.42..0.52m sculpted relief");
        }
    }

    // 1b. Canonical lore roster checks: Venturer vs Fenrir on Skycarrier, Gill-M + M_submarine on equatorial Nautilus.
    {
        bool sawVenturer = false, sawFenrir = false;
        const DioramaSpec &sky = kDioramas[kThemeSkycarrier];
        for (unsigned ui = 0; ui < sky.unitCount; ++ui) {
            if (std::strcmp(sky.units[ui].meshStem, "f_ventura") == 0) sawVenturer = true;
            if (std::strcmp(sky.units[ui].meshStem, "boss-fenrir") == 0) sawFenrir = true;
        }
        Need(sawVenturer && sawFenrir, "Skycarrier diorama stages ACX-1 Venturer vs Aircarrier Fenrir");

        bool sawGillM = false, sawSub = false;
        const DioramaSpec &naut = kDioramas[kThemeNautilus];
        Need(std::strstr(naut.title, "Nautilus") != 0 && std::strstr(naut.title, "Gill-M") != 0,
             "Diorama IV is titled Nautilus Gill-M Ambush");
        for (unsigned ui = 0; ui < naut.unitCount; ++ui) {
            if (std::strcmp(naut.units[ui].meshStem, "boss_gill-m") == 0) sawGillM = true;
            if (std::strcmp(naut.units[ui].meshStem, "M_submarine") == 0) sawSub = true;
        }
        Need(sawGillM && sawSub, "Nautilus diorama stages Gill-M and verified M_submarine Thornback");
    }

    // 2. Sweep full 30s simulation across all 4 dioramas & every unit + tracer:
    // verify dome containment, realistic motion per MotionKind, pairwise ground clearance, and active tracers!
    for (unsigned di = 0; di < kDioramaCount; ++di) {
        const DioramaSpec &d = kDioramas[di];
        for (unsigned ui = 0; ui < d.unitCount; ++ui) {
            const UnitSpec &u = d.units[ui];
            Need(AsciiOnly(u.meshStem) && u.targetSpan > 0.15f && u.targetSpan < 0.80f, "valid miniature unit spec");
            UnitPose p0 = {};
            EvaluateUnitPose(d.theme, u, 0.0f, &p0);
            float maxMove = 0.0f;
            float maxBank = 0.0f;
            float maxYawDelta = 0.0f;
            for (int step = 0; step <= 200; ++step) {
                const float t = float(step) * 0.15f;
                UnitPose p = {};
                EvaluateUnitPose(d.theme, u, t, &p);
                Need(std::isfinite(p.dx) && std::isfinite(p.dy) && std::isfinite(p.dz) &&
                     std::isfinite(p.yawRad) && std::isfinite(p.pitchRad) && std::isfinite(p.rollRad),
                     "finite unit pose");
                const float r = std::hypot(p.dx, p.dy);
                const float domeR = DomeHorizontalRadius(p.dz + u.targetSpan * 0.35f);
                Need(p.dz >= -0.01f && p.dz < (kDomeTopZ - kTabletopZ - 0.15f), "unit vertically inside crystal dome");
                Need(r <= domeR - u.targetSpan * 0.50f - 0.04f, "unit radially contained inside crystal dome");
                const float mv = std::hypot(p.dx - p0.dx, p.dy - p0.dy);
                if (mv > maxMove) maxMove = mv;
                if (std::fabs(p.rollRad) > maxBank) maxBank = std::fabs(p.rollRad);
                const float yd = std::fabs(p.yawRad - p0.yawRad);
                if (yd > maxYawDelta) maxYawDelta = yd;
            }
            if (u.motion == kMotionOrbit) {
                Need(maxMove > 0.8f, "orbiting aircraft flies around tabletop");
                Need(maxBank > 0.20f, "orbiting aircraft banks realistically");
            } else if (u.motion == kMotionPatrol) {
                Need(maxMove > 0.10f, "patrolling ground unit moves");
            } else if (u.motion == kMotionTurretSweep) {
                Need(maxYawDelta > 0.25f, "sweeping turret tracks across field");
            }
        }
        // Pairwise clearance between surface/ground units across full 30s simulation so units never overlap.
        for (int step = 0; step <= 120; ++step) {
            const float t = float(step) * 0.25f;
            UnitPose poses[kMaxUnitsPerDiorama] = {};
            for (unsigned ui = 0; ui < d.unitCount; ++ui)
                EvaluateUnitPose(d.theme, d.units[ui], t, &poses[ui]);
            for (unsigned a = 0; a < d.unitCount; ++a) {
                if (d.units[a].motion == kMotionOrbit) continue;
                for (unsigned b = a + 1u; b < d.unitCount; ++b) {
                    if (d.units[b].motion == kMotionOrbit) continue;
                    // Ignore units on distinct altitude layers (e.g. hovering skycarriers vs sea escorts)
                    if (std::fabs(poses[a].dz - poses[b].dz) > 0.32f) continue;
                    const float dist = std::hypot(poses[a].dx - poses[b].dx, poses[a].dy - poses[b].dy);
                    const float minSep = 0.42f * (d.units[a].targetSpan + d.units[b].targetSpan);
                    Need(dist >= minSep, "surface miniature units stay clear of each other across 30s");
                }
            }
        }
        // Extreme forced-overflow synthetic unit must be clamped inside the dome by EvaluateUnitPose!
        {
            UnitSpec wild = {"test", true, kShapeJet, kMotionOrbit, 5.0f, 5.0f, 1.40f, 3.0f, 1.0f, 0.0f, 0.3f, 0.30f};
            UnitPose clamped = {};
            EvaluateUnitPose(d.theme, wild, 0.5f, &clamped);
            const float r = std::hypot(clamped.dx, clamped.dy);
            const float domeR = DomeHorizontalRadius(clamped.dz + wild.targetSpan * 0.35f);
            Need(r <= domeR - wild.targetSpan * 0.50f, "dome safety clamp catches wide coordinates");
        }
        for (unsigned ti = 0; ti < d.tracerCount; ++ti) {
            const TracerSpec &tr = d.tracers[ti];
            Need(tr.fromUnit < d.unitCount && tr.toUnit < d.unitCount && tr.fromUnit != tr.toUnit, "valid tracer endpoints");
            bool sawBolt = false, sawImpact = false, sawSmoke = false, sawArc = (tr.arcHeight <= 0.0f);
            for (int step = 0; step < 60; ++step) {
                const float t = float(step) * (tr.periodSec / 60.0f);
                TracerSegment seg = {};
                EvaluateTracer(d, tr, t, &seg);
                if (seg.active) {
                    sawBolt = true;
                    Need(std::isfinite(seg.ax) && std::isfinite(seg.bx) && std::isfinite(seg.bz), "finite tracer bolt");
                    if (tr.arcHeight > 0.0f) {
                        UnitPose sPose = {}, dPose = {};
                        EvaluateUnitPose(d.theme, d.units[tr.fromUnit], t, &sPose);
                        EvaluateUnitPose(d.theme, d.units[tr.toUnit], t, &dPose);
                        const float dxTotal = dPose.dx - sPose.dx, dyTotal = dPose.dy - sPose.dy;
                        const float len2 = dxTotal * dxTotal + dyTotal * dyTotal;
                        float s = 0.5f;
                        if (len2 > 1e-5f) s = ((seg.bx - sPose.dx) * dxTotal + (seg.by - sPose.dy) * dyTotal) / len2;
                        const float linearZ = (sPose.dz + 0.07f) + ((dPose.dz + 0.07f) - (sPose.dz + 0.07f)) * s;
                        if (seg.bz > linearZ + 0.01f) sawArc = true;
                    }
                }
                if (seg.impactAlpha > 0.05f) sawImpact = true;
                if (seg.smokeAlpha > 0.05f && seg.smokeRise > 0.02f) sawSmoke = true;
            }
            Need(sawBolt && sawImpact && sawSmoke && sawArc,
                 "every tracer fires bolts, arches when set, flashes on impact, and raises smoke");
        }
    }

    // 3. Simulation step, dt clamp, per-diorama pause toggle, and global game pause freeze.
    DioramaState st = {};
    Reset(&st);
    Need(st.initialized && !st.paused[0] && !st.paused[3], "dioramas start live and unpaused");
    const float t0 = st.simTime[0];
    Step(&st, 0.1f, false);
    Need(Near(st.simTime[0], t0 + 0.1f), "live diorama advances by dt");
    const float tBeforeSpike = st.simTime[0];
    Step(&st, 5.0f, false);
    Need(Near(st.simTime[0], tBeforeSpike + 0.25f), "large dt clamped to 0.25s");
    const float tBeforeGamePause = st.simTime[0];
    Step(&st, 0.2f, true);
    Need(Near(st.simTime[0], tBeforeGamePause), "game pause freezes all dioramas");
    TogglePause(&st, 0);
    Need(st.paused[0] && !st.paused[1], "toggle pauses only targeted diorama");
    const float t0Frozen = st.simTime[0], t1Before = st.simTime[1];
    Step(&st, 0.2f, false);
    Need(Near(st.simTime[0], t0Frozen) && Near(st.simTime[1], t1Before + 0.2f),
         "paused diorama freezes while others keep battling");
    TogglePause(&st, 0);
    Need(!st.paused[0], "second toggle resumes diorama");
    TogglePause(&st, -1);
    TogglePause(&st, kDioramaCount);

    // 4. Brass lectern stud ray-pick and curator plaque focus (strictly Atrium-scoped).
    for (unsigned i = 0; i < kDioramaCount; ++i) {
        const DioramaSpec &d = kDioramas[i];
        float sx = 0.0f, sy = 0.0f, sz = 0.0f;
        StudPosition(i, &sx, &sy, &sz);
        const float ex = sx + d.studDirX * 1.8f;
        const float ey = sy + d.studDirY * 1.8f;
        const float ez = 1.65f;
        Need(PickStud(ex, ey, ez, sx - ex, sy - ey, sz - ez, CeMuseumTour::kWingAtrium) == int(i),
             "direct aim picks exact diorama lectern stud");
        Need(PickStud(ex, ey, ez, sx - ex, sy - ey, sz - ez, CeMuseumTour::kWingCourtyard) == -1,
             "stud pick rejects non-Atrium wing");
        Need(PickStud(ex, ey, ez, d.studDirX, d.studDirY, 0.0f, CeMuseumTour::kWingAtrium) == -1,
             "looking away misses stud");
        Need(FocusDiorama(ex, ey, -d.studDirX, -d.studDirY, CeMuseumTour::kWingAtrium) == int(i),
             "facing diorama focuses its curator plaque");
        Need(FocusDiorama(ex, ey, -d.studDirX, -d.studDirY, CeMuseumTour::kWingForseti) == -1,
             "wrong wing clears diorama focus");
        Need(FocusDiorama(ex, ey, d.studDirX, d.studDirY, CeMuseumTour::kWingAtrium) == -1,
             "looking away clears diorama focus");
    }
    Need(PickStud(0.0f, 32.0f, 1.65f, 0.0f, 0.0f, 0.0f, CeMuseumTour::kWingAtrium) == -1, "zero aim misses stud");

    std::printf("ce_museum_diorama_tests: all checks passed\n");
    return 0;
}
"""

MUTANTS = [
    ("ignore-game-pause", "if (!s || !s->initialized || gamePaused || !(dtSeconds > 0.0f)) return;",
     "if (!s || !s->initialized || !(dtSeconds > 0.0f)) return;"),
    ("ignore-slot-pause", "if (!s->paused[i]) s->simTime[i] += dt;",
     "s->simTime[i] += dt;"),
    ("no-dt-clamp", "const float dt = dtSeconds > 0.25f ? 0.25f : dtSeconds;",
     "const float dt = dtSeconds;"),
    ("no-dome-clamp", "if (curR > safeR && curR > 1e-5f) {",
     "if (false && curR > safeR && curR > 1e-5f) {"),
    ("no-wing-filter-pick", "if (currentWing != CeMuseumTour::kWingAtrium) return -1;\n    const float alen",
     "const float alen"),
    ("no-wing-filter-focus", "if (currentWing != CeMuseumTour::kWingAtrium) return -1;\n    const float flen",
     "const float flen"),
]


def run(cmd, expect=0):
    res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return res.returncode == expect, res.stdout + res.stderr


def main():
    failures = []

    def check(name, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + name + ("" if ok else ("\n" + detail if detail else "")))
        if not ok:
            failures.append(name)

    out = ROOT / "local-data" / "museum-diorama-tests"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        d = Path(tmp)
        (d / "driver.cpp").write_text(DRIVER)
        flags = ["c++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        ok, detail = run(flags + ["-I" + str(GAME), str(d / "driver.cpp"), "-o", str(d / "driver")])
        check("diorama driver builds with ASan/UBSan", ok, detail)
        if ok:
            ok, detail = run([str(d / "driver")])
            check("all 4 glass-domed dioramas, terrain, kinematics, tracers and clearance hold", ok, detail)

        src = HEADER.read_text()
        for label, old, new in MUTANTS:
            if src.count(old) != 1:
                check("mutant anchor " + label, False, "not found once: " + old)
                continue
            m = d / label
            m.mkdir()
            (m / HEADER.name).write_text(src.replace(old, new, 1))
            built, detail = run(["c++", "-std=c++14", "-O1", "-I" + str(m), "-I" + str(GAME),
                                 str(d / "driver.cpp"), "-o", str(m / "driver")])
            if not built:
                check("mutant " + label + " caught", False, detail)
                continue
            caught, _ = run([str(m / "driver")], 2)
            check("mutant " + label + " caught", caught)

    hdr = HEADER.read_text()
    cpp = CPP.read_text()
    check("diorama runtime caches tables, terrains and crystal domes in D3DPOOL_MANAGED VBs/IBs",
          "D3DPOOL_MANAGED" in cpp and "stageVb" in cpp and "domeVb" in cpp
          and "DrawIndexedPrimitive(D3DPT_TRIANGLELIST" in cpp)
    check("diorama runtime raises GPU/VFX buffer capacities with strict bounds checks",
          "kMaxStageVerts = 32000u" in cpp and "kMaxStageIdx = 128000u" in cpp
          and "kMaxDomeVerts = 12288u" in cpp and "kMaxDomeIdx = 56000u" in cpp
          and "kMaxVfxVerts = 8192u" in cpp and "HasSpace(" in cpp)
    check("diorama runtime uses CeArchiveScanner SwapArena and shuts it down right after GPU upload",
          "SwapArena arena" in cpp and "arena.Init(" in cpp and "arena.Shutdown();" in cpp)
    check("diorama runtime degrades gracefully to procedural miniature silhouettes when retail files are absent",
          "BuildFallbackUnitMesh(" in cpp and "loadedRetail" in cpp)
    check("diorama runtime releases all GPU VBs/IBs, textures, and heap state in RuntimeShutdown",
          "void RuntimeShutdown()" in cpp and "ReleaseStageBuffers(gDio);" in cpp
          and "ReleaseUnitAssets(gDio);" in cpp and "std::free(gDio);" in cpp
          and "terrainDetailTex->Release()" in cpp)
    check("diorama runtime uploads 256px full-mipmapped textures and 256x256 MODULATE2X terrain micro-detail",
          "kDioramaTexMaxEdge = 256u" in cpp and "D3DX_FILTER_BOX" in cpp
          and "D3DSAMP_MIPFILTER, D3DTEXF_LINEAR" in cpp and "D3DTOP_MODULATE2X" in cpp
          and "CreateTerrainDetailTexture(" in cpp)
    check("diorama runtime builds 32x64 terrain, 32-seg drum pedestal, lit curator lectern, and 48x16 2-pass specular blown glass",
          "kRings = 32u" in cpp and "kSegs = 64u" in cpp
          and "kDomeSegs = 48u" in cpp and "kDomeRings = 16u" in cpp
          and "DrumTier(" in cpp and "EmitCuratorLectern(" in cpp
          and "D3DCULL_CW" in cpp and "D3DRS_SPECULARENABLE, TRUE" in cpp
          and "D3DRS_LOCALVIEWER, TRUE" in cpp and "glassMat.Power = 64.0f" in cpp)
    check("diorama runtime renders additive starbursts and smoke BEFORE glass dome with ZWRITEENABLE FALSE and zero cube impacts",
          "DrawDioramaVfx(dev, d, floorZ);" in cpp
          and cpp.index("DrawDioramaVfx(dev, d, floorZ);") < cpp.index("d->domeGlassRange.firstIndex")
          and "D3DRS_ZWRITEENABLE, FALSE" in cpp and "pushStarburst" in cpp
          and "pushBox" not in cpp)
    check("diorama runtime clamps ARGB byte channels after float-to-int conversion (x87 FE_TONEAREST wrap guard)",
          "FloatToByte(" in cpp and "ClampByteU(" in cpp
          and "((r & 255u) << 16)" not in cpp)
    check("diorama runtime keeps zero retail C++ new/delete or fopen calls",
          "new " not in cpp and "delete " not in cpp and "fopen(" not in cpp)
    check("diorama player-facing strings stay free of forbidden technical jargon",
          not re.search(r'"[^"]*(?:prototype|experimental|ce\.ini)[^"]*"', hdr, re.I))
    check("diorama header stays free of non-canon Arctic/Kraken/XF-24/missing-mesh references",
          not re.search(r'"[^"]*(?:kraken|arctic|polar|iceberg|xf-24|dominion|boss_sub|thunderhead)[^"]*"', hdr, re.I))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
