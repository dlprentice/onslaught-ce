#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The judging half of port/tools/ce_commander_isolation_run.py, without a game: authored update dumps and probe notes in the formats the game
writes them (updatedump.cpp's `U`/`N`/`H` lines; the map's close notes `view picture ...` and `view audit ...`), and an authored layout of an
executable's sections and symbols.

What these hold: a run equal to the reference passes and one that differs anywhere fails, at the right update; the level's own noise (the
places two never-opened runs differ) is left out and nothing else is; a run that ended early, an empty dump and two reference runs of different
lengths are failures and never passes; the control must differ; the audit passes only when it audited frames of a drawn picture with the
Commander's records apart, changed no heap byte and changed data only in the dynamic linker's sections; the audit's own control must name the map
object in every frame; and the key plans open, orbit and toggle the view as the runs describe.

No game, GPU, display or network is used.
"""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ce_commander_isolation_run as run  # noqa: E402


def dump(updates, change=None, extra=None, drop_from=None):
    """An authored dump: `updates` blocks of three things. change = (update, thing) alters one thing's position; extra = update adds a thing
    there; drop_from = update ends the dump before it."""
    lines = []
    for u in range(1, updates + 1):
        if drop_from is not None and u >= drop_from:
            break
        things = [(1, "6CRound", 0x41200000 + u), (2, "14CGroundVehicle", 0x42c80000), (3, "10CExplosion", 0x40400000)]
        if extra == u:
            things.append((4, "10CExplosion", 0x3f800000))
        lines.append(f"U {u} F {u + u // 2} E 3dcccccd R {1000 + u} 123456 D {u * 7} N {len(things)}\n")
        for number, name, x in things:
            if change == (u, number):
                x += 1
            lines.append(f"H {number} 00000010 {name} {x:08x} 42000000 c1200000\n")
    return "".join(lines)


def write(folder, name, text):
    path = Path(folder) / name
    path.write_text(text)
    return path


# An authored executable: its data starts at 0x1000; a loader table, the Commander's own section, the game's zeroed data with the map object.
START = 0x1000
SECTIONS = [(0x1000, 0x400, ".data"), (0x1400, 0x80, ".got.plt"), (0x1480, 0x200, "cecmdd"), (0x1680, 0x4000, ".bss")]
SYMBOLS = [(0x1000, "__data_start"), (0x1040, "gSomething"), (0x1100, "(anonymous namespace)::gMixer"), (0x1300, "gAfterTheMixer"),
           (0x1400, "_GLOBAL_OFFSET_TABLE_"), (0x1480, "(anonymous namespace)::gStrategicSample"), (0x1680, "SHADOWS"), (0x2000, "MAP"), (0x4000, "RENDERINFO")]


def line_of(address):
    """The audit's key for a data byte: its offset from the data's start."""
    return address - START


def probe(frames=190, changed=0, heap=0, data=0, entries=(), own=1, picture=True, pieces=16):
    text = "P 1 100 map close world 0 solid 0 things 3 components 0 time 0 frames 0 avg 0.00 ms max 0.00 ms\n"
    if picture:
        text += f"P 1 100 view picture frames {frames} pieces {pieces} marks 48 vertices 5052 texels 262144 mixer 1\n"
    text += "P 1 100 view periods live 190 avg 3.000 ms other 268 avg 2.900 ms\n"
    text += f"P 1 100 view audit frames {frames} changed-frames {changed} heap-bytes {heap} data-bytes {data} entries {len(entries)} spans 7 span-bytes 78100929 own-sections {own}\n"
    for kind, key, f, b in entries:
        text += f"P 1 100 view audit {kind} {key} frames {f} bytes {b}\n"
    return text


class Dumps(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def blocks(self, text):
        return run.blocks(write(self.tmp.name, "d.dump", text))

    def test_the_reader_makes_one_block_an_update(self):
        b = self.blocks(dump(5))
        self.assertEqual(len(b), 5)
        self.assertTrue(all(block[0].startswith("U ") and len(block) == 4 for block in b))

    def test_equal_runs_pass(self):
        verdict = run.compare(self.blocks(dump(40)), self.blocks(dump(40)))
        self.assertTrue(verdict["equal"])
        self.assertEqual((verdict["differing"], verdict["first"], verdict["updates"], verdict["reference_updates"]), (0, None, 40, 40))

    def test_one_changed_position_fails_at_its_update(self):
        verdict = run.compare(self.blocks(dump(40)), self.blocks(dump(40, change=(17, 2))))
        self.assertFalse(verdict["equal"])
        self.assertEqual((verdict["differing"], verdict["first"], verdict["first_present"]), (1, 17, 17 + 8))

    def test_a_thing_more_fails(self):
        verdict = run.compare(self.blocks(dump(40)), self.blocks(dump(40, extra=9)))
        self.assertFalse(verdict["equal"])
        self.assertEqual(verdict["first"], 9)

    def test_a_run_that_ended_early_is_not_equal(self):
        verdict = run.compare(self.blocks(dump(40)), self.blocks(dump(40, drop_from=30)))
        self.assertFalse(verdict["equal"], "a shorter run equal as far as it goes is not a pass")
        self.assertEqual((verdict["differing"], verdict["why"]), (0, "the runs are of different lengths"))
        longer = run.compare(self.blocks(dump(30)), self.blocks(dump(40)))
        self.assertFalse(longer["equal"])

    def test_an_empty_dump_is_not_equal(self):
        self.assertFalse(run.compare(self.blocks(dump(40)), [])["equal"])
        self.assertFalse(run.compare([], self.blocks(dump(40)))["equal"])
        self.assertEqual(run.compare([], [])["why"], "a dump is empty")

    def test_a_reference_shorter_than_the_run_asked_for_is_refused(self):
        verdict = run.compare(self.blocks(dump(40)), self.blocks(dump(40)), expect_updates=41)
        self.assertFalse(verdict["equal"])
        self.assertEqual(verdict["why"], "the reference run is shorter than the run asked for")

    def test_noise_is_the_places_two_reference_runs_differ_and_only_those_are_left_out(self):
        first, second = self.blocks(dump(40)), self.blocks(dump(40, change=(3, 3)))
        places = run.noise(first, second)
        self.assertEqual(places, {(2, 3)})
        # a run that differs only at the noisy place passes; one that differs at another line of the same update does not
        self.assertTrue(run.compare(first, second, places)["equal"])
        self.assertEqual(run.compare(first, second, places)["masked_lines"], 1)
        self.assertFalse(run.compare(first, self.blocks(dump(40, change=(3, 2))), places)["equal"])
        self.assertFalse(run.compare(first, self.blocks(dump(40, change=(4, 3))), places)["equal"])
        self.assertEqual(run.noise(first, first), set())

    def test_reference_runs_of_different_shape_have_no_noise_to_offer(self):
        first = self.blocks(dump(40))
        self.assertIsNone(run.noise(first, self.blocks(dump(39))))
        self.assertIsNone(run.noise(first, self.blocks(dump(40, extra=5))))


class Audit(unittest.TestCase):
    def judge(self, text):
        head, entries, picture = run.read_audit(text)
        return run.judge_audit(head, entries, picture, START, SECTIONS, SYMBOLS)

    def test_the_report_is_read_as_written(self):
        head, entries, picture = run.read_audit(probe(changed=1, data=4, entries=[("data-byte", line_of(0x1400), 1, 4)]))
        self.assertEqual((head["frames"], head["changed_frames"], head["heap_bytes"], head["data_bytes"], head["own_sections"]), (190, 1, 0, 4, 1))
        self.assertEqual(entries, [{"kind": "data-byte", "key": line_of(0x1400), "frames": 1, "bytes": 4}])
        self.assertEqual(run.read_audit("P 1 2 view audit data-line 7 frames 1 bytes 1\n")[1], [], "an entry before any report's head is no entry")
        self.assertEqual((picture["frames"], picture["pieces"], picture["mixer"]), (190, 16, 1))

    def test_a_clean_audit_passes(self):
        passed, reasons, named = self.judge(probe())
        self.assertTrue(passed, reasons)
        self.assertEqual(named, [])

    def test_the_loaders_own_table_is_allowed_and_named(self):
        passed, reasons, named = self.judge(probe(changed=1, data=4, entries=[("data-byte", line_of(0x1400), 1, 4)]))
        self.assertTrue(passed, reasons)
        self.assertEqual((named[0]["where"], named[0]["allowed"], named[0]["why"]), ("data .got.plt", True, "the dynamic linker's table"))

    def test_a_record_another_thread_writes_is_allowed_and_named_and_only_when_the_whole_line_is_its(self):
        passed, reasons, named = self.judge(probe(changed=45, data=90, entries=[("data-byte", line_of(0x1140), 45, 90)]))
        self.assertTrue(passed, reasons)
        self.assertEqual((named[0]["symbol"], named[0]["allowed"], named[0]["why"]), ("(anonymous namespace)::gMixer", True, "another thread's record"))
        # the mixer's last byte is the mixer's; the byte after it is the next object's, and that fails
        self.assertTrue(self.judge(probe(changed=1, data=1, entries=[("data-byte", line_of(0x12ff), 1, 1)]))[0])
        passed, reasons, named = self.judge(probe(changed=1, data=1, entries=[("data-byte", line_of(0x1300), 1, 1)]))
        self.assertFalse(passed)
        self.assertEqual(named[0]["symbol"], "gAfterTheMixer")
        self.assertTrue(all(reason for reason in run.THREAD_OWNED.values()), "every listed record says why")

    def test_a_changed_heap_byte_fails(self):
        passed, reasons, named = self.judge(probe(changed=3, heap=12, entries=[("heap-type", 16, 3, 12)]))
        self.assertFalse(passed)
        self.assertIn("12 heap bytes changed", reasons)
        self.assertEqual(named[0]["where"], "heap memory type 16")

    def test_a_changed_byte_of_the_games_data_fails_and_is_named(self):
        passed, reasons, named = self.judge(probe(changed=190, data=190, entries=[("data-byte", line_of(0x1680), 190, 190)]))
        self.assertFalse(passed)
        self.assertTrue(any("SHADOWS" in r and ".bss" in r for r in reasons), reasons)
        self.assertEqual(named[0]["symbol"], "SHADOWS")

    def test_the_byte_before_the_loaders_table_is_not_the_loaders(self):
        passed, reasons, named = self.judge(probe(changed=1, data=1, entries=[("data-byte", line_of(0x13ff), 1, 1)]))
        self.assertFalse(passed)
        self.assertEqual(named[0]["where"], "data .data")
        self.assertTrue(self.judge(probe(changed=1, data=1, entries=[("data-byte", line_of(0x147f), 1, 1)]))[0], "the table's last byte")

    def test_a_report_whose_entries_do_not_add_up_is_refused(self):
        passed, reasons, _ = self.judge(probe(changed=9, data=500, entries=[("data-byte", line_of(0x1400), 1, 4)]))
        self.assertFalse(passed)
        self.assertTrue(any("cut short" in r for r in reasons), reasons)

    def test_an_audit_that_audited_nothing_is_not_a_pass(self):
        for name, text in (("no report", "P 1 100 map close world 0\n"), ("no frames", probe(frames=0)), ("no picture note", probe(picture=False)),
                           ("the Commander's records not apart", probe(own=0))):
            with self.subTest(case=name):
                self.assertFalse(self.judge(text)[0])
        # The view is the game's own render with CE's overlay: its frames are counted and no replacement scenery is drawn, so a picture with no
        # terrain pieces is the ordinary case and passes (where nothing else fails).
        self.assertTrue(self.judge(probe(pieces=0))[0])

    def test_the_renderers_own_state_is_named_and_allowed_and_nothing_else_is(self):
        self.assertEqual(set(run.RENDER_STATE), {"PCLTShell::mRenderStates", "PCLTShell::mCurrentTextures", "PCLTShell::mCurrentFVF", "RENDERINFO", "FASTVB"})

    def test_the_audits_own_control_must_name_the_map_in_every_frame(self):
        def leak(text):
            head, entries, _ = run.read_audit(text)
            return run.judge_leak(head, entries, START, SECTIONS, SYMBOLS)
        self.assertTrue(leak(probe(changed=190, data=190, entries=[("data-byte", line_of(0x2000), 190, 190)]))[0])
        self.assertFalse(leak(probe())[0], "a clean report is the control failing")
        self.assertFalse(leak(probe(changed=1, data=1, entries=[("data-byte", line_of(0x2000), 1, 1)]))[0], "once is not every frame")
        self.assertFalse(leak(probe(changed=190, data=190, entries=[("data-byte", line_of(0x4000), 190, 190)]))[0], "another object is not the map")
        self.assertFalse(leak(probe(frames=0))[0])

    def test_a_byte_is_named_by_its_section_and_the_symbol_it_lies_in(self):
        self.assertEqual(run.name_byte(line_of(0x2000), START, SECTIONS, SYMBOLS), (".bss", "MAP"))
        self.assertEqual(run.name_byte(line_of(0x3fff), START, SECTIONS, SYMBOLS), (".bss", "MAP"), "inside a large object: the object it lies in")
        self.assertEqual(run.name_byte(line_of(0x4000), START, SECTIONS, SYMBOLS), (".bss", "RENDERINFO"))
        self.assertEqual(run.name_byte(line_of(0x1480), START, SECTIONS, SYMBOLS), ("cecmdd", "(anonymous namespace)::gStrategicSample"))
        self.assertEqual(run.name_byte(line_of(0x1041), START, SECTIONS, SYMBOLS), (".data", "gSomething"))
        self.assertEqual(run.name_byte(0x100000, START, SECTIONS, SYMBOLS)[0], None, "past every section")


class Keys(unittest.TestCase):
    def test_a_level_names_its_opening_present_and_may_name_its_own_end(self):
        self.assertEqual(run.parse_level("110:330", 2800), (110, 330, 2800))
        self.assertEqual(run.parse_level("800:400:860", 2800), (800, 400, 860))
        for spec in ("110", "110:330:900:1", "110:abc", "a:330", "110:330:400", ""):
            with self.subTest(spec=spec):
                with self.assertRaises(ValueError):
                    run.parse_level(spec, 2800)

    def test_never_presses_nothing(self):
        self.assertEqual(run.keys("never", 330, 2800), "")
        self.assertEqual(run.keys("never2", 330, 2800), "")

    def test_open_opens_once_and_orbits_to_the_end(self):
        for arm in ("open", "control", "audit", "leak"):
            self.assertEqual(run.keys(arm, 330, 2800), "330:16:32:d,331:16:32:u,332:8:12:d,2790:8:12:u")

    def test_toggle_opens_and_closes_all_run_and_ends_closed(self):
        plan = run.keys("toggle", 330, 800).split(",")
        presses = [int(p.split(":")[0]) for p in plan if p.endswith(":16:32:d")]
        self.assertEqual(presses, [330, 420, 510, 600, 690, 780])
        self.assertEqual(len(presses) % 2, 0, "an even number of presses: the view is closed at the end")
        holds = [(int(a.split(":")[0]), int(b.split(":")[0])) for a, b in zip(plan, plan[1:]) if a.endswith(":8:12:d") and b.endswith(":8:12:u")]
        self.assertEqual(holds, [(335, 410), (515, 590), (695, 770)], "the orbit key is held only inside the open stretches")
        for down, up in holds:
            self.assertTrue(any(open_at < down and up < open_at + run.TOGGLE_EVERY for open_at in presses[0::2]))


CLOSE_LINE = "P 499 699 map close world 0 solid 0 things 1 components 0 time 0 frames 0 avg 0.00 ms max 0.00 ms\n"
CLOSED_NOW = "P 500 700 map closed now camera 1 cockpit 0 token 0 keytrap 0 squads 1 units 0 returning 1\n"
CLOSED_SETTLED = "P 530 745 map closed settled camera 1 cockpit 1 token 0 keytrap 0 squads 1 units 0 returning 0\n"
CLOSED_OK = CLOSE_LINE + CLOSED_NOW + CLOSED_SETTLED


class Levels(unittest.TestCase):
    """judge_level over a folder of authored files, with every arm's exit code."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)
        self.arms = list(run.ARMS)
        for arm in ("never", "never2", "open", "open2", "toggle"):
            write(self.folder, f"{arm}.dump", dump(60))
        write(self.folder, "control.dump", dump(60, change=(20, 2)))
        for arm in ("open", "open2", "toggle", "control"):
            write(self.folder, f"{arm}.probe", probe(frames=2000))
        write(self.folder, "toggle.probe", probe(frames=2000) + CLOSED_NOW + CLOSED_OK * 3)
        write(self.folder, "rapid.dump", dump(60))
        write(self.folder, "rapid.probe", probe(frames=2000) + CLOSED_NOW + (CLOSE_LINE + CLOSED_NOW) * 5)
        write(self.folder, "audit.probe", probe())
        write(self.folder, "leak.probe", probe(changed=190, data=190, entries=[("data-byte", line_of(0x2000), 190, 190)]))

    def judge(self, codes=None):
        return run.judge_level(self.folder, self.arms, codes or {a: 0 for a in self.arms}, 400, 2800, START, SECTIONS, SYMBOLS)

    def test_a_good_level_passes(self):
        verdict = self.judge()
        self.assertTrue(verdict["passed"], verdict)
        self.assertEqual(verdict["arms"]["control"]["first"], 20)

    def test_each_way_a_level_fails(self):
        cases = {
            "the same inputs gave another battle": ("open2.dump", dump(60, change=(41, 1)), "open2"),
            "the repeat ended early": ("open2.dump", dump(60, drop_from=50), "open2"),
            "the repeat's view never opened": ("open2.probe", probe(frames=0), "open2"),
            "open ended early": ("open.dump", dump(60, drop_from=50), "open"),
            "the control is equal": ("control.dump", dump(60), "control"),
            "the control left no dump": ("control.dump", "", "control"),
            "the audit saw a heap write": ("audit.probe", probe(changed=1, heap=4, entries=[("heap-type", 59, 1, 4)]), "audit"),
            "the audit saw the shadow manager written": ("audit.probe", probe(changed=1, data=8, entries=[("data-byte", line_of(0x1680), 1, 8)]), "audit"),
            "the leak was not seen": ("leak.probe", probe(), "leak"),
            "the reference runs differ in length": ("never2.dump", dump(59), "never2"),
            "the view never opened": ("open.probe", probe(frames=0), "open"),
            "the view was open for a moment only": ("open.probe", probe(frames=300), "open"),
            "the toggled view never opened": ("toggle.probe", "P 1 100 map close world 0\n", "toggle"),
            "a close left the view's camera": ("toggle.probe", probe(frames=2000) + CLOSED_NOW + CLOSED_OK + CLOSED_OK.replace("camera 1", "camera 0"), "toggle"),
            "a close left the keys trapped": ("toggle.probe", probe(frames=2000) + CLOSED_NOW + CLOSED_OK.replace("keytrap 0", "keytrap 1"), "toggle"),
            "no close was noted": ("toggle.probe", probe(frames=2000), "toggle"),
            "a quick reopen's close left the keys trapped": ("rapid.probe", probe(frames=2000) + CLOSED_NOW + (CLOSE_LINE + CLOSED_NOW) * 4 + CLOSE_LINE
                                                            + CLOSED_NOW.replace("keytrap 0", "keytrap 1"), "rapid"),
            "a close was not judged": ("rapid.probe", probe(frames=2000) + CLOSED_NOW + (CLOSE_LINE + CLOSED_NOW) * 4 + CLOSE_LINE, "rapid"),
            "settled with the return still running": ("toggle.probe", probe(frames=2000) + CLOSED_NOW + CLOSE_LINE + CLOSED_NOW
                                                      + CLOSED_SETTLED.replace("returning 0", "returning 1"), "toggle"),
            "shut with the cockpit hidden and no return": ("toggle.probe", probe(frames=2000) + CLOSED_NOW + CLOSE_LINE
                                                           + CLOSED_NOW.replace("returning 1", "returning 0"), "toggle"),
            "the control's view never opened": ("control.probe", probe(frames=0), "control"),
        }
        for name, (file, text, arm) in cases.items():
            with self.subTest(case=name):
                kept = (self.folder / file).read_text()
                write(self.folder, file, text)
                verdict = self.judge()
                self.assertFalse(verdict["passed"], name)
                self.assertFalse(verdict["arms"][arm]["passed"], name)
                write(self.folder, file, kept)
        self.assertTrue(self.judge()["passed"], "the folder is whole again")

    def test_a_view_that_changes_the_battle_is_reported_not_failed(self):
        # The original's drawing writes the battle, so the view may change it as turning the cockpit's view does; the same inputs twice may not.
        write(self.folder, "open.dump", dump(60, change=(41, 1)))
        write(self.folder, "open2.dump", dump(60, change=(41, 1)))
        write(self.folder, "toggle.dump", dump(60, change=(30, 2)))
        verdict = self.judge()
        self.assertTrue(verdict["passed"], verdict)
        self.assertTrue(verdict["arms"]["open"]["differs_from_never"])
        self.assertTrue(verdict["arms"]["toggle"]["differs_from_never"])

    def test_a_run_that_did_not_end_zero_fails_its_arm(self):
        codes = {a: 0 for a in self.arms}
        codes["toggle"] = 137
        verdict = self.judge(codes)
        self.assertFalse(verdict["passed"])
        self.assertFalse(verdict["arms"]["toggle"]["passed"])

    def test_noise_of_the_reference_is_left_out_for_every_arm(self):
        write(self.folder, "never2.dump", dump(60, change=(3, 3)))
        write(self.folder, "open.dump", dump(60, change=(3, 3)))
        verdict = self.judge()
        self.assertTrue(verdict["passed"], verdict)
        self.assertEqual((verdict["arms"]["never2"]["noise_lines"], verdict["arms"]["never2"]["noise_updates"]), (1, [3]))
        # the control that differs only at the noisy place has not differed
        write(self.folder, "control.dump", dump(60, change=(3, 3)))
        self.assertFalse(self.judge()["arms"]["control"]["passed"])

    def test_a_missing_file_is_a_failure(self):
        for file, arm in (("open.dump", "open"), ("open.probe", "open"), ("audit.probe", "audit"), ("leak.probe", "leak"), ("never.dump", "never")):
            with self.subTest(file=file):
                kept = (self.folder / file).read_text()
                (self.folder / file).unlink()
                verdict = self.judge()
                self.assertFalse(verdict["passed"])
                self.assertFalse(verdict["arms"][arm]["passed"])
                write(self.folder, file, kept)


if __name__ == "__main__":
    unittest.main()
