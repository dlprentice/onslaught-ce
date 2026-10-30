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
//   controls, God flags, vibration, controller configuration and screen position are kept if changed during the
//   level. Key bindings live outside the career copy;
// - the front end goes straight to the debriefing, without the save page, and the debriefing shows no goodies and
//   returns to the Onslaught CE page, to its Bonus Levels list with the level highlighted.
//
// The same list offers level 856, a local two-player building-defence scenario that the retail Multiplayer grid leaves
// out (the original ships it twice, as levels 856 and 858, which are the same map: the list has it once, and this module
// accepts both). It opens retail's Multiplayer start page (each player's Battle Engine, then the
// level, as for a grid map), the career is kept out of it in the same way, and after it the front end comes back to
// the Bonus Levels list (patch 0116; a map started from the Multiplayer page itself goes back there, as in retail, and no
// save page is shown after a multiplayer level in retail either). In them the CE gamepad defaults (ce_gamepad.cpp) also
// run, so a second pad drives player 2. The career copy is put back only when the level that ended is the one it was
// taken for; backing out of the start page and playing anything else drops it.
//
// The five Race Challenges, levels 901-905 (the original's goodies 66-70: one-player three-lap time trials in the
// "Racer" configuration, which the retail game opens from the Goodies page: the first after 26 campaign levels at grade C
// or better (Career.cpp UpdateGoodieStates), each later one when the race before it is finished fast enough (that race's
// LapMonitor.msl "Race Complete" sets the next goodie: under 145, 195, 150 and 105 seconds), the fifth opening nothing),
// start at once like 201, and patch 0116 keeps every career out of them as well: the original's race branch in
// CFrontEnd::Init sends a finished or abandoned race through the autosave page to the Goodies page, which would write the
// career (a race's script also changes goodie states, which the snapshot below puts back in memory). With a CE race the
// branch skips the autosave and opens the Onslaught CE page instead.
//
// Every bonus level comes back to the list it was started from, with its entry highlighted: the CE page asks
// PortCeBonusTakeReturn() when it opens, once.
//
// This route does not save level progress at the end. While the bonus level is running, patch 0181 also makes any
// CCareer::Save of the live career serialize the entry snapshot's progress with the current settings. The normal save
// path still writes bindings, PC options and settings, including on pause/continue. Only in the CE layer's menus
// (PortCeMenus: off in faithful play, a replay, a recording or under probes).
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
const int	kRaceFirst = 901, kRaceLast = 905;
CCareer		*sCareer;		// the career as it was before the bonus level (raw bytes, as CCareer::Save keeps them)
int			sLevel;			// the level the copy was taken for
bool		sActive;		// a bonus level has been started
bool		sReturning;		// the front end is showing its debriefing
bool		sRaceReturning;	// the front end is back after a race (patch 0116 asks once)
bool		sCoopReturning;	// the front end is back after a bonus co-op map (patch 0116 asks once)
int			sReturnLevel;	// the bonus level the front end has come back from, for the CE page's list (0: none)

bool IsRace(int level) { return level >= kRaceFirst && level <= kRaceLast; }
bool IsCoop(int level) { return level == 856 || level == 858; }
}

// Starts a bonus level from the front end: 201 or a race (901-905) at once; 856 or 858 through the Multiplayer start page.
// False when it cannot start (the CE menus are off, or no memory).
bool PortCeBonusStart(int level)
{
	if (!PortCeMenus() || (level != kBonusLevel && !IsCoop(level) && !IsRace(level)))
		return false;
	if (!sCareer)
		sCareer = (CCareer *)malloc(sizeof(CCareer));
	if (!sCareer)
		return false;
	memcpy((void *)sCareer, (const void *)&CAREER, sizeof(CCareer));
	sLevel = level;
	sActive = true;
	sReturning = false;
	sRaceReturning = false;
	sCoopReturning = false;
	sReturnLevel = 0;
	FRONTEND.mSelectedLevel = level;
	if (level == kBonusLevel || IsRace(level))
		FRONTEND.SetQuit(level);
	else
		FRONTEND.SetPage(FEP_MULTIPLAYER_START, 30);
	return true;
}

// A bonus co-op level is being played (the CE gamepad defaults run in it as in a solo level).
bool PortCeBonusCoopRunning()
{
	return sActive && IsCoop(sLevel) && GAME.GetCurrentLevel() == sLevel;
}

// CCareer::Save first copies the live object to dest. For an active bonus level, replace only that object in the output
// with the entry snapshot and current settings. The caller has already applied patch 0052's god-mode save policy, so
// read its temporarily adjusted values here. Do not change the live career, the version, the key bindings or the PC
// options; Save restores its temporary god flags and writes the trailing records as it normally does.
void PortCeBonusFilterSave(CCareer *career, void *dest)
{
	if (career != &CAREER || !PortCeMenus() || !sActive || !sCareer ||
		GAME.GetCurrentLevel() != sLevel || FRONTEND.mSelectedLevel != sLevel)
		return;
	CCareer filtered;
	memcpy((void *)&filtered, (const void *)sCareer, sizeof(filtered));
	filtered.SetMusicVolume(career->GetMusicVolume());
	filtered.SetSoundVolume(career->GetSoundVolume());
	for (int player = 0; player < 2; ++player)
	{
		filtered.SetInvertYAxis(player, career->GetInvertYAxis(player));
		filtered.SetInvertJetYAxis(player, career->GetInvertJetYAxis(player));
		filtered.SetIsGod(player, career->GetIsGod(player));
		filtered.SetVibration(player, career->GetVibration(player));
		filtered.SetControllerConfigurationNum(player, career->GetControllerConfigurationNum(player));
	}
	// Screen position shares its upper bytes with the first two kill counters. Use the accessors so their lower bytes
	// remain the snapshot's counters rather than copying progress from the live career.
	filtered.SetScreenPosX(career->GetScreenPosX());
	filtered.SetScreenPosY(career->GetScreenPosY());
	memcpy(dest, (const void *)&filtered, sizeof(filtered));
}

// The front end is back after a level: true when that was the bonus level and the career has been put back. Each entry
// starts with no return pending, so a debrief or race return the front end never asked for cannot outlive its level.
bool PortCeBonusLevelEnd()
{
	sReturning = false;
	sRaceReturning = false;
	sCoopReturning = false;
	sReturnLevel = 0;
	if (!sActive)
		return false;
	sActive = false;
	if (FRONTEND.mSelectedLevel != sLevel)
		return false;	// backed out of the start page and played something else: the career is updated as usual
	float music = CAREER.GetMusicVolume(), sound = CAREER.GetSoundVolume();
	const int screenX = CAREER.GetScreenPosX(), screenY = CAREER.GetScreenPosY();
	BOOL invertY[2], invertJetY[2], isGod[2], vibration[2];
	int controllerConfig[2];
	for (int player = 0; player < 2; ++player)
	{
		invertY[player] = CAREER.GetInvertYAxis(player);
		invertJetY[player] = CAREER.GetInvertJetYAxis(player);
		isGod[player] = CAREER.GetIsGod(player);
		vibration[player] = CAREER.GetVibration(player);
		controllerConfig[player] = CAREER.GetControllerConfigurationNum(player);
	}
	memcpy((void *)&CAREER, (const void *)sCareer, sizeof(CCareer));
	CAREER.SetMusicVolume(music);
	CAREER.SetSoundVolume(sound);
	for (int player = 0; player < 2; ++player)
	{
		CAREER.SetInvertYAxis(player, invertY[player]);
		CAREER.SetInvertJetYAxis(player, invertJetY[player]);
		CAREER.SetIsGod(player, isGod[player]);
		CAREER.SetVibration(player, vibration[player]);
		CAREER.SetControllerConfigurationNum(player, controllerConfig[player]);
	}
	CAREER.SetScreenPosX(screenX);
	CAREER.SetScreenPosY(screenY);
	sReturning = sLevel == kBonusLevel;	// a co-op level goes back to the Multiplayer page, as in retail
	sRaceReturning = IsRace(sLevel);	// a race goes back to the Onslaught CE page, with no save (patch 0116)
	sCoopReturning = IsCoop(sLevel);	// so does a co-op map, instead of the Multiplayer page (patch 0116)
	sReturnLevel = sLevel;				// the CE page opens on the Bonus Levels list, this level highlighted
	return true;
}

// A console map command can leave a level without returning through the front end. Restore the old bonus before
// System.cpp changes the selected level, without leaving a bonus debrief or list return for the destination level.
// A cancelled multiplayer-start page may leave sActive set; the running-level check keeps an unrelated level intact.
void PortCeBonusBeforeConsoleLevel()
{
	if (!sActive || !sCareer || GAME.GetCurrentLevel() != sLevel || FRONTEND.mSelectedLevel != sLevel)
		return;
	PortCeBonusLevelEnd();
	sReturning = false;
	sRaceReturning = false;
	sCoopReturning = false;
	sReturnLevel = 0;
}

// The front end's race branch (CFrontEnd::Init, patch 0116) asks whether the race that ended was a CE bonus race: true
// once. Then it opens the Onslaught CE page and writes nothing, in place of the original's autosave page.
bool PortCeBonusRaceReturning()
{
	const bool returning = sRaceReturning;
	sRaceReturning = false;
	return returning;
}

// The front end's multiplayer branch (CFrontEnd::Init, patch 0116) asks whether the map that ended was a CE bonus co-op map:
// true once. Then it opens the Onslaught CE page in place of the Multiplayer page.
bool PortCeBonusCoopReturning()
{
	const bool returning = sCoopReturning;
	sCoopReturning = false;
	return returning;
}

// The CE page has opened: the bonus level the front end has just come back from, or 0 (once): its list is shown, the level
// highlighted.
int PortCeBonusTakeReturn()
{
	const int level = sReturnLevel;
	sReturnLevel = 0;
	return level;
}

bool PortCeBonusReturning()
{
	return sReturning;
}

// The debriefing is done: back to the Onslaught CE page (it opens on the Bonus Levels list, PortCeBonusTakeReturn).
void PortCeBonusLeaveDebriefing()
{
	sReturning = false;
	FRONTEND.SetPage(FEP_LANGUAGE_TEST, 30);	// the CE page's slot (patch 0050)
}

#endif
