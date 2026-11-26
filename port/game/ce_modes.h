// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_modes.h: one registry of the CE modes that take the game over for a while: the map editor, a museum visit,
// a bonus level, the Commander view, playing as a unit, a project, Carrier Clash, Living Sandbox, Flagship Duel, and an
// online match. A mode begins and ends here; a begin that conflicts with an active mode is refused, and the hook table
// (ce_hooks.cpp) dispatches a handler tagged with a mode only while that mode is active. Main thread only.
// Also owns the single career baseline snapshot slot (CeCareerSnapshotOwner) so at most one career-fenced world-owner
// mode can ever hold or filter a career save snapshot at once.
#ifndef PORT_CE_MODES_H
#define PORT_CE_MODES_H

enum CeMode
{
	CE_MODE_NONE = 0,			// not a mode: a hook handler tagged with it runs whatever is active
	// Existing values 1..6 preserved in exact numeric order:
	CE_MODE_EDITOR = 1,			// World-owner (career-fenced)
	CE_MODE_MUSEUM_VISIT = 2,	// World-owner (career-fenced)
	CE_MODE_BONUS_LEVEL = 3,	// World-owner (career-fenced)
	CE_MODE_COMMANDER = 4,		// Local human view/input takeover (viewport 0)
	CE_MODE_PLAY_UNIT = 5,		// Local human pilot/view takeover (viewport 0)
	CE_MODE_PROJECT = 6,		// Sub-owner (coexists with EDITOR only)
	// Appended v3 world-owner modes (career-fenced):
	CE_MODE_CARRIER_CLASH = 7,	// World-owner (career-fenced)
	CE_MODE_LIVING_SANDBOX = 8,	// World-owner (career-fenced)
	CE_MODE_FLAGSHIP_DUEL = 9,	// World-owner (career-fenced)
	CE_MODE_ONLINE_MATCH = 10,	// World-owner (career-fenced; complements 0185 PCMemoryCard fence)
	CE_MODE_COUNT				// 11 (< 32, fits unsigned CeModeActiveMask())
};

enum CeModeRefusal { CE_MODE_BEGUN = 0, CE_MODE_ALREADY_ACTIVE, CE_MODE_CONFLICT, CE_MODE_INVALID };

bool			CeModesCompatible(CeMode a, CeMode b);	// may a and b be active together (a pair is symmetric)
CeModeRefusal	CeModeBegin(CeMode mode);				// CE_MODE_BEGUN, or why not (nothing changes when refused)
bool			CeModeEnd(CeMode mode);					// false when it was not active; also releases its career snapshot
bool			CeModeActive(CeMode mode);				// CE_MODE_NONE is always active
unsigned		CeModeActiveMask();						// bit (1 << mode) per active mode
CeMode			CeModeConflictWith(CeMode mode);		// the first active mode a begin of `mode` would conflict with, or CE_MODE_NONE

// Single career-snapshot owner (plain byte storage, no game headers, no allocation).
// Enforces at most one whole-copy snapshot owner at a time, tied to the active career-fenced world-owner CeMode.
enum { CE_CAREER_SNAPSHOT_MAX_BYTES = 16384 };

bool			CeModeIsCareerFenced(CeMode mode);
bool			CeCareerSnapshotAcquire(CeMode owner, const void *src, unsigned size);
bool			CeCareerSnapshotRead(void *dest, unsigned size);
bool			CeCareerSnapshotRelease(CeMode owner);
bool			CeCareerSnapshotHasOwner();
CeMode			CeCareerSnapshotOwner();
unsigned		CeCareerSnapshotSize();

#endif
