#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the actual suspended-updater entry points against authored host boundaries for both platform branches.

No game, browser, Windows process, network or player file is opened. Historical automatic-update controls live in
Git with their retired implementation; these controls require the conservative manual-only safety behavior.
"""
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SOURCE = Path(os.environ.get("UPDATE_RESTART_SOURCE", HERE.parent / "ce_launch.c"))
SEAMS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PATHN 4096
#define CE_VERSION "0.1.7"
#define CE_VERSION_NOTE ""
#define CE_COMMIT "authored-test"
#define LOG_CONTINUE "test-continue"
#define STD_OUTPUT_HANDLE 1
#define STD_ERROR_HANDLE 2
#define ATTACH_PARENT_PROCESS 1
#define SDL_MESSAGEBOX_INFORMATION 1
static const char PUBLIC_KEY_HEX[]="retained-source-key";
static char sHere[PATHN],sUser[PATHN];
static int sReplay,paths,logs,finds,games,records,reports,dialogs,forbidden;
static char **game_args;
static int game_argc;
static int init_paths(void) { ++paths; strcpy(sHere,"authored install");strcpy(sUser,"authored user");return 1; }
static int redirected_output(int stream) { (void)stream;return 1; }
static int AttachConsole(int parent) { (void)parent;return 0; }
static void log_open(int continuation) { (void)continuation;++logs; }
static void log_close(void) {}
static void unset_env(const char *name) { (void)name; }
static int ini_get(const char *key,char *out,size_t n) { (void)key;(void)out;(void)n;return 0; }
static int ini_set(const char *key,const char *value) { (void)key;(void)value;++forbidden;return 0; }
static int find_game(char *out,size_t n) { ++finds;snprintf(out,n,"authored game folder");return 1; }
static int start_game(char **args,int n,const char *folder)
{ (void)folder;++games;game_args=args;game_argc=n;return 0; }
static int finish_recording(int rc) { return rc; }
static void start_recording(char **args,int n) { (void)args;(void)n;++records; }
static int ask_record(void) { return 0; }
static int ce_open_report(const char *v,const char *c,const char *u,char *out,size_t n)
{ (void)v;(void)c;(void)u;++reports;snprintf(out,n,"not opened");return 0; }
static int SDL_ShowSimpleMessageBox(int flags,const char *title,const char *text,void *window)
{ (void)flags;(void)title;(void)text;(void)window;++dialogs;return 1; }
static void say(const char *text,...) { (void)text; }
/* Causal mutants reintroduce these calls into the actual entry point. None performs its named side effect. */
static void tidy_old(void) { ++forbidden; }
static void check_for_update(void) { ++forbidden; }
@FUNCTIONS@
static void string_out(const char *s)
{ putchar('"');for(;*s;s++){if(*s=='"'||*s=='\\')putchar('\\');putchar(*s);}putchar('"'); }
int main(int argc,char **argv)
{
 int rc=ce_test_main(argc,argv);
 printf("RESULT {\"return\":%d,\"paths\":%d,\"logs\":%d,\"finds\":%d,\"games\":%d,\"records\":%d,\"reports\":%d,\"dialogs\":%d,\"forbidden\":%d,\"game_args\":[",
 rc,paths,logs,finds,games,records,reports,dialogs,forbidden);
 for(int i=0;i<game_argc;i++){if(i)putchar(',');string_out(game_args[i]);}puts("]}");return 0;
}
'''


def production_functions(source):
    functions = []
    for name in ("manual_update_help", "main"):
        matches = re.findall(r"^(?:static )?(?:int|void) " + name + r"\([^\n]*\)\n\{\n.*?^\}", source, re.M | re.S)
        if len(matches) != 1:
            raise RuntimeError(f"expected one complete production {name}")
        functions.append(matches[0].replace("int main(", "int ce_test_main(", 1) if name == "main" else matches[0])
    return "\n".join(functions)


class SuspendedUpdater(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        owner = HERE.parents[2] / "local-data/port/manual-update-tests"
        owner.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="case-", dir=owner)
        cls.source = SOURCE.read_text()
        folder = Path(cls.temp.name)
        functions = production_functions(cls.source)
        cls.drivers = {}
        compiler = shlex.split(os.environ.get("CC", "cc"))
        for platform in ("windows", "linux"):
            c = folder / (platform + ".c")
            c.write_text(SEAMS.replace("@FUNCTIONS@", functions))
            exe = folder / platform
            result = subprocess.run([*compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                                     "-Wno-unused-function", "-Wno-unused-variable",
                                     *(["-D_WIN32"] if platform == "windows" else []), str(c), "-o", str(exe)],
                                    capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            cls.drivers[platform] = exe

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def invoke(self, platform, *args, **extra):
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_PORT_", "BEA_UPDATE_"))}
        env.update(extra)
        result = subprocess.run([str(self.drivers[platform]), *args], env=env, check=True,
                                capture_output=True, text=True, timeout=5)
        rows = [line[7:] for line in result.stdout.splitlines() if line.startswith("RESULT ")]
        self.assertEqual(len(rows), 1, result.stdout + result.stderr)
        return json.loads(rows[0]), result

    def assert_no_paths(self, row):
        self.assertEqual([row[k] for k in ("paths", "logs", "finds", "games", "records", "reports", "dialogs", "forbidden")], [0]*8)

    def test_update_only_is_explicitly_disabled_before_any_path_or_browser(self):
        for platform in self.drivers:
            for args in (("--update-only",), ("--no-update-check", "--update-only"),
                         ("--record", "--update-only"), ("--update-only", "--report")):
                with self.subTest(platform=platform, args=args):
                    row, result = self.invoke(platform, *args, BEA_UPDATE_FEED="file:///unread-sentinel", BEA_UPDATE_ANSWER="update")
                    self.assertEqual(row["return"], 2)
                    self.assert_no_paths(row)
                    self.assertIn("checking and installation are temporarily disabled", result.stderr)
                    self.assertIn("did not check whether your version is current", result.stderr)
                    self.assertIn("https://github.com/dlprentice/onslaught-ce/releases", result.stderr)
                    self.assertIn("new empty folder", result.stderr)
                    self.assertIn("Relative BEA_USER_DIR may place launcher files and game", result.stderr)
                    self.assertIn("careers in different folders. Identify and back up both", result.stderr)
                    self.assertIn("check careers after migration", result.stderr)
                    self.assertIn("BEA_USER_DIR to an absolute path", result.stderr)
                    self.assertIn("game's user-data folder", result.stderr)
                    self.assertIn("outside the original game and old CE folders", result.stderr)
                    self.assertNotIn("already current", result.stderr)

    def test_historical_apply_refuses_even_malformed_or_complete_arguments(self):
        for platform in self.drivers:
            for args in (("--apply",), ("--apply", "fresh", "install", "42"),
                         ("--update-only", "--apply", "fresh", "install", "42")):
                with self.subTest(platform=platform, args=args):
                    row, result = self.invoke(platform, *args)
                    self.assertEqual(row["return"], 2)
                    self.assert_no_paths(row)
                    self.assertIn("--apply was refused before accessing installation, user or update paths", result.stderr)

    def test_version_is_successful_and_side_effect_free(self):
        for platform in self.drivers:
            row, result = self.invoke(platform, "--version")
            self.assertEqual(row["return"], 0)
            self.assert_no_paths(row)
            self.assertTrue(result.stdout.startswith("Onslaught CE v0.1.7, build authored-test\n"))
            self.assertEqual(result.stderr, "")

    def test_ordinary_launch_has_no_updater_and_preserves_game_arguments(self):
        for platform in self.drivers:
            for args, game_args in (((), []), (("--", "-level", "100", "a b"), ["-level", "100", "a b"]),
                                    (("--no-update-check", "--", "-skipfmv"), ["-skipfmv"]),
                                    (("--", "--update-only", "--apply"), ["--update-only", "--apply"])):
                with self.subTest(platform=platform, args=args):
                    row, result = self.invoke(platform, *args, BEA_UPDATE_FEED="file:///unread-sentinel", BEA_UPDATE_ANSWER="update")
                    self.assertEqual((row["return"], row["paths"], row["logs"], row["finds"], row["games"]), (0,1,1,1,1))
                    self.assertEqual((row["forbidden"], row["dialogs"]), (0,0))
                    self.assertEqual(row["game_args"], game_args)
                    self.assertEqual(result.stderr, "")

    def test_replay_does_not_reintroduce_updater_or_logging(self):
        for platform in self.drivers:
            row, _ = self.invoke(platform, "--", "-level", "100", BEA_PORT_TAPE="authored.tape")
            self.assertEqual((row["return"], row["games"], row["logs"], row["forbidden"]), (0,1,0,0))

    def test_install_swap_cleanup_and_feed_implementations_are_absent(self):
        for name in ("swap_in", "remove_tree", "tidy_old", "apply_update", "check_for_update", "prepare_update_helper", "fetch"):
            self.assertIsNone(re.search(r"\b" + name + r"\s*\(", self.source), name)
        self.assertNotIn("sCache", self.source)
        # The existing signing tools still bind the source's public key, even though this launcher reads no feed.
        self.assertRegex(self.source, r'static const char PUBLIC_KEY_HEX\[\] = "[0-9a-f]{64}";')


if __name__ == "__main__":
    unittest.main()
