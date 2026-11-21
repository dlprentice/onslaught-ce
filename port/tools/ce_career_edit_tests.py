#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host unit tests and causal mutants for port/game/ce_career_edit.h.

Exercises the exact production pure-byte career editor header compiled with g++ -Wall -Wextra -Werror
(zero game or D3D headers) on both a fully constructed 43-mission authored career and any real careers
present in BEA_GAME_DIR/savegames. Verifies bit-for-bit untouched-byte preservation, Goodies unlock/lock
+ pending popup clearing, Mission unlock/S-rank invariants, 24-bit kill masking (screen-pos top byte
preservation), Saved Settings float/bool round-trip, input/enum refusals, and causal mutants.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port/game"
DRIVER = ROOT / "port/tools/tests/ce_career_edit_driver.cpp"

SIZE = 10004
VERSION = 0x4BD1
PENDING_GOODIES = 0x0002
MISSIONS = 0x0006
MISSION_STRIDE = 64
ROUTES = 0x1906
ROUTE_STRIDE = 8
GOODIES = 0x1F46
KILLS = 0x23F6
IN_PROGRESS = 0x248A
SOUND = 0x248E
MUSIC = 0x2492
P1_GOD = 0x2496
P2_GOD = 0x249A

# 43 campaign missions matching src/Career.cpp's level_structure (world, lower_to, higher_to).
CAMPAIGN = (
    (100, 1, -1), (110, 2, -1), (200, 3, 4), (211, 5, 6), (212, 5, 6),
    (221, 7, 8), (222, 7, 8), (231, 9, -1), (232, 9, -1), (300, 10, 11),
    (311, 12, 13), (312, 12, 13), (321, 14, 15), (322, 14, 15), (331, 16, -1),
    (332, 16, -1), (400, 17, 18), (411, 19, 20), (412, 19, 20), (421, 21, 22),
    (422, 21, 22), (431, 23, -1), (432, 23, -1), (500, 24, 25), (511, 26, 27),
    (512, 28, 29), (521, 30, -1), (522, 30, -1), (523, 30, -1), (524, 30, -1),
    (600, 31, 32), (611, 33, 34), (612, 33, 34), (621, 35, -1), (622, 35, -1),
    (700, 36, -1), (710, 37, -1), (720, 38, 39), (731, 40, -1), (732, 41, -1),
    (741, -1, -1), (742, 42, -1), (800, -1, -1),
)


def shown(slot):
    return 0 <= slot < 233 and not (71 <= slot <= 73)


def make_authored_career():
    """Construct a valid 10,004-byte career with sentinel padding, partial progress, and packed kill top bytes."""
    buf = bytearray((i * 73 + 19) & 0xFF for i in range(SIZE))
    struct.pack_into("<H", buf, 0, VERSION)
    struct.pack_into("<I", buf, PENDING_GOODIES, 17)  # non-zero unannounced count to test clearing

    # Clear unused mission nodes and initialize 43 campaign nodes + 86 routes.
    for n in range(100):
        at = MISSIONS + n * MISSION_STRIDE
        struct.pack_into("<I", buf, at + 0x04, 0)
        struct.pack_into("<i", buf, at + 0x08, -1)
        struct.pack_into("<i", buf, at + 0x0C, -1)
        struct.pack_into("<I", buf, at + 0x10, 0)
        struct.pack_into("<f", buf, at + 0x3C, -1.0)
    for r in range(200):
        struct.pack_into("<Ii", buf, ROUTES + r * ROUTE_STRIDE, 0, -1)

    for idx, (world, low_to, high_to) in enumerate(CAMPAIGN):
        at = MISSIONS + idx * MISSION_STRIDE
        low_link = idx * 2
        high_link = idx * 2 + 1
        complete = 1 if idx < 3 else 0
        rank = (0.8, 0.5, 0.25)[idx] if complete else -1.0
        struct.pack_into("<I", buf, at + 0x04, complete)
        struct.pack_into("<i", buf, at + 0x08, low_link)
        struct.pack_into("<i", buf, at + 0x0C, high_link)
        struct.pack_into("<I", buf, at + 0x10, world)
        struct.pack_into("<f", buf, at + 0x3C, rank)
        struct.pack_into("<Ii", buf, ROUTES + low_link * ROUTE_STRIDE, 1 if complete else 0, low_to)
        struct.pack_into("<Ii", buf, ROUTES + high_link * ROUTE_STRIDE, 1 if (complete and high_to >= 0) else 0, high_to)

    # Goodies: mixed states on shown slots, distinct sentinel values on unshown slots 71..73 and 233..299.
    for g in range(300):
        if shown(g):
            state = (g % 4)
        elif 71 <= g <= 73:
            state = 2
        else:
            state = 0x5A000000 | g
        struct.pack_into("<I", buf, GOODIES + g * 4, state)

    # Kills with non-zero top bytes (screen position X/Y live in top bytes of slots 0 and 1).
    for k, (top, count) in enumerate(((0x84, 125), (0x7B, 2500), (0x12, 33), (0x99, 777), (0x44, 9))):
        struct.pack_into("<I", buf, KILLS + k * 4, (top << 24) | count)

    struct.pack_into("<I", buf, IN_PROGRESS, 0)
    struct.pack_into("<f", buf, SOUND, 0.80000001)  # intentionally unrounded float bits
    struct.pack_into("<f", buf, MUSIC, 0.65)
    struct.pack_into("<I", buf, P1_GOD, 0)
    struct.pack_into("<I", buf, P2_GOD, 1)
    return bytes(buf)


def build_driver(out, include, werror=True):
    cmd = ["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", *(["-Werror"] if werror else []),
           f"-I{include}", str(DRIVER), "-o", str(out)]
    done = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
    if done.returncode:
        raise RuntimeError("driver compile failed:\n" + done.stdout + done.stderr)
    return out


def run_apply(binary, scratch, orig_bytes, *ops):
    in_path = scratch / "in.bes"
    out_path = scratch / "out.bes"
    in_path.write_bytes(orig_bytes)
    if out_path.exists():
        out_path.unlink()
    done = subprocess.run([str(binary), "apply", str(in_path), str(out_path), *ops],
                          capture_output=True, text=True, timeout=30)
    if done.returncode:
        raise AssertionError(f"driver error {done.returncode}: {done.stderr}")
    lines = done.stdout.splitlines()
    if lines == ["refused"]:
        return None, None
    assert lines and lines[0] == "ok", lines
    info = {}
    for line in lines[1:]:
        parts = line.split()
        if parts[0] == "changed":
            info["changed"] = int(parts[1])
        elif parts[0] == "missions":
            info.update(mission_count=int(parts[1]), complete=int(parts[3]),
                        open=int(parts[5]), locked=int(parts[7]), rank_s=int(parts[9]))
        elif parts[0] == "goodies":
            info.update(goodies_found=int(parts[1]), goodies_all=int(parts[3]))
        elif parts[0] == "kills":
            info["kills"] = [int(x) for x in parts[1:]]
        elif parts[0] == "settings":
            info["settings"] = [int(x) for x in parts[1:]]
    return out_path.read_bytes(), info


def diff_offsets(a, b):
    return {i for i in range(len(a)) if a[i] != b[i]}


class CareerEditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp(prefix="ce-career-edit-"))
        cls.binary = build_driver(cls.tmp / "driver", GAME)
        cls.authored = make_authored_career()
        cls.samples = [("authored", cls.authored)]
        game = os.environ.get("BEA_GAME_DIR")
        if game:
            for p in sorted(Path(game).glob("savegames/*.bes")):
                cls.samples.append((p.name, p.read_bytes()))

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_noop_roundtrip_preserves_every_byte(self):
        for label, data in self.samples:
            with self.subTest(career=label):
                out, info = run_apply(self.binary, self.tmp, data)
                self.assertIsNotNone(out)
                self.assertEqual(info["changed"], 0)
                self.assertEqual(out, data, f"{label}: untouched career must remain byte-for-byte identical")

    def test_goodies_unlock_all_and_clears_pending_popups(self):
        out, info = run_apply(self.binary, self.tmp, self.authored, "goodies=1")
        self.assertIsNotNone(out)
        self.assertEqual(info["changed"], 1)
        self.assertEqual(info["goodies_found"], 230)
        self.assertEqual(struct.unpack_from("<I", out, PENDING_GOODIES)[0], 0)
        allowed = set(range(PENDING_GOODIES, PENDING_GOODIES + 4))
        for g in range(300):
            val = struct.unpack_from("<I", out, GOODIES + g * 4)[0]
            if shown(g):
                self.assertEqual(val, 3, f"shown slot {g} must be CE_GOODIE_VIEWED (3)")
                allowed.update(range(GOODIES + g * 4, GOODIES + g * 4 + 4))
            else:
                orig_val = struct.unpack_from("<I", self.authored, GOODIES + g * 4)[0]
                self.assertEqual(val, orig_val, f"unshown slot {g} must stay untouched")
        self.assertTrue(diff_offsets(self.authored, out).issubset(allowed))

    def test_goodies_lock_all_and_preserves_unshown_slots(self):
        out, info = run_apply(self.binary, self.tmp, self.authored, "goodies=2")
        self.assertIsNotNone(out)
        self.assertEqual(info["goodies_found"], 0)
        self.assertEqual(struct.unpack_from("<I", out, PENDING_GOODIES)[0], 0)
        for g in range(300):
            val = struct.unpack_from("<I", out, GOODIES + g * 4)[0]
            if shown(g):
                self.assertEqual(val, 0)
            else:
                self.assertEqual(val, struct.unpack_from("<I", self.authored, GOODIES + g * 4)[0])

    def test_missions_unlock_all_opens_every_route_and_keeps_ranks(self):
        out, info = run_apply(self.binary, self.tmp, self.authored, "missions=1")
        self.assertIsNotNone(out)
        self.assertEqual(info["mission_count"], 43)
        self.assertEqual(info["complete"], 3)
        self.assertEqual(info["open"], 40)
        self.assertEqual(info["locked"], 0)
        self.assertEqual(info["rank_s"], 0)
        # Mission records themselves (including ranks and completion flags) must be untouched.
        self.assertEqual(out[MISSIONS:MISSIONS + 100 * MISSION_STRIDE],
                         self.authored[MISSIONS:MISSIONS + 100 * MISSION_STRIDE])

    def test_missions_srank_completed_only_upgrades_completed(self):
        out, info = run_apply(self.binary, self.tmp, self.authored, "missions=2")
        self.assertIsNotNone(out)
        self.assertEqual(info["complete"], 3)
        self.assertEqual(info["rank_s"], 3)
        self.assertEqual(info["locked"], 38)
        for idx in range(43):
            at = MISSIONS + idx * MISSION_STRIDE
            rank = struct.unpack_from("<f", out, at + 0x3C)[0]
            if idx < 3:
                self.assertEqual(rank, 1.0)
            else:
                self.assertEqual(out[at + 0x3C:at + 0x40], self.authored[at + 0x3C:at + 0x40])
        self.assertEqual(out[ROUTES:ROUTES + 200 * ROUTE_STRIDE],
                         self.authored[ROUTES:ROUTES + 200 * ROUTE_STRIDE])

    def test_missions_srank_all_completes_sranks_and_opens_routes(self):
        out, info = run_apply(self.binary, self.tmp, self.authored, "missions=3")
        self.assertIsNotNone(out)
        self.assertEqual(info["mission_count"], 43)
        self.assertEqual(info["complete"], 43)
        self.assertEqual(info["open"], 0)
        self.assertEqual(info["locked"], 0)
        self.assertEqual(info["rank_s"], 43)
        self.assertEqual(struct.unpack_from("<I", out, IN_PROGRESS)[0], 1)
        for idx in range(43):
            at = MISSIONS + idx * MISSION_STRIDE
            self.assertEqual(struct.unpack_from("<I", out, at + 0x04)[0], 1)
            self.assertEqual(struct.unpack_from("<f", out, at + 0x3C)[0], 1.0)

    def test_kills_preserve_top_byte_and_clamp_to_range(self):
        out, info = run_apply(self.binary, self.tmp, self.authored,
                              "kill0=123456", "kill1=-50", "kill3=99999999")
        self.assertIsNotNone(out)
        self.assertEqual(info["kills"], [123456, 0, 33, 999999, 9])
        # Verify exact top bytes preserved on every single kill slot!
        expected_tops = (0x84, 0x7B, 0x12, 0x99, 0x44)
        for k, top in enumerate(expected_tops):
            word = struct.unpack_from("<I", out, KILLS + k * 4)[0]
            self.assertEqual((word >> 24) & 0xFF, top, f"kill slot {k} top byte clobbered")
        # Untouched kill slots (2 and 4) must remain bit-for-bit identical, as must every byte outside edited slots.
        allowed = set(range(KILLS, KILLS + 4)) | set(range(KILLS + 4, KILLS + 8)) | set(range(KILLS + 12, KILLS + 16))
        self.assertTrue(diff_offsets(self.authored, out).issubset(allowed))

    def test_saved_settings_roundtrip_and_preserve_untouched_float_bits(self):
        # Editing only P1 invincible leaves Sound/Music float bits exact.
        out1, info1 = run_apply(self.binary, self.tmp, self.authored, "p1=1")
        self.assertEqual(info1["settings"], [80, 65, 1, 1])
        self.assertEqual(out1[SOUND:SOUND + 4], self.authored[SOUND:SOUND + 4])
        self.assertEqual(out1[MUSIC:MUSIC + 4], self.authored[MUSIC:MUSIC + 4])
        self.assertTrue(diff_offsets(self.authored, out1).issubset(set(range(P1_GOD, P1_GOD + 4))))

        # Editing Sound, Music, P1, P2 rounds cleanly and clamps out-of-range percents.
        out2, info2 = run_apply(self.binary, self.tmp, self.authored,
                                "sound=40", "music=100", "p1=1", "p2=0")
        self.assertEqual(info2["settings"], [40, 100, 1, 0])
        self.assertAlmostEqual(struct.unpack_from("<f", out2, SOUND)[0], 0.4, places=5)
        self.assertAlmostEqual(struct.unpack_from("<f", out2, MUSIC)[0], 1.0, places=5)
        self.assertEqual(struct.unpack_from("<I", out2, P1_GOD)[0], 1)
        self.assertEqual(struct.unpack_from("<I", out2, P2_GOD)[0], 0)
        allowed = set(range(SOUND, P2_GOD + 4))
        self.assertTrue(diff_offsets(self.authored, out2).issubset(allowed))

        # Corrupt NaN volume preserved if untouched, cleanly overwritten when edited.
        weird = bytearray(self.authored)
        struct.pack_into("<f", weird, SOUND, float("nan"))
        out_keep, info_keep = run_apply(self.binary, self.tmp, bytes(weird))
        self.assertEqual(out_keep, bytes(weird))
        self.assertEqual(info_keep["settings"][0], -1)
        out_fix, info_fix = run_apply(self.binary, self.tmp, bytes(weird), "sound=70")
        self.assertEqual(info_fix["settings"][0], 70)

    def test_step_and_preset_helpers(self):
        done = subprocess.run([str(self.binary), "steps"], capture_output=True, text=True, timeout=20)
        self.assertEqual(done.returncode, 0, done.stderr)
        lines = dict(line.split(None, 1) for line in done.stdout.splitlines())
        self.assertEqual(lines["clamp"], "0 0 12345 999999")
        self.assertEqual(lines["step"], "0 450 350 999999")
        self.assertEqual(lines["preset"], "100 500 1000 999999 0")
        self.assertEqual(lines["vol"], "80 0 90 80 100 0 100")

    def test_invalid_inputs_and_enums_refused(self):
        bad_version = bytearray(self.authored)
        struct.pack_into("<H", bad_version, 0, VERSION + 1)
        bad_world = bytearray(self.authored)
        struct.pack_into("<I", bad_world, MISSIONS + 0x10, 9999)
        for label, payload, ops in (
            ("short", self.authored[:-1], ()),
            ("long", self.authored + b"\x00", ()),
            ("version", bytes(bad_version), ()),
            ("implausible world", bytes(bad_world), ()),
            ("bad goodies enum", self.authored, ("goodies=9",)),
            ("bad missions enum", self.authored, ("missions=9",)),
        ):
            with self.subTest(case=label):
                out, info = run_apply(self.binary, self.tmp, payload, *ops)
                self.assertIsNone(out)
                self.assertIsNone(info)

    def test_causal_mutants_are_caught(self):
        source = (GAME / "ce_career_edit.h").read_text()
        mutants = (
            ("PutU32(out + kPendingGoodiesOffset, 0u);", "// dropped pending clear",
             "unlocking goodies must clear unannounced popup count at 0x0002",
             ("goodies=1",), lambda o, i: o is not None and struct.unpack_from("<I", o, PENDING_GOODIES)[0] == 0),
            ("PutU32(out + kGoodieOffset + g * 4, unsigned(CE_GOODIE_VIEWED));",
             "PutU32(out + kGoodieOffset + g * 4, unsigned(CE_GOODIE_NEW));",
             "unlock all goodies must write VIEWED (3), not NEW (2)",
             ("goodies=1",), lambda o, i: o is not None and struct.unpack_from("<I", o, GOODIES)[0] == 3),
            ("if (CeGoodieShown(g))\n\t\t\t\tPutU32(out + kGoodieOffset + g * 4, unsigned(CE_GOODIE_VIEWED));",
             "PutU32(out + kGoodieOffset + g * 4, unsigned(CE_GOODIE_VIEWED));",
             "unlock all goodies must not alter unshown slots",
             ("goodies=1",), lambda o, i: o is not None and struct.unpack_from("<I", o, GOODIES + 72 * 4)[0] == 2),
            ("const unsigned topByte = U32(slot) & 0xff000000u;",
             "const unsigned topByte = 0u;",
             "kill editing must preserve the screen-position top byte",
             ("kill0=500",), lambda o, i: o is not None and (struct.unpack_from("<I", o, KILLS)[0] >> 24) == 0x84),
            ("if (value > CE_EDIT_MAX_KILLS)\n\t\treturn CE_EDIT_MAX_KILLS;",
             "if (value > 16777215)\n\t\treturn 16777215;",
             "kill clamping must cap at 999999",
             ("kill0=5000000",), lambda o, i: o is not None and i["kills"][0] == 999999),
            ("PutF32(mission + kMissionRank, 1.0f);",
             "PutF32(mission + kMissionRank, 0.9f);",
             "S-rank edits must write rank float 1.0f",
             ("missions=2",), lambda o, i: o is not None and i["rank_s"] == 3),
            ("if (U32(mission + kMissionComplete))\n\t\t\t\tPutF32(mission + kMissionRank, 1.0f);",
             "PutF32(mission + kMissionRank, 1.0f);",
             "S-rank completed must not alter uncompleted missions",
             ("missions=2",), lambda o, i: o is not None and o[MISSIONS + 3 * MISSION_STRIDE + 0x3C:MISSIONS + 3 * MISSION_STRIDE + 0x40] == self.authored[MISSIONS + 3 * MISSION_STRIDE + 0x3C:MISSIONS + 3 * MISSION_STRIDE + 0x40]),
            ("if (!CeCareerViewBuild(orig, size, &before) || !CeCareerViewPlausible(before))\n\t\treturn false;",
             "if (!CeCareerViewBuild(orig, size, &before))\n\t\treturn false;",
             "implausible input careers must be refused before applying edits",
             None, None),
        )
        bad_goodie = bytearray(self.authored)
        struct.pack_into("<I", bad_goodie, GOODIES + 10 * 4, 7)

        for idx, (old, new, what, ops, check) in enumerate(mutants):
            with self.subTest(mutant=what):
                if old == "PutU32(out + kPendingGoodiesOffset, 0u);":
                    self.assertGreaterEqual(source.count(old), 1, what)
                    mutated = source.replace(old, new, 1)
                elif old == "PutF32(mission + kMissionRank, 1.0f);":
                    self.assertGreaterEqual(source.count(old), 1, what)
                    mutated = source.replace(old, new)
                else:
                    self.assertEqual(source.count(old), 1, f"{what}: expected 1 occurrence")
                    mutated = source.replace(old, new, 1)
                folder = self.tmp / f"mutant-{idx}"
                folder.mkdir()
                shutil.copyfile(GAME / "ce_career_view.h", folder / "ce_career_view.h")
                (folder / "ce_career_edit.h").write_text(mutated)
                binary = build_driver(folder / "driver", folder, werror=False)
                if ops is None:
                    # Without entry plausibility check, goodies=1 would overwrite slot 10 and pass exit plausibility!
                    out, _ = run_apply(binary, folder, bytes(bad_goodie), "goodies=1")
                    self.assertIsNotNone(out, f"mutant {what} did not change behavior as expected")
                    real_out, _ = run_apply(self.binary, self.tmp, bytes(bad_goodie), "goodies=1")
                    self.assertIsNone(real_out)
                else:
                    out, info = run_apply(binary, folder, self.authored, *ops)
                    self.assertFalse(check(out, info), f"mutant went unnoticed: {what}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
