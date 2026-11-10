#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the real frame-boundary full-model policy with authored configuration inputs.

These tests establish configuration fences, atomic view-to-view switching and bounded
observations. Actual geometry, device lifetime and performance require native game runs.
"""
import os
from pathlib import Path
import re
import subprocess
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

    def test_default_is_off(self):
        self.passed("default")

    def test_stored_choice_waits_until_frame(self):
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
            elif "CeModelQualityEnabled" in match[2]:
                after.append(match[2])
                self.assertEqual(lines[i+1].strip(), "return;")
        self.assertEqual(len(before), 1)
        self.assertEqual(len(after), 1)
        original, current = before[0], after[0]
        mutant = current.replace("!CeModelQualityEnabled() &&", "CeModelQualityEnabled() ||")
        self.assertNotEqual(mutant, current)
        for label, expression, expected in (("tree-good", current, 0), ("tree-missing-mutant", mutant, 1)):
            source = self.folder / (label + ".cpp")
            source.write_text("""
#include <stdio.h>
static bool full;
bool CeModelQualityEnabled() { return full; }
bool old_skip(bool close, bool falling, bool FORCE_OBJECT_IMPOSTERS) { return OLD; }
bool new_skip(bool close, bool falling, bool FORCE_OBJECT_IMPOSTERS) { return NEW; }
int main() {
  for (int bits=0; bits<8; ++bits) {
    bool close=bits&1, falling=bits&2, force=bits&4;
    full=false;
    if (old_skip(close,falling,force) != new_skip(close,falling,force)) return 1;
    full=true;
    if (new_skip(close,falling,force)) return 1;
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

    def test_causal_mutants(self):
        source = SOURCE.read_text()
        mutants = [
            ("mid-frame", "requested = value;", "requested = value; active = value == 1;", "switch", {}),
            ("ignore-reference", "if (value && *value) return false;", "if (false && value && *value) return false;",
             "fenced", {"BEA_PORT_RECORD": "1", "BEA_PORT_CE_LAYER": "1"}),
            ("default-on", 'CE_SettingGetBool("full_models", 0)', 'CE_SettingGetBool("full_models", 1)', "default", {}),
        ]
        for name, old, new, scenario, env in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1)
                path = self.folder / (name + ".cpp")
                path.write_text(source.replace(old, new))
                binary = self.build(name, path)
                result = self.run_case(scenario, binary=binary, env=env)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Assertion", result.stderr)


if __name__ == "__main__":
    unittest.main()
