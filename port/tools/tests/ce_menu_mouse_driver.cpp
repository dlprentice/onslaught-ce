// SPDX-License-Identifier: GPL-3.0-only
// Runs the production widescreen.cpp's pointer for the game's in-game menus (PortUiMouseMenu, PortUiMouseMenuPoint, PortUiMouse) against stand-ins for the rest of the port: whether those menus read
// the virtual screen's pointer (the game's mouse released, the CE layer on, the commander map not open), and where a window position lands in the 640 x 480 virtual screen for a wide window (through the
// 4:3 box), a window taller than 4:3, a 4:3 window of any size and 640 x 480. ce_menu_mouse_tests.py compiles this with and without CE_VERSION and runs it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include "platform.h"
#include "portlog.h"

static bool layer = true, mapOpen = false;
static int sharpSettingsReads, sessionLogs;
bool PortCeLayer() { return layer; }
bool PortCeCmdMapOpen() { return mapOpen; }
bool PortDevLog() { return false; }
int PortHudAnchored() { return 0; }
SDL_Window *PortMainSdlWindow() { return 0; }
extern "C" int CE_SettingGet(const char *, int def) { return def; }
extern "C" int CE_SettingSet(const char *, int) { return 1; }
extern "C" int CE_SettingGetBool(const char *, int def) { ++sharpSettingsReads; return def; }
void PortSessionLog(PortSessionCategory, bool, const char *, ...) { ++sessionLogs; }

static int failures;
static void expect(bool ok, const char *what, int a = 0, int b = 0)
{
	if (ok) return;
	printf("FAIL %s (%d, %d)\n", what, a, b);
	++failures;
}

// A back buffer of w x h, and the point (x, y) read as the menus' pointer: it must land at (ex, ey).
static void lands(unsigned w, unsigned h, int x, int y, int ex, int ey)
{
	PortWideBackBuffer(w, h);
	int px = x, py = y;
	PortUiMouseMenuPoint(&px, &py);
	char what[96];
	snprintf(what, sizeof(what), "%ux%u: window point %d,%d lands at %d,%d", w, h, x, y, ex, ey);
	expect(px == ex && py == ey, what, px, py);
}

int main()
{
	setenv("BEA_PORT_WIDE_TEST", "1280x720", 1);		// the wide paths on, as a test forces them (the real choice depends on the video driver)
#ifdef CE_VERSION
	for (int active = 0; active < 2; ++active)
		for (int ce = 0; ce < 2; ++ce)
			for (int open = 0; open < 2; ++open)
			{
				layer = ce != 0; mapOpen = open != 0;
				char what[96];
				snprintf(what, sizeof(what), "the menus' pointer is virtual exactly when the mouse is released (active %d), the layer is on (%d) and the map is closed (%d)", active, ce, open);
				expect(PortUiMouseMenu(active) == (!active && ce && !open ? 1 : 0), what);
			}
	layer = true; mapOpen = false;
#else
	for (int active = 0; active < 2; ++active)
		for (int ce = 0; ce < 2; ++ce)
		{
			layer = ce != 0;
			expect(PortUiMouseMenu(active) == 0, "a private build's menus keep the window's pixels as retail");
		}
#endif
	// a wide window: through the 4:3 box (960 x 720 at x = 160 for 1280 x 720), clamped to the virtual screen
	lands(1280, 720, 640, 360, 320, 240);
	lands(1280, 720, 160, 0, 0, 0);
	lands(1280, 720, 0, 0, 0, 0);
	lands(1280, 720, 1279, 719, 639, 479);
	lands(1280, 720, 800, 360, 426, 240);
	lands(1920, 1080, 960, 540, 320, 240);
	lands(1920, 1080, 240, 0, 0, 0);
	lands(1920, 1080, 1680, 1080, 639, 479);
	lands(3840, 2160, 1920, 1080, 320, 240);
	// taller than 4:3: the box sits between bars above and below
	lands(1000, 1000, 500, 500, 320, 240);
	// 4:3 windows: the game's own scaling by the window's size, and 640 x 480 untouched
	lands(1280, 960, 640, 480, 320, 240);
	lands(1280, 960, 1279, 959, 639, 479);
	lands(1024, 768, 512, 384, 320, 240);
	lands(640, 480, 123, 45, 123, 45);
	expect(sharpSettingsReads == 0 && sessionLogs == 0, "mouse mapping does not consult sharp text settings or log");
	printf(failures ? "FAIL\n" : "PASS\n");
	return failures ? 1 : 0;
}
