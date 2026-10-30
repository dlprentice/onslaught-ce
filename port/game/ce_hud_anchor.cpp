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

// Whether the HUD about to be drawn is pinned to the corners: the HUD scale's gate with ce.ini's hud_anchor, for a single
// viewpoint and not in multiplayer, the same test as the HUD scale's scopes (patch 0089) apply to each group.
int PortHudAnchored()
{
	return CeHudAnchorEnabled() && !GAME.IsMultiplayer() && ENGINE.GetNumViewpoints() == 1;
}
