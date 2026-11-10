#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The unfinished features, one ce.ini line each under [Experimental] (map_editor, museum, commander, play_unit; each off unless its line is 1,
read once): what each line is, that CE writes the block once without touching a line the player set, and that every entry point to an
unfinished feature asks its own line.

- The lines themselves, as the actual port/platform/common/ce.cpp reads them (PortCeExperimental over CE_SettingGetBool) on scratch user folders:
  off with no ce.ini, no line, 0, any value but a lone 1, a line twice, a refused folder; on only for 1 (play_unit also for the older tank);
  each line turns on only its own feature; a header and comment lines between them change nothing; the retired experimental_modes switch does
  nothing; read once, so that a later edit of ce.ini changes nothing in the process.
- The block, as the actual PortCeExperimentalBlock and ce_report.h's ce_ini_edit write it: the header, one warning line and a line "mode=0"
  for each feature with no line yet, after every line the file had; written once (a second start, a second call in one process, a CRLF
  file); a value the player set (commander=1 or play_unit=tank from 0.1.13, a 1 in the block) is never changed, moved or repeated; a refused
  ce.ini is left as it was. A key the CE Settings page writes later goes above the header, a key already there changes in place.
- The entry points, as the sources hold them: Commander (the map's gate in ce_commander.cpp: only after the fence and its own line, the ini is
  not read at all on a fenced path) and playing a unit (ce_playunit.cpp), the front end (ce_frontend.cpp: each feature's rows and pages are left
  out of the pages without its line, a forced action does nothing, the Help text drops the feature) and the first main menu writing the block
  and asking each line (PortCeMenus). ce_frontend_tests.py runs those pages on fixtures with each line on alone; this suite pins the
  sources' wording that it and the commander and play-unit suites read.
- Network play is not behind these lines: no network symbol appears in their region of ce.cpp.
Causal mutants of the sources must each be caught. No game, display or device is used.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ce_setting_text_tests import SDL_STUB  # noqa: E402  (the authored SDL header the readers' file includes)

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
CE_CPP = PORT / "platform/common/ce.cpp"
CE_REPORT = PORT / "updater/ce_report.h"
FRONTEND = PORT / "game/ce_frontend.cpp"
COMMANDER = PORT / "game/ce_commander.cpp"
PLAYUNIT = PORT / "game/ce_playunit.cpp"
DRIVER = PORT / "tools/ce_experimental_driver.cpp"

MODES = ("map_editor", "museum", "commander", "play_unit")
WARNING = "; Unfinished features, off in this release. They may change or break. Set a line to 1 to try one.\n"
BLOCK = "[Experimental]\n" + WARNING + "".join(m + "=0\n" for m in MODES)


def build(out, ce_text, report_text=None):
    folder = out
    include = '#include "../../updater/ce_report.h"'
    if ce_text.count(include) != 1:
        raise AssertionError("ce.cpp no longer includes ce_report.h as the test expects")
    (folder / "ce.cpp").write_text(ce_text.replace(include, '#include "ce_report.h"'))
    (folder / "ce_report.h").write_text(report_text if report_text is not None else CE_REPORT.read_text())
    (folder / "SDL3").mkdir(exist_ok=True)
    (folder / "SDL3/SDL.h").write_text(SDL_STUB)
    binary = folder / "driver"
    command = ["g++", "-std=c++17", "-O0", "-g", "-Wall", "-Wextra", "-Wno-unused-variable", "-Wno-unused-parameter", '-DCE_VERSION="test"',
               "-I", str(folder), "-I", str(PORT / "platform/common"), str(DRIVER), str(folder / "ce.cpp"), "-o", str(binary)]
    done = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if done.returncode:
        raise AssertionError("the driver did not compile:\n" + done.stdout + done.stderr)
    return binary


def drive(binary, folder, *args):
    env = {"PATH": os.environ.get("PATH", "/usr/bin"), "HOME": "/nonexistent"}
    done = subprocess.run([str(binary), args[0], str(folder), *args[1:]], capture_output=True, text=True, timeout=20, env=env)
    if done.returncode:
        raise AssertionError("the driver failed: %s" % done.stderr)
    return done.stdout.strip()


def ini(base, name, text):
    folder = base / name
    folder.mkdir()
    if text is not None:
        (folder / "ce.ini").write_bytes(text)
    return folder


def answers(binary, folder):
    return drive(binary, folder, "all")


# (name, ce.ini bytes, the four answers: map_editor museum commander play_unit)
CASES = (
    ("no-file", None, "0 0 0 0"),
    ("empty", b"", "0 0 0 0"),
    ("other-line", b"hud_scale=1\n", "0 0 0 0"),
    ("zero", b"map_editor=0\nmuseum=0\ncommander=0\nplay_unit=0\n", "0 0 0 0"),
    ("map-editor-alone", b"map_editor=1\n", "1 0 0 0"),
    ("museum-alone", b"museum=1\n", "0 1 0 0"),
    ("commander-alone", b"commander=1\n", "0 0 1 0"),
    ("play-unit-alone", b"play_unit=1\n", "0 0 0 1"),
    ("play-unit-tank", b"play_unit=tank\n", "0 0 0 1"),
    ("play-unit-tank-case", b"play_unit= Tank \r\n", "0 0 0 1"),
    ("play-unit-other-word", b"play_unit=jeep\n", "0 0 0 0"),
    ("commander-tank", b"commander=tank\n", "0 0 0 0"),
    ("all-on", b"map_editor=1\nmuseum=1\ncommander=1\nplay_unit=1\n", "1 1 1 1"),
    ("one-crlf", b"museum=1\r\n", "0 1 0 0"),
    ("one-blanks", b"museum= 1 \n", "0 1 0 0"),
    ("one-among-others", b"hud_scale=1\nmuseum=1\ncommander=0\n", "0 1 0 0"),
    ("two", b"museum=2\n", "0 0 0 0"),
    ("word", b"museum=yes\n", "0 0 0 0"),
    ("on", b"museum=on\n", "0 0 0 0"),
    ("one-and-more", b"museum=1x\n", "0 0 0 0"),
    ("negative", b"museum=-1\n", "0 0 0 0"),
    ("twice-same", b"museum=1\nmuseum=1\n", "0 0 0 0"),
    ("twice-differ", b"museum=1\nmuseum=0\n", "0 0 0 0"),
    ("other-key-prefix", b"museum_extra=1\nmap_editor_x=1\n", "0 0 0 0"),
    ("comment", b"# museum=1\n; commander=1\n", "0 0 0 0"),
    ("the-retired-switch", b"experimental_modes=1\n", "0 0 0 0"),
    ("block-as-written", BLOCK.encode(), "0 0 0 0"),
    ("block-with-one-on", BLOCK.replace("museum=0", "museum=1").encode(), "0 1 0 0"),
    ("block-with-two-on", BLOCK.replace("map_editor=0", "map_editor=1").replace("play_unit=0", "play_unit=1").encode(), "1 0 0 1"),
    ("block-crlf", BLOCK.replace("\n", "\r\n").replace("commander=0", "commander=1").encode(), "0 0 1 0"),
    ("lines-above-and-block", b"update_check=0\ncommander=1\n\n" + BLOCK.replace("commander=0\n", "").encode(), "0 0 1 0"),
    ("other-sections-ignored", b"[Other]\nmuseum=1\n[Experimental]\n", "0 1 0 0"),
)


def run_block(binary, folder):
    return drive(binary, folder, "block")


class Lines(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.base = Path(cls.tmp.name)
        (cls.base / "actual").mkdir()
        cls.binary = build(cls.base / "actual", CE_CPP.read_text())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_what_ce_ini_says(self):
        for name, text, want in CASES:
            with self.subTest(case=name):
                self.assertEqual(answers(self.binary, ini(self.base, name, text)), want)

    def test_each_line_is_read_for_its_own_feature_in_a_fresh_process(self):
        for i, mode in enumerate(MODES):
            for value, want in (("1", "1"), ("0", "0"), ("2", "0")):
                with self.subTest(mode=mode, value=value):
                    folder = ini(self.base, f"ask-{mode}-{value}", f"{mode}={value}\n".encode())
                    self.assertEqual(drive(self.binary, folder, "ask", mode), want)
                    for j, other in enumerate(MODES):
                        if j != i:
                            self.assertEqual(drive(self.binary, folder, "ask", other), "0", f"{mode}={value} leaves {other} off")

    def test_a_name_that_is_no_feature_answers_no(self):
        folder = ini(self.base, "unknown", b"experimental=1\nmap=1\ntank=1\n")
        for name in ("experimental", "map", "tank", "", "Museum", "MUSEUM", "museums"):
            self.assertEqual(drive(self.binary, folder, "ask", name), "0", name)

    def test_a_refused_folder_answers_no(self):
        # a ce.ini that is a link, and a folder that does not exist: not read, so not on
        link = self.base / "link"
        link.mkdir()
        target = self.base / "target.ini"
        target.write_bytes(b"museum=1\ncommander=1\n")
        (link / "ce.ini").symlink_to(target)
        self.assertEqual(answers(self.binary, link), "0 0 0 0")
        self.assertEqual(answers(self.binary, self.base / "does-not-exist"), "0 0 0 0")

    def test_the_answer_is_read_once(self):
        # the process asks, then ce.ini is rewritten to the other value, then it asks again: the second answer is the first
        for mode in MODES:
            self.assertEqual(drive(self.binary, ini(self.base, f"once-a-{mode}", f"{mode}=0\n".encode()), "once", mode), "0 0")
            self.assertEqual(drive(self.binary, ini(self.base, f"once-b-{mode}", f"{mode}=1\n".encode()), "once", mode), "1 1")

    def test_the_legacy_integer_reader_is_not_confused_by_the_block(self):
        # CE_SettingGet (atoi on the first line that starts with the key) sees a key under the header, and no key for the header or the warning
        folder = ini(self.base, "legacy", b"hud_scale=1\n\n" + BLOCK.replace("commander=0", "commander=1").encode())
        self.assertEqual(drive(self.binary, folder, "get", "commander"), "1")
        self.assertEqual(drive(self.binary, folder, "get", "museum"), "0")
        self.assertEqual(drive(self.binary, folder, "get", "hud_scale"), "1")
        self.assertEqual(drive(self.binary, folder, "get", "[Experimental]"), "-1")
        self.assertEqual(drive(self.binary, folder, "get", "; Unfinished features, off in this release. They may change or break. Set a line to 1 to try one."), "-1")

    def test_mutants_of_the_lines_are_caught(self):
        text = CE_CPP.read_text()
        mutants = {
            "default-on": ('CE_SettingGetBool(mode, 0) == 1', 'CE_SettingGetBool(mode, 1) == 1'),
            "tank-for-every-mode": ('(i == 3 && CE_SettingIs(mode, "tank"))', '(i >= 0 && CE_SettingIs(mode, "tank"))'),
            "tank-lost": ('|| (i == 3 && CE_SettingIs(mode, "tank"))', ''),
            "read-every-time": ("\t\tif (on[i] < 0)\n\t\t\ton[i] =", "\t\tif (true)\n\t\t\ton[i] ="),
            "one-cache-for-all": ("static int on[4] = {-1, -1, -1, -1};\n\tfor (int i = 0; mode && i < 4; i++)\n\t{\n\t\tif (strcmp(mode, kExperimentalKeys[i]))\n\t\t\tcontinue;\n\t\tif (on[i] < 0)\n\t\t\ton[i] = ",
                                  "static int on[4] = {-1, -1, -1, -1};\n\tfor (int i = 0; mode && i < 4; i++)\n\t{\n\t\tif (strcmp(mode, kExperimentalKeys[i]))\n\t\t\tcontinue;\n\t\tif (on[i] < 0 && on[0] < 0)\n\t\t\ton[i] = "),
            "key-renamed": ('"map_editor", "museum"', '"map_editor_", "museum"'),
            "keys-swapped": ('"map_editor", "museum", "commander", "play_unit"};\n\nbool', '"map_editor", "museum", "play_unit", "commander"};\n\nbool'),
            "any-name-counts": ("\tfor (int i = 0; mode && i < 4; i++)\n\t{\n\t\tif (strcmp(mode, kExperimentalKeys[i]))\n\t\t\tcontinue;", "\tfor (int i = 0; mode && i < 4; i++)\n\t{\n\t\tif (false)\n\t\t\tcontinue;"),
        }
        for name, (old, new) in mutants.items():
            with self.subTest(mutant=name):
                self.assertEqual(text.count(old), 1, name)
                folder = self.base / ("mutant-" + name)
                folder.mkdir()
                try:
                    binary = build(folder, text.replace(old, new))
                    results = [answers(binary, ini(self.base, f"m-{name}-{c}", t)) for c, t, _ in CASES]
                    expected = [w for _, _, w in CASES]
                    for mode in MODES:
                        results.append(drive(binary, ini(self.base, f"m-{name}-once-{mode}", f"{mode}=0\n".encode()), "once", mode))
                        expected.append("0 0")
                    results.append(drive(binary, ini(self.base, f"m-{name}-unknown", b"experimental=1\n"), "ask", "experimental"))
                    expected.append("0")
                    survived = results == expected
                except AssertionError:
                    survived = False
                self.assertFalse(survived, f"mutant {name} survived")

    def test_the_lines_name_no_network_symbol(self):
        text = CE_CPP.read_text()
        region = text[text.index("static const char *const kExperimentalKeys"):text.index("// Whether ce.ini sets key to exactly value")]
        for word in ("CE_NETWORK", "network", "Network", "CE_Session", "CE_Boot", "Lockstep"):
            self.assertNotIn(word, region)


class Block(unittest.TestCase):
    """The [Experimental] block: written when the file lacks it, once, around the player's own lines."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.base = Path(cls.tmp.name)
        (cls.base / "actual").mkdir()
        cls.binary = build(cls.base / "actual", CE_CPP.read_text())
        cls.n = 0

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def folder(self, text):
        Block.n += 1
        return ini(self.base, f"b{Block.n}", text)

    def written(self, text, runs=1, binary=None):
        folder = self.folder(text)
        for _ in range(runs):
            drive(binary or self.binary, folder, "block")
        return (folder / "ce.ini").read_bytes().decode("latin-1")

    def test_a_new_profile_gets_the_block_and_nothing_else(self):
        self.assertEqual(self.written(None), BLOCK)
        self.assertEqual(self.written(b""), BLOCK)

    def test_the_block_follows_every_line_the_file_had(self):
        before = "update_check=0\nhud_scale=1\nmods=a,b\n"
        self.assertEqual(self.written(before.encode()), before + "\n" + BLOCK)

    def test_it_is_written_once(self):
        for text in (None, b"update_check=0\n", b"commander=1\n", BLOCK.encode(), b"a=1\nno-newline"):
            with self.subTest(text=text):
                once = self.written(text, 1)
                self.assertEqual(once.count("[Experimental]"), 1)
                self.assertEqual(self.written(text, 3), once)
                again = self.folder(once.encode())
                drive(self.binary, again, "block")
                self.assertEqual((again / "ce.ini").read_bytes().decode("latin-1"), once, "a second start writes nothing")

    def test_a_second_call_in_one_process_writes_nothing(self):
        folder = self.folder(b"a=1\n")
        drive(self.binary, folder, "block")  # the driver calls it twice
        self.assertEqual((folder / "ce.ini").read_bytes().decode().count("[Experimental]"), 1)

    def test_a_value_the_player_set_is_never_changed_or_repeated(self):
        # 0.1.13's commander=1 and the older play_unit=tank stay where they are; the block adds only the lines that are missing
        before = "update_check=1\ncommander=1\nplay_unit=tank\nrumble=0\n"
        got = self.written(before.encode())
        self.assertEqual(got, before + "\n[Experimental]\n" + WARNING + "map_editor=0\nmuseum=0\n")
        self.assertEqual(got.count("commander="), 1)
        self.assertEqual(got.count("play_unit="), 1)
        folder = self.folder(before.encode())
        drive(self.binary, folder, "block")
        self.assertEqual(drive(self.binary, folder, "all"), "0 0 1 1", "commander=1 and play_unit=tank still turn their features on")

    def test_every_other_line_is_kept_byte_for_byte(self):
        before = b"# the player's note\xc3\xa9\nupdate_check=0\n  indented=1\nhud_scale=2\r\nmods=x\n\n\nlast=1"
        got = self.written(before)
        self.assertTrue(got.encode("latin-1").startswith(before + b"\n"), "all lines before the block are as they were (a missing final newline added)")
        self.assertEqual(got.encode("latin-1")[len(before) + 1:], b"\n" + BLOCK.encode())

    def test_all_four_present_leaves_a_header_and_the_warning(self):
        before = "map_editor=1\nmuseum=0\ncommander=1\nplay_unit=tank\n"
        self.assertEqual(self.written(before.encode()), before + "\n[Experimental]\n" + WARNING)

    def test_a_block_the_player_edited_is_left_alone(self):
        edited = BLOCK.replace("museum=0", "museum=1").replace(WARNING, "; my own words\n") + "extra=1\n"
        self.assertEqual(self.written(edited.encode()), edited)
        removed = BLOCK.replace("play_unit=0\n", "")
        self.assertEqual(self.written(removed.encode()), removed, "a line the player removed is not put back while the header is there")

    def test_a_header_with_a_final_cr_or_blanks_counts(self):
        for header in ("[Experimental]\r\n", "[Experimental] \n", "[Experimental]\t\r\n"):
            text = "a=1\n" + header + "museum=1\n"
            self.assertEqual(self.written(text.encode()), text, header)

    def test_a_header_inside_a_longer_line_does_not_count(self):
        for text in ("; see [Experimental]\nnote=[Experimental]\n", "[Experimental]x\n", " [Experimental]\n"):
            got = self.written(text.encode())
            self.assertEqual(got, text + "\n" + BLOCK, text)

    def test_a_refused_ce_ini_is_left_as_it_was(self):
        folder = self.base / "refused"
        folder.mkdir()
        target = self.base / "refused-target.ini"
        target.write_bytes(b"update_check=0\n")
        (folder / "ce.ini").symlink_to(target)
        drive(self.binary, folder, "block")
        self.assertEqual(target.read_bytes(), b"update_check=0\n")
        self.assertTrue((folder / "ce.ini").is_symlink())
        directory = self.base / "dir-as-ini"
        (directory / "ce.ini").mkdir(parents=True)
        drive(self.binary, directory, "block")
        self.assertTrue((directory / "ce.ini").is_dir())

    def test_the_file_stays_private_and_no_temporary_file_is_left(self):
        folder = self.folder(None)
        drive(self.binary, folder, "block")
        self.assertEqual(sorted(p.name for p in folder.iterdir()), ["ce.ini"])
        self.assertEqual((folder / "ce.ini").stat().st_mode & 0o777, 0o600)

    def test_the_lines_read_back_as_off_and_a_player_turns_one_on_by_editing(self):
        folder = self.folder(None)
        drive(self.binary, folder, "block")
        self.assertEqual(drive(self.binary, folder, "all"), "0 0 0 0")
        text = (folder / "ce.ini").read_text().replace("museum=0", "museum=1")
        (folder / "ce.ini").write_text(text)
        self.assertEqual(drive(self.binary, folder, "all"), "0 1 0 0")

    def test_settings_written_later_keep_the_block_whole_and_last(self):
        folder = self.folder(b"update_check=1\n")
        drive(self.binary, folder, "block")
        # a key that is not there goes above the header; one that is there changes in place; one in the block changes in place
        self.assertEqual(drive(self.binary, folder, "set", "hud_scale", "1"), "1")
        self.assertEqual(drive(self.binary, folder, "set", "update_check", "0"), "1")
        self.assertEqual(drive(self.binary, folder, "set", "commander", "1"), "1")
        self.assertEqual(drive(self.binary, folder, "set", "cheat_malloy", "1"), "1")
        got = (folder / "ce.ini").read_text()
        self.assertEqual(got, "update_check=0\nhud_scale=1\ncheat_malloy=1\n\n[Experimental]\n" + WARNING + "map_editor=0\nmuseum=0\ncommander=1\nplay_unit=0\n")
        self.assertEqual(got.count("[Experimental]"), 1)
        # and the block is not written a second time at the next start
        drive(self.binary, folder, "block")
        self.assertEqual((folder / "ce.ini").read_text(), got)

    def test_a_new_key_goes_above_the_blank_line_that_sets_the_header_apart(self):
        for before, after in (
            (b"a=1\n\n[Experimental]\nmuseum=1\n", b"a=1\nk=7\n\n[Experimental]\nmuseum=1\n"),
            (b"a=1\r\n\r\n[Experimental]\r\nmuseum=1\r\n", b"a=1\r\nk=7\n\r\n[Experimental]\r\nmuseum=1\r\n"),
            (b"a=1\n[Experimental]\nmuseum=1\n", b"a=1\nk=7\n[Experimental]\nmuseum=1\n"),
            (b"[Experimental]\nmuseum=1\n", b"k=7\n[Experimental]\nmuseum=1\n"),
            (b"\n[Experimental]\nmuseum=1\n", b"k=7\n\n[Experimental]\nmuseum=1\n"),
            (b"a=1\n\n\n[Experimental]\nmuseum=1\n", b"a=1\n\nk=7\n\n[Experimental]\nmuseum=1\n"),
        ):
            with self.subTest(before=before):
                folder = self.folder(before)
                drive(self.binary, folder, "set", "k", "7")
                self.assertEqual((folder / "ce.ini").read_bytes(), after)

    def test_a_key_set_without_a_block_still_goes_to_the_end(self):
        folder = self.folder(b"a=1\n")
        drive(self.binary, folder, "set", "b", "2")
        self.assertEqual((folder / "ce.ini").read_text(), "a=1\nb=2\n")

    def test_a_duplicate_of_a_key_collapses_as_before(self):
        folder = self.folder(b"a=1\nb=1\na=2\n")
        drive(self.binary, folder, "set", "a", "3")
        self.assertEqual((folder / "ce.ini").read_text(), "a=3\nb=1\n")

    def test_a_start_with_the_block_already_there_leaves_the_file_alone(self):
        # F1: not even a rewrite of the same bytes (a new inode, a new mtime, an fsync) and no change of mode
        for text in (BLOCK.encode(), b"a=1\r\n" + BLOCK.replace("\n", "\r\n").encode() + b"no-newline"):
            folder = self.folder(text)
            os.utime(folder / "ce.ini", ns=(1_000_000_000, 2_000_000_000))
            (folder / "ce.ini").chmod(0o644)
            before = (folder / "ce.ini").stat()
            drive(self.binary, folder, "block")
            after = (folder / "ce.ini").stat()
            self.assertEqual((after.st_ino, after.st_mtime_ns, after.st_mode), (before.st_ino, before.st_mtime_ns, before.st_mode))
            self.assertEqual((folder / "ce.ini").read_bytes(), text)

    def test_the_block_keeps_the_mode_of_a_file_it_has_to_rewrite(self):
        # V1: a ce.ini a player shares or keeps in a dotfiles repository is not made private by the first start
        folder = self.folder(b"update_check=0\n")
        (folder / "ce.ini").chmod(0o644)
        drive(self.binary, folder, "block")
        self.assertEqual((folder / "ce.ini").read_text(), "update_check=0\n\n" + BLOCK)
        self.assertEqual((folder / "ce.ini").stat().st_mode & 0o777, 0o644)

    def run_block_logged(self, binary, folder):
        env = {"PATH": os.environ.get("PATH", "/usr/bin"), "HOME": "/nonexistent"}
        done = subprocess.run([str(binary), "block", str(folder)], capture_output=True, text=True, timeout=20, env=env)
        self.assertEqual(done.returncode, 0, done.stderr)
        return done.stderr

    def test_a_read_only_ce_ini_is_not_replaced_and_the_start_says_so_once(self):
        folder = self.folder(b"update_check=0\n")
        (folder / "ce.ini").chmod(0o444)
        before = (folder / "ce.ini").stat()
        log = self.run_block_logged(self.binary, folder)   # the driver asks twice in one process
        after = (folder / "ce.ini").stat()
        self.assertEqual((folder / "ce.ini").read_bytes(), b"update_check=0\n")
        self.assertEqual((after.st_ino, after.st_mode), (before.st_ino, before.st_mode))
        self.assertEqual(log.count("could not add the [Experimental] lines to ce.ini; it is unchanged"), 1, log)
        self.assertEqual(sorted(p.name for p in folder.iterdir()), ["ce.ini"])
        # with the block already there a read-only file needs no write and nothing is logged
        folder = self.folder(BLOCK.encode())
        (folder / "ce.ini").chmod(0o444)
        self.assertEqual(self.run_block_logged(self.binary, folder), "")

    def test_a_ce_ini_at_the_size_limit_is_not_grown_past_it(self):
        # F2: every reader refuses a ce.ini over 1 MiB and takes its defaults, so the writer must not make one
        full = b"a=1\n" * (1 << 18)
        folder = self.folder(full)
        log = self.run_block_logged(self.binary, folder)
        self.assertEqual((folder / "ce.ini").read_bytes(), full)
        self.assertIn("it is unchanged", log)

    def behaviour(self, binary):
        """What a build of the writer does with the file as such (inode, mode, a read-only file, the size limit), as plain data."""
        seen = []
        env = {"PATH": os.environ.get("PATH", "/usr/bin"), "HOME": "/nonexistent"}
        for text, mode in ((BLOCK.encode(), 0o640), (b"update_check=0\n", 0o640), (b"update_check=0\n", 0o444),
                          (b"a=1\n" * (1 << 18), 0o600), (b"a=1\n" * ((1 << 18) - 32), 0o600)):   # the last: the block would end 32 bytes past the limit
            folder = self.folder(text)
            (folder / "ce.ini").chmod(mode)
            before = (folder / "ce.ini").stat()
            done = subprocess.run([str(binary), "block", str(folder)], capture_output=True, text=True, timeout=20, env=env)
            after = (folder / "ce.ini").stat()
            seen.append((done.returncode, after.st_ino == before.st_ino, after.st_mode & 0o777, (folder / "ce.ini").stat().st_size, done.stderr))
        return seen

    def test_mutants_of_the_file_handling_are_caught(self):
        ce_text = CE_CPP.read_text()
        report = CE_REPORT.read_text()
        reference = self.behaviour(self.binary)
        self.assertEqual(reference[0][1], True, "the real writer leaves a file that has the block alone")
        mutants = {
            "always-rewrites": ("if (!key && t && ce_ini_find_header(t, len, section_header) < len) { free(t); ce_settings_dir_close(&dir); return 1; }", ""),
            "header-early-return-needs-no-header": ("if (!key && t && ce_ini_find_header(t, len, section_header) < len)", "if (!key && t)"),
            "mode-not-kept": ("if (ok && fchmod(output, (mode_t)keep_mode) != 0) ok = 0;", ""),
            "mode-always-world-readable": ("(mode_t)keep_mode", "(mode_t)0644"),
            "read-only-replaced": ("if (readonly) { free(t); ce_settings_dir_close(&dir); return 0; }", ""),
            "read-only-by-group-bit": ("readonly = !(st.st_mode & S_IWUSR);", "readonly = !(st.st_mode & S_IWGRP);"),
            "size-not-limited": ("if (size < 0 || (unsigned long)size > CE_INI_MAX_BYTES) ok = 0;", "if (size < 0) ok = 0;"),
            "size-limit-off-by-a-line": ("(unsigned long)size > CE_INI_MAX_BYTES", "(unsigned long)size > CE_INI_MAX_BYTES + 64"),
        }
        for name, (old, new) in mutants.items():
            with self.subTest(mutant=name):
                self.assertEqual(report.count(old), 1, name)
                folder = self.base / ("fm-" + name)
                folder.mkdir()
                binary = build(folder, ce_text, report.replace(old, new))
                self.assertNotEqual(self.behaviour(binary), reference, f"mutant {name} survived")

    def test_mutants_of_the_block_are_caught(self):
        ce_text = CE_CPP.read_text()
        report = CE_REPORT.read_text()
        mutants = {
            "header-not-looked-for": (report, "if (!key && t && ce_ini_find_header(t, len, section_header) < len) { free(t); ce_settings_dir_close(&dir); return 1; }", ""),
            "present-keys-repeated": (report, "(!t || !ce_ini_has_key(t, len, section_keys[i])) && fprintf", "fprintf"),
            "block-values-on": (report, '"%s=0\\n", section_keys[i]', '"%s=1\\n", section_keys[i]'),
            "no-note": (report, 'fprintf(o, "%s\\n%s", section_header, section_note ? section_note : "")', 'fprintf(o, "%s\\n", section_header)'),
            "no-separator": (report, "if (len > 0 && fputc('\\n', o) == EOF) ok = 0;", ""),
            "header-needs-no-cr-handling": (report, "while (n > 0 && (l[n - 1] == '\\r' || l[n - 1] == ' ' || l[n - 1] == '\\t')) n--;", ""),
            "header-prefix-counts": (report, "return n == h && !memcmp(l, header, h);", "return n >= h && !memcmp(l, header, h);"),
            "key-not-above-the-blank-line": (report, "if (header_at < len) header_at = ce_ini_insert_before(t, header_at);", ""),
            "key-below-the-header": (report, "if (key && !has_key && !written && at == header_at && fprintf(o, \"%s=%s\\n\", key, value) < 0) ok = 0;\n\t\tif (key && !has_key && at == header_at) written = 1;", ""),
            "key-above-even-if-present": (report, "int has_key = key && t ? ce_ini_has_key(t, len, key) : 0;", "int has_key = 0;"),
            "drops-other-lines": (report, "else if (fwrite(l, 1, n, o) != n || (!nl && fputc('\\n', o) == EOF))", "else if (key && (fwrite(l, 1, n, o) != n || (!nl && fputc('\\n', o) == EOF)))"),
            "keys-forgotten": (ce_text, "kExperimentalKeys, 4))", "kExperimentalKeys, 3))"),
        }
        probes = [(None, "n"), (b"update_check=0\n", "u"), (b"commander=1\nplay_unit=tank\n", "c"), (BLOCK.encode(), "b"), (b"a=1\r\n[Experimental]\r\nmuseum=1\r\n", "r"),
                  (b"; see [Experimental]\n", "s"), (b"x=1\nno-newline", "e"), (b"[Experimental]x\n", "p"), (b"[Experimental]\r\nmuseum=1\r\n", "q")]
        for name, (source, old, new) in mutants.items():
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, name)
                folder = self.base / ("bm-" + name)
                folder.mkdir()
                if source is report:
                    binary = build(folder, ce_text, report.replace(old, new))
                else:
                    binary = build(folder, ce_text.replace(old, new))
                caught = False
                for text, tag in probes:
                    good = self.written(text, 1)
                    mutant = self.written(text, 1, binary)
                    twice = self.written(text, 2, binary)
                    if mutant != good or twice != mutant:
                        caught = True
                for text in (b"a=1\nb=2\n", BLOCK.encode()):
                    folder2 = self.folder(text)
                    drive(self.binary, folder2, "block")
                    reference = self.folder(text)
                    drive(self.binary, reference, "block")
                    drive(self.binary, reference, "set", "zz", "1")
                    drive(self.binary, reference, "set", "museum", "1")
                    candidate = self.folder(text)
                    drive(binary, candidate, "block")
                    drive(binary, candidate, "set", "zz", "1")
                    drive(binary, candidate, "set", "museum", "1")
                    if (candidate / "ce.ini").read_bytes() != (reference / "ce.ini").read_bytes():
                        caught = True
                self.assertTrue(caught, f"mutant {name} survived")


def has(text, *pieces):
    return all(piece in text for piece in pieces)


class Entry(unittest.TestCase):
    """Every way into an unfinished feature asks its own line first."""

    def test_the_first_main_menu_writes_the_block_and_asks_each_line_once(self):
        frontend = FRONTEND.read_text()
        menus = frontend[frontend.index("bool\tPortCeMenus()"):]
        menus = menus[:menus.index("\n}\n")]
        self.assertTrue(has(menus, "PortCeExperimentalBlock();", "(void)PortCeExperimental(mode);",
                            'static const char *const modes[] = {"map_editor", "museum", "commander", "play_unit"};'))
        self.assertLess(menus.index("on = (PortCeLayer()"), menus.index("PortCeExperimentalBlock();"))
        self.assertLess(menus.index("PortCeExperimentalBlock();"), menus.index("(void)PortCeExperimental(mode);"), "the block is written before the lines are read")
        self.assertEqual(frontend.count("\n\t\t\tPortCeExperimentalBlock();"), 1, "the block is written from the one place")

    def test_the_block_is_written_only_where_the_ce_layer_is_on(self):
        # PortCeMenus is on only with the CE layer (never in a replay, a recording or a probe run), and nothing else in the game calls the writer
        for path in PORT.rglob("*.cpp"):
            if path.name in ("ce.cpp", "ce_frontend.cpp") or "tools" in path.parts:
                continue
            self.assertNotIn("PortCeExperimentalBlock", path.read_text(errors="replace"), str(path))

    def test_commander_counts_only_after_the_fence_and_its_own_line(self):
        text = COMMANDER.read_text()
        gate = 'gMap = allowed && (proofMap || PortCeExperimental("commander"));'
        self.assertEqual(text.count(gate), 1)
        self.assertLess(text.index("const bool allowed = !Fenced();"), text.index(gate))
        self.assertEqual(text.count('PortCeExperimental("'), 1, "the commander line is read at one place")
        self.assertNotIn('CE_SettingGetBool("commander"', text)

    def test_playing_a_unit_counts_only_after_the_fence_and_its_own_line(self):
        text = PLAYUNIT.read_text()
        gate = 'gWanted = !fenced && PortCeExperimental("play_unit") && !CeCmdMapOn();'
        self.assertEqual(text.count(gate), 1)
        self.assertEqual(text.count('PortCeExperimental("'), 1, "the play_unit line is read at one place")
        self.assertNotIn('CE_SettingIs("play_unit"', text)

    def test_the_front_end_offers_no_unfinished_row_without_its_line(self):
        text = FRONTEND.read_text()
        self.assertTrue(has(text, "if (ExperimentalOffered(kHubRows[i].mAction))",
                            'if (commander || strcmp(setting.mKey, "commander"))',
                            "if (!ExperimentalOffered(action))\n\t\t\treturn;"))
        mode = text[text.index("static const char\t*ExperimentalMode(int action)"):]
        mode = mode[:mode.index("\n}\n")]
        editor, museum = mode.split('return "map_editor";')[0], mode.split('return "map_editor";')[1].split('return "museum";')[0]
        for name in ("CEA_PAGE_EDITOR", "CEA_EDITOR_FIRST"):
            self.assertIn(name, editor)
        for name in ("CEA_MUSEUM", "CEA_MUSEUM_VISIT"):
            self.assertIn(name, museum)
        # every hub row that opens an unfinished feature has an action the filter names, under the feature's own line
        hub = text[text.index("static const SCeRow\tkHubRows[]"):]
        hub = hub[:hub.index("};")]
        want = {"Map Editor": "map_editor", "Museum Prototype": "museum", "Goodies Gallery": "museum"}
        for label, action in re.findall(r'\{L"([^"]+)", (CEA_[A-Z_]+)', hub):
            if label in want:
                self.assertIn(action, editor if want[label] == "map_editor" else museum, label)
        # and the features' own dispatches sit behind the one guard above them
        guard = text.index("if (!ExperimentalOffered(action))")
        for call in ("PortCeEditorBegin(", "PortCeMuseumOpen()", "PortCeMuseumVisitBegin()", "OpenPage(CEP_EDITOR);"):
            self.assertGreater(text.index(call, guard), guard, call)

    def test_the_help_pages_follow_the_features_own_lines(self):
        text = FRONTEND.read_text()
        self.assertTrue(has(text, '{CEPL_TOOLS, "map_editor",', '{CEPL_GODOT, "commander",', 'PortCeExperimental("commander") ? kControlsTipsCommander : ""',
                            "if ((plain.mIndex == index) && !PortCeExperimental(plain.mMode))", "if (!commander)\n\t\t\tmHelpRows[0].mHint.mHelp = kControlsHintPlain;"))

    def test_mutants_of_the_entry_points_are_caught(self):
        def commander_ok(t):
            return 'proofMap || PortCeExperimental("commander")' in t
        text = COMMANDER.read_text()
        self.assertTrue(commander_ok(text))
        self.assertFalse(commander_ok(text.replace('PortCeExperimental("commander")', "true")))
        text = PLAYUNIT.read_text()
        self.assertIn('!fenced && PortCeExperimental("play_unit") &&', text)
        self.assertNotIn('!fenced && PortCeExperimental("play_unit") &&', text.replace('PortCeExperimental("play_unit") && ', ""))


if __name__ == "__main__":
    unittest.main(verbosity=2)
