#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The release package says what the program inside it contains: the default public build has no ENet and its package carries no ENet notice; the public build made with
--with-network (the experimental co-op release) links ENet and carries ENet's licence and notice row; a build folder that says one thing about a program that is the other is refused.
No compiler, game files or network required."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package  # noqa: E402


class FlavourTests(unittest.TestCase):
    def rows(self, network):
        with patch.object(package, "NETWORK", network):
            return {row[0]: row for row in package.third_party(False)}

    def test_the_default_public_package_has_no_enet_row_and_monocypher_is_the_launcher_s(self):
        rows = self.rows(False)
        self.assertNotIn("ENet", rows)
        self.assertEqual("compiled into the launcher: the v2 update feed's signature check", rows["Monocypher"][3])
        self.assertNotIn("licenses/ENet-LICENSE", [f for row in rows.values() for f in row[4]])

    def test_the_network_package_carries_enet_and_says_the_game_has_monocypher_too(self):
        rows = self.rows(True)
        self.assertIn("ENet", rows)
        self.assertEqual(["licenses/ENet-LICENSE"], rows["ENet"][4])
        self.assertEqual("MIT", rows["ENet"][2])
        self.assertIn("and the game", rows["Monocypher"][3])

    def test_enet_symbols_are_recognised_in_a_listing(self):
        self.assertTrue(package.links_enet("00401000 T enet_host_create\n00401100 T main\n"))
        self.assertTrue(package.links_enet("U _enet_peer_send@12"))
        self.assertFalse(package.links_enet("00401000 T main\n00401100 T sendto\n00401200 T enetwork_helper\n"))

    def test_the_build_folder_names_its_flavour(self):
        with tempfile.TemporaryDirectory() as folder:
            build = Path(folder)
            self.assertEqual("public", package.build_flavour(build))
            (build / "flavour.txt").write_text("public-network\n")
            self.assertEqual("public-network", package.build_flavour(build))
            (build / "flavour.txt").write_text("public\n")
            self.assertEqual("public", package.build_flavour(build))
            (build / "flavour.txt").write_text("private\n")
            with self.assertRaises(SystemExit):
                package.build_flavour(build)

    def test_a_windows_program_with_the_mod_loader_switched_on_is_refused_while_mods_are_off_on_windows(self):
        # flavour.txt says public for a build.py --mods-windows-test build too: the program itself tells, by the switch's line that only the default build holds.
        source = package.MODS_CPP.read_text()
        self.assertIn(package.MODS_WINDOWS_OFF, source, "mods are off on Windows today: the check is live")
        self.assertIn('"%s"' % package.MODS_CHECK_COMMAND, source, "every public program holds the check command")
        default = b"\0MZ...\0--check-mods\0" + package.MODS_WINDOWS_OFF.encode() + b"\0..."
        switched_on = b"\0MZ...\0--check-mods\0off in this build\0..."
        self.assertIsNone(package.mods_windows_switch_problem(default, source))
        self.assertIn("--mods-windows-test", package.mods_windows_switch_problem(switched_on, source))
        # a test's authored stand-in, which is no CE program, is left to the other checks
        self.assertIsNone(package.mods_windows_switch_problem(b"authored candidate\n", source))
        # once the commit that enables mods on Windows has removed the line, nothing is refused
        self.assertIsNone(package.mods_windows_switch_problem(switched_on, source.replace(package.MODS_WINDOWS_OFF, "")))


if __name__ == "__main__":
    unittest.main()
