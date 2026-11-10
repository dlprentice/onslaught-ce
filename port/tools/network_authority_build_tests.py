#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Standalone authority allocator boundary; no compiler, game, or network run."""
from contextlib import ExitStack
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build


class AuthorityBuildTests(unittest.TestCase):
    def setUp(self):
        scratch = build.ROOT / "local-data/network-authority-build-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        mono = self.root / "src/monocypher-4.0.2/src"
        mono.mkdir(parents=True)
        (mono / "monocypher.c").write_text("/* authored fixture */\n")
        for key, value in {"PUBLIC": True, "WITH_NETWORK": True, "WITH_ICE": False,
                           "WITH_NATIVE_SIGNALING": False, "WITH_TEXT_CHAT": False,
                           "BUILD": self.root / "build", "CE_DEFINES": []}.items():
            self.stack.enter_context(patch.object(build, key, value))
        self.stack.enter_context(patch.object(build.deps, "SRC", self.root / "src"))

    def test_authority_uses_host_allocator_on_both_targets(self):
        for target in ("linux", "windows"):
            with self.subTest(target=target):
                rows = [row for row in build.port_sources(target) if row[0].name == "ce_network_boot_authority.cpp"]
                self.assertEqual([(build.PORT / "game/ce_network_boot_authority.cpp", "pcxx", "")], rows)

    def test_existing_game_hooks_keep_game_compilation_and_allocator(self):
        # The explicit reviewed set of host-allocator units; the museum's two are checked with their adapter in ce_museum_visit_build_tests.py.
        rows = [(source, rule) for source, rule, _ in build.port_sources("linux")
                if source.parent == build.PORT / "game" and source.name not in (
                    'ce_network_boot_authority.cpp', 'ce_network_invitation.cpp', 'ce_project.cpp',
                    'ce_museum_visit.cpp', 'ce_museum_visit_generate.cpp')]
        self.assertTrue(rows)
        self.assertTrue(all(rule == "qcxx" for _, rule in rows))
        self.assertEqual("bea_game_new", build.GAME_ALLOCATOR["_Znwj"])
        self.assertEqual("bea_game_delete", build.GAME_ALLOCATOR["_ZdlPv"])

    def test_default_public_excludes_authority(self):
        build.WITH_NETWORK = False
        for target in ("linux", "windows"):
            self.assertFalse(any(source.name in ('ce_network_boot_authority.cpp', 'ce_network_invitation.cpp')
                                 for source, _, _ in build.port_sources(target)))

    def test_generated_authority_rule_cannot_apply_game_symbol_renames(self):
        with patch.object(build, "link_flags", return_value=""):
            for target in ("linux", "windows"):
                with self.subTest(target=target):
                    text = build.ninja_file(target, [], False, True).read_text()
                    edge = next(line for line in text.splitlines()
                                if line.startswith("build ") and "ce_network_boot_authority.cpp.o:" in line)
                    self.assertIn(": pcxx ", edge)
                    command = text.split("rule pcxx\n", 1)[1].splitlines()[0]
                    self.assertIn("-std=gnu++17", command)
                    self.assertNotIn("--redefine-syms", command)
                    self.assertNotIn("-include ", command)



    def test_invitation_uses_host_allocator_on_both_targets(self):
        for target in ("linux", "windows"):
            with self.subTest(target=target):
                rows = [row for row in build.port_sources(target) if row[0].name == "ce_network_invitation.cpp"]
                self.assertEqual([(build.PORT / "game/ce_network_invitation.cpp", "pcxx", "")], rows)


    def test_generated_invitation_rule_cannot_apply_game_symbol_renames(self):
        with patch.object(build, "link_flags", return_value=""):
            for target in ("linux", "windows"):
                with self.subTest(target=target):
                    text = build.ninja_file(target, [], False, True).read_text()
                    edge = next(line for line in text.splitlines()
                                if line.startswith("build ") and "ce_network_invitation.cpp.o:" in line)
                    self.assertIn(": pcxx ", edge)
                    command = text.split("rule pcxx\n", 1)[1].splitlines()[0]
                    self.assertIn("-std=gnu++17", command)
                    self.assertNotIn("--redefine-syms", command)
                    self.assertNotIn("-include ", command)

    def test_project_loader_uses_host_allocator_in_every_public_flavour(self):
        # Not a network module: the placement hooks of the default public build call it as well.
        for network in (True, False):
            build.WITH_NETWORK = network
            for target in ("linux", "windows"):
                with self.subTest(target=target, network=network):
                    rows = [(Path(source).name, rule, extra) for source, rule, extra in build.port_sources(target)
                            if Path(source).name == "ce_project.cpp"]
                    self.assertEqual([("ce_project.cpp", "pcxx", "")], rows)

    def test_generated_project_rule_cannot_apply_game_symbol_renames(self):
        with patch.object(build, "link_flags", return_value=""):
            for target in ("linux", "windows"):
                with self.subTest(target=target):
                    text = build.ninja_file(target, [], False, True).read_text()
                    edge = next(line for line in text.splitlines()
                                if line.startswith("build ") and "ce_project.cpp.o:" in line)
                    self.assertIn(": pcxx ", edge)
                    command = text.split("rule pcxx\n", 1)[1].splitlines()[0]
                    self.assertIn("-std=gnu++17", command)
                    self.assertNotIn("--redefine-syms", command)
                    self.assertNotIn("-include ", command)

if __name__ == "__main__":
    unittest.main()
