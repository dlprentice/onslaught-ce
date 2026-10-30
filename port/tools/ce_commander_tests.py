#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Commander mode (port/game/ce_commander.cpp, the order layer, and ce_cmdmap.cpp, the map; patches 0160 to 0162; build.py --commander-test): the static fences.

What these tests hold, without a game:
- The patches sit where the design puts them (the relaxed squad's goal hook after its centre moved and before its members are sent; the merge guard first in
  CanMerge; the map's key hook between photo mode's and the console's, its drawing after theirs, its frame and level-end calls after photo mode's), and the
  faithful view of the patched files, with CE_VERSION undefined, is exactly the text without the patches: every added line is in a CE_VERSION block.
- The test machinery is private: what only a test build has (the proof switch, the order tape, the probe, the diagnostics) sits under CE_COMMANDER_TEST, the
  define comes only from build.py's --commander-test, and a build without it compiles no environment switch, file or probe of the commander's (the one
  environment read left is the fence's list of the switches that mean "not a plain game").
- The order layer and the map draw no random number and allocate nothing in the game's heap; the map reaches the squads only through the order layer's face.
- The layout the order layer reads by hand (the squad member record) is checked at compile time; the table of squads says when it is full.
- The map's gate names every fence, and the switch reads ce.ini only where no fenced switch is set.
- The tape grammar and the probe columns in the order layer's header comment are the ones the code reads and writes.
Causal mutants of each must be caught. What the squads do with an order was measured by runs of a test build on the game's own levels (each order against
a run with none: the squad's goal, its fire and its units, tick by tick); the runs' own fences are black-box runs of the built game.
"""
import os
from pathlib import Path
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ce_photo_tests import faithful, patched  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
PATCHES = ("0160-commander-relaxed-goal.patch", "0161-commander-merge-guard.patch", "0162-commander-map-hooks.patch")
MODULE = ROOT / "port/game/ce_commander.cpp"
MAP = ROOT / "port/game/ce_cmdmap.cpp"
HEADER = ROOT / "port/game/ce_commander.h"
UPDATE = ROOT / "port/game/updatedump.cpp"
BUILD = ROOT / "port/build.py"
FILES = ("SquadRelaxed.cpp", "SquadNormal.cpp", "ltshell.cpp", "DXEngine.cpp", "game.cpp")

# What the map's gate (Allowed in ce_cmdmap.cpp) must name: the CE layer (faithful play), a recording, the network's boot intent, the demo and test modes, the front end,
# multiplayer and more than one player, the menus, a pause menu that is up, the free camera. And what the order layer's switch must name: the fenced environment.
GATE_CLAUSES = ("CeCmdMapOn()", "PortCeLayer()", "PortRecording()", "CE_BootRequested", "PLAYABLE_DEMO", "CLIPARAMS.mAutoConfigTest", "CLIPARAMS.mRecordDemo",
                "CLIPARAMS.mPlaybackDemo", "CLIPARAMS.mStressTest", "CLIPARAMS.mAttractMode", "FRONTEND.IsRunning()", "GAME_STATE_PLAYING", "GAME.IsMultiplayer()",
                "GAME.GetNPlayers() == 1", "GAMEINTERFACE.IsMenuUp()", "GAME.GetPauseMenu()->mActive", "GAME.IsFreeCameraOn(0)")
FENCED_ENV = ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN")
# The layout reads the order layer makes by hand, each checked where it is made.
LAYOUT_CHECKS = ("static_assert(sizeof(void *) == 4,", "static_assert(sizeof(CActiveReader<CUnit>) == 4 && sizeof(float) == 4,",
                 "static_assert(sizeof(MemberView) == 0x14 && offsetof(MemberView, unit) == 0,")


def comments_off(text):
    return "\n".join(l for l in text.split("\n") if not l.strip().startswith("//"))


def without_test(text):
    """What a build without CE_COMMANDER_TEST compiles: the `#ifdef CE_COMMANDER_TEST` branches dropped and their `#else` branches kept; other conditionals stay."""
    out, stack = [], []				# each entry: None (not ours) or [active]
    for line in text.split("\n"):
        d = line.strip()
        if re.match(r"#\s*ifdef\s+CE_COMMANDER_TEST\b", d):
            stack.append([False])
            continue
        if re.match(r"#\s*if", d):
            stack.append(None)
        elif re.match(r"#\s*else\b", d) and stack and stack[-1] is not None:
            stack[-1][0] = not stack[-1][0]
            continue
        elif re.match(r"#\s*endif\b", d) and stack:
            if stack.pop() is not None:
                continue
        if all(s is None or s[0] for s in stack):
            out.append(line)
    assert not stack, "an unterminated conditional"
    return "\n".join(out)


def gate(text):
    return text[text.index("bool Eligible()"):text.index("void Note(const char *text)")]


def switch(text):
    return text[text.index("void ReadConfiguration()"):text.index("// ---- the squads")]


def check_gates(map_text, module_text):
    g = gate(map_text)
    for clause in GATE_CLAUSES:
        assert clause in g, f"the map's gate lacks {clause}"
    for name in FENCED_ENV:
        assert f'"{name}"' in module_text, f"the order layer's fence lacks {name}"
    assert "bool NotYet()\n{\n\tif (!Eligible()) return false;" in g and "GAME_STATE_PRE_RUNNING" in g and "GAME_STATE_PANNING" in g, \
        "the note before the battle starts is given only where the map could exist, and only before the level plays"
    said, asked = map_text.find("if (NotYet())"), map_text.find("if (!Allowed()) return false;\n\tif (repeat) return true;")
    assert "\tif (NotYet())\n\t{\n\t\tif (!repeat) { Note(" in map_text and 0 <= said < asked, \
        "the open key says the battle has not started before it asks whether the map may open"
    assert "if (gNoteNotYet && !NotYet()) { gNote[0] = 0; gNoteNotYet = false; }" in map_text, "the note that the battle has not started goes when it has"
    shipped_map, shipped_module = without_test(map_text), without_test(module_text)
    assert "CE_CMD_STOP" not in shipped_map and "CE_CMD_STOP" not in shipped_module and "case V_STOP" not in shipped_module, \
        "the passive stop is a test verb: a squad that does not defend itself is a trap, so the shipped map and order layer hold none"
    assert "H, X: hold, fires" in map_text and "case DIK_X: Order(CE_CMD_HOLD" in map_text, "X holds, and the hints say so"
    sw = switch(module_text)
    shipped = without_test(sw)
    assert "const bool allowed = !Fenced();" in shipped, "a plain build is allowed only where no fenced switch is set"
    assert "const bool allowed = gProof || !Fenced();" in sw, "a test build's proof switch is the one way past the fence"
    assert re.search(r"gMap = allowed && \(proofMap \|\| CE_SettingGetBool\(\"commander\", 0\) != 0\);", shipped), "the map is on only where allowed, and by the ini"
    assert re.search(r"gEnabled = allowed && \(gMap", shipped), "the order layer is on only where allowed"
    assert sw.index("allowed &&") < sw.index("CE_SettingGetBool"), "the ini is read only where it is allowed"


def check_layout(module_text):
    for check in LAYOUT_CHECKS:
        assert check in module_text, f"the order layer's layout read lacks {check}"
    assert "0x304" not in module_text and "SetView" not in module_text and "NodeView" not in module_text, "no hand-made set layout and no hard-coded offset"
    assert "++gDropped" in module_text and "int CeCmdUntracked() { return gDropped; }" in module_text, "a full table is counted and said"


def function(text, head):
    start = text.index(head)
    nxt = re.search(r"\n(?:BOOL|void|int|bool|inline|static|FVector|float|SINT|CUnit)\b[^\n]*\(", text[start + len(head):])
    return text[start: start + len(head) + (nxt.start() if nxt else len(text))]


# The map's one change to DXEngine.cpp's PostRender: its own declaration (so that the patch does not lean on the lines other patches add beside it), the mirror-pass guard
# and the call, after the block that draws the interface and the notes of the other CE patches.
RENDER_BLOCK = r"#ifdef CE_VERSION\n\tvoid PortCeCmdRender\(\);[^\n]*\n\tif \(!gMirrorPass\)\n\t\tPortCeCmdRender\(\);[^\n]*\n#endif\n"


def check_keys(map_text):
    """Keys the map sees: those already held when it opens are held (their auto-repeats are not new presses), and the Esc that closes it is the map's until a new press."""
    opened = function(map_text, "void Open()")
    assert "for (int k = 0; k < 256; ++k) gDown[k] = LT.KeyDown[k] != 0;" in opened and "memset(gDown" not in opened, \
        "a key that is already held when the map opens is held, not a new press when it repeats"
    assert opened.index("gDown[k] = LT.KeyDown[k]") < opened.index("gDown[kOpenKey] = true;"), "the open key stays held whatever the game's state says"
    assert "\tLay();" in opened and opened.index("else FitView();") < opened.index("\tLay();"), "the layout exists once the view is set, before the first frame: a zoom in the same batch as the open key works on it"
    assert "else { gEscClosed = true; Close(); }" in map_text, "the Esc that closes the map is remembered"
    hook = function(map_text, "bool PortCeCmdKey(unsigned key, bool repeat)")
    assert hook.index("if (key == DIK_ESCAPE && gEscClosed)") < hook.index("if (key != kOpenKey"), "the repeats of that Esc are looked at before the open key's test"
    assert "if (!repeat) gEscClosed = false;" in hook and "else if (!LT.GetKeytrap()) return true;" in hook, \
        "its auto-repeats do not reach the game (they would open the pause menu), and a new press does"


def check_placement(files):
    relaxed = function(files["SquadRelaxed.cpp"], "void CRelaxedSquad::Unknown66(")
    hook = relaxed.index("int ceOrder = CeCommanderRelaxed(this, &mPos);")
    assert relaxed.index("mPos += d;") < hook < relaxed.index("ListIterator<CActiveReader<CUnit> >\tit(&mMembers);"), \
        "the goal hook comes after the squad moved its centre and before its members are sent"
    assert relaxed.index("if (ceOrder == 2)") > relaxed.index("for (CActiveReader<CUnit> *r = it.First(); r; r = it.Next())"), \
        "order 2 leaves the loop that sends members"
    assert relaxed.count("CeCommanderRelaxed(") == 1 and "EVENT_MANAGER.AddEvent(4002" in relaxed, "the squad's own next event is untouched"
    merge = function(files["SquadNormal.cpp"], "inline BOOL CNormalSquad::CanMerge()")
    assert re.match(r"inline BOOL CNormalSquad::CanMerge\(\)\n\{\n#ifdef CE_VERSION\n\tif \(CeCommanderNoMerge\(this\)\)[^\n]*\n\t\treturn FALSE;\n#endif\n\tif \(mUnknown10C > 0",
                    merge), "the merge guard is the first statement of CanMerge"
    # patch 0162: the map's key goes at the end of the CE key chain (after photo mode's and the console's), before the key trap sees the key
    keys = files["ltshell.cpp"]
    photo = keys.index("if (PortCePhotoKey(key,")
    cmd = keys.index("else if (PortCeCmdKey(key, (lParam & 0x40000000) != 0)) {}", photo)
    assert photo < keys.index("else if (PortCePaletteSuppressDown(key))", photo) < keys.index("else if (PortCePaletteOpen(key,", photo) < cmd < keys.index("if (mCurrentKeytrap)", photo), \
        "the map's open key is taken after photo mode's and the console's and before the key trap"
    assert keys.count("PortCeCmdKey(") == 2, "one declaration and one call"
    engine = files["DXEngine.cpp"]
    assert re.search(r"\t\}\n\n" + RENDER_BLOCK + r"\tPLATFORM\.EndScene\(\);", engine), \
        "the map is drawn after everything the frame's interface block drew, outside the mirror pass, inside the scene"
    assert engine.count("PortCeCmdRender(") == 2, "one declaration and one call"
    game = files["game.cpp"]
    assert re.search(r"\tPortCePhotoFrame\(\);[^\n]*\n\tPortCeCmdFrame\(\);[^\n]*\n#endif\n", game), "the map's frame call follows photo mode's"
    assert re.search(r"\tPortCePhotoLevelEnd\(\);[^\n]*\n\tPortCeCmdLevelEnd\(\);[^\n]*\n#endif\n", game), "the map closes after photo mode ends, before anything is freed"
    assert game.count("PortCeCmdFrame(") == 2 and game.count("PortCeCmdLevelEnd(") == 2, "one declaration and one call each"


class Static(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        base = Path(cls.tmp.name)
        cls.with_ = patched(base / "with", FILES)
        cls.without = patched(base / "without", FILES, skip=PATCHES)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_hooks_are_where_the_design_puts_them(self):
        check_placement(self.with_)

    def test_the_faithful_view_is_the_text_without_the_patches(self):
        for name in FILES:
            self.assertNotEqual(self.with_[name], self.without[name], f"{name}: the patches change nothing")
            self.assertEqual(faithful(self.with_[name]), faithful(self.without[name]), f"{name}: the faithful build (CE_VERSION undefined) is not the unpatched text")

    def test_the_test_machinery_is_private(self):
        for path in (MODULE, MAP, HEADER):
            text = path.read_text()
            body = [l for l in text.split("\n") if l.strip() and not l.lstrip().startswith("//")]
            first = body[0].strip() if path != HEADER else next(l for l in body if l.startswith("#ifdef"))
            self.assertEqual(first, "#ifdef CE_VERSION", path.name)
            self.assertEqual(body[-1].strip(), "#endif", path.name)
        update = UPDATE.read_text()
        self.assertNotIn("CE_COMMANDER_TEST", update, "updatedump.cpp: the one call is the order layer's, in every CE build")
        self.assertIn("#ifdef CE_VERSION\n#include \"ce_commander.h\"\n#endif", update)
        self.assertIn("#ifdef CE_VERSION\n\tCeCommanderUpdate();", update)
        build = BUILD.read_text()
        self.assertEqual(len(re.findall(r"CE_COMMANDER_TEST", build)), 1, "only build.py's flag defines it")
        self.assertIn('if a.commander_test:\n        CE_DEFINES.append("-DCE_COMMANDER_TEST=1")', build)
        users = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "port").rglob("*")
                       if p.is_file() and p.suffix in (".cpp", ".h", ".py", ".patch", ".c") and "CE_COMMANDER_TEST" in p.read_text(errors="ignore"))
        self.assertEqual(users, ["port/build.py", "port/game/ce_cmdmap.cpp", "port/game/ce_commander.cpp", "port/game/ce_commander.h",
                                 "port/tools/ce_commander_tests.py"])

    def test_a_plain_build_has_no_commander_environment_switch_file_or_probe(self):
        module = without_test(comments_off(MODULE.read_text()))
        for word in ("BEA_PORT_COMMANDER", "fopen", "fprintf", "PortPresentCount", "gOut", "gProbePath", "gTapePath", "Parse(", "Probe(", "Apply(", "kVerbName", "CeCmdWorldHash",
                     "CeCmdWorldDump", "SeedUniformRandomNumberStream", "RngValue"):
            self.assertNotIn(word, module, f"a build without the test define still compiles {word!r} of the order layer")
        self.assertEqual(re.findall(r"getenv\(([^)]*)\)", module), ["name"], "the one environment read is the fence's list")
        map_text = without_test(comments_off(MAP.read_text()))
        for word in ("BEA_PORT_COMMANDER", "getenv", "clock_gettime", "time.h", "CeCmdWorldHash", "CeCmdWorldDump", "CeCmdHash", "fopen"):
            self.assertNotIn(word, map_text, f"a build without the test define still compiles {word!r} of the map")

    def test_the_order_layer_draws_no_random_number_and_allocates_nothing(self):
        text = without_test(comments_off(MODULE.read_text()))
        for word in ("FloatRandom", "IntRandom", "rand(", "srand(", "Random(", "new ", "malloc", "calloc", "realloc", "delete ", "std::", "MEM_MANAGER"):
            self.assertNotIn(word, text, f"the order layer names {word!r}")

    def test_the_map_draws_no_random_number_and_allocates_nothing(self):
        text = comments_off(MAP.read_text())
        for word in ("FloatRandom", "IntRandom", "rand(", "srand(", "Random(", "malloc", "calloc", "realloc", "delete ", "std::", "MEM_MANAGER", "<new>"):
            self.assertNotIn(word, text, f"the map names {word!r}")
        self.assertEqual(re.findall(r"\bnew\s*[A-Za-z_\[(]", text), [], "no new: the controller sink is a global, as the game's own reconnect interfaces are")

    def test_the_layout_reads_are_checked_at_compile_time(self):
        check_layout(MODULE.read_text())

    def test_the_map_gate_names_every_fence(self):
        check_gates(MAP.read_text(), MODULE.read_text())

    def test_the_map_reaches_the_squads_only_through_the_order_layer(self):
        text = MAP.read_text()
        for word in ("WORLD.", "CNormalSquad", "CRelaxedSquad", "CSquad", "GoToPoint", "SetAIState", "Unknown85", "mUnknown", "SetSquadTarget"):
            self.assertNotIn(word, text, f"the map names {word!r}: orders and squads belong to the order layer")
        for call in ("CeCmdList(", "CeCmdIssue(", "CeCmdNote(", "CeCmdUntracked("):
            self.assertIn(call, text)
        self.assertIn("more squads are not shown", text, "the map says when the order layer's table was full")

    def test_the_grammar_and_the_columns_are_the_documented_ones(self):
        text = MODULE.read_text()
        head = text[:text.index("#ifdef CE_VERSION")]
        verbs = head[head.index("Verbs (a squad S"):head.index("// Probe (")]
        for verb in re.findall(r"^//   (\w+) ", verbs, re.M):
            self.assertTrue(f'"{verb}"' in text, f"the header comment documents {verb}, the code has no such verb")
        for verb in sorted(set(re.findall(r'!strcmp\(v, "(\w+)"\)', text))):
            self.assertIn(f"//   {verb} ", verbs, f"the code reads {verb}, the header comment does not document it")
        self.assertIn("Q ordinal class(N|R) side state defensive forced members x y z centre-x centre-y goal-x goal-y target slot83 script verb members-with-target members-able-to-fire", head)
        self.assertEqual(len(re.findall(r'"Q %d ', text)), 1)
        self.assertEqual(text.count("%d %c %d %d %d %d %d %.3f %.3f %.3f %.3f %.3f %.3f %.3f %s %d %d %s %d %d"), 1)
        self.assertNotIn("AI_ON and the go-to-point; the layer puts the goal back when the squad has no target.", head, "amove is documented as the verb that loses its goal")


class Mutants(unittest.TestCase):
    """Each mutation of the patched text or the sources must break the check that guards it."""

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.files = patched(Path(cls.tmp.name) / "with", FILES)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def mutate(self, name, old, new):
        files = dict(self.files)
        self.assertEqual(files[name].count(old), 1, f"{name}: {old!r}")
        files[name] = files[name].replace(old, new)
        return files

    def test_relaxed_hook_before_the_centre_moves(self):
        files = self.mutate("SquadRelaxed.cpp", "#ifdef CE_VERSION\n\tint ceOrder = CeCommanderRelaxed(this, &mPos);",
                            "#ifdef CE_VERSION\n\tint ceOrder = 0;")
        files["SquadRelaxed.cpp"] = files["SquadRelaxed.cpp"].replace("\tCThing\t*target = NULL;", "#ifdef CE_VERSION\n\tint ceEarly = CeCommanderRelaxed(this, &mPos);\n#endif\n\tCThing\t*target = NULL;", 1)
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_relaxed_loop_not_left(self):
        files = self.mutate("SquadRelaxed.cpp", "#ifdef CE_VERSION\n\t\tif (ceOrder == 2)\n\t\t\tbreak;\n#endif\n", "")
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_merge_guard_not_first(self):
        files = self.mutate("SquadNormal.cpp", "#ifdef CE_VERSION\n\tif (CeCommanderNoMerge(this))\t// patch 0161: a squad the commander module holds an order for is not dissolved into another\n\t\treturn FALSE;\n#endif\n\tif (mUnknown10C > 0",
                            "\tif (mUnknown10C > 0")
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_map_key_before_the_console(self):
        call = "\t\t\telse if (PortCeCmdKey(key, (lParam & 0x40000000) != 0)) {}\t// patch 0162: the commander map's open key\n"
        files = self.mutate("ltshell.cpp", call, "")
        files["ltshell.cpp"] = files["ltshell.cpp"].replace("\t\t\telse if (PortCePaletteSuppressDown(key)) {}\n", call + "\t\t\telse if (PortCePaletteSuppressDown(key)) {}\n", 1)
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_map_key_after_the_key_trap(self):
        files = self.mutate("ltshell.cpp", "\t\t\telse if (PortCeCmdKey(key, (lParam & 0x40000000) != 0)) {}\t// patch 0162: the commander map's open key\n", "")
        files["ltshell.cpp"] = files["ltshell.cpp"].replace("\t\t\tif (mCurrentKeytrap)\n\t\t\t{\n\t\t\t\tmCurrentKeytrap(key,KEY_EVENT_DOWN);",
                                                            "\t\t\tif (PortCeCmdKey(key, false)) {} else\n\t\t\tif (mCurrentKeytrap)\n\t\t\t{\n\t\t\t\tmCurrentKeytrap(key,KEY_EVENT_DOWN);", 1)
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def render_block(self):
        return re.search(RENDER_BLOCK, self.files["DXEngine.cpp"]).group(0)

    def test_map_drawn_before_the_console(self):
        block = self.render_block()
        files = self.mutate("DXEngine.cpp", block, "")
        files["DXEngine.cpp"] = files["DXEngine.cpp"].replace("\t\tGAMEINTERFACE.Render();\n", "\t\tPortCeCmdRender();\n\t\tGAMEINTERFACE.Render();\n", 1)
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_map_drawn_after_the_scene_ends(self):
        block = self.render_block()
        files = self.mutate("DXEngine.cpp", block + "\tPLATFORM.EndScene();\n", "\tPLATFORM.EndScene();\n" + block)
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_map_drawn_in_the_mirror_pass(self):
        files = self.mutate("DXEngine.cpp", "\tif (!gMirrorPass)\n\t\tPortCeCmdRender();", "\tPortCeCmdRender();")
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_map_not_closed_with_the_level(self):
        files = self.mutate("game.cpp", "\tPortCeCmdLevelEnd();\t\t// patch 0162: the commander map closes with the level\n", "")
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_an_unguarded_hook_changes_the_faithful_view(self):
        # a hook outside the CE_VERSION conditional is a difference the faithful build would compile
        text = self.files["SquadNormal.cpp"].replace("#ifdef CE_VERSION\n\tif (CeCommanderNoMerge(this))", "\tif (0)\n\tif (CeCommanderNoMerge(this))", 1)
        self.assertNotEqual(faithful(text), faithful(self.files["SquadNormal.cpp"]))

    def test_gate_without_attract_mode(self):
        text = MAP.read_text()
        self.assertEqual(text.count("CLIPARAMS.mAttractMode"), 1)
        with self.assertRaises(AssertionError):
            check_gates(text.replace("CLIPARAMS.mAttractMode || ", ""), MODULE.read_text())

    def test_gate_without_the_ce_layer(self):
        text = MAP.read_text()
        with self.assertRaises(AssertionError):
            check_gates(text.replace("!PortCeLayer() || ", "", 1), MODULE.read_text())

    def test_note_given_where_the_map_cannot_exist(self):
        text = MAP.read_text()
        old = "bool NotYet()\n{\n\tif (!Eligible()) return false;\n"
        self.assertEqual(text.count(old), 1)
        with self.assertRaises(AssertionError):
            check_gates(text.replace(old, "bool NotYet()\n{\n"), MODULE.read_text())

    def test_stale_not_yet_note_stays(self):
        text = MAP.read_text()
        old = "\tif (gNoteNotYet && !NotYet()) { gNote[0] = 0; gNoteNotYet = false; }"
        self.assertEqual(text.count(old), 1)
        with self.assertRaises(AssertionError):
            check_gates(text.replace(old, "\t(void)0;"), MODULE.read_text())

    def test_open_key_asks_the_level_before_the_note(self):
        text = MAP.read_text()
        said = "\tif (NotYet())\n\t{\n\t\tif (!repeat) { Note(\"The battle has not started yet.\"); gNoteNotYet = true; }\n\t\treturn true;\n\t}\n"
        self.assertEqual(text.count(said + "\tif (!Allowed()) return false;\n"), 1)
        with self.assertRaises(AssertionError):
            check_gates(text.replace(said + "\tif (!Allowed()) return false;\n", "\tif (!Allowed()) return false;\n" + said), MODULE.read_text())

    def test_the_keys_the_map_sees(self):
        check_keys(MAP.read_text())

    def test_a_key_held_when_the_map_opens_repeats_as_a_press(self):
        text = MAP.read_text()
        old = "for (int k = 0; k < 256; ++k) gDown[k] = LT.KeyDown[k] != 0;"
        self.assertEqual(text.count(old), 1)
        with self.assertRaises(AssertionError):
            check_keys(text.replace(old, "memset(gDown, 0, sizeof(gDown));"))

    def test_a_zoom_in_the_batch_of_the_open_key_works_on_no_layout(self):
        text = MAP.read_text()
        old = "\tLay();\t\t// the layout exists before the first frame draws it"
        self.assertEqual(text.count(old), 1)
        with self.assertRaises(AssertionError):
            check_keys(text.replace(old, "\t// the layout exists before the first frame draws it"))

    def test_the_repeats_of_the_esc_that_closed_the_map_reach_the_game(self):
        text = MAP.read_text()
        for old, new in (("else { gEscClosed = true; Close(); }", "else Close();"),
                         ("else if (!LT.GetKeytrap()) return true;", "else if (!LT.GetKeytrap()) return false;"),
                         ("if (!repeat) gEscClosed = false;", "gEscClosed = false;")):
            self.assertEqual(text.count(old), 1)
            if old == "if (!repeat) gEscClosed = false;":
                continue		# a press that is new clears it; clearing on every message too would only be the first mutant over again
            with self.assertRaises(AssertionError):
                check_keys(text.replace(old, new))

    def test_a_passive_stop_in_the_shipped_map(self):
        text = MAP.read_text()
        old = "case DIK_X: Order(CE_CMD_HOLD, 0.f, 0.f); break;"
        self.assertEqual(text.count(old), 1)
        with self.assertRaises(AssertionError):
            check_gates(text.replace(old, "case DIK_X: Order(CE_CMD_HOLD + 0 * CE_CMD_STOP, 0.f, 0.f); break;"), MODULE.read_text())

    def test_a_passive_stop_in_the_shipped_order_layer(self):
        text = MODULE.read_text()
        old = "#ifdef CE_COMMANDER_TEST\n\t\tcase V_STOP: n->SetAIState(AI_OFF); n->GoToPoint(goal, FALSE); break;\n"
        self.assertEqual(text.count(old), 1)
        with self.assertRaises(AssertionError):
            check_gates(MAP.read_text(), text.replace(old, "\t\tcase V_STOP: n->SetAIState(AI_OFF); n->GoToPoint(goal, FALSE); break;\n#ifdef CE_COMMANDER_TEST\n"))

    def test_switch_without_the_fence(self):
        text = MODULE.read_text()
        self.assertEqual(text.count("const bool allowed = !Fenced();"), 1)
        with self.assertRaises(AssertionError):
            check_gates(MAP.read_text(), text.replace("const bool allowed = !Fenced();", "const bool allowed = true;"))

    def test_switch_reading_the_ini_before_the_fence(self):
        text = MODULE.read_text()
        old = "gMap = allowed && (proofMap || CE_SettingGetBool(\"commander\", 0) != 0);"
        self.assertEqual(text.count(old), 1)
        with self.assertRaises(AssertionError):
            check_gates(MAP.read_text(), text.replace(old, "gMap = (CE_SettingGetBool(\"commander\", 0) != 0 && allowed) || proofMap;"))

    def test_member_record_without_its_size_check(self):
        text = MODULE.read_text()
        self.assertEqual(text.count("static_assert(sizeof(MemberView) == 0x14"), 1)
        with self.assertRaises(AssertionError):
            check_layout(text.replace("static_assert(sizeof(MemberView) == 0x14", "static_assert(sizeof(MemberView) >= 0x10"))

    def test_a_full_table_that_goes_unsaid(self):
        text = MODULE.read_text()
        self.assertEqual(text.count("++gDropped"), 1)
        with self.assertRaises(AssertionError):
            check_layout(text.replace("++gDropped", "(void)0"))


if __name__ == "__main__":
    unittest.main()
