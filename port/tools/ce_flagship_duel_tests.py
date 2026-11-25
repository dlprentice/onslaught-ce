#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Clean-Room Level 888 ("Flagship Duel") ASan/UBSan + mutant test suite.

Exercises the pure C++14 Flagship Duel headers & contracts:
  - port/game/ce_flagship_duel_world.h
  - port/game/ce_flagship_duel.h

Coverage:
  1. Version-50 binary world serializer & inspector for BOTH 1P Solo (level 788,
     worldType = 0, {"Standard"}, 1 CStart) and 2P Split-Screen (level 888,
     worldType = 2, {"Standard", "Laser", "Blaster"}, 2 CStarts + 4 CSpawnPoints),
     authentic Level 888 flagship placement (Venturer at (256, 420, -60) yaw pi,
     Fenrir at (256, 100, -60) yaw 0 — 320.0 m apart), and strict rejection of
     "Fenrir Multiplayer"
  2. Strict retail PC preflight matrix + complete 857_res_PC.aya + loose retail
     mesh/texture/support closure (boss-fenrir + all 7 sub-components + inner
     wreckage + Venturer + F24 + Wildcats + ATF + m_rocket + destroyed Wildcat)
  3. North-vs-South broadside patrol lanes, altitude stagger, 175 m AI separation
     guard, and 135 m hard hull-overlap kinematic barrier
  4. 1P Victory/Defeat + 2P P1Won/P2Won + 3-lives exhaustion sync + three-act
     escalation + hull hit-flash state machine
  5. 1-hour (108,000 ticks @ 30 Hz) handle churn & entity-cap soak in both 1P & 2P
  6. 1P top-center HUD + 2P split-seam HUD geometry & FONT_SMALL glyph safety
  7. 26 causal mutants across ce_flagship_duel_world.h and ce_flagship_duel.h
  8. Patch & source hygiene (#ifdef CE_VERSION guards, SPDX headers, Career fence,
     mod 888 non-interference, and public export cleanliness)
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "port" / "game"
PATCHES = ROOT / "port" / "patches"

DRIVER_SRC = r'''
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ce_flagship_duel.h"

namespace {

int gFailures = 0;

void Check(bool ok, const char *msg, int line) {
	if (!ok) {
		std::fprintf(stderr, "FAIL [line %d]: %s\n", line, msg);
		++gFailures;
	}
}
#define CHECK(cond, msg) Check((cond), (msg), __LINE__)

void TestWorldSerializer1PAnd2P() {
	using namespace CeFlagshipDuelWorld;

	CHECK(kOidUnit == 8u && kOidStart == 15u && kOidSpawnPoint == 41u,
	      "OIDs match OID_CUnit=8, OID_CStart=15, OID_CSpawnPoint=41");

	// 1. 1P Solo mode (level 788, worldType = 0).
	const CeFdWorldPlan plan1P = CeFlagshipDuelBuildWorldPlan(CeFlagshipDuelMode::kSolo1P);
	CHECK(plan1P.levelNumber == kSoloLevel && kSoloLevel == 788, "1P uses level 788 outside 850..899");
	CHECK(plan1P.worldType == kWorldTypeSinglePlayer, "1P worldType is kSinglePlayer (0)");
	CHECK(plan1P.beConfigCount == 1u && std::string(plan1P.beConfigs[0]) == "Standard", "1P BE config is Standard");

	uint8_t base1P[kMaxWorldBodyBytes] = {};
	unsigned base1PLen = 0u;
	CHECK(CeFlagshipDuelSerializeWorld(plan1P, false, base1P, sizeof(base1P), &base1PLen),
	      "1P base world serializes cleanly");
	const CeFdWorldSummary baseSum1P = CeFlagshipDuelInspectWorld(base1P, base1PLen, false);
	CHECK(baseSum1P.valid && baseSum1P.baseLevel == -1 && baseSum1P.thingCount == 0u,
	      "1P base world has baseLevel=-1 and 0 things");

	uint8_t bytes1P[kMaxWorldBodyBytes] = {};
	unsigned bytes1PLen = 0u;
	CHECK(CeFlagshipDuelSerializeWorld(plan1P, true, bytes1P, sizeof(bytes1P), &bytes1PLen),
	      "1P real world serializes cleanly");
	const CeFdWorldSummary sum1P = CeFlagshipDuelInspectWorld(bytes1P, bytes1PLen, true);
	CHECK(sum1P.valid, "1P serialized world passes binary self-inspection");
	CHECK(sum1P.worldVersion == 50u && sum1P.headerVersion == 3u, "1P world version 50 header 3");
	CHECK(sum1P.levelNumber == 788u, "1P header level is 788");
	CHECK(sum1P.worldType == 0, "1P serialized worldType is 0");
	CHECK(sum1P.mapNumber == 6 && sum1P.baseLevel == 10, "1P donor mapNumber=6 baseLevel=10");
	CHECK(sum1P.startCount == 1u && sum1P.sawPlayer1Start && !sum1P.sawPlayer2Start, "1P has exactly P1 CStart");
	CHECK(sum1P.hasVenturer && sum1P.hasFenrir && !sum1P.hasForbiddenFenrirMp, "1P has Venturer and Fenrir, no Fenrir MP");

	uint8_t stub[336] = {};
	unsigned stubLen = 0u;
	CHECK(BuildMeshStubChunk("boss-fenrir.msh", 944, true, stub, sizeof(stub), &stubLen) &&
	      stubLen == 325u && stub[324] == 1u,
	      "BuildMeshStubChunk writes 325-byte PMSH/PMS2 header with keepBuckets=1");
	char texDisk[256] = {};
	CHECK(BuildTextureDiskPath("meshtex\\BOSSfenrir-4PAGE.tga", texDisk, sizeof(texDisk)) &&
	      std::string(texDisk) == "data\\resources\\dxtntextures\\meshtex%BOSSfenrir-4PAGE.tga(0)A1R5G5B5.aya",
	      "BuildTextureDiskPath uses retail '%' separator");

	// Authentic Level 888 coordinates: (256, 420, -60) yaw pi vs (256, 100, -60) yaw 0 -> 320 m apart.
	CHECK(std::fabs(sum1P.venturerX - 256.0f) < 1e-4f &&
	      std::fabs(sum1P.venturerY - 420.0f) < 1e-4f &&
	      std::fabs(sum1P.venturerZ - (-60.0f)) < 1e-4f &&
	      std::fabs(sum1P.venturerYaw - kPi) < 1e-4f,
	      "1P Venturer at authentic (256, 420, -60) yaw pi");
	CHECK(std::fabs(sum1P.fenrirX - 256.0f) < 1e-4f &&
	      std::fabs(sum1P.fenrirY - 100.0f) < 1e-4f &&
	      std::fabs(sum1P.fenrirZ - (-60.0f)) < 1e-4f &&
	      std::fabs(sum1P.fenrirYaw - 0.0f) < 1e-4f,
	      "1P Fenrir at authentic (256, 100, -60) yaw 0");
	const float sep1P = std::hypot(sum1P.venturerX - sum1P.fenrirX, sum1P.venturerY - sum1P.fenrirY);
	CHECK(std::fabs(sep1P - 320.0f) < 0.01f, "opening flagship separation is 320.0 m");

	// 2. 2P Split-Screen mode (level 888, worldType = 2).
	const CeFdWorldPlan plan2P = CeFlagshipDuelBuildWorldPlan(CeFlagshipDuelMode::kSplitScreen2P);
	CHECK(plan2P.levelNumber == kSplitLevel && kSplitLevel == 888, "2P uses authentic level 888");
	CHECK(plan2P.worldType == kWorldTypeVersusMultiplayer, "2P worldType is kVersusMultiplayer (2)");
	CHECK(plan2P.beConfigCount == 3u &&
	      std::string(plan2P.beConfigs[0]) == "Standard" &&
	      std::string(plan2P.beConfigs[1]) == "Laser" &&
	      std::string(plan2P.beConfigs[2]) == "Blaster",
	      "2P BE configs match worldheaders.dat record 888 order (Standard, Laser, Blaster)");

	uint8_t bytes2P[kMaxWorldBodyBytes] = {};
	unsigned bytes2PLen = 0u;
	CHECK(CeFlagshipDuelSerializeWorld(plan2P, true, bytes2P, sizeof(bytes2P), &bytes2PLen),
	      "2P real world serializes cleanly");
	const CeFdWorldSummary sum2P = CeFlagshipDuelInspectWorld(bytes2P, bytes2PLen);
	CHECK(sum2P.valid, "2P serialized world passes binary self-inspection");
	CHECK(sum2P.levelNumber == 888u && sum2P.worldType == 2, "2P header level=888 worldType=2");
	CHECK(sum2P.startCount == 2u && sum2P.sawPlayer1Start && sum2P.sawPlayer2Start, "2P has both P1 and P2 CStarts");
	CHECK(sum2P.spawnPointCount == 4u, "2P has 4 CSpawnPoint respawn anchors");
	CHECK(std::fabs(sum2P.venturerY - 420.0f) < 1e-4f && std::fabs(sum2P.fenrirY - 100.0f) < 1e-4f,
	      "2P preserves 320 m North-vs-South flagship placement");

	// Truncated world must fail inspection cleanly.
	const CeFdWorldSummary trunc = CeFlagshipDuelInspectWorld(bytes2P, bytes2PLen - 6u);
	CHECK(!trunc.valid, "truncated world rejected by inspector");

	// Injecting "Fenrir Multiplayer" into a plan must be rejected by inspector.
	CeFdWorldPlan badPlan = plan2P;
	for (size_t i = 0; i < badPlan.actorCount; ++i) {
		if (badPlan.actors[i].role == kRoleFenrir) badPlan.actors[i].definition = "Fenrir Multiplayer";
	}
	uint8_t badBytes[kMaxWorldBodyBytes] = {};
	unsigned badBytesLen = 0u;
	CHECK(CeFlagshipDuelSerializeWorld(badPlan, true, badBytes, sizeof(badBytes), &badBytesLen),
	      "bad plan serializes for inspector check");
	const CeFdWorldSummary badSum = CeFlagshipDuelInspectWorld(badBytes, badBytesLen);
	CHECK(!badSum.valid && badSum.hasForbiddenFenrirMp, "inspector rejects Fenrir Multiplayer");
}

void TestPreflightAndAssetClosure() {
	using namespace CeFlagshipDuelWorld;

	CHECK(std::string(kDonorArchivePath) == "data\\resources\\857_res_PC.aya", "donor archive is 857_res_PC.aya");
	CHECK(kCeFdRequiredMeshCount == 17u, "17 required flagship/subcomponent/escort meshes");
	CHECK(kCeFdRequiredTextureCount == 10u, "10 required flagship/escort textures");
	CHECK(kCeFdExpectedDonorBeMeshCount == 6u, "6 expected Battle Engine + cockpit meshes in 857");
	CHECK(kCeFdSupportMeshCount == 2u, "2 support meshes (m_rocket + destroyed Wildcat)");

	auto hasMesh = [](const char *name) {
		for (size_t i = 0; i < kCeFdRequiredMeshCount; ++i) {
			if (std::strcmp(kCeFdPreloadMeshes[i], name) == 0) return true;
		}
		for (size_t i = 0; i < kCeFdSupportMeshCount; ++i) {
			if (std::strcmp(kCeFdSupportMeshes[i].mesh, name) == 0) return true;
		}
		return false;
	};
	CHECK(hasMesh("f_ventura.msh") && hasMesh("f_ventura_hpad.msh") &&
	      hasMesh("f_ventura_gun.msh") && hasMesh("fa_f24.msh"), "Venturer mesh closure complete");
	CHECK(hasMesh("boss-fenrir.msh") && hasMesh("m_fbossgt.msh") && hasMesh("m_fbossmg.msh") &&
	      hasMesh("m_fbosspe.msh") && hasMesh("m_fbossbe.msh") && hasMesh("m_fbossme.msh") &&
	      hasMesh("m_fbossmea.msh") && hasMesh("m_fbossbd.msh") && hasMesh("m_innerwreckage.msh"),
	      "Fenrir + all 7 sub-components + inner wreckage mesh closure complete");
	CHECK(hasMesh("m_atf.msh") && hasMesh("fa_wildcard_j.msh") &&
	      hasMesh("fa_wildcard_b.msh") && hasMesh("fa_wildcard_t.msh") &&
	      hasMesh("m_rocket.msh") && hasMesh("fa_wildcard_t_destroyed.msh"),
	      "fighter + rocket + debris mesh closure complete");

	CeFdPreflightInput base{true, false, true, true, true, true, false};
	const CeFdPreflightVerdict ok = CeFlagshipDuelEvaluatePreflight(base);
	CHECK(ok.allowed && ok.status == CeFdPreflightStatus::kOk, "clean retail input allowed");

	{
		CeFdPreflightInput m = base;
		m.ceMenusEnabled = false;
		const auto v = CeFlagshipDuelEvaluatePreflight(m);
		CHECK(!v.allowed && v.status == CeFdPreflightStatus::kMenusOff && v.message[0] == '\0',
		      "menus off refused silently");
	}
	{
		CeFdPreflightInput m = base;
		m.anotherSessionOpen = true;
		const auto v = CeFlagshipDuelEvaluatePreflight(m);
		CHECK(!v.allowed && v.status == CeFdPreflightStatus::kAnotherSessionOpen &&
		      CeFlagshipDuelIsFontSmallSafeText(v.message),
		      "active session conflict refused with safe message");
	}
	{
		CeFdPreflightInput m = base;
		m.usesFenrirMultiplayer = true;
		const auto v = CeFlagshipDuelEvaluatePreflight(m);
		CHECK(!v.allowed && v.status == CeFdPreflightStatus::kForbiddenFenrirMultiplayer &&
		      CeFlagshipDuelIsFontSmallSafeText(v.message),
		      "Fenrir Multiplayer explicitly refused");
	}
	{
		CeFdPreflightInput m = base;
		m.hasDonorArchive857 = false;
		const auto v = CeFlagshipDuelEvaluatePreflight(m);
		CHECK(!v.allowed && v.status == CeFdPreflightStatus::kMissingDonorArchive &&
		      CeFlagshipDuelIsFontSmallSafeText(v.message),
		      "missing 857_res_PC.aya refused");
	}
	{
		CeFdPreflightInput m = base;
		m.hasAllRequiredMeshes = false;
		const auto v = CeFlagshipDuelEvaluatePreflight(m);
		CHECK(!v.allowed && v.status == CeFdPreflightStatus::kMissingCarrierMesh &&
		      CeFlagshipDuelIsFontSmallSafeText(v.message),
		      "missing retail mesh refused");
	}
	{
		CeFdPreflightInput m = base;
		m.hasAllRequiredTextures = false;
		const auto v = CeFlagshipDuelEvaluatePreflight(m);
		CHECK(!v.allowed && v.status == CeFdPreflightStatus::kMissingCarrierTexture &&
		      CeFlagshipDuelIsFontSmallSafeText(v.message),
		      "missing retail texture refused");
	}
	{
		CeFdPreflightInput m = base;
		m.hasAllRequiredUnitDefs = false;
		const auto v = CeFlagshipDuelEvaluatePreflight(m);
		CHECK(!v.allowed && v.status == CeFdPreflightStatus::kMissingUnitDefinition &&
		      CeFlagshipDuelIsFontSmallSafeText(v.message),
		      "missing unit definition refused");
	}
}

void TestNorthSouthPatrolAndSeparationGuard() {
	CHECK(kCeFdVenturerPatrolCount == kCeFdFenrirPatrolCount && kCeFdVenturerPatrolCount >= 4u,
	      "matching multi-leg patrol loops");
	for (size_t i = 0; i < kCeFdVenturerPatrolCount; ++i) {
		const CeFdPatrolPoint &vp = kCeFdVenturerPatrol[i];
		const CeFdPatrolPoint &fp = kCeFdFenrirPatrol[i];
		CHECK(vp.y >= 360.0f && fp.y <= 160.0f, "Venturer stays North, Fenrir stays South");
		const float legDist = std::hypot(vp.x - fp.x, vp.y - fp.y);
		CHECK(legDist >= 210.0f, "corresponding patrol waypoints maintain >= 210 m separation");
		CHECK(std::fabs(vp.z - fp.z) >= 2.0f || i == 0u,
		      "patrol legs maintain vertical stagger after opening station");
	}

	CeFlagshipDuelDirector dir;
	CeFlagshipDuelConfig cfg{};
	cfg.mode = CeFlagshipDuelMode::kSplitScreen2P;
	dir.Reset(cfg);
	dir.BeginMatch(0x00010001u, 0x00010002u);

	// Safe 320 m distance must not trigger guard.
	CHECK(!dir.UpdateFlagshipPositions(256.0f, 420.0f, -60.0f, 256.0f, 100.0f, -60.0f),
	      "320 m opening stations do not trigger separation guard");

	// Closing to 140 m (< 175 m) triggers North/South retreat redirect.
	CHECK(dir.UpdateFlagshipPositions(256.0f, 330.0f, -60.0f, 256.0f, 190.0f, -60.0f),
	      "140 m approach triggers separation guard");
	CeFdPatrolPoint vr{}, fr{};
	CHECK(dir.ConsumeSeparationRedirect(&vr, &fr), "redirect consumed once");
	CHECK(!dir.ConsumeSeparationRedirect(&vr, &fr), "second consume returns false");
	CHECK(vr.y > 330.0f && fr.y < 190.0f && (vr.y - fr.y) >= 220.0f,
	      "redirect pushes Venturer North and Fenrir South");

	// Cooldown blocks re-trigger immediately, then clears after 3.1s.
	CHECK(!dir.UpdateFlagshipPositions(256.0f, 330.0f, -60.0f, 256.0f, 190.0f, -60.0f),
	      "separation guard respects cooldown");
	for (int i = 0; i < 16; ++i)
		(void)dir.Tick(0.20f, 10u);
	CHECK(dir.UpdateFlagshipPositions(256.0f, 330.0f, -60.0f, 256.0f, 190.0f, -60.0f),
	      "separation guard re-arms after cooldown");
	(void)dir.ConsumeSeparationRedirect(&vr, &fr);

	// Crossed-through approach (vy < fy) pushes each ship further along its current vector instead of back through!
	for (int i = 0; i < 16; ++i)
		(void)dir.Tick(0.20f, 10u);
	CHECK(dir.UpdateFlagshipPositions(256.0f, 210.0f, -60.0f, 256.0f, 290.0f, -60.0f),
	      "crossed-through approach triggers guard");
	CHECK(dir.ConsumeSeparationRedirect(&vr, &fr), "crossed-through redirect consumed");
	CHECK(vr.y < 210.0f && fr.y > 290.0f,
	      "crossed-through redirect separates outward without crossing hulls back through");

	// Hard hull-overlap kinematic barrier (< 135 m).
	const CeFdSeparationPush safePush = CeFlagshipDuelComputeSeparationPush(256.0f, 420.0f, 256.0f, 100.0f);
	CHECK(!safePush.active, "320 m does not activate kinematic push");

	const CeFdSeparationPush closePush = CeFlagshipDuelComputeSeparationPush(256.0f, 295.0f, 256.0f, 225.0f);
	CHECK(closePush.active, "70 m overlap risk activates kinematic push");
	const float afterDist = std::hypot(closePush.venturerNewX - closePush.fenrirNewX,
	                                   closePush.venturerNewY - closePush.fenrirNewY);
	CHECK(afterDist > closePush.distance2D, "kinematic push strictly increases separation");
	CHECK(std::hypot(closePush.venturerNewX - 256.0f, closePush.venturerNewY - 295.0f) <= kCeFdMaxPushStepPerTick + 1e-4f,
	      "per-tick push is smoothly capped at kCeFdMaxPushStepPerTick");
}

void TestSoloAndSplitScreenOutcomeStateMachines() {
	const CeEntityHandle kVenturer = 0x00010010u;
	const CeEntityHandle kFenrir = 0x00010011u;

	// 1. 1P Victory: Fenrir destroyed -> kVictoryHold -> declareSoloWon, Venturer loss ignored during hold.
	{
		CeFlagshipDuelDirector dir;
		CeFlagshipDuelConfig cfg{};
		cfg.mode = CeFlagshipDuelMode::kSolo1P;
		cfg.soloVictoryHoldSec = 1.0f;
		dir.Reset(cfg);
		dir.BeginMatch(kVenturer, kFenrir);

		dir.OnEntityDestroyed(kFenrir);
		CHECK(dir.Phase() == CeFlagshipDuelPhase::kVictoryHold && dir.ShouldShieldWinningFlagships(),
		      "1P Fenrir kill enters kVictoryHold and shields surviving flagship");
		dir.OnEntityDestroyed(kVenturer);
		CHECK(dir.Phase() == CeFlagshipDuelPhase::kVictoryHold,
		      "1P VictoryHold cannot be overwritten by subsequent Venturer loss");

		bool sawWon = false;
		for (int i = 0; i < 6; ++i) {
			const auto d = dir.Tick(0.25f, 8u);
			if (d.declareSoloWon) sawWon = true;
			CHECK(!d.declareSoloLost && !d.declareMpPlayer1Won && !d.declareMpPlayer2Won,
			      "1P Victory never fires defeat or MP win");
		}
		CHECK(sawWon && dir.Phase() == CeFlagshipDuelPhase::kFinishedVictory,
		      "1P VictoryHold transitions cleanly to kFinishedVictory");
	}

	// 2. 1P Defeat: Venturer destroyed -> kDefeatHold -> declareSoloLost.
	{
		CeFlagshipDuelDirector dir;
		CeFlagshipDuelConfig cfg{};
		cfg.mode = CeFlagshipDuelMode::kSolo1P;
		cfg.soloDefeatHoldSec = 0.5f;
		dir.Reset(cfg);
		dir.BeginMatch(kVenturer, kFenrir);

		dir.OnEntityDestroyed(kVenturer);
		CHECK(dir.Phase() == CeFlagshipDuelPhase::kDefeatHold, "1P Venturer loss enters kDefeatHold");
		bool sawLost = false;
		for (int i = 0; i < 4; ++i) {
			const auto d = dir.Tick(0.20f, 8u);
			if (d.declareSoloLost) sawLost = true;
		}
		CHECK(sawLost && dir.Phase() == CeFlagshipDuelPhase::kFinishedDefeat,
		      "1P DefeatHold transitions cleanly to kFinishedDefeat");
	}

	// 3. 2P Split-Screen: Fenrir destroyed -> MPDeclarePlayerWon(1); 3 initial lives each!
	{
		CeFlagshipDuelDirector dir;
		CeFlagshipDuelConfig cfg{};
		cfg.mode = CeFlagshipDuelMode::kSplitScreen2P;
		cfg.splitOutcomeHoldSec = 0.75f;
		dir.Reset(cfg);
		CHECK(dir.Player1Lives() == 3 && dir.Player2Lives() == 3, "2P starts with 3 lives per player");
		dir.BeginMatch(kVenturer, kFenrir);

		dir.OnEntityDestroyed(kFenrir);
		CHECK(dir.Phase() == CeFlagshipDuelPhase::kVictoryHold, "2P Fenrir kill enters hold");
		bool sawP1Won = false;
		for (int i = 0; i < 5; ++i) {
			const auto d = dir.Tick(0.20f, 8u);
			if (d.declareMpPlayer1Won) sawP1Won = true;
			CHECK(!d.declareSoloWon && !d.declareSoloLost && !d.declareMpPlayer2Won,
			      "2P Fenrir kill fires only MPDeclarePlayerWon(1)");
		}
		CHECK(sawP1Won && dir.Phase() == CeFlagshipDuelPhase::kFinishedP1Won,
		      "2P Fenrir kill finishes as kFinishedP1Won");
	}

	// 4. 2P Split-Screen: Venturer destroyed -> MPDeclarePlayerWon(2).
	{
		CeFlagshipDuelDirector dir;
		CeFlagshipDuelConfig cfg{};
		cfg.mode = CeFlagshipDuelMode::kSplitScreen2P;
		cfg.splitOutcomeHoldSec = 0.75f;
		dir.Reset(cfg);
		dir.BeginMatch(kVenturer, kFenrir);

		dir.OnEntityDestroyed(kVenturer);
		bool sawP2Won = false;
		for (int i = 0; i < 5; ++i) {
			const auto d = dir.Tick(0.20f, 8u);
			if (d.declareMpPlayer2Won) sawP2Won = true;
			CHECK(!d.declareSoloWon && !d.declareSoloLost && !d.declareMpPlayer1Won,
			      "2P Venturer loss fires only MPDeclarePlayerWon(2)");
		}
		CHECK(sawP2Won && dir.Phase() == CeFlagshipDuelPhase::kFinishedP2Won,
		      "2P Venturer loss finishes as kFinishedP2Won");
	}

	// 5. 2P External lives-exhaustion sync + Three-Act escalation + hull flash.
	{
		CeFlagshipDuelDirector dir;
		CeFlagshipDuelConfig cfg{};
		cfg.mode = CeFlagshipDuelMode::kSplitScreen2P;
		cfg.maxAliveEscortsPerSide = 4u;
		dir.Reset(cfg);
		dir.BeginMatch(kVenturer, kFenrir);

		CHECK(dir.CurrentAct() == CeFlagshipDuelAct::kActI_OpeningSalvo, "starts in Act I");
		CHECK(dir.ActEscortCap() == 3u, "Act I holds 1 escort slot in reserve");

		dir.UpdateFlagshipHealth(0.95f, 0.62f);
		CHECK(dir.CurrentAct() == CeFlagshipDuelAct::kActII_BroadsideExchange, "Act II at <= 65% hull");
		CHECK(dir.FenrirFlashSec() > 0.0f && dir.VenturerFlashSec() > 0.0f, "hull drop >= 2% triggers flash");
		CHECK(dir.ActEscortCap() == 4u, "Act II unlocks full escort cap");

		// Healing cannot regress Act II back to Act I.
		dir.UpdateFlagshipHealth(1.0f, 0.90f);
		CHECK(dir.CurrentAct() == CeFlagshipDuelAct::kActII_BroadsideExchange, "Act progression is monotonic");

		dir.UpdateFlagshipHealth(0.25f, 0.80f);
		CHECK(dir.CurrentAct() == CeFlagshipDuelAct::kActIII_FinalStand && dir.IsVenturerInDanger(),
		      "Act III + Venturer danger pulse at <= 30% hull");

		// External 3-lives exhaustion in CGame transitions to kFinishedP2Won cleanly.
		dir.SyncExternalMultiplayerOutcome(false, true);
		CHECK(dir.Phase() == CeFlagshipDuelPhase::kFinishedP2Won,
		      "external MP P2 win syncs director to kFinishedP2Won");
	}
}

void TestOneHourSoakBothModes() {
	for (CeFlagshipDuelMode mode : {CeFlagshipDuelMode::kSolo1P, CeFlagshipDuelMode::kSplitScreen2P}) {
		CeFlagshipDuelDirector dir;
		CeFlagshipDuelConfig cfg{};
		cfg.mode = mode;
		cfg.escortWaveIntervalSec = 15.0f;
		cfg.maxAliveEscortsPerSide = (mode == CeFlagshipDuelMode::kSplitScreen2P) ? 3u : 4u;
		cfg.maxTotalUnitsInWorld = 24u;
		dir.Reset(cfg);
		dir.BeginMatch(0x00010001u, 0x00010002u);

		uint32_t nextGen = 2u;
		uint32_t simulatedExtraUnits = (mode == CeFlagshipDuelMode::kSplitScreen2P) ? 4u : 3u;
		std::vector<CeEntityHandle> liveF, liveM;

		const float dt = 1.0f / 30.0f;
		for (uint32_t tick = 0; tick < 108000u; ++tick) {
			if (tick % 240u == 120u && !liveF.empty()) {
				dir.OnEntityDestroyed(liveF.back());
				liveF.pop_back();
			}
			if (tick % 240u == 180u && !liveM.empty()) {
				dir.OnEntityDestroyed(liveM.back());
				liveM.pop_back();
			}

			const uint32_t worldCount = simulatedExtraUnits +
				uint32_t(liveF.size()) + uint32_t(liveM.size());
			const auto d = dir.Tick(dt, worldCount);
			CHECK(worldCount + d.spawnForsetiEscorts + d.spawnMuspellEscorts <= cfg.maxTotalUnitsInWorld,
			      "soak never exceeds maxTotalUnitsInWorld");

			for (uint32_t i = 0; i < d.spawnForsetiEscorts; ++i) {
				const CeEntityHandle h = (++nextGen << 16) | 0x0010u;
				if (dir.RegisterEscort(0, h)) liveF.push_back(h);
			}
			for (uint32_t i = 0; i < d.spawnMuspellEscorts; ++i) {
				const CeEntityHandle h = (++nextGen << 16) | 0x0020u;
				if (dir.RegisterEscort(1, h)) liveM.push_back(h);
			}

			CHECK(dir.AliveEscortCount(0) <= cfg.maxAliveEscortsPerSide, "Forseti escort cap respected");
			CHECK(dir.AliveEscortCount(1) <= cfg.maxAliveEscortsPerSide, "Muspell escort cap respected");
		}
		CHECK(dir.WavesSpawned() >= 200u, "1-hour soak sustained continuous escort waves");
	}
}

void TestHudLayoutsAndGlyphSafety() {
	// Glyph safety across all 1P and 2P titles/subtitles.
	CHECK(!CeFlagshipDuelIsFontSmallSafeChar('_') && !CeFlagshipDuelIsFontSmallSafeChar('[') &&
	      !CeFlagshipDuelIsFontSmallSafeChar('~') && !CeFlagshipDuelIsFontSmallSafeChar(';'),
	      "glyph validator rejects unsupported FONT_SMALL punctuation");

	for (bool split2P : {false, true}) {
		for (CeFlagshipDuelPhase p : {
			CeFlagshipDuelPhase::kVictoryHold,
			CeFlagshipDuelPhase::kFinishedVictory,
			CeFlagshipDuelPhase::kFinishedP1Won,
			CeFlagshipDuelPhase::kDefeatHold,
			CeFlagshipDuelPhase::kFinishedDefeat,
			CeFlagshipDuelPhase::kFinishedP2Won}) {
			CHECK(CeFlagshipDuelIsFontSmallSafeText(CeFlagshipDuelBannerTitle(p, split2P)),
			      "banner title is FONT_SMALL safe");
			CHECK(CeFlagshipDuelIsFontSmallSafeText(CeFlagshipDuelBannerSubtitle(p, split2P)),
			      "banner subtitle is FONT_SMALL safe");
		}
	}
	for (CeFlagshipDuelAct act : {
		CeFlagshipDuelAct::kActI_OpeningSalvo,
		CeFlagshipDuelAct::kActII_BroadsideExchange,
		CeFlagshipDuelAct::kActIII_FinalStand}) {
		CHECK(CeFlagshipDuelIsFontSmallSafeText(CeFlagshipDuelActRoman(act)), "Act numeral safe");
		CHECK(CeFlagshipDuelIsFontSmallSafeText(CeFlagshipDuelActTitle(act)), "Act title safe");
	}

	struct Res { float w, h; };
	const Res resolutions[] = {{640.f, 480.f}, {1280.f, 720.f}, {1920.f, 1080.f}, {2560.f, 1440.f}};
	for (const Res &r : resolutions) {
		const CeFdHudLayout solo = CeFlagshipDuelComputeHudLayout(r.w, r.h, 0.8f, 0.6f, true);
		CHECK(solo.stripPanel.x0 >= 0.0f && solo.stripPanel.x1 <= r.w, "1P strip inside viewport X");
		CHECK(solo.venturerBarOuter.x1 < solo.centerDivider.x0 &&
		      solo.centerDivider.x1 < solo.fenrirBarOuter.x0, "1P bars do not overlap divider");
		CHECK(solo.centerDivider.y0 >= solo.venturerBarOuter.y0 - 2.5f * solo.uiScale,
		      "1P divider stays in bar row below VS label");
		CHECK(solo.stripBottomY > solo.stripPanel.y1, "1P sub-banner extends stripBottomY");

		const CeFdHudLayout split = CeFlagshipDuelComputeSplitHudLayout(r.w, r.h, 0.8f, 0.6f);
		CHECK(split.centerDivider.y0 >= split.venturerBarOuter.y0 - 1.5f * split.uiScale,
		      "2P divider stays in bar row below VS label");
		const float seamY = r.h * 0.5f;
		CHECK(std::fabs(0.5f * (split.stripPanel.y0 + split.stripPanel.y1) - seamY) < 0.5f,
		      "2P split strip is centered on the horizontal split divider");
		CHECK(split.stripPanel.x0 >= 140.0f * split.uiScale &&
		      split.stripPanel.x1 <= r.w - 140.0f * split.uiScale,
		      "2P split strip clears left target/radar and right weapon/battleline HUD columns");
		const float p1CrosshairY = r.h * 0.25f;
		const float p2CrosshairY = r.h * 0.75f;
		CHECK(split.stripPanel.y0 > p1CrosshairY + 40.0f * split.uiScale &&
		      split.stripPanel.y1 < p2CrosshairY - 40.0f * split.uiScale,
		      "2P split strip clears both P1 and P2 crosshairs");
	}
	CHECK(CeFlagshipDuelWorld::kDuelFogDensity > 0.0f &&
	      CeFlagshipDuelWorld::kDuelFogDensity <= 0.0012f,
	      "duel fog stays clear enough for 320 m flagship silhouettes");
	CHECK(std::fabs(CeFlagshipDuelWorld::kDuelSunDirY) < 1e-4f &&
	      CeFlagshipDuelWorld::kDuelSunDirZ < -0.3f,
	      "duel sun sits perpendicular to North-South axis so neither player faces sun glare");
}

} // namespace

int main() {
	TestWorldSerializer1PAnd2P();
	TestPreflightAndAssetClosure();
	TestNorthSouthPatrolAndSeparationGuard();
	TestSoloAndSplitScreenOutcomeStateMachines();
	TestOneHourSoakBothModes();
	TestHudLayoutsAndGlyphSafety();
	if (gFailures != 0) {
		std::fprintf(stderr, "%d check(s) failed.\n", gFailures);
		return 1;
	}
	std::printf("PASS: all Flagship Duel (Level 888 1P + 2P) checks succeeded.\n");
	return 0;
}
'''


def compile_and_run(include_dirs, extra_cxxflags=()):
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory(prefix="ce-fd-test-") as td:
        td_path = Path(td)
        src_path = td_path / "driver.cpp"
        bin_path = td_path / "driver"
        src_path.write_text(DRIVER_SRC, encoding="utf-8")
        cmd = [
            cxx, "-std=c++14", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-Wall", "-Wextra", "-Werror",
        ]
        for inc in include_dirs:
            cmd.extend(["-I", str(inc)])
        cmd.extend(extra_cxxflags)
        cmd.extend([str(src_path), "-o", str(bin_path)])
        comp = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        if comp.returncode != 0:
            return comp.returncode, f"COMPILE FAILED:\n{comp.stdout}\n{comp.stderr}"
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
        env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
        run = subprocess.run([str(bin_path)], capture_output=True, text=True, env=env, timeout=60)
        return run.returncode, f"{run.stdout}\n{run.stderr}"


class CeFlagshipDuelTests(unittest.TestCase):
    def test_asan_ubsan_suite(self):
        rc, out = compile_and_run([GAME])
        self.assertEqual(rc, 0, out)

    def test_causal_mutants_killed(self):
        world_src = (GAME / "ce_flagship_duel_world.h").read_text(encoding="utf-8")
        dir_src = (GAME / "ce_flagship_duel.h").read_text(encoding="utf-8")

        mutants = [
            ("solo-level-in-mp-range", "ce_flagship_duel_world.h",
             "static constexpr int kSoloLevel = 788;",
             "static constexpr int kSoloLevel = 880;"),
            ("wrong-oid-unit", "ce_flagship_duel_world.h",
             "static constexpr uint32_t kOidUnit = 8u;",
             "static constexpr uint32_t kOidUnit = 1u;"),
            ("split-level-not-888", "ce_flagship_duel_world.h",
             "static constexpr int kSplitLevel = 888;",
             "static constexpr int kSplitLevel = 857;"),
            ("wrong-donor-archive", "ce_flagship_duel_world.h",
             'static constexpr const char *kDonorArchivePath = "data\\\\resources\\\\857_res_PC.aya";',
             'static constexpr const char *kDonorArchivePath = "data\\\\resources\\\\742_res_PC.aya";'),
            ("wrong-2p-world-type", "ce_flagship_duel_world.h",
             "plan.worldType = kWorldTypeVersusMultiplayer;",
             "plan.worldType = kWorldTypeSinglePlayer;"),
            ("wrong-2p-be-config-order", "ce_flagship_duel_world.h",
             '\t"Laser",\n\t"Blaster",',
             '\t"Blaster",\n\t"Laser",'),
            ("missing-p2-start", "ce_flagship_duel_world.h",
             "\tif (mode == CeFlagshipDuelMode::kSplitScreen2P) {\n\t\t// Player 2 Start",
             "\tif (false && mode == CeFlagshipDuelMode::kSplitScreen2P) {\n\t\t// Player 2 Start"),
            ("wrong-venturer-station", "ce_flagship_duel_world.h",
             "static constexpr float kVenturerStartY = 420.0f;",
             "static constexpr float kVenturerStartY = 250.0f;"),
            ("wrong-fenrir-station", "ce_flagship_duel_world.h",
             "static constexpr float kFenrirStartY = 100.0f;",
             "static constexpr float kFenrirStartY = 260.0f;"),
            ("allow-fenrir-multiplayer", "ce_flagship_duel_world.h",
             "if (IsForbiddenUnit(def))\n\t\t\t\ts.hasForbiddenFenrirMp = true;",
             "if (IsForbiddenUnit(def))\n\t\t\t\ts.hasForbiddenFenrirMp = false;"),
            ("wrong-world-version", "ce_flagship_duel_world.h",
             "static constexpr uint16_t kWorldVersion = 50u;",
             "static constexpr uint16_t kWorldVersion = 49u;"),
            ("keep-buckets-off", "ce_flagship_duel_world.h",
             "outBuf[324] = keepBuckets ? 1u : 0u;",
             "outBuf[324] = 0u;"),
            ("skip-preflight-donor", "ce_flagship_duel_world.h",
             "if (!in.hasDonorArchive857) {",
             "if (false && !in.hasDonorArchive857) {"),
            ("skip-preflight-mesh", "ce_flagship_duel_world.h",
             "if (!in.hasAllRequiredMeshes) {",
             "if (false && !in.hasAllRequiredMeshes) {"),
            ("wrong-2p-lives", "ce_flagship_duel.h",
             "int initialLives2P = 3;",
             "int initialLives2P = 2;"),
            ("no-1p-victory", "ce_flagship_duel.h",
             "d.declareSoloWon = true;",
             "d.declareSoloWon = false;"),
            ("no-2p-p1-win", "ce_flagship_duel.h",
             "d.declareMpPlayer1Won = true;",
             "d.declareMpPlayer1Won = false;"),
            ("no-2p-p2-win", "ce_flagship_duel.h",
             "d.declareMpPlayer2Won = true;",
             "d.declareMpPlayer2Won = false;"),
            ("victory-overwritten-by-defeat", "ce_flagship_duel.h",
             "if (h == mVenturer) {\n\t\t\tmVenturerAlive = false;\n\t\t\tmVenturerHealthFrac = 0.0f;\n\t\t\tif (mPhase == CeFlagshipDuelPhase::kRunning)",
             "if (h == mVenturer) {\n\t\t\tmVenturerAlive = false;\n\t\t\tmVenturerHealthFrac = 0.0f;\n\t\t\tif (true)"),
            ("guard-never-fires", "ce_flagship_duel.h",
             "if (dist < kCeFdSeparationGuardDist && mSeparationCooldownSec <= 0.0f) {",
             "if (false && dist < kCeFdSeparationGuardDist) {"),
            ("guard-no-cooldown", "ce_flagship_duel.h",
             "mSeparationCooldownSec = kCeFdSeparationCooldownSec;",
             "mSeparationCooldownSec = 0.0f;"),
            ("guard-crossed-through", "ce_flagship_duel.h",
             "if (vy >= fy - 8.0f) {",
             "if (true) {"),
            ("push-wrong-way", "ce_flagship_duel.h",
             "out.venturerNewY = CeFlagshipDuelClampF(vy + out.ny * halfDeficit, kCeFdArenaMinY, kCeFdArenaMaxY);",
             "out.venturerNewY = CeFlagshipDuelClampF(vy - out.ny * halfDeficit, kCeFdArenaMinY, kCeFdArenaMaxY);"),
            ("push-uncapped", "ce_flagship_duel.h",
             "if (halfDeficit > maxStepPerTick)\n\t\thalfDeficit = maxStepPerTick;",
             "if (false)\n\t\thalfDeficit = maxStepPerTick;"),
            ("act-regresses", "ce_flagship_duel.h",
             "CeFlagshipDuelAct targetAct = mAct;",
             "CeFlagshipDuelAct targetAct = CeFlagshipDuelAct::kActI_OpeningSalvo;\n\t\t\tmAct = targetAct;"),
            ("no-hull-flash", "ce_flagship_duel.h",
             "if (mVenturerPrevHealthFrac - vClamped >= kCeFdHullFlashThreshold)",
             "if (false && mVenturerPrevHealthFrac - vClamped >= kCeFdHullFlashThreshold)"),
            ("allow-forbidden-glyphs", "ce_flagship_duel.h",
             "\tcase '[':\n\tcase ']':",
             "\tcase '\\x01':"),
            ("split-hud-off-seam", "ce_flagship_duel.h",
             "const float topY = seamY - panelH * 0.5f;",
             "const float topY = 8.0f * s;"),
        ]

        self.assertGreaterEqual(len(mutants), 25)

        for name, filename, old, new in mutants:
            with self.subTest(mutant=name):
                src = world_src if filename == "ce_flagship_duel_world.h" else dir_src
                self.assertEqual(src.count(old), 1, f"mutant {name}: target not unique in {filename}")
                with tempfile.TemporaryDirectory(prefix=f"fd-mut-{name}-") as td:
                    inc = Path(td)
                    (inc / "ce_flagship_duel_world.h").write_text(
                        world_src.replace(old, new) if filename == "ce_flagship_duel_world.h" else world_src,
                        encoding="utf-8",
                    )
                    (inc / "ce_flagship_duel.h").write_text(
                        dir_src.replace(old, new) if filename == "ce_flagship_duel.h" else dir_src,
                        encoding="utf-8",
                    )
                    rc, _ = compile_and_run([inc, GAME])
                    self.assertNotEqual(rc, 0, f"mutant {name} survived!")

    def test_patch_spdx_and_career_hygiene(self):
        patch_files = sorted(PATCHES.glob("*flagship-duel*.patch"))
        self.assertEqual(len(patch_files), 1, "expected flagship-duel patch in port/patches/")
        patch_text = patch_files[0].read_text(encoding="utf-8")
        self.assertIn("--- a/ResourceAccumulator.cpp", patch_text)
        self.assertIn("--- a/world.cpp", patch_text)
        self.assertIn("--- a/game.cpp", patch_text)
        self.assertIn("--- a/FrontEnd.cpp", patch_text)
        self.assertIn("PortCeFlagshipDuelDonorArchivePath", patch_text)
        self.assertIn("PortCeFlagshipDuelReadWorld", patch_text)
        self.assertIn("PortCeFlagshipDuelPreloadMeshes", patch_text)
        self.assertIn("PortCeFlagshipDuelTakeReturn", patch_text)

        # Verify balanced preprocessor directives within the patch itself.
        opens = sum(1 for l in patch_text.splitlines()
                    if l.startswith("+") and not l.startswith("+++") and l[1:].strip().startswith("#if"))
        closes = sum(1 for l in patch_text.splitlines()
                     if l.startswith("+") and not l.startswith("+++") and l[1:].strip().startswith("#endif"))
        self.assertEqual(opens, closes, "unbalanced #ifdef/#endif in flagship-duel patch")

        for path in (
            GAME / "ce_flagship_duel_world.h",
            GAME / "ce_flagship_duel_world.cpp",
            GAME / "ce_flagship_duel.h",
            GAME / "ce_flagship_duel.cpp",
            Path(__file__).resolve(),
        ):
            head = "\n".join(path.read_text(encoding="utf-8").splitlines()[:3])
            self.assertIn("SPDX-License-Identifier: GPL-3.0-only", head, f"missing SPDX in {path.name}")

        fd_cpp = (GAME / "ce_flagship_duel.cpp").read_text(encoding="utf-8")
        self.assertIn("PortCeFlagshipDuelFilterSave", fd_cpp)
        self.assertIn("GAME.SetPlayerLives(1, cfg.initialLives2P)", fd_cpp)
        self.assertIn("GAME.SetPlayerLives(2, cfg.initialLives2P)", fd_cpp)
        self.assertIn("GAME.MPDeclarePlayerWon(1)", fd_cpp)
        self.assertIn("GAME.MPDeclarePlayerWon(2)", fd_cpp)
        self.assertIn("GAME.DeclareLevelWon()", fd_cpp)
        self.assertIn("GAME.DeclareLevelLost(", fd_cpp)

        # Ensure Carrier Clash files were never modified or referenced improperly.
        bonus_cpp = (GAME / "ce_bonus.cpp").read_text(encoding="utf-8")
        self.assertIn("PortCeBonusStartFlagshipDuel1P", bonus_cpp)
        self.assertIn("PortCeBonusStartFlagshipDuel2P", bonus_cpp)
        self.assertIn("PortCeBonusStartModLevel", bonus_cpp)


if __name__ == "__main__":
    unittest.main()
