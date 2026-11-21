// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_modes.cpp: the CE mode registry (ce_modes.h). Plain state, no game headers and no allocation, so every
// build compiles it and host tests drive it directly (port/tools/ce_hooks_tests.py).
#include "ce_modes.h"

namespace {
unsigned gActive;	// bit (1 << mode) per active mode

// The pairs that may be active together; every other pair conflicts. Provisional, from today's own checks: the editor
// works on a project (a project's placement is active while the editor is), so those two coexist. A pair is added
// only with the evidence that the two modes share the game safely.
const struct { CeMode a, b; } kCompatible[] = {
	{CE_MODE_EDITOR, CE_MODE_PROJECT},
};

bool Valid(CeMode mode) { return mode > CE_MODE_NONE && mode < CE_MODE_COUNT; }
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
