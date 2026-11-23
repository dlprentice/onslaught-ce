#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Extras pages' list box (port/game/ce_scroll_list.h) and the rules the Extras sources keep (port/game/ce_extras.cpp and friends).

Part one runs the list box's real functions, as the game compiles them, in a small driver (tests/ce_extras_driver.cpp) through random
sequences of moves, pages, wheel turns and track drags, and compares every step with a second model written here from the Load Game
list's behaviour: the cursor stops at the ends, a page goes to the view's edge and then a whole view, the view moves a whole row at a
time and keeps the cursor in sight, the bar covers the rows in view in a track less five at each end. Causal mutants of the header
must each be caught.

Part two reads the sources: the pages open no file for writing, call no save writer, never use the two game calls that touch the live
career (CCareer::Load, CCareerNodeLink::GetToNode), keep the whole feature default-on under the CE menus with extras=0 opt-out, and keep
their text free of the words the interface guide forbids in player text. Each rule is also run on a deliberately broken copy to show
the check can fail.

No game, display, sound or network is used.
"""
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
DRIVER = ROOT / "port/tools/tests/ce_extras_driver.cpp"
FILES = ("ce_extras.cpp", "ce_extras.h", "ce_scroll_list.h", "ce_career_view.cpp", "ce_career_view.h", "ce_service_record.cpp")

LEFT, TOP, PITCH, TRACK, RIGHT, ROWS = 132.0, 130.0, 24.0, 510.0, 530.0, 10
BOTTOM = TOP + PITCH * ROWS + 7.0
TRACK_LENGTH = BOTTOM - TOP - 10.0          # the scroll track less five at each end


class Model:
    """The list box as the Load Game list behaves, written from its description, not from the header's text."""

    def __init__(self, count):
        self.count, self.cursor, self.top = count, 0, 0

    def clamp(self):
        self.cursor = max(0, min(self.cursor, self.count - 1))
        last = max(0, self.count - ROWS)
        self.top = max(0, min(self.top, last))
        if self.cursor < self.top:
            self.top = self.cursor
        if self.cursor >= self.top + ROWS:
            self.top = self.cursor - ROWS + 1

    def move(self, delta):
        was = self.cursor
        self.cursor += delta
        self.clamp()
        return self.cursor != was

    def page(self, direction):
        was = self.cursor
        if direction < 0:
            self.cursor = self.top if self.cursor > self.top else self.cursor - ROWS
        else:
            foot = self.top + ROWS - 1
            self.cursor = foot if self.cursor < foot else self.cursor + ROWS
        self.clamp()
        return self.cursor != was

    def scroll(self, delta):
        was = self.top
        top = max(0, min(self.top + delta, max(0, self.count - ROWS)))
        self.cursor = max(top, min(self.cursor, top + ROWS - 1))
        self.top = top
        self.clamp()
        return self.top != was

    def drag(self, y):
        if self.count <= ROWS:
            return
        length = self.bar_length()
        share = (y - length * 0.5 - TOP - 5.0) / (TRACK_LENGTH - length)
        self.scroll(int(share * (self.count - ROWS) + 0.5) - self.top)      # C truncates toward zero, as int() does

    def bar_length(self):
        """The share of the track the rows in view are, never shorter than one row; the whole track when the list fits."""
        if self.count <= ROWS:
            return TRACK_LENGTH
        return max(TRACK_LENGTH * ROWS / self.count, PITCH)

    def bar(self):
        length = self.bar_length()
        top = TOP + 5.0
        if self.count > ROWS:
            top += (TRACK_LENGTH - length) * self.top / (self.count - ROWS)
        return top, top + length

    def row_at(self, y):
        if y < TOP:
            return -1
        row = int((y - TOP) / PITCH)
        if row < 0 or row >= ROWS or self.top + row >= self.count:
            return -1
        return self.top + row

    def line(self, op, moved):
        top, bottom = self.bar()
        return f"{op} {moved} {self.count} {self.cursor} {self.top} {top:.3f} {bottom:.3f} {self.row_at(TOP + 1.0)} {self.row_at(BOTTOM - 8.0)} {self.row_at(250.0)}"


def build(out, include, werror=True):
    """Compiles the driver; a mutant is compiled without -Werror (a removed line may leave a variable unused) so that it still runs."""
    done = subprocess.run(["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", *(["-Werror"] if werror else []), f"-I{include}", str(DRIVER), "-o", str(out)],
                          capture_output=True, text=True, timeout=180)
    if done.returncode:
        raise RuntimeError("the driver did not compile:\n" + done.stdout + done.stderr)
    return out


def run_list(binary, count, ops):
    done = subprocess.run([str(binary), "list", str(count), *ops], capture_output=True, text=True, timeout=20)
    if done.returncode:
        raise AssertionError(done.stderr)
    return done.stdout.splitlines()


def expected(count, ops):
    m = Model(count)
    m.clamp()
    out = [f"{m.count} {m.cursor} {m.top} {m.bar()[0]:.3f} {m.bar()[1]:.3f} {m.row_at(TOP + 1.0)} {m.row_at(BOTTOM - 8.0)} {m.row_at(250.0)}"]
    for op in ops:
        kind = op[0]
        moved = {"u": lambda: m.move(-1), "d": lambda: m.move(1), "U": lambda: m.page(-1), "D": lambda: m.page(1),
                 "w": lambda: m.scroll(int(op[1:])), "g": lambda: (m.drag(float(op[1:])), False)[1],
                 "c": lambda: (setattr(m, "cursor", int(op[1:])), m.clamp(), False)[2],
                 "n": lambda: (setattr(m, "count", int(op[1:])), m.clamp(), False)[2]}[kind]()
        out.append(m.line(op, 1 if moved else 0))
    return out


def random_ops(rng, n):
    ops = []
    for _ in range(n):
        kind = rng.choice("uuddUDwwgcn")
        if kind in "udUD":
            ops.append(kind)
        elif kind == "w":
            ops.append(f"w{rng.choice((-9, -3, -1, 1, 3, 9, 40))}")
        elif kind == "g":
            ops.append(f"g{rng.randint(120, 390)}")
        elif kind == "n":
            ops.append(f"n{rng.randint(0, 60)}")
        else:
            ops.append(f"c{rng.randint(-2, 60)}")
    return ops


def same(got, want):
    """Two output lines are equal, the bar's pixel positions within a hundredth (the game's are single-precision floats)."""
    a, b = got.split(), want.split()
    if len(a) != len(b):
        return False
    return all(x == y or ("." in x and "." in y and abs(float(x) - float(y)) <= 0.01) for x, y in zip(a, b))


def comparison(binary):
    """None when the real list agrees with the model on every sequence, else the first difference."""
    rng = random.Random(20261112)
    for count in (0, 1, 2, 9, 10, 11, 12, 20, 43, 100, 230):
        for _ in range(12):
            ops = random_ops(rng, 40)
            got, want = run_list(binary, count, ops), expected(count, ops)
            if len(got) != len(want) or not all(same(g, w) for g, w in zip(got, want)):
                for i, (g, w) in enumerate(zip(got, want)):
                    if not same(g, w):
                        return f"count {count}, after {ops[:max(i - 1, 0) + 1]}: got {g!r}, wanted {w!r}"
                return f"count {count}: {len(got)} lines against {len(want)}"
    return None


# ---- the source rules ----------------------------------------------------------------------------------------------------

WRITERS = re.compile(r"\b(fopen|freopen|open|creat|remove|unlink|rename|fwrite|fputs|fputc|fprintf\s*\(\s*(?!stderr)\w+|ofstream|"
                     r"CreateSave|WriteSave|DeleteSave|PortReplaceUserFile|PortCreateUserFile|CE_SettingSet\w*|CE_WriteSettings\w*)\s*\(")
LIVE_CAREER = re.compile(r"CCareer\s*::\s*Load\b|\bGetToNode\b|\bCAREER\s*\.\s*(Load|Save|Set\w+|Reset\w*|Unlock\w*|Add\w+)\b")
FORBIDDEN_WORDS = re.compile(r"ce\.ini|\.ini\b|--\w+|README|console|\bpath\b|\bfolder\b|\bdirectory\b|Prototype|Experimental|Companion|[~\\]", re.I)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def string_literals(text):
    out = []
    for m in re.finditer(r'L?"((?:[^"\\\n]|\\.)*)"', strip_comments(text)):
        out.append(m.group(1))
    return out


LACKING = set("$@[\\]^_{|}~`")
SMALL = {"of", "and", "the", "for", "to", "in", "a", "on"}


def player_literals(text):
    """The string literals a player can read: everything except the log lines (they follow PORT_LOG_PREFIX) and includes."""
    code = strip_comments(text)
    out = []
    for line in code.splitlines():
        if "PORT_LOG_PREFIX" in line or "fprintf(" in line or line.lstrip().startswith("#"):
            continue
        out += [m.group(1) for m in re.finditer(r'L?"((?:[^"\\\n]|\\.)*)"', line)]
    return out


def lint(text):
    """The interface guide's wording rules (T2, T3, W2) over a source's player strings; a list of what breaks them."""
    found = []
    for lit in player_literals(text):
        if not re.search(r"[A-Za-z]", lit) or "%" in lit or lit in ("rb", "r", "wb") or lit.startswith(("data", "extras")):
            continue
        bad = sorted(set(lit) & LACKING)
        if bad:
            found.append(f"{lit!r}: the font lacks {''.join(bad)}")
        words = lit.replace(":", "").split()
        if lit.rstrip().endswith(".") or len(words) > 4:
            if not lit[0].isupper() or not lit.rstrip().endswith("."):
                found.append(f"{lit!r}: a hint starts with a capital and ends with a full stop")
        elif any(w[0].isalpha() and not (w[0].isupper() or w.lower() in SMALL) for w in words):
            found.append(f"{lit!r}: a label is in Title Case")
    return found


def violations(sources):
    """Every rule broken in {name: text}; an empty list when the sources keep them."""
    found = []
    for name, text in sources.items():
        code = strip_comments(text)
        for m in WRITERS.finditer(code):
            found.append(f"{name}: writes ({m.group(1)})")
        for m in LIVE_CAREER.finditer(code):
            found.append(f"{name}: touches the live career ({m.group(0).strip()})")
        if name.endswith(".cpp") and "#ifdef CE_VERSION" not in text:
            found.append(f"{name}: not behind CE_VERSION")
        if name.endswith(".cpp"):
            for s in string_literals(text):
                if FORBIDDEN_WORDS.search(s) and not s.startswith("extras:") and "%" not in s[:2]:
                    found.append(f"{name}: player text {s!r}")
    for name, text in sources.items():
        if name.endswith(".cpp") and name != "ce_career_view.cpp":      # that file holds no player text, only the reasons its log gives
            found += [f"{name}: {issue}" for issue in lint(text)]
    host = sources["ce_extras.cpp"]
    if not re.search(r'sOffered\s*=\s*\(PortCeMenus\(\)\s*&&\s*\(CE_SettingGetBool\("extras",\s*1\)\s*==\s*1\)\)', host):
        found.append("ce_extras.cpp: the switch is not default-on with extras=0 opt-out under the CE menus")
    return found


JUKEBOX = "ce_jukebox.cpp"


def function_body(code, signature):
    """The text from a function's signature to the brace that closes it, or None."""
    at = code.find(signature)
    if at < 0:
        return None
    opening = code.index("{", at)
    depth, end = 1, opening + 1
    while depth and end < len(code):
        depth += (code[end] == "{") - (code[end] == "}")
        end += 1
    return code[at:end]


def wiring_violations(text):
    """What the Music Player page does with the pure parts that the host models test (ce_jukebox_state.h, ce_ogg_length.h): the page must ask
    TrackStarts of a custom track before it hands the file to the game and refuse the track when it cannot start, and its hint must say so
    for the row that failed. The state machine and the length reader are tested elsewhere; this is the wiring between them and the game."""
    code = strip_comments(text)
    found = []
    play = function_body(code, "bool\tDevicePlay(int row)")
    if play is None:
        return ["ce_jukebox.cpp: DevicePlay is not found"]
    refuse = re.search(r"if\s*\(\s*!name\s*\|\|\s*!TrackStarts\(name\)\s*\)\s*\{[^{}]*\breturn\s+false\s*;[^{}]*\}", play)
    if not refuse:
        found.append("DevicePlay does not refuse a custom track that TrackStarts cannot start (the round-1 defect: Playing over silence)")
    elif refuse.start() > play.find("MUSIC.Play(path)") >= 0:
        found.append("DevicePlay hands the track to the game before it asks TrackStarts")
    if "MUSIC.Play(path)" not in play:
        found.append("DevicePlay no longer plays a custom track")
    starts = function_body(code, "bool\tTrackStarts(const char *path)")
    if starts is None or not re.search(r"CeOggStarts\(head,\s*int\(got\),\s*got\s*<\s*sizeof\(head\)\)", starts):
        found.append("TrackStarts does not judge the file by CeOggStarts over the bytes it read")
    elif "fopen(path" not in starts or re.search(r'fopen\(path,\s*"[^r]', starts):
        found.append("TrackStarts must open the file for reading only")
    hint = function_body(code, "void\tPageHint(int index, WCHAR *help, WCHAR *extra, int n)")
    if hint is None or not re.search(r'if\s*\(index\s*==\s*sBox\.mFailed\)\s*Widen\(help,\s*n,\s*"This track cannot be played\."\)', hint):
        found.append("the hint of the row that failed does not say \"This track cannot be played.\"")
    row = function_body(code, "void\tPageRow(int index, SCeListRow *out)")
    if row is None or not re.search(r'else if\s*\(index\s*==\s*sBox\.mFailed\)\s*\{[^{}]*"Cannot Play"[^{}]*CE_TONE_BAD', row):
        found.append("the row that failed does not carry the word \"Cannot Play\" in the failed colour")
    select = function_body(code, "bool\tPageSelect(int index)")
    if select is None or "CeJukeboxSelect(&sBox, &kDevice, index)" not in select:
        found.append("Select does not go through the state machine with the page's device")
    return found


class ListBoxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-extras-"))
        cls.binary = build(cls.tmp / "driver", GAME)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_geometry_is_the_load_game_lists(self):
        done = subprocess.run([str(self.binary)], capture_output=True, text=True, timeout=20)
        self.assertEqual(done.stdout.split(), ["geometry", f"{LEFT:.1f}", f"{TOP:.1f}", f"{PITCH:.1f}", f"{TRACK:.1f}", f"{RIGHT:.1f}", str(ROWS), f"{BOTTOM:.1f}"])

    def test_model_agrees_on_random_sequences(self):
        self.assertIsNone(comparison(self.binary))

    def test_the_ends_and_pages(self):
        ops = ["D", "D", "D", "D", "D", "U", "U", "u", "c42", "d", "c0", "u"]
        got = run_list(self.binary, 43, ops)
        # each step: the cursor and the first row in view; a page goes to the view's foot first, then a whole view further
        want = [("9", "0"), ("19", "10"), ("29", "20"), ("39", "30"), ("42", "33"), ("33", "33"), ("23", "23"), ("22", "22"),
                ("42", "33"), ("42", "33"), ("0", "0"), ("0", "0")]
        self.assertEqual([tuple(l.split()[3:5]) for l in got[1:]], want)
        self.assertEqual([l.split()[1] for l in got[1:]], ["1"] * 8 + ["0", "0", "0", "0"])      # moving the cursor by hand is not a move

    def test_empty_and_short_lists_do_not_scroll(self):
        for count in (0, 1, 9, 10):
            for line in run_list(self.binary, count, ["D", "w5", "g300", "d", "U"])[1:]:
                self.assertEqual(line.split()[4], "0", f"count {count}: {line}")
        got = run_list(self.binary, 5, ["D", "g300"])
        self.assertEqual(got[1].split()[3:7], ["4", "0", "135.000", "372.000"])
        # a long list's bar is never shorter than a row, and at the last rows it ends where the track ends
        got = run_list(self.binary, 230, ["c0", "c229"])
        self.assertEqual(got[1].split()[5:7], ["135.000", "159.000"])
        self.assertEqual(got[2].split()[5:7], ["348.000", "372.000"])
        self.assertEqual(run_list(self.binary, 0, ["D"])[1].split()[1], "0")

    def test_mutants_are_caught(self):
        source = (GAME / "ce_scroll_list.h").read_text()
        mutants = (
            ("list->mCursor = (list->mCursor > list->mTop) ? list->mTop : list->mCursor - CeListRowsOf(list);",
             "list->mCursor = list->mCursor - CeListRowsOf(list);", "a page up goes to the view's edge first"),
            ("list->mCursor = (list->mCursor < foot) ? foot : list->mCursor + CeListRowsOf(list);",
             "list->mCursor = list->mCursor + CeListRowsOf(list);", "a page down goes to the view's edge first"),
            ("if (list->mTop + CeListRowsOf(list) <= list->mCursor)", "if (list->mTop + CeListRowsOf(list) < list->mCursor)", "the cursor never leaves the view"),
            ("if (list->mTop > list->mCursor)\n\t\tlist->mTop = list->mCursor;", "", "the view follows the cursor up"),
            ("const int last = (list->mCount > CeListRowsOf(list)) ? list->mCount - CeListRowsOf(list) : 0;\n\tif (list->mTop > last)",
             "const int last = list->mCount;\n\tif (list->mTop > last)", "the view stops at the last page"),
            ("if (list->mCursor < top)\n\t\tlist->mCursor = top;", "", "the wheel keeps the cursor in view"),
            ("const int row = int((y - CeListTopOf(list)) / kCeListPitch);", "const int row = int((y - CeListTopOf(list)) / (kCeListPitch + 1.f));", "a row is 24 high"),
            ("(list->mTop + row >= list->mCount)", "(list->mTop + row > list->mCount)", "no row below the last"),
            ("return CeListBottom(list) - CeListTopOf(list) - 10.f;", "return CeListBottom(list) - CeListTopOf(list) - 4.f;", "the bar sits inside its track"),
            ("return (length < kCeListPitch) ? kCeListPitch : length;", "return length;", "the bar is never shorter than a row"),
            ("*top += (CeListTrackLength(list) - length) * float(list->mTop) / float(last);", "*top += CeListTrackLength(list) * float(list->mTop) / float(last + CeListRowsOf(list));",
             "the bar reaches the foot of its track with the last rows"),
            ("const float share = (y - length * 0.5f - CeListTopOf(list) - 5.f) / travel;", "const float share = (y - CeListTopOf(list) - 5.f) / travel;", "the bar's middle is under the pointer"),
            ("return CeListTopOf(list) + kCeListPitch * CeListRowsOf(list) + 7.f;", "return CeListTopOf(list) + kCeListPitch * CeListRowsOf(list) + 8.f;", "the box's foot"),
            ("const int top = int(share * float(list->mCount - CeListRowsOf(list)) + 0.5f);", "const int top = int(share * float(list->mCount - CeListRowsOf(list)));", "the drag rounds to the nearest row"),
        )
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            folder.mkdir()
            for f in GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / f.name)
            (folder / "ce_scroll_list.h").write_text(source.replace(old, new))
            binary = build(folder / "driver", folder, werror=False)          # a mutant that does not compile fails the test: it would prove nothing
            try:
                caught = comparison(binary) is not None
            except AssertionError:
                caught = True
            self.assertTrue(caught, f"a change to {what} went unnoticed")


class SourceRuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = {name: (GAME / name).read_text() for name in FILES}

    def test_the_sources_keep_the_rules(self):
        self.assertEqual(violations(self.sources), [])

    def test_each_rule_can_fail(self):
        broken = (
            ("ce_service_record.cpp", "void\tCareersClose()", "FILE *f = fopen(\"x\", \"wb\");\nvoid\tCareersClose()", "writes"),
            ("ce_career_view.cpp", "memcpy((void *)sScratch", "MEMORYCARD.WriteSave(0, asked, bytes, 1);\n\tmemcpy((void *)sScratch", "writes"),
            ("ce_career_view.cpp", "memcpy((void *)sScratch", "CAREER.Load(0);\n\tmemcpy((void *)sScratch", "live career"),
            ("ce_career_view.cpp", "CCareerNode *node = career->GetNode(n);", "career->GetLink(0)->GetToNode();\n\tCCareerNode *node = career->GetNode(n);", "live career"),
            ("ce_extras.cpp", "(CE_SettingGetBool(\"extras\", 1) == 1)", "(CE_SettingGetBool(\"extras\", 0) == 1)", "switch"),
            ("ce_extras.cpp", "(CE_SettingGetBool(\"extras\", 1) == 1)", "true", "opt-out"),
            ("ce_extras.cpp", "(PortCeMenus() && (CE_SettingGetBool", "((CE_SettingGetBool", "switch"),
            ("ce_extras.cpp", "\"Every mission, with your rank.\"", "\"Set in ce.ini.\"", "player text"),
            ("ce_service_record.cpp", "#ifdef CE_VERSION", "", "CE_VERSION"),
            ("ce_extras.cpp", '"Every mission, with your rank."', '"Every mission, with your rank"', "full stop"),
            ("ce_service_record.cpp", 'L"Missions"', 'L"missions"', "title case"),
            ("ce_extras.cpp", '"Every mission, with your rank."', '"Every mission_with your rank."', "font character"),
        )
        for name, old, new, kind in broken:
            text = self.sources.get(name) or (GAME / name).read_text()
            self.assertIn(old, text, f"{name}: the text this check breaks on purpose has moved; a rule that is never tried proves nothing")
            sources = dict(self.sources)
            sources[name] = text.replace(old, new, 1)
            self.assertTrue(violations(sources), f"{name}: {kind} went unnoticed")


class JukeboxWiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = (GAME / JUKEBOX).read_text()

    def test_the_page_keeps_the_wiring(self):
        self.assertEqual(wiring_violations(self.text), [])

    def test_each_wiring_rule_can_fail(self):
        broken = (
            ('if (!name || !TrackStarts(name))', 'if (!name)', "the track is not asked about before it is played"),
            ('if (!name || !TrackStarts(name))', 'if (!name || TrackStarts(name))', "the question is turned round"),
            ('\t\t\tfprintf(stderr, PORT_LOG_PREFIX "music player: row %d cannot be played\\n", row);\n\t\t\treturn false;', '\t\t\tfprintf(stderr, PORT_LOG_PREFIX "music player: row %d cannot be played\\n", row);', "an unplayable track is not refused"),
            ('Widen(help, n, "This track cannot be played.")', 'Widen(help, n, "Select to play.")', "the failed row is not said to have failed"),
            ('if (index == sBox.mFailed)\n\t\tWiden(help', 'if (index == sBox.mNow)\n\t\tWiden(help', "the hint is for the wrong row"),
            ('return (got > 0) && CeOggStarts(head, int(got), got < sizeof(head));', 'return got > 0;', "the file is not judged by its headers"),
            ('return CeJukeboxSelect(&sBox, &kDevice, index);', 'return false;', "Select goes through the state machine"),
            ('out->mTone = CE_TONE_BAD;', 'out->mTone = CE_TONE_GOOD;', "a failed track is not marked in the colour of a good state"),
            ('"Cannot Play"', '"Playing"', "a failed track is marked by its word"),
            ('else if (index == sBox.mFailed)\n\t{', 'else if (index == sBox.mNow)\n\t{', "the mark is on the row that failed"),
            ('FILE *file = fopen(path, "rb");\n\tif (!file)\n\t\treturn false;\n\tstatic unsigned char head[CE_OGG_START];', 'FILE *file = fopen(path, "wb");\n\tif (!file)\n\t\treturn false;\n\tstatic unsigned char head[CE_OGG_START];', "the file is opened for reading only"),
        )
        for old, new, what in broken:
            self.assertEqual(self.text.count(old), 1, f"{what}: the text this check breaks on purpose must occur once")
            self.assertTrue(wiring_violations(self.text.replace(old, new, 1)), f"{what} went unnoticed")


if __name__ == "__main__":
    unittest.main(verbosity=2)
