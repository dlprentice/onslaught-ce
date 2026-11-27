#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Music Player (port/game/ce_jukebox.cpp): its state machine, its length reader and the rules its source keeps.

Part one runs the state machine (ce_jukebox_state.h), as the game compiles it, against a fake device that logs every call, through
random sequences of selects, track ends, leaves and reopenings, and compares each step with a second model written here from the plan:
Select plays a track and Select on the playing track stops it; row 0 changes Order for this visit only; a track that ended is
followed by the next (wrapping), by any other one (Shuffle) or by silence (One Track); a visit that never started a track gives
nothing back and one that did gives the menu theme back once, however many ways out follow. Causal mutants must each be caught.

Part two runs the Ogg length reader on every retail soundtrack file in BEA_GAME_DIR (read only) and on tones made here, against ffprobe,
and on damaged input.

Part three reads the source: the page writes no file and no setting, opens its files for reading only, plays through the game's own
music calls and restores in the way the Credits page does.

No game, display, sound device or network is used.
"""
import random
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ce_extras_tests as base  # noqa: E402  (the shared source rules and the driver build)

ROOT = base.ROOT
GAME = base.GAME
FILES = ("ce_jukebox.cpp", "ce_jukebox_state.h", "ce_ogg_length.h", "ce_name_text.h")


class Model:
    """The Music Player as the plan describes it, from scratch."""

    def __init__(self, tracks):
        self.tracks, self.now, self.order, self.touched = tracks, -1, 0, False
        self.playing, self.allowed, self.rand = True, True, 0
        self.bad, self.failed = set(), -1

    def start(self, row, calls):
        if row in self.bad:
            calls.append(f"refuse {row}")
            self.failed = row
            return False
        calls.append(f"play {row}")
        self.playing, self.now, self.touched, self.failed = True, row, True, -1
        return True

    def following(self, ended, calls):
        if self.order == 2 or self.tracks < 2:
            return -1
        if self.order == 1:
            calls.append("random")
            others = [r for r in range(1, self.tracks + 1) if r != ended]
            return others[self.rand % len(others)]
        return 1 if ended >= self.tracks else ended + 1

    def op(self, op):
        calls, result = [], 0
        kind = op[0]
        if kind == "s":
            row = int(op[1:])
            if self.allowed and 0 <= row <= self.tracks:
                result = 1
                if row == 0:
                    self.order = (self.order + 1) % 3
                elif row == self.now:
                    calls.append("stop")
                    self.playing, self.now, self.failed = False, -1, -1
                else:
                    self.start(row, calls)
        elif kind in "ep":
            if kind == "e":
                self.playing = False
            if self.now > 0 and not self.playing:
                ended = self.now
                for _ in range(self.tracks + 1):          # a track that cannot be played is passed over, for one round of the list
                    nxt = self.following(ended, calls)
                    if nxt < 0:
                        break
                    if self.start(nxt, calls):
                        return self.line(op, result, calls)
                    ended = nxt
                self.now = -1
        elif kind == "x":
            self.now = -1
            if self.touched:
                self.touched = False
                calls.append("restore")
                self.playing = True
        elif kind == "o":
            self.now, self.order, self.touched, self.failed = -1, 0, False, -1
        elif kind == "f":
            self.bad.add(int(op[1:]))
        elif kind == "h":
            self.bad.clear()
        elif kind == "r":
            self.rand = int(op[1:])
        elif kind == "a":
            self.allowed = op[1] != "0"
        return self.line(op, result, calls)

    def line(self, op, result, calls):
        return (f"{op} {result} now {self.now} order {self.order} touched {int(self.touched)} failed {self.failed} calls"
                + "".join(f" [{c}]" for c in calls))


def run_driver(binary, tracks, ops):
    done = subprocess.run([str(binary), "jukebox", str(tracks), *ops], capture_output=True, text=True, timeout=20)
    if done.returncode:
        raise AssertionError(done.stderr or f"driver exit {done.returncode}")
    return done.stdout.splitlines()


def expected(tracks, ops):
    model = Model(tracks)
    return [model.op(op) for op in ops]


def random_ops(rng, tracks, n):
    ops = []
    for _ in range(n):
        kind = rng.choice("ssssseeppxxorfhaa")
        if kind == "f":
            ops.append(f"f{rng.randint(1, max(tracks, 1))}")
        elif kind == "s":
            ops.append(f"s{rng.randint(0, tracks + 1)}")
        elif kind == "r":
            ops.append(f"r{rng.randint(0, 50)}")
        elif kind == "a":
            ops.append(rng.choice(("a0", "a1", "a1")))
        else:
            ops.append(kind)
    return ops


def comparison(binary):
    rng = random.Random(20261112)
    for tracks in (0, 1, 2, 3, 10, 14):
        for _ in range(25):
            ops = random_ops(rng, tracks, 40)
            got, want = run_driver(binary, tracks, ops), expected(tracks, ops)
            if got != want:
                for i, (g, w) in enumerate(zip(got, want)):
                    if g != w:
                        return f"{tracks} tracks, after {ops[:i + 1]}: got {g!r}, wanted {w!r}"
                return f"{tracks} tracks: {len(got)} lines against {len(want)}"
    return None


class StateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-jukebox-"))
        cls.binary = base.build(cls.tmp / "driver", GAME)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_model_agrees_on_random_sequences(self):
        self.assertIsNone(comparison(self.binary))

    def test_the_plan_in_a_few_lines(self):
        def lines(tracks, ops):
            return [l.split(" calls")[1].strip() for l in run_driver(self.binary, tracks, ops)]
        # a visit that starts nothing gives nothing back
        self.assertEqual(lines(10, ["x", "x"]), ["", ""])
        # play, stop, play another, leave twice: the theme comes back once
        self.assertEqual(lines(10, ["s3", "s3", "s4", "x", "x"]), ["[play 3]", "[stop]", "[play 4]", "[restore]", ""])
        # in order wraps from the last track to the first
        self.assertEqual(lines(3, ["s2", "e", "e", "e"]), ["[play 2]", "[play 3]", "[play 1]", "[play 2]"])
        # one track plays once, then silence, and a later end does nothing
        self.assertEqual(lines(3, ["s0", "s0", "s2", "e", "e"]), ["", "", "[play 2]", "", ""])
        # shuffle never repeats the track that just ended, whatever the number drawn
        for draw in range(0, 40):
            got = lines(10, ["s0", "s5", f"r{draw}", "e"])
            row = int(re.search(r"play (\d+)", got[3]).group(1))
            self.assertNotEqual(row, 5, draw)
            self.assertTrue(1 <= row <= 10)
        # the order is for this visit only
        self.assertIn("order 0", run_driver(self.binary, 10, ["s0", "s0", "o"])[2])
        # a track that cannot be played is reported, leaves what played alone, and is passed over when a track ends
        got = run_driver(self.binary, 4, ["f2", "s3", "s2", "e", "e"])
        self.assertEqual([l.split(" calls")[1].strip() for l in got], ["", "[play 3]", "[refuse 2]", "[play 4]", "[play 1]"])
        self.assertIn("now 3 order 0 touched 1 failed 2", got[2])
        self.assertIn("now 4 order 0 touched 1 failed -1", got[3])
        got = run_driver(self.binary, 4, ["f1", "f3", "s4", "e"])
        self.assertEqual(got[3].split(" calls")[1].strip(), "[refuse 1] [play 2]")
        # when none can be played the music stays stopped, and leaving still gives the theme back
        got = run_driver(self.binary, 3, ["s1", "f1", "f2", "f3", "e", "e", "x"])
        self.assertIn("now -1", got[4])
        self.assertNotIn("[play", got[4])
        self.assertEqual(got[6].split(" calls")[1].strip(), "[restore]")
        # no music allowed: nothing is played and the select makes no sound
        self.assertEqual(run_driver(self.binary, 10, ["a0", "s3"])[1], "s3 0 now -1 order 0 touched 0 failed -1 calls")

    def test_mutants_are_caught(self):
        source = (GAME / "ce_jukebox_state.h").read_text()
        mutants = (
            ("if (!box->mTouched)\n\t\treturn;", "", "a visit that started nothing gives nothing back"),
            ("box->mTouched = false;\n\tdevice->mRestore();", "device->mRestore();", "the theme comes back once"),
            ("box->mNow = -1;\n\tif (!box->mTouched)", "if (!box->mTouched)", "leaving forgets what played"),
            ("if (row == box->mNow)", "if (false)", "Select on the playing track stops it"),
            ("return (box->mNow >= count - 1) ? 1 : box->mNow + 1;", "return box->mNow + 1;", "in order wraps"),
            ("if (pick >= box->mNow)\n\t\t\tpick++;", "", "shuffle never repeats the track"),
            ("(box->mOrder == CE_ORDER_ONE_TRACK) || (tracks < 2)", "(tracks < 2)", "one track plays once"),
            ("box->mOrder = (box->mOrder + 1) % CE_ORDER_COUNT;", "box->mOrder = (box->mOrder + 2) % CE_ORDER_COUNT;", "the order cycles"),
            ("box->mOrder = CE_ORDER_IN_ORDER;\n\tbox->mTouched = false;", "box->mTouched = false;", "the order is for one visit"),
            ("if (!device->mAllowed() || ", "if (", "nothing plays where music is not allowed"),
            ("if ((box->mNow <= 0) || device->mPlaying())", "if (device->mPlaying())", "a track that is not ours is not followed"),
            ("if ((box->mNow <= 0) || device->mPlaying())", "if (box->mNow <= 0)", "a track still playing is not followed"),
            ("box->mTouched = true;\n\treturn true;", "return true;", "starting a track marks the visit"),
            ("box->mFailed = row;\n\t\treturn false;", "return false;", "a track that cannot be played is reported"),
            ("box->mFailed = -1;\n\tbox->mNow = row;", "box->mNow = row;", "a track that plays is not reported as failed"),
            ("if (!device->mPlay(row))", "if (!device->mPlay(row) && false)", "a refusal is not taken as a start"),
            ("\t\tended.mNow = next;\n", "", "an unplayable track is passed over for the one after it"),
            ("if (CeJukeboxStart(box, device, next))\n\t\t\treturn;", "CeJukeboxStart(box, device, next);\n\t\treturn;", "a track that cannot follow does not stay current"),
        )
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            folder.mkdir()
            for f in GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / f.name)
            (folder / "ce_jukebox_state.h").write_text(source.replace(old, new))
            binary = base.build(folder / "driver", folder, werror=False)          # a mutant that does not compile fails the test: it would prove nothing
            self.assertIsNotNone(comparison(binary), f"a change to {what} went unnoticed")


class OggLengthTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-jukebox-ogg-"))
        cls.binary = base.build(cls.tmp / "driver", GAME)
        game = Path(__import__("os").environ.get("BEA_GAME_DIR", ""))
        cls.retail = sorted((game / "data/Music").glob("*.ogg")) if game.is_dir() else []

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def read(self, path):
        done = subprocess.run([str(self.binary), "ogg", str(path)], capture_output=True, text=True, timeout=20)
        self.assertEqual(done.returncode, 0, done.stderr)
        f = done.stdout.split()
        return int(f[1]), int(f[3]), int(f[5])

    @staticmethod
    def truth(path):
        done = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(path)],
                              capture_output=True, text=True, timeout=60)
        return float(done.stdout.strip())

    def tone(self, name, seconds, rate=44100, channels=2):
        path = self.tmp / name
        done = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", f"sine=frequency=440:duration={seconds}:sample_rate={rate}",
                               "-ac", str(channels), "-c:a", "libvorbis", str(path)], capture_output=True, text=True, timeout=60)
        if done.returncode:
            self.fail("ffmpeg cannot make an Ogg tone here (the suite needs ffmpeg with libvorbis; a check that cannot run is a failure, not a skip): " + done.stderr[:200])
        return path

    def test_retail_files_are_present(self):
        self.assertTrue(self.retail, "no soundtrack found: set BEA_GAME_DIR to a game folder with data/Music")
        self.assertEqual(len(self.retail), 10)

    def test_retail_lengths_agree_with_ffprobe(self):
        for path in self.retail:
            rate, samples, seconds = self.read(path)
            self.assertEqual(rate, 44100, path.name)
            self.assertAlmostEqual(seconds, self.truth(path), delta=1.0, msg=path.name)
            self.assertGreater(samples, 44100 * 30, path.name)

    def test_tones(self):
        for seconds in (1, 3.5, 20):
            rate, samples, got = self.read(self.tone(f"tone-{seconds}.ogg", seconds))
            self.assertEqual(rate, 44100)
            self.assertAlmostEqual(got, seconds, delta=1.0)
            self.assertAlmostEqual(samples / rate, seconds, delta=0.2)

    def crafted(self, samples):
        """A tone whose last page is given a position of this many samples (the reader does not check page sums)."""
        data = bytearray(self.tone("tone-crafted.ogg", 2).read_bytes())
        at = data.rfind(b"OggS")
        data[at + 6:at + 14] = samples.to_bytes(8, "little")
        path = self.tmp / f"crafted-{samples}.ogg"
        path.write_bytes(bytes(data))
        return path

    def test_a_position_past_32_bits(self):
        for samples in (0xFFFFFFFE, 0x100000000, 5_000_000_000):
            rate, got, seconds = self.read(self.crafted(samples))
            self.assertEqual((rate, got), (44100, samples))
            self.assertEqual(seconds, round(samples / 44100))

    def test_damaged_input_gives_nothing(self):
        data = self.tone("tone-damage.ogg", 3).read_bytes()
        cases = {"empty": b"", "not ogg": b"x" * 1000, "no header": data[200:], "cut short": data[:300], "wrong stream": b"OggS\0" + b"\0" * 400,
                 "other codec": data.replace(b"\x01vorbis", b"\x01xorbis", 1)}
        for name, content in cases.items():
            path = self.tmp / f"damage-{name.replace(' ', '-')}.ogg"
            path.write_bytes(content)
            self.assertEqual(self.read(path)[2], 0, name)
        # the last page cut off in the middle: the earlier whole page is used, or nothing, never more than the file holds
        cut = self.tmp / "cut-tail.ogg"
        cut.write_bytes(data[:-40])
        self.assertLessEqual(self.read(cut)[1], self.read(self.tmp / "tone-damage.ogg")[1])

    def test_mutants_are_caught(self):
        source = (GAME / "ce_ogg_length.h").read_text()
        tone = self.tone("tone-mutant.ogg", 6)
        self.test_damaged_input_gives_nothing()          # makes the damaged files the mutants are tried on
        mutants = (
            ("U32(packet + 12)", "U32(packet + 8)", "the sample rate's place"),
            ("U32(tail + at + 6), high = U32(tail + at + 10)", "U32(tail + at + 10), high = U32(tail + at + 6)", "the position's halves"),
            ("(static_cast<unsigned long long>(high) << 32) | low", "low", "the position's high half"),
            ("if (at + 27 + segments + body > size)\n\t\t\tcontinue;", "", "a cut page is not used"),
            ("whole + (rem >= (rate - rate / 2u) ? 1ull : 0ull)", "whole / 2ull", "the seconds"),
            ("packet[0] != 1 || memcmp(packet + 1, \"vorbis\", 6)", "false", "only a Vorbis header counts"),
        )
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            folder.mkdir()
            for f in GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / f.name)
            (folder / "ce_ogg_length.h").write_text(source.replace(old, new))
            binary = base.build(folder / "driver", folder, werror=False)          # a mutant that does not compile fails the test: it would prove nothing
            files = [tone, self.crafted(5_000_000_000)] + self.retail[:2] + [self.tmp / f"damage-{c}.ogg" for c in ("no-header", "cut-short", "other-codec")] + [self.tmp / "cut-tail.ogg"]
            honest = [self.read(p) for p in files if p.exists()]
            other = []
            for p in files:
                if p.exists():
                    done = subprocess.run([str(binary), "ogg", str(p)], capture_output=True, text=True, timeout=20)
                    other.append(tuple(int(x) for x in done.stdout.split()[1::2]) if done.returncode == 0 else None)
            self.assertNotEqual(honest, other, f"a change to {what} went unnoticed")


class NameTests(unittest.TestCase):
    """A file name's bytes as characters (ce_name_text.h): UTF-8 on Linux, the ANSI page on Windows."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-name-"))
        cls.binary = base.build(cls.tmp / "driver", GAME)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    @staticmethod
    def utf8_of(binary, text):
        """The characters (code points) the driver makes of text's UTF-8 bytes, for UTF-8 and for the ANSI page."""
        done = subprocess.run([str(binary), "name", text.encode("utf-8").hex()], capture_output=True, text=True, timeout=20)
        assert done.returncode == 0, done.stderr
        utf8, ansi = [[int(x, 16) for x in line.split()[1:]] for line in done.stdout.splitlines()]
        return utf8, ansi

    def raw(self, hexbytes):
        done = subprocess.run([str(self.binary), "name", hexbytes], capture_output=True, text=True, timeout=20)
        self.assertEqual(done.returncode, 0, done.stderr)
        return [int(x, 16) for x in done.stdout.splitlines()[0].split()[1:]]

    def test_characters_of_several_bytes_are_one_character(self):
        utf8, ansi = self.utf8_of(self.binary, "D\u00e9j\u00e0 vu")
        self.assertEqual(utf8, [ord(c) for c in "D\u00e9j\u00e0 vu"])
        self.assertEqual(len(ansi), len("D\u00e9j\u00e0 vu".encode()))          # the ANSI page takes each byte as a character
        self.assertEqual(self.utf8_of(self.binary, "caf\u00e9 \u20ac")[0], [ord(c) for c in "caf\u00e9 \u20ac"])
        self.assertEqual(self.utf8_of(self.binary, "plain name 12")[0], [ord(c) for c in "plain name 12"])

    def test_bytes_that_are_not_utf8_are_a_minus_sign_each(self):
        minus = ord("-")
        self.assertEqual(self.raw("6dff6e"), [ord("m"), minus, ord("n")])                   # a lone high byte
        self.assertEqual(self.raw("61c3"), [ord("a"), minus])                               # cut inside a character
        self.assertEqual(self.raw("c0af"), [minus, minus])                                  # overlong
        self.assertEqual(self.raw("eda080"), [minus] * 3)                                   # a surrogate
        self.assertEqual(self.raw("e080af"), [minus] * 3)                                   # overlong, three bytes
        self.assertEqual(self.raw("f0808080"), [minus] * 4)                                 # overlong, four bytes
        self.assertEqual(self.raw("c328"), [minus, ord("(")])                               # a lead byte, then no continuation
        self.assertEqual(self.raw("f09f8eb5"), [minus])                                     # above 65535: one character the font lacks
        self.assertEqual(self.raw("80"), [minus])                                           # a continuation byte alone

    def test_the_end_of_the_buffer_is_kept(self):
        done = subprocess.run([str(self.binary), "name", "61" * 100], capture_output=True, text=True, timeout=20)
        self.assertEqual(len(done.stdout.splitlines()[0].split()) - 1, 63)                  # 64 characters hold 63 and the end mark

    def test_mutants_are_caught(self):
        source = (GAME / "ce_name_text.h").read_text()
        mutants = (
            ("const bool shortest = (extra == 1) || ((extra == 2) && (code >= 0x800)) || ((extra == 3) && (code >= 0x10000));",
             "const bool shortest = true;", "an overlong form is not a character"),
            ("((code >= 0xD800) && (code <= 0xDFFF))", "false", "a surrogate is not a character"),
            ("(code > 0xFFFF) ? W('-') : W(code)", "W(code)", "above 65535 is shown as a minus sign"),
            ("if ((next & 0xC0) != 0x80)\n\t\t\t\t\tbreak;", "", "a continuation byte is checked"),
            ("else if (utf8 && (c >= 0x80))", "else if (false)", "a stray high byte is a minus sign"),
            ("extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;", "extra = (c >= 0xF0) ? 3 : 2;", "the length of a sequence"),
        )
        probes = ["c3a9", "e282ac", "6dff6e", "61c3", "c0af", "eda080", "c328", "f09f8eb5", "80", "e080af", "f0808080"]
        want = subprocess.run([str(self.binary), "name", *probes], capture_output=True, text=True, timeout=20).stdout
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            folder.mkdir()
            for f in GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / f.name)
            (folder / "ce_name_text.h").write_text(source.replace(old, new))
            binary = base.build(folder / "driver", folder, werror=False)          # a mutant that does not compile fails the test: it would prove nothing
            got = subprocess.run([str(binary), "name", *probes], capture_output=True, text=True, timeout=20).stdout
            self.assertNotEqual(got, want, f"a change to {what} went unnoticed")


class StartTests(unittest.TestCase):
    """Whether a track the scan let through can really start (CeOggStarts): the music worker gives no sign when it cannot."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-start-"))
        cls.binary = base.build(cls.tmp / "driver", GAME)
        path = cls.tmp / "tone.ogg"
        done = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=330:duration=20:sample_rate=44100",
                               "-ac", "2", "-c:a", "libvorbis", str(path)], capture_output=True, text=True, timeout=60)
        if done.returncode:
            raise unittest.SkipTest("ffmpeg cannot make an Ogg tone here: " + done.stderr[:200])
        cls.data = path.read_bytes()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def starts(self, data, name="t.ogg", binary=None):
        path = self.tmp / name
        path.write_bytes(data)
        done = subprocess.run([str(binary or self.binary), "start", str(path)], capture_output=True, text=True, timeout=20)
        self.assertEqual(done.returncode, 0, done.stderr)
        return done.stdout.strip() == "1"

    def pages(self):
        """The offsets of the file's pages and, for each, the packets that end on it."""
        out, at = [], 0
        while at + 27 <= len(self.data):
            segments = self.data[at + 26]
            table = self.data[at + 27:at + 27 + segments]
            out.append((at, sum(1 for v in table if v < 255), at + 27 + segments + sum(table)))
            at = out[-1][2]
        return out

    def test_a_whole_tone_starts(self):
        self.assertTrue(self.starts(self.data))

    def test_files_cut_inside_or_just_after_their_headers_do_not(self):
        pages, packets = self.pages(), 0
        for number, (at, ended, end) in enumerate(pages[:6]):
            before, packets = packets, packets + ended
            cut = self.starts(self.data[:end], f"cut-{number}.ogg")
            self.assertEqual(cut, packets >= 4, f"cut after page {number} ({packets} packets)")
            if at + 40 < end:
                self.assertEqual(self.starts(self.data[:at + 40], f"mid-{number}.ogg"), before >= 4, f"cut inside page {number}")
        self.assertGreaterEqual(len(pages), 5)

    def test_damage_is_refused(self):
        pages = self.pages()
        self.assertFalse(self.starts(b"", "empty.ogg"))
        self.assertFalse(self.starts(b"x" * 5000, "text.ogg"))
        broken = bytearray(self.data)
        broken[pages[1][0]:pages[1][0] + 4] = b"XXXX"         # the second page's capture pattern
        self.assertFalse(self.starts(bytes(broken), "page.ogg"))
        other = self.data.replace(b"\x01vorbis", b"\x01xorbis", 1)
        self.assertFalse(self.starts(other, "codec.ogg"))

    def test_a_long_file_is_judged_by_its_beginning(self):
        # the first bytes are all that is read: a file larger than the window starts when its headers and first audio are whole
        path = self.tmp / "long.ogg"
        done = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "anoisesrc=d=60:c=white:r=44100:a=0.3",
                               "-ac", "2", "-c:a", "libvorbis", "-q:a", "9", str(path)], capture_output=True, text=True, timeout=60)
        if done.returncode or path.stat().st_size < 140000:
            self.fail("ffmpeg made no file larger than the window here: the check of a long file cannot run")
        self.assertTrue(self.starts(path.read_bytes()))

    def test_mutants_are_caught(self):
        source = (GAME / "ce_ogg_length.h").read_text()
        mutants = (
            ("while (packets < 4)", "while (packets < 3)", "audio must follow the headers"),
            ("if (memcmp(page, \"OggS\", 4) || page[4] != 0)\n\t\t\treturn false;", "", "every page is checked"),
            ("return !complete;\t\t\t\t\t// the page runs past the bytes given", "return true;\t\t\t\t\t// the page runs past the bytes given", "a page cut short is not proof"),
            ("if (at + 27 > size)\n\t\t\treturn !complete;", "if (at + 27 > size)\n\t\t\treturn true;", "a file that ends is not proof"),
        )
        cases = {"whole": self.data, "damaged": bytes(self.data[:self.pages()[1][2] + 5]), "headers": self.data[:self.pages()[2][2]],
                 "mid": self.data[:self.pages()[1][0] + 60]}
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            folder.mkdir()
            for f in GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / f.name)
            (folder / "ce_ogg_length.h").write_text(source.replace(old, new))
            binary = base.build(folder / "driver", folder, werror=False)          # a mutant that does not compile fails the test: it would prove nothing
            broken = bytearray(self.data)
            broken[self.pages()[1][0]:self.pages()[1][0] + 4] = b"XXXX"
            cases["page"] = bytes(broken)
            honest = [self.starts(d, f"h{k}.ogg") for k, d in cases.items()]
            other = [self.starts(d, f"m{k}.ogg", binary) for k, d in cases.items()]
            self.assertNotEqual(honest, other, f"a change to {what} went unnoticed")


class SourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = {name: (GAME / name).read_text() for name in FILES}

    @staticmethod
    def problems(sources):
        found = []
        page = base.strip_comments(sources["ce_jukebox.cpp"])
        for m in re.finditer(r"fopen\s*\(([^;]*)\)", page):
            if '"rb"' not in m.group(1):
                found.append("a file opened other than for reading: " + m.group(0))
        for name, text in sources.items():
            code = base.strip_comments(text)
            for word in ("fwrite", "fputs", "remove", "unlink", "rename", "CE_SettingSet", "SetMusicVolume", "SetVolume", "CAREER"):
                if re.search(r"\b" + word, code):
                    found.append(f"{name}: {word}")
            if name.endswith(".cpp"):
                for s in base.string_literals(text):
                    if base.FORBIDDEN_WORDS.search(s) and not s.startswith("music player:"):
                        found.append(f"{name}: player text {s!r}")
        found += [f"ce_jukebox.cpp: {issue}" for issue in base.lint(sources["ce_jukebox.cpp"])]
        if not re.search(r"MPT_SELECTION", page) or "PlaySelection(MUS_FRONTEND, TRUE)" not in page:
            found.append("the menu theme is not given back as the Credits page gives it")
        if "PlayFromList(MUSIC.GetSong(row - 1), TRUE)" not in page:
            found.append("a soundtrack piece is not played with the game's playlist call and its fade")
        if "CeJukeboxClose(&sBox, &kDevice)" not in page or "CeJukeboxProcess(&sBox, &kDevice)" not in page:
            found.append("the page does not use the state machine")
        return found

    def test_the_source_keeps_the_rules(self):
        self.assertEqual(self.problems(self.sources), [])

    def test_each_rule_can_fail(self):
        broken = (
            ("ce_jukebox.cpp", 'fopen(name, "rb")', 'fopen(name, "wb")'),
            ("ce_jukebox.cpp", 'fopen(path, "rb")', 'fopen(path, "ab")'),
            ("ce_jukebox.cpp", "MUSIC.mPlayType = MPT_SELECTION;", ""),
            ("ce_jukebox.cpp", "PlaySelection(MUS_FRONTEND, TRUE)", "PlaySelection(MUS_FRONTEND, FALSE)"),
            ("ce_jukebox.cpp", "PlayFromList(MUSIC.GetSong(row - 1), TRUE)", "PlayFromList(MUSIC.GetSong(row - 1), FALSE)"),
            ("ce_jukebox.cpp", 'Widen(help, n, "What plays when a track ends. Select to change.")', 'Widen(help, n, "Set music_dir in ce.ini.")'),
            ("ce_jukebox.cpp", "CeJukeboxClose(&sBox, &kDevice)", "(void)0"),
            ("ce_jukebox.cpp", "MUSIC.Stop();", "MUSIC.SetVolume(0.f);"),
            ("ce_jukebox.cpp", '"Shuffle"', '"shuffle"'),
            ("ce_jukebox.cpp", '"Music volume is in Sound Options."', '"Music volume is in Sound_Options."'),
        )
        for name, old, new in broken:
            self.assertIn(old, self.sources[name], old)
            sources = dict(self.sources)
            sources[name] = sources[name].replace(old, new, 1)
            self.assertTrue(self.problems(sources), f"{old!r} -> {new!r} went unnoticed")


if __name__ == "__main__":
    unittest.main(verbosity=2)
