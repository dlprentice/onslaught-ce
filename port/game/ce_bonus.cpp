// SPDX-License-Identifier: GPL-3.0-only
// The CE bonus level: level 201, "Aquila Prototype", the playable demo's level, which the retail game reaches only
// through -playabledemo (FEPDemoMain starts it with FRONTEND.SetQuit(201)). The Onslaught CE page starts it the same
// way from the main menu, and patch 0107 keeps every career out of it:
//
// - before the level, the career structure is copied in memory, as CCareer::Save copies it;
// - when the front end comes back, that copy is put back in place of CCareer::Update (as CCareer::Load does for a
//   career load: the volumes in use are kept), restoring the in-memory career progress (kills, slots, goodie states,
//   completed nodes). Not CCareer::Load itself: with its options it sets the language, and the front end's
//   text is not loaded yet at that point (CText::Copy crashed). Volumes, the two players' invert-Y and invert-jet-Y
//   controls, and their God flags are kept if changed during the level. Key bindings live outside the career copy.
//   The snapshot still restores other career fields, including vibration and controller configuration: the current
//   in-level menus do not call their setters;
// - the front end goes straight to the debriefing, without the save page, and the debriefing shows no goodies and
//   returns to the main menu.
//
// The same page offers levels 856 and 858, two selections for a local two-player building-defence scenario that the
// retail Multiplayer grid leaves out. They open retail's Multiplayer start page (each player's Battle Engine, then the
// level, as for a grid map), the career is kept out of them in the same way, and after them the front end goes back to
// the Multiplayer page, as after any multiplayer level (no save page there in retail either). In them the CE gamepad
// defaults (ce_gamepad.cpp) also run, so a second pad drives player 2. The career copy is put back only when the level
// that ended is the one it was taken for; backing out of the start page and playing anything else drops it.
//
// This route does not save level progress at the end. Other game paths can still save the live career and settings,
// including on pause/continue; restoring the snapshot later is not a fence around those earlier writes. Only in the CE
// layer's menus (PortCeMenus: off in faithful play, a replay, a recording or under probes).
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "Career.h"
#include "game.h"

#include <stdlib.h>
#include <string.h>

bool	PortCeMenus();

namespace
{
const int	kBonusLevel = 201;
CCareer		*sCareer;		// the career as it was before the bonus level (raw bytes, as CCareer::Save keeps them)
int			sLevel;			// the level the copy was taken for
bool		sActive;		// a bonus level has been started
bool		sReturning;		// the front end is showing its debriefing
}

// Starts a bonus level from the front end: 201 at once; 856 or 858 through the Multiplayer start page. False when it
// cannot start (the CE menus are off, or no memory).
bool PortCeBonusStart(int level)
{
	if (!PortCeMenus() || (level != kBonusLevel && level != 856 && level != 858))
		return false;
	if (!sCareer)
		sCareer = (CCareer *)malloc(sizeof(CCareer));
	if (!sCareer)
		return false;
	memcpy((void *)sCareer, (const void *)&CAREER, sizeof(CCareer));
	sLevel = level;
	sActive = true;
	sReturning = false;
	FRONTEND.mSelectedLevel = level;
	if (level == kBonusLevel)
		FRONTEND.SetQuit(level);
	else
		FRONTEND.SetPage(FEP_MULTIPLAYER_START, 30);
	return true;
}

// A bonus co-op level is being played (the CE gamepad defaults run in it as in a solo level).
bool PortCeBonusCoopRunning()
{
	return sActive && sLevel != kBonusLevel && GAME.GetCurrentLevel() == sLevel;
}

// The front end is back after a level: true when that was the bonus level and the career has been put back.
bool PortCeBonusLevelEnd()
{
	if (!sActive)
		return false;
	sActive = false;
	if (FRONTEND.mSelectedLevel != sLevel)
		return false;	// backed out of the start page and played something else: the career is updated as usual
	float music = CAREER.GetMusicVolume(), sound = CAREER.GetSoundVolume();
	BOOL invertY[2], invertJetY[2], isGod[2];
	for (int player = 0; player < 2; ++player)
	{
		invertY[player] = CAREER.GetInvertYAxis(player);
		invertJetY[player] = CAREER.GetInvertJetYAxis(player);
		isGod[player] = CAREER.GetIsGod(player);
	}
	memcpy((void *)&CAREER, (const void *)sCareer, sizeof(CCareer));
	CAREER.SetMusicVolume(music);
	CAREER.SetSoundVolume(sound);
	for (int player = 0; player < 2; ++player)
	{
		CAREER.SetInvertYAxis(player, invertY[player]);
		CAREER.SetInvertJetYAxis(player, invertJetY[player]);
		CAREER.SetIsGod(player, isGod[player]);
	}
	sReturning = sLevel == kBonusLevel;	// a co-op level goes back to the Multiplayer page, as in retail
	return true;
}

bool PortCeBonusReturning()
{
	return sReturning;
}

void PortCeBonusLeaveDebriefing()
{
	sReturning = false;
	FRONTEND.SetPage(FEP_MAIN, 30);
}

#endif
