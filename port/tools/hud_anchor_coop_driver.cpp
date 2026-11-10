// SPDX-License-Identifier: GPL-3.0-only
// The co-op gate of the HUD anchoring: the network flavour of port/game/ce_hud_anchor.cpp (a network co-op game counts as one
// player's view although its level is a multiplayer one), against a fake game and engine and the production gate in hud_scale.cpp.
// Usage: hud_anchor_coop_driver
#include "game.h"
#include "DXEngine.h"
#include "hud_scale.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#define PORT_LOG_PREFIX "onslaught-ce: "
bool PortCeLayer() { return true; }
int PortWideModes() { return 1; }
bool PortDevLog() { return false; }
extern "C" int CE_SettingGetBool(const char *, int def) { return def; }
FakeGame GAME;
FakeEngine ENGINE;
static int lockstep;
extern "C" int CE_LockstepRequested(void) { return lockstep; }
int PortHudAnchored();	// production ce_hud_anchor.cpp, network flavour

static void check(bool value, const char *message)
{
	if (!value)
	{
		fprintf(stderr, "FAIL: %s\n", message);
		exit(2);
	}
}

int main()
{
	for (const char *name : {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_HUD_SCALE"})
		unsetenv(name);
	GAME.multiplayer = false; ENGINE.viewpoints = 1; lockstep = 0;
	check(PortHudAnchored(), "ordinary single-player play is anchored");
	ENGINE.viewpoints = 2;
	check(!PortHudAnchored(), "a solo level that counts two viewpoints is not anchored");
	ENGINE.viewpoints = 1; GAME.multiplayer = true;
	check(!PortHudAnchored(), "local multiplayer is not anchored");
	ENGINE.viewpoints = 2;
	check(!PortHudAnchored(), "local split screen (two viewpoints, no network game) is not anchored");
	lockstep = 1;
	check(PortHudAnchored(), "a network co-op game is anchored although the engine counts two viewpoints (it draws one player's view)");
	ENGINE.viewpoints = 1;
	check(PortHudAnchored(), "a network co-op game that counts one viewpoint is anchored");
	GAME.multiplayer = false; ENGINE.viewpoints = 2;
	check(PortHudAnchored(), "a network co-op game on a solo level (the tutorial) is anchored whatever the count");
	GAME.multiplayer = true; ENGINE.viewpoints = 1;
	setenv("BEA_PORT_HUD_SCALE", "0", 1);
	check(!PortHudAnchored(), "BEA_PORT_HUD_SCALE=0 turns it off in a co-op game too");
	puts("PASS");
	return 0;
}
