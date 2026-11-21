#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Goodies Catalogue's rule table (port/game/ce_goodie_rules.h), made by gen_goodie_rules.py from the Companion's tables.

Held here: the table has a row for each of the game's 233 slots and every row is the rule a player reads, in the characters the game's
font has (so no row is shown with a hole in it); a dozen rows are what the Companion's published rules give, written out here by hand
(a level as the level select writes it, a Goodie by its kind and number, the evidence class of the slot); no row breaks the interface
guide's forbidden words; every hint the Goodies page builds from a row fits in two lines of the hint area. Where the Toolkit checkout is
beside this repository the table is also regenerated from its two files and must match byte for byte, and mutants of the generator (the
level form, the Goodie names, the grade ranges, the evidence classes) are each caught. The mutants run on inputs written by the test, so
they run everywhere; without the checkout only the byte-equal regeneration is reported as skipped, not passed.
No game, display, sound or network is used.
"""
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_goodie_rules as gen  # noqa: E402
import ce_extras_tests as base  # noqa: E402

TABLE = gen.OUT
SOURCE = gen.find_source()


def rows():
    text = TABLE.read_text()
    found = re.findall(r'\{"([^"]*)", (\d)\},\t// (\d+)', text)
    return {int(slot): (rule, int(evidence)) for rule, evidence, slot in found}


class TableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = rows()
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-goodie-"))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_a_row_for_each_slot_of_the_games_table(self):
        self.assertEqual(sorted(self.rows), list(range(233)))

    def test_rows_in_the_players_words(self):
        want = {
            0: ("Complete level 1.00.", 1),
            1: ("Earn C or better on level 1.10.", 2),
            2: ("Unlock Biography 2, then earn C or better on level 2.00.", 0),
            3: ("Unlock Biography 3, then earn C or better on level 2.31 or 2.32.", 2),
            33: ("Defeat 40 infantry.", 2),
            40: ("Destroy 25 aircraft and defeat 80 infantry.", 2),
            50: ("Unlocked by a mission script in level 5.21 or 5.22.", 2),
            66: ("Earn C or better on 26 campaign missions.", 2),
            67: ("Unlocked by a mission script in Race Level 1.", 2),
            71: ("Complete level 7.41 or 7.42.", 3),
            74: ("Earn S rank on 20 campaign missions.", 1),
            78: ("Earn C or better on level 1.00.", 1),
            120: ("Earn C or better on level 8.00.", 2),
            121: ("Earn B or better on level 1.00.", 1),
            164: ("Earn A or better on level 1.00.", 1),
            200: ("Earn A or better on level 7.10.", 2),
            201: ("Watch Cutscene 1 during normal play.", 2),
            232: ("Watch Cutscene 33 during normal play.", 2),
        }
        for slot, expected in want.items():
            self.assertEqual(self.rows[slot], expected, f"slot {slot}")

    def test_every_row_is_a_sentence_the_font_can_show(self):
        for slot, (rule, evidence) in self.rows.items():
            self.assertRegex(rule, r"^[A-Z][A-Za-z0-9 ,.']+\.$", f"slot {slot}: {rule!r}")
            self.assertFalse(set(rule) & base.LACKING, f"slot {slot}")
            self.assertIsNone(base.FORBIDDEN_WORDS.search(rule), f"slot {slot}: {rule!r}")
            self.assertNotRegex(rule, r"levels? \d{3}", f"slot {slot}: a level is written 2.11, never 211")
            self.assertNotRegex(rule, r"Goodie \d", f"slot {slot}: a Goodie is named by its kind")
            self.assertIn(evidence, range(4), f"slot {slot}: only the slots the wall shows (or never shows) are rules")

    @staticmethod
    def widths(binary, face, lines):
        done = subprocess.run([str(binary), "width", face], input="\n".join(lines) + "\n", capture_output=True, text=True, timeout=60)
        return [float(x) for x in done.stdout.split()]

    def hints(self):
        for slot, (rule, _) in self.rows.items():
            if slot <= 232 and not 71 <= slot <= 73:
                yield slot, rule

    def test_no_internal_provenance_strings_in_table(self):
        text = TABLE.read_text()
        for phrase in ("Seen in play.", "Matches the game's code.", "From the developers' source.", "kCeGoodieEvidence"):
            self.assertNotIn(phrase, text)

    def balance_problem(self, binary, face):
        """None when every shown Goodie's hint, broken as the page breaks it (CeBreakAt, by the face's widths), is as even as any break
        can make it and, where two lines can hold it at all, no line is wider than the hint area; else the first that is not."""
        area = 420.0
        for slot, text in self.hints():
            whole = self.widths(binary, face, [text])[0]
            spaces = [i for i, c in enumerate(text) if c == " " and 0 < i < len(text) - 1]
            halves = [self.widths(binary, face, [text[:i], text[i + 1:]]) for i in spaces]
            best = min(max(h) for h in halves)
            done = subprocess.run([str(binary), "balance", face, text], capture_output=True, text=True, timeout=20)
            at, widest = int(done.stdout.split()[0]), float(done.stdout.split()[1])
            if abs(widest - best) > 0.05:
                return f"slot {slot} ({face}): the wider line is {widest} where {best} can be had: {text!r}"
            if at not in spaces or abs(max(self.widths(binary, face, [text[:at], text[at + 1:]])) - widest) > 0.05:
                return f"slot {slot} ({face}): the break is not at a space that gives the width it reports: {text!r}"
            # a sentence the face holds in two lines must come out as two lines that fit; one the face holds in one line is not broken by
            # the page at all (the game's wrap gives one line), so only the break itself is judged here
            if best <= area < whole and widest > area:
                return f"slot {slot} ({face}): a line of {widest} is wider than the {area} of the hint area: {text!r}"
        return None

    def test_a_hint_breaks_in_two_even_lines_by_width(self):
        # the page breaks a sentence the game's wrap would spread over two lines at the space that makes the wider line narrowest by the
        # face's own widths (CeBreakAt). Three stand-in faces: nine pixels a character, a proportional one and a wide proportional one;
        # the real face is the game's, and the page logs a line wider than its area for the evidence runs.
        binary = base.build(self.tmp / "driver", base.GAME)
        for face in ("even", "mixed", "wide"):
            self.assertIsNone(self.balance_problem(binary, face))

    def test_balance_mutants_are_caught(self):
        source = (base.GAME / "ce_name_text.h").read_text()
        mutants = (
            ("(wider < bestWidth)", "(wider > bestWidth)", "the narrowest of the breaks is the one used"),
            ("const float wider = (a > b) ? a : b;", "const float wider = (a < b) ? a : b;", "the wider of the two lines is the one judged"),
            ("if (text[i] != ' ')\n\t\t\tcontinue;", "if (text[i] != 'x')\n\t\t\tcontinue;", "a break is made at a space"),
            ("if ((best < 0) || (wider < bestWidth))", "if (wider < bestWidth)", "the first space is a candidate"),
            ("for (int i = 1; i < length - 1; i++)", "for (int i = 1; i < length / 2; i++)", "every space is looked at"),
            ("const float a = measure(text, i), b = measure(text + i + 1, length - i - 1);", "const float a = float(i), b = float(length - i - 1);",
             "the break is balanced by the face's widths and not by a count of characters"),
        )
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            folder.mkdir()
            for f in base.GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / f.name)
            (folder / "ce_name_text.h").write_text(source.replace(old, new))
            binary = base.build(folder / "driver", folder, werror=False)
            found = [self.balance_problem(binary, face) for face in ("even", "mixed", "wide")]
            self.assertTrue(any(found), f"a change to {what} went unnoticed")

    def unlocked(self, binary, rule):
        done = subprocess.run([str(binary), "unlock", rule], capture_output=True, text=True, timeout=20)
        return tuple(int(x) for x in done.stdout.split())

    def test_a_rule_that_unlocks_a_goodie_first_is_found_by_its_words(self):
        # "Unlock Biography 3, then ...": the page puts the Goodie's title in place of those words when the career has found it
        binary = base.build(self.tmp / "driver", base.GAME)
        names = {slot: gen.kind_name(slot) for slot in range(233)}
        unlocking = {slot: rule for slot, (rule, _) in self.rows.items() if rule.startswith("Unlock ")}
        self.assertEqual(sorted(unlocking), [2, 3, 4, 5, 6, 7])
        for slot, rule in unlocking.items():
            got = self.unlocked(binary, rule)
            self.assertEqual(got[0], slot - 1, f"slot {slot}: {rule!r}")        # each of these unlocks the Goodie before it (Biography 2 for slot 2... as the table says)
            self.assertEqual(rule[7:7 + got[1]], names[got[0]], f"slot {slot}")
        self.test_unlock_expectations(binary)

    def test_the_unlock_reader_mutants_are_caught(self):
        source = (base.GAME / "ce_name_text.h").read_text()
        mutants = (
            ('const char *lead = "Unlock ";', 'const char *lead = "Unlock";', "the lead words"),
            ("return kCeKindStarts[k].mFirst + number - 1;", "return kCeKindStarts[k].mFirst + number;", "the number counts from one"),
            ("*length = n + 1 + digits;", "*length = n + digits;", "the length covers the whole name"),
            ('{66, "Race Level"}', '{67, "Race Level"}', "each kind starts where the wall's does"),
            ("if (!digits || (number < 1))", "if (!digits)", "no Goodie number zero"),
        )
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"unlock-mutant-{n}"
            folder.mkdir()
            for f in base.GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / f.name)
            (folder / "ce_name_text.h").write_text(source.replace(old, new))
            binary = base.build(folder / "driver", folder, werror=False)
            try:
                self.test_unlock_expectations(binary)
                caught = False
            except AssertionError:
                caught = True
            self.assertTrue(caught, f"a change to {what} went unnoticed")

    def test_unlock_expectations(self, binary=None):
        binary = binary or base.build(self.tmp / "driver", base.GAME)
        cases = (("Unlock Biography 3, then earn C or better.", (2, 11)), ("Unlock Unit 3, then complete level 2.05.", (10, 6)),
                 ("Unlock Race Level 2 first.", (67, 12)), ("Unlock Extra 1, then.", (74, 7)), ("Unlock Biography 0, then.", (-1, 0)),
                 ("Complete level 1.00.", (-1, 0)), ("Unlock Bio 3.", (-1, 0)), ("Unlock Unit 0, then.", (-1, 0)),
                 ("unlock Biography 3.", (-1, 0)), ("Unlock Cutscene 33, then.", (233, 11)))
        for rule, want in cases:
            self.assertEqual(self.unlocked(binary, rule), want, rule)

    def test_the_header_names_its_inputs(self):
        head = TABLE.read_text().split("#ifndef")[0]
        for name in gen.FILES:
            self.assertRegex(head, name + r" [0-9a-f]{64}")


ORDER = [100, 110, 200, 205, 212, 221, 231, 300, 311, 312, 321, 400, 411, 421, 431, 441, 500, 511, 521, 522, 531, 600, 611, 621, 631,
         641, 700, 711, 721, 731, 741, 742, 800, 811, 821, 831, 841, 851, 861, 871, 881, 891, 899]


def fixture(folder):
    """The two input files in the shape the generator reads, written for this test: 78 explicit rules (slots 0 to 77), the campaign's
    order of 43 levels and the evidence line. The rule texts are the Companion's shapes; the expected rows below are written by hand."""
    explicit = {n: f"Defeat {10 + n} infantry." for n in range(78)}
    explicit.update({0: "Complete level 100.", 1: "Earn C or better on level 110.", 2: "Unlock Goodie 001, then earn C or better on level 200.",
                     3: "Unlock Goodie 002, then earn C or better on level 231 or 232.", 33: "Defeat 40 infantry.",
                     50: "Unlocked by mission script in level 521 or 522.", 67: "Unlocked by mission script in Race 1.",
                     71: "Complete level 741 or 742.", 74: "Earn S rank on 20 campaign missions.", 77: "Unlock Goodie 008, then complete level 205."})
    text = "CampaignWorldOrder =\n    [" + ", ".join(str(n) for n in ORDER) + "];\n" + "\n".join(f'[{n}] = "{explicit[n]}"' for n in sorted(explicit)) + "\n"
    (folder / gen.FILES[0]).write_text(text)
    (folder / gen.FILES[1]).write_text("x switch { 2 => GoodieEvidence.SeenInGame,\n 0 or 8 or (>= 74 and <= 78) or 121 or 164 => GoodieEvidence.GameCodeChecked }\n")


FIXTURE_WANT = {
    0: ("Complete level 1.00.", 1), 1: ("Earn C or better on level 1.10.", 2), 2: ("Unlock Biography 2, then earn C or better on level 2.00.", 0),
    3: ("Unlock Biography 3, then earn C or better on level 2.31 or 2.32.", 2), 33: ("Defeat 40 infantry.", 2),
    50: ("Unlocked by a mission script in level 5.21 or 5.22.", 2), 67: ("Unlocked by a mission script in Race Level 1.", 2),
    71: ("Complete level 7.41 or 7.42.", 3), 73: ("Defeat 83 infantry.", 3), 70: ("Defeat 80 infantry.", 2), 74: ("Earn S rank on 20 campaign missions.", 1),
    77: ("Unlock Unit 1, then complete level 2.05.", 1), 78: ("Earn C or better on level 1.00.", 1), 79: ("Earn C or better on level 1.10.", 2),
    81: ("Earn C or better on level 2.05.", 2), 120: ("Earn C or better on level 8.99.", 2), 121: ("Earn B or better on level 1.00.", 1),
    163: ("Earn B or better on level 8.99.", 2), 164: ("Earn A or better on level 1.00.", 1), 200: ("Earn A or better on level 8.41.", 2),
    201: ("Watch Cutscene 1 during normal play.", 2), 231: ("Watch Cutscene 31 during normal play.", 2),
    232: ("Watch Cutscene 33 during normal play.", 2),
}


class GeneratorFixtureTests(unittest.TestCase):
    """The generator against inputs written here, so that it and every mutant of it are run wherever the suite runs, with or without the
    Toolkit checkout beside this repository. A mutant that is not run is a failure, never a skip."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-goodie-fixture-"))
        cls.inputs = cls.tmp / "inputs"
        cls.inputs.mkdir()
        fixture(cls.inputs)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def table(self, script):
        """The fixture's rows as the given generator script builds them, or the reason it could not."""
        done = subprocess.run([sys.executable, "-I", "-c", "import sys; sys.path.insert(0, sys.argv[1]); import gen; print(gen.build(sys.argv[2]))",
                               str(script.parent), str(self.inputs)], capture_output=True, text=True, timeout=60)
        if done.returncode:
            return None
        found = re.findall(r'\{"([^"]*)", (\d)\},\t// (\d+)', done.stdout)
        return {int(slot): (rule, int(evidence)) for rule, evidence, slot in found}

    def differences(self, rows):
        if rows is None:
            return ["the generator did not run"]
        if sorted(rows) != list(range(233)):
            return ["not a row for each slot"]
        return [f"slot {slot}: {rows[slot]} for {want}" for slot, want in FIXTURE_WANT.items() if rows[slot] != want]

    def test_the_generator_gives_the_hand_written_rows(self):
        folder = self.tmp / "honest"
        folder.mkdir()
        script = folder / "gen.py"
        script.write_text(Path(gen.__file__).read_text().replace("ROOT = Path(__file__).resolve().parents[2]", f"ROOT = Path({str(gen.ROOT)!r})"))
        self.assertEqual(self.differences(self.table(script)), [])

    def test_mutants_are_caught(self):
        text = Path(gen.__file__).read_text()
        mutants = (
            ('return f"{n // 100}.{n % 100:02d}"', 'return f"{n // 100}.{n % 100}"', "a level is written 2.11"),
            ('lambda m: kind_name(int(m.group(1)))', 'lambda m: "Goodie " + m.group(1)', "a Goodie is named by its kind and number"),
            ("(range(78, 121), 78, \"C\")", "(range(78, 120), 78, \"C\")", "the grade ranges"),
            ("(range(121, 164), 121, \"B\")", "(range(121, 164), 121, \"A\")", "the grade of the second range"),
            ("if s in (0, 8) or 74 <= s <= 78 or s in (121, 164):", "if s in (0, 8) or 74 <= s <= 77 or s in (121, 164):", "the evidence classes"),
            ("if 71 <= s <= 73:\n            return 3", "if 71 <= s <= 72:\n            return 3", "slots the wall never shows"),
            ('33 if s == 232 else s - 200', 's - 200', "the last cutscene"),
            ('text = text.replace("Watch cutscene", "Watch Cutscene")', 'text = text', "a cutscene is a proper noun"),
            ('r"a mission script in Race Level \\1"', 'r"a mission script in Race \\1"', "a race level is named as the wall names it"),
        )
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(text.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            folder.mkdir()
            script = folder / "gen.py"
            script.write_text(text.replace(old, new).replace("ROOT = Path(__file__).resolve().parents[2]", f"ROOT = Path({str(gen.ROOT)!r})"))
            self.assertTrue(self.differences(self.table(script)), f"a change to {what} went unnoticed")


@unittest.skipIf(SOURCE is None, "the Toolkit checkout is not beside this repository: the committed table was not regenerated from the Toolkit's own files")
class GeneratorTests(unittest.TestCase):
    def test_the_table_is_what_its_inputs_give(self):
        done = subprocess.run([sys.executable, "-I", str(Path(gen.__file__)), "--source", str(SOURCE), "--check"], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
