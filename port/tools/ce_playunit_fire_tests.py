#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Play as a unit, free fire (port/game/ce_playunit.cpp, ce_playunit_rules.h): the production fire region compiled against fake weapons.

The module's free-fire region (between its "free fire (begin)" and "free fire (end)" lines: the ground height, the weapon choice, the view's aim and the native calls) is taken
from the source as it stands and compiled with the real rules header against a fake unit and a fake map. The fake unit mimics the stock handler's phases as they were read from
the game's code (aim; the second phase arms, the first discharges once the unit stands armed; a shot leaves the weapon recovering) and records every call, so what is checked is
who owns the trigger and which native calls are made, with no enemy, no world and no projectile simulation: only the game's own run (a test build's probe on level 201) shows
what the real weapon does with the point. Causal controls change the production text (an AI target kept, the arm or the discharge left out, the held-input gate dropped, the
range or the line not asked, the sky given a point, a cleared weapon not replaced) and each must make the fixture fail.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
MODULE = PORT / "game/ce_playunit.cpp"
BEGIN = "// ---- free fire (begin)"
END = "// ---- free fire (end)"


def production_region():
    source = MODULE.read_text()
    start = source.index(BEGIN)
    return source[start:source.index(END, start)]


DRIVER = r'''
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include <cstddef>
#include "ce_playunit_rules.h"
#include "ce_playunit_tactics.h"
typedef intptr_t SINT;
typedef int BOOL;
#define FALSE 0
#define TRUE 1
struct FVector {
    float X, Y, Z;
    FVector(float x=0, float y=0, float z=0): X(x), Y(y), Z(z) {}
};
// the map: z down, so a higher ground has a lower z
static int gTerrain = 0;		// 0 flat, 1 a ridge, two units high, across y in -8..-6
struct Map { float Collide(FVector &p) { if (gTerrain == 1 && p.Y > -8.f && p.Y < -6.f) return -2.f; return 0.f; } } MAP;
struct CWeapon {
    void *mMode = nullptr;
    float lo = 0.f, hi = 80.f;
    bool ready = true;
    BOOL ReadyToFire() { return ready; }
    float GetMinRange(FVector) { return lo; }
    float GetMaxRange(FVector) { return hi; }
};
template <class T> struct SPtrSet { std::vector<T*> items; };
template <class T> struct ListIterator {
    std::vector<T*> *list; size_t at = 0;
    explicit ListIterator(SPtrSet<T> *s): list(&s->items) {}
    T *First() { at = 0; return Next(); }
    T *Next() { return at < list->size() ? (*list)[at++] : nullptr; }
};
struct CUnit {
    CWeapon *mCurrentWeapon = nullptr;
    SPtrSet<CWeapon> mWeapons;
    FVector pos{0.f, 0.f, -1.f};
    int mUnknown168 = 0;			// arming begun
    bool armed = false;			// stands ready to discharge
    int armTicks = 0, recoverTicks = 0;
    // what was asked
    std::vector<int> order;			// 1 aim, 2 discharge, 3 arm
    int aimCalls = 0, armCalls = 0, fireCalls = 0, rounds = 0;
    SINT lastTarget = -1;
    FVector aimed;
    FVector GetPos() const { return pos; }
    void WeaponUnknown509140(SINT target, SINT point) {
        ++aimCalls; order.push_back(1); lastTarget = target;
        aimed = *reinterpret_cast<FVector*>(point);
    }
    BOOL Unknown84() { return mCurrentWeapon && armed && mCurrentWeapon->ready; }
    BOOL Unknown74() {
        ++fireCalls; order.push_back(2);
        if (mCurrentWeapon && armed && mCurrentWeapon->ready) {
            ++rounds; mCurrentWeapon->ready = false; recoverTicks = 6; armed = false; mUnknown168 = 0; return TRUE;
        }
        return FALSE;
    }
    BOOL Unknown86() {
        ++armCalls; order.push_back(3);
        if (!mCurrentWeapon || mUnknown168 || armed || !mCurrentWeapon->ready) return FALSE;
        mUnknown168 = 1; armTicks = 3; return TRUE;
    }
    // the unit's own time: arming completes, recovery ends
    void World() {
        if (mUnknown168 && armTicks > 0 && --armTicks == 0) armed = true;
        if (mCurrentWeapon && !mCurrentWeapon->ready && recoverTicks > 0 && --recoverTicks == 0) mCurrentWeapon->ready = true;
    }
};
static FVector gAimPoint;
static bool gAimValid = false;
@REGION@
static int failed = 0, checks = 0;
static void check(bool ok, const char *name) { ++checks; if (!ok) { ++failed; std::printf("FAIL %s\n", name); } }
static bool near(float a, float b, float e = 0.02f) { return std::fabs(a - b) < e; }
static CePlayUnitLaw Law() { return CePlayUnitClasses()[CE_PLAYUNIT_CLASS_TANK].law; }

struct Run {
    CUnit u; CWeapon gun; std::vector<int> states; FVector aim; bool haveAim = false;
    FVector eye{0.f, -10.f, -8.f}, look{0.f, 0.6f, 0.8f};		// the view: ground 10 along the ray, at y = -4
    Run() { gun.mMode = &gun; u.mCurrentWeapon = &gun; u.mWeapons.items = {&gun}; }
    // one tick, as the module's FireTick makes it (the gate it asks first is the rules' own, checked in ce_playunit_tests.py)
    int Tick(bool held) {
        const CePlayUnitLaw law = Law();
        const int state = FireAim(&u, eye, look, law, &aim, &haveAim);
        DriveFire(&u, aim, haveAim, state, held);
        u.World();
        states.push_back(state);
        return state;
    }
    bool Saw(int s) const { for (int v : states) if (v == s) return true; return false; }
};

int main() {
    // no enemy anywhere: held fire arms, discharges, recovers and arms again, always with no target and at the ground point under the view
    {
        Run r; for (int i = 0; i < 40; ++i) r.Tick(true);
        check(r.u.rounds >= 3, "no_enemy_still_fires");
        check(r.u.lastTarget == 0, "aim_has_no_target");
        check(near(r.u.aimed.X, 0.f) && near(r.u.aimed.Y, -4.f) && near(r.u.aimed.Z, 0.f), "aim_is_the_ground_under_the_view");
        check(r.Saw(CE_PLAYUNIT_FIRE_READY) && r.Saw(CE_PLAYUNIT_FIRE_ARMING) && r.Saw(CE_PLAYUNIT_FIRE_RECOVERING), "state_goes_through_ready_arming_recovering");
        check(r.u.armCalls >= 3 && r.u.fireCalls >= 3, "uses_the_native_arm_and_the_native_discharge");
        // the order inside the held ticks: each aim is followed by the arm (not armed) or the discharge (armed), never both in one tick
        bool paired = r.u.order.size() > 0 && r.u.order.size() % 2 == 0;
        for (size_t i = 0; paired && i + 1 < r.u.order.size(); i += 2) paired = r.u.order[i] == 1 && (r.u.order[i + 1] == 2 || r.u.order[i + 1] == 3);
        check(paired, "aim_comes_before_arm_or_discharge");
        check(r.u.rounds <= 40 / 4, "no_shortcut_on_the_rate_of_fire");
        // a discharge happens only after an arm and never in the tick that arms
        int arms = 0;
        for (int v : r.u.order) if (v == 3) ++arms;
        check(r.u.rounds <= arms, "every_round_follows_an_arm");
    }
    // no hold, no calls
    { Run r; for (int i = 0; i < 20; ++i) r.Tick(false); check(r.u.aimCalls == 0 && r.u.armCalls == 0 && r.u.fireCalls == 0 && r.u.rounds == 0, "no_hold_no_native_calls");
      check(r.Saw(CE_PLAYUNIT_FIRE_READY), "state_is_told_without_a_hold"); }
    // the sky: the point is along the look, brought in to the weapon's reach on the same line, and fired at
    { Run r; r.look = FVector(0.f, 0.6f, -0.8f); for (int i = 0; i < 40; ++i) r.Tick(true);
      const float dx = r.u.aimed.X - r.u.pos.X, dy = r.u.aimed.Y - r.u.pos.Y, dz = r.u.aimed.Z - r.u.pos.Z;
      check(r.u.rounds >= 1 && near(std::sqrt(dx * dx + dy * dy + dz * dz), 80.f, 0.1f), "the_sky_is_fired_at_the_weapons_reach");
      check(r.u.aimed.Y > r.u.pos.Y && r.u.aimed.Z < r.u.pos.Z, "the_sky_is_fired_along_the_look");
      check(!r.Saw(CE_PLAYUNIT_FIRE_OUT_OF_RANGE), "the_sky_is_not_out_of_range"); }
    // beyond the weapon's range: the gun is turned toward the clamped point on the same line and fires there
    { Run r; r.gun.hi = 3.f; for (int i = 0; i < 40; ++i) r.Tick(true);
      check(r.u.rounds >= 1, "beyond_reach_is_fired_at_the_weapons_reach");
      check(r.u.aimCalls == 40, "beyond_reach_turns_the_gun_every_tick");
      const float dx = r.u.aimed.X - r.u.pos.X, dy = r.u.aimed.Y - r.u.pos.Y, dz = r.u.aimed.Z - r.u.pos.Z;
      check(near(std::sqrt(dx * dx + dy * dy + dz * dz), 3.f), "aim_is_clamped_to_the_weapons_range");
      check(!r.Saw(CE_PLAYUNIT_FIRE_OUT_OF_RANGE), "beyond_reach_is_not_out_of_range"); }
    // inside the weapon's minimum
    { Run r; r.gun.lo = 30.f; for (int i = 0; i < 10; ++i) r.Tick(true); check(r.u.rounds == 0 && r.u.armCalls == 0, "too_near_is_not_fired_at"); }
    // the ground rises into the line of fire
    { Run r; gTerrain = 1; r.eye = FVector(0.f, -10.f, -8.f); r.look = FVector(0.f, 0.6f, 0.8f); r.u.pos = FVector(0.f, -12.f, 0.f);
      for (int i = 0; i < 10; ++i) { r.Tick(true); }
      gTerrain = 0;
      check(r.u.rounds == 0 && r.u.armCalls == 0, "a_blocked_line_is_not_fired_through"); check(r.Saw(CE_PLAYUNIT_FIRE_BLOCKED), "a_blocked_line_is_told"); }
    // the weapon is between shots: held fire asks the native calls and the weapon decides; no round is made before it is ready
    { Run r; r.gun.ready = false; r.gun.hi = 80.f; for (int i = 0; i < 3; ++i) r.Tick(true);
      check(r.u.rounds == 0 && r.Saw(CE_PLAYUNIT_FIRE_RECOVERING), "a_weapon_between_shots_does_not_fire"); }
    // a cleared current weapon is replaced by the first ready one; with none ready or none at all there is nothing to say or do
    { Run r; CWeapon invalid; r.u.mCurrentWeapon = nullptr; r.u.mWeapons.items = {&invalid, &r.gun};
      for (int i = 0; i < 12; ++i) r.Tick(true);
      check(r.u.mCurrentWeapon == &r.gun && r.u.rounds >= 1, "a_cleared_weapon_is_replaced_by_a_usable_one"); }
    { Run r; r.u.mCurrentWeapon = nullptr; r.gun.ready = false; for (int i = 0; i < 5; ++i) r.Tick(true);
      check(r.u.rounds == 0 && r.Saw(CE_PLAYUNIT_FIRE_RECOVERING), "no_ready_weapon_is_recovering"); }
    { Run r; r.u.mCurrentWeapon = nullptr; r.u.mWeapons.items.clear(); for (int i = 0; i < 5; ++i) r.Tick(true);
      check(r.u.aimCalls == 0 && r.u.armCalls == 0 && r.u.fireCalls == 0, "no_weapon_is_safe"); }
    // a weapon with no mode is not usable
    { Run r; r.gun.mMode = nullptr; r.u.mCurrentWeapon = nullptr; for (int i = 0; i < 5; ++i) r.Tick(true); check(r.u.aimCalls == 0 && r.u.fireCalls == 0, "a_weapon_with_no_mode_is_not_used"); }
    // a number that is not one never reaches the weapon
    { Run r; r.gun.hi = NAN; for (int i = 0; i < 5; ++i) r.Tick(true); check(r.u.armCalls == 0 && r.u.fireCalls == 0 && r.u.rounds == 0, "a_range_that_is_not_a_number_is_safe"); }
    { Run r; r.look = FVector(NAN, 0.6f, 0.8f); for (int i = 0; i < 5; ++i) r.Tick(true); check(r.u.armCalls == 0 && r.u.fireCalls == 0 && r.u.aimCalls == 0, "a_view_that_is_not_a_number_is_safe"); }
    // turning the view moves the point
    { Run r; r.look = FVector(0.6f, 0.f, 0.8f); r.eye = FVector(-10.f, 0.f, -8.f); for (int i = 0; i < 6; ++i) r.Tick(true); check(near(r.u.aimed.X, -4.f) && near(r.u.aimed.Y, 0.f), "turning_the_view_moves_the_aim"); }
    std::printf("checks %d failed %d\n", checks, failed);
    return failed ? 1 : 0;
}
'''


class FreeFire(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data" / "ce-playunit-fire-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(dir=parent)
        cls.scratch = Path(cls.tmp.name)
        cls.region = production_region()
        cls.module = MODULE.read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_control(self, name, region):
        folder = self.scratch / name
        folder.mkdir()
        cpp = folder / "driver.cpp"
        cpp.write_text(DRIVER.replace("@REGION@", region))
        exe = folder / "driver"
        built = subprocess.run(["g++", "-std=c++14", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-parameter", "-I", str(PORT / "game"), str(cpp), "-o", str(exe)],
                               capture_output=True, text=True, timeout=120)
        self.assertEqual(built.returncode, 0, "the fixture did not compile:\n" + built.stdout[-1500:] + built.stderr[-1500:])
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
        return ran.returncode, ran.stdout.splitlines()

    def test_the_production_region_fires_with_no_enemy(self):
        status, lines = self.run_control("native", self.region)
        self.assertEqual([l for l in lines if l.startswith("FAIL")], [])
        self.assertEqual(status, 0)
        summary = [l for l in lines if l.startswith("checks ")]
        self.assertEqual(len(summary), 1)
        self.assertGreaterEqual(int(summary[0].split()[1]), 25, "too few checks ran")

    def test_causal_controls(self):
        aim_call = "u->WeaponUnknown509140(0, (SINT)&gAimPoint);"
        changes = {
            "keep_an_ai_target": (aim_call, aim_call.replace("(0,", "(1,"), "aim_has_no_target"),
            "omit_arm": ("else u->Unknown86();", "else {}", "uses_the_native_arm_and_the_native_discharge"),
            "omit_discharge": ("if (u->Unknown84()) u->Unknown74();", "if (u->Unknown84()) {}", "no_enemy_still_fires"),
            "arm_and_discharge_together": ("if (u->Unknown84()) u->Unknown74();\n	else u->Unknown86();", "if (u->Unknown84()) u->Unknown74();\n	u->Unknown86();", "aim_comes_before_arm_or_discharge"),
            "discharge_before_aim": (aim_call + "\n	if (!CePlayUnitFireActs", "if (u->Unknown84()) u->Unknown74();\n	" + aim_call + "\n	if (!CePlayUnitFireActs", "aim_comes_before_arm_or_discharge"),
            "held_gate_on_aim": ("if (!haveAim || !CePlayUnitFireAims(held, state)) return;", "if (!haveAim || !CePlayUnitFireAims(true, state)) return;", "no_hold_no_native_calls"),
            "calls_ignore_the_state": ("if (!CePlayUnitFireActs(held, state)) return;", "if (false) return;", "too_near_is_not_fired_at"),
            "calls_without_a_point": ("if (!haveAim || !CePlayUnitFireAims(held, state)) return;", "if (!CePlayUnitFireAims(held, state)) return;", "a_view_that_is_not_a_number_is_safe"),
            "the_sky_has_no_point": ("	if (!ground)\n		for (int i = 0; i < 3; ++i) hit[i] = e[i] + l[i] * law.viewReach;\n", "	(void)ground;\n", "the_sky_is_fired_at_the_weapons_reach"),
            "the_sky_is_behind": ("hit[i] = e[i] + l[i] * law.viewReach;", "hit[i] = e[i] - l[i] * law.viewReach;", "the_sky_is_fired_along_the_look"),
            "range_not_asked": ("range = CePlayUnitRange(at, hit, w->GetMinRange(target), w->GetMaxRange(target), shot);",
                                "range = CE_PLAYUNIT_RANGE_OK; shot[0] = hit[0]; shot[1] = hit[1]; shot[2] = hit[2]; (void)at; (void)target;", "aim_is_clamped_to_the_weapons_range"),
            "range_from_the_wrong_end": ("range = CePlayUnitRange(at, hit, w->GetMinRange(target), w->GetMaxRange(target), shot);",
                                         "range = CePlayUnitRange(at, hit, w->GetMinRange(target), w->GetMaxRange(target) * 1000.f, shot);", "aim_is_clamped_to_the_weapons_range"),
            "far_is_out_of_range": ("CePlayUnitFireStateOf(true, *haveAim, range,", "CePlayUnitFireStateOf(true, *haveAim && range != CE_PLAYUNIT_RANGE_TOO_FAR, range,", "beyond_reach_is_fired_at_the_weapons_reach"),
            "minimum_not_asked": ("w->GetMinRange(target), w->GetMaxRange(target), shot)", "0.f, w->GetMaxRange(target), shot)", "too_near_is_not_fired_at"),
            "line_not_asked": ("line = CePlayUnitLineClear(", "line = true || CePlayUnitLineClear(", "a_blocked_line_is_not_fired_through"),
            "aim_at_the_unit_not_the_ground": ("*aim = FVector(shot[0], shot[1], shot[2]);", "*aim = FVector(pos.X, pos.Y + 20.f, pos.Z);", "aim_is_the_ground_under_the_view"),
            "view_direction_mirrored": ("l[3] = {look.X, look.Y, look.Z}", "l[3] = {look.X, -look.Y, look.Z}", "aim_is_the_ground_under_the_view"),
            "view_origin_is_the_unit": ("e[3] = {eye.X, eye.Y, eye.Z}", "e[3] = {u->GetPos().X, u->GetPos().Y, u->GetPos().Z}", "aim_is_the_ground_under_the_view"),
            "cleared_weapon_not_replaced": ("if (assign && list[pick] != u->mCurrentWeapon) u->mCurrentWeapon = list[pick];", "", "a_cleared_weapon_is_replaced_by_a_usable_one"),
            "a_weapon_with_no_mode": ("hasMode[count] = w->mMode != NULL;\n\t\tready[count] = w->mMode && w->ReadyToFire() != FALSE;", "hasMode[count] = true;\n\t\tready[count] = w->ReadyToFire() != FALSE;", "a_cleared_weapon_is_replaced_by_a_usable_one"),
            "state_ignores_arming": ("u->Unknown84() != FALSE, u->mUnknown168 != 0)", "false, false)", "state_goes_through_ready_arming_recovering"),
            "state_ignores_recovery": ("w->ReadyToFire() != FALSE, u->Unknown84()", "true, u->Unknown84()", "state_goes_through_ready_arming_recovering"),
        }
        for name, (before, after, expected) in changes.items():
            with self.subTest(control=name):
                self.assertEqual(self.region.count(before), 1, f"the text a control changes is not unique: {before[:60]!r}")
                status, lines = self.run_control("control-" + name, self.region.replace(before, after))
                self.assertNotEqual(status, 0, f"control escaped: {name}")
                self.assertIn("FAIL " + expected, lines, f"the fixture did not say {expected!r}: {[l for l in lines if l.startswith('FAIL')][:4]}")

    def test_the_region_is_the_only_owner_of_the_native_calls(self):
        code = "\n".join(l for l in self.module.split("\n") if not l.strip().startswith("//"))
        region = "\n".join(l for l in self.region.split("\n") if not l.strip().startswith("//"))
        for call in ("WeaponUnknown509140(", "Unknown74()", "Unknown86()"):
            self.assertEqual(region.count(call), 1, f"{call} is made once in the region")
        self.assertEqual(code.count("WeaponUnknown509140("), 1, "the module calls the weapon's aim in one place")
        self.assertEqual(code.count("->Unknown86()"), 1)
        for banned in ("mAI", "mTarget", "FloatRandom", "rand(", "GetTargetablePos", "Unknown3()", "Unknown4fb840"):
            self.assertNotIn(banned, region, f"free fire touches {banned}")
        # the held input and the gate stand in the tick that calls the region
        tick = code[code.index("void FireTick("):code.index("void Drive(")]
        self.assertIn("CePlayUnitFreeFireAllowed(FillGate(true), gLaw->freeFire, PortCeCmdMapOpen())", tick)
        self.assertLess(tick.index("CePlayUnitFreeFireAllowed("), tick.index("DriveFire(u, aim"), "the gate is asked before any native call")
        self.assertIn("DriveFire(u, aim, haveAim, gFireState, held);", tick)
        drive = code[code.index("void Drive("):code.index("void CePlayUnitRenderState()") if "void CePlayUnitRenderState()" in code else None]
        self.assertEqual(drive.count("FireTick(u, in.fire);"), 1, "the held input of this tick is what the fire tick is given")
        self.assertEqual(code.count("FireTick("), 2, "one definition, one call")
        # the stock handler stays as the fallback, only for a held fire the free path did not take
        self.assertIn("u->mAI->CUnitAI::Unknown3();", tick)
        self.assertRegex(tick, r"return;\n\t}\n\tif \(held && u->mAI\)")

    def test_the_test_only_control_is_compiled_out_of_shipped_builds(self):
        text = self.module
        self.assertIn("BEA_PORT_PLAYUNIT_STOCKFIRE", text)
        shipped = re.sub(r"#ifdef CE_PLAYUNIT_TEST.*?(?:#else.*?)?#endif", "", text, flags=re.S)
        self.assertNotIn("STOCKFIRE", shipped)
        self.assertNotIn("gStockFire", shipped)


if __name__ == "__main__":
    unittest.main(verbosity=2)
