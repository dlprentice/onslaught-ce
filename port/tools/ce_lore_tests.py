#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Lore pages (port/game/ce_lore.cpp, ce_reading.h, ce_lore_pack.h and the pack's generator gen_lore_pack.py).

Held here:
- the reading page's state and bar (ce_reading.h) against a second model written from the description of the list box beside it, over random
  sequences of lines, pages, wheel turns and bar drags, with causal mutants of the header each caught;
- the generator, run on a markdown file written here with every construct the lore library uses (status header, headings, a section left out,
  tables, links, emphasis, quotations, bullets, accents and typographic marks, characters the font lacks), against the blocks it must give,
  with mutants of the generator each caught (a mutant that is not run is a failure, never a skip);
- the committed pack: six articles, every block a line of a known kind, only characters the game's font has, nothing about the tool or the
  repository, no block too long for the page's word wrap, a blurb in the interface guide's words under each title; and, where the Toolkit
  checkout is beside this repository, the pack is regenerated from its files and must match byte for byte (else that one check is reported
  skipped);
- the page's source: it opens no file, calls no writer and keeps its text free of the forbidden words.
No game, display, sound or network is used.
"""
import ast
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ce_extras_tests as base  # noqa: E402
import gen_lore_pack as gen  # noqa: E402

GAME = base.GAME
PACK = GAME / "ce_lore_pack.h"
SOURCE = gen.find_source()
ROWS, TOP, TRACK, RIGHT = 14, 130.0, 510.0, 530.0


class Model:
    """A reading page as the list box beside it behaves, written from that description and not from the header's text."""

    def __init__(self, count, pitch):
        self.count, self.top, self.pitch = count, 0, pitch

    def last(self):
        return max(0, self.count - ROWS)

    def clamp(self):
        self.top = max(0, min(self.top, self.last()))

    def move(self, delta):
        self.top += delta
        self.clamp()

    def page(self, direction):
        self.move(direction * (ROWS - 1))

    def bottom(self):
        return TOP + self.pitch * ROWS + 12.0

    def track(self):
        return self.bottom() - TOP - 10.0

    def bar_length(self):
        if self.count <= ROWS:
            return self.track()
        return max(self.pitch, self.track() * ROWS / self.count)

    def bar(self):
        top = TOP + 5.0
        if self.count - ROWS > 0:
            top += (self.track() - self.bar_length()) * self.top / (self.count - ROWS)
        return top, top + self.bar_length()

    def drag(self, y):
        if self.count <= ROWS:
            return
        travel = self.track() - self.bar_length()
        share = (y - self.bar_length() * 0.5 - TOP - 5.0) / travel
        self.move(int(share * (self.count - ROWS) + 0.5) - self.top)

    def percent(self):
        if self.count <= 0:
            return 100
        return min(self.top + ROWS, self.count) * 100 // self.count


def expected(count, pitch, ops):
    m = Model(count, pitch)
    m.clamp()
    out = []

    def line():
        top, bottom = m.bar()
        out.append(f"{m.count} {m.top} {top:.3f} {bottom:.3f} {m.percent()} {m.bottom():.3f}")
    line()
    for op in ops:
        kind = op[0]
        if kind == "u":
            m.move(-1)
        elif kind == "d":
            m.move(1)
        elif kind == "U":
            m.page(-1)
        elif kind == "D":
            m.page(1)
        elif kind == "w":
            m.move(int(op[1:]))
        elif kind == "g":
            m.drag(float(op[1:]))
        elif kind == "c":
            m.count = int(op[1:])
            m.clamp()
        line()
    return out


def random_ops(rng, n):
    ops = []
    for _ in range(n):
        k = rng.choice("udUDwwgc")
        if k in "udUD":
            ops.append(k)
        elif k == "w":
            ops.append(f"w{rng.randint(-40, 40)}")
        elif k == "g":
            ops.append(f"g{rng.uniform(120.0, 380.0):.1f}")
        else:
            ops.append(f"c{rng.randint(0, 300)}")
    return ops


def same(got, want):
    a, b = got.split(), want.split()
    return len(a) == len(b) and all(x == y or ("." in x and abs(float(x) - float(y)) <= 0.01) for x, y in zip(a, b))


def run_reading(binary, pitch, count, ops):
    done = subprocess.run([str(binary), "read", str(pitch), str(count), *ops], capture_output=True, text=True, timeout=20)
    if done.returncode:
        raise AssertionError(done.stderr or f"driver exit {done.returncode}")
    return done.stdout.splitlines()


def reading_difference(binary):
    """None when the real functions agree with the model on every sequence, else the first difference."""
    rng = random.Random(20261112)
    for pitch in (16, 18):
        for count in (0, 1, 13, 14, 15, 27, 28, 29, 100, 150, 400):
            for _ in range(12):
                ops = random_ops(rng, 40)
                got, want = run_reading(binary, pitch, count, ops), expected(count, pitch, ops)
                if len(got) != len(want):
                    return f"pitch {pitch} count {count}: {len(got)} lines against {len(want)}"
                for i, (g, w) in enumerate(zip(got, want)):
                    if not same(g, w):
                        return f"pitch {pitch} count {count}, after {ops[:i]}: got {g!r}, wanted {w!r}"
    return None


class ReadingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-lore-"))
        cls.binary = base.build(cls.tmp / "driver", GAME)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_the_reading_page_agrees_with_the_model(self):
        self.assertIsNone(reading_difference(self.binary))

    def test_contents_jump_cancel_and_failure_leave_the_right_line(self):
        # Opening/cancelling the picker only looks up the current heading.
        # Two later headings share one legal final screen; focus must distinguish them.
        ops = ["o", "w7", "o", "o", "j2", "o", "j3", "j4", "o", "j-1", "j5", "e0", "w-1", "j0"]
        want = [(0, 0, 0), (0, 7, 1), (0, 7, 1), (0, 7, 1), (1, 20, 2), (0, 20, 2),
                (1, 26, 3), (1, 26, 4), (0, 26, 4), (0, 26, 4), (0, 26, 4), (0, 26, 4),
                (0, 25, 2), (1, 0, 0)]
        result = subprocess.run([str(self.binary), "contents", *ops], check=True, capture_output=True,
                                text=True, timeout=5)
        self.assertEqual([tuple(map(int, row.split())) for row in result.stdout.splitlines()], want)

    def test_some_states_by_hand(self):
        # 40 lines: the view stops at line 26 (40 less 14), a page is 13 lines, the bar is never shorter than a line
        got = run_reading(self.binary, 16, 40, ["D", "D", "D", "D", "u", "U"])
        tops = [int(line.split()[1]) for line in got]
        self.assertEqual(tops, [0, 13, 26, 26, 26, 25, 12])
        self.assertEqual(run_reading(self.binary, 16, 10, ["d"])[1].split()[1], "0")        # an article that fits does not scroll
        long = run_reading(self.binary, 16, 4000, [])[0].split()
        self.assertAlmostEqual(float(long[3]) - float(long[2]), 16.0, places=2)              # the bar of a very long article is a line

    def test_mutants_are_caught(self):
        source = (GAME / "ce_reading.h").read_text()
        mutants = (
            ("direction * (kCeReadRows - 1)", "direction * kCeReadRows", "a page keeps one line of the last"),
            ("return (length < pitch) ? pitch : length;", "return length;", "the bar is never shorter than a line"),
            ("if (page->mTop > last)\n\t\tpage->mTop = last;", "if (page->mTop > last + 1)\n\t\tpage->mTop = last + 1;", "the view stops at the last page"),
            ("page->mTop < 0)\n\t\tpage->mTop = 0;", "page->mTop < -1)\n\t\tpage->mTop = -1;", "the view stops at the first line"),
            ("float(page->mCount - kCeReadRows) + 0.5f) - page->mTop", "float(page->mCount - kCeReadRows)) - page->mTop", "the drag rounds to the nearest line"),
            ("*top = kCeReadTop + 5.f;", "*top = kCeReadTop + 4.f;", "the bar sits inside its track"),
            ("return (shown * 100) / page->mCount;", "return (page->mTop * 100) / page->mCount;", "the percent counts the last line in view"),
            ("return kCeReadTop + pitch * kCeReadRows + 12.f;", "return kCeReadTop + pitch * kCeReadRows + 10.f;", "the panel's foot"),
            ("if (page->mCount <= kCeReadRows)\n\t\treturn track;", "", "an article that fits has a whole bar"),
            ("const float share = (y - length * 0.5f - kCeReadTop - 5.f) / travel;", "const float share = (y - kCeReadTop - 5.f) / travel;", "the bar's middle is under the pointer"),
            ("*top += (CeReadTrackLength(pitch) - length) * float(page->mTop) / float(last);", "*top += CeReadTrackLength(pitch) * float(page->mTop) / float(last + kCeReadRows);",
             "the bar reaches the foot with the last lines"),
        )
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            folder.mkdir()
            for f in GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / f.name)
            (folder / "ce_reading.h").write_text(source.replace(old, new))
            binary = base.build(folder / "driver", folder, werror=False)
            try:
                caught = reading_difference(binary) is not None
            except AssertionError:
                caught = True
            self.assertTrue(caught, f"a change to {what} went unnoticed")


FIXTURE = """# A Title

- **Status:** a header that is
  not for the player
- **Last updated:** 2026-01-01
- **Summary:** nor this

### First Section

A paragraph with **bold**, *italic*, a [link](other.md) and a caf\u00e9\u2014and \u201cquotes\u201d, an ellipsis\u2026 and a_name with [brackets] and ~3 miles.
It goes on a second line.

> A quotation that
> has two lines.

- a bullet with `code`
* another bullet

| Name | Meaning |
|------|---------|
| **One** | The first |
| Two | The second; and more |

### Left Out

This is about the SHA-256 of a file.

#### Under the left-out section

Still left out.

### After

A paragraph that mentions the Toolkit.

Plain text again.
"""

FIXTURE_WANT = [
    ("H", "First Section"),
    ("P", 'A paragraph with bold, italic, a link and a cafe - and "quotes", an ellipsis... and a name with (brackets) and about 3 miles. It goes on a second line.'),
    ("Q", "A quotation that has two lines."),
    ("B", "a bullet with code"),
    ("B", "another bullet"),
    ("B", "One: The first"),
    ("B", "Two: The second; and more"),
    ("H", "After"),
    ("P", "Plain text again."),
]


class GeneratorTests(unittest.TestCase):
    """The generator on a markdown file written here, so that it and every mutant of it run wherever the suite runs."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-lore-gen-"))
        (cls.tmp / "lore.md").write_text(FIXTURE)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def blocks(self, script):
        done = subprocess.run([sys.executable, "-I", "-c", "import sys; sys.path.insert(0, sys.argv[1]); import gen; "
                               "print(repr(gen.blocks(open(sys.argv[2]).read(), ('Left Out',))))", str(script.parent), str(self.tmp / "lore.md")],
                              capture_output=True, text=True, timeout=60)
        return ast.literal_eval(done.stdout) if not done.returncode else None        # the repr of a list of pairs of strings, written by the run above

    def test_the_blocks_are_what_the_markdown_gives(self):
        folder = self.tmp / "honest"
        folder.mkdir()
        script = folder / "gen.py"
        script.write_text(Path(gen.__file__).read_text())
        self.assertEqual(self.blocks(script), FIXTURE_WANT)

    def test_mutants_are_caught(self):
        text = Path(gen.__file__).read_text()
        mutants = (
            ('text = text.replace("**", "").replace("__", "")', 'text = text.replace("__", "")', "bold marks are removed"),
            (r'text = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", text)', 'text = text', "a link keeps its words"),
            ('"\\u2014": " - "', '"\\u2014": "-"', "a dash is spaced"),
            ('"_": " "', '"_": "_"', "an underscore, which the font lacks, is a space"),
            ('"~": "about "', '"~": "~"', "a tilde, which the font lacks, is a word"),
            ('base = unicodedata.normalize("NFKD", ch).encode("ascii", "ignore").decode()', 'base = ""', "an accent keeps its letter"),
            ("if skipping is None and title in skip_headings:", "if False:", "a section named is left out"),
            ("if skipping is not None and level <= skipping:", "if skipping is not None and level < skipping:", "a section ends at a heading of its level"),
            (r'if re.match(r"^- \*\*(Status|Last updated|Summary):\*\*", line):', "if False:", "the status header is left out"),
            ('if re.match(r"^\\s*\\|", line):', "if False:", "a table row is a bullet"),
            ('if re.match(r"^\\|\\s*:?-{2,}", nxt):', "if False:", "a table's header row is left out"),
            ('if line.startswith(">"):', 'if line.startswith("<<"):', "a quotation is a quotation"),
            ("return [(k, t) for k, t in out if t and not MAINTAINER.search(t)]", "return [(k, t) for k, t in out if t]", "a block about the tool is left out"),
            ('level, title = len(head.group(1)), head.group(2).strip()', 'level, title = len(head.group(1)), head.group(2).strip().upper()', "a heading keeps its case"),
        )
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(text.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            folder.mkdir()
            script = folder / "gen.py"
            script.write_text(text.replace(old, new))
            self.assertNotEqual(self.blocks(script), FIXTURE_WANT, f"a change to {what} went unnoticed")


def articles():
    """The committed pack read back: [(title, blurb, [(kind, text)])]."""
    text = PACK.read_text()
    found = re.findall(r'\t\{"([^"]*)", "([^"]*)",\n((?:\t\t"[^\n]*"\}?,?\n)+)', text)
    out = []
    for title, blurb, body in found:
        joined = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', body))
        lines = joined.replace('\\"', '"').replace("\\\\", "\\").split("\\n")
        out.append((title, blurb, [(l[0], l[1:]) for l in lines if l]))
    return out


class PackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.articles = articles()

    def test_six_articles_in_known_kinds(self):
        self.assertEqual(len(self.articles), len(gen.ARTICLES))
        self.assertIn(f"kCeLoreCount = {len(gen.ARTICLES)};", PACK.read_text())
        for title, blurb, blocks in self.articles:
            self.assertGreater(len(blocks), 3, title)
            self.assertTrue(all(k in "HPBQ" for k, _ in blocks), title)

    def test_only_characters_the_font_has(self):
        for title, blurb, blocks in self.articles:
            for text in (title, blurb, *[t for _, t in blocks]):
                self.assertTrue(all(32 <= ord(c) <= 126 for c in text), f"{title}: {text[:60]!r}")
                self.assertFalse(set(text) & base.LACKING, f"{title}: {text[:60]!r}")

    def test_nothing_about_the_tool(self):
        for title, blurb, blocks in self.articles:
            for kind, text in blocks:
                self.assertIsNone(gen.MAINTAINER.search(text), f"{title}: {text[:80]!r}")
                self.assertNotRegex(text, r"[0-9a-f]{32,}", f"{title}: a hash")

    def test_a_block_fits_the_word_wrap(self):
        # the game's word wrap fills 40 lines of 100 characters; a block is cut at 1590 characters by the page and should never be near it
        for title, blurb, blocks in self.articles:
            for kind, text in blocks:
                self.assertLess(len(text), 1200, f"{title}: {text[:60]!r}")

    def test_titles_and_blurbs_are_in_the_guides_words(self):
        for title, blurb, _ in self.articles:
            self.assertLessEqual(len(title), 40, title)
            self.assertTrue(blurb[0].isupper() and blurb.endswith("."), blurb)
            self.assertLess(len(blurb), 70, blurb)                       # one line of the hint area
            self.assertIsNone(base.FORBIDDEN_WORDS.search(blurb), blurb)

    def test_the_header_names_its_inputs_and_its_licence(self):
        head = PACK.read_text().split("#ifndef")[0]
        self.assertIn("SPDX-License-Identifier: CC-BY-SA-4.0", head)
        for name, _, _, _ in gen.ARTICLES:
            self.assertRegex(head, name + r" [0-9a-f]{64}")

    @unittest.skipIf(SOURCE is None, "the Toolkit checkout is not beside this repository: the committed pack was not regenerated from its lore")
    def test_the_pack_is_what_its_inputs_give(self):
        done = subprocess.run([sys.executable, "-I", str(Path(gen.__file__)), "--source", str(SOURCE), "--check"], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stdout)


class SourceTests(unittest.TestCase):
    def test_the_page_keeps_the_rules(self):
        sources = {"ce_lore.cpp": (GAME / "ce_lore.cpp").read_text(), "ce_reading.h": (GAME / "ce_reading.h").read_text()}
        found = []
        for name, text in sources.items():
            code = base.strip_comments(text)
            found += [f"{name}: writes ({m.group(1)})" for m in base.WRITERS.finditer(code)]
            found += [f"{name}: touches the live career" for _ in base.LIVE_CAREER.finditer(code)]
        self.assertIn("#ifdef CE_VERSION", sources["ce_lore.cpp"])
        for lit in base.string_literals(sources["ce_lore.cpp"]):
            if base.FORBIDDEN_WORDS.search(lit) and "%" not in lit[:2] and not lit.startswith("lore:"):
                found.append(f"ce_lore.cpp: player text {lit!r}")
        found += base.lint(sources["ce_lore.cpp"])
        self.assertEqual(found, [])

    def test_the_rules_can_fail(self):
        text = (GAME / "ce_lore.cpp").read_text()
        for old, new in (("static WCHAR none[] = L\"No articles are available.\";", "FILE *f = fopen(\"x\", \"wb\"); static WCHAR none[] = L\"No articles are available.\";"),
                         ("\"Select to read.\"", "\"Set in ce.ini.\""), ("#ifdef CE_VERSION", "")):
            self.assertIn(old, text)
            code = base.strip_comments(text.replace(old, new, 1))
            bad = bool(base.WRITERS.search(code)) or "#ifdef CE_VERSION" not in text.replace(old, new, 1) or any(
                base.FORBIDDEN_WORDS.search(l) for l in base.string_literals(text.replace(old, new, 1)) if not l.startswith("lore:"))
            self.assertTrue(bad, f"{new!r} went unnoticed")


if __name__ == "__main__":
    unittest.main(verbosity=2)
