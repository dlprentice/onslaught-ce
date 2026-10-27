#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the launcher's actual entry point (main) against authored host boundaries for both platform branches: which
commands reach the v2 updater (ce_update_v2.h, counted stubs here), which reach the game, and that --apply is refused
before anything. The updater itself is tested end to end by updater_v2_tests.py.

No game, browser, Windows process, network or player file is opened.
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
#define CE_VERSION "0.1.8"
#define CE_VERSION_NOTE ""
#define CE_COMMIT "authored-test"
#define LOG_CONTINUE "test-continue"
#define STD_OUTPUT_HANDLE 1
#define STD_ERROR_HANDLE 2
#define ATTACH_PARENT_PROCESS 1
#define SDL_MESSAGEBOX_INFORMATION 1
static const char PUBLIC_KEY_HEX[]="retained-source-key";
static char sHere[PATHN],sUser[PATHN];
static int sReplay,paths,logs,finds,games,records,reports,dialogs,forbidden,handoffs,checks,rollbacks;
static char **game_args;
static int game_argc;
static int init_paths(void) { ++paths; strcpy(sHere,"authored install");strcpy(sUser,"authored user");return 1; }
static int redirected_output(int stream) { (void)stream;return 1; }
static int AttachConsole(int parent) { (void)parent;return 0; }
static void log_open(int continuation) { (void)continuation;++logs; }
static void log_close(void) {}
static void unset_env(const char *name) { (void)name; }
static void set_env(const char *name,const char *value) { (void)name;(void)value; }
#define UPD_TEST_NOTE ""
#define UPD_UPDATED 0
#define UPD_DISABLED 12
static void init_here(void) {}
static void upd_find_root(void) {}
static int upd_start_active(int argc,char **argv,int after,int *status)
{ (void)argc;(void)argv;(void)after;(void)status;++handoffs;return 0; }
static int upd_check(int explicit_cmd) { ++checks;return explicit_cmd ? 13 : 13; }
static int upd_rollback(void) { ++rollbacks;return 10; }
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
@FUNCTIONS@
static void string_out(const char *s)
{ putchar('"');for(;*s;s++){if(*s=='"'||*s=='\\')putchar('\\');putchar(*s);}putchar('"'); }
int main(int argc,char **argv)
{
 int rc=ce_test_main(argc,argv);
 printf("RESULT {\"return\":%d,\"paths\":%d,\"logs\":%d,\"finds\":%d,\"games\":%d,\"records\":%d,\"reports\":%d,\"dialogs\":%d,\"forbidden\":%d,\"handoffs\":%d,\"checks\":%d,\"rollbacks\":%d,\"game_args\":[",
 rc,paths,logs,finds,games,records,reports,dialogs,forbidden,handoffs,checks,rollbacks);
 for(int i=0;i<game_argc;i++){if(i)putchar(',');string_out(game_args[i]);}puts("]}");return 0;
}
'''


def production_functions(source):
    functions = []
    for name in ("main",):
        matches = re.findall(r"^(?:static )?(?:int|void) " + name + r"\([^\n]*\)\n\{\n.*?^\}", source, re.M | re.S)
        if len(matches) != 1:
            raise RuntimeError(f"expected one complete production {name}")
        functions.append(matches[0].replace("int main(", "int ce_test_main(", 1) if name == "main" else matches[0])
    return "\n".join(functions)


class EntryPoint(unittest.TestCase):
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

    def test_update_only_and_rollback_reach_the_updater_and_never_the_game(self):
        for platform in self.drivers:
            for args, field, rc in ((("--update-only",), "checks", 13),
                                    (("--record", "--update-only"), "checks", 13), (("--rollback",), "rollbacks", 10),
                                    (("--update-only", "-level", "100"), "checks", 13)):
                with self.subTest(platform=platform, args=args):
                    row, _ = self.invoke(platform, *args)
                    self.assertEqual(row["return"], rc)
                    self.assertEqual(row[field], 1)
                    self.assertEqual((row["games"], row["finds"], row["records"], row["handoffs"]), (0, 0, 0, 1))
            with self.subTest(platform=platform, args="--no-update-check --update-only"):
                row, _ = self.invoke(platform, "--no-update-check", "--update-only")
                self.assertEqual((row["return"], row["checks"], row["games"]), (12, 0, 0))  # no request at all

    def test_historical_apply_refuses_even_malformed_or_complete_arguments(self):
        for platform in self.drivers:
            for args in (("--apply",), ("--apply", "fresh", "install", "42"),
                         ("--update-only", "--apply", "fresh", "install", "42"), ("--version", "--apply")):
                with self.subTest(platform=platform, args=args):
                    row, result = self.invoke(platform, *args)
                    self.assertEqual(row["return"], 2)
                    self.assert_no_paths(row)
                    self.assertEqual((row["handoffs"], row["checks"]), (0, 0))
                    self.assertIn("--apply was refused before accessing installation, user or update paths", result.stderr)

    def test_version_is_successful_and_side_effect_free(self):
        for platform in self.drivers:
            row, result = self.invoke(platform, "--version")
            self.assertEqual(row["return"], 0)
            self.assert_no_paths(row)
            self.assertEqual(row["checks"], 0)
            self.assertTrue(result.stdout.startswith("Onslaught CE V0.1.8, build authored-test\n"))
            self.assertEqual(result.stderr, "")

    def test_ordinary_launch_checks_once_and_preserves_game_arguments(self):
        for platform in self.drivers:
            for args, game_args, checks in (((), [], 1), (("--", "-level", "100", "a b"), ["-level", "100", "a b"], 1),
                                            (("--no-update-check", "--", "-skipfmv"), ["-skipfmv"], 0),
                                            (("--", "--update-only", "--apply"), ["--update-only", "--apply"], 1),
                                            (("-level", "--apply"), ["-level", "--apply"], 1)):
                with self.subTest(platform=platform, args=args):
                    row, result = self.invoke(platform, *args)
                    self.assertEqual((row["return"], row["paths"], row["logs"], row["finds"], row["games"]), (0,1,1,1,1))
                    self.assertEqual((row["checks"], row["forbidden"], row["dialogs"]), (checks, 0, 0))
                    self.assertEqual(row["game_args"], game_args)

    def test_just_updated_and_replay_do_not_check(self):
        for platform in self.drivers:
            row, _ = self.invoke(platform, "--", "-level", "100", BEA_PORT_TAPE="authored.tape")
            self.assertEqual((row["return"], row["games"], row["logs"], row["checks"]), (0,1,0,0))
            row, _ = self.invoke(platform, BEA_CE_JUST_UPDATED="1")
            self.assertEqual((row["games"], row["checks"]), (1, 0))

    def test_v1_feed_swap_and_pattern_cleanup_are_absent(self):
        updater = (SOURCE.parent / "ce_update_v2.h").read_text()
        for text in (self.source, updater):
            for name in ("swap_in", "remove_tree", "tidy_old", "apply_update", "check_for_update", "prepare_update_helper"):
                self.assertIsNone(re.search(r"\b" + name + r"\s*\(", text), name)
            self.assertNotIn("sCache", text)
            self.assertIsNone(re.search(r'"[^"\n]*latest\.json[^"\n]*"', text), "a latest.json string literal")
            self.assertNotIn('".old-', text)
            self.assertNotIn("SHFileOperation", text)
        self.assertIn('#define UPD_FEED_NAME "ce-update-v2.json"', updater)
        self.assertRegex(self.source, r'static const char PUBLIC_KEY_HEX\[\] = "[0-9a-f]{64}";')


if __name__ == "__main__":
    unittest.main()
