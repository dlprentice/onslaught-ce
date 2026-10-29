#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The port's mod layer (port/platform/common/mods.cpp, with ce.cpp's ce.ini reader) on the host, through
port/tools/mods_driver.cpp, and port/tools/mods.py's packing. Generated fixtures only: no game, retail file, device or
network. Then mutants: each removes one guard from a copy of mods.cpp and must make a named test fail.

    python3 port/tools/mods_tests.py            # the tests, then the mutants
    python3 port/tools/mods_tests.py --no-mutants
"""
import hashlib
import json
import os
import errno
import resource
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
MODS_CPP = PORT / "platform/common/mods.cpp"
sys.path.insert(0, str(PORT / "tools"))
import mods  # noqa: E402

FENCES = ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT",
          "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN", "BEA_NET_HANDOFF")
TEX = "data/resources/dxtntextures/meshtex%Test.tga(0)A1R5G5B5.aya"
SND = "data/sounds/test.wav"
BUILD = ROOT / "local-data/port/mods-tests"
DRIVER = {}		# (source text, ce version) -> driver path


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


PLATFORM_OFF = ("PORT_MODS_PLATFORM_OFF",)		# builds the branch a Windows build takes (mods refused), on the host


def driver(source=None, ce_version=None, defines=()):
    """Builds the driver from mods.cpp (or a mutant's text) once per (text, version, defines)."""
    text = source if source is not None else MODS_CPP.read_text()
    key = (hashlib.sha256(text.encode()).hexdigest()[:16], ce_version, tuple(defines))
    if key in DRIVER:
        return DRIVER[key]
    out = BUILD / (f"driver-{key[0]}-{ce_version or 'dev'}" + "".join("-" + d for d in defines))
    # mods.cpp includes "../../updater/sha256.h": the copy sits at the same depth (a/b) as in port/platform/common.
    (out / "a/b").mkdir(parents=True, exist_ok=True)
    (out / "updater").mkdir(exist_ok=True)
    (out / "a/b/mods.cpp").write_text(text)
    for name in ("mods.h", "portlog.h"):
        shutil.copy(PORT / "platform/common" / name, out / "a/b" / name)
    shutil.copy(PORT / "updater/sha256.h", out / "updater/sha256.h")
    cxx, cc = os.environ.get("CXX", "g++"), os.environ.get("CC", "gcc")
    define = ([f'-DCE_VERSION="{ce_version}"'] if ce_version else []) + [f"-D{d}" for d in defines]
    steps = [
        [cc, "-O2", "-c", str(PORT / "updater/sha256.c"), "-o", str(out / "sha256.o")],
        # ce.cpp's ce.ini reader does not depend on the version (with CE_VERSION it would also need SDL).
        [cxx, "-std=gnu++17", "-O1", "-w", "-c", "-I", str(PORT / "platform/common"),
         str(PORT / "platform/common/ce.cpp"), "-o", str(out / "ce.o")],
        [cxx, "-std=gnu++17", "-O1", "-Wall", "-Wextra", "-Werror", *define, "-I", str(out / "a/b"),
         "-I", str(PORT / "updater"), str(PORT / "tools/mods_driver.cpp"), str(out / "a/b/mods.cpp"),
         str(out / "ce.o"), str(out / "sha256.o"), "-lz", "-o", str(out / "driver")],
    ]
    for step in steps:
        r = subprocess.run(step, capture_output=True, text=True, timeout=120, preexec_fn=no_core)
        if r.returncode:
            raise RuntimeError(r.stderr)
    DRIVER[key] = out / "driver"
    return DRIVER[key]


def dds(width=8, height=8):
    """A small DXT3 image: header + one level."""
    head = bytearray(128)
    head[:4] = b"DDS "
    struct.pack_into("<IIII", head, 4, 124, 0x1007, height, width)
    struct.pack_into("<I", head, 76, 32)
    struct.pack_into("<I", head, 80, 4)
    head[84:88] = b"DXT3"
    return bytes(head) + bytes(range(256)) * ((width // 4) * (height // 4) * 16 // 256 + 1)


class Fixture:
    def __init__(self, tmp):
        self.tmp = Path(tmp)
        self.game = self.tmp / "game"
        self.user = self.tmp / "user"
        self.outside = self.tmp / "outside"
        for d in (self.game, self.user, self.outside):
            d.mkdir()
        for rel in (TEX, SND, "data/other.bin"):
            (self.game / rel).parent.mkdir(parents=True, exist_ok=True)
            (self.game / rel).write_bytes(b"original " + rel.encode())

    def mod(self, mod_id, files, manifest=None, raw=None, extra=None):
        """A mod with files {path: bytes}; manifest overrides fields; raw replaces mod.json's text."""
        d = self.user / "mods" / mod_id
        for rel, data in files.items():
            (d / rel).parent.mkdir(parents=True, exist_ok=True)
            (d / rel).write_bytes(data)
        m = {"schema": 1, "id": mod_id, "name": f"Mod {mod_id}", "version": "1.0", "author": "tests",
             "files": [{"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                       for rel, data in files.items()]}
        m.update(manifest or {})
        (d / "mod.json").write_text(raw if raw is not None else json.dumps(m))
        return d

    def enable(self, *ids, raw=None):
        (self.user / "ce.ini").write_text("record_sessions=0\n" + (raw if raw is not None else "mods=" + ",".join(ids)) + "\n")

    def run(self, commands, env=None, exe=None, between=None):
        e = {k: v for k, v in os.environ.items() if k not in FENCES and k != "BEA_PORT_CE_LAYER"}
        e.update(BEA_USER_DIR=str(self.user), BEA_GAME_DIR=str(self.game))
        e.update(env or {})
        p = subprocess.Popen([str(exe or driver())], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, env=e, preexec_fn=no_core)
        out = []
        for c in commands:
            if c == "!between":
                between()
                continue
            p.stdin.write(c + "\n")
            p.stdin.flush()
            out.append(p.stdout.readline().strip())
        p.stdin.close()
        err = p.stderr.read()
        p.wait(timeout=60)
        p.stdout.close()
        p.stderr.close()
        return out, err


def digest(data):
    return f"OPEN {hashlib.sha256(data).hexdigest()} {len(data)}"


class ModsTests(unittest.TestCase):
    exe = None			# a mutant's driver, when set
    ce_exe = None

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="mods-", dir=BUILD)
        self.f = Fixture(self._tmp.name)
        self.tex = mods.pack(dds())

    def tearDown(self):
        self._tmp.cleanup()

    def run_driver(self, commands, **kw):
        kw.setdefault("exe", self.exe)
        return self.f.run(commands, **kw)

    def assertRefused(self, err, words):
        self.assertIn("refused", err)
        self.assertIn(words, err)

    # ---- a working mod -----------------------------------------------------------------------------------------------
    def test_enabled_mod_replaces_files(self):
        self.f.mod("good", {TEX: self.tex, SND: b"new sound"})
        self.f.enable("good")
        out, err = self.run_driver(["active", "open " + TEX.replace("/", "\\").upper(), "open " + SND,
                                    "open data/other.bin", "open DATA\\sounds\\..\\sounds\\.\\test.wav"])
        self.assertEqual(out, ["ACTIVE 1 -", digest(self.tex), digest(b"new sound"), "NONE", digest(b"new sound")])
        self.assertIn("1 active, 2 game files replaced", err)
        self.assertIn("good replaces " + TEX, err)
        self.assertNotIn(str(self.f.user), err, "the log must not name the user folder")

    def test_absolute_and_drive_reads_are_never_replaced(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        out, _ = self.run_driver(["open /" + SND, "open C:\\" + SND.replace("/", "\\"), "open \\" + SND])
        self.assertEqual(out, ["NONE"] * 3)

    def test_no_mods_line_is_silent(self):
        self.f.mod("good", {SND: b"new"})
        out, err = self.run_driver(["active", "open " + SND])
        self.assertEqual(out, ["ACTIVE 0 -", "NONE"])
        self.assertEqual(err, "")

    def test_ambiguous_or_incomplete_config_never_enables_mods(self):
        self.f.mod("good", {SND: b"new"})
        settings = self.f.user / "ce.ini"
        for raw in (b"mods=good\nmods=other\n", b"mods=good\x00\n", b"mods=good\n" + b"x" * (1 << 20),
                    b"mods=" + b"x" * 2048 + b"\n"):
            with self.subTest(length=len(raw), prefix=raw[:16]):
                settings.write_bytes(raw)
                out, _ = self.run_driver(["active", "open " + SND])
                self.assertEqual(out, ["ACTIVE 0 -", "NONE"])
        settings.write_bytes(b"unknown=\xff\r\nmods=good\r\n")
        out, _ = self.run_driver(["active", "open " + SND])
        self.assertEqual(out, ["ACTIVE 1 -", digest(b"new")])

    def test_disabled_mod_is_not_loaded(self):
        self.f.mod("off", {SND: b"new"})
        self.f.mod("on", {"data/other.bin": b"x"})
        self.f.enable("on")
        out, err = self.run_driver(["active", "open " + SND, "open data/other.bin"])
        self.assertEqual(out, ["ACTIVE 1 -", "NONE", digest(b"x")])
        self.assertNotIn("off", err.replace("off for", ""))

    def test_later_mod_wins_and_conflict_is_logged(self):
        self.f.mod("first", {SND: b"one", "data/other.bin": b"keep"})
        self.f.mod("second", {SND: b"two"})
        self.f.enable("first", "second")
        out, err = self.run_driver(["open " + SND, "open data/other.bin"])
        self.assertEqual(out, [digest(b"two"), digest(b"keep")])
        self.assertIn("second replaces data/sounds/test.wav over first", err)

    def test_duplicate_and_bad_ids_in_ce_ini(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable(raw="mods= good , good,../x,Good,a/b")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 1 -"])
        self.assertIn("listed twice", err)
        self.assertEqual(err.count("a mod id is"), 3)

    # ---- the fences --------------------------------------------------------------------------------------------------
    def test_every_fence_keeps_mods_off(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        for name in FENCES:
            with self.subTest(name):
                out, err = self.run_driver(["active", "open " + SND], env={name: "1"})
                self.assertEqual(out, [f"ACTIVE 0 {name}", "NONE"])
                self.assertIn(f"off for this run ({name} is set): 1 enabled in ce.ini not loaded", err)

    def test_ce_layer_force_does_not_lift_a_fence(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        out, _ = self.run_driver(["active", "open " + SND], env={"BEA_PORT_TAPE": "t", "BEA_PORT_CE_LAYER": "1"})
        self.assertEqual(out, ["ACTIVE 0 BEA_PORT_TAPE", "NONE"])

    def test_empty_fence_variable_is_not_a_fence(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        out, _ = self.run_driver(["active"], env={"BEA_PORT_TAPE": ""})
        self.assertEqual(out, ["ACTIVE 1 -"])

    def test_platform_off_build_refuses_listed_mods_and_touches_nothing(self):
        # The Windows build takes this branch (mods.cpp, Load): a listed mod is not activated, one line says why, and no
        # file below the mods folder is opened (the mod folder is made unreadable: a loader that went in would log a refusal).
        exe = self.exe or driver(defines=PLATFORM_OFF)
        self.f.mod("good", {SND: b"new"})
        folder = self.f.user / "mods" / "good"
        self.f.enable("good")
        folder.chmod(0)
        try:
            out, err = self.f.run(["active", "open " + SND, "open data/other.bin"], exe=exe)
            self.assertEqual(out, ["ACTIVE 0 -", "NONE", "NONE"], err)
            lines = [line for line in err.splitlines() if "mods:" in line]
            self.assertEqual(len(lines), 1, err)
            self.assertIn("off on Windows until mod path safety is validated", lines[0])
            self.assertNotIn("refused", err)
            # A fence is checked first, so a fenced run gives the fence's reason, not the platform's.
            out, err = self.f.run(["active"], exe=exe, env={"BEA_PORT_CLOCK": "1"})
            self.assertEqual(out, ["ACTIVE 0 BEA_PORT_CLOCK"], err)
            self.assertIn("off for this run (BEA_PORT_CLOCK is set): 1 enabled in ce.ini not loaded", err)
            self.assertNotIn("off on Windows", err)
            # No mods line: silent on every platform.
            (self.f.user / "ce.ini").write_text("record_sessions=0\n")
            out, err = self.f.run(["active"], exe=exe)
            self.assertEqual(out, ["ACTIVE 0 -"], err)
            self.assertNotIn("mods:", err)
        finally:
            folder.chmod(0o755)

    # ---- paths -------------------------------------------------------------------------------------------------------
    def test_path_escapes_are_refused(self):
        bad = {"data/../../outside/x": "'.' or '..'", "../data/x": "'.' or '..'", "/data/x": "absolute",
               "data//x": "empty path component", "data\\sounds\\test.wav": "not allowed",
               "C:/data/x": "not allowed", "data/x:stream": "not allowed", "data/con.wav": "device name",
               "data/COM1": "device name", "data/aux .txt.wav": "device name", "data/x.": "ends with a space or a dot",
               "data/x ": "ends with a space", "sounds/test.wav": "outside data/", "data/./x": "'.' or '..'",
               "data/" + "a/" * 12 + "x": "too many", "data/" + "x" * 129: "over-long", "data/\u00e9": "not allowed"}
        for path, words in bad.items():
            with self.subTest(path):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                d = self.f.user / "mods/bad"
                d.mkdir(parents=True)
                m = {"schema": 1, "id": "bad", "name": "b", "version": "1",
                     "files": [{"path": path, "size": 1, "sha256": "0" * 64}]}
                (d / "mod.json").write_text(json.dumps(m))
                self.f.enable("bad")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"])
                self.assertRefused(err, words)
                self.assertNotIn(str(self.f.tmp), err)

    def test_file_the_game_lacks_is_refused(self):
        self.f.mod("adds", {"data/new.bin": b"x"})
        self.f.enable("adds")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "the game has no such file")

    def test_world_files_can_be_added_and_no_other_missing_file_can(self):
        # CWorld::Load reads data/worlds/worldNNN.wrd in place of the level archive's copy: the one name a mod may add.
        self.f.mod("w", {"data/worlds/world100.wrd": b"world body", "data/worlds/WORLD001.WRD": b"base body"})
        self.f.enable("w")
        out, err = self.run_driver(["active", "open data\\worlds\\world100.wrd", "open data/worlds/world001.wrd",
                                    "has data/worlds/world100.wrd", "has DATA\\Worlds\\world001.wrd",
                                    "has data/worlds/world002.wrd", "has data/other.bin"])
        self.assertEqual(out, ["ACTIVE 1 -", digest(b"world body"), digest(b"base body"), "HAS 1", "HAS 1", "HAS 0", "HAS 0"], err)
        self.assertIn("w adds data/worlds/world100.wrd", err)
        self.assertNotIn("w replaces data/worlds", err)

    def test_only_the_three_digit_world_name_may_be_added(self):
        for bad in ("data/worlds/world10.wrd", "data/worlds/world1000.wrd", "data/worlds/world100.wrd.bak",
                    "data/worlds/sub/world100.wrd", "data/worlds/world10a.wrd", "data/world100.wrd",
                    "data/worlds/world100.wr", "data/worlds/xworld100.wrd", "data/resources/world100.wrd"):
            with self.subTest(bad):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("w", {bad: b"x"})
                self.f.enable("w")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"], err)
                self.assertRefused(err, "the game has no such file")

    def test_world_file_size_is_bounded(self):
        for size, active in ((4 << 20, 1), ((4 << 20) + 1, 0)):
            with self.subTest(size=size):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("w", {"data/worlds/world100.wrd": b"w" * size})
                self.f.enable("w")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, [f"ACTIVE {active} -"], err)
                if not active:
                    self.assertRefused(err, "a world file larger than 4 MiB")

    def test_has_answers_by_name_and_never_when_mods_are_off(self):
        self.f.mod("good", {SND: b"new"})
        self.f.enable("good")
        out, _ = self.run_driver(["has " + SND, "has data/other.bin", "has /" + SND, "has C:\\" + SND.replace("/", "\\")])
        self.assertEqual(out, ["HAS 1", "HAS 0", "HAS 0", "HAS 0"])
        out, _ = self.run_driver(["has " + SND], env={"BEA_PORT_TAPE": "t"})
        self.assertEqual(out, ["HAS 0"])

    def test_the_first_read_of_each_replaced_file_is_logged_once(self):
        self.f.mod("good", {SND: b"new", "data/other.bin": b"other"})
        self.f.enable("good")
        out, err = self.run_driver(["open " + SND, "open " + SND, "open data/other.bin", "open " + SND])
        self.assertEqual(err.count("good serves " + SND + " to the game"), 1, err)
        self.assertEqual(err.count("good serves data/other.bin to the game"), 1, err)
        out, err = self.run_driver(["active"])
        self.assertNotIn("serves", err, "nothing was read, so nothing was served")

    def test_symlinked_file_is_refused(self):
        secret = self.f.outside / "secret"
        secret.write_bytes(b"secret")
        d = self.f.mod("link", {SND: b"secret"})
        (d / SND).unlink()
        (d / SND).symlink_to(secret)
        self.f.enable("link")
        out, err = self.run_driver(["active", "open " + SND])
        self.assertEqual(out, ["ACTIVE 0 -", "NONE"])
        self.assertRefused(err, "a link")

    def test_symlinked_folder_on_the_path_is_refused(self):
        (self.f.outside / "sounds").mkdir()
        (self.f.outside / "sounds/test.wav").write_bytes(b"secret")
        d = self.f.mod("dirlink", {SND: b"secret"})
        shutil.rmtree(d / "data/sounds")
        (d / "data/sounds").symlink_to(self.f.outside / "sounds")
        self.f.enable("dirlink")
        out, err = self.run_driver(["active", "open " + SND])
        self.assertEqual(out, ["ACTIVE 0 -", "NONE"])
        self.assertRefused(err, "link or not a folder")

    def test_symlinked_mod_folder_and_mods_folder_are_refused(self):
        real = self.f.outside / "realmod"
        self.f.mod("tmp", {SND: b"new"})
        shutil.move(str(self.f.user / "mods/tmp"), real)
        m = json.loads((real / "mod.json").read_text())
        m["id"] = "linked"
        (real / "mod.json").write_text(json.dumps(m))
        (self.f.user / "mods/linked").symlink_to(real)
        self.f.enable("linked")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "no such folder")
        # the mods folder itself
        shutil.rmtree(self.f.user / "mods")
        (self.f.outside / "mods").mkdir()
        shutil.move(str(real), self.f.outside / "mods/linked")
        (self.f.user / "mods").symlink_to(self.f.outside / "mods")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "no such folder")

    def test_hard_link_fifo_and_folder_are_refused(self):
        other = self.f.outside / "other"
        other.write_bytes(b"hard")
        d = self.f.mod("hard", {SND: b"hard"})
        (d / SND).unlink()
        os.link(other, d / SND)
        self.f.enable("hard")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "single name")
        (d / SND).unlink()
        os.mkfifo(d / SND)
        out, err = self.run_driver(["active"])		# must not block on the FIFO
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "not a regular file")

    def test_symlinked_manifest_is_refused(self):
        d = self.f.mod("mlink", {SND: b"new"})
        (self.f.outside / "mod.json").write_text((d / "mod.json").read_text())
        (d / "mod.json").unlink()
        (d / "mod.json").symlink_to(self.f.outside / "mod.json")
        self.f.enable("mlink")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "mod.json is missing, a link")

    # ---- mod.json ----------------------------------------------------------------------------------------------------
    def test_bad_manifests_are_refused(self):
        good = {"schema": 1, "id": "m", "name": "n", "version": "1",
                "files": [{"path": SND, "size": 3, "sha256": hashlib.sha256(b"new").hexdigest()}]}
        cases = {
            "trailing comma": (json.dumps(good)[:-1] + ",}", "a string was expected"),
            "missing comma": (json.dumps(good).replace(', "name"', ' "name"'), "a closing bracket"),
            "repeated key": ('{"schema":1,"schema":1}', "a repeated key"),
            "unknown key": (json.dumps({**good, "script": "x"}), "an unknown key"),
            "unknown file key": (json.dumps({**good, "files": [{**good["files"][0], "exec": 1}]}), "unknown key in a files"),
            "schema 2": (json.dumps({**good, "schema": 2}), "schema is not 1"),
            "id mismatch": (json.dumps({**good, "id": "other"}), "id is not the mod folder's name"),
            "float size": (json.dumps(good).replace('"size": 3', '"size": 3.0'), "not an integer"),
            "negative size": (json.dumps(good).replace('"size": 3', '"size": -3'), "lacks path"),
            "string size": (json.dumps(good).replace('"size": 3', '"size": "3"'), "lacks path"),
            "no files": (json.dumps({**good, "files": []}), "files is missing"),
            "no name": (json.dumps({k: v for k, v in good.items() if k != "name"}), "name is missing"),
            "long name": (json.dumps({**good, "name": "x" * 65}), "name is missing"),
            "upper hash": (json.dumps(good).replace(good["files"][0]["sha256"], good["files"][0]["sha256"].upper()),
                           "64 lowercase"),
            "huge size": (json.dumps(good).replace('"size": 3', f'"size": {64 * 2**20 + 1}'), "larger than 64 MiB"),
            "not an object": ("[1,2]", "not an object"),
            "text after": (json.dumps(good) + " x", "text after"),
            "deep": ('{"files":' + "[" * 9 + "]" * 9 + "}", "nesting deeper"),
            "control char": (json.dumps(good).replace('"n"', '"a\u0001b"'), "control character"),
            "nul escape": (json.dumps(good).replace('"n"', '"a\\u0000b"'), "NUL"),
            "bad version": (json.dumps({**good, "ce_min": "one"}), "ce_min or ce_max"),
            "empty": ("", "a value was expected"),
            "listed twice": (json.dumps({**good, "files": good["files"] * 2}), "listed twice"),
        }
        for i in range(4):
            bad = list("0000")
            bad[i] = "z"
            for field in ("name", "path"):
                text = json.dumps(good)
                old = '"n"' if field == "name" else json.dumps(SND)
                text = text.replace(old, '"\\u' + ''.join(bad) + '"', 1)
                cases[f"bad unicode {field} {i}"] = (text, "a bad \\u escape")
        cases["escaped duplicate key"] = ('{"schema":1,"sch\\u0065ma":1}', "a repeated key")
        for name, (text, words) in cases.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("m", {SND: b"new"}, raw=text)
                self.f.enable("m")
                out, err = self.run_driver(["active", "open " + SND])
                self.assertEqual(out, ["ACTIVE 0 -", "NONE"], err)
                self.assertRefused(err, words)

    def test_oversized_manifest_is_refused(self):
        self.f.mod("big", {SND: b"new"}, manifest={"description": "x" * 1000})
        d = self.f.user / "mods/big"
        text = (d / "mod.json").read_text()
        (d / "mod.json").write_text(text[:-1] + " " * (256 * 1024) + "}")
        self.f.enable("big")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "larger than 256 KiB")

    def test_hash_and_size_mismatches_are_refused(self):
        d = self.f.mod("hash", {SND: b"new"})
        (d / SND).write_bytes(b"neW")
        self.f.enable("hash")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "SHA-256 is not the manifest's")
        (d / SND).write_bytes(b"newer")
        out, err = self.run_driver(["active"])
        self.assertRefused(err, "size is not the manifest's")

    def test_one_bad_file_refuses_the_whole_mod(self):
        d = self.f.mod("half", {SND: b"new", "data/other.bin": b"ok"})
        (d / SND).write_bytes(b"bad")
        self.f.enable("half")
        out, err = self.run_driver(["active", "open data/other.bin"])
        self.assertEqual(out, ["ACTIVE 0 -", "NONE"])

    def test_names_in_the_log_are_ascii(self):
        self.f.mod("names", {SND: b"new"}, manifest={"name": "Caf\u00e9 \u001b[31m", "author": "x\ny"})
        self.f.enable("names")
        out, err = self.run_driver(["active"])
        # a raw control character is refused; the escaped ones are cleaned
        self.assertEqual(out, ["ACTIVE 1 -"])
        self.assertNotIn("\x1b", err)
        line = [l for l in err.splitlines() if "active 1:" in l][0]
        self.assertTrue(all(32 <= ord(c) < 127 for c in line), line)

    # ---- packed files ------------------------------------------------------------------------------------------------
    def block(self, data):
        z = zlib.compress(data)
        return struct.pack("<I", len(z)) + z

    def test_packed_structure_is_checked(self):
        big = bytes(1 << 20)
        cases = {
            "short block first": (self.block(b"x" * 100) + self.block(big), "under 1 MiB before the last"),
            "block over 1 MiB": (self.block(bytes((1 << 20) + 1)), "more than 1 MiB"),
            "trailing bytes": (mods.pack(b"x" * 10) + b"\0\0", "cut block header"),
            "cut block": (mods.pack(b"x" * 10)[:-1], "cut block"),
            "not zlib": (struct.pack("<I", 4) + b"abcd", "does not unpack"),
            "trailing inside block": (struct.pack("<I", len(zlib.compress(b"ok")) + 4) +
                                      zlib.compress(b"ok") + b"junk", "does not unpack"),
            "zero length": (struct.pack("<I", 0), "larger than the game's block buffer"),
            "huge length": (struct.pack("<I", 0x102928) + bytes(0x102928), "larger than the game's block buffer"),
            "empty": (b"", "an empty packed file"),
        }
        for name, (data, words) in cases.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("aya", {"data/x.aya": data})
                (self.f.game / "data/x.aya").write_bytes(b"orig")
                self.f.enable("aya")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"], err)
                self.assertRefused(err, words)
        shutil.rmtree(self.f.user / "mods")
        self.f.mod("aya", {"data/x.aya": self.block(big) + self.block(big) + self.block(b"tail")})
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 1 -"], err)

    def test_textures_must_be_dds_within_the_game_limit(self):
        cases = {"not DDS": (mods.pack(b"TGA!" + bytes(200)), "not a DDS image"),
                 "too big": (mods.pack(dds() + bytes(0x500000)), "5 MiB or more"),
                 "too wide": (mods.pack(dds(8192, 4)), "over 4096")}
        for name, (data, words) in cases.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                self.f.mod("t", {TEX: data})
                self.f.enable("t")
                out, err = self.run_driver(["active"])
                self.assertEqual(out, ["ACTIVE 0 -"], err)
                self.assertRefused(err, words)

    # ---- run time ----------------------------------------------------------------------------------------------------
    def test_file_changed_after_startup_is_not_read(self):
        d = self.f.mod("swap", {SND: b"new", "data/other.bin": b"o"})
        self.f.enable("swap")
        secret = self.f.outside / "secret"
        secret.write_bytes(b"new")

        def swap():
            (d / SND).unlink()
            (d / SND).symlink_to(secret)
            p = d / "data/other.bin"
            p.write_bytes(b"O")

        out, err = self.run_driver(["active", "!between", "open " + SND, "open data/other.bin", "open data/other.bin"],
                                   between=swap)
        self.assertEqual(out, ["ACTIVE 1 -", "NONE", "NONE", "NONE"])
        self.assertIn("can no longer be opened safely", err)
        self.assertEqual(err.count("changed after it was checked"), 1, "a refusal is logged once")

    def test_has_follows_the_run_time_check_so_a_changed_world_file_falls_back_to_the_archive(self):
        # CWorld::Load skips the level archive's copy when PortModHas says yes. If the answer came from the startup table
        # alone, a world file that changed afterwards (an editor saving it, a backup hard-linking it, a chmod) would send
        # the game to a file the file layer then refuses, and the level would not load ("data files are corrupt"). The
        # answer follows the identity check the read makes, so the level's own world is read instead.
        world = "data/worlds/world100.wrd"
        secret = self.f.outside / "secret"
        secret.write_bytes(b"world body")	# the same bytes the manifest lists, behind a link
        changed = "changed after it was checked at startup"
        unsafe = "can no longer be opened safely"
        cases = {
            "rewritten": (lambda p: p.write_bytes(b"world body, edited"), changed),
            "same size, other bytes": (lambda p: p.write_bytes(b"WORLD BODY"), changed),
            "chmod only": (lambda p: (time.sleep(0.05), os.chmod(p, 0o600)), changed),
            "hard link added": (lambda p: os.link(p, p.with_name("backup.wrd")), unsafe),
            "replaced by a link": (lambda p: (p.unlink(), p.symlink_to(secret)), unsafe),
            "deleted": (lambda p: p.unlink(), unsafe),
        }
        for name, (change, reason) in cases.items():
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                d = self.f.mod("w", {world: b"world body"})
                self.f.enable("w")
                out, err = self.run_driver(["has " + world, "has " + world, "!between", "has " + world, "has " + world,
                                            "open " + world], between=lambda: change(d / world))
                self.assertEqual(out, ["HAS 1", "HAS 1", "HAS 0", "HAS 0", "NONE"], err)
                self.assertEqual(err.count(reason), 1, "a refusal is logged once")
                self.assertEqual(err.count("the level's own world is read instead"), 1, err)
                self.assertNotIn("the game's own file is read", err)
                self.assertNotIn("the read fails", err)

    def test_has_says_yes_while_the_file_is_untouched(self):
        world = "data/worlds/world100.wrd"
        self.f.mod("w", {world: b"world body", SND: b"new sound"})
        self.f.enable("w")
        out, err = self.run_driver(["has " + world, "has " + SND, "has " + world, "open " + world, "has " + world])
        self.assertEqual(out, ["HAS 1", "HAS 1", "HAS 1", digest(b"world body"), "HAS 1"], err)
        self.assertNotIn("changed after", err)
        self.assertNotIn("can no longer", err)

    def test_a_refusal_says_what_the_game_reads_instead(self):
        world = "data/worlds/world100.wrd"
        own_file, own_world, no_copy = ("the game's own file is read", "the level's own world is read instead",
                                        "the game has no file of that name, so the read fails")
        for name, commands, expected, then in (
                ("a replaced file asked by name", ["has " + SND], ["HAS 0"], own_file),
                ("a replaced file read", ["open " + SND], ["NONE"], own_file),
                ("an added world asked by name", ["has " + world], ["HAS 0"], own_world),
                ("an added world read without asking", ["open " + world, "has " + world], ["NONE", "HAS 0"], no_copy)):
            with self.subTest(name):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                d = self.f.mod("w", {world: b"world body", SND: b"new sound"})
                self.f.enable("w")

                def change():
                    (d / world).write_bytes(b"world body, edited")
                    (d / SND).write_bytes(b"new sound, edited")

                out, err = self.run_driver(["active", "!between"] + commands, between=change)
                self.assertEqual(out, ["ACTIVE 1 -"] + expected, err)
                self.assertEqual(err.count("changed after it was checked at startup"), 1, err)
                self.assertEqual([w for w in (own_file, own_world, no_copy) if w in err], [then], err)

    def test_opened_mod_is_an_immutable_verified_snapshot(self):
        d = self.f.mod("held", {SND: b"good"})
        self.f.enable("held")
        def mutate():
            (d / SND).write_bytes(b"evil")  # the same inode, after PortModOpenRead returned
        out, err = self.run_driver(["hold " + SND, "held-write", "!between", "held-read", "open " + SND],
                                   between=mutate)
        self.assertEqual(out, ["HELD", f"WRITE -1 {errno.EPERM}", digest(b"good"), "NONE"], err)
        self.assertIn("changed after it was checked", err)

    # ---- bounds and versions -----------------------------------------------------------------------------------------
    def test_at_most_sixteen_mods(self):
        ids = [f"m{i:02}" for i in range(17)]
        for i in ids:
            self.f.mod(i, {SND: i.encode()})
        self.f.enable(*ids)
        out, err = self.run_driver(["active", "open " + SND])
        self.assertEqual(out, ["ACTIVE 16 -", digest(b"m15")])
        self.assertIn("refused m16: at most 16", err)

    def test_total_file_bound_counts_overridden_entries(self):
        # Lower the production limit in an authored test build so three overlapping manifests exercise it cheaply.
        source = MODS_CPP.read_text()
        needle = "kMaxFilesTotal = 8192"
        self.assertEqual(source.count(needle), 1)
        exe = driver(source.replace(needle, "kMaxFilesTotal = 4"))
        for mod_id in ("one", "two", "three"):
            self.f.mod(mod_id, {SND: mod_id.encode(), "data/other.bin": b"x"})
        self.f.enable("one", "two", "three")
        out, err = self.f.run(["active", "open " + SND], exe=exe)
        self.assertEqual(out, ["ACTIVE 2 -", digest(b"two")], err)
        self.assertIn("refused three", err)
        weakened = source.replace(needle, "kMaxFilesTotal = 4").replace(
            "admittedEntries + mine.size()", "((void)admittedEntries, files.size() + mine.size())")
        out, _ = self.f.run(["active", "open " + SND], exe=driver(weakened))
        self.assertEqual(out, ["ACTIVE 3 -", digest(b"three")])

    def test_more_than_1_gib_is_refused(self):
        # 17 sparse 64 MiB files: the 17th crosses 1 GiB (hashing ~1 GiB of zeros takes a few seconds).
        files = {}
        for i in range(17):
            rel = f"data/big{i}.bin"
            (self.f.game / rel).write_bytes(b"g")
            files[rel] = None
        d = self.f.user / "mods/huge"
        (d / "data").mkdir(parents=True)
        zero = hashlib.sha256()
        chunk = bytes(1 << 20)
        for _ in range(64):
            zero.update(chunk)
        for rel in files:
            with open(d / rel, "wb") as fh:
                fh.truncate(64 << 20)
        m = {"schema": 1, "id": "huge", "name": "h", "version": "1",
             "files": [{"path": rel, "size": 64 << 20, "sha256": zero.hexdigest()} for rel in files]}
        (d / "mod.json").write_text(json.dumps(m))
        self.f.enable("huge")
        out, err = self.run_driver(["active"])
        self.assertEqual(out, ["ACTIVE 0 -"])
        self.assertRefused(err, "more than 1 GiB")

    def test_ce_version_range(self):
        exe = self.ce_exe or driver(ce_version="0.1.9")
        cases = {("0.1.8", "0.2"): 1, ("0.1.9", "0.1.9"): 1, ("0.2", None): 0, (None, "0.1.8"): 0, ("0.1", None): 1}
        for (lo, hi), active in cases.items():
            with self.subTest(lo=lo, hi=hi):
                shutil.rmtree(self.f.user / "mods", ignore_errors=True)
                extra = {k: v for k, v in (("ce_min", lo), ("ce_max", hi)) if v}
                self.f.mod("v", {SND: b"new"}, manifest=extra)
                self.f.enable("v")
                out, err = self.f.run(["active"], exe=exe)
                self.assertEqual(out, [f"ACTIVE {active} -"], err)
                if not active:
                    self.assertRefused(err, "outside the mod's ce_min..ce_max")
        out, err = self.run_driver(["active"])		# a development build does not check the range
        self.assertIn("development build", err)

    def test_development_version_does_not_compare_uninitialized_numbers(self):
        # A fail-fast instrumented build proves the comparator is not called when no version is available.
        source = MODS_CPP.read_text()
        absent = "\t(void)out;\n\treturn false;"
        compare = "int VersionCompare(const unsigned a[4], const unsigned b[4])\n{"
        self.assertEqual(source.count(absent), 1)
        self.assertEqual(source.count(compare), 1)
        source = source.replace(absent, "\tout[0] = 0xdeadbeefu;\n\treturn false;")
        source = source.replace(compare, compare + "\n\tif (a[0] == 0xdeadbeefu) abort();")
        exe = driver(source)
        self.f.mod("dev", {SND: b"new"}, manifest={"ce_min": "99.0", "ce_max": "100.0"})
        self.f.enable("dev")
        out, err = self.f.run(["active", "open " + SND], exe=exe)
        self.assertEqual(out, ["ACTIVE 1 -", digest(b"new")], err)
        self.assertIn("development build", err)


# ---- mutants ---------------------------------------------------------------------------------------------------------
MUTANTS = [
    ("'..' allowed", 'if (part == "." || part == "..")', 'if (part == ".")', "test_path_escapes_are_refused"),
    ("final link followed", "O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC", "O_RDONLY | O_NONBLOCK | O_CLOEXEC",
     "test_symlinked_file_is_refused"),
    ("folder link followed", "O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);\n\t\tclose(dir);",
     "O_PATH | O_DIRECTORY | O_CLOEXEC);\n\t\tclose(dir);", "test_symlinked_folder_on_the_path_is_refused"),
    ("hard links allowed", "!S_ISREG(st.st_mode) || st.st_nlink != 1", "!S_ISREG(st.st_mode)",
     "test_hard_link_fifo_and_folder_are_refused"),
    ("tape fence dropped", '"BEA_PORT_TAPE", ', "", "test_every_fence_keeps_mods_off"),
    ("network fence dropped", '"BEA_NET_HOST", ', "", "test_every_fence_keeps_mods_off"),
    ("network handoff fence dropped", ', "BEA_NET_HANDOFF"', "", "test_every_fence_keeps_mods_off"),
    ("unicode digit not checked", "if (digit < 0)", "if (false)", "test_bad_manifests_are_refused"),
    ("hash not compared", "if (memcmp(sum, e.sha, 32) != 0)", "if (false)", "test_hash_and_size_mismatches_are_refused"),
    ("repeated keys allowed", 'return Fail("a repeated key");', "{}", "test_bad_manifests_are_refused"),
    ("unknown keys allowed", 'return "an unknown key in the manifest', 'if (0) return "an unknown key in the manifest',
     "test_bad_manifests_are_refused"),
    ("short block allowed", 'return "a packed block under 1 MiB before the last block";', "{}",
     "test_packed_structure_is_checked"),
    ("trailing compressed bytes allowed", " || consumed != packed", "", "test_packed_structure_is_checked"),
    ("game file not required", "if (!world && !PortGameHasFile(e.path.c_str()))", "if (false)", "test_file_the_game_lacks_is_refused"),
    ("run-time identity unchecked", "if (!SameIdentity(id, f->id))\n\t{\n\t\tclose(fd);",
     "if (false)\n\t{\n\t\tclose(fd);", "test_file_changed_after_startup_is_not_read"),
    ("device names allowed", 'return "a Windows device name";', "{}", "test_path_escapes_are_refused"),
    ("texture limit dropped", "if (total >= kTextureScratch)", "if (false)", "test_textures_must_be_dds_within_the_game_limit"),
    ("mod count unbounded", "if ((int)gMods.size() >= kMaxMods)", "if (false)", "test_at_most_sixteen_mods"),
    ("version range unchecked", "if (released)\n\t\t{\n\t\t\tint order", "if ((void)released, false)\n\t\t{\n\t\t\tint order", "test_ce_version_range"),
    ("snapshot not sealed", "fcntl(snapshot, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)",
     "0", "test_opened_mod_is_an_immutable_verified_snapshot"),
    ("every name is a world file", "const bool world = WorldFile(key);", "const bool world = WorldFile(key) || true;",
     "test_only_the_three_digit_world_name_may_be_added"),
    ("world name loosened",
     'if (key.size() != n + 3 + 4 || key.compare(0, n, head) != 0 || key.compare(n + 3, 4, ".wrd") != 0)',
     "if (key.compare(0, n, head) != 0)", "test_only_the_three_digit_world_name_may_be_added"),
    ("world size bound dropped", "if (world && e.size > kMaxWorld)", "if (false)", "test_world_file_size_is_bounded"),
    ("has answers for everything", "if (!f)\n\t\treturn false;\n\tIdentity id;", "if (!f)\n\t\treturn true;\n\tIdentity id;",
     "test_has_answers_by_name_and_never_when_mods_are_off"),
    ("has trusts the startup table", "bool same = SameIdentity(id, f->id);", "bool same = true;",
     "test_has_follows_the_run_time_check_so_a_changed_world_file_falls_back_to_the_archive"),
    ("has claims a file it cannot open", 'Refused(f, "can no longer be opened safely", true);\n\t\treturn false;',
     'Refused(f, "can no longer be opened safely", true);\n\t\treturn true;',
     "test_has_follows_the_run_time_check_so_a_changed_world_file_falls_back_to_the_archive"),
    ("refusal wording ignores added files", "const char *then = !f->added ?", "const char *then = true ?",
     "test_a_refusal_says_what_the_game_reads_instead"),
    ("refusal wording ignores the fallback", ": fallback ? \"the level's own world is read instead\"",
     ": (fallback || true) ? \"the level's own world is read instead\"", "test_a_refusal_says_what_the_game_reads_instead"),
    ("served logged on every read", "if (!__atomic_exchange_n(&const_cast<File *>(f)->served, 1, __ATOMIC_RELAXED))",
     "if (true)", "test_the_first_read_of_each_replaced_file_is_logged_once"),
    ("platform refusal removed", 'Log("off on Windows until mod path safety is validated");\n\treturn;',
     'Log("off on Windows until mod path safety is validated");',
     "test_platform_off_build_refuses_listed_mods_and_touches_nothing", PLATFORM_OFF),
    ("platform switch ignored", "#if defined(_WIN32) || defined(PORT_MODS_PLATFORM_OFF)", "#if defined(_WIN32)",
     "test_platform_off_build_refuses_listed_mods_and_touches_nothing", PLATFORM_OFF),
]


def run_mutants():
    source = MODS_CPP.read_text()
    failures = 0
    for name, old, new, test, *defines in MUTANTS:
        if source.count(old) != 1:
            print(f"mutant {name}: its text is not in mods.cpp exactly once")
            failures += 1
            continue
        mutant = source.replace(old, new)
        try:
            exe = driver(mutant, defines=defines[0] if defines else ())
            ce_exe = driver(mutant, ce_version="0.1.9") if test == "test_ce_version_range" else None
        except RuntimeError as e:
            print(f"mutant {name}: does not build: {e}")
            failures += 1
            continue
        ModsTests.exe, ModsTests.ce_exe = exe, ce_exe
        result = unittest.TextTestRunner(stream=open(os.devnull, "w"), verbosity=0).run(ModsTests(test))
        ModsTests.exe = ModsTests.ce_exe = None
        killed = not result.wasSuccessful()
        print(f"mutant {name}: {'killed by ' + test if killed else 'SURVIVED ' + test}")
        failures += not killed
    return failures


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    mutants = "--no-mutants" not in sys.argv
    argv = [a for a in sys.argv if a != "--no-mutants"]
    program = unittest.main(argv=argv, exit=False, verbosity=2)
    if not program.result.wasSuccessful():
        return 1
    if mutants:
        failed = run_mutants()
        print(f"mutants: {len(MUTANTS) - failed} of {len(MUTANTS)} killed")
        return 1 if failed else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
