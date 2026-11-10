#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual native quality rows/Apply, renderer policies and settings writer with authored inputs.

The extracted menu consumers and compiler diagnostics stay in ignored local-data. These host checks do not
establish native appearance, GPU behavior, a Windows filesystem or gamepad navigation.
"""
from pathlib import Path
import importlib.util
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
sys.path.insert(0, str(PORT / "tools"))
import public_d3dapp_tests


class QualityOptions(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data/quality-options-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.out = Path(tempfile.mkdtemp(prefix="native-menu-", dir=parent))
        cls.records = [{"kind": "inputs", "sha256": {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (PORT / "game/ce_quality_options.cpp", PORT / "patches/0211-ce-video-quality.patch",
                         PORT / "platform/common/ce.cpp", PORT / "platform/common/model_quality.cpp",
                         PORT / "platform/common/terrain_quality.cpp", PORT / "tools/tests/ce_quality_options_driver.cpp",
                         PORT / "tools/ce_quality_options_tests.py", ROOT / "src/MenuItem.cpp", ROOT / "src/PauseMenu.cpp")
        }}]
        spec = importlib.util.spec_from_file_location("quality_port_build", PORT / "build.py")
        build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build)
        pause = build.patched_text(["PauseMenu.cpp"])["PauseMenu.cpp"]
        first = pause.index("#ifdef CE_VERSION\n\t// Quality requests use the same native list")
        last = pause.index("#endif", first) + len("#endif")
        (cls.out / "native_quality_rows.inc").write_text(pause[first:last] + "\n")
        native = (ROOT / "src/MenuItem.cpp").read_text("latin-1")
        signatures = (r"^CMenuMenuItem::CMenuMenuItem\(", r"^void\s+CMenuMenuItem::Refresh\(",
                      r"^void\s+CMenuMenuItem::Commit\(", r"^bool\s+CMenuMenuItem::HasPendingChange\(")
        (cls.out / "native_quality_menu.inc").write_text("\n".join(
            public_d3dapp_tests.function(native, p) for p in signatures) + "\n")
        (cls.out / "native_quality_action.inc").write_text(public_d3dapp_tests.function(
            native, r"^void\s+CMenuMenuItem::ReceiveButtonAction\(") + "\n")
        render = public_d3dapp_tests.function(native, r"^void\s+CMenuMenuItem::RenderList\(")
        first = render.index("\tint\t\tvalue = GetValue();")
        last = render.index("\n\twcscpy(text, GetText());", first)
        (cls.out / "native_quality_sync.inc").write_text(render[first:last] + "\n")
        (cls.out / "native_quality_mouse.inc").write_text(public_d3dapp_tests.function(
            render, r"^\s*if\s*\(CFrontEnd::MouseClickedInRect\(lx \+ 2.f, ly, cx2, cy2\)\)") + "\n")
        (cls.out / "native_quality_apply.inc").write_text(public_d3dapp_tests.function(
            native, r"^void\s+CApplyMenuItem::ReceiveButtonAction\(") + "\n")
        for name in ("common.h", "MenuItem.h"):
            (cls.out / name).write_text("// Native boundary declarations are in the authored driver.\n")
        (cls.out / "SDL3").mkdir()
        (cls.out / "SDL3/SDL.h").write_text(
            '#pragma once\n#include <stdint.h>\ntypedef uint64_t Uint64;\n'
            'extern "C" Uint64 SDL_GetTicks(void);\nextern "C" bool SDL_OpenURL(const char *);\n')
        (cls.out / "SDL3/SDL_timer.h").write_text(
            '#pragma once\n#include <stdint.h>\ntypedef uint64_t Uint64;\nextern "C" Uint64 SDL_GetTicksNS(void);\n')
        cls.binaries = {public: cls.build("ce" if public else "reference", public=public) for public in (True, False)}

    @classmethod
    def tearDownClass(cls):
        # Keep the bounded evidence, including negative controls and any compiler failure, for the integration review.
        (cls.out / "RESULT.private.json").write_text(json.dumps(cls.records, indent=2) + "\n")

    @classmethod
    def build(cls, name, public=True, source=None):
        binary = cls.out / name
        cmd = ["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Wl,--wrap=renameat",
               "-I", str(cls.out), "-I", str(PORT / "game"), "-I", str(PORT / "platform/common"),
               '-DQUALITY_SOURCE="' + str(source or PORT / "game/ce_quality_options.cpp") + '"',
               str(PORT / "tools/tests/ce_quality_options_driver.cpp"), str(PORT / "platform/common/ce.cpp"),
               str(PORT / "platform/common/model_quality.cpp"), str(PORT / "platform/common/terrain_quality.cpp"),
               "-o", str(binary)]
        if public:
            cmd.insert(1, '-DCE_VERSION="quality-test"')
        result = subprocess.run(cmd, capture_output=True, timeout=90)
        log = cls.out / (name + "-build.private.log")
        log.write_bytes(result.stdout + result.stderr)
        cls.records.append({"kind": "compile", "name": name, "exit": result.returncode, "log": str(log)})
        if result.returncode:
            raise AssertionError("Quality UI fixture did not compile; private diagnostics: " + str(log))
        return binary

    def case(self, scenario, *, public=True, env=None, binary=None, expected=0):
        folder = Path(tempfile.mkdtemp(prefix="profile-", dir=self.out))
        if scenario != "fresh":
            folder.joinpath("ce.ini").write_text(
                "unknown=keep\nfull_models=1\nterrain_detail_distance=1\n" if scenario in ("stored", "fenced", "menus-off") else
                "unknown=keep\nfull_models=0\nterrain_detail_distance=0\n")
        environment = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(folder), "BEA_USER_DIR": str(folder)}
        environment.update(env or {})
        run = subprocess.run([str(binary or self.binaries[public]), scenario], env=environment, capture_output=True, timeout=15)
        log = folder / "driver.private.log"
        log.write_bytes(run.stdout + run.stderr)
        self.records.append({"kind": "scenario", "scenario": scenario, "public": public, "exit": run.returncode,
                             "expected": expected, "log": str(log), "binary": str(binary or self.binaries[public])})
        self.assertEqual(run.returncode, expected, "authored quality UI assertion failed; see " + str(log))
        if not expected:
            self.assertEqual(run.stdout, b"PASS\n")
        else:
            self.assertIn(b"quality UI assertion:", run.stderr)
        self.assertFalse(list(folder.glob("*.tmp-*")), "writer left temporary settings files")

    def test_choices_preserve_defaults_and_stored_preferences(self):
        for scenario in ("fresh", "defaults", "stored", "invalid", "one-row"):
            with self.subTest(scenario=scenario): self.case(scenario)

    def test_actual_keyboard_mouse_and_apply_defer_until_frame(self):
        for scenario in ("keyboard", "mouse", "round-trip"):
            with self.subTest(scenario=scenario): self.case(scenario)

    def test_persistence_refusal_keeps_session_and_preserves_old_bytes(self):
        for scenario in ("save-fails", "partial-save", "retry-later"):
            with self.subTest(scenario=scenario): self.case(scenario)

    def test_requested_extended_is_not_shown_active_after_fallback_and_pending_is_not_a_failure(self):
        for scenario in ("wording", "wording-unsaved", "wording-frontend"):
            with self.subTest(scenario=scenario): self.case(scenario)

    def test_reference_private_and_disabled_menu_have_no_rows_or_writes(self):
        self.case("fenced", public=False)
        self.case("menus-off")
        for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"):
            for value in ("1", "0", "reference"):
                with self.subTest(name=name, value=value):
                    self.case("fenced", env={name: value, "BEA_PORT_CE_LAYER": "1"})
        self.case("round-trip", env={name: "" for name in ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD")})

    def test_causal_failures(self):
        original = (PORT / "game/ce_quality_options.cpp").read_text()
        mutations = [
            ("immediate-choice", "CMenuMenuItem((EPCText)0, 3, true)", "CMenuMenuItem((EPCText)0, 3, false)", "keyboard"),
            ("rollback-unsaved", "if (!saved) PortCeSaveFailedNotice();",
             "if (!saved) { if (kind == FullModels) CE_FullModelsRequest(!value); else CE_TerrainDetailRequest(!value); PortCeSaveFailedNotice(); }", "save-fails"),
            ("lose-reopen-notice", "kind(value)\n        {\n            Refresh();",
             "kind(value)\n        {\n            results[kind] = QualitySaved; Refresh();", "save-fails"),
            ("wrong-key", 'kind == FullModels ? "full_models" : "terrain_detail_distance"',
             'kind == FullModels ? "terrain_detail_distance" : "terrain_detail_distance"', "one-row"),
            ("lose-unsaved-retry", "if (!changed && results[kind] == QualitySessionOnly)",
             "if (changed && results[kind] == QualitySessionOnly)", "retry-later"),
            ("hide-fallback", "CE_TerrainDetailStatus() == TerrainResourceFallback)", "false)", "wording"),
            ("pending-as-failure", "CE_TerrainDetailStatus() == TerrainResourceFallback)",
             "CE_TerrainDetailStatus() != TerrainActive)", "wording"),
            ("annotate-normal", "value == 1 && CE_TerrainDetailChoice() == 1 && CE_TerrainDetailStatus()",
             "CE_TerrainDetailChoice() == 1 && CE_TerrainDetailStatus()", "wording"),
            ("drop-unsaved-with-fallback", 'L"Extended (using Normal, not saved)" : L"Extended (using Normal)"',
             'L"Extended (using Normal)" : L"Extended (using Normal)"', "wording-unsaved"),
        ]
        for name, old, new, scenario in mutations:
            with self.subTest(mutant=name):
                self.assertEqual(original.count(old), 1)
                source = self.out / (name + ".cpp")
                source.write_text(original.replace(old, new))
                self.case(scenario, binary=self.build(name, source=source), expected=1)



if __name__ == "__main__":
    unittest.main()
