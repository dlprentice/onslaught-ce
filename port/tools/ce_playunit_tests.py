#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Play as a unit (port/game/ce_playunit.cpp, ce_playunit_rules.h, ce_playunit.h; build.py --play-unit-test): the rules as compiled code, and the static fences.

What these tests hold, without a game:
- The rules (ce_playunit_rules.h, which needs no game object) are compiled and run: the gate that says where the option may act (every fence refused, the proof
  switch lifting only the replay and recording fences), which unit counts as a tank, the steering and the heading it asks for, the point a unit is sent to, and when a
  go-to-point is given again, and whether hand-back may rejoin the old squad or must preserve a new owner. Causal mutants of each rule (a fence dropped, a sign turned, a clamp removed) must be caught.
- The module is part of every CE build and off by default: it is wrapped in CE_VERSION, reads ce.ini only where no fenced variable is set, names the same eight fenced
  variables the other CE modes name, fills every clause of the gate, and its test machinery (the proof switch, the probe, god mode, the controls) sits under
  CE_PLAYUNIT_TEST, which only build.py's --play-unit-test defines; a build without it compiles no environment variable of the module's.
- The module makes no direct RNG call; the opted-in native firing handler can use the game's RNG. The game owns its one active tank camera, and the driver stays in static storage that is never destroyed; it changes no file of `src/`
  and adds no patch.
- The hooks it needs are where they belong: one call in the per-update hook (updatedump.cpp), and the three calls in the commander's hook functions
  (ce_cmdmap.cpp), each inside CE_VERSION.
What the tank does when driven was measured by runs of a test build on the game's own level 201 (the probe's lines, tick by tick, and the frames); the runs' own
fences are black-box runs of the built game (port/PARITY.md, "Play as a unit").
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / "port"
MODULE = PORT / "game/ce_playunit.cpp"
HEADER = PORT / "game/ce_playunit.h"
RULES = PORT / "game/ce_playunit_rules.h"
DRIVER = PORT / "tools/ce_playunit_driver.cpp"
VIEW = PORT / "game/ce_playunit_view.h"
LOOK = PORT / "game/ce_playunit_look.h"
UPDATE = PORT / "game/updatedump.cpp"
MAP = PORT / "game/ce_cmdmap.cpp"
BUILD = PORT / "build.py"
FENCED_ENV = ("BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN")
# What the module's gate (Allowed in ce_playunit.cpp) must fill: every field of CePlayUnitGate.
GATE_FIELDS = ("optionOn", "ceLayer", "recording", "fencedEnv", "networkBoot", "demo", "multiplayer", "players", "proof")
GATE_CLAUSES = ("PortCeLayer()", "PortRecording()", "FencedEnv()", "CE_BootRequested", "PLAYABLE_DEMO", "CLIPARAMS.mAutoConfigTest", "CLIPARAMS.mRecordDemo",
                "CLIPARAMS.mPlaybackDemo", "CLIPARAMS.mStressTest", "CLIPARAMS.mAttractMode", "GAME.IsMultiplayer()", "GAME.GetNPlayers()")


def comments_off(text):
    return "\n".join(l for l in text.split("\n") if not l.strip().startswith("//"))


def without_test(text):
    """What a build without CE_PLAYUNIT_TEST compiles: the `#ifdef CE_PLAYUNIT_TEST` branches dropped and their `#else` branches kept; other conditionals stay."""
    out, stack = [], []				# each entry: None (not ours) or [active]
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
    assert not stack, "an unterminated conditional"
    return "\n".join(out)


def swap(old, new):
    """A mutation of one text: old, which must be there once, becomes new."""
    def mutate(text):
        assert text.count(old) == 1, f"the text a mutant changes is not unique: {old[:60]!r}"
        return text.replace(old, new)
    return mutate


def run_driver(rules_text, scratch, name, look_text=None):
    """The driver compiled against rules_text and run; (exit status, its lines)."""
    folder = Path(scratch) / name
    folder.mkdir()
    (folder / "ce_playunit_rules.h").write_text(rules_text)
    (folder / "ce_playunit_look.h").write_text(look_text if look_text is not None else LOOK.read_text())
    binary = folder / "driver"
    done = subprocess.run(["g++", "-std=c++14", "-O0", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Werror", "-I", str(folder), str(DRIVER), "-o", str(binary)],
                          capture_output=True, text=True, timeout=120)
    assert done.returncode == 0, "the driver did not compile:\n" + done.stdout + done.stderr
    ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    return ran.returncode, ran.stdout.splitlines()


# (name, the change, what the driver must say failed)
MUTANTS = (
    ("a different level keeps the lease", swap("!f.sameLevel || !f.unitLive", "!f.unitLive"), "return: another level cannot inherit a stale lease"),
    ("a dead unit is handed back", swap("!f.sameLevel || !f.unitLive", "!f.sameLevel"), "return: dead or removed unit is not written"),
    ("a changed side is overwritten", swap("!f.sameTeam || f.currentSquad", "f.currentSquad"), "return: a changed side is preserved"),
    ("a new squad is overwritten", swap("f.currentSquad || !f.aiStillOwned", "!f.aiStillOwned"), "return: a newer squad assignment wins"),
    ("resumed AI is overwritten", swap("f.currentSquad || !f.aiStillOwned", "f.currentSquad"), "return: resumed AI orders win"),
    ("a missing squad is rejoined", swap("f.originalSquadLive && f.originalSquadCompatible", "f.originalSquadCompatible"), "return: missing original squad uses independent fallback"),
    ("an enemy squad is rejoined", swap("f.originalSquadLive && f.originalSquadCompatible", "f.originalSquadLive"), "return: original squad on another side is not rejoined"),

    ("the network fence is dropped", swap("g.networkBoot || g.demo", "g.demo"), "gate: a network session is refused"),
    ("the demo fence is dropped", swap("g.demo || g.multiplayer", "g.multiplayer"), "gate: a demo or test mode is refused"),
    ("the multiplayer fence is dropped", swap("g.multiplayer || g.players != 1", "g.players != 1"), "gate: a multiplayer level is refused"),
    ("any player count of one or more passes", swap("g.players != 1", "g.players < 1"), "gate: two players are refused"),
    ("the option is not needed", swap("	if (!g.optionOn) return false;\n", ""), "gate: the option off is refused"),
    ("the CE layer is not needed", swap("return g.ceLayer && !g.recording && !g.fencedEnv;", "return !g.recording && !g.fencedEnv;"), "gate: no CE layer is refused"),
    ("a recording is allowed", swap("return g.ceLayer && !g.recording && !g.fencedEnv;", "return g.ceLayer && !g.fencedEnv;"), "gate: a recording is refused"),
    ("a fenced variable is allowed", swap("return g.ceLayer && !g.recording && !g.fencedEnv;", "return g.ceLayer && !g.recording;"), "gate: a fenced variable is refused"),
    ("the proof switch lifts the network fence", swap("	if (g.networkBoot || g.demo || g.multiplayer || g.players != 1) return false;\n	if (g.proof) return true;\n",
                                                      "	if (g.proof) return true;\n	if (g.networkBoot || g.demo || g.multiplayer || g.players != 1) return false;\n"),
     "gate: the proof switch does not lift the network fence"),
    ("the proof switch lifts nothing", swap("	if (g.proof) return true;\n", ""), "gate: the proof switch lifts the replay fences"),
    ("an enemy is eligible", swap("f.friendly && f.alive", "f.alive"), "eligible: an enemy is refused"),
    ("a dying unit is eligible", swap("f.alive && !f.scripted", "!f.scripted"), "eligible: a dying unit is refused"),
    ("a scripted unit is eligible", swap("!f.scripted && ", ""), "eligible: a unit a mission script runs is refused"),
    ("an objective is eligible", swap("!f.objective && ", ""), "eligible: an objective is refused"),
    ("artillery is eligible as a tank", swap("f.vehicle && f.groundUnit && !f.deploys", "f.vehicle && f.groundUnit"), "eligible: artillery is refused"),
    ("a unit outside a normal squad is eligible", swap("(!CePlayUnitNeedsNormalSquad(f) || f.normalSquad) && ", ""), "eligible: a unit outside a normal squad is refused"),
    ("a spawned squad's unit is eligible", swap("!f.spawned && ", ""), "eligible: a squad that follows its lead unit is refused"),
    ("an unarmed unit is eligible", swap(" && (f.armed || f.transport);", ";"), "eligible: an unarmed unit is refused"),
    ("a jeep is eligible", swap("f.vehicle && f.groundUnit && !f.deploys", "f.vehicle && !f.deploys"), "eligible: a jeep, mech or aircraft is refused"),
    ("steering right raises the heading", swap("desired - CePlayUnitClamp(turn, -1.f, 1.f) * rate * dt - actual", "desired + CePlayUnitClamp(turn, -1.f, 1.f) * rate * dt - actual"),
     "steer: right lowers the heading"),
    ("the request may lead without limit", swap("	if (lead > maxLead) lead = maxLead;\n", ""), "steer: the request leads by at most maxLead"),
    ("the request may trail without limit", swap("	if (lead < -maxLead) lead = -maxLead;\n", ""), "steer: the request leads by at most maxLead"),
    ("the deflection is not limited", swap("CePlayUnitClamp(turn, -1.f, 1.f) * rate", "turn * rate"), "steer: deflection is limited to full"),
    ("a negative time step turns the other way", swap("	if (!isfinite(dt) || dt < 0.f) dt = 0.f;\n", ""), "steer: a negative or unknown time step turns nothing"),
    ("the heading is not wrapped", swap("	return CePlayUnitWrap(actual + lead);", "	return actual + lead;"), "steer: wraps past a half turn"),
    ("an angle is not wrapped", swap("	float r = remainderf(a, 2.f * pi);\n", "	return a; float r = 0.f;\n"), "wrap: into one turn"),
    ("huge angles are not reduced by remainderf", swap("	float r = remainderf(a, 2.f * pi);\n", "	float r = a;\n"), "wrap: huge angles finish in O(1) inside (-pi, pi]"),
    ("a clamp lets a number that is not one through", swap("	if (!(v == v)) return 0.f;		// not a number: no input\n", ""), "clamp: a number that is not one is no input"),
    ("the point uses the sine for x", swap("*tx = x + cosf(angle) * reach;", "*tx = x + sinf(angle) * reach;"), "point: along the x axis"),
    ("the point is the unit's own place", swap("*ty = y + sinf(angle) * reach;", "*ty = y;"), "point: along the y axis"),
    ("a point with no reach is given", swap("|| !(reach > 0.f)) return false;", ") return false;"), "point: no reach, no point (the unit's own place)"),
    ("the interval is one too long", swap("tick - lastTick >= interval", "tick - lastTick > interval"), "reissue: the interval is over"),
    ("a changed wish is not enough", swap("return changed || ", "return "), "reissue: the wishes changed"),
    ("nearness is not asked", swap(" || distanceToPoint < nearRadius", ""), "reissue: the unit is near the last point"),

    # the class table
    ("a later row skips what every class refuses", swap("return CePlayUnitCommonEligible(f) && rows[row].shape(f);", "return rows[row].shape(f) && (row == 0 ? CePlayUnitCommonEligible(f) : true);"),
     "table: a scripted unit is refused by every row"),
    ("the common guard is gone", swap("return CePlayUnitCommonEligible(f) && rows[row].shape(f);", "return rows[row].shape(f);"), "table: an enemy is refused by every row"),
    ("a row that is off takes units", swap("|| !rows[row].enabled", ""), "table: a row that is off takes nothing"),
    ("a row with no shape takes units", swap("|| !rows[row].shape) return false;\n	return CePlayUnitCommonEligible(f) && rows[row].shape(f);", ") return false;\n	return CePlayUnitCommonEligible(f) && (!rows[row].shape || rows[row].shape(f));"), "table: a row with no shape takes nothing"),
    ("a row beyond the table is read", swap("row < 0 || row >= n || ", ""), "table: a row that is not there takes nothing"),
    ("the jeep row is on", swap('{"jeep", false,', '{"jeep", true,'), "table: the jeep row is off"),
    ("the tank row is off", swap('{"tank", true,', '{"tank", false,'), "table: the tank row is on"),
    ("the tank's law moves", swap("{40.f, 6.f, 1.4f, 1.2f, 8.f, 2.f, 6u, 2u, true,", "{41.f, 6.f, 1.4f, 1.2f, 8.f, 2.f, 6u, 2u, true,"), "table: the tank's control law is the one measured"),
    ("the tank has no free fire", swap("{40.f, 6.f, 1.4f, 1.2f, 8.f, 2.f, 6u, 2u, true,", "{40.f, 6.f, 1.4f, 1.2f, 8.f, 2.f, 6u, 2u, false,"), "table: the tank has free fire"),
    # free fire: the gate
    ("free fire runs for a class without it", swap("if (!classFreeFire || commanderOpen || g.proof) return false;", "if (commanderOpen || g.proof) return false;"), "free fire: a class without it is refused"),
    ("free fire runs while commanding", swap("if (!classFreeFire || commanderOpen || g.proof) return false;", "if (!classFreeFire || g.proof) return false;"), "free fire: open Commander view is refused"),
    ("free fire runs under the proof switch", swap("if (!classFreeFire || commanderOpen || g.proof) return false;", "if (!classFreeFire || commanderOpen) return false;"), "free fire: the proof switch does not lift the replay fences for it"),
    ("free fire skips the option's gate", swap("	return CePlayUnitAllowed(g);\n}\n\n// The ground's height", "	return true;\n}\n\n// The ground's height"), "free fire: a second player is refused"),
    # free fire: the aim point
    ("the ray's hit is not narrowed", swap("for (int i = 0; i < 12; ++i)", "for (int i = 0; i < 0; ++i)"), "view: a coarse search is narrowed to the same point"),
    ("the ray starts under the ground", swap("if (!isfinite(start) || start <= 0.f) return false;", "if (!isfinite(start)) return false;"), "view: an eye under the ground gives no point"),
    ("the hit's height is the eye's", swap("hit[2] = CePlayUnitSurfaceAt(height, context, hit[0], hit[1], waterZ);", "hit[2] = eye[2];"), "view: the ray reaches flat ground where the geometry says"),
    ("the ground is judged from below", swap("const float above = CePlayUnitSurfaceAt(height, context, eye[0] + look[0] * t, eye[1] + look[1] * t, waterZ) - (eye[2] + look[2] * t);", "const float above = (eye[2] + look[2] * t) - CePlayUnitSurfaceAt(height, context, eye[0] + look[0] * t, eye[1] + look[1] * t, waterZ);"),
     "view: the ray reaches flat ground where the geometry says"),
    ("water surface is ignored in aiming", swap("return (isfinite(waterZ) && waterZ < ground) ? waterZ : ground;", "return ground;"),
     "view: over-water ray hits water surface rather than seabed"),
    ("the reach is not asked", swap("		if (t >= maxDist) return false;\n", "		if (t >= maxDist) { hit[0] = eye[0]; hit[1] = eye[1]; hit[2] = eye[2]; return true; }\n"), "view: ground beyond the reach gives no point"),
    ("the sky has a point", swap("		if (t >= maxDist) return false;\n", "		if (t >= maxDist) { hit[0] = eye[0] + look[0] * t; hit[1] = eye[1] + look[1] * t; hit[2] = eye[2] + look[2] * t; return true; }\n"), "view: looking at the sky gives no point"),
    # free fire: the range
    ("the range is not clamped", swap("		const float k = maxRange / d;", "		const float k = 1.f;"), "range: a far point is clamped to the weapon's range on the same line"),
    ("a far point is in reach", swap("	if (d > maxRange)\n	{", "	if (d > maxRange * 2.f)\n	{"), "range: a far point is clamped to the weapon's range on the same line"),
    ("a near point is in reach", swap("	if (d < minRange)\n	{", "	if (d < 0.f)\n	{"), "range: a near point is clamped to the weapon's minimum on the same line"),
    ("the minimum is clamped to the maximum's side", swap("		const float k = minRange / d;", "		const float k = maxRange / d;"), "range: a near point is clamped to the weapon's minimum on the same line"),
    ("a range of nothing is a range", swap("!(maxRange > 0.f) || minRange > maxRange)", "minRange > maxRange)"), "range: no positive range is none"),
    ("a minimum above the maximum is a range", swap("|| minRange > maxRange)", ")"), "range: a minimum above the maximum is none"),
    ("the unit's own place has a direction", swap("	if (!(d > 0.f)) return CE_PLAYUNIT_RANGE_NONE;\n", ""), "range: the unit's own place has no direction"),
    ("the limit is out of reach", swap("	if (d > maxRange)\n	{", "	if (d >= maxRange)\n	{"), "range: the limits are in reach"),
    # free fire: the line of fire
    ("the ground never blocks", swap("ground + tolerance < z", "ground + tolerance < z - 100.f"), "line: a ridge across the line blocks it"),
    ("the tolerance is not used", swap("ground + tolerance < z", "ground < z"), "line: a rise within the tolerance does not"),
    ("only the first sample is looked at", swap("for (int i = 1; i <= samples; ++i)", "for (int i = 1; i <= 1; ++i)"), "line: a ridge across the line blocks it"),
    # free fire: the state, the held-input gate, and the case of no target
    ("no ground still has a state", swap("if (!haveGround || range == CE_PLAYUNIT_RANGE_NONE", "if (range == CE_PLAYUNIT_RANGE_NONE"), "state: no ground under the view is out of range"),
    ("range is not asked", swap("if (!haveGround || range == CE_PLAYUNIT_RANGE_NONE || range == CE_PLAYUNIT_RANGE_TOO_NEAR)", "if (!haveGround)"), "state: a near point is out of range"),
    ("a far point is out of range", swap("range == CE_PLAYUNIT_RANGE_NONE || range == CE_PLAYUNIT_RANGE_TOO_NEAR)", "range != CE_PLAYUNIT_RANGE_OK)"), "state: a far point is fired at the weapon's reach"),
    ("too near is in range", swap(" || range == CE_PLAYUNIT_RANGE_TOO_NEAR) return CE_PLAYUNIT_FIRE_OUT_OF_RANGE;", ") return CE_PLAYUNIT_FIRE_OUT_OF_RANGE;"), "state: a near point is out of range"),
    ("the line is not asked", swap("	if (!lineClear) return CE_PLAYUNIT_FIRE_BLOCKED;\n", ""), "state: a blocked line says so"),
    ("recovering is ready", swap("	if (!weaponReady) return CE_PLAYUNIT_FIRE_RECOVERING;\n", ""), "state: between shots is recovering"),
    ("arming is not told", swap("	if (arming && !armed) return CE_PLAYUNIT_FIRE_ARMING;\n", ""), "state: arming"),
    ("an armed unit is still arming", swap("if (arming && !armed)", "if (arming)"), "state: armed and ready is ready"),
    ("fire is given without a hold", swap("	return held && (state == CE_PLAYUNIT_FIRE_READY", "	return (state == CE_PLAYUNIT_FIRE_READY"), "fire: no held input, no native calls"),
    ("fire is given out of range", swap("|| state == CE_PLAYUNIT_FIRE_RECOVERING);", "|| state == CE_PLAYUNIT_FIRE_RECOVERING || state == CE_PLAYUNIT_FIRE_OUT_OF_RANGE);"), "fire: held fire gives the native calls only where there is something to ask"),
    ("fire is given with no weapon", swap("|| state == CE_PLAYUNIT_FIRE_RECOVERING);", "|| state == CE_PLAYUNIT_FIRE_RECOVERING || state == CE_PLAYUNIT_FIRE_NONE);"), "fire: held fire gives the native calls only where there is something to ask"),
    ("the gun turns without a hold", swap("	return held && state != CE_PLAYUNIT_FIRE_NONE;", "	return state != CE_PLAYUNIT_FIRE_NONE;"), "fire: no held input, the gun is not turned"),
    ("the gun does not turn out of range", swap("	return held && state != CE_PLAYUNIT_FIRE_NONE;", "	return held && state != CE_PLAYUNIT_FIRE_NONE && state != CE_PLAYUNIT_FIRE_OUT_OF_RANGE;"), "fire: held fire turns the gun wherever there is a weapon"),
    # taking a unit by key (ce_playunit_rules.h: CePlayUnitRefusalIn, CePlayUnitAtScreen, CePlayUnitChoose, CePlayUnitSwitchStep, CePlayUnitPressed)
    ("a takeable unit is refused", swap("	if (CePlayUnitClassIn(rows, n, f) >= 0) return CE_PLAYUNIT_TAKE_OK;\n", ""), "refusal: refused exactly when no class takes the unit"),
    ("an unsupported vehicle is refused for its squad", swap("\tif (!shaped) return f.deploys ? CE_PLAYUNIT_REFUSE_DEPLOYS : CE_PLAYUNIT_REFUSE_KIND;\n\tif ((CePlayUnitNeedsNormalSquad(f) && !f.normalSquad) || f.spawned) return CE_PLAYUNIT_REFUSE_SQUAD;\n",
                                                 "\tif ((CePlayUnitNeedsNormalSquad(f) && !f.normalSquad) || f.spawned) return CE_PLAYUNIT_REFUSE_SQUAD;\n\tif (!shaped) return f.deploys ? CE_PLAYUNIT_REFUSE_DEPLOYS : CE_PLAYUNIT_REFUSE_KIND;\n"),
     "refusal: the first question that applies is the one asked"),
    ("an objective is not the mission's", swap("if (f.scripted || f.objective) return CE_PLAYUNIT_REFUSE_MISSION;", "if (f.scripted) return CE_PLAYUNIT_REFUSE_MISSION;"), "refusal: an objective belongs to the mission"),
    ("a following squad is not asked", swap("if ((CePlayUnitNeedsNormalSquad(f) && !f.normalSquad) || f.spawned) return CE_PLAYUNIT_REFUSE_SQUAD;", "if (CePlayUnitNeedsNormalSquad(f) && !f.normalSquad) return CE_PLAYUNIT_REFUSE_SQUAD;"), "refusal: a squad that follows its lead keeps its unit"),
    ("the frame's side is not asked", swap("if (p.x < 0.f || p.x > w || p.y < 0.f", "if (p.y < 0.f"), "sight: the frame's side edges"),
    ("the frame's bottom is not asked", swap("|| p.y < 0.f || p.y > h) return false;", "|| p.y < 0.f) return false;"), "sight: the frame's top and bottom edges"),
    ("a unit behind the eye is seen", swap("if (!(p.depth > kCePlayUnitSightNear) || p.distance > reach)", "if (!(fabsf(p.depth) > kCePlayUnitSightNear) || p.distance > reach)"), "sight: behind the eye is out of sight"),
    ("the reach is ignored", swap("|| p.distance > reach) return false;", ") return false;"), "sight: beyond reach is out of sight"),
    ("the near plane is ignored", swap("if (!(p.depth > kCePlayUnitSightNear) ||", "if (!(p.depth > 0.f) ||"), "sight: nearer than the near plane is out of sight"),
    ("the wide screen stretches the scale", swap("const float half = (w < h * 4.f / 3.f ? w : h * 4.f / 3.f) * 0.5f;", "const float half = w * 0.5f;"), "sight: a wide screen is measured in the 4:3 frame's half-widths"),
    ("the tall screen is measured by its height", swap("const float half = (w < h * 4.f / 3.f ? w : h * 4.f / 3.f) * 0.5f;", "const float half = h * 4.f / 3.f * 0.5f;"), "sight: a tall screen is measured across its width"),
    ("the size is ignored", swap("- (p.radius > 0.f ? p.radius : 0.f)) / half;", ") / half;"), "sight: the unit's size brings its edge nearer the middle"),
    ("the height is ignored", swap("const float r = (sqrtf(dx * dx + dy * dy)", "const float r = (sqrtf(dx * dx + 0.f * dy)"), "sight: a tall screen is measured across its width"),
    ("the reticle admits half the screen", swap("const float kCePlayUnitSightReticle = 0.12f;", "const float kCePlayUnitSightReticle = 0.5f;"), "sight: a near unit beside the line is not at the reticle"),
    ("a switch never gives up", swap("|| !*wait) { *wait = 0; return CE_PLAYUNIT_SWITCH_GIVE_UP; }", ") { *wait = 0; return CE_PLAYUNIT_SWITCH_GIVE_UP; }"), "switch: a unit never ready gives up when the wait is over"),
    ("a switch takes an untakeable unit", swap("if (found && ready && takeable) {", "if (found && ready) {"), "switch: a unit no longer takeable gives up at once"),
    ("a switch waits for a unit that is gone", swap("if (!found || (ready && !takeable)", "if ((ready && !takeable)"), "switch: a unit that is gone gives up at once"),
    ("the nearest unit is taken even when refused", swap("if (s[i].refusal == CE_PLAYUNIT_TAKE_OK && (best < 0", "if ((best < 0"), "choose: the takeable unit nearest the middle, over a nearer one that is refused"),
    ("the reason is the first unit's", swap("? s[nearest].refusal", "? s[0].refusal"), "choose: with none takeable, the reason of the unit nearest the middle"),
    ("a held key takes again", swap("const bool edge = down && !k->wasDown;", "const bool edge = down;"), "key: one take for each press, none while it is held"),
    ("reverse drives forward", swap("float share = f - b * CePlayUnitClamp(reverseShare, 0.f, 1.f);", "float share = f + b * CePlayUnitClamp(reverseShare, 0.f, 1.f);"), "drive: S backs up, slower than forward"),
    ("W and S together drive", swap("	if (f > 0.f && b > 0.f) share = 0.f;\n", ""), "drive: W and S together hold still"),
    ("steering stops the tank", swap("share *= 1.f - CePlayUnitClamp(turnSlow, 0.f, 1.f) * fabsf(s);", "share *= fabsf(s) > 0.05f ? 0.f : 1.f;"), "drive: W and D steer right while moving, a little slower"),
    ("steering is mirrored", swap("- s * CePlayUnitClamp(steerLead, 0.f, 1.5f));", "+ s * CePlayUnitClamp(steerLead, 0.f, 1.5f));"), "drive: W and D steer right while moving, a little slower"),
    ("the heading is not wrapped", swap("m.yaw = CePlayUnitWrap((isfinite(yaw) ? yaw : 0.f) - s", "m.yaw = ((isfinite(yaw) ? yaw : 0.f) - s"), "drive: the heading wraps past a half turn"),
    ("the guard ignores what is ahead", swap("	if (share > 0.f && !aheadOpen) return 0.f;\n", ""), "guard: forward into a closed cell stops"),
    ("the guard looks ahead in reverse", swap("	if (share < 0.f && !behindOpen) return 0.f;\n", "	if (share < 0.f && !aheadOpen) return 0.f;\n"), "guard: reverse into a closed cell stops"),
    ("a replaced notice restarts", swap("	if (age >= shown) return 0;\n", "	if (age < shown + 1) return 0;\n"), "notice: a key pressed again and again keeps the capsule open"),
    ("a closing notice jumps open", swap("const unsigned long long phase = age < left ? age : left;", "const unsigned long long phase = age < left ? age : age;"), "notice: one closing opens again from the width it had"),
    ("a press in a menu is taken later", swap("	k->wasDown = down;\n	return edge && mayCount;", "	if (mayCount) k->wasDown = down;\n	return edge && mayCount;"),
     "key: a press made while it may not count is not taken later"),
)


# the look camera's mutants (ce_playunit_look.h): (name, the change, what the driver must say failed)
LOOK_MUTANTS = (
    ("the mouse turns the look left", swap("*yaw = CePlayUnitLookWrap(*yaw - dx * law.perCount);", "*yaw = CePlayUnitLookWrap(*yaw + dx * law.perCount);"), "look: the mouse to the right turns the look right (toward +x from +y)"),
    ("the mouse tilts the look up", swap("float p = *pitch + dy * law.perCount;", "float p = *pitch - dy * law.perCount;"), "look: the mouse moved down tilts the look down"),
    ("the tilt down has no limit", swap("	if (p > law.pitchDown) p = law.pitchDown;\n", ""), "look: the tilt down stops at its limit"),
    ("the tilt up has no limit", swap("	if (p < law.pitchUp) p = law.pitchUp;\n", ""), "look: the tilt up stops at its limit"),
    ("the yaw is not wrapped", swap("*yaw = CePlayUnitLookWrap(*yaw - dx * law.perCount);", "*yaw = *yaw - dx * law.perCount;"), "look: the yaw stays within a turn"),
    ("a pitch looks up", swap("forward[2] = sp;", "forward[2] = -sp;"), "look: a positive pitch looks down (the world's z is down)"),
    ("the eye is in front", swap("eye[1] = place[1] - f[1] * law.distance;", "eye[1] = place[1] + f[1] * law.distance;"), "look: the eye is behind the pivot by the distance, the pivot above the place by the lift"),
    ("the pivot is the place", swap("eye[2] = place[2] - law.lift - f[2] * law.distance;", "eye[2] = place[2] - f[2] * law.distance;"), "look: the eye is behind the pivot by the distance, the pivot above the place by the lift"),
    ("the right axis tilts", swap("right[0] = cy; right[1] = sy; right[2] = 0.f;", "right[0] = cy * cp; right[1] = sy * cp; right[2] = sp;"), "look: the axes are of unit length, square to each other, and the right axis stays level"),
    ("the yaw of a heading is mirrored", swap("return atan2f(-hx, hy);", "return atan2f(hx, hy);"), "look: a take looks along the unit's heading"),
    ("the lowest look sits on the unit", swap("{10.f, 3.5f, -0.35f, 0.3f, 0.0025f, 1.5f}", "{10.f, 2.5f, -0.35f, 0.6f, 0.0025f, 1.5f}"), "look: at the lowest look the reticle is beyond the driven unit"),
    ("the stick turns the look left", swap("CePlayUnitLookTurn(yaw, pitch, sx * counts, sy * counts, law);", "CePlayUnitLookTurn(yaw, pitch, -sx * counts, sy * counts, law);"), "look: a stick held right for a second turns the look right at the stick's rate"),
    ("the stick cannot tilt", swap("CePlayUnitLookTurn(yaw, pitch, sx * counts, sy * counts, law);", "CePlayUnitLookTurn(yaw, pitch, sx * counts, 0.f, law);"), "look: a stick pushed down tilts the look down"),
    ("the stick ignores no time", swap("|| !(seconds > 0.f) ", ""), "look: a stick that is not a number, or no time, turns nothing"),
    ("a movement that is not a number turns", swap("	if (!isfinite(dx) || !isfinite(dy)) return;\n", ""), "look: a movement that is not a number turns nothing"),
)


class Rules(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data/port/ce-playunit-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        cls.scratch = Path(cls.tmp.name)
        cls.rules = RULES.read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_rules_hold_as_compiled_code(self):
        status, lines = run_driver(self.rules, self.scratch, "as-is")
        failed = [l for l in lines if l.startswith("FAIL")]
        self.assertEqual(failed, [])
        self.assertEqual(status, 0)
        summary = [l for l in lines if l.startswith("checks ")]
        self.assertEqual(len(summary), 1)
        self.assertGreaterEqual(int(summary[0].split()[1]), 50, "too few checks ran")

    def test_each_look_mutant_is_caught(self):
        look = LOOK.read_text()
        for number, (name, mutate, said) in enumerate(LOOK_MUTANTS):
            with self.subTest(mutant=name):
                status, lines = run_driver(self.rules, self.scratch, f"look-mutant-{number}", mutate(look))
                self.assertNotEqual(status, 0, "the driver passed the mutant")
                self.assertTrue(any(l.startswith("FAIL " + said) for l in lines), f"the driver did not say {said!r}: {[l for l in lines if l.startswith('FAIL')][:3]}")

    def test_each_mutant_is_caught(self):
        for number, (name, mutate, said) in enumerate(MUTANTS):
            with self.subTest(mutant=name):
                status, lines = run_driver(mutate(self.rules), self.scratch, f"mutant-{number}")
                self.assertNotEqual(status, 0, "the driver passed the mutant")
                self.assertTrue(any(l.startswith("FAIL " + said) for l in lines), f"the driver did not say {said!r}: {[l for l in lines if l.startswith('FAIL')][:3]}")


class GuideCommands(unittest.TestCase):
    """Compile the actual command-issuing region; detect stale issued headings."""

    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data" / "ce-playunit-guide-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(dir=parent)
        cls.scratch = Path(cls.tmp.name)
        cls.module = MODULE.read_text()
        start = cls.module.index("\tconst FVector pos = u->GetPos();", cls.module.index("void Drive("))
        end = cls.module.index("#ifdef CE_PLAYUNIT_TEST\n\tif (gFireFrom", start)
        cls.region = cls.module[start:end]
        # The control law comes from the class table in the rules (the module's gLaw points at the driven class's row); the fixture drives the tank's.
        assert re.search(r"(?m)^const CePlayUnitLaw \*gLaw;", cls.module)
        for name in ("gLaw->reach", "gLaw->pivotReach", "gLaw->turnRate", "gLaw->maxLead", "gLaw->reissue", "gLaw->pivotReissue", "gLaw->nearRadius", "gLaw->pivotNear"):
            assert name in cls.region, "the drive region does not read " + name
        (cls.scratch / "ce_playunit_guide_constants.inc").write_text("static const CePlayUnitLaw *gLaw = &CePlayUnitClasses()[CE_PLAYUNIT_CLASS_TANK].law;\n")

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_control(self, region, name):
        (self.scratch / "ce_playunit_drive_region.inc").write_text(region)
        binary = self.scratch / name
        compiled = subprocess.run(
            ["g++", "-std=c++14", "-O2", "-Wall", "-Wextra", "-Werror",
             "-DCE_PLAYUNIT_GUIDE_CONTROL", "-I", str(self.scratch),
             "-I", str(PORT / "game"), str(DRIVER), "-o", str(binary)],
            text=True, capture_output=True, timeout=30, check=False)
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        result = subprocess.run([str(binary)], text=True, capture_output=True,
                                timeout=10, check=False)
        return result.returncode, [json.loads(line) for line in result.stdout.splitlines()]

    def test_small_turns_accumulate_from_the_last_issued_heading(self):
        status, rows = self.run_control(self.region, "guide-current")
        self.assertEqual(status, 0)
        self.assertEqual([r["fps"] for r in rows if "fps" in r], [30, 60, 120])
        for row in rows:
            if "fps" in row:
                self.assertEqual(row["stale_frames"], 0, row)
        self.assertTrue(all(rows[-1][key] for key in ("straight_schedule", "single_stop", "restart")))

    def test_updating_heading_without_issuing_a_command_is_detected(self):
        needle = "\t\tgLastAngle = gDesired;\n"
        self.assertEqual(self.region.count(needle), 1)
        old_bookkeeping = self.region.replace(needle, "", 1) + "\n\tgLastAngle = gDesired;\n"
        status, rows = self.run_control(old_bookkeeping, "guide-old-bookkeeping")
        self.assertEqual(status, 2)
        for row in rows:
            if "fps" in row:
                self.assertGreater(row["stale_frames"], 0, row)
        self.assertTrue(all(rows[-1][key] for key in ("straight_schedule", "single_stop", "restart")))


class CameraFrame(unittest.TestCase):
    """The actual CE camera must preserve native framing in the displayed pose."""

    @classmethod
    def setUpClass(cls):
        parent = ROOT / "local-data" / "ce-playunit-camera-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.tmp = tempfile.TemporaryDirectory(dir=parent)
        cls.scratch = Path(cls.tmp.name)
        source = MODULE.read_text()
        start = re.search(r"class CeTankCamera\s*:", source).start()
        end = source.index("\n};", start) + len("\n};")
        cls.camera = source[start:end]
        start = source.index("CCamera *PortCePlayUnitRenderCamera(")
        end = source.index("\n}", start) + len("\n}")
        cls.adapter = source[start:end]
        # the displayed camera's state and its helpers, which the adapter calls
        start = source.index("enum { kViewSmooth")
        end = source.index("void ViewReset();\n", start) + len("void ViewReset();\n")
        reset_start = source.index("CePlayUnitCameraRig gUnitViewRig")
        reset_end = source.index("bool MakeUnitView(", reset_start)
        cls.view_state = ('#include "ce_playunit_look.h"\n'
                          '#include "ce_playunit_camera.h"\n' +
                          source[reset_start:reset_end] + source[start:end])
        hide_start = source.index("bool UnitViewHidesBody()")
        hide_end = source.index("\n}", hide_start) + len("\n}")
        cls.view_state += source[hide_start:hide_end]
        start = re.search(r"unsigned long long ViewNs\(\)\s*\{", source).start()
        end = source.index("\n}\n", source.index("void SmoothView(", start)) + len("\n}\n")
        cls.view_adapter = source[start:end]
        cls.view_header = VIEW.read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_camera(self, adapter, name, view_adapter=None, view_header=None):
        (self.scratch / "ce_playunit_camera_class.inc").write_text(self.camera)
        (self.scratch / "ce_playunit_view_state.inc").write_text(self.view_state)
        (self.scratch / "ce_playunit_view_adapter.inc").write_text(view_adapter or self.view_adapter)
        (self.scratch / "ce_playunit_render_adapter.inc").write_text(adapter)
        (self.scratch / "ce_playunit_view.h").write_text(view_header or self.view_header)
        for name in ("ce_playunit_look.h", "ce_playunit_camera.h"):
            (self.scratch / name).write_text((PORT / "game" / name).read_text())
        binary = self.scratch / name
        compiled = subprocess.run(
            ["g++", "-std=c++14", "-O2", "-Wall", "-Wextra", "-Werror",
             "-DCE_PLAYUNIT_CAMERA_CONTROL", "-I", str(self.scratch),
             str(DRIVER), "-o", str(binary)],
            text=True, capture_output=True, timeout=30, check=False)
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        result = subprocess.run([str(binary)], text=True, capture_output=True,
                                timeout=10, check=False)
        return result.returncode, result.stdout

    def test_displayed_pose_preserves_logic_and_refuses_unowned_views(self):
        status, output = self.run_camera(self.adapter, "camera-current")
        self.assertEqual(status, 0, output)
        self.assertIn("failures=0", output)
        self.assertGreaterEqual(int(re.search(r"camera checks=(\d+)", output).group(1)), 90)

    def test_omitting_the_presentation_adapter_is_detected(self):
        old = "\n    return &view;\n"
        self.assertEqual(self.adapter.count(old), 1)
        mutant = self.adapter.replace(old, "\n    return original;\n", 1)
        status, output = self.run_camera(mutant, "camera-no-adapter")
        self.assertEqual(status, 2)
        self.assertIn("position follows displayed pose", output)

    def test_overriding_another_players_view_is_detected(self):
        old = "player != GAME.GetPlayer(0)"
        self.assertEqual(self.adapter.count(old), 1)
        mutant = self.adapter.replace(old, "!player", 1)
        status, output = self.run_camera(mutant, "camera-foreign-player")
        self.assertEqual(status, 2)
        self.assertIn("other player refused", output)

    def reject(self, name, check, adapter=None, view_adapter=None, view_header=None):
        status, output = self.run_camera(adapter or self.adapter, name, view_adapter, view_header)
        self.assertEqual(status, 2, f"{name} was not rejected: {output}")
        self.assertIn(check, output, f"{name} was rejected, but not by {check!r}: {output}")

    def swap(self, text, old, new, what):
        self.assertEqual(text.count(old), 1, f"{what}: the text to change is not there exactly once")
        return text.replace(old, new, 1)

    def test_a_camera_that_is_not_smoothed_is_detected(self):
        mutant = self.swap(self.adapter, "    if (gViewMode == kViewSmooth) SmoothView(unit, &pos, &ori);\n", "", "the call")
        self.reject("camera-unsmoothed", "moves the shown one by about half a degree a frame at most", adapter=mutant)

    def test_smoothing_that_moves_for_each_view_and_not_each_frame_is_detected(self):
        once = "\tif (frame == gShownFrame)\n\t{\n\t\tif (gShownMade) { *pos = gShownPos; *ori = gShownOri; }\n\t\treturn;\n\t}\n"
        mutant = self.swap(self.view_adapter, once, "", "the once-a-frame gate")
        self.reject("camera-each-view", "a frame's second view is shown the camera the first one used", view_adapter=mutant)

    def test_smoothing_that_survives_a_new_possession_is_detected(self):
        mutant = self.swap(self.view_adapter, "\tmemset(&gSmoother, 0, sizeof(gSmoother));\n", "", "the reset")
        self.reject("camera-no-reset", "position follows displayed pose", view_adapter=mutant)

    def test_a_clock_read_in_the_wrong_unit_is_detected(self):
        mutant = self.swap(self.view_adapter, "(double)(now - gShownNs) * 1e-9", "(double)(now - gShownNs) * 1e-6", "the unit of the clock")
        self.reject("camera-clock-unit", "a 9 degree step", view_adapter=mutant)

    def test_a_turn_that_is_smoothed_is_detected(self):
        header = self.swap(self.view_header, "v.yaw = CeUnitViewWrap(atan2f(fy, fx) - unitYaw);", "v.yaw = CeUnitViewWrap(atan2f(fy, fx));", "the heading in")
        header = self.swap(header, "const float heading = unitYaw + v.yaw;", "const float heading = v.yaw;", "the heading out")
        self.reject("camera-turn-smoothed", "turning included", view_header=header)

    def test_settling_that_is_not_exact_for_any_frame_time_is_detected(self):
        exact = ("\tconst float d = *x - target, e = expf(-omega * dt), t = *rate + omega * d;\n"
                 "\t*x = target + (d + t * dt) * e;\n\t*rate = (*rate - omega * t * dt) * e;\n")
        euler = "\t*rate += (-2.f * omega * *rate - omega * omega * (*x - target)) * dt;\n\t*x += *rate * dt;\n"
        header = self.swap(self.view_header, exact, euler, "the settling")
        self.reject("camera-euler", "a 9 degree step", view_header=header)

    def test_a_target_that_is_somewhere_the_ground_could_not_put_it_is_detected(self):
        header = self.swap(self.view_header, "const float kUnitViewSnapAngle = 0.9f;", "const float kUnitViewSnapAngle = 100.f;", "the snap")
        self.reject("camera-no-snap", "a tilt the ground could not have made is shown as it is", view_header=header)

    def test_a_gap_that_does_not_start_the_settling_afresh_is_detected(self):
        header = self.swap(self.view_header, " || dt > kUnitViewStall", "", "the stall")
        self.reject("camera-no-stall", "a gap of half a second", view_header=header)

    def test_a_settling_that_is_too_slow_or_too_quick_is_detected(self):
        for omega, check in (("100.f", "about half a degree a frame at most"), ("2.f", "nearly done after 600 ms")):
            header = self.swap(self.view_header, "const float kUnitViewOmega = 10.f;", f"const float kUnitViewOmega = {omega};", "omega")
            self.reject("camera-omega-" + omega[:-3], check, view_header=header)

    def test_axes_built_the_wrong_way_round_are_detected(self):
        header = self.swap(self.view_header, "right[0] = r0x * cr + wx * sr;", "right[0] = r0x * cr - wx * sr;", "the roll")
        self.reject("camera-roll-sign", "each other's inverse", view_header=header)


class Fences(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = MODULE.read_text()
        cls.header = HEADER.read_text()
        cls.rules = RULES.read_text()
        cls.update = UPDATE.read_text()
        cls.map = MAP.read_text()
        cls.build = BUILD.read_text()
        cls.code = comments_off(cls.module)

    def test_the_module_is_wrapped_in_the_ce_layer_and_off_by_default(self):
        for name, text in (("ce_playunit.cpp", self.module), ("ce_playunit.h", self.header)):
            body = [l for l in text.split("\n") if l.strip() and not l.lstrip().startswith("//")]
            first = next(l for l in body if l.startswith("#ifdef") or l.startswith("#ifndef"))
            self.assertTrue(first == "#ifdef CE_VERSION" or (name.endswith(".h") and body[1].strip() == "#define PORT_CE_PLAYUNIT_H"), name)
            self.assertEqual(body[-1].strip(), "#endif", name)
            self.assertIn("#ifdef CE_VERSION", text, f"{name} is wrapped in CE_VERSION")
        self.assertIn('PortCeExperimental("play_unit")', self.module, "the option is the ini line play_unit (1, or the older tank), read once in ce.cpp")
        self.assertEqual(self.module.count('PortCeExperimental("'), 1, "one read of the ini line")
        self.assertIn('gWanted = !fenced && PortCeExperimental("play_unit");', self.module, "the ini is read only where no fenced variable is set")
        self.assertIn("const bool fenced = FencedEnv() && !gProof;", self.module)
        self.assertLess(self.module.index("const bool fenced = FencedEnv()"), self.module.index('PortCeExperimental("play_unit")'), "the fence is asked before the ini")

    def test_the_fence_names_the_variables_every_ce_mode_names(self):
        for name in FENCED_ENV:
            self.assertIn(f'"{name}"', self.module, f"the fence lacks {name}")
        start = self.module.index("kFenced[]")
        listed = re.findall(r'"((?:BEA_PORT|BEA_NET)_[A-Z_]+)"', self.module[start:self.module.index("};", start)])
        self.assertEqual(sorted(listed), sorted(FENCED_ENV), "the fence names exactly the variables photo mode and commander name")

    def test_the_gate_is_filled_from_the_game_and_every_field_is_set(self):
        gate = self.module[self.module.index("CePlayUnitGate FillGate(bool optionOn)"):self.module.index("void ReadConfiguration()")]
        for field in GATE_FIELDS:
            self.assertRegex(gate, r"\bg\.%s = " % field, f"the gate's field {field} is set")
        for clause in GATE_CLAUSES:
            self.assertIn(clause, gate, f"the gate lacks {clause}")
        fields = re.search(r"struct CePlayUnitGate\n\{(.*?)\n\};", self.rules, re.S).group(1)
        self.assertEqual(sorted(re.findall(r"^\t(?:bool|int) (\w+);", fields, re.M)), sorted(GATE_FIELDS), "the gate holds exactly the fields the module fills")
        self.assertIn("return CePlayUnitAllowed(g);", gate)
        self.assertIn("return g;", gate)

    def test_the_take_over_waits_for_a_standing_engine(self):
        ready = self.module[self.module.index("bool Ready("):self.module.index("// The gate, filled from the game")]
        for clause in ("GAME_STATE_PLAYING", "GAME.IsPaused()", "BATTLE_ENGINE_STATE_WALKER", "IsWalkerOnGroundOrObject()", "GetIsPoweredUp()", "GetShowHUD()",
                       "controller->GetToControl() != player", "GAME.IsFreeCameraOn(0)"):
            self.assertIn(clause, ready, f"the take-over is not made before {clause} holds")

    def test_the_test_machinery_is_private(self):
        shipped = comments_off(without_test(self.module))
        for word in ("BEA_PORT_PLAYUNIT", "fopen", "gProbe", "gGod", "gKeepAI", "gObserve", "gCamTest", "gViewOnly", "gWantThing", "gOut", "Probe("):
            self.assertNotIn(word, shipped, f"a plain build compiles {word}")
        for name in ("BEA_PORT_PLAYUNIT_PROOF", "BEA_PORT_PLAYUNIT_PROBE", "BEA_PORT_PLAYUNIT_GOD", "BEA_PORT_PLAYUNIT_KEEPAI"):
            self.assertIn(name, self.module, f"the test build has {name}")
        self.assertEqual(self.build.count("-DCE_PLAYUNIT_TEST=1"), 1, "the test define is enabled only by the opt-in flag")
        self.assertIn('if a.play_unit_test:\n        CE_DEFINES.append("-DCE_PLAYUNIT_TEST=1")', self.build, "only build.py's flag defines it")
        users = sorted(str(p.relative_to(ROOT)) for p in PORT.rglob("*")
                       if p.is_file() and p.suffix in (".cpp", ".h", ".c") and "CE_PLAYUNIT_TEST" in p.read_text(errors="ignore"))
        self.assertEqual(users, ["port/game/ce_playunit.cpp", "port/game/ce_playunit.h"], "only the module and its header's comment name the test define")
        self.assertNotIn("CE_PLAYUNIT_TEST", comments_off(self.header), "the header holds no test code")

    def test_no_direct_rng_calls_and_camera_only_allocation(self):
        for banned in ("rand(", "srand(", "FloatRandom", "RANDOM", "malloc", "calloc", "realloc", "free(", " new ", "delete ", "std::", "strdup"):
            if banned in (" new ", "delete "):
                continue
            self.assertNotIn(banned, self.code, f"the module uses {banned!r}")
        shipped = comments_off(without_test(self.module))
        self.assertEqual(len(re.findall(r"\bnew \(MEMTYPE_CAMERA\)", shipped)), 1, "the production allocation is the driven unit's camera, owned by the game")
        # the direct law's guide is the one other allocation, and its region the only place that deletes: the guide itself at the release, and the unit's own guide
        # it keeps when the unit is shut down with it inside (the unit deletes its guide, CUnit::Shutdown)
        plain = without_test(self.module)
        begin, end = plain.index("// ---- the direct law's guide (begin)"), plain.index("// ---- the direct law's guide (end)")
        guide, rest = comments_off(plain[begin:end]), comments_off(plain[:begin] + plain[end:])
        self.assertEqual(len(re.findall(r"\bnew \(MEMTYPE_GUIDE\) CeDriveGuide\(", guide)), 1, "one drive guide is made, in its region")
        self.assertEqual(len(re.findall(r"\bnew \(", rest)), 1, "nothing else is allocated")
        self.assertNotIn("delete ", comments_off(rest), "the game owns camera deletion; the module must not double-delete or free a borrowed view")
        self.assertEqual(comments_off(guide).count("delete "), 2, "the drive guide deletes the kept guide once, and is deleted once at the release")
        self.assertIn("union PilotHolder", self.code, "the driver is built once in static storage and never destroyed")
        self.assertIn("~PilotHolder() {}", self.code)
        hooks_text = (PORT / "game/ce_hooks.cpp").read_text()
        ce_block = hooks_text[hooks_text.index("#ifdef CE_VERSION"):hooks_text.index("#else")]
        self.assertIn("PortCePlayUnitRenderCamera(camera, player)", ce_block)
        self.assertLess(ce_block.index("commander_camera"), ce_block.index("playunit_camera"))
        for patch in (PORT / "patches").glob("*.patch"):
            text = patch.read_text(errors="ignore")
            if patch.name == "0241-ce-infantry-first-person.patch":
                additions = [line[1:].strip() for line in text.splitlines() if line.startswith("+") and not line.startswith("+++")]
                visible_guard = "if (PortCePlayUnitHidesOwnMesh(this)) return;"
                declaration = "extern bool PortCePlayUnitHidesOwnMesh(const CThing *);"
                self.assertEqual(additions.count(visible_guard), 2)
                self.assertEqual(additions.count(declaration), 2)
                # each pair sits inside CE_VERSION: a private build (no CE_VERSION) has no play-unit module to ask
                self.assertEqual(additions.count("#ifdef CE_VERSION"), 2)
                self.assertEqual(additions.count("#endif"), 2)
                self.assertEqual(set(additions), {visible_guard, declaration, "#ifdef CE_VERSION", "#endif"})
                self.assertIn("CThing::Render(", text)
                self.assertIn("CThing::RenderImposter(", text)
            elif patch.name == "0240-ce-playunit-battleline.patch":
                # the battle line's pass also draws while a unit is driven: one weak declaration and one guarded call, inside CE_VERSION, after the cameras' own answer
                self.assertEqual(text.count("PortCePlayUnitShowsBattleline"), 3)
                self.assertNotIn("CePlayUnit", text.replace("PortCePlayUnitShowsBattleline", ""))
                self.assertIn('+extern "C" int PortCePlayUnitShowsBattleline(int viewpoint) __attribute__((weak));', text)
                self.assertIn("+		if (!render && PortCePlayUnitShowsBattleline && PortCePlayUnitShowsBattleline(0))\n+			render=TRUE;", text)
                self.assertEqual(len([l for l in text.splitlines() if l.startswith("-") and not l.startswith("---")]), 0, "the patch only adds lines")
            elif patch.name == "0275-ce-playunit-native-aim.patch":
                aim_additions = "\n".join(line[1:] for line in text.splitlines()
                                          if line.startswith("+") and not line.startswith("+++"))
                self.assertEqual(aim_additions.count("CePlayUnit"), 2)
                self.assertEqual(aim_additions.count("PortCePlayUnitAimPoint"), 2)
            elif patch.name == "0285-ce-playunit-hud.patch":
                # the HUD's focus and overlay while a unit is driven: only additions, inside CE_VERSION, naming only the focus, its reader and the overlay
                hud_additions = "\n".join(line[1:] for line in text.splitlines() if line.startswith("+") and not line.startswith("+++"))
                removed = [l for l in text.splitlines() if l.startswith("-") and not l.startswith("---")]
                self.assertEqual(len(removed), 2, "the patch only adds lines, but for the energy arc's two visibility reads")
                self.assertTrue(all("HUD.IsComponentVisible(1)" in l for l in removed))
                names = set(re.findall(r"CePlayUnit\w*", hud_additions))
                self.assertEqual(names, {"CePlayUnitHudFocus", "CePlayUnitHudFocusOf", "CePlayUnitHudOverlay"})
                self.assertEqual(hud_additions.count("#ifdef CE_VERSION"), hud_additions.count("#endif"))
            elif patch.name in ("0356-ce-playunit-firstperson-parts.patch",
                                "0358-ce-playunit-class-hooks.patch",
                                "0359-ce-playunit-muspell-side.patch"):
                additions = "\n".join(line[1:] for line in text.splitlines() if line.startswith("+") and not line.startswith("+++"))
                self.assertIn("__attribute__((weak))", additions, f"{patch.name} must use weak hooks")
                self.assertGreaterEqual(additions.count("#ifdef CE_VERSION"), 1, f"{patch.name} must guard hooks with CE_VERSION")
                self.assertEqual(additions.count("#ifdef CE_VERSION"), additions.count("#endif"), f"{patch.name} unbalanced CE_VERSION")
            else:
                # Context in a later patch may contain an existing hook. Inspect the changed code.
                changed = "\n".join(line[1:] for line in text.splitlines()
                                    if line.startswith(("+", "-")) and not line.startswith(("+++", "---")))
                self.assertNotIn("CePlayUnit", changed, f"{patch.name} unexpectedly changes a tank hook")
        for name in ("rand", "Rand"):
            self.assertNotIn(name, self.rules)

    def test_camera_lifetime_uses_game_ownership_and_waits_for_borrowers(self):
        self.assertIn("GAME.SetCurrentCamera(0, gCamera, TRUE);", self.code)
        self.assertIn("virtual ~CeTankCamera()", self.code)
        self.assertIn("if (gCamera == this) gCamera = NULL;", self.code)
        reconcile = self.code[self.code.index("void ReconcileCamera()"):self.code.index("void Release(")]
        self.assertIn("GAME.GetCamera(0) != gCamera", reconcile)
        self.assertIn("GAME.IsFreeCameraOn(0)", reconcile)
        self.assertIn("player->GotoControlView();", reconcile)
        self.assertNotIn("delete", reconcile)
        release = self.code[self.code.index("void Release("):self.code.index("bool Take(")]
        self.assertIn("if (!gDriving) return;", release)
        self.assertIn("GAME.ToggleFreeCameraOff(0)", release)
        self.assertLess(release.index("ToggleFreeCameraOff"), release.index("ReconcileController"))
        reconcile_controller = self.code[self.code.index("void ReconcileController()"):self.code.index("// ---- the world") if "// ---- the world" in self.code else self.code.index("bool IsNormal(")]
        self.assertIn("if (gDriving || !gControllerPending) return;", reconcile_controller)
        self.assertIn("controller->GetToControl() == Pilot()", reconcile_controller)
        self.assertIn("controller->RelinquishControl();", reconcile_controller)
        self.assertIn("gControllerPending = false;", reconcile_controller)
        ready = self.code[self.code.index("bool Ready("):self.code.index("bool Allowed(")]
        for clause in ("gCamera ||", "GAME.mRemoteCamera", "GAME.mOldRemoteCamera"):
            self.assertIn(clause, ready)
        self.assertIn("PortCeCmdMapOpen()", self.code, "the active Commander view owns input, not its configuration setting")
        reset = self.code[self.code.index("void Reset()"):self.code.index("void FinishBorrow(")]
        self.assertNotIn("gCamera =", reset, "level bookkeeping must not drop a borrowed camera")

    def test_handback_resolves_live_identity_and_uses_native_reciprocal_membership(self):
        previous = self.code[self.code.index("CNormalSquad *PreviousSquad()"):self.code.index("bool SquadCompatible(")]
        for check in ("gLevel != GAME.GetCurrentLevel()", "WORLD.GetSquadNB()", "candidate == gPreviousSquad", "GetThingNumber() == gPreviousSquadNumber", "IsNormal(candidate)", "IsShuttingDown()", "IsDying()"):
            self.assertIn(check, previous)
        handback = self.code[self.code.index("void ReturnUnitControl()"):self.code.index("void Release(")]
        for check in ("sameLevel ? Held() : NULL", "GetAllegiance() == gPossessedAllegiance", "GetAIState() == AI_OFF", "CePlayUnitChooseReturn(facts)", "squad->Unknown67(unit, 0, 0)", "unit->SetAIState(squad->GetAIState())"):
            self.assertIn(check, handback)
        self.assertLess(handback.index("unit->GoToPoint"), handback.index("squad->Unknown67"), "the player's destination is cleared before the native squad hand-back")
        drive = self.code[self.code.index("void Drive("):self.code.index("void CePlayUnitUpdate()")]
        self.assertIn("another owner or AI took control", drive)
        self.assertNotIn("Unknown4e6f70", drive, "the driver must not remove a replacement squad owner")
        self.assertNotIn("SetAIState(AI_OFF)", drive, "the driver must not overwrite externally resumed AI")

    def test_the_hooks(self):
        self.assertEqual(self.update.count("CePlayUnitUpdate();"), 1, "one call in the per-update hook")
        call = self.update.index("CePlayUnitUpdate();")
        self.assertTrue(self.update.rfind("#ifdef CE_VERSION", 0, call) > self.update.rfind("#endif", 0, call), "the call is inside CE_VERSION")
        self.assertLess(self.update.index("CeCommanderUpdate();"), call, "after the commander's")
        self.assertIn('#ifdef CE_VERSION\n#include "ce_playunit.h"\n#endif', self.update)
        self.assertEqual(self.map.count("CePlayUnitLevelEnd();"), 1)
        level_end = self.map[self.map.index("void PortCeCmdLevelEnd()"):]
        self.assertLess(level_end.index("CePlayUnitLevelEnd();"), level_end.index("gStrategicOwner.Reset();"), "the level's end gives the controls back before resetting camera ownership")

    def test_the_driver_stands_aside_for_everything_it_does_not_use(self):
        self.assertIn("player->ReceiveButtonAction(from, button, val);", self.code, "pause, help and the menus go on to the player")
        self.assertIn("return player ? player->CanBeControlledWhenInPause() : FALSE;", self.code)
        self.assertIn("return player ? player->GetControlType() : CONTROL_MECH;", self.code)
        self.assertIn("controller->GetToControl() == Pilot()", self.code, "the driver is taken off the stack only from the top")
        for button in ("BUTTON_MECH_FORWARD", "BUTTON_MECH_STRAFE_LEFT", "BUTTON_MECH_STRAFE_RIGHT", "BUTTON_MECH_CHARGE_GUN_POD", "BUTTON_MECH_FIRE_GUN_POD", "BUTTON_MECH_MORPH"):
            self.assertIn(button, self.code, f"the driver reads {button}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
