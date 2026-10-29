// SPDX-License-Identifier: GPL-3.0-only
// port/game/updatedump.cpp: a per-Update dump of the simulation, for bisecting the Godot rebuild's divergences against
// the decomp's own code. Built as a game file (port/build.py) so it reads the game's objects through their
// headers; called first in every CGame::Update (patch 0006). Off unless BEA_PORT_UPDATE_DUMP names a file.
//   BEA_PORT_UPDATE_DUMP=PATH            the dump
//   BEA_PORT_UPDATE_DUMP_FROM=N, _TO=N   only Updates N..M (1-based, counted from process start)
//   BEA_PORT_UPDATE_DUMP_THINGS=0        leave out the thing list
// One block per Update:
//   U <update> F <presents so far> E <event time, float bits hex> R <rng value> <rng seed> D <draws since the reseed>
//     N <things>
//   H <thing number> <thing type hex> <class (Itanium type name)> <x> <y> <z>   (position as float bits, hex)
// The draw count steps the game's own generator (Next in SeedUniformRandomNumberStream.cpp) forward from the previous
// Update's value until it reaches the current one; it restarts from the seed when the stream object or seed changes.
// Nothing here changes game state: the thing list is walked node by node, not with the set's own iterator.
#include "common.h"
#include "game.h"
#include "world.h"
#include "eventmanager.h"
#include "thing.h"
#include "SeedUniformRandomNumberStream.h"
#ifdef CE_COMMANDER_TEST
#include "ce_commander.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <typeinfo>
#include "../platform/common/portlog.h"

// The dump is the port's own output, written where the caller asks: the C library's fopen, not the game's (port.h
// sends game files' fopen through the path layer, which refuses absolute paths).
#undef fopen

extern int RANDOM_MULTIPLIER;
extern int RANDOM_MODULUS;
unsigned long long PortClockFrames();

namespace
{
FILE *gDump;
bool gInit, gThings = true;
unsigned long gUpdate, gFrom = 1, gTo = 0xffffffffUL;
const CSeedUniformRandomNumberStream *gStream;
int gSeed, gLast;
long long gDraws;

// GenericSPtrSet's storage (SPtrSet.h): first node, last, the iterator, size; a node is item, next.
struct NodeView { void *item; NodeView *next; };
struct SetView { NodeView *first, *last, *iterator; int size; };

int Step(int v)
{
	int q = RANDOM_MODULUS / RANDOM_MULTIPLIER, r = RANDOM_MODULUS % RANDOM_MULTIPLIER;
	int t = RANDOM_MULTIPLIER * (v % q) - r * (v / q);
	return t > 0 ? t : t + RANDOM_MODULUS;
}

// Draws from `from` to `to`, or -1 when not reached within a bound (a reseed the hook did not see).
long long Distance(int from, int to)
{
	for (long long n = 0; n < 50000000; n++, from = Step(from))
		if (from == to)
			return n;
	return -1;
}

unsigned Bits(float f)
{
	unsigned u;
	memcpy(&u, &f, 4);
	return u;
}
}

static void SessionGameUpdate()
{
	// Update owns live players; no inspection while loading, drawing the frontend, or tearing a level down.
	// The getters below only read fields (including monitored references), never RNG, input or a game clock.
	if (PortSessionEnabled())
	{
		static int level = -1, state = -1, pause = -1;
		static int form[MAX_PLAYERS] = {-1, -1, -1, -1}, view[MAX_PLAYERS] = {-1, -1, -1, -1};
		static int edge[MAX_PLAYERS] = {-1, -1, -1, -1};
		int nextLevel = GAME.GetCurrentLevel(), nextState = GAME.GetGameState(), nextPause = GAME.IsPaused();
		if (nextLevel != level || nextState != state || nextPause != pause)
		{
			PortSessionLog(PORT_SESSION_GAME, false, "level=%d phase=%d paused=%d", nextLevel, nextState, nextPause);
			if (nextLevel != level || (nextState != state && nextState == GAME_STATE_PRE_RUNNING))
				for (int i = 0; i < MAX_PLAYERS; ++i) form[i] = view[i] = edge[i] = -1;
			level = nextLevel; state = nextState; pause = nextPause;
		}
		int players = GAME.GetNPlayers();
		if (nextState >= GAME_STATE_PRE_RUNNING && nextState <= GAME_STATE_LEVEL_WON && players > 0 && players <= MAX_PLAYERS)
			for (int i = 0; i < players; ++i)
			{
				CPlayer *player = GAME.GetPlayer(i);
				CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
				if (!engine) { form[i] = view[i] = edge[i] = -1; continue; }
				int nextForm = engine->GetState(), nextView = player->GetIsInFPV();
				const FVector &pos = engine->GetPos();
				unsigned xyz[3];
				memcpy(&xyz[0], &pos.X, 4); memcpy(&xyz[1], &pos.Y, 4); memcpy(&xyz[2], &pos.Z, 4);
				// Positive IEEE754 encodings are ordered. Test the documented 0..512 XY region without any FP work;
				// nonfinite positions are a separate flag, not mistaken for an ordinary boundary crossing.
				int nextEdge = 0;
				for (int axis = 0; axis < 2; ++axis)
				{
					unsigned magnitude = xyz[axis] & 0x7fffffffu;
					if (magnitude >= 0x7f800000u) nextEdge |= 16;
					else if ((xyz[axis] >> 31) && magnitude) nextEdge |= 1 << (axis * 2);
					else if (magnitude > 0x44000000u) nextEdge |= 2 << (axis * 2);
				}
				if (nextForm != form[i] || nextView != view[i] || nextEdge != edge[i])
					PortSessionLog(PORT_SESSION_GAME, false,
						"level=%d player=%d form=%d (0=to_walker 1=to_aircraft 2=walker 3=aircraft) first_person=%d "
						"xy_outside_0_512=%d position_ieee754=%08x,%08x,%08x",
						level, i + 1, nextForm, nextView, nextEdge, xyz[0], xyz[1], xyz[2]);
				form[i] = nextForm; view[i] = nextView; edge[i] = nextEdge;
			}
	}
}

void PortUpdateHook()
{
#ifdef CE_COMMANDER_TEST
	CeCommanderUpdate();		// the commander order prototype (ce_commander.cpp): returns at once unless its proof switch is set
#endif
	SessionGameUpdate();
	if (!gInit)
	{
		gInit = true;
		const char *path = getenv("BEA_PORT_UPDATE_DUMP");
		if (path && *path && !(gDump = fopen(path, "w")))
			fprintf(stderr, PORT_LOG_PREFIX "update dump: cannot write %s\n", path);
		if (const char *v = getenv("BEA_PORT_UPDATE_DUMP_FROM"))
			gFrom = strtoul(v, NULL, 10);
		if (const char *v = getenv("BEA_PORT_UPDATE_DUMP_TO"))
			gTo = strtoul(v, NULL, 10);
		if (const char *v = getenv("BEA_PORT_UPDATE_DUMP_THINGS"))
			gThings = *v && *v != '0';
	}
	gUpdate++;
	if (!gDump)
		return;
	// CGame::mRandomStream is protected: read it where the probes do (GAME + 0x304, retail's offset and the port's).
	const CSeedUniformRandomNumberStream *stream = *(CSeedUniformRandomNumberStream *const *)((const char *)&GAME + 0x304);
	int value = stream ? stream->mValue : 0, seed = stream ? stream->mSeed : 0;
	if (stream && (stream != gStream || seed != gSeed))
	{
		gStream = stream;
		gSeed = gLast = seed;
		gDraws = 0;
	}
	if (stream && gDraws >= 0)
	{
		long long n = Distance(gLast, value);
		gDraws = n < 0 ? -1 : gDraws + n;
		gLast = value;
	}
	if (gUpdate < gFrom || gUpdate > gTo)
		return;
	SetView *things = (SetView *)&WORLD.GetThingNB();
	fprintf(gDump, "U %lu F %llu E %08x R %d %d D %lld N %d\n", gUpdate, PortClockFrames(),
			Bits(EVENT_MANAGER.GetTime()), value, seed, gDraws, things->size);
	if (gThings)
		for (NodeView *n = things->first; n; n = n->next)
		{
			CThing *t = (CThing *)n->item;
			if (!t)
				continue;
			FVector &p = t->GetPos();
			fprintf(gDump, "H %d %08lx %s %08x %08x %08x\n", t->GetThingNumber(), (unsigned long)t->GetThingType(),
					typeid(*t).name(), Bits(p.X), Bits(p.Y), Bits(p.Z));
		}
	fflush(gDump);
}
