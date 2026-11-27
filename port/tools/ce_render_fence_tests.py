#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render fence shadow-manager save/restore unit, patch, hygiene, and causal mutant tests.

Verifies without game assets or GPU:
1. Patch 0353-ce-render-pass-fence.patch applies cleanly with zero fuzz, wraps every added line in
   #ifdef CE_VERSION (leaving the faithful build byte-identical), and invokes PortCeCmdRenderWorld
   at the start of CDXEngine::Render.
2. PortCeCmdRenderWorld guards against re-entry, gMirrorPass, non-zero viewpoint, and inactive
   Commander view (open or eased return), runs CeRenderFenceSave/Restore around the view pass,
   and suspends world-view snapshot capture during the unseen cockpit pass.
3. The exact production shadow save/restore block (CE_RENDER_FENCE_SHADOWS_BEGIN..END) compiled
   against minimal authored CShadows/CActiveReader/CMonitor stubs restores mRequests, mTexInfo,
   mFree, mNumFree, and mUnknown590 while preserving CMonitor deletion-callback lists (no dangling
   pointers on saved units, no lingering callbacks on view-pass units).
4. Causal mutants of the shadow save/restore (dropped restore, dropped requests, dropped texInfo,
   dropped free list, and raw memcpy bypassing SetReader) are all caught.
5. Public builds compile zero getenv/BEA_PORT_* switches in ce_render_fence.cpp and zero bisect or
   measurement switches in ce_cmdmap.cpp.

    python3 port/tools/ce_render_fence_tests.py
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FENCE_CPP = ROOT / "port/game/ce_render_fence.cpp"
CMDMAP_CPP = ROOT / "port/game/ce_cmdmap.cpp"
PATCH = ROOT / "port/patches/0353-ce-render-pass-fence.patch"

sys.path.insert(0, str(ROOT / "port/tools"))
from ce_commander_tests import comments_off, without_test  # noqa: E402
from ce_photo_tests import faithful, patched  # noqa: E402

BEGIN_MARKER = "// CE_RENDER_FENCE_SHADOWS_BEGIN"
END_MARKER = "// CE_RENDER_FENCE_SHADOWS_END"

HARNESS = r"""
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using SINT = int;
using UBYTE = unsigned char;

struct FVector {
    float X = 0.f, Y = 0.f, Z = 0.f;
    bool operator==(const FVector& o) const { return X == o.X && Y == o.Y && Z == o.Z; }
};

struct CGenericActiveReader;
struct CMonitor {
    std::vector<CGenericActiveReader*> readers;
    void AddDeletionEvent(CGenericActiveReader* r) { readers.push_back(r); }
    void RemoveDeletionEvent(CGenericActiveReader* r) {
        for (auto it = readers.begin(); it != readers.end(); ++it) {
            if (*it == r) { readers.erase(it); return; }
        }
    }
    void Die();
};

struct CGenericActiveReader {
    CMonitor* mToRead = nullptr;
    void SetReader(CMonitor* to_read) {
        if (to_read == mToRead) return;
        if (mToRead) mToRead->RemoveDeletionEvent(this);
        mToRead = to_read;
        if (mToRead) mToRead->AddDeletionEvent(this);
    }
    void ToReadDied() { mToRead = nullptr; }
};

void CMonitor::Die() {
    auto copy = readers;
    readers.clear();
    for (auto* r : copy) r->ToReadDied();
}

struct CUnit : CMonitor {
    int id = 0;
};

template <class T>
struct CActiveReader : CGenericActiveReader {
    void SetReader(T* to_read) { CGenericActiveReader::SetReader(static_cast<CMonitor*>(to_read)); }
    T* ToRead() { return static_cast<T*>(mToRead); }
};

struct SShadowRequest {
    CActiveReader<CUnit> mUnit;
    float mDist = 16384.f;
};

struct SShadowTexInfo {
    CActiveReader<CUnit> mUnit;
    signed char mFade = 0;
    UBYTE mState = 0;
    UBYTE mUnused = 0;
    signed char mAlpha = 0;
    UBYTE mUnknown08 = 0;
    UBYTE mUnknown09[3] = {0, 0, 0};
    float mRadius = 0.f;
    FVector mPos;
};

struct CShadows {
    SShadowRequest mRequests[32];
    SShadowTexInfo mTexInfo[32];
    SShadowTexInfo* mFree[32] = {};
    SINT mNumFree = 0;
    SINT mUnknown590 = 0;
} SHADOWS;

namespace {
@SHADOW_FENCE_CODE@
}

static int failures = 0;
static void Check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    }
}

int main() {
    CUnit cockpitUnits[4];
    CUnit viewUnits[4];
    for (int i = 0; i < 4; ++i) {
        cockpitUnits[i].id = 100 + i;
        viewUnits[i].id = 200 + i;
    }

    // Seed cockpit-pass shadow state before the Commander view pass.
    for (int i = 0; i < 32; ++i) {
        SHADOWS.mRequests[i].mUnit.SetReader(i < 4 ? &cockpitUnits[i] : nullptr);
        SHADOWS.mRequests[i].mDist = i < 4 ? float(10 + i * 5) : 16384.f;
        SHADOWS.mTexInfo[i].mUnit.SetReader(i < 2 ? &cockpitUnits[i] : nullptr);
        SHADOWS.mTexInfo[i].mFade = static_cast<signed char>(i < 2 ? 32 - i * 4 : 0);
        SHADOWS.mTexInfo[i].mState = UBYTE(i < 2 ? i : 0);
        SHADOWS.mTexInfo[i].mUnused = UBYTE(i == 1 ? 1 : 0);
        SHADOWS.mTexInfo[i].mAlpha = static_cast<signed char>(i < 2 ? 96 - i * 16 : 0);
        SHADOWS.mTexInfo[i].mUnknown08 = UBYTE(i < 2 ? 1 : 0);
        SHADOWS.mTexInfo[i].mUnknown09[0] = UBYTE(i + 1);
        SHADOWS.mTexInfo[i].mUnknown09[1] = UBYTE(i + 2);
        SHADOWS.mTexInfo[i].mUnknown09[2] = UBYTE(i + 3);
        SHADOWS.mTexInfo[i].mRadius = float(3 + i);
        SHADOWS.mTexInfo[i].mPos = {float(i), float(i * 2), float(-i)};
        SHADOWS.mFree[i] = (i < 30) ? &SHADOWS.mTexInfo[i + 2] : nullptr;
    }
    SHADOWS.mNumFree = 30;
    SHADOWS.mUnknown590 = 7;

    SaveShadows();

    // Simulate Commander view pass mutating requests, texture assignments, and free pool.
    for (int i = 0; i < 32; ++i) {
        SHADOWS.mRequests[i].mUnit.SetReader(i < 4 ? &viewUnits[i] : nullptr);
        SHADOWS.mRequests[i].mDist = i < 4 ? float(2 + i) : 16384.f;
        SHADOWS.mTexInfo[i].mUnit.SetReader(i < 3 ? &viewUnits[i] : nullptr);
        SHADOWS.mTexInfo[i].mFade = 2;
        SHADOWS.mTexInfo[i].mState = 2;
        SHADOWS.mTexInfo[i].mUnused = 0;
        SHADOWS.mTexInfo[i].mAlpha = 12;
        SHADOWS.mTexInfo[i].mUnknown08 = 2;
        SHADOWS.mTexInfo[i].mUnknown09[0] = 99;
        SHADOWS.mTexInfo[i].mRadius = 42.f;
        SHADOWS.mTexInfo[i].mPos = {99.f, 88.f, 77.f};
        SHADOWS.mFree[i] = (i < 29) ? &SHADOWS.mTexInfo[i + 3] : nullptr;
    }
    SHADOWS.mNumFree = 29;
    SHADOWS.mUnknown590 = 19;

    RestoreShadows();

    // 1. Exact state restoration.
    for (int i = 0; i < 32; ++i) {
        Check(SHADOWS.mRequests[i].mUnit.ToRead() == (i < 4 ? &cockpitUnits[i] : nullptr), "mRequests unit restored");
        Check(SHADOWS.mRequests[i].mDist == (i < 4 ? float(10 + i * 5) : 16384.f), "mRequests dist restored");
        Check(SHADOWS.mTexInfo[i].mUnit.ToRead() == (i < 2 ? &cockpitUnits[i] : nullptr), "mTexInfo unit restored");
        Check(SHADOWS.mTexInfo[i].mFade == static_cast<signed char>(i < 2 ? 32 - i * 4 : 0), "mTexInfo fade restored");
        Check(SHADOWS.mTexInfo[i].mState == UBYTE(i < 2 ? i : 0), "mTexInfo state restored");
        Check(SHADOWS.mTexInfo[i].mUnused == UBYTE(i == 1 ? 1 : 0), "mTexInfo unused restored");
        Check(SHADOWS.mTexInfo[i].mAlpha == static_cast<signed char>(i < 2 ? 96 - i * 16 : 0), "mTexInfo alpha restored");
        Check(SHADOWS.mTexInfo[i].mUnknown08 == UBYTE(i < 2 ? 1 : 0), "mTexInfo unknown08 restored");
        Check(SHADOWS.mTexInfo[i].mUnknown09[0] == UBYTE(i + 1) &&
              SHADOWS.mTexInfo[i].mUnknown09[1] == UBYTE(i + 2) &&
              SHADOWS.mTexInfo[i].mUnknown09[2] == UBYTE(i + 3), "mTexInfo unknown09 restored");
        Check(SHADOWS.mTexInfo[i].mRadius == float(3 + i), "mTexInfo radius restored");
        Check(SHADOWS.mTexInfo[i].mPos == FVector{float(i), float(i * 2), float(-i)}, "mTexInfo pos restored");
        Check(SHADOWS.mFree[i] == ((i < 30) ? &SHADOWS.mTexInfo[i + 2] : nullptr), "mFree slot restored");
    }
    Check(SHADOWS.mNumFree == 30, "mNumFree restored");
    Check(SHADOWS.mUnknown590 == 7, "mUnknown590 restored");

    // 2. CMonitor deletion-callback integrity: killing view-pass units must NOT clear cockpit units' slots.
    for (int i = 0; i < 4; ++i) {
        Check(viewUnits[i].readers.empty(), "view-pass unit has no lingering shadow readers");
        viewUnits[i].Die();
    }
    Check(SHADOWS.mRequests[0].mUnit.ToRead() == &cockpitUnits[0], "dying view unit did not clobber cockpit request");
    Check(SHADOWS.mTexInfo[0].mUnit.ToRead() == &cockpitUnits[0], "dying view unit did not clobber cockpit texInfo");

    // 3. CMonitor deletion-callback integrity: killing a restored cockpit unit MUST clear its shadow readers (no dangling pointer).
    cockpitUnits[0].Die();
    Check(SHADOWS.mRequests[0].mUnit.ToRead() == nullptr, "dying restored cockpit unit cleared mRequests[0]");
    Check(SHADOWS.mTexInfo[0].mUnit.ToRead() == nullptr, "dying restored cockpit unit cleared mTexInfo[0]");
    Check(SHADOWS.mRequests[1].mUnit.ToRead() == &cockpitUnits[1], "other cockpit unit still intact");

    return failures ? 1 : 0;
}
"""


def extract_shadow_block(text: str) -> str:
    if text.count(BEGIN_MARKER) != 1 or text.count(END_MARKER) != 1:
        raise AssertionError("shadow fence extraction markers missing")
    return text.split(BEGIN_MARKER, 1)[1].split(END_MARKER, 1)[0]


class RenderFenceTests(unittest.TestCase):
    def compile_and_run(self, shadow_code: str, name: str) -> tuple[bool, int]:
        compiler = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
        self.assertTrue(compiler, "a C++ compiler is required")
        with tempfile.TemporaryDirectory(prefix=f"render-fence-{name}-") as td:
            tmp = Path(td)
            src = tmp / "driver.cpp"
            exe = tmp / "driver"
            src.write_text(HARNESS.replace("@SHADOW_FENCE_CODE@", shadow_code))
            built = subprocess.run(
                [compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)],
                capture_output=True, text=True, timeout=30,
            )
            if built.returncode != 0:
                return False, built.returncode
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            return True, ran.returncode

    def test_patch_applies_cleanly_and_leaves_faithful_build_unchanged(self):
        with tempfile.TemporaryDirectory(prefix="render-fence-patch-") as td:
            base = Path(td)
            full = patched(base / "full", ("DXEngine.cpp",))["DXEngine.cpp"]
            dependent = ("0381-ce-perf-surf-museum-fixes.patch", "0397-ce-audio-pause-museum-shader-fixes.patch", "0398-ce-bugclip-frontend-overlay.patch")
            with_patch = patched(base / "with", ("DXEngine.cpp",), skip=dependent)["DXEngine.cpp"]
            without_patch = patched(base / "without", ("DXEngine.cpp",), skip=(PATCH.name, *dependent))["DXEngine.cpp"]
        self.assertNotEqual(with_patch, without_patch)
        self.assertEqual(faithful(with_patch), faithful(without_patch))
        self.assertRegex(
            full,
            r"HRESULT CDXEngine::Render\(int viewpoint\)[^\n]*\n\{\n#ifdef CE_VERSION\n(?:[^\n]*\n)*?\tif \(PortCeCmdRenderWorld\(viewpoint\)\)\n\t\treturn S_OK;\n#endif\n\tSetNearZ\(DEFAULT_Z_NEAR\);",
        )

    def test_render_world_and_fence_wiring(self):
        cmdmap = CMDMAP_CPP.read_text()
        self.assertIn('#include "ce_render_fence.h"', cmdmap)
        self.assertIn("if (inside || gMirrorPass || viewpoint != 0 || (PortCeCmdWorldViewToken() == 0 && !NativeReturnEligible())) return false;", cmdmap)
        self.assertIn("if (CeRenderFenceSave()) {", cmdmap)
        self.assertIn("CeRenderFenceRestore();", cmdmap)
        self.assertIn("extern \"C\" bool PortCeWorldViewCaptureSuspended()", cmdmap)
        render_world = cmdmap[cmdmap.index("bool PortCeCmdRenderWorld("):cmdmap.index("bool PortCeCmdMapOpen()")]
        self.assertLess(render_world.index("gCockpitPass = true;"), render_world.index("if (CeRenderFenceSave()) {"))
        self.assertIn("D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER", render_world)
        self.assertNotIn("D3DRS_COLORWRITEENABLE", render_world)
        fence = FENCE_CPP.read_text()
        self.assertIn("SaveShadows();", fence)
        self.assertIn("RestoreShadows();", fence)

    def test_shadow_save_restore_production_code_and_causal_mutants(self):
        block = extract_shadow_block(FENCE_CPP.read_text())
        ok, rc = self.compile_and_run(block, "production")
        self.assertTrue(ok and rc == 0, f"production shadow fence failed: compiled={ok} rc={rc}")

        mutants = {
            "restore dropped": block.replace("if (!gShadows.valid) return;", "return;", 1),
            "requests restore dropped": block.replace(
                "SHADOWS.mRequests[i].mUnit.SetReader(gShadows.requests[i].unit);",
                "// dropped requests unit restore", 1,
            ),
            "texinfo restore dropped": block.replace(
                "t.mUnit.SetReader(gShadows.texInfo[i].unit);",
                "// dropped texInfo unit restore", 1,
            ),
            "free list restore dropped": block.replace(
                "SHADOWS.mNumFree = gShadows.numFree;",
                "// dropped numFree restore", 1,
            ),
            "raw memcpy bypasses SetReader": re.sub(
                r"void RestoreShadows\(\)\s*\{.*\}",
                "void RestoreShadows() {\n"
                "    if (!gShadows.valid) return;\n"
                "    for (int i = 0; i < 32; ++i) {\n"
                "        SHADOWS.mRequests[i].mUnit.mToRead = gShadows.requests[i].unit;\n"
                "        SHADOWS.mRequests[i].mDist = gShadows.requests[i].dist;\n"
                "        SShadowTexInfo& t = SHADOWS.mTexInfo[i];\n"
                "        t.mUnit.mToRead = gShadows.texInfo[i].unit;\n"
                "        t.mFade = gShadows.texInfo[i].fade;\n"
                "        t.mState = gShadows.texInfo[i].state;\n"
                "        t.mUnused = gShadows.texInfo[i].unused;\n"
                "        t.mAlpha = gShadows.texInfo[i].alpha;\n"
                "        t.mUnknown08 = gShadows.texInfo[i].unknown08;\n"
                "        memcpy(t.mUnknown09, gShadows.texInfo[i].unknown09, sizeof t.mUnknown09);\n"
                "        t.mRadius = gShadows.texInfo[i].radius;\n"
                "        t.mPos = gShadows.texInfo[i].pos;\n"
                "        SHADOWS.mFree[i] = gShadows.freeList[i];\n"
                "    }\n"
                "    SHADOWS.mNumFree = gShadows.numFree;\n"
                "    SHADOWS.mUnknown590 = gShadows.unknown590;\n"
                "    gShadows.valid = false;\n"
                "}",
                block, count=1, flags=re.S,
            ),
        }
        for label, mutated in mutants.items():
            with self.subTest(mutant=label):
                self.assertNotEqual(mutated, block, f"mutant {label} did not alter code")
                ok, rc = self.compile_and_run(mutated, label.replace(" ", "-"))
                self.assertTrue(ok, f"mutant {label} failed to compile")
                self.assertNotEqual(rc, 0, f"mutant {label} was NOT caught by shadow fence tests")

    def test_public_build_hygiene_no_bisect_or_test_switches(self):
        shipped_fence = without_test(comments_off(FENCE_CPP.read_text()))
        self.assertNotIn("getenv", shipped_fence)
        self.assertNotIn("BEA_PORT_", shipped_fence)
        cmdmap = CMDMAP_CPP.read_text()
        for forbidden in ("BEA_PORT_BISECT", "BEA_PORT_FENCE_DROP", "PortCeBisectSkip"):
            self.assertNotIn(forbidden, cmdmap)


if __name__ == "__main__":
    unittest.main(verbosity=1)
