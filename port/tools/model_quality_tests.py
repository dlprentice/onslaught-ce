#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real frame-boundary unit-vs-tree model policy with authored configuration inputs.

These tests establish configuration fences, legacy full_models=1 compatibility, independent
unit/tree defaults and switching, atomic view-to-view switching and bounded observations.
Actual geometry, device lifetime and performance require native game runs.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/platform/common/model_quality.cpp"
INCLUDES = ROOT / "port/platform/common"
DRIVER = ROOT / "port/tools/tests/model_quality_driver.cpp"


class Policy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        output = ROOT / "local-data/model-quality-tests"
        output.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="policy-", dir=output)
        cls.folder = Path(cls.temp.name)
        (cls.folder / "SDL3").mkdir()
        (cls.folder / "SDL3/SDL_timer.h").write_text(
            '#include <stdint.h>\ntypedef uint64_t Uint64;\n'
            'extern "C" Uint64 SDL_GetTicksNS();\n')
        cls.binaries = {}
        for public in (False, True):
            cls.binaries[public] = cls.build("public" if public else "reference", SOURCE, public)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @classmethod
    def build(cls, name, source, public=True):
        binary = cls.folder / name
        cmd = ["g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I", str(cls.folder), "-I", str(INCLUDES), str(source), str(DRIVER), "-o", str(binary)]
        if public:
            cmd.insert(1, '-DCE_VERSION="test"')
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError("authored policy driver compilation failed:\n" + result.stderr)
        return binary

    def run_case(self, scenario, *, public=True, env=None, binary=None):
        # No inherited reference, game, profile, or credentials variables.
        environment = {"PATH": os.environ.get("PATH", "/usr/bin"), "HOME": "/nonexistent"}
        environment.update(env or {})
        return subprocess.run([str(binary or self.binaries[public]), scenario], env=environment,
                              capture_output=True, text=True, timeout=10)

    def passed(self, scenario, **kwargs):
        result = self.run_case(scenario, **kwargs)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "PASS\n")

    def test_default_is_units_on_trees_off(self):
        self.passed("default")

    def test_legacy_full_models_compat(self):
        self.passed("legacy-on")
        self.passed("legacy-off")

    def test_stored_choices_wait_until_frame(self):
        self.passed("stored")

    def test_live_on_off_never_splits_views(self):
        self.passed("switch")

    def test_request_before_first_frame_and_last_request_wins(self):
        self.passed("queued-first")

    def test_faithful_fences_even_with_forced_ce_layer(self):
        for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"):
            for value in ("1", "0", "reference"):
                with self.subTest(name=name, value=value):
                    self.passed("fenced", env={name: value, "BEA_PORT_CE_LAYER": "1",
                                               "BEA_PORT_MODEL_QUALITY_LOG": "1"})

    def test_reference_build_does_not_consult_configuration(self):
        self.passed("fenced", public=False, env={"BEA_PORT_CE_LAYER": "1"})

    def test_disabled_ce_layer_is_fenced(self):
        self.passed("fenced", env={"DRIVER_LAYER_OFF": "1"})

    def test_empty_fences_do_not_change_ordinary_play(self):
        self.passed("stored", env={name: "" for name in (
            "BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD")})

    def test_diagnostics_are_bounded_and_do_not_enable_quality(self):
        self.passed("diagnostics", env={"BEA_PORT_MODEL_QUALITY_LOG": "1"})

    def test_patch_splits_units_and_trees_cleanly(self):
        patch = (ROOT / "port/patches/0208-ce-full-models.patch").read_text()
        rtmesh = patch.split("--- a/rtmesh.cpp\n", 1)[1].split("--- a/RTTree.cpp\n", 1)[0]
        rttree = patch.split("--- a/RTTree.cpp\n", 1)[1].split("--- a/gcgamut.cpp\n", 1)[0]
        gamut = patch.split("--- a/gcgamut.cpp\n", 1)[1].split("--- a/DXTrees.cpp\n", 1)[0]
        dxtrees = patch.split("--- a/DXTrees.cpp\n", 1)[1].split("--- a/game.cpp\n", 1)[0]
        self.assertIn("CeUnitModelsQualityEnabled()", rtmesh)
        self.assertIn("!gMirrorPass && CeUnitModelsQualityEnabled()", rtmesh)
        self.assertNotIn("CeTreeModelsQualityEnabled()", rtmesh)
        for sec_name, sec in (("RTTree.cpp", rttree), ("gcgamut.cpp", gamut), ("DXTrees.cpp", dxtrees)):
            self.assertIn("CeTreeModelsQualityEnabled()", sec, sec_name)
            self.assertNotIn("CeUnitModelsQualityEnabled()", sec, sec_name)

    def test_rtmesh_mirror_pass_exempts_full_models_and_keeps_main_pass_zero_impostor(self):
        sys.path.insert(0, str(ROOT / "port"))
        import build
        rtmesh_text = build.patched_text(["rtmesh.cpp"])["rtmesh.cpp"]

        def extract(signature):
            start = rtmesh_text.index(signature)
            return rtmesh_text[start:rtmesh_text.index("\n}\n", start) + 3]

        render_fn = extract("void CRTMesh::Render(DWORD flags)")
        imp_fn = extract("void CRTMesh::RenderImposter()")
        self.assertIn("const bool ce_full_models = !gMirrorPass && CeUnitModelsQualityEnabled();", render_fn)
        self.assertIn("if (!gMirrorPass && CeUnitModelsQualityEnabled()) return;", imp_fn)

        driver_code = r'''
#include <cmath>
#include <cstdio>
#include "model_quality.h"

using DWORD = unsigned;
using SINT = int;
using UINT = unsigned;
using BOOL = int;
const BOOL FALSE = 0;

struct FVector {
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    FVector() = default;
    FVector(float x, float y, float z) : X(x), Y(y), Z(z) {}
    FVector operator-(const FVector &o) const { return {X - o.X, Y - o.Y, Z - o.Z}; }
    float MagnitudeSq() const { return X * X + Y * Y + Z * Z; }
};
struct FMatrix {};
const FVector ZERO_FVECTOR{};
const FMatrix ID_FMATRIX{};

bool gMirrorPass = false;
bool FORCE_OBJECT_IMPOSTERS = false;
float IMPOSTER_FADE_START = 39.0f;
float IMPOSTER_FADE_END = 40.0f;
float MESH_LOD_BIAS = 1.0f;
float MESH_LOD_MED_THRESHOLD = 10.0f;
float MESH_LOD_LOW_THRESHOLD = 20.0f;
bool MESH_LOD_IGNORE_ZOOM = false;
bool SNOW_LAYER_ENABLE = false;
float MESH_SURFACE_LOD_BIAS = 400.0f;
UINT MESH_SURFACE_DETAIL = 100;
SINT MESH_ALPHA = 255;

static bool gUnitQuality = true;
bool CeUnitModelsQualityEnabled() { return gUnitQuality; }
static unsigned gCounts[CE_MODEL_EVENTS] = {};
void CeModelQualityCount(CeModelQualityEvent e) { if (e >= 0 && e < CE_MODEL_EVENTS) ++gCounts[e]; }

struct Camera { FVector pos; float zoom = 1.0f; FVector GetPos() const { return pos; } float GetZoom() const { return zoom; } };
struct Engine { Camera cam; Camera *GetCamera() { return &cam; } } ENGINE;
struct RenderInfo { float mSnowDensity = 0.0f; } RENDERINFO;
struct BBox { float mRadius = 1.0f; };
struct CMESH { CMESH *mNextInChain = nullptr; };
struct IRenderableThing {
    FVector pos{200.0f, 0.0f, 0.0f};
    FVector GetRenderPos() const { return pos; }
    float GetRenderImposterBias() const { return 1.0f; }
    float GetRenderScale() const { return 1.0f; }
    float GetSnowDensity() const { return 0.0f; }
    SINT GetRenderAnimation() const { return 0; }
    float GetRenderFrame() const { return 0.0f; }
};

static int gMeshDraws = 0, gImposterDraws = 0;
static CMESH *gLastDrawnMesh = nullptr;
static UINT gLastDetail = 0;
struct CDXImposter {
    SINT mNumFrames = 1;
    BOOL mAnimated = 0;
    void Render(IRenderableThing *, SINT, SINT) { ++gImposterDraws; }
};
struct CMeshRenderer {
    static SINT mRenderMode;
    static void Render(FVector *, FMatrix *, CMESH *m, IRenderableThing *, void *, UINT, bool, DWORD) {
        ++gMeshDraws;
        gLastDrawnMesh = m;
        gLastDetail = MESH_SURFACE_DETAIL;
    }
};
SINT CMeshRenderer::mRenderMode = 0;

struct CRTMesh {
    static CRTMesh *sRenderedLastFrame;
    static CRTMesh *sRenderedThisFrame;
    CRTMesh *mNextRendered = nullptr;
    CRTMesh *mPrevRendered = nullptr;
    IRenderableThing *mThing = nullptr;
    CMESH *mMesh = nullptr;
    CDXImposter *mImposter = nullptr;
    UINT mUnknown24 = 0;
    BBox box;
    BBox *GetBoundingBox() { return &box; }
    void Render(DWORD flags);
    void RenderImposter();
};
CRTMesh *CRTMesh::sRenderedLastFrame = nullptr;
CRTMesh *CRTMesh::sRenderedThisFrame = nullptr;

#include "rtmesh_actual.inc"

static int gFail = 0;
static void Need(bool cond, const char *msg) {
    if (!cond) { std::fprintf(stderr, "rtmesh assertion: %s\n", msg); ++gFail; }
}
static void ResetCounters() {
    gMeshDraws = gImposterDraws = 0;
    gLastDrawnMesh = nullptr;
    gLastDetail = 0;
    for (unsigned &c : gCounts) c = 0;
}

int main() {
    IRenderableThing thing;
    CMESH lowLod{}, hiLod{&lowLod};
    CDXImposter imp{};
    CRTMesh rt{};
    rt.mThing = &thing;
    rt.mMesh = &hiLod;
    rt.mImposter = &imp;

    // 1. Main pass with Full Unit Models ON: full 3D mesh drawn, imposter skipped, impostor==0 & reduced_mesh==0.
    ResetCounters();
    gUnitQuality = true; gMirrorPass = false; FORCE_OBJECT_IMPOSTERS = false;
    rt.Render(0);
    rt.RenderImposter();
    Need(gMeshDraws == 1 && gLastDrawnMesh == &hiLod && gLastDetail == 100, "main pass renders full-LOD mesh");
    Need(gImposterDraws == 0, "main pass draws zero imposters");
    Need(gCounts[CE_MODEL_IMPOSTOR] == 0 && gCounts[CE_MODEL_REDUCED_MESH] == 0, "main pass keeps impostor==0 and reduced_mesh==0");

    // 2. Mirror pass with Full Unit Models ON (CGame::Render sets FORCE_OBJECT_IMPOSTERS=true):
    //    exempt from full-3D override -> skips 3D mesh, renders fast imposter in reflection, keeps CE_MODEL_IMPOSTOR==0!
    ResetCounters();
    gUnitQuality = true; gMirrorPass = true; FORCE_OBJECT_IMPOSTERS = true;
    rt.Render(0);
    rt.RenderImposter();
    Need(gMeshDraws == 0, "mirror pass skips full 3D mesh for impostorable distant unit");
    Need(gImposterDraws == 1, "mirror pass still renders imposter billboard in water reflection");
    Need(gCounts[CE_MODEL_IMPOSTOR] == 0 && gCounts[CE_MODEL_REDUCED_MESH] == 0, "mirror pass does not pollute main-pass impostor/reduced_mesh counters");

    // 3. Mirror pass for non-impostorable mesh with LOD chain: uses reduced LOD and keeps CE_MODEL_REDUCED_MESH==0.
    ResetCounters();
    rt.mImposter = nullptr;
    rt.Render(0);
    Need(gMeshDraws == 1 && gLastDrawnMesh == &lowLod, "mirror pass uses reduced LOD for non-impostorable distant mesh");
    Need(gCounts[CE_MODEL_REDUCED_MESH] == 0, "mirror pass reduced mesh does not pollute main-pass reduced_mesh counter");

    // 4. Main pass with Full Unit Models OFF: retail behavior, imposter rendered & counted.
    ResetCounters();
    rt.mImposter = &imp;
    gUnitQuality = false; gMirrorPass = false; FORCE_OBJECT_IMPOSTERS = false;
    rt.Render(0);
    rt.RenderImposter();
    Need(gMeshDraws == 0 && gImposterDraws == 1 && gCounts[CE_MODEL_IMPOSTOR] == 1, "full_unit_models OFF uses retail imposter and counts it");

    if (gFail) return 1;
    std::puts("PASS");
    return 0;
}
'''
        combined = render_fn + "\n" + imp_fn + "\n"
        drv_path = self.folder / "rtmesh_mirror_driver.cpp"
        drv_path.write_text(driver_code)

        def run_variant(label, body):
            vdir = self.folder / ("rtmesh-" + label)
            vdir.mkdir(exist_ok=True)
            (vdir / "rtmesh_actual.inc").write_text(body)
            exe = vdir / "driver"
            comp = subprocess.run(
                ["g++", "-std=c++14", "-O2", "-Wall", "-Wextra",
                 "-I", str(vdir), "-I", str(INCLUDES), str(drv_path), "-o", str(exe)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(comp.returncode, 0, label + ": rtmesh driver compile failed")
            return subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)

        ok = run_variant("clean", combined)
        self.assertEqual(ok.returncode, 0, ok.stderr + ok.stdout)
        self.assertEqual(ok.stdout, "PASS\n")

        for label, old, new in (
            ("render-ignores-mirror",
             "const bool ce_full_models = !gMirrorPass && CeUnitModelsQualityEnabled();",
             "const bool ce_full_models = CeUnitModelsQualityEnabled();"),
            ("imposter-blocks-mirror",
             "if (!gMirrorPass && CeUnitModelsQualityEnabled()) return;",
             "if (CeUnitModelsQualityEnabled()) return;"),
            ("mirror-pollutes-impostor-count",
             "if (!gMirrorPass) CeModelQualityCount(CE_MODEL_IMPOSTOR);",
             "CeModelQualityCount(CE_MODEL_IMPOSTOR);"),
            ("mirror-pollutes-reduced-count",
             "if (!gMirrorPass && mesh != mMesh) CeModelQualityCount(CE_MODEL_REDUCED_MESH);",
             "if (mesh != mMesh) CeModelQualityCount(CE_MODEL_REDUCED_MESH);"),
        ):
            with self.subTest(mutant=label):
                self.assertEqual(combined.count(old), 1, label)
                ran = run_variant(label, combined.replace(old, new, 1))
                self.assertEqual(ran.returncode, 1, f"mutant {label} was NOT caught")
                self.assertIn("rtmesh assertion:", ran.stderr)

    def test_tree_guard_keeps_models_and_preserves_ordinary_policy(self):
        # Compile the actual overlay predicate against authored Boolean inputs.
        # This caught a native failure: treating the original early-return guard
        # as a draw guard removed every tree in Full models mode.
        patch = (ROOT / "port/patches/0208-ce-full-models.patch").read_text()
        section = patch.split("--- a/RTTree.cpp\n", 1)[1].split("--- a/gcgamut.cpp\n", 1)[0]
        before, after = [], []
        lines = section.splitlines()
        for i, line in enumerate(lines):
            match = re.fullmatch(r"([+-])\s*if\s*\((.*)\)\s*", line)
            if not match:
                continue
            if match[1] == "-":
                before.append(match[2])
            elif "CeTreeModelsQualityEnabled" in match[2]:
                after.append(match[2])
                self.assertEqual(lines[i+1].strip(), "return;")
        self.assertEqual(len(before), 1)
        self.assertEqual(len(after), 1)
        original, current = before[0], after[0]
        mutant = current.replace("!(!gMirrorPass && CeTreeModelsQualityEnabled()) &&", "(!gMirrorPass && CeTreeModelsQualityEnabled()) ||")
        self.assertNotEqual(mutant, current)
        for label, expression, expected in (("tree-good", current, 0), ("tree-missing-mutant", mutant, 1)):
            source = self.folder / (label + ".cpp")
            source.write_text("""
#include <stdio.h>
static bool full;
static bool gMirrorPass;
bool CeTreeModelsQualityEnabled() { return full; }
bool old_skip(bool close, bool falling, bool FORCE_OBJECT_IMPOSTERS) { return OLD; }
bool new_skip(bool close, bool falling, bool FORCE_OBJECT_IMPOSTERS) { return NEW; }
int main() {
  for (int bits=0; bits<8; ++bits) {
    bool close=bits&1, falling=bits&2, force=bits&4;
    gMirrorPass=false;
    full=false;
    if (old_skip(close,falling,force) != new_skip(close,falling,force)) return 1;
    full=true;
    if (new_skip(close,falling,force)) return 1;
    gMirrorPass=true;
    if (old_skip(close,falling,force) != new_skip(close,falling,force)) return 1;
  }
  puts("PASS"); return 0;
}
""".replace("OLD", original).replace("NEW", expression))
            binary = self.folder / label
            compiled = subprocess.run(["g++", "-std=c++11", "-O2", str(source), "-o", str(binary)],
                                      capture_output=True, text=True, timeout=60)
            # Never echo an extracted renderer expression in a public failure.
            self.assertEqual(compiled.returncode, 0, "renderer-predicate control did not compile")
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, expected, label)

    def test_tree_imposter_takes_the_original_decisions_when_off(self):
        sys.path.insert(0, str(ROOT / "port"))
        import build

        def function(text):
            start = text.index("void CRTTree::RenderImposter()")
            return text[start:text.index("\n}\n", start) + 3]

        patch = "0208-ce-full-models.patch"
        patched = function(build.patched_text(["RTTree.cpp"])["RTTree.cpp"])
        original = function(build.patched_text(["RTTree.cpp"], skip=(patch,))["RTTree.cpp"])
        self.assertNotIn("CeTreeModelsQuality", original)

        def run(label, text, *args):
            include = self.folder / (label + ".inc")
            include.write_text(text)
            binary = self.folder / ("tree-" + label)
            compiled = subprocess.run(
                ["g++", "-std=c++11", "-O2", "-I", str(INCLUDES), f'-DTREE_FUNCTION="{include}"',
                 str(ROOT / "port/tools/tests/model_quality_tree_driver.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            # Never echo the extracted renderer text in a public failure.
            self.assertEqual(compiled.returncode, 0, label + ": tree stand-in fixture did not compile")
            return subprocess.run([str(binary), *args], capture_output=True, text=True, timeout=10)

        def differences(result, reference):
            got, want = result.stdout.splitlines(), reference.stdout.splitlines()
            wrong = [g for g, w in zip(got, want) if g != w] + ([] if len(got) == len(want) else ["(row count)"])
            return wrong, result.stderr.strip()

        reference = run("original", original, "0", "base")
        self.assertEqual(reference.returncode, 0)
        rows = reference.stdout.splitlines()
        self.assertEqual(len(rows), 600)
        self.assertTrue(any("renders=1 " in row for row in rows))
        self.assertTrue(any("renders=0 " in row and row.endswith("drawn=1") for row in rows))
        off = run("patched", patched, "0")
        wrong, error = differences(off, reference)
        self.assertEqual((off.returncode, len(wrong)), (0, 0),
                         f"3D trees off: {len(wrong)} of {len(rows)} cases differ from the original (first: "
                         f"{wrong[:1]}); {error}")
        on = run("patched-on", patched, "1")
        self.assertEqual(on.returncode, 0, on.stderr)
        self.assertTrue(all("renders=0 " in row and row.endswith("drawn=0") for row in on.stdout.splitlines()))
        lines = patched.split("\n")
        at = [i for i, line in enumerate(lines) if "CE_MODEL_TREE_IMPOSTOR" in line]
        self.assertEqual(len(at), 1)
        at = at[0]
        self.assertTrue(lines[at - 1].strip() == "{" and lines[at + 2].strip() == "}",
                        "the tree imposter counter and render are not one braced body")
        unbraced = "\n".join(lines[:at - 1] + lines[at:at + 2] + lines[at + 3:])
        mutant = run("unbraced-mutant", unbraced, "0")
        wrong, _ = differences(mutant, reference)
        self.assertTrue(wrong and mutant.returncode != 0, "the unbraced control was not caught")

    def test_causal_mutants(self):
        source = SOURCE.read_text()
        mutants = [
            ("mid-frame-units", "unitRequested = value;\n    return 1;", "unitRequested = value; unitActive = value == 1;\n    return 1;", "switch", {}),
            ("mid-frame-trees", "extern \"C\" int CE_FullTreeModelsRequest(int value)\n{\n    if ((value != 0 && value != 1) || !Allowed()) return 0;\n    treeRequested = value;\n    return 1;",
             "extern \"C\" int CE_FullTreeModelsRequest(int value)\n{\n    if ((value != 0 && value != 1) || !Allowed()) return 0;\n    treeRequested = value; treeActive = value == 1;\n    return 1;", "switch", {}),
            ("ignore-reference", "if (value && *value) return false;", "if (false && value && *value) return false;",
             "fenced", {"BEA_PORT_RECORD": "1", "BEA_PORT_CE_LAYER": "1"}),
            ("units-default-off", "unitRequested = savedUnit >= 0 ? (savedUnit ? 1 : 0) : 1;",
             "unitRequested = savedUnit >= 0 ? (savedUnit ? 1 : 0) : 0;", "default", {}),
            ("trees-default-on", "treeRequested = savedTree >= 0 ? (savedTree ? 1 : 0) : (savedLegacy == 1 ? 1 : 0);",
             "{ (void)savedLegacy; treeRequested = savedTree >= 0 ? (savedTree ? 1 : 0) : 1; }", "default", {}),
            ("ignore-legacy-on", "treeRequested = savedTree >= 0 ? (savedTree ? 1 : 0) : (savedLegacy == 1 ? 1 : 0);",
             "{ (void)savedLegacy; treeRequested = savedTree >= 0 ? (savedTree ? 1 : 0) : 0; }", "legacy-on", {}),
        ]
        for name, old, new, scenario, env in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, name)
                path = self.folder / (name + ".cpp")
                path.write_text(source.replace(old, new))
                binary = self.build(name, path)
                result = self.run_case(scenario, binary=binary, env=env)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Assertion", result.stderr)


if __name__ == "__main__":
    unittest.main()
