#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Service Record's reading of a career (port/game/ce_career_view.h), on careers the game itself wrote.

The header is compiled as the game compiles it into a small driver (tests/ce_extras_driver.cpp) and its reading of each career is compared,
field by field, with a second reader written here from the Onslaught Toolkit's published layout (Careers/CareerSave.cs, CampaignGraph.cs),
and with facts the Toolkit's own tests pin for its one tracked career. The careers are real ones and are only read: every career file in
the game folder's savegames (BEA_GAME_DIR), and the Toolkit's tracked career when that checkout is beside this one (named by path, never
copied here). No career found is a failure, not a pass. Variants (a campaign part-way through, each rank, each Goodie state, a damaged
length or version) are made in memory from a real career and written only into the test's own scratch folder.

Causal mutants of the header (an offset, the open rule, the rank rule, the Goodies wall, the kill mask, the refusals) must each be caught.
No game, display, sound or network is used.
"""
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
DRIVER = ROOT / "port/tools/tests/ce_extras_driver.cpp"
SIZE, VERSION = 10004, 0x4BD1
MISSIONS, MISSION, ROUTES, ROUTE, GOODIES, KILLS = 0x0006, 64, 0x1906, 8, 0x1F46, 0x23F6


def toolkit_career():
    """The Toolkit's tracked career, by path, when that checkout is beside this repository (also from a worktree of it)."""
    for base in (ROOT, *ROOT.parents):
        candidate = base.parent / "Onslaught-Career-Editor/tests_shared/fixtures/gold_career_save.bin"
        if candidate.is_file():
            return candidate
    return None


def u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def rank_letter(bits):
    value = struct.unpack("<f", struct.pack("<I", bits))[0]
    if math.isnan(value) or value == 1.0:
        return "S"
    if value <= 0.0:
        return "E"
    if math.isinf(value) or value > 1.0:
        return "-"
    return chr(ord("D") - int(math.floor(value * 4.0)))


def shown(slot):
    return 0 <= slot < 233 and not 71 <= slot <= 73


# The independently documented fields in the saved career, before a menu load preserves the live mix.
SAVED_SOUND, SAVED_MUSIC = (9358, 9362)
PLAYER_ONE, PLAYER_TWO = (9366, 9370)


def stored_settings(data):
    def percent(at):
        value = struct.unpack_from("<f", data, at)[0]
        return int(value * 100 + 0.5) if 0 <= value <= 1 else -1
    return [percent(SAVED_SOUND), percent(SAVED_MUSIC), int(bool(u32(data, PLAYER_ONE))), int(bool(u32(data, PLAYER_TWO)))]


def read(data):
    """The second reader: the same record from the published layout, or None for bytes that are not a career."""
    if len(data) != SIZE or struct.unpack_from("<H", data, 0)[0] != VERSION:
        return None
    nodes = []
    for n in range(100):
        at = MISSIONS + n * MISSION
        nodes.append(dict(world=u32(data, at + 0x10), complete=u32(data, at + 0x04), lower=struct.unpack_from("<i", data, at + 0x08)[0],
                          higher=struct.unpack_from("<i", data, at + 0x0C)[0], rank=u32(data, at + 0x3C)))
    routes = []
    for n, node in enumerate(nodes):
        if not node["world"]:
            continue
        for link in (node["lower"], node["higher"]):
            if not 0 <= link < 200:
                continue
            state, to = u32(data, ROUTES + link * ROUTE), u32(data, ROUTES + link * ROUTE + 4)
            if to < 100 and nodes[to]["world"]:
                routes.append((n, to, state))
    missions = []
    for n, node in enumerate(nodes):
        if not node["world"]:
            continue
        into = [r for r in routes if r[1] == n]
        if node["complete"]:
            missions.append((node["world"], 0, rank_letter(node["rank"])))
        else:
            missions.append((node["world"], 1 if (not into or any(r[2] == 1 for r in into)) else 2, "-"))
    states = [u32(data, GOODIES + g * 4) for g in range(300)]
    return dict(missions=missions, complete=sum(1 for m in missions if m[1] == 0), rank_s=sum(1 for m in missions if m[2] == "S"),
                found=sum(1 for g, s in enumerate(states) if s in (2, 3) and shown(g)),
                all=sum(1 for s in states if 2 <= s < 0x80000000),
                goodies="".join(str(s) if s <= 3 else "x" for s in states),
                kills=[u32(data, KILLS + k * 4) & 0xFFFFFF for k in range(5)],
                settings=stored_settings(data))

def parse(text):
    """The driver's text as the same record."""
    lines = text.splitlines()
    if lines and lines[0].startswith("refused"):
        return None if lines[0] == "refused 0" else "refused but left a record"
    out = dict(missions=[])
    for line in lines:
        f = line.split()
        if f[0] == "missions":
            out.update(count=int(f[1]), complete=int(f[3]), rank_s=int(f[5]))
        elif f[0] == "m":
            out["missions"].append((int(f[1]), int(f[2]), f[3]))
        elif f[0] == "goodies":
            out.update(found=int(f[1]), all=int(f[3]))
        elif f[0] == "g":
            out["goodies"] = f[1]
        elif f[0] == "shown":
            out["shown"] = f[1]
        elif f[0] == "settings":
            out["settings"] = list(map(int, f[1:]))
        elif f[0] == "kills":
            out["kills"] = [int(x) for x in f[1:]]
    return out


class CareerViewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-career-view-"))
        cls.binary = cls.build(cls.tmp / "driver", GAME)
        cls.careers = []
        game = os.environ.get("BEA_GAME_DIR")
        if game:
            cls.careers += sorted(Path(game).glob("savegames/*.bes"))
        cls.toolkit = toolkit_career()
        if cls.toolkit:
            cls.careers.append(cls.toolkit)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    @staticmethod
    def build(out, include, werror=True):
        done = subprocess.run(["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", *(["-Werror"] if werror else []), f"-I{include}", str(DRIVER), "-o", str(out)],
                              capture_output=True, text=True, timeout=180)
        if done.returncode:
            raise RuntimeError("the driver did not compile:\n" + done.stdout + done.stderr)
        return out

    def view(self, data, binary=None):
        path = self.tmp / "career.bin"
        path.write_bytes(data)
        done = subprocess.run([str(binary or self.binary), "view", str(path)], capture_output=True, text=True, timeout=20)
        self.assertEqual(done.returncode, 0, done.stderr)
        return parse(done.stdout)

    def agree(self, data, binary=None):
        """None when the driver's reading equals the second reader's, else what differs first."""
        mine, theirs = read(data), self.view(data, binary)
        if mine is None or theirs is None or isinstance(theirs, str):
            return None if mine is None and theirs is None else "refusal"
        for key in ("missions", "complete", "rank_s", "found", "all", "goodies", "kills"):
            if mine[key] != theirs[key]:
                return key
        if theirs["count"] != len(mine["missions"]):
            return "count"
        if theirs["shown"] != "".join("1" if shown(g) else "0" for g in range(300)):
            return "shown"
        return None

    def variants(self, data):
        """Careers made in memory from a real one, to reach every state: a campaign part-way through, each rank, each Goodie state, damage."""
        out = {"as written": bytes(data)}
        part = bytearray(data)
        for n in range(100):                      # the campaign up to 2.00 won, 2.11 and 2.12 open, the rest not yet
            at = MISSIONS + n * MISSION
            world = u32(part, at + 0x10)
            if world:
                struct.pack_into("<I", part, at + 0x04, 1 if world <= 200 else 0)
                if world <= 200:                  # a won mission carries a plain rank (0 to 1) whatever career this was made from
                    struct.pack_into("<f", part, at + 0x3C, (1.0, 0.8, 0.6, 0.4, 0.2)[n % 5])
            for link in struct.unpack_from("<ii", part, at + 0x08):
                if world and 0 <= link < 200:
                    struct.pack_into("<I", part, ROUTES + link * ROUTE, 1 if world <= 200 else 0)
        out["part-way"] = bytes(part)
        fresh = bytearray(data)                     # a new campaign: nothing won, so only the mission nothing leads to is open
        for n in range(100):
            if u32(fresh, MISSIONS + n * MISSION + 0x10):
                struct.pack_into("<I", fresh, MISSIONS + n * MISSION + 0x04, 0)
        for link in range(200):
            struct.pack_into("<I", fresh, ROUTES + link * ROUTE, 0)
        out["new campaign"] = bytes(fresh)
        ranks = bytearray(data)
        values = [1.0, 0.99, 0.75, 0.74, 0.5, 0.25, 0.1, 0.0, -1.0, 1.5, float("inf"), float("nan")]
        used = [n for n in range(100) if u32(ranks, MISSIONS + n * MISSION + 0x10)]
        for n, value in zip(used, values):
            struct.pack_into("<I", ranks, MISSIONS + n * MISSION + 0x04, 1)
            struct.pack_into("<f", ranks, MISSIONS + n * MISSION + 0x3C, value)
        out["every rank"] = bytes(ranks)
        goodies = bytearray(data)
        for g in range(300):
            struct.pack_into("<I", goodies, GOODIES + g * 4, (0, 1, 2, 3, 7, 0xFFFFFFFF)[g % 6])
        out["every Goodie state"] = bytes(goodies)
        kills = bytearray(data)
        for k in range(5):
            struct.pack_into("<I", kills, KILLS + k * 4, 0xAB000000 | (0xFFFFFF - k * 1000))   # the top byte is the game's screen position
        out["packed kills"] = bytes(kills)
        alt = bytearray(data)                       # an alternate route (the game's broken line) does not open a mission
        for link in range(200):
            if u32(alt, ROUTES + link * ROUTE + 4) != 0xFFFFFFFF:
                struct.pack_into("<I", alt, ROUTES + link * ROUTE, 2)
        for n in range(100):
            world = u32(alt, MISSIONS + n * MISSION + 0x10)
            if world:
                struct.pack_into("<I", alt, MISSIONS + n * MISSION + 0x04, 1 if world == 100 else 0)
        out["alternate routes only"] = bytes(alt)
        out["one byte short"] = bytes(data[:-1])
        out["one byte long"] = bytes(data) + b"\0"
        out["empty"] = b""
        wrong = bytearray(data)
        struct.pack_into("<H", wrong, 0, VERSION + 1)
        out["another version"] = bytes(wrong)
        return out

    def test_real_careers_are_present(self):
        self.assertTrue(self.careers, "no real career to read: set BEA_GAME_DIR to a game folder with savegames, or keep the Toolkit checkout beside this one")
        for path in self.careers:
            self.assertEqual(path.stat().st_size, SIZE, path.name)

    def test_reading_agrees_with_the_second_reader(self):
        self.assertTrue(self.careers)
        for path in self.careers:
            for name, data in self.variants(path.read_bytes()).items():
                self.assertIsNone(self.agree(data), f"{path.name}, {name}")

    def test_saved_sound_and_player_flags_stay_independent(self):
        data = bytearray(self.careers[0].read_bytes())
        for sound, music, one, two, want in [
                (0.25, 0.75, 0, 1, [25, 75, 0, 1]), (1., 0., 1, 0, [100, 0, 1, 0]),
                (float("nan"), 2., 0, 0, [-1, -1, 0, 0]), (-1., float("inf"), 3, 7, [-1, -1, 1, 1])]:
            struct.pack_into("<f", data, SAVED_SOUND, sound)
            struct.pack_into("<f", data, SAVED_MUSIC, music)
            struct.pack_into("<I", data, PLAYER_ONE, one)
            struct.pack_into("<I", data, PLAYER_TWO, two)
            self.assertEqual(self.view(bytes(data))["settings"], want)

    def test_variants_reach_every_state(self):
        data = self.careers[0].read_bytes()
        v = self.variants(data)
        part = read(v["part-way"])
        self.assertEqual({m[1] for m in part["missions"]}, {0, 1, 2})
        self.assertEqual([m[0] for m in part["missions"] if m[1] == 1], [211, 212])
        self.assertEqual(part["complete"], 3)
        ranks = read(v["every rank"])
        self.assertEqual([m[2] for m in ranks["missions"][:12]], ["S", "A", "A", "B", "B", "C", "D", "E", "E", "-", "-", "S"])
        self.assertEqual(set(read(v["every Goodie state"])["goodies"]), set("0123x"))
        fresh = read(v["new campaign"])
        self.assertEqual([m[1] for m in fresh["missions"]][:3], [1, 2, 2])         # 1.00 open, the rest not yet
        alt = read(v["alternate routes only"])
        self.assertEqual([m[1] for m in alt["missions"]][:3], [0, 2, 2])
        for name in ("one byte short", "one byte long", "empty", "another version"):
            self.assertIsNone(read(v[name]), name)
            self.assertIsNone(self.view(v[name]), name)

    def test_toolkit_career_has_the_facts_the_toolkit_pins(self):
        if not self.toolkit:
            self.skipTest("the Toolkit checkout is not beside this repository")
        view = self.view(self.toolkit.read_bytes())
        self.assertEqual(view["count"], 43)                       # Tests/CareerSaveTests.cs: 43 missions
        self.assertEqual((view["found"], view["all"]), (229, 232))  # 229 of the 230 shown viewed, 071-073 earned but never shown
        self.assertEqual(view["goodies"][71:74], "222")
        self.assertEqual([m[0] for m in view["missions"]][:5], [100, 110, 200, 211, 212])
        self.assertEqual(view["missions"][-1][0], 800)

    def plausible(self, data, binary=None):
        path = self.tmp / "plausible.bes"
        path.write_bytes(data)
        done = subprocess.run([str(binary or self.binary), "plausible", str(path)], capture_output=True, text=True, timeout=20)
        self.assertEqual(done.returncode, 0, done.stderr)
        return done.stdout.strip()

    def damaged_values(self, data):
        """Careers that are the right length and version but hold values the game never writes."""
        out = {}
        for name, at, value in (("a level number far above the campaign", MISSIONS + 3 * MISSION + 0x10, 9999),
                                ("a level number below the campaign", MISSIONS + 3 * MISSION + 0x10, 99),
                                ("a negative level number", MISSIONS + 5 * MISSION + 0x10, 0xFFFFFFFF),
                                ("a Goodie in a state the game never stores", GOODIES + 10 * 4, 7),
                                ("a Goodie in every state at once", GOODIES + 200 * 4, 0xFFFFFFFF)):
            wrong = bytearray(data)
            struct.pack_into("<I", wrong, at, value)
            out[name] = bytes(wrong)
        return out

    def test_a_career_with_impossible_values_is_not_shown(self):
        for path in self.careers:
            data = path.read_bytes()
            self.assertEqual(self.plausible(data), "shown", path.name)
            for name, wrong in self.damaged_values(data).items():
                self.assertEqual(self.plausible(wrong), "implausible", f"{path.name}: {name}")
            # slots the wall does not show (71 to 73) may hold anything: the game's own count takes them, the page never lists them
            quiet = bytearray(data)
            struct.pack_into("<I", quiet, GOODIES + 72 * 4, 9)
            self.assertEqual(self.plausible(bytes(quiet)), "shown", path.name)
            self.assertEqual(self.plausible(data[:-1]), "refused", path.name)

    def test_rank_letters(self):
        for value, letter in ((1.0, "S"), (0.999, "A"), (0.95, "A"), (0.875, "A"), (0.75, "A"),
                              (0.7499, "B"), (0.625, "B"), (0.5, "B"),
                              (0.4999, "C"), (0.375, "C"), (0.25, "C"),
                              (0.2499, "D"), (0.125, "D"), (1e-30, "D"),
                              (0.0, "E"), (-0.0, "E"), (-5.0, "E"),
                              (1.0001, "-"), (float("inf"), "-"), (float("-inf"), "E")):
            bits = struct.unpack("<I", struct.pack("<f", value))[0]
            done = subprocess.run([str(self.binary), "rank", f"{bits:x}"], capture_output=True, text=True, timeout=20)
            self.assertEqual(done.stdout.strip(), letter, value)
            self.assertEqual(rank_letter(bits), letter, value)

    def test_rank_letters_never_wrap_under_round_to_nearest(self):
        qifist_src = self.tmp / "rank_qifist.cpp"
        qifist_bin = self.tmp / "rank_qifist"
        qifist_src.write_text(
            '#include <cfenv>\n#include <cmath>\n#include <cstdio>\n#include <cstring>\n'
            '#pragma STDC FENV_ACCESS ON\n#include "ce_career_view.h"\n'
            'int main() {\n'
            '    std::fesetround(FE_TONEAREST);\n'
            '    const float vals[] = {0.875f, 0.88f, 0.90f, 0.95f, 0.999999f, 0.625f, 0.375f, 0.125f};\n'
            '    const char want[]  = {\'A\',   \'A\',  \'A\',  \'A\',  \'A\',      \'B\',   \'C\',   \'D\'};\n'
            '    for (unsigned i = 0; i < sizeof(vals)/sizeof(vals[0]); ++i) {\n'
            '        unsigned bits = 0; std::memcpy(&bits, &vals[i], 4);\n'
            '        if (CeRankLetter(bits) != want[i]) return 1;\n'
            '    }\n'
            '    return 0;\n'
            '}\n'
        )
        built = subprocess.run(["g++", "-std=c++14", "-O1", "-frounding-math", f"-I{GAME}", str(qifist_src), "-o", str(qifist_bin)],
                               capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stderr)
        ran = subprocess.run([str(qifist_bin)], capture_output=True, text=True, timeout=20)
        self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_mutants_are_caught(self):
        source = (GAME / "ce_career_view.h").read_text()
        mutants = (
            ("kMissionRank = 0x3C", "kMissionRank = 0x38", "the rank's place"),
            ("kMissionComplete = 0x04", "kMissionComplete = 0x00", "the completion's place"),
            ("kGoodieOffset = 0x1F46", "kGoodieOffset = 0x1F4A", "the Goodies' place"),
            ("kKillsOffset = 0x23F6", "kKillsOffset = 0x23FA", "the kills' place"),
            ("& 0xffffff);", "& 0xffffffff);", "the kill count's mask"),
            ("(!into[n] || reached[n]) ? CE_MISSION_OPEN", "reached[n] ? CE_MISSION_OPEN", "a mission nothing leads to is open"),
            ("(!into[n] || reached[n]) ? CE_MISSION_OPEN", "(!into[n] || into[n]) ? CE_MISSION_OPEN", "only a completed route opens a mission"),
            ("if (state == 1)\n\t\t\t\treached[to] = true;", "if (state)\n\t\t\t\treached[to] = true;", "an alternate route does not open a mission"),
            ("((slot < 71) || (slot > 73))", "((slot < 71) || (slot > 72))", "the wall leaves out 71 to 73"),
            ("(slot < CE_CAREER_GOODIE_TABLE)", "(slot < CE_CAREER_GOODIE_SLOTS)", "the wall ends with the game's table"),
            ("if (value <= 0.f)\n\t\treturn 'E';", "if (value < 0.f)\n\t\treturn 'E';", "a rank of zero is E"),
            ("const int quarters = static_cast<int>(std::floor(value * 4.f));", "const int quarters = static_cast<int>(std::floor(value * 5.f));", "the rank's quarters"),
            ("const int quarters = static_cast<int>(std::floor(value * 4.f));", "const int quarters = static_cast<int>(std::nearbyint(value * 4.f));", "the rank's floor under round-to-nearest"),
            ("(size != CE_CAREER_SIZE) || ", "", "a file of another length is refused"),
            (" || (U16(bytes) != CE_CAREER_VERSION))", ")", "a file of another version is refused"),
            ("out.mRank = CeRankLetter(U32(mission + kMissionRank));\n", "", "a completed mission has its rank"),
            ("if (int(state) >= CE_GOODIE_NEW)", "if (int(state) > CE_GOODIE_NEW)", "the game's own Goodie count"),
        )
        plausibility = (
            ("(view.mMissions[n].mWorld < 100)", "(view.mMissions[n].mWorld < 99)", "a level number below the campaign"),
            ("(view.mMissions[n].mWorld > 999)", "(view.mMissions[n].mWorld > 9999)", "a level number above the campaign"),
            ("CeGoodieShown(g) && (view.mGoodie[g] > CE_GOODIE_VIEWED)", "(view.mGoodie[g] > CE_GOODIE_VIEWED)", "only the wall's Goodies are judged"),
            ("if (view.mMissionCount < 1)\n\t\treturn false;", "", "a career with no missions is not shown"),
        )
        data = self.careers[0].read_bytes()
        cases = self.variants(data)
        for n, (old, new, what) in enumerate(mutants):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"mutant-{n}"
            folder.mkdir()
            for f in GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / f.name)
            (folder / "ce_career_view.h").write_text(source.replace(old, new))
            binary = self.build(folder / "driver", folder, werror=False)          # a mutant that does not compile fails the test: it would prove nothing
            caught = False
            for data in cases.values():
                try:
                    if self.agree(data, binary) is not None:
                        caught = True
                        break
                except (AssertionError, ValueError, IndexError, KeyError):
                    caught = True
                    break
            self.assertTrue(caught, f"a change to {what} went unnoticed")
        base_case = self.careers[0].read_bytes()
        empty = bytearray(base_case)
        for n in range(100):
            struct.pack_into("<I", empty, MISSIONS + n * MISSION + 0x10, 0)
        tried = {**self.damaged_values(base_case), "empty": bytes(empty)}
        for n, (old, new, what) in enumerate(plausibility):
            self.assertEqual(source.count(old), 1, f"{what}: the text to change must occur once")
            folder = self.tmp / f"plausible-mutant-{n}"
            folder.mkdir()
            for f in GAME.glob("ce_*.h"):
                shutil.copyfile(f, folder / f.name)
            (folder / "ce_career_view.h").write_text(source.replace(old, new))
            binary = self.build(folder / "driver", folder, werror=False)
            quiet = bytearray(base_case)
            struct.pack_into("<I", quiet, GOODIES + 72 * 4, 9)
            honest = [self.plausible(d) for d in (*tried.values(), bytes(quiet))]
            other = [self.plausible(d, binary) for d in (*tried.values(), bytes(quiet))]
            self.assertNotEqual(honest, other, f"a change to {what} went unnoticed")


if __name__ == "__main__":
    unittest.main(verbosity=2)
