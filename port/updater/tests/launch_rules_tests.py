#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile only the portable production rules; synthetic inputs, no game, signing key, network or Windows process."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
PREFIX = "https://github.com/dlprentice/onslaught-ce/releases/download/"

# Compile the actual helper-preparation function with Windows calls replaced by an ownership-checking fake.
# This checks collisions, exclusive copies and cleanup scope; it does not claim native Windows policy validation.
HELPER_FAKE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_PATH 260
#define PATHN 4096
#define TRUE 1
#define ERROR_ALREADY_EXISTS 183
typedef unsigned long DWORD;
static char sHere[PATHN] = "C:\\install", first[260], owned[260];
static int creates, copies, removed, bad, fail_copy, bad_temp;
static DWORD GetTempPathA(size_t n, char *p)
{ if (bad_temp) return (DWORD)n; strcpy(p, "C:\\private\\tmp\\"); return (DWORD)strlen(p); }
static DWORD GetModuleFileNameA(void *unused, char *p, size_t n)
{ (void)unused; (void)n; strcpy(p, "C:\\install\\launcher.exe"); return (DWORD)strlen(p); }
static DWORD GetCurrentProcessId(void) { return 42; }
static DWORD GetTickCount(void) { return 123; }
static DWORD GetLastError(void) { return ERROR_ALREADY_EXISTS; }
static int CreateDirectoryA(const char *p, void *security)
{
    (void)security;
    if (++creates == 1) { strcpy(first, p); return 0; } /* pre-existing directory, owned by somebody else */
    strcpy(owned, p); return 1;
}
static int CopyFileA(const char *source, const char *dest, int exclusive)
{
    (void)source; ++copies;
    if (!exclusive || strncmp(dest, owned, strlen(owned)) || dest[strlen(owned)] != '\\') bad = 1;
    return !(fail_copy && copies == 2);
}
static void remove_tree(const char *p) { ++removed; if (strcmp(p, owned) || !strcmp(p, first)) bad = 1; }
@FUNCTION@
int main(int argc, char **argv)
{
    char folder[260], copy[260];
    fail_copy = argc > 1 && !strcmp(argv[1], "fail");
    bad_temp = argc > 1 && !strcmp(argv[1], "long-temp");
    int small = argc > 1 && !strcmp(argv[1], "small-buffer");
    int ok = prepare_update_helper(folder, small ? 10 : sizeof folder, copy, sizeof copy);
    if (bad_temp || small) return ok || creates || copies || removed ? 3 : 0;
    if (bad || creates != 2 || copies != 2 || ok != !fail_copy || removed != fail_copy) return 1;
    if (!strcmp(folder, first) || strcmp(folder, owned)) return 2;
    return 0;
}
'''


class LaunchRules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="ce-launch-rules-")
        cls.driver = Path(cls.temp.name) / "rules"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(HERE / "launch_rules_driver.c"), "-o", str(cls.driver)], check=True)
        source = (HERE.parent / "ce_launch.c").read_text()
        functions = re.findall(r"^static int prepare_update_helper\([^\n]+\)\n\{\n.*?^\}", source, re.M | re.S)
        if len(functions) != 1:
            raise RuntimeError("expected one complete production helper-preparation function")
        fake = Path(cls.temp.name) / "helper.c"
        fake.write_text(HELPER_FAKE.replace("@FUNCTION@", functions[0]))
        cls.helper = Path(cls.temp.name) / "helper"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(fake), "-o", str(cls.helper)], check=True)

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

    def test_update_helper_leaves_collisions_and_unrelated_temp_files_alone(self):
        self.assertEqual(subprocess.run([str(self.helper)], timeout=5).returncode, 0)

    def test_failed_helper_copy_only_removes_its_owned_directory(self):
        self.assertEqual(subprocess.run([str(self.helper), "fail"], timeout=5).returncode, 0)

    def test_truncated_helper_paths_fail_without_filesystem_changes(self):
        for option in ("long-temp", "small-buffer"):
            self.assertEqual(subprocess.run([str(self.helper), option], timeout=5).returncode, 0)


if __name__ == "__main__":
    unittest.main()
