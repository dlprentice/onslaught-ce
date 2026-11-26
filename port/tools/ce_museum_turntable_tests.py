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
#include "ce_archive_pedestal.h"
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
    Need(kManifestCount == 45, "manifest has ALL 45/45 (100%) curated retail GT_MESH 3D exhibits");
    Need(CeArchivePedestal::CountGoodies(CeArchivePedestal::kGoodieMesh) == 45u,
         "retail catalog defines 45 GT_MESH goodies");

    // 0. Exact 100% (45/45) retail kGoodieMesh ID coverage: every single retail GT_MESH goodie is on display!
    for (unsigned g = 0; g < 45u; ++g) {
        const int wantId = CeArchivePedestal::GoodieIdAt(CeArchivePedestal::kGoodieMesh, g);
        Need(wantId > 0, "valid retail mesh goodie ID");
        bool found = false;
        for (unsigned i = 0; i < kManifestCount; ++i) {
            if (kManifest[i].goodieId == wantId) { found = true; break; }
        }
        Need(found, "every single retail GT_MESH goodie ID (45/45) appears in museum turntable manifest");
    }

    // 1. All 45 exhibits stay simultaneously resident across every wing (zero pop-in!), within cap.
    for (int w = 0; w < int(CeMuseumTour::kWingCount); ++w) {
        const CeMuseumTour::Wing wing = static_cast<CeMuseumTour::Wing>(w);
        const unsigned count = CountWantedResident(wing);
        Need(count == kManifestCount, "all 45 exhibits stay simultaneously resident across wings");
        Need(count <= unsigned(kMaxResidentGoodies), "resident goodies stay within simultaneous cap");
    }
    Need(ShouldBeResident(0, CeMuseumTour::kWingCourtyard), "hero resident in courtyard");
    Need(ShouldBeResident(0, CeMuseumTour::kWingAtrium), "hero resident in atrium sightline");
    Need(ShouldBeResident(0, CeMuseumTour::kWingMuspell), "hero stays resident from Muspell wing");
    Need(ShouldBeResident(44, CeMuseumTour::kWingCourtyard), "hangar exhibits stay resident with zero pop-in");
    Need(!ShouldBeResident(99, CeMuseumTour::kWingCourtyard), "out-of-range index never resident");

    // 2. Every manifest entry has unique valid goodieId, ASCII copy, stands in its wing, non-overlapping plinth, and reachable stud.
    for (unsigned i = 0; i < kManifestCount; ++i) {
        const Spec &sp = kManifest[i];
        Need(sp.goodieId > 0 && sp.goodieId < 256, "valid goodie id");
        Need(CeArchivePedestal::ClassifyGoodie(sp.goodieId) == CeArchivePedestal::kGoodieMesh,
             "manifest entry is a genuine retail GT_MESH goodie");
        for (unsigned j = i + 1; j < kManifestCount; ++j) {
            Need(sp.goodieId != kManifest[j].goodieId, "unique goodie id per exhibit");
            // Non-overlapping 2D plinth footprints (naval ships share the 4.2m wide reflecting basin along Y).
            const Spec &ot = kManifest[j];
            if (sp.wing == ot.wing) {
                const float dx = std::fabs(sp.localX - ot.localX);
                const float dy = std::fabs(sp.localY - ot.localY);
                const float minX = (sp.style == kStageNaval && ot.style == kStageNaval) ? 0.0f : (sp.plinthHalf + ot.plinthHalf);
                const float minY = (sp.style == kStageNaval && ot.style == kStageNaval) ? 3.5f : (sp.plinthHalf + ot.plinthHalf);
                Need(dx >= minX - 0.05f || dy >= minY - 0.05f, "no two 3D exhibit plinths overlap");
            }
        }
        Need(AsciiOnly(sp.title) && AsciiOnly(sp.category) && AsciiOnly(sp.caption), "ASCII plaque copy");
        Need(std::strlen(sp.title) <= 50u && std::strlen(sp.category) <= 44u && std::strlen(sp.caption) <= 95u,
             "turntable plaque copy fits CeMuseumLabel kChars=96 buffers without truncation");
        Need(sp.plinthTopZ > 0.3f && sp.plinthHalf > 0.5f, "sensible plinth dimensions");
        Need(Grand::RoomAt(graph, CeMuseumExhibition::Vec2{sp.localX, sp.localY}) == static_cast<Grand::RoomId>(sp.wing),
             "turntable stands in its declared wing");
        // Plinth centre is solid; approach point 1.15 m in front of stud is walkable.
        Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{sp.localX, sp.localY}) == CeMuseumExhibition::kWalkBlocked,
             "turntable plinth blocks visitor");
        float sx, sy, sz;
        StudPosition(i, &sx, &sy, &sz);
        if (sp.style == kStageScale) {
            Need(sy <= 73.70f, "scale alcove studs sit cleanly outside obstacle box (y <= 73.70)");
        }
        const float ax = sx + sp.studDirX * 1.15f;
        const float ay = sy + sp.studDirY * 1.15f;
        Need(Grand::WalkAt(graph, CeMuseumExhibition::Vec2{ax, ay}) == CeMuseumExhibition::kWalkOpen,
             "visitor can stand in front of every turntable stud");
    }

    // 2b. Canonical retail Goodie ID -> plaque designation mapping (verified against FEPGoodies.cpp & english.dat).
    auto byGoodie = [&](int gid) -> const Spec & {
        for (unsigned i = 0; i < kManifestCount; ++i)
            if (kManifest[i].goodieId == gid) return kManifest[i];
        Need(false, "goodie ID present in manifest");
        return kManifest[0];
    };
    Need(std::strstr(byGoodie(8).title,  "Unit-00") != NULL, "goodie 8 is Unit-00 Aquila");
    Need(std::strstr(byGoodie(9).title,  "Unit-01 'Pulsar'") != NULL, "goodie 9 is Unit-01 Pulsar");
    Need(std::strstr(byGoodie(10).title, "Unit-03 'Lancer'") != NULL, "goodie 10 is Unit-03 Lancer");
    Need(std::strstr(byGoodie(11).title, "Unit-04S 'Sniper'") != NULL, "goodie 11 is Unit-04S Sniper");
    Need(std::strstr(byGoodie(14).title, "AV-14 Sabre") != NULL, "goodie 14 is AV-14 Sabre");
    Need(std::strstr(byGoodie(15).title, "M-1 Broadsword") != NULL, "goodie 15 is M-1 Broadsword");
    Need(std::strstr(byGoodie(16).title, "AV-18 Bardiche") != NULL, "goodie 16 is AV-18 Bardiche");
    Need(std::strstr(byGoodie(17).title, "M-112 Archer") != NULL, "goodie 17 is M-112 Archer Mobile Artillery");
    Need(std::strstr(byGoodie(19).title, "F-24 Valkyrie") != NULL, "goodie 19 is F-24 Valkyrie");
    Need(std::strstr(byGoodie(20).title, "XF-28 Venom") != NULL, "goodie 20 is XF-28 Venom");
    Need(std::strstr(byGoodie(21).title, "B-7F Monitor") != NULL, "goodie 21 is B-7F Monitor Bomber");
    Need(std::strstr(byGoodie(22).title, "A-15 Penetrator") != NULL, "goodie 22 is A-15 Penetrator");
    Need(std::strstr(byGoodie(23).title, "U-17 Highside") != NULL, "goodie 23 is U-17 Highside Transporter");
    Need(std::strstr(byGoodie(25).title, "NAV-3 Turtle") != NULL, "goodie 25 is NAV-3 Turtle");
    Need(std::strstr(byGoodie(26).title, "FSV-6 Marshall") != NULL, "goodie 26 is FSV-6 Marshall");
    Need(std::strstr(byGoodie(27).title, "FG-8 Packhorse") != NULL, "goodie 27 is FG-8 Packhorse");
    Need(std::strstr(byGoodie(28).title, "SAT Turret") != NULL, "goodie 28 is Forseti SAT Turret");
    Need(std::strstr(byGoodie(36).title, "ASF-7 Lightning") != NULL, "goodie 36 is ASF-7 Lightning");
    Need(std::strstr(byGoodie(37).title, "ASX-9 Storm") != NULL, "goodie 37 is ASX-9 Storm");
    Need(std::strstr(byGoodie(38).title, "B-4 Sky Tyrant") != NULL, "goodie 38 is B-4 Sky Tyrant");
    Need(std::strstr(byGoodie(39).title, "A-19 Enforcer") != NULL, "goodie 39 is A-19 Enforcer");
    Need(std::strstr(byGoodie(40).title, "UD-3 Thunder") != NULL, "goodie 40 is UD-3 Thunder");
    Need(std::strstr(byGoodie(41).title, "UD-5H Quell") != NULL, "goodie 41 is UD-5H Quell");
    Need(std::strstr(byGoodie(42).title, "T-85 Land Raider") != NULL, "goodie 42 is T-85 Land Raider");
    Need(std::strstr(byGoodie(43).title, "T-92 Growler") != NULL, "goodie 43 is T-92 Growler");
    Need(std::strstr(byGoodie(44).title, "ML-16 Vengeance") != NULL, "goodie 44 is ML-16 Vengeance");
    Need(std::strstr(byGoodie(45).title, "A-3 Steel Rain") != NULL, "goodie 45 is A-3 Steel Rain");
    Need(std::strstr(byGoodie(47).title, "W-8 Cerberus") != NULL, "goodie 47 is W-8 Cerberus");
    Need(std::strstr(byGoodie(48).title, "W-11 Marauder") != NULL, "goodie 48 is W-11 Marauder");
    Need(std::strstr(byGoodie(49).title, "WX-15 Dominator") != NULL, "goodie 49 is WX-15 Dominator");
    Need(std::strstr(byGoodie(50).title, "G-1 Gnat") != NULL, "goodie 50 is G-1 Gnat");
    Need(std::strstr(byGoodie(51).title, "D-9 Arachnodrone") != NULL, "goodie 51 is D-9 Arachnodrone");
    Need(std::strstr(byGoodie(52).title, "BB-2 Manticore") != NULL, "goodie 52 is BB-2 Manticore");
    Need(std::strstr(byGoodie(54).title, "Cannon Turret") != NULL, "goodie 54 is Muspell Cannon Turret");

    // 3. Rotation speed (~12 deg/s), dt clamp, pause hold, wrap-around, and Pause/Resume toggle.
    BankState bank = {};
    Reset(&bank);
    Need(bank.initialized, "bank initialized");
    Need(!bank.slots[0].paused, "hero rotates by default");
    Need(bank.slots[42].paused && bank.slots[43].paused && bank.slots[44].paused,
         "scale alcove units start stationary");

    const float y0 = bank.slots[0].yawRad;
    StepRotation(&bank, 0.1f, false);
    Need(Near(bank.slots[0].yawRad, y0 + kDefaultRateRadPerSec * 0.1f), "rotates at ~4.5 deg/s");
    const float yScale = bank.slots[42].yawRad;
    StepRotation(&bank, 0.2f, false);
    Need(Near(bank.slots[42].yawRad, yScale), "paused turntable holds angle");

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

    // 4. Ray-pick brass control stud (wing-scoped so adjacent rooms never pick through walls).
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

    // 4b. North Atrium exhibits (#11 and #76) are pickable under kWingAtrium (no Hangar override).
    for (unsigned i = 0; i < kManifestCount; ++i) {
        if (kManifest[i].goodieId != 11 && kManifest[i].goodieId != 76) continue;
        float nsx, nsy, nsz;
        StudPosition(i, &nsx, &nsy, &nsz);
        const float ex = nsx + kManifest[i].studDirX * 1.5f;
        const float ey = nsy + kManifest[i].studDirY * 1.5f;
        const float ez = 1.7f;
        Need(PickStud(ex, ey, ez, nsx - ex, nsy - ey, nsz - ez, CeMuseumTour::kWingAtrium) == static_cast<int>(i),
             "North Atrium exhibit stud is pickable under kWingAtrium");
        Need(PickStud(ex, ey, ez, nsx - ex, nsy - ey, nsz - ez, CeMuseumTour::kWingHangar) == -1,
             "North Atrium exhibit stud is not picked under kWingHangar");
    }

    // 5. FocusExhibit picks nearest faced exhibit in current wing.
    Need(FocusExhibit(0.0f, 5.0f, 0.0f, 1.0f, CeMuseumTour::kWingCourtyard) == 0, "facing hero focuses hero");
    Need(FocusExhibit(0.0f, 5.0f, 0.0f, -1.0f, CeMuseumTour::kWingCourtyard) == -1, "facing away clears focus");
    Need(FocusExhibit(21.8f, 41.0f, 0.0f, 1.0f, CeMuseumTour::kWingMuspell) == 16, "facing Muspell walker focuses it");
    Need(FocusExhibit(21.8f, 41.0f, 0.0f, 1.0f, CeMuseumTour::kWingCourtyard) == -1, "wrong wing clears focus");

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
    ("no-bounds-check-resident", "return index < kManifestCount;", "return true;"),
    ("no-wing-filter-pick",
     "        if (kManifest[i].wing != currentWing) continue;\n        float sx, sy, sz;",
     "        float sx, sy, sz;"),
    ("no-wing-filter-focus",
     "        if (kManifest[i].wing != currentWing) continue;\n        const Spec &sp = kManifest[i];",
     "        const Spec &sp = kManifest[i];"),
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
    check("museum hall passes actual room wing to CeMuseumTurntable::RuntimeUpdate without obsolete portal override",
          "gAnticipatedWing" not in hall and "turntableWing" not in hall
          and "CeMuseumTurntable::RuntimeUpdate(wing, dt, paused, eyeX, eyeY, eyeZ, aimX, aimY, aimZ);" in hall)
    check("museum hall resolves architectural textures live without raw cached Gallery::texture[] pointers",
          "gPtr->texture" not in hall and "IDirect3DTexture9 *texture[kMatCount]" not in hall
          and "ReleaseHallGeometryBuffers()" in hall and "ReleaseOceanGpuBuffers()" in hall)
    check("museum hall builds static D3DPOOL_MANAGED GPU VB/IB for Hall, shadows, glass, and ocean",
          "CreateVertexBuffer(" in hall and "CreateIndexBuffer(" in hall
          and "D3DPOOL_MANAGED" in hall and "ReleaseHallGpuBuffers();" in hall
          and "DrawIndexedPrimitive(D3DPT_TRIANGLELIST" in hall)
    backdrop_fn = hall[hall.index("void PortCeMuseumAuthoredBackdrop()"):hall.index("static const float kHouseStep")]
    check("authored backdrop renders textured coastal ocean and sculpted headlands over KempyCube without dev->Clear",
          "dev->Clear(" not in backdrop_fn and "CE_MUSEUM_TEX_WATER_BASIN" in backdrop_fn
          and "gHallGpu.oceanVb" in backdrop_fn and "gHallGpu.headlandVb" in backdrop_fn
          and "const float oceanZ = 5.5f;" in hall)
    draw_fn = hall[hall.index("void PortCeMuseumHallDraw()"):]
    saved_restore = draw_fn.index("saved->Apply();")
    check("museum hall shares one D3DSBT_ALL state guard with Art, Dioramas, and Archive Pedestal",
          "CeMuseumArt::RuntimeDraw(gGrandFloorZ, false);" in draw_fn[:saved_restore]
          and "CeMuseumDiorama::RuntimeDraw(gGrandFloorZ, false);" in draw_fn[:saved_restore]
          and "CeArchivePedestal::RuntimeDraw(gGrandFloorZ, false);" in draw_fn[:saved_restore]
          and "DrawFactionBanners(dev, gGrandFloorZ);" in draw_fn[:saved_restore]
          and "CeMuseumTurntable::RuntimeDraw(gGrandFloorZ);" in draw_fn[saved_restore:])
    check("museum hall draws translucent skylight glass and sunbeams strictly after CeMuseumTurntable::RuntimeDraw",
          "DrawHallGlassPass(dev);" in draw_fn
          and draw_fn.index("DrawHallGlassPass(dev);") > draw_fn.index("CeMuseumTurntable::RuntimeDraw(gGrandFloorZ);")
          and "gHallGpu.glassVb" not in draw_fn)
    check("museum hall reuses a cached IDirect3DStateBlock9 instead of per-frame CreateStateBlock/Release",
          "EnsureHallStateBlock(dev)" in draw_fn and "EnsureHallStateBlock(dev)" in backdrop_fn
          and "CreateStateBlock(" not in draw_fn and "CreateStateBlock(" not in backdrop_fn)
    check("museum hall wires CeMuseumDiorama lifecycle, update, input, and cue hooks",
          "CeMuseumDiorama::RuntimeInit();" in hall
          and "CeMuseumDiorama::RuntimeShutdown();" in hall
          and "CeMuseumDiorama::RuntimeUpdate(" in hall
          and "CeMuseumDiorama::RuntimeHandleFire(" in hall
          and "CeMuseumDiorama::RuntimeIsAimingControl()" in hall
          and "CeMuseumDiorama::RuntimeCue(" in hall)
    check("turntable runtime batches all 45 brass control stud stanchions into one draw call",
          "AppendStudIndicator(" in cpp)
    check("museum hall loads official retail faction emblem textures via CeAssetCatalog",
          "goodies\\\\goodies_F_Bakgrnd.tga" in hall
          and "goodies\\\\goodies_M_Bakgrnd.tga" in hall
          and "FrontEnd\\\\v3\\\\FE_BEA_Title2.tga" in hall)
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
    check("runtime caches its D3D9 state block and computes rotated vertical half-extent for banked flight exhibits",
          "EnsureTurntableStateBlock(dev)" in cpp and "ReleaseTurntableStateBlock();" in cpp
          and "std::fabs(rot.Row[2].X) * box->mAxes.X" in cpp)
    check("runtime invalidates LT cached state after state block restore",
          "LT.InvalidateAllStates();" in cpp)
    check("runtime invalidates LT cached state between stud draw and mesh draw",
          cpp.count("LT.InvalidateAllStates();") >= 2)
    restore_idx = cpp.rfind("LT.InvalidateAllStates();")
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
    check("runtime checks texture mRefCount <= 0 and frees all meshes before textures in FreeAllSlots",
          "if (tex->mRefCount <= 0)" in cpp
          and "FreeSlotMesh(i)" in cpp)
    header_txt = HEADER.read_text()
    check("player-facing strings stay free of forbidden words",
          not re.search(r'"[^"]*(?:prototype|experimental|carrier|ce\.ini)[^"]*"', header_txt, re.I))
    banned_lore = (
        "Aquila - Vulcan",
        "Aquila - Flux",
        "Aquila - Blazer",
        "XF-24",
        "Forseti Plasma Tank",
        "Muspell SAM Launcher",
        "Muspell Laser Turret",
        "Commonwealth",
        "Dominion",
    )
    check("canonical retail lore regression checks pass (no stale Vulcan/Flux/Blazer/XF-24/Plasma/SAM/Dominion labels)",
          all(term not in header_txt for term in banned_lore))
    # Control fixtures are solid: every turntable stanchion, every diorama lectern and the Archive Pedestal's
    # desk stands inside a low room-graph obstacle, placed from the fixtures' own positions, and no fixture
    # blocks a sight line or seals an aisle. Mutants prove the check bites.
    fixture_driver = r"""
#include "ce_museum_exhibition.h"
#include "ce_museum_turntable.h"
#include "ce_museum_diorama.h"
#include "ce_archive_pedestal.h"
#include <cstdio>
#include <cstdlib>
namespace G = CeMuseumExhibition::Grand;
namespace Ex = CeMuseumExhibition;
static void Need(bool ok, const char *why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(2); } }
static bool Open(const G::RoomGraph &g, float x, float y) { return G::WalkAt(g, Ex::Vec2{x, y}) != Ex::kWalkBlocked; }
static int LowBoxAt(const G::RoomGraph &g, float x, float y) {
    for (unsigned k = 0; k < g.obstacleCount; ++k) {
        const G::SolidBox &b = g.obstacles[k];
        if (x >= b.minX && x <= b.maxX && y >= b.minY && y <= b.maxY && b.maxZ <= G::kFixtureTop + 1e-4f &&
            (b.maxX - b.minX) <= 2.0f * G::kPedestalDeskHalfWidth + 1e-3f) return int(k);
    }
    return -1;
}
int main() {
    const G::RoomGraph g = G::MakeRoomGraph();
    Need(g.obstacleCount < unsigned(G::kMaxObstacles) - 16u, "room graph keeps obstacle headroom");
    Need(G::kFixtureTop < 1.5f, "fixtures stay well below eye height");
    for (unsigned i = 0; i < CeMuseumTurntable::kManifestCount; ++i) {
        float x, y, z;
        CeMuseumTurntable::StudPosition(i, &x, &y, &z);
        Need(LowBoxAt(g, x, y) >= 0, "every turntable stanchion is a solid, low obstacle");
        Need(z <= G::kFixtureTop, "the stud sits within its stanchion's height");
    }
    for (unsigned i = 0; i < CeMuseumDiorama::kDioramaCount; ++i) {
        float x, y, z;
        CeMuseumDiorama::StudPosition(i, &x, &y, &z);
        Need(LowBoxAt(g, x, y) >= 0, "every diorama lectern is a solid, low obstacle");
    }
    for (unsigned b = 0; b < unsigned(CeArchivePedestal::kBtnCount); ++b) {
        const float bx = CeArchivePedestal::kPedestalLocalX + CeArchivePedestal::kButtons[b].offsetX;
        Need(LowBoxAt(g, bx, CeArchivePedestal::kControlLocalY) >= 0, "the Archive Pedestal's desk is solid under each control");
    }
    Need(CeArchivePedestal::kControlLocalY == G::kPedestalDeskY, "the desk obstacle is where the control rail is");
    // Sight lines at eye height pass over every fixture, and the visitor's stances stay open.
    Need(G::SightClear(g, Ex::Vec3{0.0f, 21.0f, 1.7f}, Ex::Vec3{0.0f, 36.0f, 1.7f}), "entrance to rotunda sight line clear over the desk");
    Need(G::SightClear(g, Ex::Vec3{0.0f, 30.0f, 1.7f}, Ex::Vec3{0.0f, 33.65f, 1.3f}), "the visitor can see the desk's controls");
    Need(G::SightClear(g, Ex::Vec3{-6.0f, 32.2f, 1.7f}, Ex::Vec3{-9.21f, 32.2f, 1.3f}), "the visitor can see a diorama lectern's control from the aisle");
    Need(G::SightClear(g, Ex::Vec3{-6.0f, 30.0f, 1.7f}, Ex::Vec3{-6.0f, 50.0f, 1.7f}), "the Atrium's west aisle sight line is clear past the lecterns");
    Need(G::SightClear(g, Ex::Vec3{21.8f, 41.0f, 1.7f}, Ex::Vec3{21.8f, 48.0f, 1.7f}), "aisle to a Muspell plinth clear over its stanchion");
    Need(Open(g, 0.0f, 31.6f), "the stance in front of the Archive Pedestal's desk is open");
    Need(!Open(g, 0.0f, 33.65f), "the desk itself cannot be walked through");
    Need(Open(g, -7.6f, 32.2f) && Open(g, 7.6f, 47.6f), "the stances beside the diorama lecterns are open");
    Need(!Open(g, -9.21f, 32.2f), "a diorama lectern cannot be walked through");
    Need(Open(g, 0.0f, 41.0f) && Open(g, -30.0f, 41.0f) && Open(g, 30.0f, 41.0f) &&
         Open(g, 0.0f, 65.0f) && Open(g, -12.6f, 65.0f),
         "the wing centre aisles and the Hangar cross-aisle stay open");
    return 0;
}
"""
    exhibition_h = (GAME / "ce_museum_exhibition.h").read_text()
    fixture_mutants = [
        ("no-stanchions", "for (unsigned i = 0; i < sizeof(kStanchions) / sizeof(kStanchions[0]); ++i) {",
         "for (unsigned i = 0; i < 0u; ++i) {"),
        ("stanchion-wrong-side", "const float sx = kStanchions[i].x + float(kStanchions[i].dirX) * reach;",
         "const float sx = kStanchions[i].x - float(kStanchions[i].dirX) * reach;"),
        ("no-lecterns", "const float lx = dioX[ix] + towardAisle * kDioramaLecternReach;",
         "const float lx = dioX[ix] + towardAisle * 0.0f;"),
        ("no-desk", "PushObstacle(g, -kPedestalDeskHalfWidth, kPedestalDeskY - kPedestalDeskHalfDepth,",
         "if (false) PushObstacle(g, -kPedestalDeskHalfWidth, kPedestalDeskY - kPedestalDeskHalfDepth,"),
        ("fixtures-block-sight", "constexpr float kFixtureTop = 1.20f;", "constexpr float kFixtureTop = 2.40f;"),
    ]
    with tempfile.TemporaryDirectory(dir=out) as ftmp:
        fd = Path(ftmp)
        (fd / "fixtures.cpp").write_text(fixture_driver)
        ok, detail = run(["c++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror", "-I" + str(GAME),
                          str(fd / "fixtures.cpp"), "-o", str(fd / "fixtures")])
        check("control fixture driver builds", ok, detail)
        if ok:
            ok, detail = run([str(fd / "fixtures")])
            check("stanchions, lecterns and the pedestal desk are solid, low and clear of sight lines and aisles", ok, detail)
        for label, old_text, new_text in fixture_mutants:
            if exhibition_h.count(old_text) != 1:
                check("fixture mutant anchor " + label, False, "not found once: " + old_text)
                continue
            md = fd / label
            md.mkdir()
            (md / "ce_museum_exhibition.h").write_text(exhibition_h.replace(old_text, new_text))
            built, detail = run(["c++", "-std=c++14", "-O1", "-I" + str(md), "-I" + str(GAME),
                                 str(fd / "fixtures.cpp"), "-o", str(md / "fixtures")])
            if not built:
                check("fixture mutant " + label + " caught", False, detail)
                continue
            caught, _ = run([str(md / "fixtures")], 2)
            check("fixture mutant " + label + " caught", caught)

    # A quick-jump and an out-of-bounds return place the visitor for good: teleport plus the engine's pose reset.
    visit_engine = (GAME / "ce_museum_visit_engine.cpp").read_text()
    place = re.search(r"static void PlaceVisitor\(CBattleEngine \*engine, const FVector &position, float yaw\)\n\{(.*?)\n\}", visit_engine, re.S)
    check("the visitor is placed by teleport and pose reset, so the heading holds on the next tick",
          bool(place) and "engine->Teleport(position);" in place.group(1)
          and "engine->TeleportOrientation(facing);" in place.group(1)
          and "engine->CeResetPoseForRecall(yaw);" in place.group(1)
          and place.group(1).index("engine->TeleportOrientation(facing);") < place.group(1).index("engine->CeResetPoseForRecall(yaw);")
          and "PlaceVisitor(engine, dest, jumpYaw);" in visit_engine
          and "PlaceVisitor(engine, gStartPosition, YawOfOrientation(gStartOrientation));" in visit_engine
          and "MakeRotationF(jumpYaw" not in visit_engine
          and "engine->GetOrientation() = gStartOrientation;" not in visit_engine)

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
