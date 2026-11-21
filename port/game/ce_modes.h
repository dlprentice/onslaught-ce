// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_modes.h: one registry of the CE modes that take the game over for a while: the map editor, a museum visit,
// a bonus level, the Commander view, playing as a unit, and a project. A mode begins and ends here; a begin that
// conflicts with an active mode is refused, and the hook table (ce_hooks.cpp) dispatches a handler tagged with a mode
// only while that mode is active. Main thread only. Today each feature keeps its own active flag and its own checks
// against the others; nothing is registered here yet.
#ifndef PORT_CE_MODES_H
#define PORT_CE_MODES_H

enum CeMode
{
	CE_MODE_NONE = 0,			// not a mode: a hook handler tagged with it runs whatever is active
	CE_MODE_EDITOR,
	CE_MODE_MUSEUM_VISIT,
	CE_MODE_BONUS_LEVEL,
	CE_MODE_COMMANDER,
	CE_MODE_PLAY_UNIT,
	CE_MODE_PROJECT,
	CE_MODE_COUNT
};

enum CeModeRefusal { CE_MODE_BEGUN = 0, CE_MODE_ALREADY_ACTIVE, CE_MODE_CONFLICT, CE_MODE_INVALID };

bool			CeModesCompatible(CeMode a, CeMode b);	// may a and b be active together (a pair is symmetric)
CeModeRefusal	CeModeBegin(CeMode mode);				// CE_MODE_BEGUN, or why not (nothing changes when refused)
bool			CeModeEnd(CeMode mode);					// false when it was not active
bool			CeModeActive(CeMode mode);				// CE_MODE_NONE is always active
unsigned		CeModeActiveMask();						// bit (1 << mode) per active mode
CeMode			CeModeConflictWith(CeMode mode);		// the first active mode a begin of `mode` would conflict with, or CE_MODE_NONE

#endif
