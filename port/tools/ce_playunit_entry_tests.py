#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Play as a unit, taking a unit by key (port/game/ce_playunit.cpp, ce_playunit_hud.cpp, ce_playunit_hud.h, ce_playunit_rules.h): the static fences of the take key,
the notices and their cues.

What these tests hold, without a game (the rules themselves, as compiled code with their mutants, are ce_playunit_tests.py's):
- Nothing is taken without a press of the take key: Take is called only where a press is answered (AnswerTake) or a switch the player asked for finishes (FinishSwitch),
  a switch is started only by a press, and a press is made in one place, from the key's edge (CePlayUnitPressed), only where it may count.
- The take key is not read at all while the option is not wanted: the render hook returns before it asks the keyboard. A press counts only while the level plays,
  unpaused, with no key trap (a console or a map), no free camera, and the key not bound by the player to one of the game's own controls.
- A take plays the select cue and names the unit and the way back; a refusal plays the cancel cue and says why; a return the player caused plays the cancel cue and says
  so; a return the player did not cause and needs no word on (the level's end, a new level) says nothing.
- Every sentence the player can be shown passes the house style's wording rules: a full stop, no file or internal names, nothing the game's small face cannot draw,
  short enough for the capsule.
- The drawing file draws with the HUD's own capsule (CHud::RenderObjectiveBar) and the small face, scaled as the HUD is, keeps its clock in SDL's milliseconds, is
  wrapped in CE_VERSION, and holds no test machinery, environment read, allocation or random number.
Each fence is checked against a mutant of the text it reads, so that a check that cannot fail is caught.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
MODULE = PORT / "game/ce_playunit.cpp"
HUD = PORT / "game/ce_playunit_hud.cpp"
HUD_H = PORT / "game/ce_playunit_hud.h"
RULES = PORT / "game/ce_playunit_rules.h"
NOTICE = PORT / "game/ce_notice.cpp"

# The house style's forbidden words in player text (the draft UI guide, W3) and the small face's missing characters (T2).
FORBIDDEN_WORDS = ("ce.ini", ".ini", ".exe", "README", "Experimental", "Prototype", "test", "best effort", "probe", "lease", "guide", "squad-class", "AI")
MISSING_CHARACTERS = set("$@[\\]^_{|}~`")


def comments_off(text):
    return "\n".join(l for l in text.split("\n") if not l.strip().startswith("//"))


def function(text, signature):
    """The body of the function that starts with signature, to its closing brace at the start of a line."""
    start = text.index(signature)
    end = text.index("\n}\n", start)
    return text[start:end + 2]


def strings_in(text):
    return re.findall(r'"((?:[^"\\]|\\.)*)"', text)


def player_sentences(module, rules):
    """Every literal the player can be shown: the notices' arguments, the refusals' sentences, Release's second argument, and the two lines of a take."""
    out = []
    for call in re.findall(r"(?:Refuse|CePlayUnitNotice)\(([^;]*)\);", module):
        out += strings_in(call)
    for call in re.findall(r'Release\("[^"]*"((?:, "[^"]*")+)\)', module):
        out += strings_in(call)
    out += strings_in(function(module, "const char *SightSentence("))
    out += strings_in(function(module, "const char *SightHint("))
    out += strings_in(function(module, "const char *NotReadySentence()"))
    out += strings_in(function(rules, "inline const char *CePlayUnitRefusalSentence("))
    take = function(module, "bool Take(")
    out += [s for s in strings_in(take) if "%s" in s or "Take Another" in s]
    return [s for s in out if s]


class Fences:
    """The checks, run on given texts so that a mutant can be run through them."""

    @staticmethod
    def nothing_taken_without_a_press(module):
        code = comments_off(module)
        takes = [m.start() for m in re.finditer(r"\bTake\(player, engine, controller, u, kind\);", code)]
        assert len(takes) == 2, f"Take is called in {len(takes)} places, not the press's and the switch's"
        answer = function(code, "void AnswerTake()")
        finish = function(code, "void FinishSwitch()")
        assert "Take(player, engine, controller, u, kind);" in answer and "Take(player, engine, controller, u, kind);" in finish, "Take outside the press and the switch"
        assert code.count("gSwitchWait = kCePlayUnitSwitchWait;") == 1 and "gSwitchWait = kCePlayUnitSwitchWait;" in function(code, "void AnswerSwitch("), "a switch not started by a press"
        assert code.count("gTakePressed = true;") == 1, "a press made in more than one place"
        render = function(code, "void CePlayUnitRenderState()")
        assert "if (CePlayUnitPressed(&gTakeKey, held > 0, mayCount)) gTakePressed = true;" in render, "a press not made from the key's edge"
        update = function(code, "void CePlayUnitUpdate()")
        assert "AnswerTake();" in update and update.count("AnswerTake();") == 1
        idle = update[update.rindex("if (gSwitchWait) { gTakePressed = false; FinishSwitch(); return; }"):]
        assert idle.index("if (!gTakePressed) return;") < idle.index("AnswerTake();"), "a take answered without a press"
        assert "AnswerSwitch(u, engine);" in update and update.index("if (gTakePressed)") < update.index("AnswerSwitch(u, engine);")
        for gone in ("gDone", "Pick("):
            assert gone not in code, f"the automatic take-over's {gone} is still there"
        assert "kFirstLook" not in comments_off(without_test(module)), "the shipped module still waits for a first look"

    @staticmethod
    def key_read_only_where_it_counts(module):
        render = function(comments_off(module), "void CePlayUnitRenderState()")
        assert render.index("if (!gConfigured || !gWanted) return;") < render.index("PortKeyHeldNow(TakeKey())"), "the keyboard is read while the option is not wanted"
        may = re.search(r"const bool mayCount = ([^;]*);", render).group(1)
        for clause in ("GAME.GetGameState() == GAME_STATE_PLAYING", "!GAME.IsPaused()", "!LT.GetKeytrap()", "!GAME.IsFreeCameraOn(0)", "TakeKeyFree()"):
            assert clause in may, f"a press counts without {clause}"
        assert 'PortCeActionKey(CeControlAction::TakeUnit)' in function(comments_off(module), 'unsigned TakeKey()')
        free = function(comments_off(module), "bool TakeKeyFree()")
        assert 'PortCeActionConflict(CeControlAction::TakeUnit, (int)TakeKey())' in free

    @staticmethod
    def cues(module):
        code = comments_off(module)
        take = function(code, "bool Take(")
        assert "CePlayUnitNotice(first, second, true);" in take and "CFrontEnd::PlaySound(FES_SELECT);" in take, "a take without its notice and select cue"
        assert "UnitPromptLine(standing, sizeof(standing), name);" in take and "CePlayUnitPrompt(standing);" in take, "a take leaves no prompt line for the way back"
        line = function(code, "void UnitPromptLine(")
        assert "MorphKeyName(back, sizeof(back));" in line and '"%s | %s: Return | %s: %s", name, back' in line, "the standing prompt does not name the unit and the way back"
        refuse = function(code, "void Refuse(")
        assert "CePlayUnitNotice(sentence, hint);" in refuse and "CFrontEnd::PlaySound(FES_BACK);" in refuse, "a refusal without its sentence and cancel cue"
        release = function(code, "void Release(")
        notice = release[release.index("if (notice)"):release.index("}", release.index("if (notice)"))]
        assert "CePlayUnitNotice(notice, hint);" in notice and "CFrontEnd::PlaySound(FES_BACK);" in notice, "a return without its sentence and cue"
        assert release.index("CePlayUnitPrompt(NULL);") < release.index("gDriving = false;"), "a return leaves the prompt line up"
        for quiet in ('Release("the level is ending");', 'Release("a new level");', 'Release("switching to another unit");'):
            assert quiet in code, f"{quiet} says something"
        for told in ('Release("the Morph key", "', 'Release("the unit is gone", "', 'Release("free camera", "', 'Release("the option is no longer allowed here", "'):
            assert told in code, f"{told} is silent"
        # a return while another view holds the screen is said once the player's own view is back
        assert 'Release("another camera"); gBackPending = true;' in code, "a return to a cutscene is never said"
        update = function(code, "void CePlayUnitUpdate()")
        assert 'gBackPending = false; CePlayUnitNotice("Back in your Battle Engine.", NULL);' in update and "view->GetShowHUD()" in update, "the deferred return is not said, or said over another view"
        end = function(code, "void CePlayUnitLevelEnd()")
        assert "CePlayUnitNoticeClear();" in end, "a notice outlives its level"

    @staticmethod
    def wording(module, rules):
        sentences = player_sentences(module, rules)
        assert len(sentences) >= 15, f"only {len(sentences)} player sentences found"
        for s in sentences:
            shown = s.replace("%s", "Name")
            assert not (set(shown) & MISSING_CHARACTERS), f"{s!r} has a character the small face cannot draw"
            for word in FORBIDDEN_WORDS:
                assert not re.search(r"(?<![A-Za-z])" + re.escape(word) + r"(?![A-Za-z])", shown), f"{s!r} says {word!r}"
            assert len(shown) <= 48, f"{s!r} is too long for the capsule"
            assert "  " not in s.replace("   %s: Take Another", ""), f"{s!r} has a double space"
            if "%s" not in s and "Take Another" not in s:
                assert s.endswith("."), f"{s!r} has no full stop"
                assert s[0].isupper(), f"{s!r} does not start with a capital"

    @staticmethod
    def drawing(hud, hud_h, notice):
        code = comments_off(hud)
        for text, name in ((hud, "ce_playunit_hud.cpp"), (hud_h, "ce_playunit_hud.h")):
            body = [l for l in text.split("\n") if l.strip() and not l.lstrip().startswith("//")]
            assert "#ifdef CE_VERSION" in body[:4] and body[-1].strip() == "#endif", f"{name} is not wrapped in CE_VERSION"
            assert "CE_PLAYUNIT_TEST" not in text, f"{name} holds test machinery"
        for banned in ("getenv", "fopen", "rand(", "malloc", "free(", " new ", "delete ", "GAME.Get", "WORLD.", "Release(", "Take(", "SetAIState", "EVENT_MANAGER"):
            assert banned not in code, f"the drawing file uses {banned!r}"
        assert "HUD.RenderObjectiveBar(" in code, "the notice is not the HUD's own capsule"
        assert "PLATFORM.Font(FONT_SMALL)" in code, "the notice is not in the small face"
        assert "CeHudScaleFactor()" in code, "the notice is not scaled as the HUD is"
        assert "CE_ScreenshotTicks()" in code and "GetRenderFrameLength" not in code, "the notice's clock is not SDL's"
        for name in ("kShownMs", "kShownLongMs"):
            m = re.search(name + r" = (\d+);", code)
            assert m and 4000 <= int(m.group(1)) <= 6000, f"{name}: a notice is not up four to six seconds"
        # the clock counts only drawn frames, each by at most a step, so a pause never eats a notice
        assert "gUntil" not in code and code.count("Advance(&gAge, &gLast);") == 1 and "if (step > kStepMs) step = kStepMs;" in code, "a notice's clock runs while it is not drawn"
        # the reticle is the HUD's own art with no box behind its word; ready says nothing
        reticle = function(code, "void Reticle(")
        assert "HUD.mCrosshairPrimary" in reticle and "HUD.mCrosshairDot" in reticle, "the reticle is not the HUD's own"
        assert "DrawColouredSprite" not in reticle and "0x90000000" not in code, "the reticle's word sits on a box"
        assert "const char *word = NULL;" in reticle and "if (word)" in reticle, "ready says something"
        assert "HudScale(w, h)" in reticle, "the reticle is not scaled as the HUD is"
        # a line wider than the capsule is squeezed, then cut
        fit = function(code, "float Fit(")
        assert "kMinSqueeze" in fit and "kMaxWidth" in fit, "a long line runs out of the capsule"
        assert "if (CePlayUnitRenderState) CePlayUnitRenderState();" in notice, "the render hook does not call the module"


def without_test(text):
    out, stack = [], []
    for line in text.split("\n"):
        d = line.strip()
        if re.match(r"#\s*ifdef\s+CE_PLAYUNIT_TEST\b", d):
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
    return "\n".join(out)


def swap(text, old, new):
    assert text.count(old) == 1, f"the mutant's text is not unique: {old[:70]!r} ({text.count(old)})"
    return text.replace(old, new)


class TakeKey(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = MODULE.read_text()
        cls.hud = HUD.read_text()
        cls.hud_h = HUD_H.read_text()
        cls.rules = RULES.read_text()
        cls.notice = NOTICE.read_text()

    def caught(self, check, *texts):
        with self.assertRaises((AssertionError, ValueError, AttributeError)):
            check(*texts)

    def test_nothing_is_taken_without_a_press(self):
        Fences.nothing_taken_without_a_press(self.module)
        m = self.module
        self.caught(Fences.nothing_taken_without_a_press, swap(m, "	if (!gTakePressed) return;\n	gTakePressed = false;\n	AnswerTake();", "	gTakePressed = false;\n	AnswerTake();"))
        self.caught(Fences.nothing_taken_without_a_press, swap(m, "if (CePlayUnitPressed(&gTakeKey, held > 0, mayCount)) gTakePressed = true;", "if (held > 0) gTakePressed = true;"))
        self.caught(Fences.nothing_taken_without_a_press, swap(m, "	gTakePressed = false;\n	AnswerTake();\n}", "	gTakePressed = false;\n	AnswerTake();\n	gTakePressed = true;\n}"))

    def test_the_key_is_read_only_where_a_press_counts(self):
        Fences.key_read_only_where_it_counts(self.module)
        m = self.module
        self.caught(Fences.key_read_only_where_it_counts, swap(m, " && !LT.GetKeytrap()", ""))
        self.caught(Fences.key_read_only_where_it_counts, swap(m, " && !GAME.IsPaused() && !LT.GetKeytrap()", " && !LT.GetKeytrap()"))
        self.caught(Fences.key_read_only_where_it_counts, swap(m, " && TakeKeyFree();", ";"))
        self.caught(Fences.key_read_only_where_it_counts, swap(m, "\tif (!gConfigured || !gWanted) return;\n\tif (PortCeMuseumVisitActive", "\tif (PortCeMuseumVisitActive"))

    def test_each_change_has_its_cue_and_its_words(self):
        Fences.cues(self.module)
        m = self.module
        self.caught(Fences.cues, swap(m, "	CePlayUnitPrompt(standing);\n	CFrontEnd::PlaySound(FES_SELECT);", "	CePlayUnitPrompt(standing);"))
        self.caught(Fences.cues, swap(m, "	CePlayUnitNotice(sentence, hint);\n	CFrontEnd::PlaySound(FES_BACK);", "	CePlayUnitNotice(sentence, hint);"))
        self.caught(Fences.cues, swap(m, 'Release("the unit is gone", "Your unit was destroyed.", "Back in your Battle Engine.");', 'Release("the unit is gone");'))
        self.caught(Fences.cues, swap(m, 'Release("free camera", "Back in your Battle Engine.");', 'Release("free camera");'))
        self.caught(Fences.cues, swap(m, 'Release("another camera"); gBackPending = true;', 'Release("another camera");'))
        self.caught(Fences.cues, swap(m, "	CePlayUnitPrompt(standing);\n", ""))
        self.caught(Fences.cues, swap(m, '"%s | %s: Return | %s: %s", name, back', '"%s | %s: %s", name, zoom'))
        self.caught(Fences.cues, swap(m, "	CePlayUnitNoticeClear();\n	ReconcileController();", "	ReconcileController();"))

    def test_the_words_follow_the_house_style(self):
        Fences.wording(self.module, self.rules)
        self.caught(Fences.wording, swap(self.module, '"Land your Battle Engine first."', '"Land your Battle Engine first"'), self.rules)
        self.caught(Fences.wording, self.module, swap(self.rules, '"That unit has no weapon."', '"That unit has no weapon [test]."'))
        self.caught(Fences.wording, swap(self.module, '"the Morph key", "Back in your Battle Engine."', '"the Morph key", "Back in your Battle Engine (see ce.ini)."'), self.rules)

    def test_the_drawing_is_the_huds_own(self):
        Fences.drawing(self.hud, self.hud_h, self.notice)
        self.caught(Fences.drawing, swap(self.hud, "		HUD.RenderObjectiveBar(cx, cy, full, height, 1.f);\n		OverlayStates();", "		OverlayStates();"
                                         ).replace("HUD.RenderObjectiveBar(", "DrawBox("), self.hud_h, self.notice)
        self.caught(Fences.drawing, swap(self.hud, "const unsigned long long kShownMs = 4000;", "const unsigned long long kShownMs = 1500;"), self.hud_h, self.notice)
        self.caught(Fences.drawing, swap(self.hud, "	if (step > kStepMs) step = kStepMs;\n", ""), self.hud_h, self.notice)
        self.caught(Fences.drawing, swap(self.hud, "	if (HUD.mCrosshairDot)\n		CSPRITERENDERER::DrawSprite(cx, cy, 0.011f, HUD.mCrosshairDot,", "	if (HUD.mCrosshairPrimary)\n		CSPRITERENDERER::DrawSprite(cx, cy, 0.011f, HUD.mCrosshairPrimary,"), self.hud_h, self.notice)
        self.caught(Fences.drawing, swap(self.hud, "	const char *word = NULL;\n	unsigned colour = kWhite;", "	const char *word = \"READY\";\n	unsigned colour = kWhite;"), self.hud_h, self.notice)
        self.caught(Fences.drawing, swap(self.hud, "#include <string.h>\n", "#include <string.h>\n#include <stdlib.h>\nstatic const char *gEnv = getenv(\"X\");\n"), self.hud_h, self.notice)


class BoundTakeHintTests(unittest.TestCase):
    def test_prompts_name_the_selected_action_without_claiming_an_occupied_key(self):
        import subprocess
        import tempfile
        root = Path(__file__).resolve().parents[2]
        module = (root / "port/game/ce_playunit.cpp").read_text()
        take = function(module, "bool Take(")
        start = take.index("if (gOnFoot)", take.index("MorphKeyName(key,"))
        last = take.index('PortCeActionName(CeControlAction::TakeUnit));', take.index('"Morph: Return'))
        stop = take.index(';', last) + 1
        notice = take[start:stop]
        sight = function(module, "const char *SightHint(")
        # Compile the actual source slices and query them with a synthetic
        # rebound key and two Morph-name variants, without game assets.
        driver = r"""
#include <cstdio>
#include <cstring>
#include <string>
enum class CeControlAction { TakeUnit };
const int CE_PLAYUNIT_REFUSE_COUNT=9;
static bool freeKey=true;
static const char *current="J";
bool TakeKeyFree(){return freeKey;}
const char *PortCeActionName(CeControlAction a){return a==CeControlAction::TakeUnit ? current : "";}
""" + sight + r"""
std::string notice(const char *key){
 const bool gOnFoot=false; char second[160]={};
""" + notice + r"""
 return second;
}
#define EXPECT(x) do { if (!(x)) { std::fprintf(stderr,"hint expectation failed at line %d\n",__LINE__); return 1; } } while(false)
int main(){
 EXPECT(!std::strcmp(SightHint(9),"Aim at one and press J."));
 EXPECT(SightHint(0)==nullptr);
 EXPECT(notice("Z")=="Z: Return   J: Take Another");
 EXPECT(notice("Morph")=="Morph: Return   J: Take Another");
 current="L";EXPECT(!std::strcmp(SightHint(9),"Aim at one and press L."));
 freeKey=false;EXPECT(SightHint(9)==nullptr);
 EXPECT(notice("Z")=="Z: Return to Battle Engine");
 EXPECT(notice("Morph")=="Morph: Return to Battle Engine");
}
"""
        directory=root/"local-data/port/ce-playunit-entry-tests"
        directory.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=directory) as folder:
            exe=Path(folder)/"bound-hints"
            subprocess.run(["c++","-std=c++17","-x","c++","-","-o",str(exe)], input=driver,
                           capture_output=True,text=True,check=True)
            trial=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(trial.returncode,0,trial.stderr)


class SharedPilotInputTests(unittest.TestCase):
    def test_take_edges_follow_the_current_view_across_open_and_close(self):
        import tempfile, subprocess
        from pathlib import Path
        code = comments_off(MODULE.read_text())
        body = function(code, "void CePlayUnitMapBoundary()")
        owns = function(code, "bool CePlayUnitOwnsPilot()")
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            src = d / "input.cpp"
            src.write_text('''#include "ce_playunit_rules.h"
#include <cassert>
enum { DIK_V = 47, DIK_Q = 16, DIK_U = 22, DIK_C = 46, DIK_F1 = 59 };
bool gDriving, gControllerPending, gConfigured, gWanted, gTakePressed, gViewTogglePressed, gUnloadPressed, gSpotPressed, gWeaponSwitchPressed;
void *gCamera;
unsigned gSwitchWait;
CePlayUnitKey gTakeKey = {}, gViewToggleKey = {}, gUnloadKey = {}, gSpotKey = {}, gWeaponSwitchKey = {}, gHelpHintKey = {};
int TakeKey(){ return 1; }
int held, polls;
int PortKeyHeldNow(int) { ++polls; return held; }
''' + owns + "\n" + body + '''
int main() {
    held=1; CePlayUnitMapBoundary(); assert(polls==0);
    gConfigured=true; CePlayUnitMapBoundary(); assert(polls==0);
    gWanted=true; gTakePressed=true; gViewTogglePressed=true; gWeaponSwitchPressed=true; CePlayUnitMapBoundary();
    assert(!gTakePressed && !gViewTogglePressed && !gWeaponSwitchPressed && !CePlayUnitPressed(&gTakeKey,true,false));
    assert(!CePlayUnitPressed(&gViewToggleKey,true,true));
    assert(!CePlayUnitPressed(&gViewToggleKey,false,true));
    assert(CePlayUnitPressed(&gViewToggleKey,true,true));
    assert(!CePlayUnitPressed(&gWeaponSwitchKey,true,true));
    assert(!CePlayUnitPressed(&gWeaponSwitchKey,false,true));
    assert(CePlayUnitPressed(&gWeaponSwitchKey,true,true));
    CePlayUnitMapBoundary();
    assert(!CePlayUnitPressed(&gTakeKey,true,true));
    assert(!CePlayUnitPressed(&gTakeKey,false,true));
    assert(CePlayUnitPressed(&gTakeKey,true,true));
    held=0; CePlayUnitMapBoundary();
    assert(CePlayUnitPressed(&gTakeKey,true,true));
    assert(CePlayUnitPressed(&gViewToggleKey,true,true));
    assert(CePlayUnitPressed(&gWeaponSwitchKey,true,true));
    assert(!CePlayUnitOwnsPilot());
    gDriving=true; assert(CePlayUnitOwnsPilot()); gDriving=false;
    gControllerPending=true; assert(CePlayUnitOwnsPilot()); gControllerPending=false;
    gCamera=&held; assert(CePlayUnitOwnsPilot()); gCamera=0;
    gSwitchWait=1; assert(CePlayUnitOwnsPilot()); gSwitchWait=0;
    assert(!CePlayUnitOwnsPilot());
}
''')
            subprocess.run(["c++","-std=c++11","-I",str(MODULE.parent),str(src),"-o",str(d/"input")],check=True)
            subprocess.run([str(d/"input")],check=True)


class UnitViewHelpTests(unittest.TestCase):
    def test_available_views_and_saved_bindings_reach_the_real_prompt(self):
        import subprocess
        import resource
        import tempfile
        root = Path(__file__).resolve().parents[2]
        module = (root / "port/game/ce_playunit.cpp").read_text()
        helpers = "\n".join(function(module, name) for name in (
            "void UnitKeyName(", "void MorphKeyName(", "void UnitViewHelp("))
        take = function(module, "bool Take(")
        start = take.index("if (gViewMode == kViewLook)", take.index("MorphKeyName(key,"))
        stop = take.index("CePlayUnitNotice(first,", start)
        prompt = take[start:stop]
        constants = sorted(set(re.findall(r"\bDIK_[A-Z_]+\b", helpers)))
        action = re.search(r"PortCeActionName\(([^)]+)\)", helpers).group(1)
        declarations = """
#include <cstdio>
#include <cstring>
enum { KEY_ON=1000, KEY_ONCE, KEY_RELEASE, MOUSE_BUTTON_ON,
       MOUSE_BUTTON_ONCE, MOUSE_BUTTON_RELEASE, VK_F1=112, VK_F12=123 };
enum { BUTTON_MECH_MORPH=1, BUTTON_MECH_CHANGE_ZOOM_IN, BUTTON_MECH_CHANGE_ZOOM_OUT };
struct SControlInput { int mPad, mType, mKey, mVKey; };
struct SControlBinding { int mId; SControlInput mInput[4]; };
SControlBinding CONTROL_BINDINGS[5];
bool gFootDrive, gVehicleDrive, gOnFoot, takeFree, gUnitViewMade;
struct { bool valid, firstPerson; } gUnitViewRig = { false, false };
const int kViewLook=1; int gViewMode=kViewLook;
const char *takeName="P";
bool TakeKeyFree() { return takeFree; }
const char *PortCeActionName(int) { return takeName; }
"""
        declarations += "enum { " + ", ".join(constants) + " };\n"
        owner, member = action.split("::")
        declarations += "struct " + owner + " { enum { " + member + "=1 }; };\n"
        driver = declarations + helpers + """
void Prompt(char *out) {
    char second[96], key[24]; MorphKeyName(key, sizeof(key));
""" + prompt + """
    std::snprintf(out, 96, "%s", second);
}
int checks=0, failures=0;
#define EXPECT(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr,"failed check %d\\n",checks); } } while (0)
void Bind(int slot, int id, int type, int key, int vkey=0, int pad=0) {
    CONTROL_BINDINGS[slot].mId=id;
    CONTROL_BINDINGS[slot].mInput[0]=SControlInput{pad,type,key,vkey};
}
void Fresh() {
    std::memset(CONTROL_BINDINGS,0,sizeof(CONTROL_BINDINGS));
    for (int i=0;i<5;++i) CONTROL_BINDINGS[i].mId=-1;
    Bind(0,BUTTON_MECH_MORPH,KEY_ON,999,'B');
    Bind(1,BUTTON_MECH_CHANGE_ZOOM_IN,KEY_ONCE,999,'R');
    Bind(2,BUTTON_MECH_CHANGE_ZOOM_OUT,KEY_RELEASE,999,'F');
    gFootDrive=true; gVehicleDrive=false; gOnFoot=true; takeFree=true;
    gUnitViewMade=false; gUnitViewRig={false,false};
}
int main() {
    char out[96], name[24]; Fresh(); Prompt(out);
    EXPECT(std::strstr(out,"R/F: Wide/Close/First")!=0);
    EXPECT(std::strstr(out,"B: Return")!=0);
    EXPECT(std::strstr(out,"P: Take")!=0);
    EXPECT(std::strstr(out,"Zoom In")==0);
    gFootDrive=false; gVehicleDrive=true; gOnFoot=false; Prompt(out);
    EXPECT(std::strstr(out,"R/F: Wide/Close/First")!=0);
    gUnitViewMade=true; gUnitViewRig={true,false}; Prompt(out);
    EXPECT(std::strstr(out,"R/F: Wide/Close ")!=0 && std::strstr(out,"First")==0);
    gUnitViewRig={true,true}; Prompt(out);
    EXPECT(std::strstr(out,"R/F: Wide/Close/First")!=0);
    takeFree=false; Prompt(out);
    EXPECT(std::strstr(out,": Take")==0);
    EXPECT(std::strstr(out,"B: Return")!=0);
    Fresh(); Bind(1,BUTTON_MECH_CHANGE_ZOOM_IN,MOUSE_BUTTON_ONCE,3);
    Bind(2,BUTTON_MECH_CHANGE_ZOOM_OUT,MOUSE_BUTTON_RELEASE,4); Prompt(out);
    EXPECT(std::strstr(out,"Wheel: Wide/Close/First")!=0);
    Bind(0,BUTTON_MECH_MORPH,MOUSE_BUTTON_ON,1); Prompt(out);
    EXPECT(std::strstr(out,"Middle mouse: Return")!=0);
    Bind(0,BUTTON_MECH_MORPH,MOUSE_BUTTON_ON,2); Prompt(out);
    EXPECT(std::strstr(out,"Right mouse: Return")!=0);
    Fresh(); Bind(0,BUTTON_MECH_MORPH,KEY_ON,DIK_RSHIFT);
    Bind(1,BUTTON_MECH_CHANGE_ZOOM_IN,KEY_ON,DIK_LCONTROL);
    Bind(2,BUTTON_MECH_CHANGE_ZOOM_OUT,KEY_ON,DIK_BACKSLASH); Prompt(out);
    EXPECT(std::strstr(out,"Left Ctrl/Backslash")!=0);
    EXPECT(std::strstr(out,"Right Shift: Return")!=0);
    EXPECT(std::strstr(out,"P: Take")!=0);
    EXPECT(std::strlen(out)<sizeof(out));
    Fresh(); Bind(1,BUTTON_MECH_CHANGE_ZOOM_IN,KEY_ON,999,VK_F12);
    Bind(2,BUTTON_MECH_CHANGE_ZOOM_OUT,KEY_ON,999,'5'); Prompt(out);
    EXPECT(std::strstr(out,"F12/5: Wide/Close/First")!=0);
    Fresh(); CONTROL_BINDINGS[1].mInput[0].mType=0;
    CONTROL_BINDINGS[1].mInput[1]=SControlInput{0,KEY_ON,DIK_UP,0}; Prompt(out);
    EXPECT(std::strstr(out,"Up arrow/F: Wide/Close/First")!=0);
    UnitKeyName(name,sizeof(name),999,"Unbound action");
    EXPECT(std::strcmp(name,"Unbound action")==0);
    std::printf("checks=%d failures=%d\\n",checks,failures);
    return failures ? 1 : 0;
}
"""
        variants = {
            "normal": driver,
            "drop-first-person": driver.replace(
                '"Wide/Close/First"', '"Wide/Close"'),
            "ignore-saved-zoom": driver.replace(
                'BUTTON_MECH_CHANGE_ZOOM_OUT, "Zoom Out"',
                'BUTTON_MECH_CHANGE_ZOOM_IN, "Zoom Out"'),
            "occupied-take": driver.replace("if (TakeKeyFree())", "if (true)"),
            "old-take-prompt": driver.replace("UnitViewHelp(second, sizeof(second));",
                'std::snprintf(second,sizeof(second),"Zoom In: First Person");'),
        }
        directory = root / "local-data/port/ce-playunit-entry-tests"
        directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=directory) as folder:
            for name, text in variants.items():
                with self.subTest(name=name):
                    if name != "normal":
                        self.assertNotEqual(text, driver)
                    exe = Path(folder) / name
                    built = subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra",
                        "-Werror", "-x", "c++", "-", "-o", str(exe)], input=text,
                        capture_output=True, text=True, check=False)
                    self.assertEqual(built.returncode, 0, built.stderr)
                    trial = subprocess.run([str(exe)], capture_output=True, text=True,
                        preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
                    self.assertEqual(trial.returncode, 0 if name == "normal" else 1,
                                     trial.stdout + trial.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
