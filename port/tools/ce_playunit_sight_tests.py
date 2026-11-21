#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Play as a unit, which unit the take key means and how a switch ends (port/game/ce_playunit.cpp, ce_playunit_rules.h): the production regions compiled with fake units.

The module's two world-facing regions of the take key, between their "(begin)" and "(end)" lines, are taken from the source as it stands and compiled with the real rules
header against a fake world (a list of units with places, sizes and facts, flat ground with one ridge):
- the sight (Sight): a press takes the unit at the reticle in the picture the player saw; a unit beside the line of view but away from the reticle, one the ground hides,
  the unit driven now and the Battle Engine itself are never taken; a refused unit at the reticle says why; a takeable unit wins over a nearer refused one;
- the switch (FinishSwitch): a unit switched to is taken when the controls may be taken, the wait gives up (once, with a sentence) when the unit is gone, no longer
  takeable, or the wait is over, and a switch never waits for ever.
Causal controls change the production text (the line of sight, the reticle, the exclusion of the held unit or of the engine dropped; the wait kept from counting down; the
give-up's sentence or the readiness not asked) and each must make the fixture fail. What the real game shows at a press is the test build's probe ("take view", "take unit"
lines with each unit's place on the screen) beside a picture of the same frame.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
MODULE = PORT / "game/ce_playunit.cpp"
REGIONS = (("// ---- the take key's sight (begin)", "// ---- the take key's sight (end)"), ("// ---- the switch (begin)", "// ---- the switch (end)"))


def production_regions():
    source = MODULE.read_text()
    out = []
    for begin, end in REGIONS:
        start = source.index(begin)
        out.append(source[start:source.index(end, start)])
    return out


DRIVER = r'''
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "ce_playunit_rules.h"
struct FVector { float X, Y, Z; FVector(float x=0, float y=0, float z=0): X(x), Y(y), Z(z) {} };
enum { THING_TYPE_UNIT = 1 };
struct CThing {
    unsigned long type = THING_TYPE_UNIT; int number = 0; FVector pos; float radius = 2.f; bool friendly = true, takeable = true, infantry = false;
    unsigned long GetThingType() { return type; }
    int GetThingNumber() { return number; }
};
struct CUnit : CThing { FVector GetPos() { return pos; } float GetRadius() { return radius; } };
struct CBattleEngine : CUnit {};
struct CPlayer {};
struct CCONTROLLER {};
struct ThingList { std::vector<CThing *> items; };
struct World { ThingList list; ThingList &GetThingNB() { return list; } } WORLD;
template <class T> struct ListIterator {
    ThingList *l; size_t i = 0;
    explicit ListIterator(ThingList *list): l(list) {}
    T *First() { i = 0; return Next(); }
    T *Next() { return i < l->items.size() ? (T *)l->items[i++] : nullptr; }
};
// the ground: flat at z 0 (up is negative z), with a ridge thirty units high across x 70..75
float TerrainHeight(void *, float x, float) { return (x > 70.f && x < 75.f) ? -30.f : 0.f; }
CePlayUnitFacts Facts(CUnit *u, CBattleEngine *)
{
    CePlayUnitFacts f;
    memset(&f, 0, sizeof(f));
    f.friendly = u->friendly; f.alive = true; f.armed = true; f.normalSquad = true;
    f.vehicle = u->takeable; f.groundUnit = u->takeable;
    f.family = u->infantry ? CE_PLAYUNIT_FAMILY_INFANTRY : CE_PLAYUNIT_FAMILY_GROUND_VEHICLE;
    if (!u->takeable && !u->infantry) f.family = CE_PLAYUNIT_FAMILY_OTHER;
    return f;
}
// the frame: a 640x480 picture from an eye at (0, 0, -10) looking along +x, right along -y, up along -z (a plain pinhole camera, 90 degrees across)
bool ScreenPlace(CUnit *u, const float *eye, CePlayUnitScreenPlace *out)
{
    const float dx = u->pos.X - eye[0], dy = u->pos.Y - eye[1], dz = u->pos.Z - eye[2];
    out->depth = dx;
    out->distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dx <= 0.f) { out->x = out->y = -1.f; out->radius = 0.f; return true; }
    out->x = 320.f + (-dy / dx) * 320.f;
    out->y = 240.f + (dz / dx) * 320.f;
    out->radius = u->radius * kCePlayUnitSightBody * 320.f / dx;
    return true;
}
// the switch's world
unsigned gSwitchWait; CUnit *gSwitchUnit; int gSwitchThing = -1;
bool gReady; int gTakes, gRefusals; const char *gLastRefusal;
CPlayer gPlayerObj; CCONTROLLER gControllerObj; CBattleEngine gEngineObj;
bool Ready(CPlayer **p, CBattleEngine **e, CCONTROLLER **c) { if (!gReady) return false; *p = &gPlayerObj; *e = &gEngineObj; *c = &gControllerObj; return true; }
bool Take(CPlayer *, CBattleEngine *, CCONTROLLER *, CUnit *, int) { ++gTakes; return true; }
void Refuse(const char *sentence, const char * = NULL) { ++gRefusals; gLastRefusal = sentence; }

@SIGHT@
@SWITCH@

static int checks, failed;
static void Check(bool good, const char *name) { ++checks; if (good) printf("ok %s\n", name); else { ++failed; printf("FAIL %s\n", name); } }
static const float kEye[3] = {0, 0, -10};
static CUnit *Unit(int number, float x, float y, float z, bool takeable = true, bool friendly = true)
{
    CUnit *u = new CUnit; u->number = number; u->pos = FVector(x, y, z); u->takeable = takeable; u->friendly = friendly;
    WORLD.list.items.push_back(u);
    return u;
}
static void Clear() { for (CThing *t : WORLD.list.items) delete t; WORLD.list.items.clear(); }
static CUnit *Press(CBattleEngine *engine, CUnit *exclude, int *why)
{
    int family = 0, kind = -1;
    CUnit *u = Sight(engine, kEye, 640.f, 480.f, exclude, why, &family, &kind);
    if (u && kind < 0) return nullptr;
    return u;
}

int main()
{
    int why = -9;
    // the unit at the reticle, sixty units ahead
    CUnit *ahead = Unit(1, 60, 1, -8);
    Check(Press(nullptr, nullptr, &why) == ahead && why == CE_PLAYUNIT_TAKE_OK, "the_unit_at_the_reticle_is_taken");
    Clear();
    // a tank fifteen units off, three and a half beside the line of view: in the picture, far from the reticle (the review's case)
    Unit(2, 15, -3.5f, -9);
    Check(Press(nullptr, nullptr, &why) == nullptr && why == CE_PLAYUNIT_REFUSE_COUNT, "a_unit_beside_the_line_away_from_the_reticle_is_not_taken");
    Clear();
    // a unit behind the ridge, on the line of view
    Unit(3, 90, 0, -1);
    Check(Press(nullptr, nullptr, &why) == nullptr && why == CE_PLAYUNIT_REFUSE_COUNT, "a_unit_the_ground_hides_is_not_taken");
    Clear();
    // the unit driven now, at the reticle
    CUnit *held = Unit(4, 30, 0, -9);
    Check(Press(nullptr, held, &why) == nullptr && why == CE_PLAYUNIT_REFUSE_COUNT, "the_unit_driven_now_is_not_taken_again");
    Clear();
    // the Battle Engine itself at the reticle
    CBattleEngine *engine = new CBattleEngine; engine->number = 5; engine->pos = FVector(30, 0, -9); WORLD.list.items.push_back(engine);
    Check(Press(engine, nullptr, &why) == nullptr && why == CE_PLAYUNIT_REFUSE_COUNT, "the_battle_engine_is_not_taken");
    Clear();
    // an enemy at the reticle: the player is told why
    Unit(6, 50, 0, -9, true, false);
    Check(Press(nullptr, nullptr, &why) == nullptr && why == CE_PLAYUNIT_REFUSE_ENEMY, "a_refused_unit_at_the_reticle_says_why");
    Clear();
    // a takeable unit a little off the middle wins over a refused one at the middle
    Unit(7, 30, 0, -9, false, true);
    CUnit *tank = Unit(8, 60, -3, -9);
    Check(Press(nullptr, nullptr, &why) == tank && why == CE_PLAYUNIT_TAKE_OK, "a_takeable_unit_wins_over_a_nearer_refused_one");
    Clear();
    // behind the eye
    Unit(9, -30, 0, -9);
    Check(Press(nullptr, nullptr, &why) == nullptr, "a_unit_behind_the_eye_is_not_taken");
    Clear();

    // the switch: ready at once
    CUnit *next = Unit(10, 60, 0, -9);
    gSwitchUnit = next; gSwitchThing = 10; gSwitchWait = kCePlayUnitSwitchWait; gReady = true; gTakes = gRefusals = 0;
    FinishSwitch();
    Check(gTakes == 1 && gRefusals == 0 && gSwitchWait == 0 && gSwitchUnit == nullptr, "a_switch_takes_when_ready");
    // never ready: the wait ends, once, with a sentence
    gSwitchUnit = next; gSwitchThing = 10; gSwitchWait = kCePlayUnitSwitchWait; gReady = false; gTakes = gRefusals = 0;
    unsigned ticks = 0;
    while (gSwitchWait && ticks < 1000) { FinishSwitch(); ++ticks; }
    for (int i = 0; i < 5; ++i) FinishSwitch();
    Check(ticks == kCePlayUnitSwitchWait && gTakes == 0 && gRefusals == 1 && gSwitchUnit == nullptr, "a_switch_never_ready_gives_up_once_when_the_wait_is_over");
    // the unit is gone
    gSwitchUnit = next; gSwitchThing = 99; gSwitchWait = kCePlayUnitSwitchWait; gReady = true; gTakes = gRefusals = 0;
    FinishSwitch();
    Check(gTakes == 0 && gRefusals == 1 && gSwitchWait == 0, "a_switch_to_a_unit_that_is_gone_gives_up_at_once");
    // no longer takeable
    next->takeable = false;
    gSwitchUnit = next; gSwitchThing = 10; gSwitchWait = kCePlayUnitSwitchWait; gReady = true; gTakes = gRefusals = 0;
    FinishSwitch();
    Check(gTakes == 0 && gRefusals == 1 && gSwitchWait == 0, "a_switch_to_a_unit_no_longer_takeable_gives_up_at_once");
    // nothing to do without a wait
    gSwitchWait = 0; gTakes = gRefusals = 0; FinishSwitch();
    Check(gTakes == 0 && gRefusals == 0, "no_wait_no_switch");
    Clear();
    printf("checks %d failed %d\n", checks, failed);
    return failed ? 1 : 0;
}
'''


class SightAndSwitch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data" / "ce-playunit-sight-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(dir=parent)
        cls.scratch = Path(cls.tmp.name)
        cls.sight, cls.switch = production_regions()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_control(self, name, sight, switch):
        folder = self.scratch / name
        folder.mkdir()
        cpp = folder / "driver.cpp"
        cpp.write_text(DRIVER.replace("@SIGHT@", sight).replace("@SWITCH@", switch))
        exe = folder / "driver"
        built = subprocess.run(["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-parameter", "-I", str(PORT / "game"), str(cpp), "-o", str(exe)],
                               capture_output=True, text=True, timeout=120)
        self.assertEqual(built.returncode, 0, "the fixture did not compile:\n" + built.stdout[-1500:] + built.stderr[-1500:])
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        return ran.returncode, ran.stdout.splitlines()

    def test_the_production_regions_hold(self):
        status, lines = self.run_control("native", self.sight, self.switch)
        self.assertEqual([l for l in lines if l.startswith("FAIL")], [])
        self.assertEqual(status, 0)
        summary = [l for l in lines if l.startswith("checks ")]
        self.assertEqual(len(summary), 1)
        self.assertGreaterEqual(int(summary[0].split()[1]), 13, "too few checks ran")

    def test_causal_controls(self):
        line_of_sight = "		if (!CePlayUnitLineClear(eye, above, kCePlayUnitSightSamples, kCePlayUnitSightTolerance, TerrainHeight, NULL)) continue;\n"
        controls = {
            "no_line_of_sight": ("sight", line_of_sight, "		(void)above;\n", "a_unit_the_ground_hides_is_not_taken"),
            "the_held_unit_is_not_left_out": ("sight", " || t == (CThing *)exclude", "", "the_unit_driven_now_is_not_taken_again"),
            "the_engine_is_not_left_out": ("sight", " || t == (CThing *)engine", "", "the_battle_engine_is_not_taken"),
            "the_reticle_is_not_asked": ("sight", " || score > kCePlayUnitSightReticle", "", "a_unit_beside_the_line_away_from_the_reticle_is_not_taken"),
            "the_frame_is_not_asked": ("sight", "!CePlayUnitAtScreen(place, w, h, kCePlayUnitSightReach, &score) ||", "(CePlayUnitAtScreen(place, w, h, kCePlayUnitSightReach, &score), score = score < 0.f ? 0.f : score, false) ||", "a_unit_behind_the_eye_is_not_taken"),
            "the_place_is_not_asked": ("sight", "if (!ScreenPlace(u, eye, &place) || ", "if (", "the_unit_at_the_reticle_is_taken"),
            "the_nearest_refused_unit_is_not_said": ("sight", "	if (nearest >= 0) *family = families[nearest];\n", "	*why = CE_PLAYUNIT_REFUSE_COUNT;\n", "a_refused_unit_at_the_reticle_says_why"),
            "the_wait_never_counts_down": ("switch", "switch (CePlayUnitSwitchStep(&gSwitchWait,", "unsigned spare = gSwitchWait;\n	switch (CePlayUnitSwitchStep(&spare,", "a_switch_never_ready_gives_up_once_when_the_wait_is_over"),
            "the_give_up_says_nothing": ("switch", "		Refuse(\"That unit cannot be taken now.\", \"Back in your Battle Engine.\");\n", "", "a_switch_never_ready_gives_up_once_when_the_wait_is_over"),
            "readiness_not_asked": ("switch", "u != NULL, ready, kind >= 0", "u != NULL, true, kind >= 0", "a_switch_never_ready_gives_up_once_when_the_wait_is_over"),
            "identity_not_asked": ("switch", " && t->GetThingNumber() == gSwitchThing", "", "a_switch_to_a_unit_that_is_gone_gives_up_at_once"),
        }
        for name, (which, before, after, expected) in controls.items():
            with self.subTest(control=name):
                text = self.sight if which == "sight" else self.switch
                self.assertEqual(text.count(before), 1, f"the text a control changes is not unique: {before[:60]!r}")
                changed = text.replace(before, after)
                status, lines = self.run_control("control-" + name, changed if which == "sight" else self.sight, changed if which == "switch" else self.switch)
                self.assertNotEqual(status, 0, f"control escaped: {name}")
                self.assertIn("FAIL " + expected, lines, f"the fixture did not say {expected!r}: {[l for l in lines if l.startswith('FAIL')][:4]}")

    def test_both_presses_use_the_picture_the_player_saw(self):
        code = "\n".join(l for l in MODULE.read_text().split("\n") if not l.strip().startswith("//"))
        self.assertEqual(code.count("RenderedEye("), 3, "one definition, one call for a take and one for a switch")
        self.assertIn("ENGINE.GetCameraForViewpoint(0)", code[code.index("bool RenderedEye("):code.index("bool ScreenPlace(")])
        place = code[code.index("bool ScreenPlace("):code.index("// ---- the take key's sight (begin)")] if "// ---- the take key's sight (begin)" in code else code
        for kept in ("ENGINE.mWorldMatrices[0]", "ENGINE.mViewMatrices[0]", "ENGINE.mProjectionMatrices[0]", "ENGINE.GetViewportForViewpoint(0)", "D3DXVec3Project(", "LT.mScreenShapeScale"):
            self.assertIn(kept, code[code.index("bool ScreenPlace("):], f"the place on the screen is not the frame's own projection: {kept}")
        self.assertIn("RenderedEye(GAME.GetCurrentCamera(0), eye) ? Sight(engine, eye, w, h, NULL,", code)
        self.assertIn("RenderedEye(gCamera, eye) ? Sight(engine, eye, w, h, held,", code)


if __name__ == "__main__":
    unittest.main(verbosity=2)
