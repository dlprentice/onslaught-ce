#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The mod author's commands (port/tools/mods.py new and check) and the guide (port/MODS.md), on authored fixtures: a game folder holding one
authored texture where the Battle Engine's is, never a real game. `check` is run against the actual loader's validator, built from
mods.cpp by mods_tests.py (bea --check-mods), as the game would run it.

- `new` makes a sample mod the loader passes, from the texture of the install it is given, in a new folder; it refuses a bad id, a folder that
  is there and a user folder inside the game, and writes nothing else (not ce.ini, not the game).
- `check` prints the loader's lines and answers its status: 0 passed, 1 refused (with the mod named), 3 no checks in the program.
- The guide's numbers are the loader's numbers: a limit that changes in mods.cpp and not in port/MODS.md fails here.
"""
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mods  # noqa: E402
import mods_tests  # noqa: E402
import mods_tool_safety_tests as safety  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "port/tools/mods.py"
GUIDE = ROOT / "port/MODS.md"
MODS_CPP = ROOT / "port/platform/common/mods.cpp"
OUT = ROOT / "local-data/port/mods-author-tests"
VERSION = "0.2.0"


class AuthorTools(unittest.TestCase):
    def setUp(self):
        OUT.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix="case-", dir=OUT))
        self.user, self.game = self.root / "user", self.root / "game"
        self.user.mkdir()
        (self.game / mods.BE_TEXTURE).parent.mkdir(parents=True)
        # an authored texture with colour in it (the safety fixture's is all zeros, which a rotation leaves as it is)
        texture = safety.dds()[:128] + bytes([0xff] * 8 + [0x1f, 0x00, 0xe0, 0x07] + [0x1b] * 4) * 4
        self.original = mods.pack(texture)
        (self.game / mods.BE_TEXTURE).write_bytes(self.original)
        self.exe = mods_tests.driver(ce_version=VERSION)

    def tearDown(self):
        subprocess.run(["chmod", "-R", "u+rwX", str(self.root)], check=False)
        subprocess.run(["rm", "-rf", str(self.root)], check=False)

    def cli(self, *args, env=None):
        e = {k: v for k, v in os.environ.items() if k not in mods_tests.FENCES and k not in ("BEA_USER_DIR", "BEA_GAME_DIR", "BEA_EXE")}
        e.update(env or {})
        return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True, timeout=120, env=e)

    def new(self, mod_id="my-first-mod", *extra):
        return self.cli("new", mod_id, "--name", "My First Mod", "--author", "Tester", "--user", self.user, "--game", self.game, *extra)

    def check(self, *ids, bea=None):
        return self.cli("check", *ids, "--user", self.user, "--game", self.game, "--bea", bea or self.exe)

    def test_new_makes_a_mod_the_loader_passes(self):
        done = self.new()
        self.assertEqual(done.returncode, 0, done.stderr)
        manifest = json.loads((self.user / "mods/my-first-mod/mod.json").read_text())
        self.assertEqual((manifest["id"], manifest["name"], manifest["author"], manifest["schema"]), ("my-first-mod", "My First Mod", "Tester", 1))
        self.assertEqual([f["path"] for f in manifest["files"]], [mods.BE_TEXTURE])
        self.assertNotEqual((self.user / "mods/my-first-mod" / mods.BE_TEXTURE).read_bytes(), self.original, "the colours are different")
        self.assertIn("not enabled", done.stdout)
        checked = self.check("my-first-mod")
        self.assertEqual((checked.returncode, checked.stdout.splitlines()[-1]), (0, "result: 1 passed, 0 refused"), checked.stdout + checked.stderr)

    def test_new_writes_nothing_but_the_mod(self):
        (self.user / "ce.ini").write_bytes(b"record_sessions=0\nmods=other\n")
        game_before = {str(p): p.read_bytes() for p in self.game.rglob("*") if p.is_file()}
        self.assertEqual(self.new().returncode, 0)
        self.assertEqual((self.user / "ce.ini").read_bytes(), b"record_sessions=0\nmods=other\n", "it never enables a mod or edits ce.ini")
        self.assertEqual({str(p): p.read_bytes() for p in self.game.rglob("*") if p.is_file()}, game_before)
        self.assertEqual(sorted(p.name for p in (self.user / "mods").iterdir()), ["my-first-mod"])

    def test_new_refuses_what_the_loader_would_refuse_or_what_is_there(self):
        for bad in ("Bad Name", "con", "x" * 49, "UPPER"):
            done = self.new(bad)
            self.assertNotEqual(done.returncode, 0, bad)
            self.assertFalse((self.user / "mods").exists(), "nothing is made for " + bad)
        self.assertEqual(self.new().returncode, 0)
        marker = self.user / "mods/my-first-mod/data" / "marker"
        marker.parent.mkdir(exist_ok=True)
        marker.write_bytes(b"mine")
        again = self.new()
        self.assertNotEqual(again.returncode, 0)
        self.assertEqual(marker.read_bytes(), b"mine", "a folder that is there is left as it is")
        inside = self.cli("new", "x-mod", "--name", "X", "--user", self.game / "u", "--game", self.game)
        self.assertNotEqual(inside.returncode, 0, "a user folder inside the game is refused")
        gone = self.cli("new", "x-mod", "--name", "X", "--user", self.user, "--game", self.root / "nowhere")
        self.assertNotEqual(gone.returncode, 0, "a game folder that is not there is refused")
        self.assertFalse((self.user / "mods/x-mod").exists())

    def test_check_names_a_damaged_mod_and_answers_its_status(self):
        self.assertEqual(self.new().returncode, 0)
        texture = self.user / "mods/my-first-mod" / mods.BE_TEXTURE
        data = bytearray(texture.read_bytes())
        data[-1] ^= 1
        texture.write_bytes(bytes(data))
        done = self.check("my-first-mod")
        self.assertEqual(done.returncode, 1)
        self.assertRegex(done.stdout, r"refused my-first-mod: ")
        self.assertEqual(done.stdout.splitlines()[-1], "result: 0 passed, 1 refused")
        (self.user / "ce.ini").write_text("mods=my-first-mod\n")
        self.assertEqual(self.check().returncode, 1, "with no name, the mods ce.ini enables")

    def test_check_without_the_program_or_with_one_that_has_no_checks(self):
        done = self.cli("check", "--user", self.user, "--game", self.game, "--bea", self.root / "no-such-program")
        self.assertNotEqual(done.returncode, 0)
        self.assertIn("game program was not found", done.stderr)
        fake = self.root / "bea"
        fake.write_text("#!/bin/sh\nexit 3\n")
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
        done = self.check(bea=fake)
        self.assertEqual(done.returncode, 3)
        self.assertIn("no mod checks", done.stderr)

    # ---- the guide ---------------------------------------------------------------------------------------------------------------
    def test_the_guide_says_what_the_loader_does(self):
        guide, code = GUIDE.read_text(), MODS_CPP.read_text()
        limits = re.search(r"kMaxMods = (\d+), kMaxFilesPerMod = (\d+), kMaxFilesTotal = (\d+), kMaxPathParts = (\d+)", code).groups()
        sizes = re.search(r"kMaxFile = (\d+)ull << 20, kMaxTotal = 1ull << 30, kMaxWorld = (\d+)ull << 20", code).groups()
        archive = re.search(r"kMaxArchive = (\d+)ull << 20, kMaxArchiveUnpacked = (\d+)ull << 20", code).groups()
        path = re.search(r"kMaxManifest = (\d+) << 10, kMaxPath = (\d+), kMaxPart = (\d+)", code).groups()
        first, last = re.search(r"kArchiveFirst = (\d+), kArchiveLast = (\d+);\n#endif", code).groups() if False else ("850", "899")
        for text in (f"{limits[0]} mods on at once", f"{int(limits[1]):,} files in a mod", f"{int(limits[2]):,} across", "1 GiB",
                     f"{sizes[0]} MiB unpacked", f"{sizes[1]} MiB", f"{archive[0]} MiB packed and {archive[1]} MiB unpacked",
                     f"{path[1]} characters, {limits[3]} folders deep", f"no part over {path[2]}", f"{path[0]} KiB", f"NNN from {first} to {last}"):
            self.assertIn(text, guide)
        self.assertIn("kArchiveFirst = 850, kArchiveLast = 899", code)
        # the keys schema 1 knows, and the id rule and device names
        known = re.search(r'known\[\] = \{(.*?)\};', code, re.S).group(1)
        keys = re.findall(r'"(\w+)"', known)
        self.assertEqual(keys, ["schema", "id", "name", "version", "author", "description", "ce_min", "ce_max", "files"])
        for key in keys:
            self.assertIn(f"`{key}`", guide)
        self.assertIn("`com0` to `com9`", guide)
        self.assertEqual(mods.DEVICE_ID_RE.pattern, "con|prn|aux|nul|com[0-9]|lpt[0-9]")
        self.assertIn("1 to 48 lowercase letters, digits", guide)
        self.assertEqual(mods.ID_RE.pattern, "[a-z0-9_-]{1,48}")
        # the commands it names exist, and the status codes are the validator's
        for command in ("mods.py new", "mods.py check", "mods.py manifest"):
            self.assertIn(command, guide)
        self.assertIn("0 when nothing was refused, 1 when\nsomething was, and 3", guide)

    def test_the_guide_has_its_header_and_no_retail_bytes(self):
        guide = GUIDE.read_text()
        head = guide.splitlines()[:6]
        for field in ("Status:", "Last updated:", "Summary:"):
            self.assertTrue(any(line.startswith(field) for line in head), field)
        self.assertIsNone(re.search(r"(?:[0-9a-f]{2} ){8}", guide), "no run of byte values")


if __name__ == "__main__":
    unittest.main()
