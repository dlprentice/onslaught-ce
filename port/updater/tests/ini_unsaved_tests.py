#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A choice the launcher could not keep in ce.ini is said, once, with the reason.

"Skip this version" (the update question) and "Turn recording off" (the session recording question) are kept in ce.ini. A ce.ini its
owner made read-only is refused by the shared writer (ce_report.h), where 0.1.13 on Linux replaced it, and the launcher only wrote a
line in its log: the same question came back at every start with no sign of why. The launcher now says what will happen and why, in
a box, once a start for each of the two choices, or on the terminal and in the log where a switch or a test's variable gave the
answer.

The launcher's own code runs here: everything ce_launch.c has between its ce.ini reader and its game search (the texts, ini_set and
what it calls), cut out of the file whole and compiled with the real ce_report.h; say and tell_box are stand-ins that print what
they were given. The files are scratch files. Compiles with the port's SDL headers (BEA_PORT_DATA), as ce_ini_tests.py does. Nothing
here shows a box; no box of the launcher's was looked at on Windows by anyone.

    python3 port/updater/tests/ini_unsaved_tests.py [-v] [--source CE_LAUNCH_C]
"""
import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
UPDATER = HERE.parent
SOURCE = UPDATER / "ce_launch.c"          # --source: another text of the file (an earlier commit's), to show what the test sees there

DRIVER = r"""
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "ce_report.h"
static char sIni[4096];
static void put(const char *how, const char *text)
{
	fputs(how, stdout);
	putchar(' ');
	for (; *text; ++text) { if (*text == '\n') printf("\\n"); else putchar(*text); }
	printf("\n");
}
static void say(const char *fmt, ...)
{
	char text[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	put("SAY", text);
}
static void tell_box(SDL_MessageBoxFlags flags, const char *text)
{
	put(flags == SDL_MESSAGEBOX_WARNING ? "BOX" : "BOX-OTHER", text);
}
%(region)s
/* driver INI [quiet] KEY=VALUE ...: each write in turn, as one start of the launcher makes them */
int main(int argc, char **argv)
{
	if (argc < 3) return 2;
	snprintf(sIni, sizeof sIni, "%%s", argv[1]);
	for (int i = 2; i < argc; ++i)
	{
		char key[64];
		const char *equals = strchr(argv[i], '=');
		if (!strcmp(argv[i], "quiet")) { %(quiet)s continue; }
		if (!equals || (size_t)(equals - argv[i]) >= sizeof key) return 2;
		snprintf(key, sizeof key, "%%.*s", (int)(equals - argv[i]), argv[i]);
		printf("RESULT %%s %%d\n", key, ini_set(key, equals + 1));
	}
	return 0;
}
"""


def build_driver(defines=()):
    data = os.environ.get("BEA_PORT_DATA")
    include = Path(data) / "prefix-sniper/include" if data else None
    if os.name == "nt" or not include or not include.is_dir():
        return None, "set BEA_PORT_DATA to the dependency cache (the port's SDL headers)"
    source = SOURCE.read_text()
    # everything between the ce.ini reader and the game search: ini_set and all it uses, whole
    after_reader = source.index("\treturn found;\n}\n", source.index("static int ini_get(")) + len("\treturn found;\n}\n")
    region = source[after_reader:source.index("/* ---- finding the game")]
    assert "static int ini_set(const char *key, const char *value)" in region
    folder = Path(tempfile.mkdtemp(prefix="ce-ini-unsaved-"))
    # the switch's own flag, where the launcher has one (--no-record sets it around its write)
    quiet = "sIniQuiet = 1;" if "static int sIniQuiet;" in region else "return 4;"
    (folder / "driver.c").write_text(DRIVER % {"region": region, "quiet": quiet})
    exe = folder / "ini-unsaved"
    built = subprocess.run([*shlex.split(os.environ.get("CC", "gcc")), "-std=gnu11", "-O0", "-Wall", "-Werror", "-Wno-unused-function",
                            "-Wno-unused-variable", *defines, "-I", str(include), "-I", str(UPDATER), "-o", str(exe), str(folder / "driver.c")],
                           capture_output=True, text=True)
    return (str(exe), "") if built.returncode == 0 else (None, "the driver did not compile:\n" + built.stderr[-3000:])


class Unsaved(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.driver, why = build_driver()
        if not cls.driver:
            if "did not compile" in why:
                raise AssertionError(why)
            raise unittest.SkipTest(why)
        # the same code as the updater tests' builds compile it (updater_v2_tests.py), where BEA_UPDATE_ANSWER answers the update question
        cls.testing_driver, why = build_driver(("-DCE_UPDATER_TESTING",))
        if not cls.testing_driver:
            raise AssertionError(why)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ce-ini-unsaved-test-")
        self.user = Path(self.temp.name) / "user"
        self.user.mkdir()
        self.ini = self.user / "ce.ini"
        self.original = b"record_sessions=1\nunknown=retain\n"
        self.ini.write_bytes(self.original)
        self.addCleanup(self.cleanup)

    def cleanup(self):
        self.user.chmod(0o700)
        if self.ini.exists():
            self.ini.chmod(0o600)
        self.temp.cleanup()

    def run_writes(self, *writes, driver=None, **env):
        clean = {k: v for k, v in os.environ.items() if k not in ("BEA_RECORD_ANSWER", "BEA_UPDATE_ANSWER")}
        r = subprocess.run([driver or self.driver, str(self.ini), *writes], env=clean | env, capture_output=True, text=True, timeout=10)
        self.assertEqual(r.returncode, 0, r.stderr)
        lines = r.stdout.splitlines()
        return ([l[4:] for l in lines if l.startswith("BOX ")], [l[4:] for l in lines if l.startswith("SAY ")],
                [l[7:] for l in lines if l.startswith("RESULT ")])

    SKIP = "Onslaught CE could not remember to skip this version, so it will offer the update again the next time it starts."
    RECORD = "Session recording could not be turned off, so Onslaught CE will ask about it again the next time it starts."
    READONLY = ("The settings file ce.ini in your Onslaught CE user folder is read-only, and Onslaught CE does not change a read-only "
                "file. To let it save choices like this one, make ce.ini writable again.")
    UNWRITABLE = ("The settings file ce.ini in your Onslaught CE user folder could not be written. Check that the folder and the file "
                  "can be written and that the disk is not full. Starting Onslaught CE with --report shows where the folder is.")

    def test_a_write_that_works_says_nothing(self):
        boxes, said, results = self.run_writes("record_sessions=0", "skip_version=0.1.15")
        self.assertEqual((boxes, said, results), ([], [], ["record_sessions 1", "skip_version 1"]))
        self.assertEqual(self.ini.read_bytes(), b"record_sessions=0\nunknown=retain\nskip_version=0.1.15\n")

    def test_a_read_only_ce_ini_is_said_in_a_box_with_the_reason(self):
        for mode in (0o444, 0o400):
            for write, what in (("record_sessions=0", self.RECORD), ("skip_version=0.1.15", self.SKIP)):
                with self.subTest(mode=oct(mode), write=write):
                    self.ini.chmod(mode)
                    boxes, said, results = self.run_writes(write)
                    self.assertEqual(results, [write.split("=")[0] + " 0"])
                    self.assertEqual(boxes, [what + "\\n\\n" + self.READONLY], "one box: what will happen, then why")
                    self.assertEqual(said, [f"could not write {write.split('=')[0]} to ce.ini; it is unchanged (the file is read-only)"])
                    self.assertEqual(self.ini.read_bytes(), self.original)
                    self.assertEqual(self.ini.stat().st_mode & 0o777, mode)

    def test_another_failure_is_said_with_its_own_reason(self):
        self.user.chmod(0o500)     # the folder cannot take the new file; ce.ini itself is writable
        try:
            boxes, said, results = self.run_writes("record_sessions=0")
        finally:
            self.user.chmod(0o700)
        if os.geteuid() == 0:
            self.skipTest("root writes into any folder")
        self.assertEqual(results, ["record_sessions 0"])
        self.assertEqual(boxes, [self.RECORD + "\\n\\n" + self.UNWRITABLE])
        self.assertEqual(said, ["could not write record_sessions to ce.ini; it is unchanged"])

    def test_each_choice_is_said_once_a_start(self):
        self.ini.chmod(0o444)
        boxes, said, results = self.run_writes("skip_version=0.1.15", "record_sessions=0", "skip_version=0.1.15", "record_sessions=0")
        self.assertEqual(results, ["skip_version 0", "record_sessions 0", "skip_version 0", "record_sessions 0"])
        self.assertEqual(boxes, [self.SKIP + "\\n\\n" + self.READONLY, self.RECORD + "\\n\\n" + self.READONLY])
        self.assertEqual(len(said), 4, "the log has a line for each refused write")

    def test_the_game_folder_the_launcher_found_by_itself_is_no_choice_and_gets_no_box(self):
        # with a read-only ce.ini the launcher finds the game in Steam at every start and cannot keep it: a box then would come at every start
        self.ini.chmod(0o444)
        boxes, said, results = self.run_writes("gamedir=/games/Battle Engine Aquila")
        self.assertEqual((boxes, results), ([], ["gamedir 0"]))
        self.assertEqual(said, ["could not write gamedir to ce.ini; it is unchanged (the file is read-only)"])

    def test_an_answer_from_a_switch_or_a_test_variable_gets_the_words_without_a_box(self):
        self.ini.chmod(0o444)
        expected = ["could not write record_sessions to ce.ini; it is unchanged (the file is read-only)", self.RECORD + "\\n\\n" + self.READONLY]
        boxes, said, _ = self.run_writes("quiet", "record_sessions=0")                       # --no-record
        self.assertEqual((boxes, said), ([], expected))
        boxes, said, _ = self.run_writes("record_sessions=0", BEA_RECORD_ANSWER="stop")      # the tests' answer to the recording question
        self.assertEqual((boxes, said), ([], expected))
        # the updater tests' answer to the update question, in the updater tests' build, where that variable answers it
        boxes, said, _ = self.run_writes("skip_version=0.1.15", driver=self.testing_driver, BEA_UPDATE_ANSWER="skip")
        self.assertEqual(boxes, [])
        self.assertEqual(said[1], self.SKIP + "\\n\\n" + self.READONLY)

    def test_the_update_test_variable_does_not_hide_the_box_in_a_player_s_build(self):
        # BEA_UPDATE_ANSWER answers the update question only under CE_UPDATER_TESTING (ce_update_v2.h): in a player's build it is no
        # answer, so a player who has it set (for whatever reason) chose Skip this version in the box and is told in a box. The
        # recording question's variable is read in every build (ask_record), so it keeps the words on the terminal in both.
        self.ini.chmod(0o444)
        for driver, testing in ((self.driver, False), (self.testing_driver, True)):
            with self.subTest(testing=testing):
                boxes, said, _ = self.run_writes("skip_version=0.1.15", driver=driver, BEA_UPDATE_ANSWER="skip")
                words = self.SKIP + "\\n\\n" + self.READONLY
                self.assertEqual((boxes, words in said), (([], True) if testing else ([words], False)))
                boxes, said, _ = self.run_writes("record_sessions=0", driver=driver, BEA_RECORD_ANSWER="stop")
                self.assertEqual(boxes, [])
                self.assertIn(self.RECORD + "\\n\\n" + self.READONLY, said)

    def test_the_words_are_plain_and_fit_the_launcher_s_buffers(self):
        for text in (self.SKIP, self.RECORD, self.READONLY, self.UNWRITABLE):
            self.assertTrue(text.isascii())
            self.assertNotRegex(text, r"[_=;]|errno|EACCES|S_IWUSR", "no code words")
        self.assertLess(len(self.SKIP) + 2 + len(self.UNWRITABLE), 1024, "the text ini_tell_unsaved builds")
        self.assertLess(len(self.SKIP) + 2 + len(self.UNWRITABLE), 2048 - 64, "what the box wrap and the log line hold")
        source = SOURCE.read_text()
        self.assertIn("\t\tsIniQuiet = 1;\t/* --no-record:", source, "the switch sets the flag around its write")


if __name__ == "__main__":
    for i, arg in enumerate(sys.argv):
        if arg == "--source":
            SOURCE = Path(sys.argv[i + 1])
            del sys.argv[i:i + 2]
            break
    unittest.main()
