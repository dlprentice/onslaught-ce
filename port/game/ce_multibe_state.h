// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_multibe_state.h: pure-C++ Multi-Battle-Engine state, 4-slot preferences, team-aware spawn inversion,
// Walker/Jet AI flight & combat control laws, team score/respawn tracking, and options menu mapping.
// Zero game-header dependencies so host unit tests and ASan/UBSan mutant campaigns compile and verify this directly.
#ifndef PORT_CE_MULTIBE_STATE_H
#define PORT_CE_MULTIBE_STATE_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { CE_MULTIBE_SLOTS = 4 };

enum CeMultiBePlaySide
{
	CE_MULTIBE_SIDE_FORSETI = 0,
	CE_MULTIBE_SIDE_MUSPELL = 1
};

enum CeMultiBeModeChoice
{
	CE_MULTIBE_CHOICE_STANDARD = 0,	// "2" (retail 1P/2P default)
	CE_MULTIBE_CHOICE_THREE = 1,	// "3" (3 BEs)
	CE_MULTIBE_CHOICE_FOUR = 2		// "4" (4 BEs / 2v2)
};

enum CeSoloSkirmishChoice
{
	CE_SOLO_SKIRMISH_SPLITSCREEN = 0,	// "0" (retail 2P splitscreen default)
	CE_SOLO_SKIRMISH_SOLO_AI = 1		// "1" (fullscreen solo vs AI)
};

enum CeMultiBeCareerPrefKind
{
	CE_PREF_IS_GOD = 0,
	CE_PREF_INVERT_Y = 1,
	CE_PREF_INVERT_JET_Y = 2,
	CE_PREF_VIBRATION = 3,
	CE_PREF_CONTROLLER_CFG = 4
};

inline bool CeMultiBeStateStrEqCase(const char *a, const char *b)
{
	if (!a || !b)
		return false;
	while (*a && *b)
	{
		char ca = *a++, cb = *b++;
		if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
		if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
		if (ca != cb)
			return false;
	}
	return *a == 0 && *b == 0;
}

inline int CeMultiBeParsePlaySide(const char *text)
{
	if (text && CeMultiBeStateStrEqCase(text, "muspell"))
		return CE_MULTIBE_SIDE_MUSPELL;
	return CE_MULTIBE_SIDE_FORSETI;
}

inline const char *CeMultiBePlaySideIniValue(int side)
{
	return (side == CE_MULTIBE_SIDE_MUSPELL) ? "muspell" : "forseti";
}

inline int CeMultiBeModeFromIni(const char *text)
{
	if (!text || !*text)
		return CE_MULTIBE_CHOICE_STANDARD;
	if (CeMultiBeStateStrEqCase(text, "3"))
		return CE_MULTIBE_CHOICE_THREE;
	if (CeMultiBeStateStrEqCase(text, "4") || CeMultiBeStateStrEqCase(text, "1") ||
	    CeMultiBeStateStrEqCase(text, "on") || CeMultiBeStateStrEqCase(text, "true"))
		return CE_MULTIBE_CHOICE_FOUR;
	return CE_MULTIBE_CHOICE_STANDARD;
}

inline const char *CeMultiBeModeToIni(int choice)
{
	if (choice == CE_MULTIBE_CHOICE_THREE)
		return "3";
	if (choice == CE_MULTIBE_CHOICE_FOUR)
		return "4";
	return "2";
}

// Parses ce.ini `solo_skirmish` value. Fails closed to Splitscreen (0) on absent/off/invalid values.
inline int CeMultiBeParseSoloSkirmish(const char *text)
{
	if (!text || !*text)
		return CE_SOLO_SKIRMISH_SPLITSCREEN;
	if (CeMultiBeStateStrEqCase(text, "1") || CeMultiBeStateStrEqCase(text, "on") ||
	    CeMultiBeStateStrEqCase(text, "true") || CeMultiBeStateStrEqCase(text, "solo"))
		return CE_SOLO_SKIRMISH_SOLO_AI;
	return CE_SOLO_SKIRMISH_SPLITSCREEN;
}

inline const char *CeMultiBeSoloSkirmishIniValue(int choice)
{
	return (choice == CE_SOLO_SKIRMISH_SOLO_AI) ? "1" : "0";
}

// Returns the active Battle Engine target count for ce_multibe:
// - 3 or 4 when `multi_be` explicitly requests 3..4 BEs (any level);
// - 2 (1v1 Solo Skirmish) when `solo_skirmish=1` on a multiplayer/skirmish level (850..899);
// - 0 (inactive) otherwise, keeping retail 1P/2P runs completely untouched.
inline int CeMultiBeEffectiveTargetCount(int multiBeCount, bool soloSkirmish, int level)
{
	if (multiBeCount >= 3)
		return (multiBeCount > CE_MULTIBE_SLOTS) ? CE_MULTIBE_SLOTS : multiBeCount;
	if (soloSkirmish && level >= 850 && level <= 899)
		return 2;
	return 0;
}

// Formats the FEPMultiplayerStart status summary line using only BEA-drawable glyphs (no pipe/underscore/brackets).
// Returns false when all settings are retail defaults (Forseti, Splitscreen, Standard 2).
inline bool CeMultiBeFormatMpStartStatus(bool soloSkirmish, int playSide, int modeChoice, char *out, size_t cap)
{
	if (!out || cap == 0)
		return false;
	if (!soloSkirmish && playSide == CE_MULTIBE_SIDE_FORSETI && modeChoice == CE_MULTIBE_CHOICE_STANDARD)
	{
		out[0] = 0;
		return false;
	}
	const char *sideLabel = (playSide == CE_MULTIBE_SIDE_MUSPELL) ? "Muspell" : "Forseti";
	const char *modeLabel = (modeChoice == CE_MULTIBE_CHOICE_FOUR) ? "4 BEs (2v2)"
	                      : (modeChoice == CE_MULTIBE_CHOICE_THREE) ? "3 BEs" : "Standard (2)";
	if (soloSkirmish)
		snprintf(out, cap, "Solo vs AI  -  Play side: %s  -  Battle Engines: %s", sideLabel, modeLabel);
	else
		snprintf(out, cap, "Play side: %s  -  Battle Engines: %s", sideLabel, modeLabel);
	return true;
}

// Computes the final 1-based player number for CStart / CSpawnPoint after retail mInvertSides and 0359's
// CLI-only PortCePlayUnitInvertSides have already run on slots 1..2:
// - Never double-inverts slots 1..2 if retail mInvertSides or 0359 already inverted them.
// - Inverts slots 1 <-> 2 when a Versus map is launched from the Front-End menu with play_side=muspell.
// - Inverts slots 3 <-> 4 whenever ANY side inversion (retail, CLI 0359, or menu play_side=muspell) is active.
inline int CeMultiBeTransformStartSlot(int currentPlayerNumber, bool retailInvert, bool cli0359Invert,
                                       bool menuPlaySideMuspell, bool isVersusMap)
{
	if (currentPlayerNumber == 1 || currentPlayerNumber == 2)
	{
		if (isVersusMap && menuPlaySideMuspell && !retailInvert && !cli0359Invert)
			return (currentPlayerNumber == 1) ? 2 : 1;
		return currentPlayerNumber;
	}
	if (currentPlayerNumber == 3 || currentPlayerNumber == 4)
	{
		const bool invert34 = retailInvert || (isVersusMap && (cli0359Invert || menuPlaySideMuspell));
		if (invert34)
			return (currentPlayerNumber == 3) ? 4 : 3;
		return currentPlayerNumber;
	}
	return currentPlayerNumber;
}

// Selects the Battle Engine configuration ID for 1-based player slots 1..4 without perturbing P1/P2 retail behavior.
inline int CeMultiBeSelectConfigId(int playerNumber, int p1Config, int p2Config,
                                   int p3Config = -1, int p4Config = -1)
{
	if (playerNumber == 1)
		return p1Config;
	if (playerNumber == 3)
		return (p3Config >= 0) ? p3Config : p1Config;
	if (playerNumber == 4)
		return (p4Config >= 0) ? p4Config : p2Config;
	return p2Config;
}

// CE-side storage for player slots 2..3 (1-based P3/P4) career preferences so CCareer's binary [2] .bes layout
// never changes by a single byte.
struct CeMultiBeExtraSlotPrefs
{
	int isGod[2];
	int invertY[2];
	int invertJetY[2];
	int vibration[2];
	int controllerConfig[2];

	void Reset()
	{
		for (int i = 0; i < 2; i++)
		{
			isGod[i] = 0;
			invertY[i] = 0;
			invertJetY[i] = 0;
			vibration[i] = 0;
			controllerConfig[i] = 0;
		}
	}

	int Get(int zeroBasedSlot, int kind) const
	{
		if (zeroBasedSlot < 2 || zeroBasedSlot >= CE_MULTIBE_SLOTS)
			return 0;
		const int idx = zeroBasedSlot - 2;
		switch (kind)
		{
		case CE_PREF_IS_GOD: return isGod[idx];
		case CE_PREF_INVERT_Y: return invertY[idx];
		case CE_PREF_INVERT_JET_Y: return invertJetY[idx];
		case CE_PREF_VIBRATION: return vibration[idx];
		case CE_PREF_CONTROLLER_CFG: return controllerConfig[idx];
		default: return 0;
		}
	}

	void Set(int zeroBasedSlot, int kind, int value)
	{
		if (zeroBasedSlot < 2 || zeroBasedSlot >= CE_MULTIBE_SLOTS)
			return;
		const int idx = zeroBasedSlot - 2;
		switch (kind)
		{
		case CE_PREF_IS_GOD: isGod[idx] = value ? 1 : 0; break;
		case CE_PREF_INVERT_Y: invertY[idx] = value ? 1 : 0; break;
		case CE_PREF_INVERT_JET_Y: invertJetY[idx] = value ? 1 : 0; break;
		case CE_PREF_VIBRATION: vibration[idx] = value ? 1 : 0; break;
		case CE_PREF_CONTROLLER_CFG: controllerConfig[idx] = value; break;
		default: break;
		}
	}
};

// Pure O(1) angle wrapping to [-pi, pi].
inline float CeMultiBeWrapAngle(float a)
{
	if (!isfinite(a))
		return 0.0f;
	const float r = remainderf(a, 6.283185307179586f);
	return isfinite(r) ? r : 0.0f;
}

inline float CeMultiBeClamp(float v, float lo, float hi)
{
	if (!isfinite(v))
		return 0.0f;
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

// Decides whether an AI-piloted Battle Engine should morph between Walker and Jet mode.
// BEA uses +Z down, so dry land satisfies groundZ < waterZ - dryMargin.
// CRITICAL SAFETY INVARIANT: never morph from Jet -> Walker over water!
struct CeMultiBeMorphInput
{
	bool	isJet;
	bool	flightAllowed;
	bool	groundIsDry;		// strictly above water with safe margin
	bool	nearWaterHazard;	// walker dangerously close to water
	float	energyRatio;		// 0.0 .. 1.0
	float	targetDist;			// meters to primary target (or rally point); <0 if none
	float	targetAltitude;		// target height above ground (>0 means airborne)
	int		ticksSinceMorph;
	int		slotIndex;
	int		simTicks;
};

inline bool CeMultiBeShouldMorph(const CeMultiBeMorphInput &in)
{
	if (!in.flightAllowed)
		return false;
	// Anti-thrash cooldown (5 seconds at 20 Hz, except emergency water escape for walker).
	if (!in.isJet)
	{
		if (in.nearWaterHazard && in.energyRatio > 0.25f)
			return true;
		if (in.ticksSinceMorph < 90)
			return false;
		if (in.energyRatio < 0.50f)
			return false;
		// Take off to pursue distant targets, engage airborne enemies, or execute periodic air sorties.
		if (in.targetDist > 85.0f)
			return true;
		if (in.targetDist > 0.0f && in.targetAltitude > 12.0f)
			return true;
		// Alternating tactical air sortie so 2v2 / 4-BE skirmishes feature lively combined-arms Walker + Jet combat.
		if (((in.simTicks + in.slotIndex * 65) % 240) < 10 && in.energyRatio > 0.65f)
			return true;
		return false;
	}
	else
	{
		// In Jet mode: NEVER land over water!
		if (!in.groundIsDry)
			return false;
		// Low energy over dry ground -> land safely before stalling!
		if (in.energyRatio < 0.28f)
			return true;
		if (in.ticksSinceMorph < 80)
			return false;
		// Close ground combat over dry land -> drop into Walker mode for precision cannon/strafe engagement.
		if (in.targetDist >= 0.0f && in.targetDist < 52.0f && in.targetAltitude < 10.0f)
			return true;
		return false;
	}
}

struct CeMultiBeJetCommand
{
	float	turn;		// [-1, 1]
	float	pitch;		// [-1, 1]
	float	thrust;		// [-1, 1]
	bool	fire;
};

// Pure Jet-mode steering law in BEA coordinates (+Z down: negative Z is higher altitude).
// Maintains safe clearance above both terrain (`groundZ`) and water (`waterZ`), banks into targets,
// orbits instead of nose-diving at close range, and fires when aligned with the target cone.
inline CeMultiBeJetCommand CeMultiBeComputeJetControl(
	float curYaw, float curPitch, float posZ, float groundZ, float waterZ,
	float toTargetX, float toTargetY, float toTargetZ, bool hasHostile, float desiredCruiseHeight)
{
	CeMultiBeJetCommand cmd = { 0.0f, 0.0f, 0.75f, false };
	const float surfaceZ = (groundZ < waterZ) ? groundZ : waterZ;
	const float safeCeilingZ = surfaceZ - ((desiredCruiseHeight > 14.0f) ? desiredCruiseHeight : 14.0f);
	const float floorDangerZ = surfaceZ - 11.0f;

	const float horizDist = sqrtf(toTargetX * toTargetX + toTargetY * toTargetY);
	float desiredYaw = curYaw;
	if (horizDist > 2.0f)
	{
		// BEA azimuth: atan2(dx, dy) matches FVector::Azimuth().
		desiredYaw = atan2f(toTargetX, toTargetY);
		// Orbit slightly when very close so jets perform clean strafing passes instead of colliding head-on.
		if (horizDist < 38.0f)
			desiredYaw = CeMultiBeWrapAngle(desiredYaw + 0.65f);
	}
	const float yawErr = CeMultiBeWrapAngle(desiredYaw - curYaw);
	cmd.turn = CeMultiBeClamp(-yawErr * 1.4f, -0.95f, 0.95f);

	// Desired pitch: positive altitude error (too low, posZ > desiredZ) needs nose UP.
	float targetAltZ = safeCeilingZ;
	if (hasHostile && horizDist > 42.0f && horizDist < 190.0f)
	{
		const float aimZ = posZ + toTargetZ;
		const float clampedAimZ = (aimZ < surfaceZ - 14.0f) ? aimZ : (surfaceZ - 14.0f);
		targetAltZ = clampedAimZ;
	}
	float desiredPitch = 0.0f;
	if (posZ > floorDangerZ)
	{
		// Emergency pull-up above ground/water!
		desiredPitch = 0.35f;
		cmd.thrust = 1.0f;
	}
	else
	{
		const float zErr = posZ - targetAltZ; // >0 means below target altitude (need to climb, positive pitch)
		desiredPitch = CeMultiBeClamp(zErr * 0.025f, -0.28f, 0.32f);
		cmd.thrust = (horizDist > 100.0f || zErr > 8.0f) ? 1.0f : 0.70f;
	}
	const float pitchErr = desiredPitch - curPitch;
	cmd.pitch = CeMultiBeClamp(pitchErr * 1.8f, -0.85f, 0.85f);

	// Fire when facing a hostile within weapon envelope and not in emergency pull-up.
	if (hasHostile && posZ <= floorDangerZ && horizDist > 16.0f && horizDist < 220.0f && fabsf(yawErr) < 0.36f)
		cmd.fire = true;
	return cmd;
}

// Pure Walker pitch aiming command for CBattleEngineWalkerPart::Pitch(vy), which subtracts vy/117 from mPitchvel.
// Positive desiredPitch - curPitch means nose must pitch UP (+mPitch), requiring a negative vy input.
inline float CeMultiBeComputeWalkerPitchInput(float curPitch, float desiredPitch)
{
	const float clampedDesired = CeMultiBeClamp(desiredPitch, -0.55f, 0.55f);
	const float pitchErr = CeMultiBeWrapAngle(clampedDesired - curPitch);
	if (fabsf(pitchErr) <= 0.04f)
		return 0.0f;
	return CeMultiBeClamp(-pitchErr * 1.6f, -0.75f, 0.75f);
}

// Team combat tracker: tracks active Battle Engines, Walker/Jet split, team kills, and per-slot respawn timers.
struct CeMultiBeMatchState
{
	enum { kRespawnTicks = 100 };	// 5.0 seconds at 20 Hz

	int		forsetiAlive;
	int		muspellAlive;
	int		forsetiTotal;
	int		muspellTotal;
	int		walkerCount;
	int		jetCount;
	int		forsetiKills;
	int		muspellKills;
	int		respawnTimer[CE_MULTIBE_SLOTS];	// 0 = alive/idle, >0 = ticks remaining until respawn
	int		slotTeam[CE_MULTIBE_SLOTS];		// CE_MULTIBE_SIDE_FORSETI or CE_MULTIBE_SIDE_MUSPELL
	int		totalRespawns;
	int		totalMorphs;

	void Reset()
	{
		forsetiAlive = muspellAlive = 0;
		forsetiTotal = muspellTotal = 0;
		walkerCount = jetCount = 0;
		forsetiKills = muspellKills = 0;
		totalRespawns = 0;
		totalMorphs = 0;
		for (int i = 0; i < CE_MULTIBE_SLOTS; i++)
		{
			respawnTimer[i] = 0;
			slotTeam[i] = CE_MULTIBE_SIDE_FORSETI;
		}
	}

	void RegisterSlotTeam(int slot, int team)
	{
		if (slot < 0 || slot >= CE_MULTIBE_SLOTS)
			return;
		slotTeam[slot] = (team == CE_MULTIBE_SIDE_MUSPELL) ? CE_MULTIBE_SIDE_MUSPELL : CE_MULTIBE_SIDE_FORSETI;
	}

	void OnAuxDestroyed(int slot)
	{
		if (slot < 0 || slot >= CE_MULTIBE_SLOTS)
			return;
		if (slotTeam[slot] == CE_MULTIBE_SIDE_MUSPELL)
			forsetiKills++;
		else
			muspellKills++;
		if (respawnTimer[slot] <= 0)
			respawnTimer[slot] = kRespawnTicks;
	}

	// Advances respawn timers by 1 tick; returns bitmask of slots whose timer just reached 0 and are ready to respawn.
	uint8_t TickRespawnTimers()
	{
		uint8_t readyMask = 0;
		for (int i = 0; i < CE_MULTIBE_SLOTS; i++)
		{
			if (respawnTimer[i] > 0)
			{
				respawnTimer[i]--;
				if (respawnTimer[i] == 0)
					readyMask |= (uint8_t)(1u << i);
			}
		}
		return readyMask;
	}

	void FormatHudBanner(bool isVersus, int playerSide, char *out, size_t cap) const
	{
		if (!out || cap == 0)
			return;
		if (isVersus)
		{
			snprintf(out, cap, "2v2 VERSUS  FORSETI %d/%d (K:%d)  vs  MUSPELL %d/%d (K:%d)  -  %s",
			         forsetiAlive, forsetiTotal, forsetiKills,
			         muspellAlive, muspellTotal, muspellKills,
			         playerSide == CE_MULTIBE_SIDE_MUSPELL ? "MUSPELL" : "FORSETI");
		}
		else
		{
			const int total = forsetiTotal + muspellTotal;
			const int alive = forsetiAlive + muspellAlive;
			snprintf(out, cap, "%d-BE SQUAD  ACTIVE %d/%d  WALKER %d  JET %d",
			         total, alive, total, walkerCount, jetCount);
		}
	}
};

#endif
