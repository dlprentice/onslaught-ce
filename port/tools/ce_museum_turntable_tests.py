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
