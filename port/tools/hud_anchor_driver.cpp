// SPDX-License-Identifier: GPL-3.0-only
// The HUD anchoring chain against authored boundaries: the production gate (port/platform/common/hud_scale.cpp), the
// production viewpoint test (port/game/ce_hud_anchor.cpp, against a fake game and engine) and the very statements of
// widescreen.cpp that apply or withhold patch 0043's inset (cut out of the file by hud_anchor_tests.py).
// Usage: hud_anchor_driver absent|on|off  (what ce.ini's hud_anchor says: no line, 1, 0).
#include "game.h"
#include "DXEngine.h"
#include "hud_scale.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#define PORT_LOG_PREFIX "onslaught-ce: "
bool layer = true;
int wideModes = 1;
bool devLog = true;
int iniMode = 0, iniReads = 0;	// 0: no line, 1: hud_anchor=1, 2: hud_anchor=0
bool PortCeLayer() { return layer; }
int PortWideModes() { return wideModes; }
bool PortDevLog() { return devLog; }
extern "C" int CE_SettingGetBool(const char *key, int def)
{
	++iniReads;
	if (strcmp(key, "hud_anchor") != 0 || iniMode == 0)
		return def;
	return iniMode == 1 ? 1 : 0;
}
FakeGame GAME;
FakeEngine ENGINE;

// widescreen.cpp's state, which the cut-out statements read.
bool gActive = true;
float gX = 4.f / 3.f;
int PortHudAnchored();	// production ce_hud_anchor.cpp
#include "widescreen_hud.inc"

static void check(bool value, const char *message)
{
	if (!value)
	{
		fprintf(stderr, "FAIL: %s\n", message);
		exit(2);
	}
}
static bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

static void clear_env()
{
	for (const char *name : {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD", "BEA_PORT_HUD_SCALE"})
		unsetenv(name);
}

int main(int argc, char **argv)
{
	check(argc == 2, "usage");
	iniMode = !strcmp(argv[1], "on") ? 1 : !strcmp(argv[1], "off") ? 2 : 0;
	const bool wanted = iniMode != 2;	// ce.ini's answer; the default is on
	clear_env();

	// A faithful run, a replay, a recording, the player's opt-out, no CE layer, no widescreen modes: no answer from the
	// ini is even asked for.
	for (const char *name : {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD"})
	{
		setenv(name, "1", 1);
		check(!CeHudAnchorEnabled() && !PortHudAnchored(), name);
		unsetenv(name);
	}
	setenv("BEA_PORT_HUD_SCALE", "0", 1);
	check(!CeHudAnchorEnabled() && !PortHudAnchored(), "BEA_PORT_HUD_SCALE=0");
	unsetenv("BEA_PORT_HUD_SCALE");
	layer = false;
	check(!CeHudAnchorEnabled(), "faithful layer");
	layer = true;
	wideModes = 0;
	check(!CeHudAnchorEnabled(), "widescreen modes off");
	wideModes = 1;
	check(iniReads == 0, "a fenced run never reads ce.ini");

	// Ordinary single-player play: the ini decides, and is read once.
	check(CeHudAnchorEnabled() == wanted, "the ini's answer (no line means on)");
	check(PortHudAnchored() == (wanted ? 1 : 0), "one viewpoint, not multiplayer");
	check(CeHudAnchorEnabled() == wanted && iniReads == 1, "the ini is read once");
	GAME.multiplayer = true;
	check(!PortHudAnchored(), "multiplayer keeps the 4:3 columns");
	GAME.multiplayer = false;
	ENGINE.viewpoints = 2;
	check(!PortHudAnchored(), "two viewpoints keep the 4:3 columns");
	ENGINE.viewpoints = 1;
	check(PortHudAnchored() == (wanted ? 1 : 0), "one viewpoint again");

	// The inset: PortHudBegin decides, PortHudInset (the objective panel's own terms) follows it.
	struct Shape { float w, gx, inset; } shapes[] = {
		{1920.f, 4.f / 3.f, 240.f},			// 16:9 1080p: W / 8
		{2560.f, 4.f / 3.f, 320.f},			// 16:9 1440p
		{3840.f, 4.f / 3.f, 480.f},			// 16:9 4K
		{2560.f, 1.7777778f, 560.f},		// 21:9 2560 x 1080
		{1920.f, 1.2f, 160.f},				// 16:10 1920 x 1200
		{1280.f, 1.f, 0.f},					// 5:4 or 4:3: nothing widened
	};
	for (const Shape &s : shapes)
	{
		gActive = true;
		gX = s.gx;
		float began = PortHudBegin(0.f, s.w);
		float explicitTerm = PortHudInset(s.w);
		check(near(began, wanted ? 0.f : s.inset) && near(explicitTerm, wanted ? 0.f : s.inset), "pinned: no inset; else the 4:3 columns");
		check(near(PortHudOriginal(), 0.f), "the game's own offset is kept");
		GAME.multiplayer = true;
		began = PortHudBegin(0.f, s.w);
		check(near(began, s.inset) && near(PortHudInset(s.w), s.inset), "multiplayer: the 4:3 columns");
		GAME.multiplayer = false;
	}
	gActive = true;
	gX = 4.f / 3.f;
	check(near(PortHudBegin(5.f, 1920.f), wanted ? 5.f : 245.f) && near(PortHudOriginal(), 5.f), "an offset other than 0 is kept, with or without the shift");
	gActive = false;
	check(near(PortHudBegin(0.f, 1920.f), 0.f) && near(PortHudInset(1920.f), 0.f), "a back buffer that is not widened has no shift");
	gActive = true;

	// The developer log tells each change once, and nothing when it is off.
	devLog = false;
	GAME.multiplayer = true;
	PortHudBegin(0.f, 1920.f);
	GAME.multiplayer = false;
	PortHudBegin(0.f, 1920.f);
	puts("HUD anchoring chain: PASS");
	return 0;
}
