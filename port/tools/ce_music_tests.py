#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the retail music state machine (src/Music.cpp, src/PCMusic.cpp after every port patch) with the actual CE
playlist (port/game/ce_music.cpp), its folder listing (ce_music_scan.cpp) and ce.ini reader (ce.cpp), against a
recording music stream and scratch folders of authored Ogg headers; no game, device, audio or retail data.

It measures the report's open question on the retail code itself (a level's cue sets the play type back to
MPT_SELECTION) and checks the playlist: name order, play types, the game's rand() draws unchanged, track and path
bounds, the fences, and the faithful build. Causal mutants of the playlist and its patch must each be caught.
"""
import os
from pathlib import Path
import re
import resource
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "port/tools/tests"
MUSIC = ROOT / "port/game/ce_music.cpp"
PATCH = ROOT / "port/patches/0103-ce-music-playlist.patch"
FENCES = ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT",
          "BEA_NET_HOST", "BEA_NET_JOIN")
SANITIZE = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def sections(patch):
    return {m.group(1): m.group(0) for m in re.finditer(
        r"(?ms)^--- a/([^\n]+)\n\+\+\+ b/[^\n]+\n.*?(?=^--- a/|\Z)", patch)}


def patched(out, names, skip=()):
    """src/ copies of names with every port patch's section for them applied in the build's order, at fuzz 0."""
    for name in names:
        (out / name).write_bytes((ROOT / "src" / name).read_bytes())
    for patch in sorted((ROOT / "port/patches").glob("*.patch")):
        if patch.name in skip:
            continue
        text = (out.parent / "0103.patch").read_text(encoding="latin1") if patch == PATCH and (out.parent / "0103.patch").exists() \
            else patch.read_text(encoding="latin1")
        parts = sections(text)
        for name in names:
            if name in parts:
                (out / "section.patch").write_text(parts[name], encoding="latin1")
                r = subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", "section.patch"], cwd=out,
                                   capture_output=True, text=True, timeout=10, preexec_fn=no_core)
                if r.returncode or "fuzz" in r.stdout.lower():
                    raise AssertionError(f"{patch.name} {name}: {r.stdout}{r.stderr}")


def ogg(channels=2, rate=44100):
    """The first Ogg page of a Vorbis stream: its identification packet only, which is all the listing reads."""
    packet = b"\x01vorbis" + struct.pack("<IBIiii", 0, channels, rate, 0, 128000, 0) + b"\xb8\x01"
    return b"OggS\x00\x02" + bytes(8) + struct.pack("<III", 1, 0, 0) + bytes([1, len(packet)]) + packet


class MusicTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/ce-music-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)
        self.music = self.out / "My Music"
        self.music.mkdir()
        for name in ("a.ogg", "B.ogg", "c.OGG"):
            (self.music / name).write_bytes(ogg())
        (self.music / "mono.ogg").write_bytes(ogg(channels=1))
        (self.music / "rate48.ogg").write_bytes(ogg(rate=48000))
        (self.music / "junk.ogg").write_bytes(b"ID3\x04" + bytes(200))
        (self.music / "empty.ogg").write_bytes(b"")
        (self.music / "dir.ogg").mkdir()
        (self.music / ".hidden.ogg").write_bytes(ogg())
        (self.music / "notes.txt").write_bytes(b"not music")

    def tearDown(self):
        self.tmp.cleanup()

    def fixture(self, folder, music_text=None, patch_text=None):
        game, common = folder / "game", folder / "platform/common"
        game.mkdir(parents=True)
        common.mkdir(parents=True)
        (folder / "updater").mkdir()
        if patch_text is not None:
            (folder / "0103.patch").write_text(patch_text, encoding="latin1")
        patched(game, ["Music.cpp", "PCMusic.cpp"])
        (game / "music.h").write_bytes((ROOT / "src/music.h").read_bytes())
        for name in ("common.h", "console.h", "career.h", "Career.h", "debuglog.h", "DebugLog.h", "CLIParams.h",
                     "Platform.h", "mathsconst.h", "SoundManager.h"):
            (game / name).write_text('#include "ce_music_fake.h"\n')
        shutil.copy(TESTS / "ce_music_fake.h", game)
        shutil.copy(ROOT / "port/game/ce_music.h", game)
        (game / "ce_music.cpp").write_text(music_text if music_text is not None else MUSIC.read_text())
        for name in ("portlog.h", "ce.cpp", "ce_music_scan.cpp"):
            shutil.copy(ROOT / "port/platform/common" / name, common)
        shutil.copy(ROOT / "port/updater/ce_report.h", folder / "updater")

    def build(self, folder, ce=True, network=False):
        game, common = folder / "game", folder / "platform/common"
        cxx = os.environ.get("CXX", "g++")
        defines = ['-DCE_VERSION="test"'] if ce else []
        objects = []
        for source, flags in ((game / "Music.cpp", ["-w"]), (game / "PCMusic.cpp", ["-w"]), (game / "ce_music.cpp", ["-Wall", "-Wextra", "-Werror"]),
                              (common / "ce.cpp", ["-w"]), (common / "ce_music_scan.cpp", ["-Wall", "-Wextra", "-Werror"]),
                              (TESTS / "ce_music_driver.cpp", ["-Wall", "-Werror", *(["-DFAKE_NETWORK=1"] if network else [])])):
            obj = folder / (source.stem + ".o")
            include = ["-include", str(game / "ce_music_fake.h")] if source.parent == game or source.parent == TESTS else []
            r = subprocess.run([cxx, "-std=c++17", "-g", *SANITIZE, *defines, *flags, *include, "-I", str(game), "-c",
                                str(source), "-o", str(obj)], capture_output=True, text=True, timeout=120, preexec_fn=no_core)
            self.assertEqual(r.returncode, 0, f"{source.name}: {r.stderr}")
            objects.append(str(obj))
        exe = folder / ("driver-net" if network else "driver")
        r = subprocess.run([cxx, *SANITIZE, *objects, "-o", str(exe)], capture_output=True, text=True, timeout=120,
                           preexec_fn=no_core)
        self.assertEqual(r.returncode, 0, r.stderr)
        return exe

    def run_case(self, exe, mode, ini=None, extra=None, music=None):
        user = self.out / f"user-{mode}-{len(list(self.out.glob('user-*')))}"
        user.mkdir()
        if ini is None:
            ini = f"music_dir={music or self.music}\n"
        (user / "ce.ini").write_text(ini)
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "TEST_"))}
        env.update(BEA_USER_DIR=str(user), TEST_MUSIC_DIR=str(music or self.music), HOME=str(self.out),
                   ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
        env.update(extra or {})
        return subprocess.run([str(exe), mode], env=env, capture_output=True, text=True, timeout=30, preexec_fn=no_core)

    def passes(self, ran, text):
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn(text, ran.stdout)

    def test_retail_cue_resets_the_play_type(self):
        folder = self.out / "retail"
        self.fixture(folder)
        ran = self.run_case(self.build(folder), "measure", ini="")
        self.passes(ran, "measured")
        print("\n" + "\n".join(l for l in ran.stdout.splitlines() if l.startswith("MEASURE")))

    def test_playlist(self):
        folder = self.out / "ce"
        self.fixture(folder)
        exe = self.build(folder)
        self.passes(self.run_case(exe, "linear"), "linear playlist passed")
        self.passes(self.run_case(exe, "linear", ini=f"music_dir=  {self.music} \r\nmusic_playtype=linear\n"), "linear playlist passed")
        self.passes(self.run_case(exe, "random", ini=f"music_dir={self.music}\nmusic_playtype=Random\n"), "random playlist passed")
        self.passes(self.run_case(exe, "single", ini=f"music_dir={self.music}\nmusic_playtype=single\n"), "single playlist passed")
        # Linux's home folder shorthand.
        self.passes(self.run_case(exe, "linear", ini="music_dir=~/My Music\n"), "linear playlist passed")
        # No folder, a relative or missing folder, a duplicated key: the game's music.
        self.passes(self.run_case(exe, "empty", ini=""), "game music kept")
        self.passes(self.run_case(exe, "relative", ini="music_dir=My Music\n"), "game music kept")
        self.passes(self.run_case(exe, "empty", ini=f"music_dir={self.out}/missing\n"), "game music kept")
        self.passes(self.run_case(exe, "empty", ini=f"music_dir={self.music}\nmusic_dir={self.music}\n"), "game music kept")

    def test_bounds(self):
        folder = self.out / "bounds"
        self.fixture(folder)
        exe = self.build(folder)
        many = self.out / "many"
        many.mkdir()
        for i in range(300):
            (many / ("a.ogg" if i == 0 else f"t{i:03}.ogg")).write_bytes(ogg())
        more = self.out / "more"
        more.mkdir()
        (more / "zz.ogg").write_bytes(ogg())
        self.passes(self.run_case(exe, "bounds", music=many, extra={"TEST_MUSIC_DIR2": str(more)}), "bounds passed")
        # A path of exactly 255 bytes plays; 256 is left out.
        self.passes(self.run_case(exe, "long", music=self.deep("deep")), "path bound passed")

    def deep(self, name):
        deep = self.out / name
        deep.mkdir()
        fill = 255 - len(str(deep)) - 1 - len(".ogg")
        (deep / ("a" * fill + ".ogg")).write_bytes(ogg())
        (deep / ("a" * (fill + 1) + ".ogg")).write_bytes(ogg())
        return deep

    def test_fences_and_faithful(self):
        folder = self.out / "fences"
        self.fixture(folder)
        exe = self.build(folder)
        for flag in FENCES:
            with self.subTest(flag=flag):
                self.passes(self.run_case(exe, "fenced", extra={flag: "1"}), "game music kept")
        for flag in ("TEST_RECORDING", "TEST_ATTRACT", "TEST_DEMO"):
            self.passes(self.run_case(exe, "fenced", extra={flag: "1"}), "game music kept")
        self.passes(self.run_case(exe, "fenced", extra={"BEA_PORT_CE_LAYER": "0"}), "game music kept")
        # The CE layer forced on for screenshots does not open a replay's fence.
        self.passes(self.run_case(exe, "fenced", extra={"BEA_PORT_CE_LAYER": "1", "BEA_PORT_TAPE": "x"}), "game music kept")
        net = self.build(folder, network=True)
        self.passes(self.run_case(net, "fenced", extra={"TEST_NET_BOOT": "1"}), "game music kept")
        self.passes(self.run_case(net, "linear"), "linear playlist passed")
        faithful = self.out / "faithful"
        self.fixture(faithful)
        self.passes(self.run_case(self.build(faithful, ce=False), "faithful"), "faithful music unchanged")

    def test_mutants_are_caught(self):
        source = MUSIC.read_text()
        patch = PATCH.read_text()
        mutants = [
            # (name, file, old, new, the case that must fail)
            ("no-tape-fence", "music", '"BEA_PORT_TAPE", "BEA_PORT_PROBES"', '"BEA_UNUSED", "BEA_PORT_PROBES"', ("fenced", {"BEA_PORT_TAPE": "1", "BEA_PORT_CE_LAYER": "1"})),
            ("no-recording-fence", "music", "if (!PortCeLayer() || PortRecording()) return false;", "if (!PortCeLayer()) return false;",
             ("fenced", {"TEST_RECORDING": "1"})),
            ("no-attract-fence", "music", "CLIPARAMS.mStressTest || CLIPARAMS.mAttractMode", "CLIPARAMS.mStressTest", ("fenced", {"TEST_ATTRACT": "1"})),
            ("game-rand-shuffle", "music", "if (!shuffle) shuffle = CE_MusicSeed();", "shuffle = unsigned(rand()) * 2654435761u + 1;", ("random", None)),
            ("repeats", "music", "if (count > 1 && pick == at) pick", "if (false) pick", ("random", None)),
            ("end-to-game", "music", "\tif (type == CE_MUSIC_SINGLE) return true;\t// retail", "\treturn false;\t// retail", ("linear", None)),
            ("single-restarts", "music", "\tif (type == CE_MUSIC_SINGLE) return true;\t// retail", "\tif (false) return true;\t// retail", ("single", None)),
            ("front-end-replaced", "music", "levelCue = selection == MUS_TUTORIAL ||", "levelCue = selection == MUS_FRONTEND || selection == MUS_TUTORIAL ||",
             ("linear", None)),
            ("no-track-limit", "music", "\tif (count >= kTracks) return 0;\n", "", ("bounds", None)),
            ("unsorted", "music", "while (i < count && Compare(tracks[i], path) < 0) ++i;", "i = count;", ("linear", None)),
            ("no-dedupe", "music", "if (i < count && !Compare(tracks[i], path)) return 1;", "", ("linear", None)),
            ("clear-silent", "music", "if (custom && MUSIC.mPlaying && MUSIC.mCurrentSong) PlayMusicStream(MUSIC.mCurrentSong->mFilename);", "",
             ("linear", None)),
            ("no-header-check", "scan", "return p[11] == 2 && rate == 44100;", "return p[11] != 0xff && rate != 1;", ("linear", None)),
            ("path-unbounded", "scan", "(size_t)len > kTrackPath", "(size_t)len > kTrackPath + 1", ("long", None)),
            ("no-device-swap", "patch", "+\tfilename = PortCeMusicPlay(filename, mSelection);\n", "+\t(void)mSelection;\n", ("linear", None)),
            ("end-unhandled", "patch", "+\tif (GetMusicStreamFinished() && PortCeMusicTrackEnded())\n+\t\treturn FALSE;\n",
             "+\tif (GetMusicStreamFinished() && PortCeMusicTrackEnded())\n+\t\t;\n", ("single", None)),
        ]
        scan = (ROOT / "port/platform/common/ce_music_scan.cpp").read_text()
        caught = []
        for name, where, old, new, (case, extra) in mutants:
            with self.subTest(mutant=name):
                text = {"music": source, "scan": scan, "patch": patch}[where]
                self.assertEqual(text.count(old), 1, name)
                folder = self.out / name
                if where == "patch":
                    self.fixture(folder, patch_text=patch.replace(old, new))
                else:
                    self.fixture(folder)
                    if where == "music":
                        (folder / "game/ce_music.cpp").write_text(source.replace(old, new))
                    else:
                        (folder / "platform/common/ce_music_scan.cpp").write_text(scan.replace(old, new))
                exe = self.build(folder)
                if case == "long":
                    ran = self.run_case(exe, "long", music=self.deep(f"deep-{name}"))
                    self.assertNotEqual(ran.returncode, 0, f"mutant {name} survived")
                elif case == "bounds":
                    many = self.out / f"many-{name}"
                    many.mkdir()
                    for i in range(300):
                        (many / ("a.ogg" if i == 0 else f"t{i:03}.ogg")).write_bytes(ogg())
                    ran = self.run_case(exe, "bounds", music=many, extra={"TEST_MUSIC_DIR2": str(many)})
                    self.assertNotEqual(ran.returncode, 0, f"mutant {name} survived")
                else:
                    ini = {"random": f"music_dir={self.music}\nmusic_playtype=random\n",
                           "single": f"music_dir={self.music}\nmusic_playtype=single\n"}.get(case)
                    ran = self.run_case(exe, case, ini=ini, extra=extra)
                    self.assertNotEqual(ran.returncode, 0, f"mutant {name} survived: {ran.stdout}")
                caught.append(name)
        self.assertEqual(len(caught), len(mutants))


if __name__ == "__main__":
    unittest.main(verbosity=2)
