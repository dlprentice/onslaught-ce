// SPDX-License-Identifier: GPL-3.0-only
// port/platform/common/testhooks.h: the two places the platform calls a test harness, compiled into every build and inert in
// all but one. gPortTestHooks is null unless a --test-kit build's own files set it at start (ce_testkit_input.cpp); no player's
// build has those files, so in it every call site below sees null and does nothing. The window's held-key table is exposed
// for the same harness, as a plain read of state the window already keeps.
#ifndef PORT_TESTHOOKS_H
#define PORT_TESTHOOKS_H

struct PortTestHooks
{
	void		(*frame)();			// once per Pump (window.cpp), before the pump reads input
	const char	*(*shotPath)();		// at a Present (d3d9hooks.cpp): a frame asked for, or null
	void		(*shotDone)();
	bool		(*wantsQuit)();		// at a Present: the harness is done; the window is closed as a player closes it
	// at each DirectInput mouse poll (dinput.cpp): movement to add and buttons to hold (bit 0 left, 1 right, 2 middle), the way
	// a feel script's authored mouse is added there; the game reads the level's mouse from that poll, not from SDL's queue
	void		(*mouseTake)(long *dx, long *dy, unsigned *buttons);
	// whether a journey drives this run (runmode.cpp's test-kit mode): only the kit's own file can answer, so a build without
	// the kit can never be in that mode, whatever its environment holds
	bool		(*journeyActive)();
};

extern PortTestHooks *gPortTestHooks;

bool	*PortWindowFakeHeld();		// window.cpp: keys held by test input, by SDL scancode

#endif
