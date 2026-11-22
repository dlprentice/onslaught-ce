// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_hooks_check.cpp: the guard test builds put around every hook handler (ce_hooks_dispatch.h:
// CeHookGuardBefore and CeHookGuardAfter, given the point, the handler and its tag). This is the STUB of that guard: a
// stronger detector (a snapshot of the simulation like the lockstep fence's) replaces these two functions, with the same
// interface. Compiled only into a test build (any build.py test switch defines CE_TEST_BUILD); every other build has
// none of it, and the dispatch compiles the guard away.
//
// The stub checks RENDER handlers only, with a digest that reads, never writes: the event manager's clock, the C
// runtime's random state and call count on this thread, the number of things and units in the world, and each player's
// Battle Engine position. It catches a RENDER handler that advances the clock, draws from the runtime's random numbers, creates
// or removes a thing, or moves a player. It does not catch: a write to any other state (a unit's health, an AI target,
// the game's own seeded random stream, which cannot be read here without drawing from it); a write deferred past the
// handler (queued and applied later); a change made and undone within the handler; render-state writes (allowed by
// design); and handlers on other threads (audio_clock runs no guard). A failure ends the program at once with exit
// status 70 and a line naming the point and the handler.
#if defined(CE_VERSION) && defined(CE_TEST_BUILD)
#include "common.h"
#include "game.h"
#include "world.h"
#include "eventmanager.h"
#include "player.h"
#include "BattleEngine.h"
#include "ce_hooks_dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void PortCrtRandState(unsigned int *calls, unsigned int *hold);	// port/platform/common/crtrand.cpp

namespace {
unsigned long long Mix(unsigned long long h, const void *data, size_t size)
{
	const unsigned char *p = (const unsigned char *)data;
	for (size_t i = 0; i < size; i++)
		h = (h ^ p[i]) * 1099511628211ull;	// FNV-1a, 64 bits
	return h;
}
}

static unsigned long long CeHookSimDigest()
{
	unsigned long long h = 14695981039346656037ull;
	const float time = EVENT_MANAGER.GetTime();
	h = Mix(h, &time, sizeof time);
	unsigned int calls = 0, hold = 0;
	PortCrtRandState(&calls, &hold);
	h = Mix(h, &calls, sizeof calls);
	h = Mix(h, &hold, sizeof hold);
	const int things = WORLD.GetThingNB().Size();
	h = Mix(h, &things, sizeof things);
	const int units = WORLD.GetUnitNB().Size();
	h = Mix(h, &units, sizeof units);
	const int players = GAME.GetNPlayers();
	for (int i = 0; i < players && i < MAX_PLAYERS; i++)
	{
		CPlayer *player = GAME.GetPlayer(i);
		CBattleEngine *be = player ? player->GetBattleEngine() : NULL;
		if (be)
		{
			const FVector pos = be->GetPos();
			h = Mix(h, &pos, sizeof pos);
		}
	}
	return h;
}

void CeHookGuardBefore(CeHookGuard *guard)
{
	if (guard->tag == CE_HOOK_RENDER)
		guard->state = CeHookSimDigest();
}

void CeHookGuardAfter(CeHookGuard *guard)
{
	if (guard->tag != CE_HOOK_RENDER)
		return;
	const unsigned long long after = CeHookSimDigest();
	if (after == guard->state)
		return;
	fprintf(stderr, "CE hook check: the RENDER handler %s at %s changed the simulation (digest %016llx, then %016llx)\n",
	        guard->handler, guard->point, guard->state, after);
	fflush(stderr);
	_Exit(70);	// not abort(): a test build's failure needs its message, not a core file
}
#endif
