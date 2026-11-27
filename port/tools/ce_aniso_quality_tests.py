#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify hardware anisotropic filtering policy, hardware caps, faithful fences, and patch 0355.

Compiles the real port/platform/common/aniso_quality.cpp and ce.cpp against an authored host driver
(with and without CE_VERSION), checks patch 0355-ce-anisotropic-filtering.patch leaves the faithful
non-CE build byte-identical across all 4 patched files, and verifies causal mutants are caught.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
COMMON = PORT / "platform/common"

sys.path.insert(0, str(PORT / "tools"))
from ce_photo_tests import faithful, patched  # noqa: E402

DRIVER = r"""
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "aniso_quality.h"
#include "portlog.h"

static bool gLayer = true;
bool PortCeLayer() { return gLayer; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) {}
extern "C" uint64_t SDL_GetTicks() { return 1000; }
extern "C" bool SDL_OpenURL(const char *) { return false; }
extern "C" int CE_CompatibilityRuntimeMutation(char *, size_t) { return 1; }

static int gFailures = 0;
static void Check(bool cond, const char *msg) {
    if (!cond) {
        std::fprintf(stderr, "aniso assertion: %s\n", msg);
        ++gFailures;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const std::string mode = argv[1];
    if (mode == "layer-off") gLayer = false;

    if (mode == "default") {
        Check(CE_AnisoLevelChoice() == 16 && CE_AnisoChoice() == 3, "default CE aniso is 16x");
        CeAnisoQualityBeginFrame();
        Check(CeAnisoQualityActive(), "active in ordinary CE");
        Check(CeAnisoEffectiveLevel(4, 16, true) == 16u, "16x on 16x hardware");
        Check(CeAnisoUseAnisotropicFilter(4, 16, true), "uses anisotropic filter at 16x");
    } else if (mode == "stored-8") {
        Check(CE_AnisoLevelChoice() == 8 && CE_AnisoChoice() == 2, "reads stored 8x");
        CeAnisoQualityBeginFrame();
        Check(CeAnisoEffectiveLevel(4, 16, true) == 8u, "applies stored 8x");
    } else if (mode == "stored-0") {
        Check(CE_AnisoLevelChoice() == 0 && CE_AnisoChoice() == 0, "reads stored Off (0)");
        CeAnisoQualityBeginFrame();
        Check(CeAnisoEffectiveLevel(4, 16, true) == 1u, "Off resolves to maxAnisotropy 1");
        Check(!CeAnisoUseAnisotropicFilter(4, 16, true), "Off disables D3DTEXF_ANISOTROPIC");
    } else if (mode == "garbage") {
        Check(CE_AnisoLevelChoice() == 16, "garbage value falls back to 16x rather than Off");
    } else if (mode == "switch") {
        CeAnisoQualityBeginFrame();
        Check(CeAnisoEffectiveLevel(4, 16, true) == 16u, "starts at 16x");
        Check(CE_AnisoRequest(4) == 1, "accepts 4x request");
        // Must remain 16x until next frame boundary!
        Check(CeAnisoEffectiveLevel(4, 16, true) == 16u, "immutable mid-frame");
        CeAnisoQualityBeginFrame();
        Check(CeAnisoEffectiveLevel(4, 16, true) == 4u, "switches to 4x at frame boundary");
        Check(CE_AnisoRequest(7) == 0 && CE_AnisoRequest(-1) == 0 && CE_AnisoRequest(32) == 0, "rejects invalid levels");
        Check(CE_AnisoLevelChoice() == 4, "invalid requests leave choice intact");
    } else if (mode == "caps") {
        CeAnisoQualityBeginFrame();
        Check(CeAnisoEffectiveLevel(4, 8, true) == 8u, "16x clamped to hardware 8x");
        Check(CeAnisoEffectiveLevel(4, 4, true) == 4u, "16x clamped to hardware 4x");
        Check(CeAnisoEffectiveLevel(4, 1, true) == 1u && !CeAnisoUseAnisotropicFilter(4, 1, true), "maxHw=1 disables AF");
        Check(CeAnisoEffectiveLevel(4, 16, false) == 1u && !CeAnisoUseAnisotropicFilter(4, 16, false), "unsupported raster cap disables AF");
        CeAnisoSetHardwareCaps(8, true);
        Check(CE_AnisoHardwareMax() == 8u, "reported hardware cap 8");
        CeAnisoSetHardwareCaps(16, false);
        Check(CE_AnisoHardwareMax() == 1u, "unsupported hardware cap reports 1");
    } else if (mode == "fenced" || mode == "layer-off") {
        Check(CE_AnisoLevelChoice() == 4 && CE_AnisoChoice() == 1, "fenced reads retail 4x");
        Check(CE_AnisoRequest(16) == 0 && CE_AnisoRequest(0) == 0, "fenced refuses requests");
        CeAnisoQualityBeginFrame();
        Check(!CeAnisoQualityActive(), "fenced never activates CE AF");
        Check(CeAnisoEffectiveLevel(4, 16, true) == 4u, "fenced keeps retail level 4");
        Check(CeAnisoEffectiveLevel(1, 16, true) == 1u, "fenced keeps retail level 1");
        Check(CeAnisoUseAnisotropicFilter(4, 16, true) && !CeAnisoUseAnisotropicFilter(1, 16, true), "fenced matches retail filter");
    } else {
        return 2;
    }
    if (gFailures) return 1;
    std::puts("PASS");
    return 0;
}
"""


class AnisoQualityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("g++") or shutil.which("clang++") or shutil.which("c++")
        if not cls.compiler:
            raise RuntimeError("a C++ compiler is required")
        cls.tmp = tempfile.TemporaryDirectory(prefix="ce-aniso-")
        cls.dir = Path(cls.tmp.name)
        (cls.dir / "SDL3").mkdir()
        (cls.dir / "SDL3/SDL.h").write_text(
            '#pragma once\n#include <stdint.h>\ntypedef uint64_t Uint64;\n'
            'extern "C" Uint64 SDL_GetTicks(void);\nextern "C" bool SDL_OpenURL(const char *);\n'
        )
        (cls.dir / "driver.cpp").write_text(DRIVER)
        cls.binaries = {
            public: cls.compile_one("ce" if public else "reference", COMMON / "aniso_quality.cpp", public=public)
            for public in (True, False)
        }

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    @classmethod
    def compile_one(cls, name: str, source: Path, *, public: bool = True) -> Path:
        exe = cls.dir / name
        ce_obj = cls.dir / f"{name}-ce.o"
        ce_cmd = [
            cls.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Wno-format-truncation",
            "-DPortCeLayer=CeSettingsPortCeLayer",
            "-I", str(cls.dir), "-I", str(COMMON),
            "-c", str(COMMON / "ce.cpp"), "-o", str(ce_obj),
        ]
        if public:
            ce_cmd.insert(1, '-DCE_VERSION="aniso-test"')
        built_ce = subprocess.run(ce_cmd, capture_output=True, text=True, timeout=60)
        if built_ce.returncode != 0:
            raise RuntimeError(f"compile ce.cpp for {name} failed:\n{built_ce.stderr}")
        cmd = [
            cls.compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(cls.dir), "-I", str(COMMON),
            str(source), str(ce_obj), str(cls.dir / "driver.cpp"),
            "-o", str(exe),
        ]
        if public:
            cmd.insert(1, '-DCE_VERSION="aniso-test"')
        built = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if built.returncode != 0:
            raise RuntimeError(f"compile {name} failed:\n{built.stderr}")
        return exe

    def run_case(self, mode: str, *, public: bool = True, ini: str | None = None, env: dict[str, str] | None = None, binary: Path | None = None):
        home = Path(tempfile.mkdtemp(prefix="aniso-prof-", dir=self.dir))
        if ini is not None:
            (home / "ce.ini").write_text(ini)
        run_env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home), "BEA_USER_DIR": str(home)}
        if env:
            run_env.update(env)
        return subprocess.run([str(binary or self.binaries[public]), mode], env=run_env, capture_output=True, text=True, timeout=15)

    def assert_pass(self, mode: str, **kwargs):
        res = self.run_case(mode, **kwargs)
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertEqual(res.stdout, "PASS\n")

    def test_defaults_stored_and_invalid(self):
        self.assert_pass("default")
        self.assert_pass("stored-8", ini="anisotropic_filtering=8\n")
        self.assert_pass("stored-0", ini="anisotropic_filtering=0\n")
        self.assert_pass("garbage", ini="anisotropic_filtering=notanumber\n")
        self.assert_pass("garbage", ini="anisotropic_filtering=7\n")

    def test_frame_boundary_and_caps(self):
        self.assert_pass("switch")
        self.assert_pass("caps")

    def test_faithful_fences_preserve_retail(self):
        self.assert_pass("fenced", public=False)
        self.assert_pass("layer-off")
        for fence in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"):
            with self.subTest(fence=fence):
                self.assert_pass("fenced", ini="anisotropic_filtering=16\n", env={fence: "1", "BEA_PORT_CE_LAYER": "1"})

    def test_patch_leaves_faithful_build_unchanged(self):
        files = ("game.cpp", "DXEngine.cpp", "DXLandscape.cpp", "DXWater.cpp")
        hook_sites = tuple(sorted(p.name for p in (ROOT / "port/patches").glob("03[0-9][0-9]-ce-hook-*.patch")))
        with tempfile.TemporaryDirectory(prefix="aniso-patch-") as td:
            base = Path(td)
            # Isolate 0355 cleanly by stopping the queue after 0355 (later patches like 0381+ sit on top of 0355).
            later = tuple(sorted(p.name for p in (ROOT / "port/patches").glob("*.patch") if p.name > "0355-ce-anisotropic-filtering.patch"))
            with_p = patched(base / "with", files, skip=later)
            without_p = patched(base / "without", files, skip=("0355-ce-anisotropic-filtering.patch", *later))
        for name in files:
            with self.subTest(file=name):
                self.assertNotEqual(with_p[name], without_p[name])
                self.assertEqual(faithful(with_p[name]), faithful(without_p[name]))

    def test_causal_mutants_are_caught(self):
        orig = (COMMON / "aniso_quality.cpp").read_text()
        cases = [
            ("mid-frame-switch", "requested = level;", "requested = level; activeLevel = level;", "switch", None, {}),
            ("ignore-fence", "if (value && *value) return false;", "if (false && value && *value) return false;", "fenced", "anisotropic_filtering=16\n", {"BEA_PORT_CLOCK": "1"}),
            ("ignore-hw-cap", "return target < maxHardwareAnisotropy ? target : maxHardwareAnisotropy;", "return target;", "caps", None, {}),
            ("ignore-unsupported", "if (!supported || maxHardwareAnisotropy < 2 || activeLevel <= 0) return 1;", "(void)supported; if (activeLevel <= 0) return 1;", "caps", None, {}),
            ("off-still-aniso", "return CeAnisoEffectiveLevel(retailLevel, maxHardwareAnisotropy, supported) >= 2;", "(void)maxHardwareAnisotropy; (void)supported; return true;", "stored-0", "anisotropic_filtering=0\n", {}),
        ]
        for label, old, new, mode, ini, env in cases:
            with self.subTest(mutant=label):
                self.assertEqual(orig.count(old), 1, f"anchor mismatch for {label}")
                mut = self.dir / f"{label}.cpp"
                mut.write_text(orig.replace(old, new))
                exe = self.compile_one(f"mut-{label}", mut)
                res = self.run_case(mode, ini=ini, env=env, binary=exe)
                self.assertEqual(res.returncode, 1, f"mutant {label} was NOT caught")
                self.assertIn("aniso assertion:", res.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=1)
