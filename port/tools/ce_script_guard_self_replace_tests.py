#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise mod-only self-replacement through real patched native/interpreter method bodies.

The game, allocation, object lifetime, stack and instruction-list boundaries are authored. These tests
do not launch a game or establish real mod-level reachability. The separate notification queue defects
remain unresolved, and passing these checks does not approve the script guard for a release.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ce_palette_tests import function, no_core  # noqa: E402
from ce_photo_tests import faithful  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "port/tools/tests/ce_script_guard_self_replace_driver.cpp"
PATCHES = [ROOT / "port/patches/0176-ce-script-vm-guard.patch",
           ROOT / "port/patches/0182-ce-self-script-replace.patch"]


def run(args, **kwargs):
    return subprocess.run([str(x) for x in args], capture_output=True, text=True, timeout=120,
                          preexec_fn=no_core, **kwargs)


class SelfReplacementCandidate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data/port/ce-script-guard-self-replace"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
        source = cls.out / "src/MissionScript"
        source.mkdir(parents=True)
        for name in ("IScript.cpp", "IScript.h", "vm.cpp"):
            shutil.copyfile(ROOT / "src/MissionScript" / name, source / name)
        for name in ("game.cpp", "world.cpp"):
            shutil.copyfile(ROOT / "src" / name, cls.out / "src" / name)
        for patch in PATCHES:
            result = run(["patch", "--batch", "--fuzz=0", "-p1", "-i", patch], cwd=cls.out / "src")
            if result.returncode or "fuzz" in result.stdout.lower():
                raise AssertionError(result.stdout + result.stderr)

        script = (source / "IScript.cpp").read_text(encoding="latin1")
        vm = (source / "vm.cpp").read_text(encoding="latin1")
        instruction = (ROOT / "src/MissionScript/AsmInstruction.cpp").read_text(encoding="latin1")
        thing = (ROOT / "src/thing.cpp").read_text(encoding="latin1")
        start = script.index("struct CeSelfReplacement")
        end = script.index("\n#endif", start)
        helper = script[start:end]
        cls.methods = "\n\n".join((helper,
            function(script, "void\tIScript::SetScript(CDataType"),
            function(thing, "void\tCComplexThing::SetScript("),
            function(instruction, "void\tCInstructionOP_CALL::Execute("),
            function(vm, "void\tCVM::CallEvent("), function(vm, "void\tCVM::CallFunction("),
            function(vm, "void\tCVM::Run()"), function(script, "void\tIScript::Init()")))
        cls.patched = {"IScript.cpp": script, "vm.cpp": vm}
        cls.exe = cls.build("actual", cls.methods)
        print(f"self-replacement candidate artifacts: {cls.out}", flush=True)

    @classmethod
    def build(cls, name, methods):
        folder = cls.out / name
        folder.mkdir()
        (folder / "guard_self_replace_methods.inc").write_text(methods)
        exe = folder / "driver"
        result = run([os.environ.get("CXX", "g++"), "-std=c++14", "-g", "-Wall", "-Wextra", "-Werror",
                      "-Wno-unused-parameter", "-DCE_VERSION=1", "-I", folder, DRIVER, "-o", exe])
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        return exe

    def check(self, exe, mode):
        result = run([exe, mode])
        self.assertEqual(0, result.returncode, f"{mode}: {result.stdout}{result.stderr}")

    def test_event_handler_and_empty_replacement(self):
        for mode in ("event", "blank"):
            with self.subTest(mode=mode): self.check(self.exe, mode)

    def test_initializer_in_event_and_named_function(self):
        for mode in ("init-code", "function-init", "native-init"):
            with self.subTest(mode=mode): self.check(self.exe, mode)

    def test_non_mod_level_stays_on_original_path(self):
        self.check(self.exe, "stock")

    def test_other_target_keeps_original_synchronous_behavior(self):
        self.check(self.exe, "other-target")

    def test_cancellation_and_misrouted_or_duplicate_requests(self):
        for mode in ("owner-cancel", "script-cancel", "wrong-vm", "duplicate"):
            with self.subTest(mode=mode): self.check(self.exe, mode)

    def test_inactive_conditional_text_matches_original(self):
        # Compare nonempty faithful text, not compiled binary or runtime parity.
        for name in ("IScript.cpp", "vm.cpp"):
            before = (ROOT / "src/MissionScript" / name).read_text(encoding="latin1")
            nonempty = lambda text: [line for line in faithful(text).splitlines() if line.strip()]
            self.assertEqual(nonempty(before), nonempty(self.patched[name]), name)

    def test_controls_detect_removed_instruction_and_init_guards(self):
        needle = "if (PortCeCompleteSelfReplacement(this)) return;"
        self.assertEqual(1, self.methods.count(needle))
        changed = self.methods.replace(needle, "(void)0;", 1)
        self.assertNotEqual(0, run([self.build("no-instruction-guard", changed), "event"]).returncode)

        needle = "if (!mCode) { for (int i = 0; i < num_args; i++) delete args[i]; return; } // the init instruction replaced and retired this script"
        self.assertEqual(2, self.methods.count(needle))
        changed = self.methods.replace(needle, "if (!mCode) { code->mInitRun = TRUE; return; }", 2)
        exe = self.build("no-init-code-guard", changed)
        for mode in ("init-code", "function-init"):
            with self.subTest(mode=mode): self.assertNotEqual(0, run([exe, mode]).returncode)

        needle = "if (watchSelf && !self.ToRead())\n        return;"
        self.assertEqual(1, self.methods.count(needle))
        changed = self.methods.replace(needle, "if (watchSelf && !self.ToRead())\n        (void)0;", 1)
        self.assertNotEqual(0, run([self.build("no-init-owner-guard", changed), "native-init"]).returncode)


if __name__ == "__main__":
    unittest.main(verbosity=2)
