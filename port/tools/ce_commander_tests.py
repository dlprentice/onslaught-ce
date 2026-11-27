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
- The live view cannot change the battle by construction: its picture (ce_cmdview.cpp) names only the game facilities on its list of reads, calls none of the
  game's drawing, allocation, random or clock routines, and takes its memory from the C library; the map sends its own boxes and text to that picture while it
  is drawn and asks for none of the game's drawing state; the engine's presentation hook hands back the camera it is given; and no patch gives the Commander
  an entry into the game's render beyond the five it has (a new one must be put on the list here on purpose). The runs that measure it (the update dumps of a
  run with the view open against one that never opened it, and the footprint audit) are made with port/tools/ce_commander_isolation_run.py.
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
from ce_palette_tests import sections  # noqa: E402
from ce_photo_tests import faithful, patched  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
PATCHES = ("0160-commander-relaxed-goal.patch", "0161-commander-merge-guard.patch", "0162-commander-map-hooks.patch")
# Patches whose hunks in these files carry no context lines (`@@ -N,0 +M,K @@`, patch 0189's campaign outcome hooks). patch places such a hunk by its line number alone, so in a tree that lacks the
# commander patches' added lines the hook lands a few lines away from where it belongs and the two texts differ for a reason that is not the commander's. They are left out of both trees of the
# faithful comparison; their own patches' suites check them.
# Patches whose hunks anchor on lines the commander patches add (the native editor's lifecycle hooks sit beside the commander map's in game.cpp and ltshell.cpp, and the
# museum visit's beside the editor's): in the tree without the commander patches they cannot apply. They are left out of both trees as well, and their own suites check them.
BUILT_ON_COMMANDER = (
    "0206-native-editor-lifecycle.patch",
    "0207-ce-museum-visit-lifecycle.patch",
    # The exhibition render hunk also needs the editor lifecycle's context in DXEngine.cpp.
    "0265-ce-authored-exhibition-world.patch",
    "0381-ce-perf-surf-museum-fixes.patch",
    "0385-ce-flagship-duel.patch",
    "0397-ce-audio-pause-museum-shader-fixes.patch",
    "0398-ce-bugclip-frontend-overlay.patch",
)
POSITIONAL = ()    # none since patch 0189 carries context (port/tools/patch_control_body_tests.py refuses a hunk without any)
# The CE hook points (patches 0300-0319, port/game/ce_hooks.h): each adds one call into an empty, ordered table, and their
# include hunks rest on the exact lines of the whole queue, so in the tree without the commander patches they cannot apply.
# They change nothing the commander's faithful comparison measures (no handler is registered); ce_hooks_tests.py checks them.
HOOK_SITES = tuple(sorted(p.name for p in (Path(__file__).resolve().parents[1] / "patches").glob("03[0-9][0-9]-ce-hook-*.patch")))
LEFT_OUT = (*POSITIONAL, *BUILT_ON_COMMANDER, *HOOK_SITES)
OVERLAY_HOOK = r"(?:\tif \(!gMirrorPass\) ce_hook_draw_overlay\(\);[^\n]*\n)?" + r"(?:#ifdef CE_VERSION\n\tif \(!gMirrorPass\) \{ extern void PortCeBugClipRender\(\); PortCeBugClipRender\(\); \}\n#endif\n)?"   # optional hook (0311) + bugclip overlay (0398)
MODULE = ROOT / "port/game/ce_commander.cpp"
MAP = ROOT / "port/game/ce_cmdmap.cpp"
VIEW = ROOT / "port/game/ce_cmdview.cpp"
VIEW_HEADER = ROOT / "port/game/ce_cmdview.h"
VIEW_AUDIT = ROOT / "port/game/ce_cmdview_audit.cpp"
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


def without_nonallocating_std(text):
    # Scalar IDs and these math functions allocate no objects. All other std
    # facilities remain subject to the existing namespace/allocation ban.
    text = text.replace("std::numeric_limits<float>::quiet_NaN", "")
    return re.sub(r"\bstd::(?:min|max|exp|ceil|fabs|isfinite|log|uint64_t)\b", "", text)


def map_resource_violations(text):
    text = without_nonallocating_std(comments_off(text))
    # The one allocation the map makes: the paused map's terrain texture, made once when it first draws (the frame-feel lane) and kept for the process.
    text = text.replace("new (MEMTYPE_TEXTURE) CDXTexture;", "", 1)
    forbidden = ("FloatRandom", "IntRandom", "rand(", "srand(", "Random(", "malloc", "calloc", "realloc",
                 "delete ", "std::", "MEM_MANAGER", "<new>")
    found = [word for word in forbidden if word in text]
    if re.search(r"\bnew\s*[A-Za-z_\[(]", text):
        found.append("new")
    return found


# ---- the live view's unit projection helper (ce_cmdview.cpp) ------------------------------------------------------------------------------------------------
# Everything ce_cmdview.cpp may take from the game, by the object it is read through: the members named here only read. The file's own READS comment is held to
# the same list.
VIEW_READS = {
    "WORLD": {"GetThingNB"},
    "PLATFORM": {"Font"},
}
# Calls made through a pointer or a reference to one of the game's objects (a thing, a unit, the font): each only reads.
VIEW_GAME_CALLS = {"GetThingType", "GetFlags", "GetPos", "GetAllegiance", "GetHeight"}
# Direct3D device objects are no longer touched by ce_cmdview.cpp (Commander renders the native 3D scene inside CeRenderFence).
VIEW_DEVICE_OBJECTS = ()
# What the picture must never name: the game's drawing and its render records, its allocator, its random numbers and clocks, the routines of the engine's
# render, and the two readers of the height field that write as they read (the extents are measured into the map object, the lit texel routine fills its
# scratch buffer).
VIEW_FORBIDDEN = ("CSPRITERENDERER", "FASTVB", "RENDERINFO", "RENDERQUEUE", "STATE.", "LT.SRS", "LT.STS", "LT.SSS", "LT.D3D_", "CVBufTexture", "DrawText", "GetTextExtent",
                  "InitFromTexture", "InitFromSysFont", "CanDisplay", "MEMTYPE_", "MEM_MANAGER", "FloatRandom", "IntRandom", "rand(", "srand(", "Random(", "GetSysTime",
                  "PortClock", "clock_gettime", "EVENT_MANAGER", "GetRenderFrameLength", "PARTICLE_MANAGER", "SHADOWS.", "CIMPOSTER", "TREES.", "CRTMesh", "CMeshRenderer",
                  "CAtmospherics", "CDebris", "MAP_WHO", "SetViewpoint", "SelectViewpoint", "GetExtents", "Unknown47eff0", "GetCacheTexels", "SetPos(", "SetFlags(",
                  "MakeVisible", "MakeInvisible", "SetAllegiance", "ListIterator", ".First()", ".Next()", "getenv", "fopen", "std::")


def view_violations(text):
    """Every way the picture's source steps outside its list of reads (an empty list: none)."""
    text = comments_off(text)
    found = [f"names {word}" for word in VIEW_FORBIDDEN if re.search((r"\b" if re.match(r"\w", word) else "") + re.escape(word), text)]
    if re.search(r"\bnew\b", text):
        found.append("uses operator new (the game's allocator in a game file)")
    if re.search(r"\bdelete\b", text):
        found.append("uses operator delete")
    # the game's objects: every upper-case object named with a member must be on the list, with a member on its list
    for name, member in re.findall(r"\b([A-Z][A-Z_]{1,})\s*\.\s*([A-Za-z_]\w*)", text):
        if name not in VIEW_READS:
            found.append(f"reads through {name}")
        elif member not in VIEW_READS[name]:
            found.append(f"reads {name}.{member}")
    # calls through pointers: the device's and the picture's own objects, or a reading call of the game's
    for owner, call in re.findall(r"\b([A-Za-z_]\w*)\s*->\s*([A-Za-z_]\w*)\s*\(", text):
        # The unit's squad reader only exposes its current pointer; drawing keeps the ordinal.
        if owner == "unit" and call == "GetSquad":
            continue
        if owner in VIEW_DEVICE_OBJECTS:
            continue
        if call not in VIEW_GAME_CALLS:
            found.append(f"calls {owner}->{call}")
    for owner, call in re.findall(r"\b([a-z]\w*)\s*\.\s*([A-Z]\w*)\s*\(", text):
        if call not in VIEW_GAME_CALLS and call not in ("MakeRotationPitchF", "TransposeInPlace"):
            found.append(f"calls {owner}.{call}")
    return sorted(set(found))


def view_reads_documented(text):
    """The READS comment at the top of the picture's source names every object and member on the list, and no object that is not on it."""
    block = text[text.index("// READS ("):text.index("namespace\n{")]
    missing = [f"{name}.{member}" for name, members in VIEW_READS.items() for member in members
               if not re.search(r"//\s+" + name + r"\b", block) or member not in block]
    listed = set(re.findall(r"(?m)^//   ([A-Z][A-Z_]+)\s", block))
    return missing + sorted(f"lists {name}" for name in listed - set(VIEW_READS))


def marked_block(text, begin, end):
    assert text.count(begin) == 1 and text.count(end) == 1, f"the markers {begin} .. {end} are not there once each"
    return text[text.index(begin):text.index(end)]


# The game's drawing and render state, which the live view's frame in the map must not ask for.
MAP_GAME_DRAWING = ("BeginDraw", "EndDraw", "CSPRITERENDERER", "FASTVB", "RENDERINFO", "LT.SRS", "LT.STS", "gFont->Draw", "GetTextExtent", "SetViewpoint", "ENGINE.Render",
                    "PortCeDrawHooks", "PortCeDrawsDropped", "CePhotoDrawScope")


def map_live_view_violations(text):
    text = comments_off(text)
    picture = function(text, "bool StrategicPictureBegin")
    hook = function(text, "CCamera* PortCeCmdRenderCamera")
    token = function(text, "uint64_t PortCeCmdWorldViewToken")
    photo = function(text, "bool PortCeCmdSuppressFlightHud")
    failures = []
    if "CeWorldViewRead(" not in picture:
        failures.append("native world snapshot is missing")
    if "CeCmdViewCollectUnits(" not in picture:
        failures.append("native member selection projection is missing")
    if "CeCmdViewBegin(" in picture or "CeCmdViewWorld(" in picture:
        failures.append("replacement scenery paints over the native scene")
    for guard in ("camera!=gStrategicOriginal", "gOpen", "gStrategicRender", "gStrategicOwner.Active()", "player!=GAME.GetPlayer(0)"):
        if guard not in hook:
            failures.append("native camera lacks an ownership guard")
    if "StrategicFrameCamera()" not in hook:
        failures.append("native renderer receives a stale camera")
    if "gStrategicOwner.Active()" not in token or "gStrategicViewSession" not in token:
        failures.append("native snapshot owner is not bound to its session")
    if "return false;" not in photo:
        failures.append("generic photo draw filtering became active")
    return failures



# The Commander's entries into the game's own code, by patch: the key, the drawing after the frame, the frame and level-end calls, and the presentation hook
# that hands its camera back. A patch that gives it another (an entry into the render above all) fails here until this list is changed on purpose.
COMMANDER_ENTRIES = {'PortCeCmdFrame', 'PortCeCmdHideCockpit', 'PortCeCmdKey', 'PortCeCmdLevelEnd', 'PortCeCmdRender', 'PortCeCmdRenderCamera', 'PortCeCmdRenderWorld', 'PortCeCmdWorldViewToken', 'PortCeCmdThingGone', 'PortCeCmdGuidePoint'}


def commander_entries(patch_texts):
    names = set()
    for text in patch_texts:
        for line in text.split("\n"):
            if line.startswith("+") and not line.startswith("+++"):
                names.update(re.findall(r"\b(PortCeCmd[A-Za-z0-9_]*|CeCmdView[A-Za-z0-9_]*|CeCmd(?:Save|Restore)[A-Za-z0-9_]*)\b", line))
    return names


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
    assert re.search(r"gMap = allowed && \(proofMap \|\| PortCeExperimental\(\"commander\"\)\);", shipped), \
        "the map is on only where allowed, and by the commander line of the ini (ce.cpp PortCeExperimental, read once)"
    assert re.search(r"gEnabled = allowed && \(gMap", shipped), "the order layer is on only where allowed"
    assert sw.index("allowed &&") < sw.index('PortCeExperimental("commander")'), "the ini is read only where it is allowed"
    assert "CE_SettingGetBool" not in shipped, "the commander line is read at one place, ce.cpp's PortCeExperimental"


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
# The map's call alone under the mirror-pass test (this patch by itself), or braced together with the map editor's overlay
# call (patch 0206): never the editor call after the unbraced test, where it would run in the mirrored pass too.
RENDER_BLOCK = (r"#ifdef CE_VERSION\n\tvoid PortCeCmdRender\(\);[^\n]*\n\tif \(!gMirrorPass\)\n"
                r"(?:\t\tPortCeCmdRender\(\);[^\n]*\n|\t\{\n\t\tPortCeCmdRender\(\);[^\n]*\n\t\tPortCeEditorRender\(\);[^\n]*\n\t\}\n)#endif\n")


def check_keys(map_text):
    """Keys the map sees: those already held when it opens are held (their auto-repeats are not new presses), and the Esc that closes it is the map's until a new press."""
    opened = function(map_text, 'void Open(bool strategic)')
    assert "for (int k = 0; k < 256; ++k) gDown[k] = LT.KeyDown[k] != 0;" in opened and "memset(gDown" not in opened, \
        "a key that is already held when the map opens is held, not a new press when it repeats"
    assert opened.index("gDown[k] = LT.KeyDown[k]") < opened.index("gCloseKey = OpenKey();") < opened.index("gDown[gCloseKey] = true;"), "the open key stays held whatever the game's state says"
    assert "gLooseBounds = strategic;" in opened and "gKeptMapLevel != GAME.GetCurrentLevel()" in opened and "gKeptLevel != GAME.GetCurrentLevel()" in opened, \
        "the 2D tactical map resets loose bounds and keeps its own fitted state apart from the 3D live view"
    assert "else FitView();\n\tLay();" in opened, "the layout exists once the view is set, before the first frame: a zoom in the same batch as the open key works on it"
    closed = function(map_text, "void Close()\n{")
    assert "if (wasStrategic)" in closed and "gKeptLevel = GAME.GetCurrentLevel();" in closed and "gKeptMapLevel = GAME.GetCurrentLevel();" in closed, \
        "closing saves the 3D live view and the 2D tactical map into separate kept records"
    ended = function(map_text, "void PortCeCmdLevelEnd()\n{")
    assert "\tgKeptLevel = -1;" in ended and "\tgKeptMapLevel = -1;" in ended, "level end resets both 3D and 2D kept views"
    assert "else { gEscClosed = true; Close(); }" in map_text, "the Esc that closes the map is remembered"
    hook = function(map_text, "bool PortCeCmdKey(unsigned key, bool repeat)")
    assert hook.index("if (key == DIK_ESCAPE && gEscClosed)") < hook.index("key != OpenKey()"), "the repeats of that Esc are looked at before the open key's test"
    assert "if (!repeat) gEscClosed = false;" in hook and "else if (!LT.GetKeytrap()) return true;" in hook, \
        "its auto-repeats do not reach the game (they would open the pause menu), and a new press does"


def check_placement(files, hooks_text=None):
    if hooks_text is None:
        hooks_text = (ROOT / "port/game/ce_hooks.cpp").read_text()
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
    # key_capture: the map's key goes at the end of the CE key chain (after photo mode's and the console's), before the key trap sees the key
    keys = files["ltshell.cpp"]
    keydown = keys[keys.index("if( WM_KEYDOWN == uMsg )"):keys.index("WM_KEYUP == uMsg")]
    hook_key = keydown.index("if (ce_hook_key_capture(key, (lParam & 0x40000000) != 0)) {}")
    assert hook_key < keydown.index("if (mCurrentKeytrap)"), "the CE key hook runs before the key trap"
    key_table = hooks_text[hooks_text.index("kKeyCapture[]"):hooks_text.index("CE_HOOK_CHECK(kKeyCapture")]
    assert key_table.index("CeHookPhotoKey") < key_table.index("CeHookPaletteSuppressDown") < key_table.index("CeHookPaletteOpen") < key_table.index("CeHookCmdKey"), \
        "the map's open key is taken after photo mode's and the console's and before the key trap"
    engine = files["DXEngine.cpp"]
    # The CE hook point draw_overlay (patch 0311, port/game/ce_hooks.h) may sit between the map and EndScene: it runs after
    # every CE overlay, the map included, by design (HOOK-MAP section 2.9).
    assert re.search(r"\t\}\n\n" + RENDER_BLOCK + OVERLAY_HOOK + r"\tPLATFORM\.EndScene\(\);", engine), \
        "the map is drawn after everything the frame's interface block drew, outside the mirror pass, inside the scene"
    assert engine.count("PortCeCmdRender(") == 2, "one declaration and one call"
    game = files["game.cpp"]
    assert re.search(r"\tPortCePhotoFrame\(\);[^\n]*\n\tPortCeCmdFrame\(\);[^\n]*\n#endif\n", game), "the map's frame call follows photo mode's"
    assert "ce_hook_level_end(" in function(game, "void\tCGame::ShutdownRestartLoop()"), "level_end hook is called before anything is freed"
    end_table = hooks_text[hooks_text.index("kLevelEnd[]"):hooks_text.index("CE_HOOK_CHECK(kLevelEnd")]
    assert end_table.index("CeHookPhotoLevelEnd") < end_table.index("CeHookCmdLevelEnd"), "the map closes after photo mode ends, before anything is freed"
    assert game.count("PortCeCmdFrame(") == 2, "one declaration and one call"


# The ICE link receipt (build.py, write_ice_link_receipt) marks a test build via test_build_switches(CE_DEFINES) (which reads TEST_BUILD_SWITCHES) or TEST_KIT.
RECEIPT_READ = re.compile(r'test_build=bool\(test_build_switches\(CE_DEFINES\)\)\s+or\s+TEST_KIT')
# The packager's table of test switches (build.py, TEST_BUILD_SWITCHES) also reads the defines a build was given, to mark the build folder as a test build; it defines nothing.
SWITCH_TABLE_READ = re.compile(r'(?m)^TEST_BUILD_SWITCHES = \(\("-DCE_COMMANDER_TEST", "commander-test"\)')


def check_define_sites(build):
    """Where build.py names CE_COMMANDER_TEST (raises AssertionError): its --commander-test flag is the one place that defines it, the packager's table of test switches reads it to refuse a test build,
    and the ICE link receipt delegates to test_build_switches(CE_DEFINES); no other line mentions it."""
    assert build.count("-DCE_COMMANDER_TEST=") == 1 and build.count('CE_DEFINES.append("-DCE_COMMANDER_TEST=1")') == 1, "only build.py's flag defines it"
    assert 'if a.commander_test:\n        CE_DEFINES.append("-DCE_COMMANDER_TEST=1")' in build, "and only where --commander-test was given"
    assert len(RECEIPT_READ.findall(build)) == 1, "the ICE receipt delegates test_build detection to test_build_switches(CE_DEFINES) or TEST_KIT"
    assert len(SWITCH_TABLE_READ.findall(build)) == 1, "the packager's table of test switches names it once"
    assert len(re.findall(r"CE_COMMANDER_TEST", build)) == 2, "the flag's definition and the switch table are every mention"


def faithful_differences(alone, without):
    """The files whose faithful view (CE_VERSION undefined) differs between a tree with the commander patches and one without them."""
    return [name for name in FILES if faithful(alone[name]) != faithful(without[name])]


class Static(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        base = Path(cls.tmp.name)
        cls.with_ = patched(base / "with", FILES)
        # The faithful comparison isolates the commander patches: both trees leave out the positional patches (see POSITIONAL).
        cls.alone = patched(base / "alone", FILES, skip=LEFT_OUT)
        cls.without = patched(base / "without", FILES, skip=(*PATCHES, *LEFT_OUT))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_hooks_are_where_the_design_puts_them(self):
        check_placement(self.with_)

    def test_the_faithful_view_is_the_text_without_the_patches(self):
        for name in FILES:
            if name != "ltshell.cpp":
                self.assertNotEqual(self.alone[name], self.without[name], f"{name}: the patches change nothing")
            self.assertEqual(faithful(self.alone[name]), faithful(self.without[name]), f"{name}: the faithful build (CE_VERSION undefined) is not the unpatched text")
        self.assertEqual(faithful_differences(self.alone, self.without), [])

    def test_the_patches_left_out_of_that_comparison_are_placed_by_line_number(self):
        # A name stays in POSITIONAL only while it exists and still has a hunk of no context in one of these files; one that no longer does is taken out, not kept.
        for patch in POSITIONAL:
            parts = sections((ROOT / "port/patches" / patch).read_text(encoding="latin1"))
            blind = [name for name in FILES if name in parts
                     and any(old != "0" and count == "0" for old, count in re.findall(r"(?m)^@@ -(\d+),(\d+) ", parts[name]))]
            self.assertTrue(blind, f"{patch} has no hunk of no context in these files: it need not be left out")

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
        check_define_sites(BUILD.read_text())
        users = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "port").rglob("*")
                       if p.is_file() and p.suffix in (".cpp", ".h", ".py", ".patch", ".c") and "CE_COMMANDER_TEST" in p.read_text(errors="ignore"))
        self.assertEqual(users, ["port/build.py", "port/game/ce_cmdmap.cpp", "port/game/ce_cmdorders.cpp", "port/game/ce_cmdview.cpp", "port/game/ce_cmdview.h", "port/game/ce_cmdview_audit.cpp",
                                 "port/game/ce_commander.cpp", "port/game/ce_commander.h", "port/game/ce_render_fence.cpp", "port/tools/ce_commander_tests.py"])
        # The audit is a test build's alone: nothing of it is compiled without the define.
        audit = VIEW_AUDIT.read_text()
        code = [l for l in audit.split("\n") if l.strip() and not l.lstrip().startswith("//")]
        self.assertTrue(all("defined(CE_COMMANDER_TEST)" in l for l in code if l.startswith("#if")), "every conditional of the audit asks for the test define")
        self.assertTrue(code[0].startswith("#if defined(CE_VERSION) && defined(CE_COMMANDER_TEST)") and code[-1] == "#endif")
        depth, outside = 0, []
        for l in code:
            if l.startswith("#if"):
                depth += 1
            elif l.startswith("#endif"):
                depth -= 1
            elif depth == 0:
                outside.append(l)
        self.assertEqual(outside, [], "the audit has code outside its test conditionals")

    def test_a_plain_build_has_no_commander_environment_switch_file_or_probe(self):
        module = without_test(comments_off(MODULE.read_text()))
        for word in ("BEA_PORT_COMMANDER", "fopen", "fprintf", "PortPresentCount", "gOut", "gProbePath", "gTapePath", "Parse(", "Probe(", "Apply(", "kVerbName", "CeCmdWorldHash",
                     "CeCmdWorldDump", "SeedUniformRandomNumberStream", "RngValue"):
            self.assertNotIn(word, module, f"a build without the test define still compiles {word!r} of the order layer")
        self.assertEqual(re.findall(r"getenv\(([^)]*)\)", module), ["name"], "the one environment read is the fence's list")
        map_text = without_test(comments_off(MAP.read_text()))
        # The two comparison switches of the frame-feel lane (README "Frame-feel measurement": BEA_PORT_COMMANDER_VIEW and BEA_PORT_COMMANDER_TERRAIN bring the earlier behaviours
        # back on one build) are read while the map draws, and only then: the map draws only with the experimental switch and commander=1. They are the only environment reads.
        self.assertEqual(sorted(re.findall(r"getenv\(\"(BEA_PORT_COMMANDER_[A-Z]+)\"\)", map_text)), ["BEA_PORT_COMMANDER_TERRAIN", "BEA_PORT_COMMANDER_VIEW"])
        self.assertEqual(len(re.findall(r"getenv\(", map_text)), 2)
        map_text = re.sub(r"getenv\(\"BEA_PORT_COMMANDER_(?:TERRAIN|VIEW)\"\)", "", map_text)
        for word in ("BEA_PORT_COMMANDER", "getenv", "clock_gettime", "time.h", "CeCmdWorldHash", "CeCmdWorldDump", "CeCmdHash", "fopen"):
            self.assertNotIn(word, map_text, f"a build without the test define still compiles {word!r} of the map")

    def test_the_order_layer_draws_no_random_number_and_allocates_nothing(self):
        text = without_test(comments_off(MODULE.read_text()))
        for word in ("FloatRandom", "IntRandom", "rand(", "srand(", "Random(", "new ", "malloc", "calloc", "realloc", "delete ", "std::", "MEM_MANAGER"):
            self.assertNotIn(word, text, f"the order layer names {word!r}")

    def test_the_map_draws_no_random_number_and_allocates_nothing(self):
        self.assertEqual(map_resource_violations(MAP.read_text()), [], "the map names a forbidden allocation/random facility")

    def test_scalar_math_exception_keeps_allocation_and_random_controls(self):
        self.assertEqual(map_resource_violations("std::ceil(x); std::fabs(x); std::isfinite(x); std::log(x); (std::numeric_limits<float>::quiet_NaN)();"), [])
        for text in ("std::vector<float> values;", "std::string text;", "std::make_unique<X>();", "std::logarithm(x);", "std::numeric_limits<X>::allocate();",
                     "new X;", "new\tX[2];", "new (p) X;", "malloc(4);", "calloc(1,4);", "realloc(p,4);", "delete p;",
                     "rand();", "srand(1);", "FloatRandom();", "IntRandom();", "Random();", "MEM_MANAGER;", "#include <new>"):
            with self.subTest(control=text):
                self.assertTrue(map_resource_violations(text), "a forbidden authored control escaped the scan")

    def test_the_layout_reads_are_checked_at_compile_time(self):
        check_layout(MODULE.read_text())

    def test_the_map_gate_names_every_fence(self):
        check_gates(MAP.read_text(), MODULE.read_text())

    def test_the_live_view_picture_only_reads_the_game(self):
        text = VIEW.read_text()
        body = [l for l in text.split("\n") if l.strip() and not l.lstrip().startswith("//")]
        self.assertEqual(body[0].strip(), "#ifdef CE_VERSION")
        self.assertEqual(body[-1].strip(), "#endif")
        self.assertEqual(view_violations(text), [], "the live view's picture steps outside its list of reads")
        self.assertEqual(view_reads_documented(text), [], "the picture's READS comment and the list here differ")
        # every object on the list is in fact used (a list wider than the code hides a later addition)
        code = comments_off(text)
        for name, members in VIEW_READS.items():
            for member in members:
                self.assertRegex(code, r"\b" + name + r"\s*\.\s*" + member + r"\b", f"{name}.{member} is on the list and not read")
        # its memory is the C library's, and the retired replacement terrain/device renderer is gone
        for word in ("realloc(", "free("):
            self.assertIn(word, code)
        header_code = comments_off(VIEW_HEADER.read_text())
        for retired in ("CeCmdViewBegin", "CeCmdViewWorld", "CeCmdViewEnd", "CeCmdViewMatrices", "CeCmdViewBox", "CeCmdViewText(", "CeCmdViewTextWidth",
                        "CeCmdViewDrawing", "CeCmdViewCamera", "CreateStateBlock", "DrawPrimitiveUP", "DrawIndexedPrimitive"):
            self.assertNotIn(retired, code, f"retired replacement renderer symbol {retired!r} remains in ce_cmdview.cpp")
            self.assertNotIn(retired, header_code, f"retired replacement renderer symbol {retired!r} remains in ce_cmdview.h")
        collect = function(code, "void CeCmdViewCollectUnits(const CeWorldViewSample& sample, int friendlyAllegiance, const void *pilot)")
        self.assertLess(collect.index("CeViewDetail::Valid("), collect.index("WORLD.GetThingNB()"))
        self.assertLess(collect.index("CeViewDetail::Combined("), collect.index("WORLD.GetThingNB()"))

    def test_the_view_scan_catches_what_it_guards(self):
        text = VIEW.read_text()
        self.assertEqual(view_violations(text), [])
        for name, addition in (("the game's sprite", "\tCSPRITERENDERER::DrawColouredSprite(0, 0, 0, NULL, 0, 1, 1);\n"),
                               ("the game's vertex buffer", "\tFASTVB.Render();\n"),
                               ("the render-state cache", "\tLT.SRS(D3DRS_ZENABLE, FALSE);\n"),
                               ("the font's own draw", "\tfont->DrawTextWithShadow(0, 0, 0, NULL);\n"),
                               ("the font's lazy texture", "\tfont->InitFromTexture();\n"),
                               ("the game's allocator", "\tCDXTexture *t = new (MEMTYPE_TEXTURE) CDXTexture;\n"),
                               ("plain new", "\tint *p = new int[4];\n"),
                               ("a random number", "\tint r = rand();\n"),
                               ("a clock", "\tfloat now = PLATFORM.GetSysTimeFloat();\n"),
                               ("an unlisted member", "\tMAP.mHeightScale = 1.f;\n"),
                               ("an unlisted object", "\tHUD.Render();\n"),
                               ("the extents cache", "\tMAP.GetExtents();\n"),
                               ("the lit texel routine", "\tMAP.Unknown47eff0(0, NULL, NULL, 0, 0, 5, 0, 0, 8, 8);\n"),
                               ("the set's own iterator", "\tWORLD.GetThingNB().First();\n"),
                               ("a write to a thing", "\tthing->SetFlags(0);\n"),
                               ("an unlisted call", "\tthing->Hit(NULL, NULL);\n"),
                               ("the engine's render", "\tENGINE.Render(0);\n"),
                               ("the particle step", "\tPARTICLE_MANAGER.Process(0.f, FALSE);\n"),
                               ("the shadow manager", "\tSHADOWS.ResetShads();\n"),
                               ("an environment switch", "\tif (getenv(\"X\")) return;\n")):
            anchor = "void CeCmdViewCollectUnits(const CeWorldViewSample& sample, int friendlyAllegiance, const void *pilot)\n{\n"
            self.assertEqual(text.count(anchor), 1)
            with self.subTest(mutant=name):
                self.assertTrue(view_violations(text.replace(anchor, anchor + addition, 1)), f"{name} in ce_cmdview.cpp was not caught")
        world_doc = "//   WORLD           GetThingNB"
        platform_doc = "//   PLATFORM        Font(FONT_SMALL): GetHeight"
        self.assertEqual(text.count(world_doc), 1)
        self.assertEqual(text.count(platform_doc), 1)
        self.assertTrue(view_reads_documented(text.replace(world_doc, "//   WORLDS          GetThingNB", 1)), "an object missing from the READS comment")
        self.assertTrue(view_reads_documented(text.replace(platform_doc, platform_doc + "\n//   HUD             Render", 1)),
                        "an object the READS comment lists and the list here does not")

    def test_the_map_leaves_the_frame_behind_the_live_view_alone(self):
        text = MAP.read_text()
        self.assertEqual(map_live_view_violations(text), [])
        mutations = {
            "unbound camera": text.replace("camera!=gStrategicOriginal || ", ""),
            "missing native snapshot": text.replace("CeWorldViewRead(", "MissingNativeView("),
            "missing actual member picks": text.replace("CeCmdViewCollectUnits(", "MissingMemberPicks("),
            "replacement scenery": text.replace("CeCmdViewCollectUnits(*view,friendly,pilot);", "CeCmdViewWorld(friendly,pilot);"),
        }
        for label, changed in mutations.items():
            with self.subTest(label=label):
                self.assertNotEqual(text, changed)
                self.assertTrue(map_live_view_violations(changed))


    def test_no_patch_gives_the_commander_another_entry_into_the_game(self):
        patches = sorted((ROOT / "port/patches").glob("*.patch"))
        texts = [p.read_text(encoding="latin1") for p in patches]
        hooks_added = "\n".join("+" + l for l in (ROOT / "port/game/ce_hooks.cpp").read_text().splitlines())
        self.assertEqual(commander_entries(texts + [hooks_added]), COMMANDER_ENTRIES)
        self.assertFalse([p.name for p in patches if "render-fence" in p.name], "the render fence's patch is gone")
        # the presentation hook's line is in ce_hooks.cpp (dispatched from CEngine::SetViewpoint), and nothing else of the Commander's is in the engine's or the renderer's files
        owners = {}
        for patch, text in zip(patches, texts):
            for name, body in sections(text).items():
                for entry in commander_entries([body]):
                    owners.setdefault(entry, set()).add(name)
        for entry in commander_entries([hooks_added]):
            owners.setdefault(entry, set()).add("ce_hooks.cpp")
        self.assertEqual(owners["PortCeCmdRenderCamera"], {"ce_hooks.cpp"})
        self.assertEqual(owners["PortCeCmdRenderWorld"], {"DXEngine.cpp"})
        self.assertEqual(owners["PortCeCmdKey"], {"ce_hooks.cpp"})
        self.assertEqual(owners["PortCeCmdLevelEnd"], {"ce_hooks.cpp"})
        self.assertEqual(owners["PortCeCmdRender"], {"DXEngine.cpp"})
        self.assertEqual(owners["PortCeCmdHideCockpit"], {"DXEngine.cpp", "Hud.cpp"})
        self.assertEqual(owners["PortCeCmdWorldViewToken"], {"MessageBox.cpp"})
        self.assertEqual(owners["PortCeCmdThingGone"], {"thing.cpp", "Unit.cpp"})
        self.assertEqual(owners["PortCeCmdGuidePoint"], {"Guide.cpp"})
        self.assertEqual({name for names in owners.values() for name in names}, {'DXEngine.cpp', 'Hud.cpp', 'MessageBox.cpp', 'ce_hooks.cpp', 'game.cpp', 'thing.cpp', 'Unit.cpp', 'Guide.cpp'})
        with self.subTest(mutant="a hook in the render"):
            self.assertNotEqual(commander_entries(texts + [hooks_added, "+\tif (!PortCeCmdStrategicPass())\n"]), COMMANDER_ENTRIES)
        with self.subTest(mutant="the picture called from a patch"):
            self.assertNotEqual(commander_entries(texts + [hooks_added, "+\tCeCmdViewWorld(0, NULL);\n"]), COMMANDER_ENTRIES)
        with self.subTest(mutant="a keep and put back around a render"):
            self.assertNotEqual(commander_entries(texts + [hooks_added, "+\tCeCmdSaveRenderCaches();\n"]), COMMANDER_ENTRIES)

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
        base = Path(cls.tmp.name)
        cls.files = patched(base / "with", FILES)
        cls.alone = patched(base / "alone", FILES, skip=LEFT_OUT)
        cls.without = patched(base / "without", FILES, skip=(*PATCHES, *LEFT_OUT))

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
        hooks_text = (ROOT / "port/game/ce_hooks.cpp").read_text()
        row = '\t{40, "commander_key", CE_HOOK_RENDER, CE_GATE_VERSION, CE_MODE_NONE, CeHookCmdKey},\n'
        target = '\t{20, "palette_suppress_down", CE_HOOK_RENDER, CE_GATE_VERSION, CE_MODE_NONE, CeHookPaletteSuppressDown},\n'
        self.assertEqual(hooks_text.count(row), 1)
        mutated = hooks_text.replace(row, "", 1).replace(target, row + target, 1)
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(self.files, mutated)

    def test_map_key_after_the_key_trap(self):
        call = "\t\t\tif (ce_hook_key_capture(key, (lParam & 0x40000000) != 0)) {}\t// CE hook point (port/game/ce_hooks.h): a consumed key goes no further\n\t\t\telse\n"
        files = self.mutate("ltshell.cpp", call, "")
        files["ltshell.cpp"] = files["ltshell.cpp"].replace("\t\t\tif (mCurrentKeytrap)\n\t\t\t{\n\t\t\t\tmCurrentKeytrap(key,KEY_EVENT_DOWN);\n\t\t\t}\n",
                                                            "\t\t\tif (mCurrentKeytrap)\n\t\t\t{\n\t\t\t\tmCurrentKeytrap(key,KEY_EVENT_DOWN);\n\t\t\t}\n" + call, 1)
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
        end = re.match(OVERLAY_HOOK + r"\tPLATFORM\.EndScene\(\);\n", self.files["DXEngine.cpp"][self.files["DXEngine.cpp"].index(block) + len(block):]).group(0)
        files = self.mutate("DXEngine.cpp", block + end, end + block)
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_map_drawn_in_the_mirror_pass(self):
        braced = "\tif (!gMirrorPass)\n\t{\n\t\tPortCeCmdRender();"
        if braced in self.files["DXEngine.cpp"]:
            files = self.mutate("DXEngine.cpp", braced, "\t{\n\t\tPortCeCmdRender();")
        else:
            files = self.mutate("DXEngine.cpp", "\tif (!gMirrorPass)\n\t\tPortCeCmdRender();", "\tPortCeCmdRender();")
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_editor_overlay_after_the_unbraced_mirror_test(self):
        # an extra overlay call placed after the map's unbraced mirror-pass test, indented as if guarded
        files = self.mutate("DXEngine.cpp", "\tif (!gMirrorPass)\n\t\tPortCeCmdRender();\t// the commander map, over everything else the frame drew\n",
                            "\tif (!gMirrorPass)\n\t\tPortCeCmdRender();\t// the commander map, over everything else the frame drew\n\t\tPortCeEditorRender();\n")
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(files)

    def test_map_not_closed_with_the_level(self):
        hooks_text = (ROOT / "port/game/ce_hooks.cpp").read_text()
        row = '\t{40, "commander_level_end", CE_HOOK_SIM, CE_GATE_VERSION, CE_MODE_NONE, CeHookCmdLevelEnd},\n'
        self.assertEqual(hooks_text.count(row), 1)
        with self.assertRaises((AssertionError, ValueError)):
            check_placement(self.files, hooks_text.replace(row, "", 1))

    def test_an_unguarded_hook_changes_the_faithful_view(self):
        # a hook outside the CE_VERSION conditional is a difference the faithful build would compile
        text = self.files["SquadNormal.cpp"].replace("#ifdef CE_VERSION\n\tif (CeCommanderNoMerge(this))", "\tif (0)\n\tif (CeCommanderNoMerge(this))", 1)
        self.assertNotEqual(faithful(text), faithful(self.files["SquadNormal.cpp"]))

    def test_each_unguarded_hook_is_caught_by_the_faithful_comparison(self):
        # The comparison itself, not only faithful(): one line of each commander hook taken out of its CE_VERSION block (the block closed before it and opened again after) must make the file differ.
        self.assertEqual(faithful_differences(self.alone, self.without), [], "the unmutated trees are equal")
        hooks = (("SquadRelaxed.cpp", r"(?m)^\s*int ceOrder = CeCommanderRelaxed\(this, &mPos\);"),
                 ("SquadNormal.cpp", r"(?m)^\s*if \(CeCommanderNoMerge\(this\)\)"),
                 ("DXEngine.cpp", r"(?m)^\s*void PortCeCmdRender\(\);"),
                 ("DXEngine.cpp", r"(?m)^\s*PortCeCmdRender\(\);"),
                 ("game.cpp", r"(?m)^void PortCeCmdFrame\(\);"),
                 ("game.cpp", r"(?m)^\s*PortCeCmdFrame\(\);"))
        for name, pattern in hooks:
            with self.subTest(file=name, hook=pattern):
                text = self.alone[name]
                found = list(re.finditer(pattern, text))
                self.assertEqual(len(found), 1, "the hook line is found once")
                start, end = found[0].start(), text.index("\n", found[0].end())
                mutant = dict(self.alone)
                mutant[name] = text[:start] + "#endif\n" + text[start:end] + "\n#ifdef CE_VERSION" + text[end:]
                self.assertEqual(faithful_differences(mutant, self.without), [name], f"a hook of {name} outside CE_VERSION went unseen")

    def test_a_second_definition_of_the_test_define_is_caught(self):
        build = BUILD.read_text()
        check_define_sites(build)
        anchor = "    if a.mods_windows_test:\n"
        self.assertEqual(build.count(anchor), 1)
        mutants = {
            "a second definition": build.replace(anchor, '    CE_DEFINES.append("-DCE_COMMANDER_TEST=1")\n' + anchor),
            "the definition outside its flag": build.replace('if a.commander_test:\n        CE_DEFINES.append("-DCE_COMMANDER_TEST=1")',
                                                              'CE_DEFINES.append("-DCE_COMMANDER_TEST=1")'),
            "a third mention": build + "\n# CE_COMMANDER_TEST\n",
            "the switch table's entry lost": build.replace('(("-DCE_COMMANDER_TEST", "commander-test"), ', '('),
            "the receipt's test lost": build.replace('test_build=bool(test_build_switches(CE_DEFINES)) or TEST_KIT', 'test_build=False'),
        }
        for name, text in mutants.items():
            with self.subTest(mutant=name):
                self.assertNotEqual(text, build, "the mutant changed the text")
                with self.assertRaises(AssertionError, msg=f"{name} went unseen"):
                    check_define_sites(text)

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

    def test_kept_map_view_decoupled_from_live_view(self):
        text = MAP.read_text()
        for old, new in (("gLooseBounds = strategic;", "gLooseBounds = true;"),
                         ("gKeptMapLevel != GAME.GetCurrentLevel()", "gKeptLevel != GAME.GetCurrentLevel()"),
                         ("gKeptMapLevel = GAME.GetCurrentLevel();", "gKeptLevel = GAME.GetCurrentLevel();"),
                         ("\tgKeptMapLevel = -1;", "\t(void)gKeptMapLevel;")):
            self.assertEqual(text.count(old), 1)
            with self.assertRaises(AssertionError):
                check_keys(text.replace(old, new, 1))

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
        old = "gMap = allowed && (proofMap || PortCeExperimental(\"commander\"));"
        self.assertEqual(text.count(old), 1)
        with self.assertRaises(AssertionError):
            check_gates(MAP.read_text(), text.replace(old, "gMap = (PortCeExperimental(\"commander\") && allowed) || proofMap;"))

    def test_switch_without_the_commander_line(self):
        # ce.ini commander=1 (under [Experimental], or as 0.1.13 wrote it) is the map's switch, read once in ce.cpp: a gate that does not ask it is caught.
        text = MODULE.read_text()
        gate = "proofMap || PortCeExperimental(\"commander\")"
        self.assertEqual(text.count(gate), 1)
        with self.assertRaises(AssertionError):
            check_gates(MAP.read_text(), text.replace(gate, "proofMap || true"))
        with self.assertRaises(AssertionError):
            check_gates(MAP.read_text(), text.replace(gate, "proofMap || PortCeExperimental(\"museum\")"))

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




class CommandBoundary(unittest.TestCase):
    """Execute production order code; only the surrounding world is authored."""

    @staticmethod
    def definition(source, name):
        match = re.search(
            r"(?m)^[\t ]*[\w :*&<>]+\b" + re.escape(name) +
            r"\([^;{]*\)\s*\{", source)
        if not match:
            raise AssertionError("production function missing: " + name)
        position = source.index("{", match.start()) + 1
        depth = 1
        while position < len(source) and depth:
            depth += (source[position] == "{") - (source[position] == "}")
            position += 1
        if depth:
            raise AssertionError("production function is incomplete: " + name)
        return source[match.start():position]

    @classmethod
    def setUpClass(cls):
        import subprocess
        cls.subprocess = subprocess
        cls.root = Path(__file__).resolve().parents[2]
        scratch = cls.root / "local-data" / "commander-boundary-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=scratch)
        cls.directory = Path(cls.tmp.name)
        cls.logs = scratch / (cls.directory.name + "-logs")
        cls.logs.mkdir()
        cls.source = (cls.root / "port/game/ce_commander.cpp").read_text()
        records = []
        for kind, name in (("enum", "Verb"), ("struct", "Rec")):
            match = re.search(r"\b" + kind + r"\s+" + name +
                              r"\s*\{.*?\};", cls.source, re.S)
            if not match:
                raise AssertionError("production order type missing: " + name)
            records.append(match.group(0))
        (cls.directory / "ce_commander_boundary_records.inc").write_text(
            "\n".join(records))
        scope = re.search(r"bool gExplicitUnitReplacement = false;\n"
                          r"struct ExplicitUnitReplacement \{.*?\n\};", cls.source, re.S)
        if not scope:
            raise AssertionError("production explicit-order scope missing")
        # This fixture observes native state/destination delivery. The actual
        # token adapter and release timing have their own source-coupled driver.
        cls.production = scope.group(0) + "\n"
        cls.production += "\n\n".join(cls.definition(cls.source, name) for name in
            ("Ground", "ReleaseMemberOrders", "SetMembers", "Plant", "Hold", "CeCmdMarchLand", "CeCmdIssue"))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def execute(self, name, production):
        include = self.directory / "ce_commander_boundary_under_test.inc"
        include.write_text(production)
        binary = self.directory / name
        command = [os.environ.get("CXX", "g++"), "-std=c++17", "-O2", "-Wall",
                   "-DCE_VERSION=1",
                   "-Wextra", "-Wno-unused-parameter", "-I", str(self.directory),
                   "-I", str(self.root / "port/game"),
                   str(self.root / "port/tools/ce_commander_boundary_driver.cpp"),
                   "-o", str(binary)]
        with (self.logs / (name + ".compile.stdout")).open("wb") as out, \
             (self.logs / (name + ".compile.stderr")).open("wb") as err:
            result = self.subprocess.run(command, stdout=out, stderr=err, timeout=60)
        self.assertEqual(result.returncode, 0,
                         "boundary driver compilation failed; private logs: " +
                         str(self.logs))
        with (self.logs / (name + ".stdout")).open("wb") as out, \
             (self.logs / (name + ".stderr")).open("wb") as err:
            return self.subprocess.run([str(binary)], stdout=out, stderr=err,
                                       timeout=20).returncode

    def test_production_orders_reach_consumers_and_invalid_orders_do_not(self):
        self.assertEqual(self.execute("production", self.production), 0,
                         "boundary execution failed; private logs: " +
                         str(self.logs))

    def test_explicit_scope_precedes_native_consumers(self):
        original = self.definition(self.production, "CeCmdIssue")
        self.assertEqual(original.count("ExplicitUnitReplacement replacingUnits;"), 1)
        late = original.replace("    ExplicitUnitReplacement replacingUnits;\n", "")
        final_return = late.rfind("\treturn true;")
        self.assertGreater(final_return, 0)
        late = late[:final_return] + "    ExplicitUnitReplacement replacingUnits;\n" + late[final_return:]
        mutant = self.production.replace(original, late, 1)
        self.assertEqual(self.execute("late-explicit-scope", mutant), 1)
        self.assertIn("explicit scope precedes native state",
                      (self.logs / "late-explicit-scope.stderr").read_text())

    def test_routine_point_cannot_claim_native_override(self):
        original = self.definition(self.production, "Hold")
        self.assertIn("n->GoToPoint(goal, FALSE)", original)
        changed = original.replace("n->GoToPoint(goal, FALSE)", "n->GoToPoint(goal, true)", 1)
        mutant = self.production.replace(original, changed, 1)
        self.assertEqual(self.execute("routine-native-override", mutant), 1)
        self.assertIn("routine setter cannot claim native override",
                      (self.logs / "routine-native-override.stderr").read_text())

    def test_nonfinite_guard_is_causal(self):
        guard = re.compile(r"if \(!isfinite\(x\) \|\| "
                           r"!isfinite\(y\)\) \{[^}]*\}", re.S)
        mutant, count = guard.subn("", self.production)
        self.assertEqual(count, 1, "finite guard was not uniquely identified")
        self.assertNotEqual(self.execute("missing-finite-guard", mutant), 0,
                            "driver accepted a removed finite guard")

    def test_destination_delivery_is_causal(self):
        mutant, count = re.subn(r"Ground\(x, y\)", "Ground(y, x)",
                                self.production, count=1)
        self.assertEqual(count, 1, "point destination was not found")
        self.assertNotEqual(self.execute("swapped-destination", mutant), 0,
                            "driver accepted a wrong downstream destination")


if __name__ == "__main__":
    unittest.main()
