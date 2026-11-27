// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/ce_testkit_input.cpp: the test kit's way into the platform (port/journeys/README.md), compiled only by
// build.py --test-kit. It sets testhooks.h's hooks at start, so the window's Pump runs the kit's frame and each Present takes
// its frames and its end; and it gives the kit's key and mouse steps the player's own path: SDL's event queue, with the
// window's held-key table for keys (exactly what a BEA_PORT_FAKE_KEYS token or BEA_PORT_FAKE_CLICK does).
#include "ce_testkit.h"
#include "testhooks.h"
#include <SDL3/SDL.h>
#include <stdlib.h>

void PortTestKitKey(unsigned sc, unsigned raw, bool press)
{
	if (sc < SDL_SCANCODE_COUNT)
		PortWindowFakeHeld()[sc] = press;
	SDL_Event f{};
	f.type = press ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	f.key.scancode = (SDL_Scancode)sc;
	f.key.key = SDL_GetKeyFromScancode((SDL_Scancode)sc, SDL_KMOD_NONE, false);
	f.key.raw = (Uint16)raw;
	f.key.down = press;
	SDL_PushEvent(&f);
}

namespace
{
long sTakeX, sTakeY;		// the level mouse's movement not yet given to a DirectInput poll
unsigned sHeld;				// its held buttons
void TakeMouse(long *dx, long *dy, unsigned *buttons)
{
	*dx = sTakeX;
	*dy = sTakeY;
	*buttons = sHeld;
	sTakeX = sTakeY = 0;
}
void ResetLevelMouse()
{
	sHeld = 0;
	sTakeX = 0;
	sTakeY = 0;
}
}

// The menus' and the editor's mouse: kind 0 moves the pointer to window point x,y, 1 presses and 2 releases the left button
// there, through SDL's queue (6/7 for right button). The level's mouse, which the game reads from the DirectInput poll
// (dinput.cpp): kind 3 adds a relative movement x,y, 4/5 hold/release left button (bit 0), 8/9 hold/release right button
// (bit 1), 10/11 hold/release middle button (bit 2), 12 clears all DirectInput mouse state.
void PortTestKitMouse(int kind, float x, float y)
{
	if (kind == 3) { sTakeX += long(x); sTakeY += long(y); return; }
	if (kind == 4) { sHeld |= 1u; return; }
	if (kind == 5) { sHeld &= ~1u; return; }
	if (kind == 8) { sHeld |= 2u; return; }
	if (kind == 9) { sHeld &= ~2u; return; }
	if (kind == 10) { sHeld |= 4u; return; }
	if (kind == 11) { sHeld &= ~4u; return; }
	if (kind == 12) { ResetLevelMouse(); return; }
	SDL_Event f{};
	if (kind == 0) { f.type = SDL_EVENT_MOUSE_MOTION; f.motion.x = x; f.motion.y = y; }
	else
	{
		const bool right = (kind == 6 || kind == 7);
		const bool down = (kind == 1 || kind == 6);
		f.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
		f.button.button = right ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
		f.button.down = down;
		f.button.x = x;
		f.button.y = y;
	}
	SDL_PushEvent(&f);
}

namespace
{
bool JourneyActive()
{
	const char *steps = getenv("BEA_PORT_VERBS");
	return steps && *steps;
}
PortTestHooks sKitHooks = {CeTestKitFrame, CeTestKitShotPath, CeTestKitShotDone, CeTestKitWantsQuit, TakeMouse, JourneyActive};
struct InstallHooks
{
	InstallHooks()
	{
		gPortTestHooks = &sKitHooks;
		CeTestKitRegisterCleanup(ResetLevelMouse);
	}
} sInstall;
}
