#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The launcher's message boxes (ce_launch_rules.h): every text wrapped at CE_BOX_COLUMNS characters before SDL shows it, because SDL's own
dialogs (the X11 one, Windows') size themselves to the longest line and never wrap. Pure: a driver compiled from the header alone, no SDL, game,
network or display. The real width of a dialog on a screen is messagebox_tests.py's (a private Xvfb).

    python3 port/updater/tests/box_text_tests.py [-v]
"""
import os
import random
import re
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
UPDATER = HERE.parent
COLUMNS = 76
PIXELS_PER_CHARACTER = 10     # the widest font SDL's X11 dialog or Windows' uses, with room to spare
SCREEN = 1280


def characters(line: str) -> int:
    return len(line)


class Driver(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="ce-box-text-")
        cls.driver = Path(cls.temp.name) / "box-text"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror", str(HERE / "box_text_driver.c"),
                        "-o", str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def wrap(self, text: str, cols: int = COLUMNS, cap: int = 4096):
        r = subprocess.run([str(self.driver), "wrap", str(cols), str(cap)], input=text.encode(), capture_output=True, timeout=10)
        return None if r.returncode else r.stdout.decode()

    def texts(self) -> dict:
        out = {}
        for line in subprocess.run([str(self.driver), "texts"], capture_output=True, check=True, timeout=10).stdout.decode().splitlines():
            name, original, wrapped = line.split("\t")
            out[name] = (bytes.fromhex(original).decode(), None if wrapped == "TOO-LONG" else bytes.fromhex(wrapped).decode())
        return out


class Wrap(Driver):
    def test_the_width_is_76_and_a_screen_of_1280_pixels_is_far_off(self):
        self.assertEqual(str(COLUMNS), subprocess.run([str(self.driver), "columns"], capture_output=True, text=True, check=True).stdout.strip())
        self.assertLess(COLUMNS * PIXELS_PER_CHARACTER, SCREEN * 0.6, "a box of this width leaves room for the dialog's margins and a smaller screen")

    def test_short_text_is_unchanged_and_the_edge_cases_are_exact(self):
        self.assertEqual("Quit", self.wrap("Quit"))
        self.assertEqual("", self.wrap(""))
        self.assertEqual("aaa bbb", self.wrap("aaa bbb", 7), "exactly as wide as the line may be")
        self.assertEqual("aaa bbb\nccc", self.wrap("aaa bbb ccc", 7))
        self.assertEqual("aaa\nbbbb", self.wrap("aaa bbbb", 7), "one character over: the word moves to the next line")
        self.assertEqual("aaa\nbbbbbbbbb\nccc", self.wrap("aaa bbbbbbbbb ccc", 7), "a word longer than a line stays whole on a line of its own")
        self.assertEqual("bbbbbbbbbb", self.wrap("bbbbbbbbbb", 7), "and with no space there is no break at all")
        self.assertEqual("one\n\ntwo three\nfour", self.wrap("one\n\ntwo three\nfour", 9), "line feeds that are there stay and start a new count")
        self.assertEqual("aaa bb\nccc dd", self.wrap("aaa bb ccc dd", 7))

    def test_a_multi_byte_character_counts_once(self):
        self.assertEqual("ääää ääää", self.wrap("ääää ääää", 9), "nine characters, eighteen bytes")
        self.assertEqual("ääää\nääääx", self.wrap("ääää ääääx", 9))
        self.assertEqual("日本語 日本語", self.wrap("日本語 日本語", 7))
        self.assertEqual("日本語\n日本語日", self.wrap("日本語 日本語日", 7))

    def test_a_buffer_that_is_too_small_is_refused_and_a_width_of_zero_too(self):
        self.assertIsNone(self.wrap("abcdef", 76, cap=6), "no room for the terminator")
        self.assertEqual("abcdef", self.wrap("abcdef", 76, cap=7))
        self.assertIsNone(self.wrap("abc", 0))

    def test_words_are_never_lost_or_split_and_no_line_is_too_long_whatever_the_text(self):
        rng = random.Random(20261031)
        alphabet = ["a", "bb", "ccc", "dddd", "eeeee", "x" * 20, "y" * 40, "z" * 90, "ä" * 5, "日本語" * 6, "Onslaught", "CE", "(35.2", "MB),", "BEA.exe"]
        for case in range(300):
            cols = rng.choice([5, 10, 20, 40, 76])
            text = ""
            for _ in range(rng.randint(1, 60)):
                text += rng.choice(alphabet) + rng.choice([" ", " ", " ", "\n", "\n\n"])
            text = text.rstrip(" ")
            wrapped = self.wrap(text, cols)
            self.assertIsNotNone(wrapped, text)
            self.assertEqual(text.split(), wrapped.split(), "no word is lost, split or reordered")
            self.assertEqual(len(text), len(wrapped), "only spaces became line feeds")
            self.assertEqual(text.count("\n") + text.count(" "), wrapped.count("\n") + wrapped.count(" "))
            for line in wrapped.split("\n"):
                self.assertTrue(characters(line.strip(" ")) <= cols or " " not in line.strip(" ") , f"{case}: {line!r} at {cols}")
            self.assertEqual(wrapped, self.wrap(wrapped, cols), "wrapping twice changes nothing")


class AllTheTexts(Driver):
    def test_every_text_is_built_and_fits_the_buffer_the_launcher_gives_it(self):
        names = set(self.texts())
        self.assertEqual({"game-missing", "record", "update-download-failed", "update-not-the-release", "update-install-failed", "update-offer",
                          "update-offer-longest-versions", "bad-folder", "bad-folder-longest-reason", "bad-folder-one-long-word", "report-opened",
                          "report-not-opened"}, names)
        for name, (original, wrapped) in self.texts().items():
            self.assertIsNotNone(wrapped, name)
            self.assertLess(len(original.encode()), 2048, f"{name}: ask_box wraps into a buffer of 2048 bytes")

    def test_no_line_of_any_text_is_wider_than_the_wrap_and_so_no_box_is_wider_than_a_screen(self):
        for name, (original, wrapped) in self.texts().items():
            with self.subTest(name):
                for line in wrapped.split("\n"):
                    tokens = line.split()
                    # a line longer than the width is allowed only when it is one unbreakable word (a path, a reason in one piece)
                    self.assertTrue(characters(line) <= COLUMNS or len(tokens) == 1, f"{name}: {line!r}")
                    if len(tokens) > 1:
                        self.assertLessEqual(characters(line) * PIXELS_PER_CHARACTER, SCREEN, name)
                self.assertEqual(original.split(), wrapped.split(), f"{name}: the words are the text's own")

    def test_the_wrap_matters_for_the_long_texts_that_were_wider_than_a_screen(self):
        # before the wrap the recording question's longest line was 221 characters, the update offer's 177 and the signed-release notice's 195: at the
        # widest font those dialogs use that is over 1,280 pixels for the first and close to it for the others (SDL's own dialogs add their margins)
        longest = lambda name: max(len(l) for l in self.texts()[name][0].split("\n"))
        for name in ("record", "update-offer", "game-missing", "update-not-the-release"):
            self.assertGreater(longest(name), 110, name)
        self.assertGreater(longest("record") * PIXELS_PER_CHARACTER, SCREEN)
        self.assertGreater(longest("update-offer") * PIXELS_PER_CHARACTER + 100, SCREEN, "with the dialog's margins")

    def test_the_texts_keep_their_meaning_and_the_failure_notices_are_calm(self):
        t = {name: original for name, (original, _) in self.texts().items()}
        self.assertIn("Choose the folder that contains BEA.exe", t["game-missing"])
        self.assertIn("Record: record this session.", t["record"])
        self.assertIn("Stop recording: play without recording, and turn session recording off.", t["record"])
        self.assertIn("Onslaught CE V0.1.14 is available (you have V0.1.13).", t["update-offer"])
        self.assertIn("(35.2 MB)", t["update-offer"])
        self.assertIn("do not ask about V0.1.14 again", t["update-offer"], "the same V as the first line")
        for name in ("update-download-failed", "update-not-the-release", "update-install-failed"):
            self.assertIn("Nothing was changed, and this version is starting now.", t[name], name)
            self.assertIn("offered the update again next time", t[name], name)
            for alarming in ("error", "Error", "ERROR", "failed", "did not match", "invalid", "tamper", "attack", "!", "bad", "corrupt"):
                self.assertNotIn(alarming, t[name], f"{name}: {alarming}")
        self.assertIn("is not the one the signed release lists", t["update-not-the-release"])
        self.assertIn("so it was thrown away", t["update-not-the-release"])

    def test_a_builder_given_a_buffer_one_byte_short_says_so_and_given_exactly_enough_succeeds(self):
        lines = subprocess.run([str(self.driver), "short"], capture_output=True, text=True, check=True, timeout=10).stdout.splitlines()
        self.assertEqual(["update-offer short 0", "update-offer exact 1", "bad-folder short 0", "bad-folder exact 1", "report short 0", "report exact 1"], lines)


class Sources(unittest.TestCase):
    """Every box goes through ask_box, whose wrap is the only place the text is shaped; a new box with a literal text, or one that shows itself with
    SDL directly, would skip both the wrap and the test above."""

    @classmethod
    def setUpClass(cls):
        cls.launch = (UPDATER / "ce_launch.c").read_text()
        cls.update = (UPDATER / "ce_update_v2.h").read_text()
        cls.rules = (UPDATER / "ce_launch_rules.h").read_text()

    def test_sdl_shows_a_box_only_inside_ask_box_and_only_the_wrapped_copy(self):
        self.assertEqual(2, self.launch.count("SDL_ShowMessageBox("))
        self.assertEqual(2, self.launch.count("SDL_ShowMessageBox(&shown,"))
        self.assertEqual(0, self.update.count("SDL_ShowMessageBox("))
        start = self.launch.index("static int ask_box(")
        end = self.launch.index("\n}\n", start)
        body = self.launch[start:end]
        self.assertEqual(2, body.count("SDL_ShowMessageBox(&shown,"))
        self.assertIn("if (ce_box_wrap(box->message, wrapped, sizeof wrapped, CE_BOX_COLUMNS)) shown.message = wrapped;", body)
        self.assertIn("ask_terminal(box, which)", body, "the terminal keeps the text as written")

    def test_the_one_simple_box_wraps_its_text_too(self):
        self.assertEqual(1, self.launch.count("SDL_ShowSimpleMessageBox("))
        i = self.launch.index("SDL_ShowSimpleMessageBox(")
        self.assertIn("ce_box_wrap(text, shown, sizeof shown, CE_BOX_COLUMNS) ? shown : text", self.launch[i:i + 300])

    def test_no_box_carries_a_literal_text_of_its_own(self):
        for source in (self.launch, self.update):
            for m in re.finditer(r"SDL_MessageBoxData\s+\w+\s*=\s*\{([^}]*)\}", source):
                fields = [f.strip() for f in re.split(r",(?![^()]*\))", m.group(1))]
                if len(fields) < 4:
                    continue
                message = fields[3]
                self.assertTrue(message == "text" or re.fullmatch(r"CE_BOX_[A-Z_]+", message), f"a box with its own text: {message[:60]}")
        for call in re.findall(r"\btell_box\(([^;]*)\);", self.launch + self.update):
            argument = call.split(",", 1)[1].strip()
            self.assertTrue(argument == "text" or re.fullmatch(r"CE_BOX_[A-Z_]+", argument), f"tell_box with a literal: {argument[:60]}")
        for call in re.findall(r"\bupd_tell\(([^;]*)\);", self.update):
            argument = call.split(",", 1)[1].strip()
            self.assertTrue(argument == "text" or re.fullmatch(r"CE_BOX_[A-Z_]+", argument), f"upd_tell with a literal: {argument[:60]}")

    def test_the_failure_notices_are_warnings_not_errors_and_say_nothing_alarming_in_the_log(self):
        self.assertIn("tell_box(SDL_MESSAGEBOX_WARNING, text);", self.update)
        self.assertNotIn("SDL_MESSAGEBOX_ERROR", self.update)
        self.assertNotIn("did not verify", self.update)
        self.assertNotIn("did not match", self.update)
        self.assertIn("the update feed's signature could not be verified", self.update)
        self.assertIn("so no update is offered", self.update)
        self.assertIn("is not the one the signed feed lists", self.update)


if __name__ == "__main__":
    unittest.main()
