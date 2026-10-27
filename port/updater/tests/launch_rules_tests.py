#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile only the portable production rules; synthetic inputs, no game, signing key, network or Windows process."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
PREFIX = "https://github.com/dlprentice/onslaught-ce/releases/download/"

class LaunchRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="ce-launch-rules-")
        cls.driver = Path(cls.temp.name) / "rules"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(HERE / "launch_rules_driver.c"), "-o", str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def url(self, tail, *, version="0.1.6", platform="linux-i686", extension="tar.gz", absolute=False):
        return subprocess.run([str(self.driver), "url", tail if absolute else PREFIX + tail,
                               version, platform, extension], timeout=5).returncode

    def test_exact_published_asset_forms(self):
        self.assertEqual(self.url("v0.1.6/onslaught-ce-0.1.6-linux-i686.tar.gz"), 0)
        self.assertEqual(self.url("v0.1.0/onslaught-ce-0.1-linux-i686.tar.gz", version="0.1"), 0)
        self.assertEqual(self.url("v0.1.6/onslaught-ce-0.1.6-windows-i686.zip",
                                  platform="windows-i686", extension="zip"), 0)
        self.assertEqual(self.url("v0.1.6-rc.1/onslaught-ce-0.1.6-rc.1-linux-i686.tar.gz", version="0.1.6-rc.1"), 0)

    def test_repository_escape_and_noncanonical_urls_are_refused(self):
        name = "onslaught-ce-0.1.6-linux-i686.tar.gz"
        tails = ["../../../../../other/repo/releases/download/v9/" + name,
                 "%2e%2e/" + name, "./" + name, "../" + name, "v0.1.6/../" + name,
                 "v0.1.6/" + name + "?redirect=elsewhere", "v0.1.6/" + name + "#fragment",
                 "v0.1.6/" + name + "/extra", "v0.1.6/onslaught-ce-linux-i686.tar.gz",
                 "v0.1.6/onslaught-ce-0.1.6-windows-i686.zip", "v0.1.6/" + name.replace(".", "%2e"),
                 "v0.1.6\\" + name]
        for tail in tails:
            with self.subTest(tail=tail):
                self.assertEqual(self.url(tail), 1)
        for prefix in ("http://github.com/", "https://github.com.evil/", "https://github.com/other/repo/"):
            self.assertEqual(self.url(prefix + "v0.1.6/" + name, absolute=True), 1)

    def test_version_cannot_supply_path_components(self):
        for version in ("", ".", "..", "0.1.6/../other", "0.1.6?x", "0.1.6%2fother"):
            self.assertEqual(self.url("v0.1.6/onslaught-ce-0.1.6-linux-i686.tar.gz", version=version), 1)

    def command(self, *args, raw=False, capacity=8192):
        return subprocess.run([str(self.driver), "cmd", str(int(raw)), str(capacity), *args],
                              capture_output=True, text=True, timeout=5)

    def test_system_tool_arguments_quote_empty_quotes_and_trailing_slashes(self):
        result = self.command("C:\\Program Files\\curl.exe", "", 'a"b', "C:\\with space\\")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.rstrip("\n"), '"C:\\Program Files\\curl.exe" "" "a\\"b" "C:\\with space\\\\"')

    def test_game_preserves_legacy_raw_argument_joining(self):
        result = self.command("C:\\CE\\bea.exe", "-level", "100", "a b", 'x"y', raw=True)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.rstrip("\n"), '"C:\\CE\\bea.exe" -level 100 a b x"y')

    def test_bounds_fail_before_spawn_and_do_not_touch_canary(self):
        for raw in (False, True):
            for length in (8192, 20000):
                result = self.command("bea.exe", "a" * length, raw=raw)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "\n")
        self.assertEqual(self.command("x", capacity=4).returncode, 0)  # three chars plus NUL
        self.assertEqual(self.command("x", capacity=3).returncode, 1)

    def test_windows_manifest_declares_normal_token_and_keeps_utf8(self):
        root = ET.parse(HERE.parents[1] / "platform/windows/utf8.manifest").getroot()
        level = root.find(".//{urn:schemas-microsoft-com:asm.v3}requestedExecutionLevel")
        self.assertIsNotNone(level)
        self.assertEqual(level.attrib, {"level": "asInvoker", "uiAccess": "false"})
        self.assertEqual(root.find(".//{http://schemas.microsoft.com/SMI/2019/WindowsSettings}activeCodePage").text, "UTF-8")



if __name__ == "__main__":
    unittest.main()
