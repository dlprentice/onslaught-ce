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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <typeinfo>

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

void PortUpdateHook()
{
	if (!gInit)
	{
		gInit = true;
		const char *path = getenv("BEA_PORT_UPDATE_DUMP");
		if (path && *path && !(gDump = fopen(path, "w")))
			fprintf(stderr, "bea-port: update dump: cannot write %s\n", path);
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
