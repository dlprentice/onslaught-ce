#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Mods page's model (port/game/ce_modspage_model.h, the file the game compiles) over the actual mod loader's report
(port/platform/common/mods.cpp: PortModsReport, PortModsClash, PortModsWriteLine) and the actual ce.ini reader and writer (ce.cpp), on the
host through port/tools/tests/ce_modspage_driver.cpp. Mod folders are made by the producer authors use, port/tools/mods.py (its packer and
its manifest writer), and then damaged the ways a download or a player damages one. No game, retail file, window, device or network.

- The rows: every folder of the mods folder, the mods of ce.ini's line first in its order; a packed download, a link, a folder that cannot
  be a mod and a mod the line names that is gone are rows too.
- Every kind of refusal (PortModWhy) is reached by a mod damaged that way, shows Can't Load with that kind's sentence, and the validator
  (bea --check-mods through port/tools/mods_driver.cpp) refuses the same mods with the same words the row keeps as its detail.
- Turning a mod on or off and moving it write exactly ce.ini's mods= line and nothing else; a write that fails changes nothing; sixteen at most.
- Two and three mods on one file: which wins, as the loader itself then serves it.
- What a stranger's mod.json may hold never reaches the page outside the game face's characters; no sentence of the page breaks the house
  rules for player text (no file names, settings keys or forbidden words; a full stop).
Then mutants: each removes one behaviour from a copy of mods.cpp or of the model and must make a named test fail.

    python3 port/tools/ce_modspage_tests.py            # the tests, then the mutants
    python3 port/tools/ce_modspage_tests.py --no-mutants
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
sys.path.insert(0, str(PORT / "tools"))
import mods  # noqa: E402
import mods_tests  # noqa: E402  (its fixture of a game and a user folder, its authored texture and level archive, its build of the validator)

MODS_CPP = PORT / "platform/common/mods.cpp"
CE_CPP = PORT / "platform/common/ce.cpp"
MODS_H = PORT / "platform/common/mods.h"
MODEL = PORT / "game/ce_modspage_model.h"
DRIVER = PORT / "tools/tests/ce_modspage_driver.cpp"
BUILD = Path(os.environ.get("CE_MODSPAGE_BUILD") or ROOT / "local-data/port/ce-modspage-tests")
VERSION = "0.2.0"
TEX, SND, OTHER = mods_tests.TEX, mods_tests.SND, "data/other.bin"
BUILT = {}

# PortModWhy, as mods.h numbers it (test_the_kinds_are_the_headers checks the names against the header).
WHY = ("NONE", "MISSING", "LINK", "NAME", "PACKED", "INFO", "RENAMED", "NEWER", "OLDER", "LIST", "NOT_IN_GAME", "FILE_MISSING", "DAMAGED", "FORMAT",
       "TOO_BIG", "OTHER")
DONE, FULL, NOT_SAVED, NOTHING = range(4)

# Player text may not hold these (the interface guide's wording rule): settings and file names, flags, and words for unfinished work.
FORBIDDEN = ("ce.ini", "mod.json", ".json", ".aya", "README", "--", "mods=", "Experimental", "experimental", "Prototype", "test", "best effort", "SHA", "manifest",
             "schema", "sha256")
NOT_IN_FACE = "$@[\\]^_`{|}~"


def driver(mods_text=None, model_text=None, ce_text=None):
    """Builds the driver from mods.cpp, the model and ce.cpp (or a mutant's text of any), once per set of texts."""
    mods_text = mods_text if mods_text is not None else MODS_CPP.read_text()
    model_text = model_text if model_text is not None else MODEL.read_text()
    ce_text = ce_text if ce_text is not None else CE_CPP.read_text()
    key = hashlib.sha256((mods_text + "\0" + model_text + "\0" + ce_text).encode()).hexdigest()[:16]
    if key in BUILT:
        return BUILT[key]
    out = BUILD / f"driver2-{key}"
    # the model includes "../platform/common/mods.h", mods.cpp "../../updater/sha256.h" and ce.cpp the network and updater headers: the copies sit as
    # the sources do
    for folder in ("port/game", "port/platform/common"):
        (out / folder).mkdir(parents=True, exist_ok=True)
    for folder in ("network", "updater"):
        if not (out / "port" / folder).exists():
            os.symlink(PORT / folder, out / "port" / folder)
    (out / "port/platform/common/mods.cpp").write_text(mods_text)
    (out / "port/platform/common/ce.cpp").write_text(ce_text)
    (out / "port/game/ce_modspage_model.h").write_text(model_text)
    for name in ("mods.h", "portlog.h"):
        shutil.copy(PORT / "platform/common" / name, out / "port/platform/common" / name)
    cxx, cc = os.environ.get("CXX", "g++"), os.environ.get("CC", "gcc")
    steps = [
        [cc, "-O2", "-c", str(PORT / "updater/sha256.c"), "-o", str(out / "sha256.o")],
        # ce.cpp's ce.ini reader and writer do not depend on the version (with CE_VERSION it would also need SDL)
        [cxx, "-std=gnu++17", "-O1", "-w", "-c", "-I", str(out / "port/platform/common"), str(out / "port/platform/common/ce.cpp"), "-o", str(out / "ce.o")],
        [cxx, "-std=gnu++17", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-format-truncation", f'-DCE_VERSION="{VERSION}"', "-I", str(out / "port/game"),
         "-I", str(out / "port/platform/common"), "-I", str(out / "port/updater"), str(DRIVER), str(out / "port/platform/common/mods.cpp"),
         str(out / "ce.o"), str(out / "sha256.o"), "-lz", "-o", str(out / "driver")],
    ]
    for step in steps:
        done = subprocess.run(step, capture_output=True, text=True, timeout=300, preexec_fn=mods_tests.no_core)
        if done.returncode:
            raise RuntimeError(done.stderr)
    BUILT[key] = out / "driver"
    return BUILT[key]


class House(mods_tests.Fixture):
    """A game folder and a user folder, with mods as an author's tool leaves them."""

    def make(self, mod_id, files, name=None, version="1.0", author="", description="", **manifest):
        """A mod folder written by mods.py: the files, then its manifest (mods.py manifest)."""
        folder = self.user / "mods" / mod_id
        for rel, data in files.items():
            (folder / rel).parent.mkdir(parents=True, exist_ok=True)
            (folder / rel).write_bytes(data)
        mods.write_manifest(folder, name if name is not None else mod_id.replace("-", " ").title(), version, author, description, game=self.game, **manifest)
        return folder

    def edit(self, mod_id, change):
        """mod.json rewritten: change(dict) edits the manifest mods.py wrote."""
        path = self.user / "mods" / mod_id / "mod.json"
        manifest = json.loads(path.read_text())
        change(manifest)
        path.write_text(json.dumps(manifest, indent=2) + "\n")

    def line(self, *ids):
        (self.user / "ce.ini").write_text("record_sessions=0\nupdate_check=0\nmods=" + ",".join(ids) + "\n")

    def ini(self):
        return (self.user / "ce.ini").read_text() if (self.user / "ce.ini").exists() else None

    def page(self, commands, exe=None, env=None):
        """The driver's answers, one for each command."""
        e = {k: v for k, v in os.environ.items() if k not in mods_tests.FENCES and k != "BEA_PORT_CE_LAYER"}
        e.update(BEA_USER_DIR=str(self.user), BEA_GAME_DIR=str(self.game))
        e.update(env or {})
        done = subprocess.run([str(exe or driver())], input="".join(c + "\n" for c in commands), capture_output=True, text=True, env=e, timeout=120,
                              preexec_fn=mods_tests.no_core)
        if done.returncode:
            raise AssertionError("the driver failed: " + done.stderr)
        answers = done.stdout.splitlines()
        if len(answers) != len(commands):
            raise AssertionError("an answer for each command: %r" % (answers,))
        return answers

    def rows(self, exe=None, env=None):
        """The page as opened: (the ROWS line's fields, the rows as dicts)."""
        head = self.page(["read"], exe, env)[0]
        count = int(head.split()[1])
        answers = self.page(["read"] + [f"row {i}" for i in range(count)] + [f"hint {i}" for i in range(count)] + [f"detail {i}" for i in range(count)], exe, env)
        fields = dict(part.split("=", 1) for part in answers[0].split()[2:])
        rows = []
        for i in range(count):
            mod_id, name, state, why, actions, loaded = answers[1 + i][4:].split("|")
            description, by, clash, sentence, longer = answers[1 + count + i][5:].split("|")
            rows.append(dict(id=mod_id, name=name, state=state, why=WHY[int(why)], actions=actions.split(","), loaded=loaded == "1", description=description, by=by,
                             clash=clash, sentence=sentence, longer=longer, detail=answers[1 + 2 * count + i][7:]))
        return fields, rows


def sentences(exe=None):
    done = subprocess.run([str(exe or driver())], input="sentences\n", capture_output=True, text=True, timeout=60, env={"PATH": os.environ.get("PATH", "/usr/bin")})
    lines = done.stdout.splitlines()
    assert lines[-1] == "END", lines
    return [line[9:] for line in lines[:-1]]


def kinds_of_the_model():
    return re.search(r"kCeModWhy\[PORT_MOD_WHY_COUNT\] = \{(.*?)\n\};", MODEL.read_text(), re.S).group(1)


class ModsPageTests(unittest.TestCase):
    exe = None		# a mutant's driver, when set

    def setUp(self):
        BUILD.mkdir(parents=True, exist_ok=True)
        self._tmp = tempfile.TemporaryDirectory(prefix="page-", dir=BUILD)
        self.h = House(self._tmp.name)
        self.tex = mods.pack(mods_tests.dds())

    def tearDown(self):
        subprocess.run(["chmod", "-R", "u+rwX", self._tmp.name], check=False)
        self._tmp.cleanup()

    def rows(self, **kw):
        return self.h.rows(self.exe, **kw)

    def page(self, commands, **kw):
        return self.h.page(commands, self.exe, **kw)

    def three(self):
        """Three mods as an author's tool writes them: two change the texture, one of those the sound too, one adds a level."""
        self.h.make("red-engine", {TEX: self.tex}, name="Red Engine", version="1.2", author="Kite", description="Paints the Battle Engine red.")
        self.h.make("night-ops", {TEX: mods.pack(mods_tests.dds(16, 16)), SND: b"night sound"}, name="Night Ops", author="Vale",
                    description="Darker hull and a new engine sound.")
        self.h.make("arena", {mods_tests.LEVEL_ARCHIVE: mods_tests.level_archive()}, name="Arena", version="0.3", description="A new arena for two players.")

    # ---- the rows ------------------------------------------------------------------------------------------------------------------
    def test_no_mods_folder_and_an_empty_one_are_empty_pages(self):
        fields, rows = self.rows()
        self.assertEqual((rows, fields["folder"], fields["off"]), ([], "0", "0"))
        (self.h.user / "mods").mkdir()
        fields, rows = self.rows()
        self.assertEqual((rows, fields["folder"]), ([], "1"))
        self.assertEqual(self.page(["read", "note"])[1], "NOTE -")

    def test_every_folder_is_a_row_the_lines_mods_first_in_its_order(self):
        self.three()
        self.h.line("night-ops", "arena")
        fields, rows = self.rows()
        self.assertEqual([r["id"] for r in rows], ["night-ops", "arena", "red-engine"], "the line's order, then the rest by name")
        self.assertEqual([r["state"] for r in rows], ["On", "On", "Off"])
        self.assertEqual([r["name"] for r in rows], ["Night Ops", "Arena", "Red Engine"])
        self.assertEqual(fields["loaded"], "night-ops,arena", "this process loaded the line's mods, in order")
        self.assertEqual([r["loaded"] for r in rows], [True, True, False])
        self.assertEqual(rows[2]["by"], "By Kite. Version 1.2.")
        self.assertEqual(rows[2]["description"], "Paints the Battle Engine red.")
        self.assertEqual(rows[1]["by"], "Version 0.3.", "a mod that names no author")
        self.assertEqual(rows[0]["actions"], ["Turn Off", "Move Down", "Details"], "the first row cannot move up")
        self.assertEqual(rows[1]["actions"], ["Turn Off", "Move Up", "Details", "Play"], "the last of the mods that are on cannot move down past one that is off; a level it adds can be played")
        self.assertEqual(rows[2]["actions"], ["Turn On", "Details"], "a mod that is off has no place in the order")

    def test_a_line_that_names_a_mod_twice_has_one_row(self):
        self.three()
        self.h.line("night-ops", "arena", "night-ops")
        _, rows = self.rows()
        self.assertEqual([r["id"] for r in rows], ["night-ops", "arena", "red-engine"])

    def test_what_a_mod_changes_is_said_in_words(self):
        self.three()
        self.h.make("mix", {TEX: self.tex, SND: b"s", "data/worlds/world100.wrd": mods_tests.world_body(), "data/worlds/world110.wrd": mods_tests.world_body()})
        self.h.line()
        _, rows = self.rows()
        index = {r["id"]: i for i, r in enumerate(rows)}
        answers = self.page(["read"] + [f"contents {index[m]}" for m in ("red-engine", "night-ops", "arena", "mix")])
        self.assertEqual(answers[1:], ["CONTENTS Replaces 1 game file.", "CONTENTS Replaces 2 game files.", "CONTENTS Adds 1 level.",
                                       "CONTENTS Replaces 2 game files. Changes 2 levels."])

    def test_reading_the_page_writes_and_changes_nothing(self):
        self.three()
        (self.h.user / "mods/packed.zip").write_bytes(b"PK")
        self.h.line("night-ops", "gone")
        before = self.h.tree()
        self.rows()
        self.assertEqual(self.h.tree(), before, "no file of the user or game folder is written, touched or made")

    def test_more_folders_than_rows(self):
        for i in range(70):
            self.h.make(f"m{i:02d}", {SND: b"x%d" % i})
        fields, rows = self.rows()
        self.assertEqual((len(rows), fields["more"]), (64, "6"))
        self.assertEqual(self.page(["read", "note"])[1], "NOTE Only the first 64 mods are shown.")

    def test_a_level_a_mod_adds_can_be_played_only_while_the_game_has_the_mod(self):
        self.three()
        self.h.make("quartet", {f"data/resources/{n}_res_PC.aya": mods_tests.level_archive(level=n) for n in (884, 885, 886, 887)}, name="Quartet")
        self.h.line("arena", "quartet")
        fields, rows = self.rows()
        by_id = {r["id"]: r for r in rows}
        self.assertEqual(by_id["arena"]["actions"][-1], "Play", "one level: Play")
        self.assertEqual(by_id["quartet"]["actions"], ["Turn Off", "Move Up", "Details", "Play Level 1", "Play Level 2", "Play Level 3"], "several: the first three, in order")
        self.assertEqual(by_id["red-engine"]["actions"], ["Turn On", "Details"], "a mod that adds no level has none to play")
        index = {r["id"]: i for i, r in enumerate(rows)}
        answers = self.page(["read", f"play {index['arena']} 0", f"play {index['arena']} 1", f"play {index['quartet']} 0", f"play {index['quartet']} 2", f"play {index['quartet']} 3",
                             f"play {index['red-engine']} 0"])
        self.assertEqual(answers[1:], ["PLAY 888", "PLAY 0", "PLAY 884", "PLAY 886", "PLAY 0", "PLAY 0"])
        # turned off: nothing to play. Turned on since the game started (it has not loaded the mod): nothing to play until the next start
        answers = self.page(["read", f"toggle {index['arena']}"] + [f"row {i}" for i in range(len(rows))])
        row = [a for a in answers if a.startswith("ROW arena")][0]
        self.assertTrue("|Off|" in row and "Play" not in row, row)
        self.h.line("quartet")
        _, rows = self.rows()
        arena = [i for i, r in enumerate(rows) if r["id"] == "arena"][0]
        answers = self.page(["read", f"toggle {arena}", "order"] + [f"row {i}" for i in range(len(rows))])
        row = [a for a in answers if a.startswith("ROW arena")][0]
        self.assertTrue("|On|" in row and "Play" not in row and row.endswith("|0"), "turned on since the game started: " + row)

    def test_only_the_package_serving_a_level_can_play_it_or_receive_its_return(self):
        shared, other = 884, 885

        def archives(*levels):
            return {f"data/resources/{n}_res_PC.aya": mods_tests.level_archive(level=n) for n in levels}

        self.h.make("first-pack", archives(shared, other))
        self.h.make("last-pack", archives(shared))
        self.h.line("first-pack", "last-pack")
        _, rows = self.rows()
        self.assertEqual([r["actions"][-1:] for r in rows], [["Play"], ["Play"]])
        result = self.page(["read", "play 0 0", "play 0 1", "play 1 0", f"return {shared}", f"return {other}",
                            "move 0 1", "read", "play 0 0", "play 1 0", f"return {shared}", f"return {other}"])
        self.assertEqual(result[1:6], [f"PLAY {other}", "PLAY 0", f"PLAY {shared}", "RETURN last-pack", "RETURN first-pack"])
        # Rereading a pending reorder changes row positions, but which package supplies each archive stays fixed until restart.
        self.assertEqual(result[8:], [f"PLAY {shared}", f"PLAY {other}", "RETURN last-pack", "RETURN first-pack"])
        _, rows = self.rows()
        self.assertEqual([r["id"] for r in rows], ["last-pack", "first-pack"])
        self.assertFalse(any(a.startswith("Play") for a in rows[0]["actions"]))
        self.assertEqual(rows[1]["actions"][-2:], ["Play Level 1", "Play Level 2"])
        restarted = self.page(["read", "play 0 0", "play 1 0", f"return {shared}", f"return {other}", "return 0"])
        self.assertEqual(restarted[1:], ["PLAY 0", f"PLAY {shared}", "RETURN first-pack", "RETURN first-pack", "RETURN -"])

    def test_pages_share_the_mods_evenly_so_the_last_is_never_a_few_rows(self):
        # up to ten mods are one page; more are pages of at most nine rows and More Mods, the fewest pages, each with about the same rows
        answers = self.page(["pages %d" % n for n in (0, 1, 10, 11, 18, 19, 27, 28, 61, 64)])
        self.assertEqual(answers, ["PAGES 1 10", "PAGES 1 10", "PAGES 1 10", "PAGES 2 6", "PAGES 2 9", "PAGES 3 7", "PAGES 3 9", "PAGES 4 7", "PAGES 7 9",
                                   "PAGES 8 8"])
        for n in range(1, 65):
            pages, per = (int(x) for x in self.page(["pages %d" % n])[0].split()[1:])
            last = n - (pages - 1) * per
            self.assertTrue(per <= (10 if n <= 10 else 9) and pages * per >= n and 0 < last <= per, (n, pages, per, last))
            self.assertTrue(pages == 1 or last >= per - pages + 1, ("the last page is short by more than the pages are", n, pages, per, last))

    def test_the_mods_folder_is_made_when_missing_and_a_file_or_link_named_mods_is_not_one(self):
        mods_dir = self.h.user / "mods"
        if mods_dir.exists():
            shutil.rmtree(mods_dir)
        self.assertEqual(self.page(["folder"]), ["FOLDER 1"])
        self.assertTrue(mods_dir.is_dir())
        self.assertEqual(self.page(["folder"]), ["FOLDER 1"], "an existing folder is fine")
        mods_dir.rmdir()
        mods_dir.write_text("not a folder")
        self.assertEqual(self.page(["folder"]), ["FOLDER 0"], "a file named mods is not a mods folder")
        self.assertEqual(mods_dir.read_text(), "not a folder", "and is left as it is")
        mods_dir.unlink()
        (self.h.user / "elsewhere").mkdir()
        os.symlink(self.h.user / "elsewhere", mods_dir)
        self.assertEqual(self.page(["folder"]), ["FOLDER 0"], "a link is refused as the loader refuses it")

    # ---- every kind of refusal ------------------------------------------------------------------------------------------------------
    def damaged(self):
        """One mod for every kind a folder can show, each damaged one way. Returns {id or entry name: kind}."""
        h = self.h
        good = {SND: b"sound"}
        h.make("fine", good)
        os.symlink(h.user / "mods/fine", h.user / "mods/a-link")
        h.make("renamed-copy", good)
        h.edit("renamed-copy", lambda m: m.update(id="shiny-hull"))
        (h.user / "mods/Big Mod (1)").mkdir()
        (h.user / "mods/cool-mod.zip").write_bytes(b"PK\x03\x04")
        (h.user / "mods/no-info").mkdir()
        h.make("bad-info", good)
        (h.user / "mods/bad-info/mod.json").write_text("{ not json")
        h.make("too-new", good, ce_min="9.0")
        h.make("next-schema", good)
        h.edit("next-schema", lambda m: m.update(schema=2))
        h.make("too-old", good, ce_max="0.1.0")
        h.make("bad-list", good)
        h.edit("bad-list", lambda m: m["files"].append(dict(m["files"][0])))
        h.make("other-game", {"data/not-in-this-game.bin": b"x"})
        h.make("lost-file", {SND: b"sound", OTHER: b"other"})
        (h.user / "mods/lost-file" / OTHER).unlink()
        h.make("changed", good)
        (h.user / "mods/changed" / SND).write_bytes(b"sounD")
        h.make("not-a-texture", {TEX: mods.pack(b"this is not an image")})
        h.make("huge-world", {"data/worlds/world123.wrd": bytes((4 << 20) + 1)})
        return {"fine": "NONE", "gone": "MISSING", "a-link": "LINK", "Big Mod (1)": "NAME", "cool-mod.zip": "PACKED", "no-info": "INFO", "bad-info": "INFO",
                "renamed-copy": "RENAMED", "too-new": "NEWER", "next-schema": "NEWER", "too-old": "OLDER", "bad-list": "LIST", "other-game": "NOT_IN_GAME",
                "lost-file": "FILE_MISSING", "changed": "DAMAGED", "not-a-texture": "FORMAT", "huge-world": "TOO_BIG"}

    def test_every_kind_of_refusal_is_reached_and_has_its_sentence(self):
        expected = self.damaged()
        self.h.line("gone")
        _, rows = self.rows()
        said = sentences(self.exe)
        table = dict(zip(WHY[1:], said))
        got = {}
        for row in rows:
            name = row["id"] if row["id"] != "-" else row["name"]
            got[name] = row["why"]
        self.assertEqual(got, expected)
        self.assertEqual([r["name"] for r in rows if r["id"] == "renamed-copy"], ["Renamed Copy"], "a mod that cannot load still shows the name it gives itself")
        for row in rows:
            if row["why"] == "NONE":
                self.assertEqual((row["state"], row["sentence"], row["detail"]), ("Off", "", ""))
                continue
            self.assertEqual(row["state"], "Can't Load", row)
            self.assertTrue(row["detail"], "the loader's own words are kept for the log")
            if row["why"] == "RENAMED":
                self.assertEqual(row["sentence"], "This mod's folder has the wrong name. Rename the folder to shiny-hull.")
            else:
                self.assertEqual(row["sentence"], table[row["why"]], row)
        self.assertEqual(sorted(set(got.values()) - {"NONE"}), sorted(set(WHY) - {"NONE", "OTHER"}),
                         "every kind but the one no refusal names is reached by a damaged mod")

    def test_a_refused_mod_can_be_turned_off_but_not_on(self):
        self.damaged()
        self.h.line("changed", "gone")
        _, rows = self.rows()
        by_id = {r["id"]: r for r in rows}
        self.assertEqual(by_id["changed"]["actions"], ["Turn Off", "Details"])
        self.assertEqual(by_id["gone"]["actions"], ["Turn Off", "Details"])
        self.assertEqual(by_id["too-old"]["actions"], ["Details"], "a mod that cannot load is not offered Turn On")
        self.assertEqual(self.page(["read", "toggle 0", "line"])[1:], ["RESULT 0", "LINE gone"], "off")
        # it is a row among those that are off now, and cannot be turned on again; an entry only the line named is gone with its name
        _, rows = self.rows()
        by_id = {r["id"]: r for r in rows}
        self.assertEqual((by_id["changed"]["state"], by_id["changed"]["actions"]), ("Can't Load", ["Details"]))
        self.assertEqual(self.page(["read", "toggle %d" % [r["id"] for r in rows].index("changed"), "line"])[1:], ["RESULT 3", "LINE gone"])
        self.assertEqual(self.page(["read", "toggle 0", "line", "order"])[1:3], ["RESULT 0", "LINE -"])
        self.assertNotIn("gone", self.page(["read", "toggle 0", "order"])[2], "an entry that only the line named is gone once it is off")

    def test_the_validator_refuses_the_same_mods_in_the_same_words(self):
        self.damaged()
        self.h.line()
        _, rows = self.rows()
        validator = mods_tests.driver(ce_version=VERSION)
        for row in rows:
            if row["id"] == "-" or row["why"] == "LINK":
                continue		# no id to name on the command line; the validator opens what a link names no more than the loader does
            status, out, _ = self.h.check(row["id"], exe=validator)
            self.assertEqual(status, 0 if row["why"] == "NONE" else 1, (row["id"], out))
            if row["why"] != "NONE":
                self.assertTrue(any(row["detail"][:200] in line for line in out), (row["id"], row["detail"], out))

    def test_a_renamed_folder_whose_name_the_face_cannot_show_is_not_named(self):
        self.h.make("copy", {SND: b"s"})
        self.h.edit("copy", lambda m: m.update(id="shiny_hull"))
        _, rows = self.rows()
        self.assertEqual((rows[0]["why"], rows[0]["sentence"]), ("RENAMED", "This mod's folder has the wrong name. Unpack the mod again and keep the folder's name."))

    def test_the_kinds_are_the_headers(self):
        names = re.findall(r"^\tPORT_MOD_WHY_(\w+)", MODS_H.read_text(), re.M)
        self.assertEqual(tuple(n for n in names if n != "COUNT"), WHY)
        self.assertEqual(len(re.findall(r'^\t"', kinds_of_the_model(), re.M)), len(WHY), "a sentence for every kind, in the header's order")

    # ---- the one write -------------------------------------------------------------------------------------------------------------
    def test_turning_on_and_off_writes_exactly_the_line(self):
        self.three()
        self.h.line("night-ops")
        # the mods that are on are the top of the list in load order, the others follow by name: a mod turned on joins the end of the first
        # group, a mod turned off goes to its place by name (the list is as a fresh read of the line would show it)
        answers = self.page(["read", "note", "toggle 2", "order", "line", "note", "toggle 0", "order", "line", "toggle 0", "line", "note", "toggle 0", "line"])
        self.assertEqual(answers[1:], ["NOTE -", "RESULT 0", "ORDER night-ops:On,red-engine:On,arena:Off", "LINE night-ops,red-engine",
                                       "NOTE Applies the next time you start the game.",
                                       "RESULT 0", "ORDER red-engine:On,arena:Off,night-ops:Off", "LINE red-engine", "RESULT 0", "LINE -",
                                       "NOTE Applies the next time you start the game.", "RESULT 0", "LINE arena"])
        self.assertEqual(self.h.ini(), "record_sessions=0\nupdate_check=0\nmods=arena\n", "the other lines are kept as they were")

    def test_no_mod_on_is_an_empty_line_that_every_reader_takes_as_none(self):
        self.three()
        self.h.line("night-ops")
        self.page(["read", "toggle 0"])
        self.assertEqual(self.h.ini(), "record_sessions=0\nupdate_check=0\nmods=\n")
        fields, rows = self.rows()
        self.assertEqual((fields["loaded"], [r["state"] for r in rows]), ("-", ["Off", "Off", "Off"]))
        status, out, _ = self.h.check(exe=mods_tests.driver(ce_version=VERSION))
        self.assertEqual((status, out), (0, ["no mods enabled: ce.ini has no usable mods= line", "result: 0 passed, 0 refused"]))

    def test_turning_back_what_was_changed_ends_the_note(self):
        self.three()
        self.h.line("night-ops", "arena")
        answers = self.page(["read", "toggle 1", "note", "toggle 1", "note", "line"])
        self.assertEqual(answers[2:], ["NOTE Applies the next time you start the game.", "RESULT 0", "NOTE -", "LINE night-ops,arena"])

    def test_moving_changes_the_order_the_line_holds(self):
        self.three()
        self.h.line("night-ops", "arena", "red-engine")
        answers = self.page(["read", "move 2 -1", "line", "row 1", "move 1 -1", "line", "move 0 -1", "move 2 1", "line", "move 0 1", "line", "note"])
        self.assertEqual(answers[1:3], ["RESULT 0", "LINE night-ops,red-engine,arena"])
        self.assertTrue(answers[3].startswith("ROW red-engine|"), "the mod is now the row above")
        self.assertEqual(answers[4:], ["RESULT 0", "LINE red-engine,night-ops,arena", "RESULT 3", "RESULT 3", "LINE red-engine,night-ops,arena", "RESULT 0",
                                       "LINE night-ops,red-engine,arena", "NOTE Applies the next time you start the game."])

    def test_the_highlight_goes_with_the_mod_that_was_moved_or_turned(self):
        # the page puts the highlight on mAt: the row the changed mod is in afterwards, so that it can be changed again
        self.three()
        self.h.line("night-ops", "arena", "red-engine")
        answers = self.page(["read", "move 2 -1", "at", "move 1 -1", "at", "move 0 1", "at", "toggle 0", "order", "at", "toggle 2", "order", "at"])
        self.assertEqual(answers[1:], ["RESULT 0", "AT 1", "RESULT 0", "AT 0", "RESULT 0", "AT 1", "RESULT 0", "ORDER red-engine:On,arena:On,night-ops:Off", "AT 2",
                                       "RESULT 0", "ORDER red-engine:On,arena:On,night-ops:On", "AT 2"])

    def test_moving_past_a_mod_that_is_off_writes_nothing(self):
        self.three()
        self.h.line("night-ops")
        before = self.h.tree()
        self.assertEqual(self.page(["read", "move 0 1", "row 0", "note"])[1:], ["RESULT 3", "ROW night-ops|Night Ops|On|0|Turn Off,Details|1", "NOTE -"],
                         "a mod moves only past another that is on: below it the mods that are off are in their own order")
        self.assertEqual(self.h.tree(), before, "nothing was written")

    def test_a_write_that_fails_changes_nothing_and_says_so(self):
        self.three()
        self.h.line("night-ops", "arena")
        os.chmod(self.h.user, 0o555)
        try:
            answers = self.page(["read", "toggle 2", "row 2", "toggle 0", "row 0", "move 1 -1", "row 0", "line", "note"])
        finally:
            os.chmod(self.h.user, 0o755)
        self.assertEqual([a for a in answers if a.startswith("RESULT")], ["RESULT 2"] * 3, "each change answers that it was not saved")
        self.assertTrue(answers[2].startswith("ROW red-engine|Red Engine|Off|"), answers[2])
        self.assertTrue(answers[4].startswith("ROW night-ops|Night Ops|On|"), answers[4])
        self.assertTrue(answers[6].startswith("ROW night-ops|"), "the order is as it was")
        self.assertEqual(answers[7:], ["LINE night-ops,arena", "NOTE -"])
        self.assertIn("Your settings could not be saved, so this was not changed.", sentences(self.exe))

    def test_sixteen_mods_at_most(self):
        ids = [f"m{i:02d}" for i in range(18)]
        for mod_id in ids:
            self.h.make(mod_id, {SND: mod_id.encode()})
        self.h.line(*ids[:15])
        answers = self.page(["read", "toggle 15", "toggle 16", "line", "toggle 0", "toggle 16", "line"])
        self.assertEqual(answers[1:3], ["RESULT 0", "RESULT 1"], "the sixteenth turns on, the seventeenth is refused")
        self.assertEqual(answers[3], "LINE " + ",".join(ids[:16]))
        self.assertEqual(answers[4:6], ["RESULT 0", "RESULT 0"])
        self.assertEqual(answers[6], "LINE " + ",".join(ids[1:17]))
        self.assertIn("Only 16 mods can be on at once. Turn one off first.", sentences(self.exe))

    def test_the_line_writer_takes_only_mod_ids(self):
        source = MODS_CPP.read_text()
        body = source[source.index("bool PortModsWriteLine("):source.index("#if defined(_WIN32) && defined(CE_VERSION)")]
        for guard in ("if (count < 0 || count > PORT_MODS_ON)", "if (!ValidId(id) || DeviceName(id))", "if (id == ids[j])"):
            self.assertIn(guard, body)
        # and by itself: what it refuses leaves the line as it was
        ids = ["m%02d" % n for n in range(1, 18)]
        answers = self.page(["write " + " ".join(ids[:16]), "write " + " ".join(ids), "line", "write m01 m01", "write m01 Bad", "write", "line"])
        self.assertEqual(answers[:3], ["WROTE 1", "WROTE 0", "LINE " + ",".join(ids[:16])])
        self.assertEqual(answers[3:], ["WROTE 0", "WROTE 0", "WROTE 1", "LINE -"])
        # and through the page: an entry with no id (a folder that cannot be a mod) has nothing to turn on
        (self.h.user / "mods/Not A Mod").mkdir(parents=True)
        self.assertEqual(self.page(["read", "toggle 0", "line"])[1:], ["RESULT 3", "LINE -"])

    # ---- a line edited by hand ---------------------------------------------------------------------------------------------------------
    def test_a_line_edited_by_hand_never_locks_the_page(self):
        # the way to choose mods before the page: a capital, a device name, a name twice, and a name with no folder, beside real mods
        self.h.make("plain-a", {SND: b"a"}, name="Plain A")
        self.h.make("plain-b", {SND: b"b"}, name="Plain B")
        self.h.line("Plain-B", "plain-a", "plain-a", "con", "ghost")
        fields, rows = self.rows()
        self.assertEqual([(r["id"], r["state"]) for r in rows], [("-", "Can't Load"), ("plain-a", "On"), ("-", "Can't Load"), ("ghost", "Can't Load"), ("plain-b", "Off")])
        self.assertEqual([r["why"] for r in rows[:4]], ["MISSING", "NONE", "MISSING", "MISSING"], "an entry of the line with no folder is a mod that is no longer there, whatever its name")
        self.assertEqual(rows[0]["actions"], ["Turn Off", "Details"], "and it can be taken off the line")
        # turning a real mod off writes a line the writer takes (the entries it refuses are taken off), and the entries go from the list
        answers = self.page(["read", "toggle 1", "line", "order"])
        self.assertEqual(answers[1:], ["RESULT 0", "LINE ghost", "ORDER ghost:Can't Load,plain-a:Off,plain-b:Off"], "the line is written, not refused")
        answers = self.page(["read", "toggle 0", "line", "order", "toggle 0", "line"])
        self.assertEqual(answers[1:], ["RESULT 0", "LINE -", "ORDER plain-a:Off,plain-b:Off", "RESULT 0", "LINE plain-a"], "taking the entry with no folder off; then a mod on")
        # turning one on, too
        self.h.line("Plain-B", "con")
        answers = self.page(["read", "toggle 2", "line", "order"])
        self.assertEqual(answers[1:], ["RESULT 0", "LINE plain-a", "ORDER plain-a:On,plain-b:Off"])

    def test_more_than_sixteen_on_by_hand_is_cut_to_the_sixteen_the_loader_takes(self):
        ids = [f"m{i:02d}" for i in range(18)]
        for mod_id in ids:
            self.h.make(mod_id, {SND: mod_id.encode()})
        self.h.line(*ids)
        answers = self.page(["read", "toggle 5", "line", "order"])
        want = [i for i in ids if i != "m05"][:16]
        self.assertEqual(answers[1], "RESULT 0")
        self.assertEqual(answers[2], "LINE " + ",".join(want), "the first sixteen that stay on are written; the loader refused the rest too")
        self.assertEqual(answers[3].count(":On"), 16)

    def test_the_list_is_always_what_reading_the_line_would_show(self):
        # any run of turnings and moves leaves the list in the order a fresh read of the line it wrote gives: the mods that are on in load
        # order, then the others by name (a capital and an unusable name among them)
        for name in ("zeta", "alpha", "mid-1", "mid-2", "omega"):
            self.h.make(name, {SND: name.encode()})
        (self.h.user / "mods/Capital Name").mkdir()
        self.h.line("mid-2", "alpha")
        import random
        rng = random.Random(20261112)
        for run in range(6):
            commands = ["read"]
            for _ in range(12):
                commands.append(rng.choice(["toggle %d", "move %d -1", "move %d 1"]) % rng.randrange(6))
            commands.append("order")
            done = self.page(commands)
            _, rows = self.rows()
            fresh = ",".join(f"{r['id'] if r['id'] != '-' else r['name'].replace(' ', '')}:{r['state']}" for r in rows)
            held = done[-1][6:].replace("Capital Name", "CapitalName")
            self.assertEqual(held, fresh, (run, commands))

    def test_turning_a_mod_that_cannot_load_off_is_a_change_that_waits_for_the_next_start(self):
        self.damaged()
        self.h.line("changed", "fine")
        self.assertEqual(self.page(["read", "note"])[1], "NOTE -", "the page opened on what this start refused: nothing is waiting")
        self.assertEqual(self.page(["read", "toggle 0", "note"])[1:], ["RESULT 0", "NOTE Applies the next time you start the game."],
                         "the damaged mod is off the line: the next start will not refuse it")

    def test_what_a_mod_says_of_itself_is_cut_for_the_box_at_the_end_of_a_word(self):
        self.h.make("wordy", {SND: b"w"}, name="Wordy", description=" ".join(f"word{i:03d}" for i in range(130)))
        answers = self.page(["read", "details 0", "details 0 60"])
        full, short = answers[1][8:], answers[2][8:]
        self.assertLessEqual(len(full), 40 + 400 + 3 + 60, full)
        self.assertTrue(full.split("\\n\\n")[1].endswith("..."), full)
        self.assertNotIn("word129", full)
        self.assertTrue(short.split("\\n\\n")[1].endswith("word006..."), short)
        # a description that fits is whole
        self.h.make("brief", {SND: b"b"}, name="Brief", description="Short and complete.")
        self.assertIn("Short and complete.\\n\\n", self.page(["read", "details 0"])[1])

    # ---- the page's drawing and input (ce_modspage.cpp: game types, so read, not run) ------------------------------------------------
    def test_every_way_out_or_on_of_the_page_makes_the_games_sound(self):
        code = (PORT / "game/ce_modspage.cpp").read_text()

        def body(start, end):
            return code[code.index(start):code.index(end, code.index(start))]
        self.assertIn("PlaySound(FES_BACK)", body("void\tClose()", "One Select reaches"), "leaving the page")
        pause = body("case BUTTON_PAUSE_MENU_SELECT:", "case BUTTON_FRONTEND_MENU_BACK:")
        self.assertEqual(pause.count("PlaySound(FES_SELECT)"), 2, "More Mods and Open Mods Folder each make the select sound")
        self.assertIn("Close()", pause, "Back")
        self.assertIn("PlaySound(FES_SELECT)", body("if (button == BUTTON_FRONTEND_MENU_SELECT)\n\t\t\t{", "if (count < 2)\n\t\t\t\treturn;"), "a row's own select")
        self.assertIn("case BUTTON_FRONTEND_MENU_BACK:\n\t\t\tClose();", code, "the page's Back key leaves it")

    def test_the_open_list_keeps_its_label_steady_and_a_choice_a_change(self):
        code = (PORT / "game/ce_modspage.cpp").read_text()
        # the item's pulse (value != highlighted line) is held off while the list is open, and the value is nothing again before any choice is read
        self.assertIn("virtual int\t\tGetValue() { return mExpanded ? mSelection : -1; }", code)
        self.assertIn("if (n == 0 && mExpanded)\n\t\t\tmValue = -1;", code)
        self.assertIn("\t\tmValue = -1;\n\t\tif (!mExpanded)", code, "and whenever a key reaches the row")

    # ---- two mods on one file ------------------------------------------------------------------------------------------------------
    def test_two_mods_on_one_file_say_which_wins(self):
        self.three()
        self.h.line("red-engine", "night-ops", "arena")
        _, rows = self.rows()
        self.assertEqual(rows[0]["clash"], "Loses a file to Night Ops.")
        self.assertEqual(rows[1]["clash"], "Wins a file over Red Engine.")
        self.assertEqual(rows[2]["clash"], "")
        self.assertEqual(rows[0]["longer"], "Night Ops changes the same file, and wins.")
        self.assertEqual(rows[1]["longer"], "This mod and Red Engine change the same file.\\nThis mod wins.")
        # the loader agrees: the later mod's copy is what the game is given
        out, _ = self.h.run(["open " + TEX], exe=mods_tests.driver(ce_version=VERSION))
        self.assertEqual(out[0], mods_tests.digest(mods.pack(mods_tests.dds(16, 16))))
        # moved above, the other wins; turned off, nothing is shared
        answers = self.page(["read", "move 1 -1", "hint 0", "hint 1", "toggle 0", "hint 0"])
        self.assertIn("|Loses a file to Red Engine.|", answers[2])
        self.assertIn("|Wins a file over Night Ops.|", answers[3])
        self.assertTrue(answers[5].endswith("|By Kite. Version 1.2.|||"), answers[5])

    def test_three_mods_on_one_file_and_two_on_another(self):
        self.h.make("a-mod", {TEX: self.tex, SND: b"a", OTHER: b"a"}, name="Alpha")
        self.h.make("b-mod", {TEX: mods.pack(mods_tests.dds(16, 16)), SND: b"b"}, name="Bravo")
        self.h.make("c-mod", {TEX: mods.pack(mods_tests.dds(32, 32))}, name="Charlie")
        self.h.line("a-mod", "b-mod", "c-mod")
        _, rows = self.rows()
        self.assertEqual(rows[0]["clash"], "Shares 2 files with 2 other mods.")
        self.assertEqual(rows[1]["clash"], "Shares 2 files with 2 other mods.")
        self.assertEqual(rows[2]["clash"], "Wins a file over 2 other mods.")
        self.assertEqual(rows[0]["longer"], "2 other mods change some of the same files. The lowest of them in the list wins each file.")
        self.assertEqual(rows[2]["longer"], "2 other mods change the same file.\\nThis mod wins.")
        self.h.line("c-mod", "a-mod", "b-mod")
        _, rows = self.rows()
        self.assertEqual(rows[0]["clash"], "Loses a file to Bravo.")
        self.assertEqual(rows[1]["clash"], "Loses 2 files to Bravo.")
        self.assertEqual(rows[2]["clash"], "Wins 2 files over 2 other mods.")
        self.assertEqual(rows[0]["longer"], "Bravo wins the file this mod shares with 2 other mods.")
        self.assertEqual(rows[1]["longer"], "Bravo wins every file this mod shares with 2 other mods.")

    def test_a_mod_that_is_off_or_cannot_load_shares_nothing(self):
        self.three()
        (self.h.user / "mods/night-ops" / SND).write_bytes(b"changed after")
        self.h.line("red-engine", "night-ops")
        _, rows = self.rows()
        self.assertEqual([(r["state"], r["clash"]) for r in rows[:2]], [("On", ""), ("Can't Load", "")])

    # ---- the fence and the details ---------------------------------------------------------------------------------------------------
    def test_a_fenced_run_loads_none_and_says_so(self):
        self.three()
        self.h.line("night-ops")
        fields, rows = self.rows(env={"BEA_PORT_TAPE": "1"})
        self.assertEqual((fields["off"], fields["loaded"], rows[0]["state"]), ("1", "-", "On"))
        self.assertEqual(self.page(["read", "note"], env={"BEA_PORT_TAPE": "1"})[1], "NOTE Mods are off in recordings, replays and network games. None is loaded now.")

    def test_details_say_everything_in_order(self):
        self.three()
        self.h.line("red-engine", "night-ops")
        answers = self.page(["read", "details 0", "details 2", "toggle 2", "details 2"])
        # the mod's name is the title bar's while the box is up, so the box starts with who made it
        self.assertEqual(answers[1], "DETAILS By Kite. Version 1.2.\\n\\nPaints the Battle Engine red.\\n\\nReplaces 1 game file.\\n"
                                     "Night Ops changes the same file, and wins.\\n\\nThis mod is on.")
        self.assertEqual(answers[2], "DETAILS Version 0.3.\\n\\nA new arena for two players.\\n\\nAdds 1 level.\\n\\nThis mod is off.")
        self.assertTrue(answers[4].endswith("\\n\\nThis mod is on from the next start."), answers[4])
        (self.h.user / "mods/red-engine" / TEX).write_bytes(b"cut")
        self.assertEqual(self.page(["read", "details 0"])[1], "DETAILS By Kite. Version 1.2.\\n\\nPaints the Battle Engine red.\\n\\n"
                                                             "This mod's files are damaged or were changed. Get the mod again.")

    def test_details_of_a_loaded_mod_say_when_the_next_start_loads_another_order(self):
        self.three()
        self.h.line("night-ops", "arena")
        answers = self.page(["read", "details 0", "move 0 1", "details 1", "details 0"])
        self.assertTrue(answers[1].endswith("\\n\\nThis mod is on."), answers[1])
        self.assertTrue(answers[3].endswith("\\n\\nThis mod is on from the next start."), "night-ops is where it was but later in the order: " + answers[3])

    def test_a_long_name_is_cut_where_a_word_ends_and_a_sentence_never_ends_on_it(self):
        self.h.make("aaa", {TEX: self.tex}, name="The Complete Forseti Navy Livery Collection")
        self.h.make("bbb", {TEX: self.tex}, name="Bee")
        self.h.line("aaa", "bbb")
        answers = self.page(["read", "hint 1", "details 1"])
        self.assertIn("|Wins a file over The Complete Forseti...|", answers[1], "the sentence ends on the cut name's dots, with no fourth")
        self.assertIn("This mod and The Complete Forseti... change the same file.\\nThis mod wins.", answers[2])
        self.assertNotIn("Navy", answers[1] + answers[2])
        for answer in answers:
            for bad in ("....", ",...", "(..."):
                self.assertNotIn(bad, answer)

    def test_a_cut_ends_where_a_word_ends(self):
        cases = [(22, "Flagship duel (level 888, the long one)", "Flagship duel..."), (22, "CE test: recoloured battle engine", "CE test: recoloured..."),
                 (22, "Short", "Short"), (10, "Extraordinarily", "Extraordin..."), (22, "The Complete Forseti Navy Livery Collection", "The Complete Forseti..."),
                 (12, "Night Paint: Dark - Edition", "Night Paint..."), (5, "Ab cd", "Ab cd")]
        answers = self.page(["cut %d %s" % (n, text) for n, text, _ in cases])
        for (n, text, want), answer in zip(cases, answers):
            self.assertEqual(answer, "CUT " + want, (n, text))

    # ---- text from strangers -------------------------------------------------------------------------------------------------------
    def test_text_is_kept_to_the_faces_characters(self):
        cases = {"plain": "plain", "under_score_name": "under score name", "\u201cQuoted\u201d \u2014 it\u2019s\u2026": "\"Quoted\" - it's...",
                 "a\tb   c": "a b c", "caf\u00e9 [HD] {2x}": "caf\u00e9 (HD) (2x)", "C:\\path|x~y`z": "C:/path/x-y'z", "price $5 @home ^": "price ?5 ?home ?",
                 "\u041c\u043e\u0434 one": "? one", "\U0001f680\U0001f680 rockets": "? rockets", "  spaced  ": "spaced",
                 "a" * 60: "a" * 28 + " " + "a" * 28 + " aaaa"}		# no word wider than a line reaches the game's word wrap
        answers = self.page(["text " + key for key in cases])
        for key, answer in zip(cases, answers):
            self.assertEqual(answer, "TEXT " + cases[key], key)
        for answer in answers:
            self.assertFalse(set(answer[5:]) & set(NOT_IN_FACE), answer)

    def test_a_strangers_manifest_reaches_the_page_only_in_the_faces_characters(self):
        self.h.make("odd", {SND: b"s"}, name="Mod_[One] {\u2605}", author="A|B", version="v~1", description="Line one\nline_two \u2014 ok\tend $")
        self.h.make("mixname", {SND: b"u"}, name="\u0424a\u0424b\u0424")
        self.h.make("cyrillic", {SND: b"t"}, name="\u041c\u043e\u0434\u0438\u0444\u0438\u043a\u0430\u0446\u0438\u044f", description="\u041e\u043f\u0438\u0441\u0430\u043d\u0438\u0435")
        _, rows = self.rows()
        by_id = {r["id"]: r for r in rows}
        self.assertEqual(by_id["odd"]["name"], "Mod (One) (?)")
        self.assertEqual(by_id["odd"]["by"], "By A/B. Version v-1.")
        self.assertEqual(by_id["odd"]["description"], "Line one line two - ok end ?")
        self.assertEqual(by_id["mixname"]["name"], "mixname", "a name that is mostly marks is the folder's name")
        self.assertEqual(by_id["cyrillic"]["name"], "cyrillic", "a name in a script the face lacks is shown as the folder's name")
        self.assertEqual(by_id["cyrillic"]["description"], "", "a text with nothing in it but marks says nothing")
        for row in rows:
            for field in ("name", "by", "description"):
                self.assertFalse(set(row[field]) & set(NOT_IN_FACE), row)

    def test_letters_the_face_has_survive_the_manifests_escapes(self):
        # mods.py writes mod.json with \u escapes: the page's texts are taken from a second parse that keeps them (the checks' own parse turns them into '?')
        self.h.make("accents", {SND: b"s"}, name="Caf\u00e9 \u2605 \U0001f680 \ud83d x", author="Ren\u00e9e", description="d\u00e9j\u00e0 vu")
        _, rows = self.rows()
        by_id = {r["id"]: r for r in rows}
        self.assertEqual(by_id["accents"]["name"], "Caf\u00e9 ? ? ? x")
        self.assertEqual(by_id["accents"]["by"], "By Ren\u00e9e. Version 1.0.")
        self.assertEqual(by_id["accents"]["description"], "d\u00e9j\u00e0 vu")
        self.assertEqual(self.page(["read", "row 0"])[1].split("|")[2], "Off", "a mod with accents in its texts still passes its checks")

    def test_no_sentence_breaks_the_rules_for_player_text(self):
        said = sentences(self.exe)
        self.assertGreater(len(said), 20)
        for text in said:
            text = text.replace("\\n", " ")		# the driver shows a line break as \n
            for word in FORBIDDEN:
                self.assertNotIn(word, text, text)
            self.assertFalse(set(text) & set(NOT_IN_FACE), text)
            self.assertTrue(all(32 <= ord(c) < 127 for c in text), text)
            self.assertNotIn("%", text, "a sentence with a gap in it is filled before it is shown")
            if " " in text and len(text) > 12 and text != "Open Mods Folder":
                self.assertTrue(text.endswith("."), "a sentence ends with a full stop: " + text)
                self.assertLessEqual(len(text), 110, "one or two short sentences: " + text)


# ---- mutants: each must make its named test fail -------------------------------------------------------------------------------------
MODS_MUTANTS = [
    ("an unmounted package offers its added level", "if (owner && name == owner)", "if (!owner || name == owner)",
     "test_a_level_a_mod_adds_can_be_played_only_while_the_game_has_the_mod"),
    ("a losing package also offers the shared level", "if (owner && name == owner)", "if (owner)",
     "test_only_the_package_serving_a_level_can_play_it_or_receive_its_return"),
    ("the folder's other mods are not listed", "\tfor (const Listed &entry : folder)\n\t{\n\t\tif (std::find(taken.begin(), taken.end(), entry.name) != taken.end())\n\t\t\tcontinue;",
     "\tfor (const Listed &entry : folder)\n\t{\n\t\tcontinue;", "test_every_folder_is_a_row_the_lines_mods_first_in_its_order"),
    ("a mod the line names twice is two rows", "\t\tif (std::find(taken.begin(), taken.end(), id) != taken.end())\n\t\t\tcontinue;\t// a second mention", "\t\tif (false)\n\t\t\tcontinue;\t// a second mention",
     "test_a_line_that_names_a_mod_twice_has_one_row"),
    ("a link is opened as a folder", "\telse if (kind == 1)\n\t{\n\t\trow->why = PORT_MOD_WHY_LINK;", "\telse if (false)\n\t{\n\t\trow->why = PORT_MOD_WHY_LINK;",
     "test_every_kind_of_refusal_is_reached_and_has_its_sentence"),
    ("a packed download is not a row", "\t\telse if (S_ISREG(st.st_mode) && ArchiveFileName(name))", "\t\telse if (S_ISREG(st.st_mode) && ArchiveFileName(name) && false)", "test_every_kind_of_refusal_is_reached_and_has_its_sentence"),
    ("a damaged file has the missing file's kind", "\tgWhy = PORT_MOD_WHY_DAMAGED;\t// not the bytes the manifest lists\n", "", "test_every_kind_of_refusal_is_reached_and_has_its_sentence"),
    ("a file the game lacks has the list's kind", "\tgWhy = PORT_MOD_WHY_NOT_IN_GAME;\n", "", "test_every_kind_of_refusal_is_reached_and_has_its_sentence"),
    ("an unreadable texture has the damaged kind", "\tgWhy = PORT_MOD_WHY_FORMAT;\t// the bytes the author listed", "\t// the bytes the author listed", "test_every_kind_of_refusal_is_reached_and_has_its_sentence"),
    ("an older mod has the newer kind", "gWhy = order < 0 ? PORT_MOD_WHY_NEWER : PORT_MOD_WHY_OLDER;", "gWhy = PORT_MOD_WHY_NEWER;", "test_every_kind_of_refusal_is_reached_and_has_its_sentence"),
    ("a renamed folder is not told its name", "\t\t\tgWanted = jid->s;\n", "", "test_every_kind_of_refusal_is_reached_and_has_its_sentence"),
    ("a refused mod's texts are not kept", "\t\t\t\t\tif (v->type == Json::STR && v->s.size() <= 4096)\n\t\t\t\t\t\tmod->shown[i] = v->s;", "\t\t\t\t\tif (false)\n\t\t\t\t\t\tmod->shown[i] = v->s;",
     "test_every_kind_of_refusal_is_reached_and_has_its_sentence"),
    ("the earlier mod wins", "\t\t\tif (i > index)\n\t\t\t\tlast = i;", "\t\t\tif (i < index)\n\t\t\t\tlast = i;", "test_two_mods_on_one_file_say_which_wins"),
    ("a third mod's win is the second's", "\tif (winners.size() == 1 && out->lost == out->shared)", "\tif (!winners.empty())", "test_three_mods_on_one_file_and_two_on_another"),
    ("more than sixteen ids are written", "\tif (count < 0 || count > PORT_MODS_ON)\n\t\treturn false;", "\tif (count < 0)\n\t\treturn false;", "test_the_line_writer_takes_only_mod_ids"),
    ("no mod on writes no line", "(line.empty() ? CE_SettingSetEmpty(\"mods\") : CE_SettingSetText(\"mods\", line.c_str())) != 0", "(line.empty() ? 1 : CE_SettingSetText(\"mods\", line.c_str())) != 0",
     "test_no_mod_on_is_an_empty_line_that_every_reader_takes_as_none"),
    ("the loaded mods are not reported", "\t\tsnprintf(local.loaded[local.loaded_count++], sizeof local.loaded[0], \"%s\", loaded.id.c_str());", "\t\t(void)loaded;",
     "test_turning_back_what_was_changed_ends_the_note"),
    ("an escaped letter is a placeholder for the page too", "\t\t\t\tif (v >= 0x80 && utf8)", "\t\t\t\tif (false && utf8)", "test_letters_the_face_has_survive_the_manifests_escapes"),
    ("a name on the line with no folder is a name problem", "\telse if (kind < 0)\n\t{\n\t\t// ce.ini's line names it", "\telse if (false)\n\t{\n\t\t// ce.ini's line names it",
     "test_a_line_edited_by_hand_never_locks_the_page"),
    ("a fenced run is not reported", "\tlocal.off = gFence ? 1 : PlatformOff() ? 2 : 0;", "\tlocal.off = PlatformOff() ? 2 : 0;", "test_a_fenced_run_loads_none_and_says_so"),
]
CE_MUTANTS = [
    ("a file named mods is a mods folder too", "\treturn (mkdir(dir, 0700) == 0 || errno == EEXIST) && lstat(dir, &st) == 0 && S_ISDIR(st.st_mode);", "\treturn (mkdir(dir, 0700) == 0 || errno == EEXIST);",
     "test_the_mods_folder_is_made_when_missing_and_a_file_or_link_named_mods_is_not_one"),
]
MODEL_MUTANTS = [
    ("return focus forgets the mounted provider", "return id ? RowOf(id) : -1;", "return id ? 0 : -1;",
     "test_only_the_package_serving_a_level_can_play_it_or_receive_its_return"),
    ("a change is not written", "\t\tif (!PortModsWriteLine(ids, n))\n\t\t\treturn false;\n\t\tint kept = 0;", "\t\tif (ids[0] == (const char *)1)\n\t\t\treturn false;\n\t\tint kept = 0;",
     "test_turning_on_and_off_writes_exactly_the_line"),
    ("a failed write is not told", "\t\tif (!PortModsWriteLine(ids, n))\n\t\t\treturn false;\n\t\tint kept = 0;", "\t\t(void)PortModsWriteLine(ids, n);\n\t\tint kept = 0;",
     "test_a_write_that_fails_changes_nothing_and_says_so"),
    ("a failed write still changes the list", "\t\tif (!PortModsWriteLine(ids, n))\n\t\t\treturn false;\n\t\tint kept = 0;",
     "\t\tmemcpy(mOn, state, sizeof mOn);\n\t\tif (!PortModsWriteLine(ids, n))\n\t\t\treturn false;\n\t\tint kept = 0;", "test_a_write_that_fails_changes_nothing_and_says_so"),
    ("a seventeenth mod turns on", "\t\tif (turn_on && OnCount() >= PORT_MODS_ON)", "\t\tif (false)", "test_sixteen_mods_at_most"),
    ("a refused mod can be turned on", "return On(row) || (Mod(row).id[0] && CanLoad(row));", "return Mod(row).id[0] != 0;", "test_a_refused_mod_can_be_turned_off_but_not_on"),
    ("a move does not reorder", "\t\torder[row] = mOrder[other];\n\t\torder[other] = mOrder[row];\n", "", "test_moving_changes_the_order_the_line_holds"),
    ("after a move the highlight lands on the other mod", "\t\tint at = mOrder[row];\n\t\tif (!Settle(order, mOn, mCount, &at))", "\t\tint at = mOrder[other];\n\t\tif (!Settle(order, mOn, mCount, &at))",
     "test_the_highlight_goes_with_the_mod_that_was_moved_or_turned"),
    ("a mod turned on joins the top of the mods that are on", "\t\t\tfor (int i = 0; i < mCount; i++)\t\t// the end of the mods that are on\n\t\t\t\tif (i != row && mOn[mOrder[i]])\n\t\t\t\t\torder[n++] = mOrder[i];\n\t\t\torder[n++] = index;",
     "\t\t\torder[n++] = index;\n\t\t\tfor (int i = 0; i < mCount; i++)\t\t// the end of the mods that are on\n\t\t\t\tif (i != row && mOn[mOrder[i]])\n\t\t\t\t\torder[n++] = mOrder[i];",
     "test_turning_on_and_off_writes_exactly_the_line"),
    ("the last page is not shared out", "\treturn count <= kCeModsSlots ? kCeModsSlots : (count + pages - 1) / pages;", "\treturn count <= kCeModsSlots ? kCeModsSlots : kCeModsSlots - 1 + 0 * pages;",
     "test_pages_share_the_mods_evenly_so_the_last_is_never_a_few_rows"),
    ("a mod that is off offers Play", "if (!On(row) || !CanLoad(row) || mRun.off)", "if (!CanLoad(row) || mRun.off)",
     "test_a_level_a_mod_adds_can_be_played_only_while_the_game_has_the_mod"),
    ("only the first of several levels can be played", "return n < kCeModsPlayLevels ? n : kCeModsPlayLevels;", "return n > 1 ? 1 : n;",
     "test_a_level_a_mod_adds_can_be_played_only_while_the_game_has_the_mod"),
    ("a mod moves past one that is off", "|| !On(row) || !On(other))", "|| !On(row))", "test_moving_past_a_mod_that_is_off_writes_nothing"),
    ("an entry that cannot be written stays on", "if (state[index] && (!mMods[index].id[0] || written >= PORT_MODS_ON))", "if (state[index] && written >= PORT_MODS_ON)",
     "test_a_line_edited_by_hand_never_locks_the_page"),
    ("more than sixteen stay on", "written >= PORT_MODS_ON))", "written < 0))", "test_more_than_sixteen_on_by_hand_is_cut_to_the_sixteen_the_loader_takes"),
    ("an entry only the line named stays", "\t\t\tif (mMods[rest[i]].why != PORT_MOD_WHY_MISSING)", "\t\t\tif (true)", "test_a_line_edited_by_hand_never_locks_the_page"),
    ("the mods that are off are not in name order", "for (; j > 0 && Before(index, rest[j - 1]); j--)", "for (; j > 0 && false; j--)", "test_turning_on_and_off_writes_exactly_the_line"),
    ("the note never ends", "\t\tif (n != mRun.loaded_count)\n\t\t\treturn true;", "\t\treturn true;", "test_turning_back_what_was_changed_ends_the_note"),
    ("a changed order needs no restart", "\t\t\tif (strcmp(ids[i], mRun.loaded[i]))\n\t\t\t\treturn true;", "\t\t\tif (false)\n\t\t\t\treturn true;", "test_moving_changes_the_order_the_line_holds"),
    ("a damaged mod taken off needs no restart", "\t\treturn mBase >= 0 && Unloadable() != mBase;", "\t\treturn false;",
     "test_turning_a_mod_that_cannot_load_off_is_a_change_that_waits_for_the_next_start"),
    ("an underscore reaches the page", "case 0x00a0: case 0x2002: case 0x2003: case 0x2009: case '_': case '\\t':", "case 0x00a0: case 0x2002: case 0x2003: case 0x2009: case '\\t':",
     "test_text_is_kept_to_the_faces_characters"),
    ("a name in another script is question marks", "\tif (!length || 2 * marks > length)", "\tif (!length || marks < 0)", "test_a_strangers_manifest_reaches_the_page_only_in_the_faces_characters"),
    ("a text of marks says something", "\tif (!word)\n\t\tout[0] = 0;", "\t(void)word;", "test_a_strangers_manifest_reaches_the_page_only_in_the_faces_characters"),
    ("a renamed folder's name is shown though the face lacks it", "\t\t\t\tif (!shown((unsigned char)*c))\n\t\t\t\t\twhole = false;", "\t\t\t\tif (!shown((unsigned char)*c) && false)\n\t\t\t\t\twhole = false;", "test_a_renamed_folder_whose_name_the_face_cannot_show_is_not_named"),
    ("a fenced run has the restart note", "\t\tif (mRun.off == 1)\n\t\t\treturn kCeModsOff;", "", "test_a_fenced_run_loads_none_and_says_so"),
    ("a loaded mod is on in Details whatever the next start loads", "Mod(row).loaded && !Pending() ? \"This mod is on.\"", "Mod(row).loaded ? \"This mod is on.\"",
     "test_details_of_a_loaded_mod_say_when_the_next_start_loads_another_order"),
    ("a cut ends inside a word", "\twhile (cut > 0 && text[cut] != ' ')\n\t\tcut--;", "\t(void)cut;", "test_a_cut_ends_where_a_word_ends"),
    ("a cut keeps an open bracket's tail", "\t\t\tcut = i;\n\t\t\tbreak;", "\t\t\tbreak;", "test_a_cut_ends_where_a_word_ends"),
    ("a cut ends on a comma or a colon", "while (cut > 0 && strchr(\" ,;:-/(\", text[cut - 1]))", "while (cut > 0 && text[cut - 1] == ' ')", "test_a_cut_ends_where_a_word_ends"),
    ("a sentence ends on a cut name with a fourth dot", "(length < 3 || strcmp(text + length - 3, \"...\"))", "true", "test_a_long_name_is_cut_where_a_word_ends_and_a_sentence_never_ends_on_it"),
    ("the box shows all a mod says of itself", "\t\tCeModCut(cut, sizeof cut, text, chars);", "\t\tsnprintf(cut, sizeof cut, \"%s\", text); (void)chars;",
     "test_what_a_mod_says_of_itself_is_cut_for_the_box_at_the_end_of_a_word"),
]


def run_mutants():
    failures = 0
    for which, table in (("mods.cpp", MODS_MUTANTS), ("ce.cpp", CE_MUTANTS), ("the model", MODEL_MUTANTS)):
        source = {"mods.cpp": MODS_CPP, "ce.cpp": CE_CPP, "the model": MODEL}[which].read_text()
        for name, old, new, test in table:
            if source.count(old) != 1:
                print(f"mutant {name}: its text is not in {which} exactly once")
                failures += 1
                continue
            try:
                mutated = source.replace(old, new)
                exe = driver(mutated) if which == "mods.cpp" else driver(None, None, mutated) if which == "ce.cpp" else driver(None, mutated)
            except RuntimeError as error:
                print(f"mutant {name}: does not build: {error}")
                failures += 1
                continue
            ModsPageTests.exe = exe
            result = unittest.TextTestRunner(stream=open(os.devnull, "w"), verbosity=0).run(ModsPageTests(test))
            ModsPageTests.exe = None
            caught = not result.wasSuccessful()
            print(f"mutant {name}: {'caught by ' + test if caught else 'NOT CAUGHT by ' + test}")
            failures += not caught
    return failures


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a not in ("--no-mutants", "--mutants-only")]
    status = 0
    if "--mutants-only" not in sys.argv:
        program = unittest.main(argv=[sys.argv[0], *args], exit=False, defaultTest="ModsPageTests")
        status = 0 if program.result.wasSuccessful() else 1
        skipped = len(program.result.skipped)
        print(f"tests: {program.result.testsRun} run, {len(program.result.failures) + len(program.result.errors)} failed, {skipped} skipped")
    if "--no-mutants" not in sys.argv and not args and status == 0:
        failed = run_mutants()
        counted = len(MODS_MUTANTS) + len(CE_MUTANTS) + len(MODEL_MUTANTS)
        print(f"mutants: {counted} run, {failed} not caught or not applied")
        status = status or (1 if failed else 0)
    sys.exit(status)
