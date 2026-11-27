// SPDX-License-Identifier: GPL-3.0-only
// HUD anchoring on wide screens (port/platform/common/widescreen.cpp, hud_scale.cpp; patches 0043 and 0089). The game lays its
// HUD out against the window's edges; on a wide back buffer patch 0043 moved the corner groups into the columns a 4:3 picture
// would have. In ordinary single-player play they stay in the screen's corners instead, which is the game's own rule at any
// size. Presentation only: this reads the game's state and changes none, and nothing here is on a replay or recording path
// (CeHudAnchorEnabled is false there).
#include "common.h"
#include "game.h"
#include "DXEngine.h"
#include "hud_scale.h"
extern bool PortCeLayer();
extern bool PortCeOneScreenMatch() __attribute__((weak));
extern bool CeCmdMatch() __attribute__((weak));
extern bool CeCmdMapOn() __attribute__((weak));
extern "C" int PortCeSoloSkirmishActive(void) __attribute__((weak));
#ifndef CE_NETWORK_OFF
extern "C" int CE_LockstepRequested(void);
extern "C" int CE_LockstepActive(void) __attribute__((weak));
extern "C" int CE_WorldActive(void) __attribute__((weak));
extern "C" int PortCeOnlineMatchActive(void) __attribute__((weak));
#endif

namespace {
struct CeGameSplitAccess : CGame {
	static bool Horizontal() { return static_cast<const CGame &>(GAME).*(&CeGameSplitAccess::mHorizontalSplitscreen); }
	static bool FullscreenMultiplayer() { return static_cast<const CGame &>(GAME).*(&CeGameSplitAccess::mFullscreenMultiplayer) != 0; }
};

bool CeSingleViewportMultiplayer()
{
	if (CeGameSplitAccess::FullscreenMultiplayer())
		return true;
	if (PortCeOneScreenMatch && PortCeOneScreenMatch())
		return true;
	if (PortCeSoloSkirmishActive && PortCeSoloSkirmishActive() != 0)
		return true;
	if (CeCmdMatch && CeCmdMatch() && (!CeCmdMapOn || CeCmdMapOn()))
		return true;
#ifndef CE_NETWORK_OFF
	if (CE_LockstepRequested() != 0 ||
	    (CE_LockstepActive && CE_LockstepActive() != 0) ||
	    (CE_WorldActive && CE_WorldActive() != 0) ||
	    (PortCeOnlineMatchActive && PortCeOnlineMatchActive() != 0))
		return true;
#endif
	return false;
}
}

// Whether this multiplayer session is actually rendering a split-screen layout (more than one viewpoint).
// Single-viewport multiplayer modes (Solo vs AI Skirmish, Commander Match, Online MP, cg_fullscreenmultiplayer)
// render one full-screen viewport, so HUD layout, HUD anchoring and target indicators follow the 1P layout.
// Checking single-viewport sources directly also handles level-load calls (ENGINE.BuildLevelSpecifics) before
// the first CGame::Render() sets ENGINE.GetNumViewpoints().
// When the CE layer is off (replays, recordings, faithful mode), this returns GAME.IsMultiplayer() unchanged.
int PortCeIsSplitScreen()
{
	if (!PortCeLayer())
		return GAME.IsMultiplayer() ? 1 : 0;
	if (!GAME.IsMultiplayer())
		return 0;
	if (ENGINE.GetNumViewpoints() == 1 || CeSingleViewportMultiplayer())
		return 0;
	return 1;
}

// Whether the camera should use split-screen aspect:
// 0 = normal (0.75f: 1P, single-viewport MP, 3P/4P quad-split)
// 1 = 2P horizontal split (0.5f)
// 2 = 2P vertical split (1.5f)
// Faithful/replay runs keep GAME.IsMultiplayer() unchanged (0 or 1).
int PortCeCameraSplitAspect()
{
	if (!PortCeLayer())
		return GAME.IsMultiplayer() ? 1 : 0;
	const int vp = ENGINE.GetNumViewpoints();
	if (!GAME.IsMultiplayer() || vp == 1 || vp == 3 || vp == 4 || CeSingleViewportMultiplayer())
		return 0;
	return CeGameSplitAccess::Horizontal() ? 1 : 2;
}

// Whether the HUD about to be drawn is pinned to the corners: the HUD scale's gate with ce.ini's hud_anchor, for a single
#ifdef CE_NETWORK_OFF
// viewpoint and not split-screen multiplayer, matching the HUD scale's scopes (patches 0089, 0235, 0387).
#else
// viewpoint and not split-screen multiplayer (a network co-op game draws one player's view whatever the engine's count of viewpoints),
// matching the HUD scale's scopes (patches 0089, 0157, 0235, 0387).
#endif
int PortHudAnchored()
{
#ifdef CE_NETWORK_OFF
	return CeHudAnchorEnabled() && !PortCeIsSplitScreen() && ENGINE.GetNumViewpoints() == 1;
#else
	return CeHudAnchorEnabled() && (CE_LockstepRequested() || (!PortCeIsSplitScreen() && ENGINE.GetNumViewpoints() == 1));
#endif
}
