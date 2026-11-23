#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host unit & causal-mutant suite for Play Any Unit (all unit classes, 1P barrel/weapon framing,
artillery 3D ballistic arc & spotter camera, aircraft flight law, naval/dropship unload state machine,
and Forseti vs Muspell faction HUD palettes & class readout formatting).

Compiles port/game/ce_playunit_tactics.h, port/game/ce_playunit_rules.h, and port/game/ce_cmd_hud.h
under ASan/UBSan + -Wall -Wextra -Werror with zero game assets or display required.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RULES_H = ROOT / "port/game/ce_playunit_rules.h"
TACTICS_H = ROOT / "port/game/ce_playunit_tactics.h"
CMD_HUD_H = ROOT / "port/game/ce_cmd_hud.h"
PLAYUNIT_CPP = ROOT / "port/game/ce_playunit.cpp"
PLAYUNIT_HUD_CPP = ROOT / "port/game/ce_playunit_hud.cpp"
PATCH_0358 = ROOT / "port/patches/0358-ce-playunit-class-hooks.patch"

DRIVER = r'''// SPDX-License-Identifier: GPL-3.0-only
#include "ce_playunit_rules.h"
#include "ce_playunit_tactics.h"
#include "ce_cmd_hud.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

static int gChecks = 0;

static void Require(bool ok, const char *label)
{
	++gChecks;
	if (!ok)
	{
		std::fprintf(stderr, "FAIL %s\n", label);
		std::exit(1);
	}
}

static bool Near(float a, float b, float eps = 1e-3f)
{
	return std::fabs(a - b) <= eps;
}

static float FlatGround(void *, float, float) { return 0.f; }
static float HillGround(void *, float x, float) { return -0.15f * x; } // ground rises toward +X (BEA Z down)
static float BasinGround(void *, float, float) { return 25.f; }        // below water level

static void CheckAllClassEligibility()
{
	CePlayUnitFacts base = {};
	base.friendly = true;
	base.alive = true;
	base.armed = true;

	// Stationary Turret / Emplacement
	{
		CePlayUnitFacts f = base;
		f.turret = true;
		f.family = CE_PLAYUNIT_FAMILY_EMPLACEMENT;
		Require(CePlayUnitClassFor(f) == CE_PLAYUNIT_CLASS_TURRET, "turret: friendly armed emplacement eligible");
		Require(CePlayUnitRefusalFor(f) == CE_PLAYUNIT_TAKE_OK, "turret: take ok");
		f.friendly = false;
		Require(CePlayUnitRefusalFor(f) == CE_PLAYUNIT_REFUSE_ENEMY, "turret: enemy turret refused");
		f.friendly = true; f.scripted = true;
		Require(CePlayUnitRefusalFor(f) == CE_PLAYUNIT_REFUSE_MISSION, "turret: scripted turret refused");
		f.scripted = false; f.armed = false;
		Require(CePlayUnitRefusalFor(f) == CE_PLAYUNIT_REFUSE_UNARMED, "turret: unarmed emplacement refused");
	}

	// Aircraft: Fighter, Bomber, and unarmed Dropship transport
	{
		CePlayUnitFacts fighter = base;
		fighter.aircraft = true;
		fighter.family = CE_PLAYUNIT_FAMILY_AIRCRAFT;
		Require(CePlayUnitClassFor(fighter) == CE_PLAYUNIT_CLASS_AIRCRAFT, "aircraft: armed fighter/bomber eligible");

		CePlayUnitFacts dropship = base;
		dropship.aircraft = true;
		dropship.armed = false;
		dropship.transport = true;
		dropship.family = CE_PLAYUNIT_FAMILY_AIRCRAFT;
		Require(CePlayUnitClassFor(dropship) == CE_PLAYUNIT_CLASS_AIRCRAFT, "aircraft: unarmed dropship with cargo eligible");

		dropship.transport = false;
		Require(CePlayUnitRefusalFor(dropship) == CE_PLAYUNIT_REFUSE_UNARMED, "aircraft: unarmed empty aircraft refused");
	}

	// Naval Vessels & Landing Craft
	{
		CePlayUnitFacts lander = base;
		lander.naval = true;
		lander.armed = false;
		lander.transport = true;
		lander.family = CE_PLAYUNIT_FAMILY_NAVAL;
		Require(CePlayUnitClassFor(lander) == CE_PLAYUNIT_CLASS_NAVAL, "naval: landing craft with cargo eligible");

		CePlayUnitFacts gunboat = base;
		gunboat.naval = true;
		gunboat.family = CE_PLAYUNIT_FAMILY_NAVAL;
		Require(CePlayUnitClassFor(gunboat) == CE_PLAYUNIT_CLASS_NAVAL, "naval: armed naval vessel eligible");

		lander.objective = true;
		Require(CePlayUnitRefusalFor(lander) == CE_PLAYUNIT_REFUSE_MISSION, "naval: mission objective boat refused");
	}

	// Artillery (deploying ground vehicle, both in squad and standalone)
	{
		CePlayUnitFacts art = base;
		art.vehicle = true;
		art.groundUnit = true;
		art.deploys = true;
		art.normalSquad = false;
		Require(CePlayUnitClassFor(art) == CE_PLAYUNIT_CLASS_ARTILLERY, "artillery: standalone deploying artillery eligible");
		Require(!CePlayUnitTankEligible(art), "artillery: not classified as ordinary tank");

		art.spawned = true;
		Require(CePlayUnitRefusalFor(art) == CE_PLAYUNIT_REFUSE_SQUAD, "artillery: spawned-lead artillery refused");
	}
}

static void CheckFirstPersonPartsAndWeaponView()
{
	// Real retail mesh part names across Forseti & Muspell tanks, artillery, turrets, and landers
	const char *barrelParts[] = {
		"barrel", "barrela", "barrelb", "X1 barrel", "X1 Barrel 01",
		"cannon", "blaster", "blaster01", "blaster02",
		"gun-lid", "gun-inside", "gun-base",
		"TOP firer", "right firer", "left firer", "muzzle01"
	};
	for (unsigned i = 0; i < sizeof(barrelParts) / sizeof(barrelParts[0]); ++i)
	{
		Require(CePlayUnitPartIsBarrel(barrelParts[i]), "1P part filter recognizes retail barrel/gun sub-mesh");
		Require(!CePlayUnitFirstPersonShouldSkipPart(true, barrelParts[i]), "1P barrel pass keeps barrel sub-mesh");
		Require(!CePlayUnitFirstPersonShouldSkipPart(false, barrelParts[i]), "normal pass never skips barrel");
	}

	const char *bodyParts[] = {
		"tankbody", "Tank_Hull", "turret", "body", "hull", "base",
		"turretbase", "guard", "gun_support", "plough", "right door", "Object02", "forsettitrooper", ""
	};
	for (unsigned i = 0; i < sizeof(bodyParts) / sizeof(bodyParts[0]); ++i)
	{
		Require(!CePlayUnitPartIsBarrel(bodyParts[i]), "1P part filter rejects hull/turret interior part");
		Require(CePlayUnitFirstPersonShouldSkipPart(true, bodyParts[i]), "1P barrel pass skips hull/turret interior part");
		Require(!CePlayUnitFirstPersonShouldSkipPart(false, bodyParts[i]), "normal pass never skips hull/turret part");
	}
	Require(CePlayUnitFirstPersonShouldSkipPart(true, NULL), "1P barrel pass skips null part name safely");

	// Tuned 1P eye offsets keep gunner above turret roof (negative Z = up in BEA)
	float off[3];
	CePlayUnitFirstPersonEyeAdjust(CE_PLAYUNIT_CLASS_TANK, 6.f, off);
	Require(off[2] < -0.5f && off[1] > 0.f, "1P tank hatch offset sits slightly above turret roof");
	CePlayUnitFirstPersonEyeAdjust(CE_PLAYUNIT_CLASS_AIRCRAFT, 10.f, off);
	Require(off[1] > 1.5f, "1P aircraft cockpit offset sits forward at nose");

	// Infantry weapon viewmodel bob & recoil animation
	CePlayUnitWeaponViewState ws;
	CePlayUnitWeaponViewReset(&ws);
	CePlayUnitWeaponViewStep(&ws, 0.05f, 3.0f, 0.1f, true);
	Require(Near(ws.recoil, 1.f) && Near(ws.flash, 1.f) && ws.bobAmp > 0.f, "infantry weapon fire triggers recoil & flash");
	const float r0 = ws.recoil;
	CePlayUnitWeaponViewStep(&ws, 0.05f, 3.0f, 0.0f, false);
	Require(ws.recoil < r0 && ws.recoil > 0.f, "infantry weapon recoil decays smoothly after shot");
	CePlayUnitWeaponViewStep(&ws, 0.05f, 0.0f, std::numeric_limits<float>::quiet_NaN(), false);
	Require(std::isfinite(ws.swayX) && std::isfinite(ws.swayY), "infantry weapon animation tolerates NaN yaw delta");
}

static void CheckBallisticArcAndSpotter()
{
	// Flat ground launch at 45 degrees up (BEA pitch = -pi/4), yaw = -pi/2 (facing +X)
	const float muzzle[3] = {0.f, 0.f, -2.f};
	const float yaw = -1.5707963f; // forward = (-sin(-pi/2), cos(-pi/2)) = (+1, 0)
	const float pitch = -0.78539816f;
	CePlayUnitBallisticArc arc;
	Require(CePlayUnitBallisticSolve(muzzle, yaw, pitch, 60.f, 1.0f, 12.f, 100.f, FlatGround, NULL, &arc),
	        "ballistic: valid arc solved on flat ground");
	Require(arc.valid && arc.hitsGround && arc.count >= 10 && arc.count <= kCePlayUnitArcMaxPoints,
	        "ballistic: arc points populated within bounds");
	Require(Near(arc.pts[0].x, muzzle[0]) && Near(arc.pts[0].z, muzzle[2]), "ballistic: starts at muzzle");
	Require(Near(arc.impact[2], 0.f, 0.05f) && arc.impact[0] > 100.f, "ballistic: lands accurately on flat ground");
	Require(Near(arc.elevationDeg, 45.f, 0.2f), "ballistic: elevation degrees positive upward");
	Require(Near(arc.azimuthDeg, 90.f, 0.5f), "ballistic: east heading is 090 deg");
	// Apex rises well above muzzle (more negative Z)
	float minZ = muzzle[2];
	for (int i = 0; i < arc.count; ++i)
		if (arc.pts[i].z < minZ) minZ = arc.pts[i].z;
	Require(minZ < muzzle[2] - 20.f, "ballistic: trajectory curves cleanly above muzzle");

	// Sloped hillside shortens impact distance compared to flat ground
	CePlayUnitBallisticArc hillArc;
	Require(CePlayUnitBallisticSolve(muzzle, yaw, pitch, 60.f, 1.0f, 12.f, 100.f, HillGround, NULL, &hillArc),
	        "ballistic: solved against rising slope");
	Require(hillArc.hitsGround && hillArc.impact[0] < arc.impact[0] - 20.f,
	        "ballistic: rising hillside intercepts shell earlier");
	Require(Near(hillArc.impact[2], HillGround(NULL, hillArc.impact[0], hillArc.impact[1]), 0.1f),
	        "ballistic: impact height matches slope surface");

	// Water surface stops trajectory when ground is underwater basin
	CePlayUnitBallisticArc waterArc;
	Require(CePlayUnitBallisticSolve(muzzle, yaw, pitch, 60.f, 1.0f, 12.f, 2.0f, BasinGround, NULL, &waterArc),
	        "ballistic: solved over water");
	Require(waterArc.hitsGround && Near(waterArc.impact[2], 2.0f, 0.05f) && waterArc.impact[0] < arc.impact[0] + 15.f,
	        "ballistic: trajectory impacts water surface");

	// Pitch solver hits requested target accurately when re-integrated (discrete refinement!)
	float solvedPitch = 0.f;
	const float target[3] = {180.f, 0.f, 0.f};
	Require(CePlayUnitBallisticPitchForTarget(muzzle, target, 60.f, 1.0f, -1.25f, 0.3f, false, &solvedPitch),
	        "ballistic: pitch solver succeeds for in-range target");
	CePlayUnitBallisticArc verifyArc;
	Require(CePlayUnitBallisticSolve(muzzle, yaw, solvedPitch, 60.f, 1.0f, 12.f, 100.f, FlatGround, NULL, &verifyArc),
	        "ballistic: solved pitch integrates cleanly");
	Require(std::fabs(verifyArc.impact[0] - target[0]) < 0.5f, "ballistic: discrete-refined pitch lands tightly on target");

	// Out-of-range target returns false cleanly instead of falsely claiming true at 45 deg
	const float farTarget[3] = {520.f, 0.f, 0.f};
	float farPitch = 0.f;
	Require(!CePlayUnitBallisticPitchForTarget(muzzle, farTarget, 60.f, 1.0f, -1.25f, 0.3f, false, &farPitch),
	        "ballistic: out-of-range target returns false");
	Require(std::isfinite(farPitch), "ballistic: out-of-range fallback pitch stays finite");

	// Heading wrap handles extreme finite and non-finite inputs in O(1)
	const float hHugePos = CePlayUnitHeadingDegFromYaw(1e10f);
	const float hHugeNeg = CePlayUnitHeadingDegFromYaw(-1e30f);
	Require(std::isfinite(hHugePos) && hHugePos >= 0.f && hHugePos < 360.f, "heading: 1e10f finishes in O(1) in [0,360)");
	Require(std::isfinite(hHugeNeg) && hHugeNeg >= 0.f && hHugeNeg < 360.f, "heading: -1e30f finishes in O(1) in [0,360)");
	Require(CePlayUnitHeadingDegFromYaw(std::numeric_limits<float>::quiet_NaN()) == 0.f, "heading: NaN returns 0");

	// Artillery ridge clearance: mid-range ridge (-10m blocks straight-line LOS at -1m, cleared by -13.6m low-arc apex);
	// sheer cliff right in front of muzzle blocks the parabolic arc and is rejected.
	auto LowMidRidge = [](void *, float x, float) -> float { return (x > 80.f && x < 100.f) ? -10.f : 0.f; };
	auto SheerCliff  = [](void *, float x, float) -> float { return (x > 10.f && x < 30.f) ? -80.f : 0.f; };
	Require(CePlayUnitBallisticArcClearsRidge(muzzle, target, 60.f, 1.0f, 12.f, 100.f, LowMidRidge, NULL, -1.25f, 0.3f, false),
	        "ballistic: parabolic arc clears mid-range ridge");
	Require(!CePlayUnitBallisticArcClearsRidge(muzzle, target, 60.f, 1.0f, 12.f, 100.f, SheerCliff, NULL, -1.25f, 0.3f, false),
	        "ballistic: sheer cliff blocking trajectory is rejected");

	// Invalid inputs fail safely
	Require(!CePlayUnitBallisticSolve(muzzle, yaw, pitch, 0.f, 1.0f, 12.f, 100.f, FlatGround, NULL, &arc),
	        "ballistic: zero speed rejected");

	// Spotter / Shot-Follow camera transitions
	CePlayUnitSpotterState st;
	CePlayUnitSpotterReset(&st);
	Require(!st.enabled && st.mode == CE_SPOTTER_OFF, "spotter: starts off");
	Require(CePlayUnitSpotterToggle(&st, true, verifyArc.impact), "spotter: toggle enables overwatch");
	Require(st.enabled && st.mode == CE_SPOTTER_OVERWATCH && st.hasImpact, "spotter: enters overwatch on predicted impact");

	const float s0[3] = {40.f, 0.f, -20.f}, sv[3] = {2.f, 0.f, -0.5f};
	CePlayUnitSpotterOnShellLaunch(&st, s0, sv);
	Require(st.mode == CE_SPOTTER_FOLLOW_SHELL && st.hasShell, "spotter: switches to shell-follow on launch");

	const float s1[3] = {120.f, 0.f, -18.f};
	CePlayUnitSpotterUpdate(&st, 0.05f, true, s1, sv, true, verifyArc.impact);
	Require(st.mode == CE_SPOTTER_FOLLOW_SHELL && Near(st.shellPos[0], 120.f), "spotter: tracks moving shell");

	// Verify spotter camera pose is valid, orthonormal, and UPRIGHT (outDown[2] > 0) while tracking shell
	float eye[3], r[3], f[3], d[3];
	const float unitPos[3] = {0.f, 0.f, 0.f};
	Require(CePlayUnitSpotterCameraPose(st, unitPos, yaw, 0.f, eye, r, f, d), "spotter: valid shell-follow pose");
	Require(eye[2] < s1[2] - 5.f, "spotter: aerial vantage sits above shell");
	Require(Near(r[0]*r[0]+r[1]*r[1]+r[2]*r[2], 1.f) && Near(f[0]*f[0]+f[1]*f[1]+f[2]*f[2], 1.f) && Near(r[0]*f[0]+r[1]*f[1]+r[2]*f[2], 0.f),
	        "spotter: camera axes orthonormal");
	Require(d[2] > 0.f, "spotter: camera down vector points toward +Z (upright, never inverted)");

	// Explicit uprightness & framing check in overwatch across multiple firing azimuths:
	// unit must project into the lower half of the screen (screenDown > 0) and impact point forward/ahead (screenDown < unit).
	const float testYaws[4] = {0.f, -1.5707963f, 1.5707963f, 2.4f};
	for (int yi = 0; yi < 4; ++yi)
	{
		const float ty = testYaws[yi];
		const float dirX = -std::sin(ty), dirY = std::cos(ty);
		const float imp[3] = {dirX * 65.f, dirY * 65.f, 0.f};
		CePlayUnitSpotterState ov;
		CePlayUnitSpotterReset(&ov);
		CePlayUnitSpotterToggle(&ov, true, imp);
		float oe[3], orv[3], ofv[3], odv[3];
		Require(CePlayUnitSpotterCameraPose(ov, unitPos, ty, 0.f, oe, orv, ofv, odv),
		        "spotter overwatch: valid pose across headings");
		Require(odv[2] > 0.1f, "spotter overwatch: outDown.z > 0 (upright in BEA Z-down world)");
		const float det = (orv[1]*ofv[2] - orv[2]*ofv[1])*odv[0]
		                + (orv[2]*ofv[0] - orv[0]*ofv[2])*odv[1]
		                + (orv[0]*ofv[1] - orv[1]*ofv[0])*odv[2];
		Require(Near(det, 1.f, 1e-3f), "spotter overwatch: right-handed (right x forward = down) basis");
		// Project artillery unit and impact point into camera coordinates
		const float uRel[3] = {unitPos[0] - oe[0], unitPos[1] - oe[1], unitPos[2] - oe[2]};
		const float iRel[3] = {imp[0] - oe[0], imp[1] - oe[1], imp[2] - oe[2]};
		const float uDepth = uRel[0]*ofv[0] + uRel[1]*ofv[1] + uRel[2]*ofv[2];
		const float iDepth = iRel[0]*ofv[0] + iRel[1]*ofv[1] + iRel[2]*ofv[2];
		const float uDown = (uRel[0]*odv[0] + uRel[1]*odv[1] + uRel[2]*odv[2]) / uDepth;
		const float iDown = (iRel[0]*odv[0] + iRel[1]*odv[1] + iRel[2]*odv[2]) / iDepth;
		Require(uDepth > 1.f && iDepth > uDepth, "spotter overwatch: both battery and impact in front, impact farther ahead");
		Require(uDown > 0.10f, "spotter overwatch: artillery battery projects to lower half of screen");
		Require(iDown < 0.0f && iDown < uDown - 0.25f, "spotter overwatch: impact ring projects forward/above battery");
	}

	// Shell disappears -> latches impact hold for kCeSpotterHoldSeconds, then returns to overwatch
	CePlayUnitSpotterUpdate(&st, 0.05f, false, NULL, NULL, true, verifyArc.impact);
	Require(st.mode == CE_SPOTTER_HOLD_IMPACT && Near(st.impactPos[0], 120.f), "spotter: holds on shell impact site");
	for (int k = 0; k < 50; ++k)
		CePlayUnitSpotterUpdate(&st, 0.05f, false, NULL, NULL, true, verifyArc.impact);
	Require(st.mode == CE_SPOTTER_OVERWATCH, "spotter: returns cleanly to predicted overwatch after hold");
	Require(!CePlayUnitSpotterToggle(&st, true, verifyArc.impact) && st.mode == CE_SPOTTER_OFF && !st.hasImpact,
	        "spotter: second toggle turns off and clears stale hasImpact");
	Require(CePlayUnitSpotterToggle(&st, false, NULL) && st.mode == CE_SPOTTER_OVERWATCH && !st.hasImpact,
	        "spotter: re-enabling without predicted impact does not reuse stale impact");

	// Spotter terrain clearance at eye position: hill behind battery/focus lifts camera above hill
	{
		CePlayUnitSpotterState hillSt;
		CePlayUnitSpotterReset(&hillSt);
		const float imp[3] = {0.f, 65.f, 0.f};
		CePlayUnitSpotterToggle(&hillSt, true, imp);
		auto HillAtEye = [](void *, float, float y) -> float { return y < 10.f ? -90.f : 0.f; };
		float he[3], hr[3], hf[3], hd[3];
		Require(CePlayUnitSpotterCameraPose(hillSt, unitPos, 0.f, 0.f, he, hr, hf, hd, HillAtEye, NULL),
		        "spotter: valid pose over intervening hill");
		Require(he[2] <= -90.f - 11.9f, "spotter: camera eye clears intervening hill by at least 12m");
	}

	// Spotter transition around 125u threshold is continuous (no jarring hard cut)
	{
		CePlayUnitSpotterState sA, sB;
		CePlayUnitSpotterReset(&sA); CePlayUnitSpotterReset(&sB);
		const float impA[3] = {0.f, 124.5f, 0.f}, impB[3] = {0.f, 125.5f, 0.f};
		CePlayUnitSpotterToggle(&sA, true, impA);
		CePlayUnitSpotterToggle(&sB, true, impB);
		float eA[3], eB[3], rT[3], fT[3], dT[3];
		Require(CePlayUnitSpotterCameraPose(sA, unitPos, 0.f, 0.f, eA, rT, fT, dT), "spotter: pose at 124.5u");
		Require(CePlayUnitSpotterCameraPose(sB, unitPos, 0.f, 0.f, eB, rT, fT, dT), "spotter: pose at 125.5u");
		const float jump = std::sqrt((eB[0]-eA[0])*(eB[0]-eA[0]) + (eB[1]-eA[1])*(eB[1]-eA[1]) + (eB[2]-eA[2])*(eB[2]-eA[2]));
		Require(jump < 4.f, "spotter: smooth transition across 125u threshold with no hard cut");
	}
}

static void CheckAircraftFlightAndNavalUnload()
{
	// Fighter flight: turns right & banks, boost raises speed, altitude floor protects from ground crash
	CePlayUnitFlightState fl = {};
	const CePlayUnitFlightParams fighter = CePlayUnitFighterParams();
	CePlayUnitFlightReset(&fl, 0.f, fighter);
	float vCruise[3], vBoost[3];
	CePlayUnitFlightStep(&fl, fighter, 0.05f, 0.f, 0.f, 0.f, 0.f, 0.f, false, -30.f, 0.f, vCruise);
	Require(vCruise[1] > 0.3f, "flight: fighter maintains forward airspeed along +Y");
	CePlayUnitFlightStep(&fl, fighter, 0.05f, 1.f, 0.f, 1.f, -0.6f, 0.f, true, -30.f, 0.f, vBoost);
	Require(fl.speedUps > (fighter.minSpeedUps + (fighter.maxSpeedUps - fighter.minSpeedUps) * fighter.cruiseThrottle),
	        "flight: throttle + boost increases airspeed");
	Require(fl.roll < -0.15f, "flight: right turn banks right (negative roll in BEA left-handed frame)");

	// Mouse-only right turn and A/D-only right turn MUST agree in roll sign (both negative = right wing down)
	{
		CePlayUnitFlightState fMouse = {}, fKey = {}, fLeft = {};
		CePlayUnitFlightReset(&fMouse, 0.f, fighter);
		CePlayUnitFlightReset(&fKey, 0.f, fighter);
		CePlayUnitFlightReset(&fLeft, 0.f, fighter);
		float vTmp[3];
		CePlayUnitFlightStep(&fMouse, fighter, 0.05f, 0.f, 0.f, 0.f, -0.5f, 0.f, false, -30.f, 0.f, vTmp);
		CePlayUnitFlightStep(&fKey,   fighter, 0.05f, 0.f, 0.f, 1.f,  0.0f, 0.f, false, -30.f, 0.f, vTmp);
		CePlayUnitFlightStep(&fLeft,  fighter, 0.05f, 0.f, 0.f, -1.f, 0.5f, 0.f, false, -30.f, 0.f, vTmp);
		Require(fMouse.roll < -0.15f, "flight: mouse right turn dips right wing (negative roll)");
		Require(fKey.roll < -0.20f, "flight: D-key right turn reinforces negative roll (no sign fight)");
		Require(fLeft.roll > 0.15f, "flight: left turn dips left wing (positive roll)");
	}

	// Sudden cliff terrain step does NOT teleport aircraft vertically in one tick
	{
		CePlayUnitFlightState fCliff = {};
		CePlayUnitFlightReset(&fCliff, 0.f, fighter);
		float vCliff[3];
		CePlayUnitFlightStep(&fCliff, fighter, 0.05f, 0.f, 0.f, 0.f, 0.f, 0.f, false, -10.f, -40.f, vCliff);
		Require(vCliff[2] < -0.5f && vCliff[2] >= -3.0f, "flight: cliff pull-up is strong yet bounded per tick (no teleport)");
	}

	// Diving into ground is stopped above minClearance
	float z = -10.f;
	for (int i = 0; i < 40; ++i)
	{
		float vDive[3];
		CePlayUnitFlightStep(&fl, fighter, 0.05f, 1.f, 0.f, 0.f, 0.f, 0.7f, false, z, 0.f, vDive);
		z += vDive[2];
	}
	Require(z <= -fighter.minClearance + 0.05f, "flight: altitude floor prevents accidental ground crash");
	Require(fl.pitch <= 0.05f, "flight: low clearance automatically levels or pulls nose up");

	// Dropship can decelerate to hover (0 speed)
	CePlayUnitFlightState ds = {};
	const CePlayUnitFlightParams drop = CePlayUnitDropshipParams();
	CePlayUnitFlightReset(&ds, 0.f, drop);
	float vDrop[3] = {0.f, 0.f, 0.f};
	for (int i = 0; i < 60; ++i)
		CePlayUnitFlightStep(&ds, drop, 0.05f, 0.f, 1.f, 0.f, 0.f, 0.f, false, -12.f, 0.f, vDrop);
	Require(ds.speedUps < 0.5f, "flight: dropship decelerates smoothly to hover");

	// Naval beach-unload state machine
	Require(CePlayUnitNavalEvaluateUnload(0, false, false, false, 0.f, 0.f) == CE_NAVAL_NO_CARGO, "naval: empty craft reports no cargo");
	Require(CePlayUnitNavalEvaluateUnload(4, false, false, false, 8.f, 0.f) == CE_NAVAL_AT_SEA, "naval: deep water reports at sea");
	Require(CePlayUnitNavalEvaluateUnload(4, false, false, false, 1.5f, 0.f) == CE_NAVAL_READY_UNLOAD, "naval: shallow shore reports ready to unload");
	Require(CePlayUnitNavalEvaluateUnload(4, true, true, false, -0.5f, 0.f) == CE_NAVAL_DEPLOYING, "naval: ramp animating reports deploying");
	Require(CePlayUnitNavalEvaluateUnload(4, true, true, true, -0.5f, 0.f) == CE_NAVAL_READY_UNLOAD, "naval: open ramp with cargo stays ready");
	Require(CePlayUnitNavalEvaluateUnload(0, true, true, true, -0.5f, 0.f) == CE_NAVAL_DEPLOYED, "naval: empty after landing reports deployed");

	// Dropship cargo-drop readiness
	Require(CePlayUnitDropshipEvaluateUnload(6, false, 45.f, -2.f, 0.f) == CE_NAVAL_AT_SEA, "dropship: high altitude not ready to drop");
	Require(CePlayUnitDropshipEvaluateUnload(6, false, 10.f, 5.f, 0.f) == CE_NAVAL_AT_SEA, "dropship: over water not ready to drop");
	Require(CePlayUnitDropshipEvaluateUnload(6, false, 10.f, -2.f, 0.f) == CE_NAVAL_READY_UNLOAD, "dropship: low over land ready to drop");
}

static void CheckFactionPalettesAndClassReadouts()
{
	// PlayUnit faction palette symmetry (Forseti cyan/slate vs Muspell fiery amber/crimson/iron)
	const CePlayUnitHudPalette forseti = CePlayUnitHudPaletteFor(0);
	const CePlayUnitHudPalette muspell = CePlayUnitHudPaletteFor(1);
	Require(forseti.panelFill != muspell.panelFill && forseti.accentBright != muspell.accentBright,
	        "hud palette: Forseti and Muspell palettes are distinct");
	Require((forseti.accentBright & 0xffu) > ((forseti.accentBright >> 16) & 0xffu),
	        "hud palette: Forseti accent is cool blue/cyan");
	Require(((muspell.accentBright >> 16) & 0xffu) > (muspell.accentBright & 0xffu) + 0x80u,
	        "hud palette: Muspell accent is fiery red/amber");
	Require(((muspell.arcMain >> 16) & 0xffu) > (muspell.arcMain & 0xffu) + 0x80u,
	        "hud palette: Muspell ballistic arc is fiery amber");

	// Commander RTS HUD palette symmetry
	const CeCmdHudPalette cmdF = CeCmdHudPaletteForAllegiance(0);
	const CeCmdHudPalette cmdM = CeCmdHudPaletteForAllegiance(1);
	Require(cmdF.cyanAccent != cmdM.cyanAccent && cmdF.barBorder != cmdM.barBorder,
	        "commander palette: Forseti and Muspell RTS bars are distinct");

	// Every class readout formats cleanly with zero forbidden bitmap-font glyphs
	const int classes[] = {
		CE_PLAYUNIT_CLASS_INFANTRY, CE_PLAYUNIT_CLASS_TANK, CE_PLAYUNIT_CLASS_TURRET,
		CE_PLAYUNIT_CLASS_AIRCRAFT, CE_PLAYUNIT_CLASS_NAVAL, CE_PLAYUNIT_CLASS_ARTILLERY
	};
	for (unsigned i = 0; i < sizeof(classes) / sizeof(classes[0]); ++i)
	{
		CePlayUnitHudTelemetry t = {};
		t.valid = true;
		t.classId = classes[i];
		t.allegiance = (i & 1) ? 1 : 0;
		t.hullPct = 85;
		t.headingDeg = 135;
		t.speedUps = 12.f;
		t.altitude = 32.f;
		t.pitchDeg = 10.f;
		t.azDeg = 210.f;
		t.elDeg = 38.f;
		t.rangeMeters = 240.f;
		t.arcOn = true;
		t.spotterMode = CE_SPOTTER_OVERWATCH;
		t.cargoCount = 4;
		t.unloadStatus = CE_NAVAL_READY_UNLOAD;
		t.stance = 1;
		std::snprintf(t.weaponName, sizeof(t.weaponName), "Pulse_Gun [A]|$");
		char h[48], l1[64], l2[64], act[48];
		CePlayUnitFormatClassHud(t, h, l1, l2, act);
		Require(h[0] != 0 && l1[0] != 0 && l2[0] != 0, "class hud: header and telemetry lines populated");
		const char *all[4] = {h, l1, l2, act};
		for (int k = 0; k < 4; ++k)
			for (const char *c = all[k]; *c; ++c)
				Require(!CePlayUnitCharForbidden(static_cast<unsigned char>(*c)),
				        "class hud: zero forbidden bitmap-font glyphs in formatted output");
		if (classes[i] == CE_PLAYUNIT_CLASS_INFANTRY)
			Require(std::strstr(l2, "HEALTH ") != NULL && std::strstr(l2, "HULL") == NULL,
			        "infantry hud: displays HEALTH rather than HULL");
		if (classes[i] == CE_PLAYUNIT_CLASS_NAVAL)
			Require(std::strstr(act, "UNLOAD") != NULL, "naval hud: ready landing craft shows UNLOAD prompt");
		if (classes[i] == CE_PLAYUNIT_CLASS_ARTILLERY)
			Require(std::strstr(l2, "ARC ON") != NULL && std::strstr(act, "SPOT CAM") != NULL,
			        "artillery hud: shows ARC ON and SPOT CAM prompt");
	}
	// Aircraft title reconciliation: Ground Attack Aircraft vs Bomber vs Fighter
	{
		CePlayUnitHudTelemetry gaa = {};
		gaa.valid = true; gaa.classId = CE_PLAYUNIT_CLASS_AIRCRAFT; gaa.isBomber = true; gaa.hullPct = 100;
		std::snprintf(gaa.unitName, sizeof(gaa.unitName), "Ground Attack Aircraft");
		char h[48], l1[64], l2[64], act[48];
		CePlayUnitFormatClassHud(gaa, h, l1, l2, act);
		Require(std::strcmp(h, "GROUND ATTACK") == 0, "aircraft hud: Ground Attack Aircraft header reconciles cleanly");
		std::snprintf(gaa.unitName, sizeof(gaa.unitName), "Heavy Bomber");
		CePlayUnitFormatClassHud(gaa, h, l1, l2, act);
		Require(std::strcmp(h, "BOMBER") == 0, "aircraft hud: actual bomber still shows BOMBER");
	}
	// Artillery max range display (RNG 70M / MAX 60M) so OUT OF RANGE is immediately self-explanatory
	{
		CePlayUnitHudTelemetry art = {};
		art.valid = true; art.classId = CE_PLAYUNIT_CLASS_ARTILLERY; art.hullPct = 100;
		art.rangeMeters = 70.f; art.maxRangeMeters = 60.f; art.arcOn = true;
		char h[48], l1[64], l2[64], act[48];
		CePlayUnitFormatClassHud(art, h, l1, l2, act);
		Require(std::strstr(l1, "RNG 70M / MAX 60M") != NULL, "artillery hud: shows RNG and MAX range together");
	}
	// Per-class compact reticle kind & 9-slice fixed corner radius + mirror symmetry
	{
		Require(CePlayUnitCompactReticleKind(CE_PLAYUNIT_CLASS_INFANTRY) == CE_PLAYUNIT_RETICLE_INFANTRY, "reticle: infantry compact crosshair");
		Require(CePlayUnitCompactReticleKind(CE_PLAYUNIT_CLASS_AIRCRAFT) == CE_PLAYUNIT_RETICLE_AIRCRAFT, "reticle: aircraft forward-aim horizon cue");
		Require(CePlayUnitCompactReticleKind(CE_PLAYUNIT_CLASS_ARTILLERY) == CE_PLAYUNIT_RETICLE_ARTILLERY, "reticle: artillery ballistic range reticle");
		Require(CePlayUnitCompactReticleKind(CE_PLAYUNIT_CLASS_TANK) == CE_PLAYUNIT_RETICLE_VEHICLE, "reticle: tank uses vehicle reticle");
		const CePlayUnitNineSlice nsA = CePlayUnitComputeNineSlice(40.f, 200.f, 320.f, 96.f, 21.f, 1.5f);
		const CePlayUnitNineSlice nsB = CePlayUnitComputeNineSlice(40.f, 200.f, 320.f, 220.f, 21.f, 1.5f);
		Require(nsA.valid && nsB.valid && Near(nsA.radius, 21.f) && Near(nsB.radius, 21.f), "9-slice: fixed radius never stretches vertically");
		Require(Near(nsB.railHeight, 220.f - 42.f), "9-slice: straight vertical rail spans tall panels");
		Require(Near(nsB.leftCenterX - nsB.x, (nsB.x + nsB.w) - nsB.rightCenterX), "9-slice: exact left/right symmetry");
	}
	// NaN/Inf/extreme telemetry values format safely without float-to-int UB under UBSan
	{
		CePlayUnitHudTelemetry bad = {};
		bad.valid = true;
		bad.classId = CE_PLAYUNIT_CLASS_ARTILLERY;
		bad.azDeg = std::numeric_limits<float>::quiet_NaN();
		bad.elDeg = std::numeric_limits<float>::infinity();
		bad.pitchDeg = -std::numeric_limits<float>::infinity();
		bad.speedUps = 1e20f;
		bad.altitude = -1e20f;
		bad.rangeMeters = std::numeric_limits<float>::quiet_NaN();
		char bh[48], bl1[64], bl2[64], bact[48];
		CePlayUnitFormatClassHud(bad, bh, bl1, bl2, bact);
		Require(std::strcmp(bh, "ARTILLERY") == 0 && bl1[0] != 0, "hud format: NaN/Inf artillery telemetry formats safely");
		bad.classId = CE_PLAYUNIT_CLASS_AIRCRAFT;
		CePlayUnitFormatClassHud(bad, bh, bl1, bl2, bact);
		Require(std::strcmp(bh, "AIRCRAFT") == 0 && bl1[0] != 0, "hud format: NaN/Inf aircraft telemetry formats safely");
	}
}

static void CheckMultiWeaponAndPromptCapacity()
{
	// Class-appropriate control verbs for take notice banner
	Require(std::strcmp(CePlayUnitControlVerb(CE_PLAYUNIT_CLASS_INFANTRY), "On foot") == 0, "verb: infantry is On foot");
	Require(std::strcmp(CePlayUnitControlVerb(CE_PLAYUNIT_CLASS_TANK), "Driving") == 0, "verb: tank is Driving");
	Require(std::strcmp(CePlayUnitControlVerb(CE_PLAYUNIT_CLASS_ARTILLERY), "Driving") == 0, "verb: artillery is Driving");
	Require(std::strcmp(CePlayUnitControlVerb(CE_PLAYUNIT_CLASS_TURRET), "Operating") == 0, "verb: turret is Operating");
	Require(std::strcmp(CePlayUnitControlVerb(CE_PLAYUNIT_CLASS_AIRCRAFT), "Flying") == 0, "verb: aircraft is Flying");
	Require(std::strcmp(CePlayUnitControlVerb(CE_PLAYUNIT_CLASS_NAVAL), "Piloting") == 0, "verb: naval is Piloting");
	Require(std::strcmp(CePlayUnitControlVerb(99), "Controlling") == 0, "verb: unknown class falls back to Controlling");

	// Standing prompt must hold at least 5 pieces (unit + Return + Zoom + View + C: Spot / U: Unload).
	Require(kCePlayUnitPromptMaxPieces >= 6, "prompt: piece buffer holds >= 5 pieces without truncating class hints");

	// Multi-weapon Turret / Aircraft selection: recovering secondary never locks out a ready primary.
	const bool modes2[2] = {true, true};
	const bool readyPrimaryOnly[2] = {true, false};
	const bool readySecondaryOnly[2] = {false, true};
	const bool noneReady[2] = {false, false};
	Require(CePlayUnitSelectReadyWeapon(2, 1, false, modes2, readyPrimaryOnly) == 0,
	        "multi-weapon: switches from recovering secondary to ready primary");
	Require(CePlayUnitSelectReadyWeapon(2, 0, false, modes2, readySecondaryOnly) == 1,
	        "multi-weapon: switches from recovering primary to ready secondary");
	Require(CePlayUnitSelectReadyWeapon(2, 1, true, modes2, readyPrimaryOnly) == 1,
	        "multi-weapon: preserves current mount while actively arming");
	Require(CePlayUnitSelectReadyWeapon(2, 1, false, modes2, noneReady) == 1,
	        "multi-weapon: keeps current weapon when all mounts recovering");
	const bool modesFirstInvalid[2] = {false, true};
	Require(CePlayUnitSelectReadyWeapon(2, 0, false, modesFirstInvalid, noneReady) == 1,
	        "multi-weapon: skips mount without active weapon mode");
	Require(CePlayUnitSelectReadyWeapon(0, 0, false, modes2, readyPrimaryOnly) == -1,
	        "multi-weapon: empty weapon list returns -1");

	// Non-finite lookYaw must be refused cleanly by spotter camera pose solver.
	{
		float oe[3], orv[3], ofv[3], odv[3];
		const float up[3] = {0.f, 0.f, 0.f};
		CePlayUnitSpotterState ov;
		CePlayUnitSpotterReset(&ov);
		CePlayUnitSpotterToggle(&ov, false, NULL);
		Require(!CePlayUnitSpotterCameraPose(ov, up, std::numeric_limits<float>::quiet_NaN(), 0.f, oe, orv, ofv, odv),
		        "spotter: non-finite lookYaw refused");
	}

	// Telemetry card layout must stay strictly left of central circular compass ring (including right crescent overhang)
	// at BOTH 1280x720 and 640x480 even for the longest weapon names (Pulse Torpedo Launcher, Plasma Grenade Thrown).
	const struct { float w, h, s; } modes[2] = {{1280.f, 720.f, 1.5f}, {640.f, 480.f, 1.0f}};
	const char *longWeapons[2] = {"Pulse Torpedo Launcher", "Plasma Grenade Thrown"};
	for (int mi = 0; mi < 2; ++mi)
	{
		const float compassLeft = modes[mi].w * 0.5f - 108.f * modes[mi].s;
		for (int wi = 0; wi < 2; ++wi)
		{
			const bool isAir = (wi == 0);
			CePlayUnitHudTelemetry tel;
			std::memset(&tel, 0, sizeof(tel));
			tel.valid = true;
			tel.classId = isAir ? CE_PLAYUNIT_CLASS_AIRCRAFT : CE_PLAYUNIT_CLASS_INFANTRY;
			tel.isBomber = isAir;
			tel.hullPct = 100;
			tel.speedUps = 66.f;
			tel.altitude = 23.f;
			tel.pitchDeg = -7.f;
			tel.headingDeg = 163.f;
			std::snprintf(tel.weaponName, sizeof(tel.weaponName), "%s", longWeapons[wi]);
			char hdr[48], l1[64], l2[64], act[48];
			CePlayUnitFormatClassHud(tel, hdr, l1, l2, act);
			// Generous worst-case character width (~8px * scale per glyph)
			const float maxChars = float(std::strlen(l1) > std::strlen(l2) ? std::strlen(l1) : std::strlen(l2));
			const float desiredTextW = maxChars * 8.0f * modes[mi].s;
			CePlayUnitClassCardLayout card;
			Require(CePlayUnitComputeClassCardLayout(modes[mi].w, modes[mi].h, modes[mi].s, desiredTextW, isAir, &card),
			        "class card layout: succeeds at 1280x720 and 640x480");
			Require(card.x0 >= modes[mi].w * 0.03f,
			        "class card layout: enforces 3-4 percent left safe-area margin (zero edge clipping)");
			Require(card.x0 + card.totalW < compassLeft - 8.f * modes[mi].s,
			        "class card layout: right crescent edge strictly clear of central compass ring");
			Require(card.bodyLeft + card.bodyW <= card.x0 + card.totalW + 0.5f,
			        "class card layout: inner body contained within total card bounds");
			Require(card.textMaxW >= 90.f * modes[mi].s,
			        "class card layout: usable text area remains readable");
		}
	}
}

int main()
{
	CheckAllClassEligibility();
	CheckFirstPersonPartsAndWeaponView();
	CheckBallisticArcAndSpotter();
	CheckAircraftFlightAndNavalUnload();
	CheckFactionPalettesAndClassReadouts();
	CheckMultiWeaponAndPromptCapacity();
	std::printf("checks=%d failures=0 skips=0\nPASS\n", gChecks);
	return 0;
}
'''

# Causal mutants proving each rule & safeguard is actively tested: (label, file_key, old, new)
MUTANTS = (
    ("barrel pass skips barrels too", "tactics",
     "return !CePlayUnitPartIsBarrel(partName);",
     "return true;"),
    ("normal pass skips hull parts", "tactics",
     "if (!barrelPassActive) return false;",
     "if (false) return false;"),
    ("ballistic gravity zeroed", "tactics",
     "const float v0 = roundSpeedPerSec / 20.f;\n\tconst float g = roundGravity * 0.025f;",
     "const float v0 = roundSpeedPerSec / 20.f;\n\tconst float g = 0.f;"),
    ("ballistic water impact ignored", "tactics",
     "const float surface = (isfinite(ground) && ground < waterLevel) ? ground : waterLevel;",
     "const float surface = ground;"),
    ("spotter fails to hold on impact", "tactics",
     "st->mode = CE_SPOTTER_HOLD_IMPACT;",
     "st->mode = CE_SPOTTER_OFF;"),
    ("spotter camera inverted 180 deg", "tactics",
     "float rx = outForward[1], ry = -outForward[0];",
     "float rx = -outForward[1], ry = outForward[0];"),
    ("turret control verb regressed to Driving", "tactics",
     'case CE_PLAYUNIT_CLASS_TURRET: return "Operating";',
     'case CE_PLAYUNIT_CLASS_TURRET: return "Driving";'),
    ("aircraft altitude floor disabled", "tactics",
     "if (clearance < p.minClearance + 4.f)",
     "if (false)"),
    ("naval deep sea falsely allows unload", "tactics",
     "if (isfinite(bowGroundZ) && isfinite(waterZ) && bowGroundZ <= waterZ + 2.4f)",
     "if (isfinite(bowGroundZ) && isfinite(waterZ))"),
    ("muspell hud falls back to forseti", "tactics",
     "if (allegiance == 1)\n\t{",
     "if (false)\n\t{"),
    ("unarmed dropship transport refused", "rules",
     "&& (f.armed || f.transport);",
     "&& f.armed;"),
    ("multi-weapon sticks on recovering secondary", "tactics",
     "for (int i = 0; i < count; ++i)\n\t\tif (hasMode[i] && ready[i])\n\t\t\treturn i;",
     "for (int i = 0; i < 0; ++i)\n\t\tif (hasMode[i] && ready[i])\n\t\t\treturn i;"),
    ("multi-weapon clobbers arming weapon", "tactics",
     "&& (ready[currentIdx] || armingCurrent)",
     "&& ready[currentIdx]"),
    ("prompt piece cap drops 5th class hint", "tactics",
     "static const int kCePlayUnitPromptMaxPieces = 6;",
     "static const int kCePlayUnitPromptMaxPieces = 4;"),
    ("class card compass clamp disabled", "tactics",
     "float maxRight = compassLeft - compassGap;",
     "float maxRight = w - 8.f;"),
    ("aircraft bank sign inverted", "tactics",
     "const float desiredRoll = CePlayUnitClamp(turnRatio * p.maxBank - trn * p.maxBank * 0.35f, -p.maxBank, p.maxBank);",
     "const float desiredRoll = CePlayUnitClamp(-turnRatio * p.maxBank - trn * p.maxBank * 0.35f, -p.maxBank, p.maxBank);"),
    ("aircraft cliff pull-up unbounded", "tactics",
     "if (needed < -maxClimbTick) needed = -maxClimbTick;",
     "if (false) needed = -maxClimbTick;"),
    ("ballistic out-of-range falsely returns true", "tactics",
     "if (!isfinite(ePeak) || ePeak > 0.05f)\n\t{\n\t\t*outPitch = CePlayUnitClamp(pPeak, minPitch, maxPitch);\n\t\treturn false;\n\t}",
     "if (!isfinite(ePeak) || ePeak > 0.05f)\n\t{\n\t\t*outPitch = CePlayUnitClamp(pPeak, minPitch, maxPitch);\n\t\treturn true;\n\t}"),
    ("spotter toggle leaks stale hasImpact", "tactics",
     "\tst->hasShell = false;\n\tst->hasImpact = false;",
     "\tst->hasShell = false;"),
    ("spotter camera ignores eye terrain hill", "tactics",
     "if (isfinite(terrainAtEye) && (!isfinite(eyeFloor) || terrainAtEye < eyeFloor))\n\t\t\teyeFloor = terrainAtEye;",
     "(void)terrainAtEye;"),
    ("infantry hud regresses to HULL", "tactics",
     'snprintf(line2Out, 64, "STANCE %s  HEALTH %d%%", stanceWord, hull);',
     'snprintf(line2Out, 64, "STANCE %s  HULL %d%%", stanceWord, hull);'),
    ("artillery max range omitted", "tactics",
     'snprintf(line1Out, 64, "AZ %03d  EL %+d  RNG %dM / MAX %dM", az, el, rng, maxRng);',
     'snprintf(line1Out, 64, "AZ %03d  EL %+d  RNG %dM", az, el, rng);'),
    ("class card safe margin dropped", "tactics",
     "const float safeLeft = floorf(w * 0.032f);",
     "const float safeLeft = 0.f;"),
)


def check_source_invariants() -> list[str]:
    errors: list[str] = []
    hud = PLAYUNIT_HUD_CPP.read_text()
    cpp = PLAYUNIT_CPP.read_text()
    p0358 = PATCH_0358.read_text()

    if "WCHAR piece[kCePlayUnitPromptMaxPieces][2][kChars];" not in hud or "pieces < kCePlayUnitPromptMaxPieces" not in hud:
        errors.append("ce_playunit_hud.cpp Prompt() must size piece[] via kCePlayUnitPromptMaxPieces (>= 6)")

    prompt_fn = cpp[cpp.index("void UnitPromptLine("):cpp.index("void RefreshUnitPrompt(")]
    if "return;" in prompt_fn:
        errors.append("UnitPromptLine() must not return early on gamepad path before appending C: Spot / U: Unload")
    for token in ("ClassKeyFree(DIK_C)", "ClassKeyFree(DIK_U)", " | C: Spot", " | U: Unload"):
        if token not in prompt_fn:
            errors.append(f"UnitPromptLine() missing {token!r}")

    net_fn = cpp[cpp.index("bool NetworkOrLockstepActive()"):cpp.index("CePlayUnitGate FillGate(")]
    for token in ("CE_BootRequested", "CE_LockstepRequested", "CE_SessionActive", "PortCeNetActive", '"BEA_LOCKSTEP_"', '"BEA_NET_"'):
        if token not in net_fn:
            errors.append(f"NetworkOrLockstepActive() missing {token!r}")
    fill_gate = cpp[cpp.index("CePlayUnitGate FillGate("):cpp.index("bool Allowed(")]
    if "NetworkOrLockstepActive()" not in fill_gate:
        errors.append("FillGate() must include NetworkOrLockstepActive() in networkBoot")
    invert_fn = cpp[cpp.index('extern "C" int PortCePlayUnitInvertSides('):cpp.index('extern "C" int PortCePlayUnitSkipsMeshPart(')]
    if "NetworkOrLockstepActive()" not in invert_fn:
        errors.append("PortCePlayUnitInvertSides() must refuse when NetworkOrLockstepActive()")

    unload_fn = cpp[cpp.index("bool HandleManualUnload("):cpp.index("bool UnitHasBallisticWeapon(")]
    for token in ("boat->Unknown83()", "ship->Unknown448120()", "ship->mDoorState = 0;", '"wingflat"'):
        if token not in unload_fn:
            errors.append(f"HandleManualUnload() missing post-unload cleanup {token!r}")
    naval_fn = cpp[cpp.index("void DriveNaval("):cpp.index("void DriveArtillery(")]
    if "TransportCargoCount(u) <= 0" not in naval_fn:
        errors.append("DriveNaval() must allow reversing off beach once cargo is empty")

    fire_fn = cpp[cpp.index("void FireTick("):cpp.index("bool UpdateInput(") if "bool UpdateInput(" in cpp else len(cpp)]
    for token in ("!w->mMode || !w->ReadyToFire()", "if (u->mUnknown168 == 1) break;", "u->mCurrentWeapon = nextReady ? nextReady : savedWeapon;"):
        if token not in fire_fn:
            errors.append(f"FireTick() multi-weapon loop missing {token!r}")

    barrel_fn = cpp[cpp.index("void DrawFirstPersonVehicleBarrel("):cpp.index("void DrawBallisticArcWorld(")]
    first_half = barrel_fn[:barrel_fn.index("if (gFirstPersonBarrelDrawn > 0) return;")]
    for token in ("CreateStateBlock(D3DSBT_ALL, &saved)", "RENDERINFO.SetLightingEnabled(FALSE);\n\t\t\tRENDERINFO.Apply();", "saved->Apply();", "saved->Release();"):
        if token not in first_half:
            errors.append(f"DrawFirstPersonVehicleBarrel() mesh pass missing {token!r}")

    render_fn = cpp[cpp.index("void CePlayUnitRenderState()"):cpp.index("void Refuse(")]
    for token in ("ClassKeyFree(DIK_U)", "ClassKeyFree(DIK_C)"):
        if token not in render_fn:
            errors.append(f"CePlayUnitRenderState() missing {token!r}")

    if "EmitCamBox(" in cpp:
        errors.append("ce_playunit.cpp must not use untextured EmitCamBox() geometry for 1P weapons")
    if '"You are driving: %s"' in cpp:
        errors.append("ce_playunit.cpp Take() must use class-appropriate CePlayUnitControlVerb(), not hardcoded You are driving")
    if "CePlayUnitComputeClassCardLayout(" not in hud:
        errors.append("ce_playunit_hud.cpp ClassPanel() must compute compass-safe bounds via CePlayUnitComputeClassCardLayout()")

    cannon_hunk = p0358[p0358.index("void CCannon::Move()"):p0358.index("--- a/Component.cpp")]
    if "if (anim != AM_INVALID)" not in cannon_hunk:
        errors.append("0358 CCannon::Move must guard GetAnimModeByName(\"Activate\") against AM_INVALID")

    return errors


def compile_and_run(cxx: str, files: dict[str, str], strict: bool) -> tuple[bool, bool, str]:
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        (d / "ce_playunit_rules.h").write_text(files["rules"])
        (d / "ce_playunit_tactics.h").write_text(files["tactics"])
        (d / "ce_cmd_hud.h").write_text(files["cmd_hud"])
        drv = d / "driver.cpp"
        drv.write_text(DRIVER)
        exe = d / "check"
        cmd = [
            cxx, "-std=c++17", "-O1", "-g", "-Wall", "-Wextra",
            *(["-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if strict else ["-Wno-unused-parameter"]),
            "-I", str(d), str(drv), "-o", str(exe),
        ]
        built = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if built.returncode != 0:
            return False, False, built.stderr[-800:]
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
        ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30, env=env)
        out = ran.stdout + ran.stderr
        ok = ran.returncode == 0 and "PASS" in ran.stdout and "Sanitizer" not in out and "runtime error:" not in out
        return True, ok, out[-800:]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cxx", default=shutil.which("c++") or "g++")
    args = ap.parse_args()

    files = {
        "rules": RULES_H.read_text(),
        "tactics": TACTICS_H.read_text(),
        "cmd_hud": CMD_HUD_H.read_text(),
    }
    ok = True
    comp, passed, out = compile_and_run(args.cxx, files, strict=True)
    print(f"{'PASS' if comp and passed else 'FAIL'} baseline all-classes PlayUnit checks (ASan/UBSan)")
    if not (comp and passed):
        print(out)
        return 1

    for label, key, old, new in MUTANTS:
        if files[key].count(old) != 1:
            print(f"FAIL mutant {label!r}: anchor found {files[key].count(old)} times in {key}")
            ok = False
            continue
        mut = dict(files)
        mut[key] = files[key].replace(old, new, 1)
        m_comp, m_pass, m_out = compile_and_run(args.cxx, mut, strict=False)
        if not m_comp:
            print(f"FAIL mutant {label!r}: compile failed\n{m_out}")
            ok = False
        elif m_pass:
            print(f"FAIL mutant {label!r}: driver unexpectedly passed")
            ok = False
        else:
            print(f"PASS mutant caught: {label}")

    src_errs = check_source_invariants()
    if src_errs:
        for e in src_errs:
            print(f"FAIL source invariant: {e}")
        ok = False
    else:
        print("PASS source invariants (prompt capacity, lockstep fence, post-unload cleanup, multi-weapon volley, D3D9 stateblock, ClassKeyFree, 0358 AM_INVALID)")

    print("playunit all-classes suite: " + ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
