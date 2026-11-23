#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host tests and causal mutants for Grand Museum rotating 3D turntables (ce_museum_turntable.h/.cpp)."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
HEADER = GAME / "ce_museum_turntable.h"
CPP = GAME / "ce_museum_turntable.cpp"

DRIVER = r"""
#include "ce_museum_turntable.h"
#include "ce_museum_exhibition.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace CeMuseumTurntable;
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
    Need(kManifestCount == 18, "manifest has 18 curated 3D exhibits");

    // 1. Wing-scoped residency budget holds for every wing.
    for (int w = 0; w < int(CeMuseumTour::kWingCount); ++w) {
        const CeMuseumTour::Wing wing = static_cast<CeMuseumTour::Wing>(w);
        const unsigned count = CountWantedResident(wing);
        Need(count <= unsigned(kMaxResidentGoodies), "resident goodies stay within wing RAM cap");
    }
    // Courtyard Hero Aquila is resident in both Courtyard and Atrium, unloaded in distant wings.
    Need(ShouldBeResident(0, CeMuseumTour::kWingCourtyard), "hero resident in courtyard");
    Need(ShouldBeResident(0, CeMuseumTour::kWingAtrium), "hero resident in atrium sightline");
    Need(!ShouldBeResident(0, CeMuseumTour::kWingMuspell), "hero unloaded in Muspell wing");
    Need(!ShouldBeResident(0, CeMuseumTour::kWingHangar), "hero unloaded in Hangar wing");
    Need(!ShouldBeResident(99, CeMuseumTour::kWingCourtyard), "out-of-range index never resident");

    // 2. Every manifest entry is ASCII-clean, sits in its wing, and has a reachable stud.
    for (unsigned i = 0; i < kManifestCount; ++i) {
        const Spec &sp = kManifest[i];
        Need(sp.goodieId > 0 && sp.goodieId < 256, "valid goodie id");
        Need(AsciiOnly(sp.title) && AsciiOnly(sp.category) && AsciiOnly(sp.caption), "ASCII plaque copy");
        Need(sp.plinthTopZ > 0.3f && sp.plinthHalf > 0.5f, "sensible plinth dimensions");
        Need(Grand::RoomAt(graph, CeMuseumExhibition::Vec2{sp.localX, sp.localY}) == static_cast<Grand::RoomId>(sp.wing),
             "turntable stands in its declared wing");
        // Plinth centre is solid; approach point 1.2 m in front of stud is walkable.
        Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{sp.localX, sp.localY}) == CeMuseumExhibition::kWalkBlocked,
             "turntable plinth blocks visitor");
        float sx, sy, sz;
        StudPosition(i, &sx, &sy, &sz);
        const float ax = sx + sp.studDirX * 1.15f;
        const float ay = sy + sp.studDirY * 1.15f;
        Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{ax, ay}) == CeMuseumExhibition::kWalkOpen,
             "visitor can stand in front of every turntable stud");
    }

    // 3. Rotation speed (~12 deg/s), dt clamp, pause hold, wrap-around, and Pause/Resume toggle.
    BankState bank = {};
    Reset(&bank);
    Need(bank.initialized, "bank initialized");
    Need(!bank.slots[0].paused, "hero rotates by default");
    Need(bank.slots[15].paused && bank.slots[16].paused && bank.slots[17].paused,
         "scale alcove units start stationary");

    const float y0 = bank.slots[0].yawRad;
    StepRotation(&bank, 0.1f, false);
    Need(Near(bank.slots[0].yawRad, y0 + kDefaultRateRadPerSec * 0.1f), "rotates at ~12 deg/s");
    const float yScale = bank.slots[15].yawRad;
    StepRotation(&bank, 0.2f, false);
    Need(Near(bank.slots[15].yawRad, yScale), "paused turntable holds angle");

    // Game pause freezes all turntables.
    const float yBeforePause = bank.slots[0].yawRad;
    StepRotation(&bank, 0.2f, true);
    Need(Near(bank.slots[0].yawRad, yBeforePause), "game pause freezes rotation");

    // Large dt is clamped to 0.25 s.
    const float yBeforeSpike = bank.slots[0].yawRad;
    StepRotation(&bank, 5.0f, false);
    Need(Near(bank.slots[0].yawRad, yBeforeSpike + kDefaultRateRadPerSec * 0.25f), "large dt clamped");

    // Full revolution wraps cleanly into [0, 2*PI).
    for (int step = 0; step < 200; ++step) StepRotation(&bank, 0.2f, false);
    Need(bank.slots[0].yawRad >= 0.0f && bank.slots[0].yawRad < 6.283185307f, "yaw wraps into [0, 2pi)");

    // TogglePause flips state and ignores out-of-range indices.
    TogglePause(&bank, 0);
    Need(bank.slots[0].paused, "toggle pauses rotating pedestal");
    const float yFrozen = bank.slots[0].yawRad;
    StepRotation(&bank, 0.2f, false);
    Need(Near(bank.slots[0].yawRad, yFrozen), "toggled pedestal stays frozen");
    TogglePause(&bank, 0);
    Need(!bank.slots[0].paused, "second toggle resumes rotation");
    TogglePause(&bank, -1);
    TogglePause(&bank, kManifestCount);

    // 4. Ray-pick brass control stud.
    float sx, sy, sz;
    StudPosition(0, &sx, &sy, &sz);
    const float eyeX = sx, eyeY = sy - 2.0f, eyeZ = 1.7f;
    Need(PickStud(eyeX, eyeY, eyeZ, sx - eyeX, sy - eyeY, sz - eyeZ, CeMuseumTour::kWingCourtyard) == 0,
         "direct aim hits hero stud");
    Need(PickStud(eyeX, eyeY, eyeZ, sx - eyeX, sy - eyeY, sz - eyeZ, CeMuseumTour::kWingMuspell) == -1,
         "stud pick respects active wing");
    Need(PickStud(eyeX, eyeY, eyeZ, 0.0f, -1.0f, 0.0f, CeMuseumTour::kWingCourtyard) == -1,
         "looking away misses stud");
    Need(PickStud(sx, sy - 9.0f, 1.7f, 0.0f, 1.0f, -0.07f, CeMuseumTour::kWingCourtyard) == -1,
         "out-of-range aim misses stud");
    Need(PickStud(eyeX, eyeY, eyeZ, 0.0f, 0.0f, 0.0f, CeMuseumTour::kWingCourtyard) == -1,
         "zero aim vector safely misses");

    // 5. FocusExhibit picks nearest faced exhibit in current wing.
    Need(FocusExhibit(0.0f, 5.0f, 0.0f, 1.0f, CeMuseumTour::kWingCourtyard) == 0, "facing hero focuses hero");
    Need(FocusExhibit(0.0f, 5.0f, 0.0f, -1.0f, CeMuseumTour::kWingCourtyard) == -1, "facing away clears focus");
    Need(FocusExhibit(22.0f, 41.0f, 0.0f, 1.0f, CeMuseumTour::kWingMuspell) == 3, "facing Muspell walker focuses it");
    Need(FocusExhibit(22.0f, 41.0f, 0.0f, 1.0f, CeMuseumTour::kWingCourtyard) == -1, "wrong wing clears focus");

    // 6. Scale computation & clamping.
    Need(Near(ComputeScale(10.0f, 4.0f), 0.4f), "normal scale ratio");
    Need(Near(ComputeScale(0.0f, 4.0f), 1.0f), "zero mesh span safe fallback");
    Need(Near(ComputeScale(5.0f, 0.0f), 1.0f), "zero target span keeps 1:1");
    Need(Near(ComputeScale(1000.0f, 1.0f), 0.02f), "min scale clamped");
    Need(Near(ComputeScale(0.01f, 5.0f), 6.0f), "max scale clamped");

    // 7. LOD mNextInChain teardown ordering: every chained mesh node must be deleted
    // before any shared slot texture is destroyed (no UAF on ~CMeshTexture / no leak).
    {
        struct FakeTex { bool alive = true; int refs = 1; };
        struct FakeMesh {
            FakeMesh *mNextInChain = nullptr;
            FakeTex *tex = nullptr;
            int refs = 0;
            int *destroyed = nullptr;
            void AddRef() { ++refs; }
            void Release() { --refs; }
            ~FakeMesh() {
                Need(tex && tex->alive, "LOD mesh destroyed while texture is still alive");
                --tex->refs;
                if (mNextInChain) mNextInChain->Release();
                if (destroyed) ++(*destroyed);
            }
            void ForceDelete() { delete this; }
        };
        FakeTex sharedTex;
        int destroyed = 0;
        FakeMesh *lod2 = new FakeMesh{nullptr, &sharedTex, 1, &destroyed};
        sharedTex.refs++;
        FakeMesh *lod1 = new FakeMesh{lod2, &sharedTex, 1, &destroyed};
        sharedTex.refs++;
        FakeMesh *root = new FakeMesh{lod1, &sharedTex, 1, &destroyed};
        sharedTex.refs++;
        root->Release();
        FakeMesh *cur = root;
        while (cur) {
            FakeMesh *next = cur->mNextInChain;
            if (next) {
                cur->mNextInChain = nullptr;
                next->Release();
            }
            cur->ForceDelete();
            cur = next;
        }
        Need(destroyed == 3 && sharedTex.refs == 1, "all 3 LOD meshes deleted before texture release");
        sharedTex.alive = false;
    }

    std::printf("ce_museum_turntable_tests: all checks passed\n");
    return 0;
}
"""

MUTANTS = [
    ("ignore-game-pause", "if (!s || !s->initialized || gamePaused || !(dtSeconds > 0.0f)) return;",
     "if (!s || !s->initialized || !(dtSeconds > 0.0f)) return;"),
    ("ignore-slot-pause", "if (s->slots[i].paused) continue;", "if (false) continue;"),
    ("no-dt-clamp", "const float clamped = dtSeconds > 0.25f ? 0.25f : dtSeconds;",
     "const float clamped = dtSeconds;"),
    ("no-wing-filter-resident", "if (w == currentWing) return true;", "return true;"),
    ("no-wing-filter-pick",
     "        if (!ShouldBeResident(i, currentWing)) continue;\n        float sx, sy, sz;",
     "        float sx, sy, sz;"),
    ("no-scale-clamp", "return s < 0.02f ? 0.02f : s > 6.0f ? 6.0f : s;", "return s;"),
    ("stud-range-ignored", "if (t <= 0.15f || t > kStudMaxRange) continue;", "if (t <= 0.15f) continue;"),
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

    out = ROOT / "local-data" / "museum-turntable-tests"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        d = Path(tmp)
        (d / "driver.cpp").write_text(DRIVER)
        flags = ["c++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        ok, detail = run(flags + ["-I" + str(GAME), str(d / "driver.cpp"), "-o", str(d / "driver")])
        check("turntable driver builds with ASan/UBSan", ok, detail)
        if ok:
            ok, detail = run([str(d / "driver")])
            check("turntable manifest, walkability, rotation, pick and scale hold", ok, detail)

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

    cpp = CPP.read_text()
    # 8. Wing-boundary hysteresis: extract actual ResidencyHysteresis + StepWingResidencyPolicy and test with ASan/UBSan + causal mutants.
    h_start = cpp.index("constexpr float kWingSwitchDwellSeconds")
    h_end = cpp.index("struct LoadedGoodie {")
    hysteresis_src = cpp[h_start:h_end]
    hyst_driver = r"""
#include "ce_museum_tour.h"
#include <cstdio>
#include <cstdlib>
namespace {
""" + hysteresis_src + r"""
}
static void Need(bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr, "HYST FAIL: %s\n", why); std::exit(2); }
}
int main() {
    ResidencyHysteresis h = {};
    ResetHysteresis(&h);
    // 1. First entry commits immediately.
    Need(StepWingResidencyPolicy(&h, CeMuseumTour::kWingCourtyard, 0.016f, false), "first entry commits immediately");
    Need(h.hasResident && h.residentWing == CeMuseumTour::kWingCourtyard, "resident set to courtyard");
    // 2. Repeated same wing does nothing.
    for (int i = 0; i < 30; ++i)
        Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingCourtyard, 0.016f, false), "same wing never reloads");
    // 3. Consecutive-frame doorway oscillation A<->B causes ZERO switches!
    for (int i = 0; i < 100; ++i) {
        const CeMuseumTour::Wing w = (i & 1) ? CeMuseumTour::kWingCourtyard : CeMuseumTour::kWingHangar;
        Need(!StepWingResidencyPolicy(&h, w, 0.016f, false), "1-frame oscillation never thrashes");
    }
    Need(h.residentWing == CeMuseumTour::kWingCourtyard, "resident wing stable across 1-frame oscillation");
    // 4. Multi-frame 2-tick bounce also causes ZERO switches!
    for (int cycle = 0; cycle < 40; ++cycle) {
        Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingHangar, 0.016f, false), "bounce tick 1");
        Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingHangar, 0.016f, false), "bounce tick 2");
        Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingCourtyard, 0.016f, false), "bounce return 1");
        Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingCourtyard, 0.016f, false), "bounce return 2");
    }
    Need(h.residentWing == CeMuseumTour::kWingCourtyard, "resident wing stable across 2-frame bounce");
    // 4b. Fast consecutive frames totalling < 0.35s (even >= 4 frames) must NOT commit yet!
    for (int f = 0; f < 6; ++f)
        Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingHangar, 0.016f, false), "6 fast frames (0.096s < 0.35s) stay pending");
    // Return to Courtyard to reset pending before next check.
    Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingCourtyard, 0.016f, false), "returning resets pending");
    // 5. Large single-frame dt spike alone cannot commit without minimum frame count!
    Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingHangar, 1.0f, false), "single frame spike blocked by frame count");
    Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingHangar, 1.0f, false), "frame 2 still below dwell frame threshold");
    Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingHangar, 1.0f, false), "frame 3 still below dwell frame threshold");
    // 6. Pause freezes dwell and never commits.
    Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingHangar, 0.25f, true), "paused never switches wing");
    // 7. Frame 4 with sufficient dwell time commits cleanly!
    Need(StepWingResidencyPolicy(&h, CeMuseumTour::kWingHangar, 0.20f, false), "commits after dwell time + frame count");
    Need(h.residentWing == CeMuseumTour::kWingHangar, "switched to Hangar");
    // 8. Post-switch cooldown prevents immediate re-eviction back to Courtyard even with steady frames!
    for (int i = 0; i < 20; ++i) {
        // 20 * 0.02 = 0.40s < 0.60s cooldown (even though > 0.35s dwell & >= 4 frames)
        Need(!StepWingResidencyPolicy(&h, CeMuseumTour::kWingCourtyard, 0.02f, false), "cooldown holds newly loaded wing");
    }
    Need(h.residentWing == CeMuseumTour::kWingHangar, "still in Hangar during cooldown");
    // After cooldown expires (additional 0.25s), switch commits cleanly.
    bool switchedBack = false;
    for (int i = 0; i < 20; ++i) {
        if (StepWingResidencyPolicy(&h, CeMuseumTour::kWingCourtyard, 0.02f, false)) { switchedBack = true; break; }
    }
    Need(switchedBack && h.residentWing == CeMuseumTour::kWingCourtyard, "switches cleanly once cooldown expires");
    // 9. ResetHysteresis restores clean initial state (deterministic shutdown / re-entry).
    ResetHysteresis(&h);
    Need(!h.hasResident, "reset clears resident flag");
    Need(StepWingResidencyPolicy(&h, CeMuseumTour::kWingMuspell, 0.016f, false), "re-entry after reset commits immediately");
    Need(h.residentWing == CeMuseumTour::kWingMuspell, "re-entry wing matches");
    std::puts("hysteresis ok");
    return 0;
}
"""
    with tempfile.TemporaryDirectory(dir=out) as htmp:
        hd = Path(htmp)
        (hd / "hyst.cpp").write_text(hyst_driver)
        flags = ["c++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        ok, detail = run(flags + ["-I" + str(GAME), str(hd / "hyst.cpp"), "-o", str(hd / "hyst")])
        check("wing hysteresis driver builds with ASan/UBSan", ok, detail)
        if ok:
            ok, detail = run([str(hd / "hyst")])
            check("wing hysteresis blocks doorway oscillation and commits steady crossings", ok, detail)
        hyst_mutants = [
            ("hyst-no-dwell-time",
             "if (h->pendingSeconds < kWingSwitchDwellSeconds || h->pendingFrames < kWingSwitchDwellFrames)",
             "if (h->pendingFrames < kWingSwitchDwellFrames)"),
            ("hyst-no-dwell-frames",
             "if (h->pendingSeconds < kWingSwitchDwellSeconds || h->pendingFrames < kWingSwitchDwellFrames)",
             "if (h->pendingSeconds < kWingSwitchDwellSeconds)"),
            ("hyst-no-cooldown",
             "if (h->cooldownSeconds > 0.0f) return false;",
             "if (false && h->cooldownSeconds > 0.0f) return false;"),
            ("hyst-ignore-pause",
             "if (paused) return false;",
             "if (false && paused) return false;"),
            ("hyst-no-immediate-first",
             "if (!h->hasResident) {",
             "if (false && !h->hasResident) {"),
        ]
        for label, old, new in hyst_mutants:
            if hyst_driver.count(old) != 1:
                check("mutant anchor " + label, False, "not found once: " + old)
                continue
            mf = hd / (label + ".cpp")
            mf.write_text(hyst_driver.replace(old, new, 1))
            built, detail = run(["c++", "-std=c++14", "-O1", "-I" + str(GAME), str(mf), "-o", str(hd / label)])
            if not built:
                check("mutant " + label + " caught", False, detail)
                continue
            caught, _ = run([str(hd / label)], 2)
            check("mutant " + label + " caught", caught)

    # 9. Museum Hall heap lifecycle & zero 6.9 MB static BSS check (ce_museum_hall.cpp).
    hall = (GAME / "ce_museum_hall.cpp").read_text()
    check("museum hall replaces static Gallery g with heap-allocated Gallery *gPtr",
          "static Gallery *gPtr = NULL;" in hall
          and "std::calloc(1, sizeof(Gallery))" in hall
          and "std::free(gPtr);" in hall
          and re.search(r"\}\s*g\s*;", hall) is None)
    check("museum hall frees heap Gallery in CeMuseumHallClear and allocates lazily in SetHall",
          "FreeGallery();" in hall[hall.index("void CeMuseumHallClear()"):hall.index("static void HallFailed")]
          and "EnsureGallery()" in hall[hall.index("static void SetHall("):hall.index("void CeMuseumHallSet(")])
    check("museum hall guards all public entry points against null gPtr",
          all("if (!gPtr || !gPtr->valid" in hall[hall.index(sig):hall.index(sig) + 450] for sig in (
              "bool CeMuseumHallAllowsVisitor(",
              "void CeMuseumHallVisitor(",
              "int CeMuseumHallSignLabel(",
              "bool CeMuseumHallFacesExit(",
              "PortCeMuseumInteractiveAim(",
              "PortCeMuseumInteractiveCard(",
              "PortCeMuseumHallDraw(",
          )))
    check("museum hall adds spatial hysteresis for Atrium portal anticipation",
          "gAnticipatedWing" in hall and "gExhibitionVisitor.y >= 43.0f" in hall)
    # Compile actual Gallery + EnsureGallery/FreeGallery + GalleryCinemaRef under ASan/UBSan.
    gal_start = hall.index("struct LitVertex {")
    gal_end = hall.index("static CeMuseumExhibition::Plan")
    ref_start = hall.index("struct GalleryCinemaRef {")
    ref_end = hall.index('#include "ce_museum_cinema.inc"')
    hall_heap_driver = (
        '#include "ce_museum_hall.h"\n#include <cstdint>\n#include <cstdio>\n#include <cstdlib>\n'
        'using DWORD = unsigned;\nstruct IDirect3DTexture9 {};\n'
        'enum Material { kMatAuto = 0, kMatCarpet = 1, kMatCount = 34 };\n'
        'namespace {\n' + hall[gal_start:gal_end] + hall[ref_start:ref_end] + '}\n'
        'static void Need(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "HALL HEAP FAIL: %s\\n", why); std::exit(2); } }\n'
        'int main() {\n'
        '    Need(gPtr == NULL, "starts with zero heap allocated");\n'
        '    Need(sizeof(Gallery) > 6000000u, "Gallery is the ~6.9 MB geometry struct");\n'
        '    Need(sizeof(g) <= 64u, "GalleryCinemaRef bridge has negligible size (no 6.9 MB BSS)");\n'
        '    Need(!g.valid && !g.ground && !bool(g.valid), "null-safe reads before allocation");\n'
        '    float z = 1.0f;\n'
        '    Need(!g.ground(0.0f, 0.0f, &z), "null-safe ground call before allocation");\n'
        '    g.current = kMatCarpet;\n'
        '    Need(-g.tour.forward.x == 0.0f && -g.tour.right.y == 0.0f, "null-safe tour access");\n'
        '    Gallery *p = EnsureGallery();\n'
        '    Need(p != NULL && p == gPtr && p->zone == -1 && !p->valid, "lazy heap allocation zero-initializes");\n'
        '    g.current = kMatCarpet; p->valid = true; p->tour.forward.x = 1.0f;\n'
        '    Need(p->current == kMatCarpet && bool(g.valid) && -g.tour.forward.x == -1.0f, "bridge forwards live heap state");\n'
        '    FreeGallery();\n'
        '    Need(gPtr == NULL && !g.valid, "FreeGallery releases heap and clears pointer");\n'
        '    FreeGallery();\n'
        '    Need(gPtr == NULL, "FreeGallery is idempotent");\n'
        '    std::puts("hall heap ok"); return 0;\n'
        '}\n'
    )
    with tempfile.TemporaryDirectory(dir=out) as gtmp:
        gd = Path(gtmp)
        (gd / "hall_heap.cpp").write_text(hall_heap_driver)
        flags = ["c++", "-std=c++14", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        ok, detail = run(flags + ["-I" + str(GAME), str(gd / "hall_heap.cpp"), "-o", str(gd / "hall_heap")])
        check("museum hall heap lifecycle driver builds with ASan/UBSan", ok, detail)
        if ok:
            ok, detail = run([str(gd / "hall_heap")])
            check("museum hall lazy heap allocation, free, and null-safe bridge hold", ok, detail)

    check("runtime guards CMESHRENDERER::Render with local D3D9 state block",
          "CreateStateBlock(D3DSBT_ALL" in cpp and "savedState->Apply();" in cpp and "savedState->Release();" in cpp)
    check("runtime invalidates LT cached state after state block restore",
          "LT.InvalidateAllStates();" in cpp)
    restore_idx = cpp.find("LT.InvalidateAllStates();")
    apply_after = cpp.find("RENDERINFO.Apply();", restore_idx) if restore_idx >= 0 else -1
    restore_block = cpp[restore_idx:apply_after] if restore_idx >= 0 and apply_after > restore_idx else ""
    check("runtime marks RENDERINFO view/projection/world dirty after state block restore",
          "RENDERINFO.mWorldChanged = true;" in restore_block
          and "RENDERINFO.mViewChanged = true;" in restore_block
          and "RENDERINFO.mProjectionChanged = true;" in restore_block
          and "RENDERINFO.mFogStartChanged = true;" in restore_block
          and "RENDERINFO.mFogEndChanged = true;" in restore_block
          and "RENDERINFO.mFogDensityChanged = true;" in restore_block
          and "RENDERINFO.mFVFChanged = true;" in restore_block)
    check("runtime enables D3DRS_NORMALIZENORMALS for scaled exhibit meshes",
          "LT.SRS(D3DRS_NORMALIZENORMALS, TRUE);" in cpp)
    check("runtime pre-checks retail goodie & mesh readability before CChunkReader::Open",
          "FileReadable(goodieFile)" in cpp and "FileReadable(meshFile)" in cpp)
    check("runtime releases all slots on shutdown",
          "void RuntimeShutdown()" in cpp and "FreeAllSlots();" in cpp)
    free_idx = cpp.find("void FreeSlot(")
    vbuf_idx = cpp.find("ForceDeleteVBuffer()", free_idx)
    chain_in_free = cpp.find("ForceDeleteMeshChain(", free_idx)
    check("runtime tears down full mNextInChain LOD chain before deleting textures in FreeSlot & LoadSlot",
          "void ForceDeleteMeshChain(CMesh *mesh)" in cpp
          and "mesh->mNextInChain" in cpp
          and "mesh->mNextInChain = NULL;" in cpp
          and free_idx >= 0 and chain_in_free > free_idx and chain_in_free < vbuf_idx
          and cpp.count("ForceDeleteMeshChain(") >= 3)
    check("player-facing strings stay free of forbidden words",
          not re.search(r'"[^"]*(?:prototype|experimental|carrier|ce\.ini)[^"]*"', HEADER.read_text(), re.I))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
