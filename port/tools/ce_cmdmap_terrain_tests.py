#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The paused Commander map draws its terrain as one stretched texture instead of 16,384 sprites a frame (port/game/ce_cmdmap.cpp).

The measured reason: the frame-feel log (port/platform/common/feel.h) showed the main thread spending about nine of its twelve milliseconds a
frame on the cells, enough to miss every other vertical sync of a 144 Hz display. These checks run the real functions, extracted from the source,
against authored fakes of the texture, the sprite renderer and the device, and mutate them. How the map looks, and what a frame costs, are for a
native run; this establishes the copy, the sampling state, the lifetime and the fallback."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/game/ce_cmdmap.cpp"
DRIVER = ROOT / "port/tools/tests/ce_cmdmap_terrain_driver.cpp"


def extract(text):
    start = text.index("// One frame's terrain from the texture.")
    end = text.index("void DrawTerrain()")
    return text[start:end]


class TerrainTexture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = SOURCE.read_text()
        cls.region = extract(cls.text)
        cls.tmp = tempfile.TemporaryDirectory(prefix="terrain-")
        cls.dir = Path(cls.tmp.name)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def build_and_run(self, region, name, args=(), env=None):
        (self.dir / "ce_cmdmap_terrain.inc").write_text(region)
        binary = self.dir / name
        done = subprocess.run(["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-maybe-uninitialized", "-I", str(self.dir),
                               str(DRIVER), "-o", str(binary)], capture_output=True, text=True, timeout=120)
        self.assertEqual(done.returncode, 0, done.stderr)
        import os
        environment = dict(os.environ)
        environment.pop("BEA_PORT_COMMANDER_TERRAIN", None)
        environment.update(env or {})
        run = subprocess.run([str(binary)] + list(args), capture_output=True, text=True, timeout=30, env=environment)
        return run.returncode, run.stdout

    def test_real_functions_pass_the_checks(self):
        status, out = self.build_and_run(self.region, "real")
        self.assertEqual(status, 0, out)
        self.assertIn("failures=0", out)
        self.assertGreaterEqual(int(re.search(r"checks=(\d+)", out).group(1)), 22)

    def test_the_cells_switch_keeps_the_old_drawing(self):
        status, out = self.build_and_run(self.region, "cells", ["cells"], {"BEA_PORT_COMMANDER_TERRAIN": "cells"})
        self.assertEqual(status, 0, out)
        self.assertIn("failures=0", out)
        # without the variable the same program takes the texture path
        status, out = self.build_and_run(self.region, "cells-off", ["cells"], {})
        self.assertNotEqual(status, 0, "the switch must be what turns the texture off")

    def mutant(self, old, new, label):
        self.assertEqual(self.region.count(old), 1, label)
        status, out = self.build_and_run(self.region.replace(old, new), "mutant-" + re.sub(r"\W", "", label))
        self.assertNotEqual(status, 0, "mutant survived: " + label + "\n" + out)

    def test_mutants_are_caught(self):
        self.mutant("memcpy((unsigned char *)lr.pBits + j * lr.Pitch, &gTerrain[j * kGrid], sizeof(DWORD) * kGrid);",
                    "memcpy((unsigned char *)lr.pBits + j * kGrid * 4, &gTerrain[j * kGrid], sizeof(DWORD) * kGrid);", "pitch ignored")
        self.mutant("device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);", "", "magnification not nearest")
        self.mutant("shiftU, 1.f + shiftU, shiftV, 1.f + shiftV", "shiftU * 0.f, 1.f + shiftU * 0.f, shiftV, 1.f + shiftV", "texture not shifted along u")
        self.mutant("shiftU, 1.f + shiftU, shiftV, 1.f + shiftV", "shiftU, 1.f + shiftU, shiftV * 0.f, 1.f + shiftV * 0.f", "texture not shifted along v")
        self.mutant("const float shiftU = 0.999f / L.mapSide", "const float shiftU = 0.5f / L.mapSide", "shift not under a pixel")
        self.mutant("device->SetSamplerState(0, D3DSAMP_MAGFILTER, magnify);", "", "sampler not restored")
        self.mutant("\tgTerrainTexture->GetTexture()->UnlockRect(0);\n", "", "unlock missing")
        self.mutant("\t\tgTerrainTexture->Release();\n", "", "texture never released")
        self.mutant("\t\t\tgTerrainTextureFailed = true;\n", "", "failure not remembered")
        self.mutant("if (FAILED(gTerrainTexture->GetTexture()->LockRect(0, &lr, NULL, D3DLOCK_NOSYSLOCK)))\n\t\treturn false;",
                    "gTerrainTexture->GetTexture()->LockRect(0, &lr, NULL, D3DLOCK_NOSYSLOCK);", "failed lock ignored")
        self.mutant("CSPRITERENDERER::mBatch = FALSE;\n\tif (device)", "if (device)", "batching left on")

    def test_it_is_wired_in(self):
        draw = self.text[self.text.index("void DrawTerrain()"):]
        draw = draw[:draw.index("\n}\n")]
        self.assertLess(draw.index("DrawTerrainFromTexture()"), draw.index("for (int j = 0; j < kGrid; ++j)"))
        self.assertIn("return;", draw[:draw.index("for (int j")])
        # freed from the frame hook, not from Close(): the Commander camera suite compiles Close() out of context
        render = self.text[self.text.index("void PortCeCmdRender()\n{"):]
        render = render[:render.index("\n}\n")]
        self.assertIn("if (!gOpen) FreeTerrainTexture();", render)
        self.assertLess(render.index("if (!gOpen) FreeTerrainTexture();"), render.index("CE_STRATEGIC_RENDER_BEGIN"))
        close = self.text[self.text.index("void Close()\n{"):]
        close = close[:close.index("\n}\n")]
        self.assertNotIn("FreeTerrainTexture", close)


if __name__ == "__main__":
    unittest.main()
