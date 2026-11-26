// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_modes.cpp: the CE mode registry and single career-snapshot owner (ce_modes.h). Plain state, no game
// headers and no allocation, so every build compiles it and host tests drive it directly (port/tools/ce_hooks_tests.py).
#include "ce_modes.h"
#include <cstring>

namespace {
unsigned		gActive;	// bit (1 << mode) per active mode
CeMode			gSnapshotOwner = CE_MODE_NONE;
unsigned		gSnapshotSize = 0;
unsigned char	gSnapshotBytes[CE_CAREER_SNAPSHOT_MAX_BYTES];

// The pairs that may be active together; every other pair conflicts.
// - All 7 career-fenced world-owners (EDITOR, MUSEUM_VISIT, BONUS_LEVEL, CARRIER_CLASH, LIVING_SANDBOX,
//   FLAGSHIP_DUEL, ONLINE_MATCH) are strictly pairwise incompatible.
// - {EDITOR, PROJECT} coexist (editor works on a project).
// - {COMMANDER, PLAY_UNIT} are incompatible (entering PLAY_UNIT closes COMMANDER first).
// - COMMANDER and PLAY_UNIT may coexist with {BONUS_LEVEL, CARRIER_CLASH, LIVING_SANDBOX, FLAGSHIP_DUEL},
//   and are refused under {EDITOR, MUSEUM_VISIT, ONLINE_MATCH, PROJECT}.
const struct { CeMode a, b; } kCompatible[] = {
	{CE_MODE_EDITOR, CE_MODE_PROJECT},
	{CE_MODE_BONUS_LEVEL, CE_MODE_COMMANDER},
	{CE_MODE_BONUS_LEVEL, CE_MODE_PLAY_UNIT},
	{CE_MODE_CARRIER_CLASH, CE_MODE_COMMANDER},
	{CE_MODE_CARRIER_CLASH, CE_MODE_PLAY_UNIT},
	{CE_MODE_LIVING_SANDBOX, CE_MODE_COMMANDER},
	{CE_MODE_LIVING_SANDBOX, CE_MODE_PLAY_UNIT},
	{CE_MODE_FLAGSHIP_DUEL, CE_MODE_COMMANDER},
	{CE_MODE_FLAGSHIP_DUEL, CE_MODE_PLAY_UNIT},
};

bool Valid(CeMode mode) { return mode > CE_MODE_NONE && mode < CE_MODE_COUNT; }
}

bool CeModeIsCareerFenced(CeMode mode)
{
	switch (mode)
	{
	case CE_MODE_EDITOR:
	case CE_MODE_MUSEUM_VISIT:
	case CE_MODE_BONUS_LEVEL:
	case CE_MODE_CARRIER_CLASH:
	case CE_MODE_LIVING_SANDBOX:
	case CE_MODE_FLAGSHIP_DUEL:
	case CE_MODE_ONLINE_MATCH:
		return true;
	default:
		return false;
	}
}

bool CeModesCompatible(CeMode a, CeMode b)
{
	if (a == CE_MODE_NONE || b == CE_MODE_NONE)
		return true;
	for (const auto &pair : kCompatible)
		if ((pair.a == a && pair.b == b) || (pair.a == b && pair.b == a))
			return true;
	return false;
}

CeMode CeModeConflictWith(CeMode mode)
{
	for (int other = CE_MODE_NONE + 1; other < CE_MODE_COUNT; other++)
		if (other != mode && (gActive & (1u << other)) && !CeModesCompatible(mode, (CeMode)other))
			return (CeMode)other;
	return CE_MODE_NONE;
}

CeModeRefusal CeModeBegin(CeMode mode)
{
	if (!Valid(mode))
		return CE_MODE_INVALID;
	if (gActive & (1u << mode))
		return CE_MODE_ALREADY_ACTIVE;
	if (CeModeConflictWith(mode) != CE_MODE_NONE)
		return CE_MODE_CONFLICT;
	gActive |= 1u << mode;
	return CE_MODE_BEGUN;
}

bool CeModeEnd(CeMode mode)
{
	if (!Valid(mode) || !(gActive & (1u << mode)))
		return false;
	if (gSnapshotOwner == mode)
	{
		gSnapshotOwner = CE_MODE_NONE;
		gSnapshotSize = 0;
	}
	gActive &= ~(1u << mode);
	return true;
}

bool CeModeActive(CeMode mode)
{
	return mode == CE_MODE_NONE || (Valid(mode) && (gActive & (1u << mode)));
}

unsigned CeModeActiveMask()
{
	return gActive;
}

bool CeCareerSnapshotAcquire(CeMode owner, const void *src, unsigned size)
{
	if (!CeModeIsCareerFenced(owner) || !CeModeActive(owner) || !src || size == 0 || size > CE_CAREER_SNAPSHOT_MAX_BYTES)
		return false;
	if (gSnapshotOwner != CE_MODE_NONE && gSnapshotOwner != owner)
		return false;
	std::memcpy(gSnapshotBytes, src, size);
	gSnapshotOwner = owner;
	gSnapshotSize = size;
	return true;
}

bool CeCareerSnapshotRead(void *dest, unsigned size)
{
	if (gSnapshotOwner == CE_MODE_NONE || !CeModeActive(gSnapshotOwner) || !dest || size == 0 || size != gSnapshotSize)
		return false;
	std::memcpy(dest, gSnapshotBytes, size);
	return true;
}

bool CeCareerSnapshotRelease(CeMode owner)
{
	if (owner == CE_MODE_NONE || gSnapshotOwner != owner)
		return false;
	gSnapshotOwner = CE_MODE_NONE;
	gSnapshotSize = 0;
	return true;
}

bool CeCareerSnapshotHasOwner()
{
	return gSnapshotOwner != CE_MODE_NONE && CeModeActive(gSnapshotOwner) && gSnapshotSize > 0;
}

CeMode CeCareerSnapshotOwner()
{
	return CeCareerSnapshotHasOwner() ? gSnapshotOwner : CE_MODE_NONE;
}

unsigned CeCareerSnapshotSize()
{
	return CeCareerSnapshotHasOwner() ? gSnapshotSize : 0u;
}
