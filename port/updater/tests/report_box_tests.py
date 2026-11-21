#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The launcher's --report box (Windows, no console) shows the user folder whole.

The box is the one place a player who started `onslaught-ce.exe --report` from a shortcut is shown the folder that holds the logs,
and it must be possible to read the folder back exactly. The whole notice used to be wrapped at 76 columns, which broke a folder
longer than a line at one of its spaces (the wrap turns a space into a line feed): only the sentence may be wrapped.

The statements of ce_launch.c that build and show the box are compiled as they are (the whole block that follows the console test,
cut out of the file by its braces), with the real ce_launch_rules.h, on this host: SDL's one call is a stand-in that keeps the text.
Nothing here shows the box on Windows; nobody has looked at it there. No SDL, game, network or display.

    python3 port/updater/tests/report_box_tests.py [-v] [--source CE_LAUNCH_C]
"""
import os
import re
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
UPDATER = HERE.parent
SOURCE = UPDATER / "ce_launch.c"          # --source: another text of the file (an earlier commit's), to show what the test sees there
COLUMNS = 76

DRIVER = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ce_launch_rules.h"
#define PATHN %(pathn)s
#define SDL_MESSAGEBOX_INFORMATION 0x40
static char sUser[PATHN];
static int boxes;
static int SDL_ShowSimpleMessageBox(int flags, const char *title, const char *message, void *window)
{
	(void)flags; (void)window;
	++boxes;
	printf("%%s\n%%s", title, message);
	return 1;
}
static void show(int opened)
%(block)s
int main(int argc, char **argv)
{
	if (argc != 3) return 2;
	snprintf(sUser, sizeof sUser, "%%s", argv[2]);
	show(atoi(argv[1]));
	return boxes == 1 ? 0 : 3;
}
"""


def block_after(source: str, start: str, statement: str) -> str:
    """The braced block that follows `statement`, the first one after `start`; all of it, by its braces."""
    at = source.index(statement, source.index(start)) + len(statement)
    opening = source.index("{", at)
    if source[at:opening].strip():
        raise ValueError("the statement is not followed by a block")
    depth, end = 1, opening + 1
    while depth:
        depth += {"{": 1, "}": -1}.get(source[end], 0)
        end += 1
    return source[opening:end]


class ReportBox(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = SOURCE.read_text()
        block = block_after(source, "\tif (report)\n", "\t\tif (!console)\n")
        # the block is the box's and nothing else's: it reads the report text, the wrap and the folder, and shows one box
        for needed in ("ce_box_text_report(", "ce_box_wrap(", "sUser", "SDL_ShowSimpleMessageBox("):
            assert needed in block, needed
        assert block.count("SDL_ShowSimpleMessageBox(") == 1
        pathn = re.search(r"^#define PATHN (\d+)$", source, re.M).group(1)
        cls.temp = tempfile.TemporaryDirectory(prefix="ce-report-box-")
        folder = Path(cls.temp.name)
        (folder / "driver.c").write_text(DRIVER % {"pathn": pathn, "block": block})
        cls.driver = folder / "report-box"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(UPDATER),
                        str(folder / "driver.c"), "-o", str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def box(self, opened: bool, folder: str) -> str:
        shown = subprocess.run([str(self.driver), "1" if opened else "0", folder], capture_output=True, timeout=10)
        self.assertEqual(shown.returncode, 0, "one box is shown")
        title, text = shown.stdout.decode().split("\n", 1)
        self.assertEqual(title, "Onslaught CE")
        return text

    FOLDERS = (
        r"C:\Users\Someone\AppData\Roaming\Onslaught CE",                                                   # the default, short
        "C:\\Users\\" + "A" * 39 + r"\AppData\Roaming\Onslaught CE",                                          # the default with a 39-character account name: 77 characters
        r"C:\Users\Someone With A Long Account Name\AppData\Roaming\Onslaught CE",
        r"\\server\share\home folders\someone with a long name\games\Onslaught CE user folder\chosen",   # a folder BEA_USER_DIR chose, with spaces
        "D:\\" + "\\".join(["a folder with spaces in every part of its name"] * 6),
        "D:\\" + "x" * 300,                                                                                     # no space at all
        "D:\\Games\\" + " ".join(["Onslaught"] * 40),                                                           # nothing but spaces to break at
        r"C:\Users\Zoë Müller\AppData\Roaming\Onslaught CE and a much longer folder name chosen by its owner",
    )

    def test_the_folder_is_shown_whole_on_the_last_line_whatever_its_length(self):
        for opened in (True, False):
            for folder in self.FOLDERS:
                with self.subTest(opened=opened, folder=folder):
                    text = self.box(opened, folder)
                    lines = text.split("\n")
                    self.assertEqual(lines[-1], folder, "the folder is the last line, exactly as it is")
                    self.assertEqual(text.count(folder), 1)
                    for line in lines[:-1]:
                        self.assertLessEqual(len(line), COLUMNS, "the sentence before it is wrapped")
                    flat = " ".join(" ".join(lines[:-1]).split())
                    self.assertTrue(flat.startswith("The problem report page is open in your browser." if opened else
                                                    "Your browser could not be opened."))
                    self.assertTrue(flat.endswith("Please describe what happened, and attach the file onslaught-ce.log from this folder:"))

    def test_the_longest_folder_the_launcher_can_hold_still_fits_the_box_s_buffer(self):
        longest = "C:\\" + ("a folder " * 500)[:4000].rstrip() + "\\end"
        self.assertEqual(self.box(True, longest).split("\n")[-1], longest)


if __name__ == "__main__":
    for i, arg in enumerate(sys.argv):
        if arg == "--source":
            SOURCE = Path(sys.argv[i + 1])
            del sys.argv[i:i + 2]
            break
    unittest.main()
