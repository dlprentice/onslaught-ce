// SPDX-License-Identifier: GPL-3.0-only
// Commander mode, the order layer. It keeps a table of the level's squads and gives them the orders the commander map (ce_cmdmap.cpp) asks for, the way the
// mission scripts give theirs: a state (AI_OFF, AI_ONF) and a go-to-point on the squad, or on each member of an infantry squad. The verbs are the ones
// measured to hold against the squads' own AI: Move (AI_OFF and a goal: passive, and the goal is never overwritten), Advance (AI_ONF and a goal: fights on the
// way; AI_ON loses the goal as soon as the squad has a target, which is always while an enemy exists), Hold (AI_ONF at the members' centre: it stays and fires) and
// Fall back (Move to the nearest own-side safe point). A Stop that left the squad passive (AI_OFF at the members' centre) was measured too and stays in the test
// module: a squad that does not defend itself is a trap for the player. Infantry squads have no goal of their own, so patch 0160 lets this layer set the
// squad's centre, or leave the members where they are; a squad with an order must not be dissolved into another, which patch 0161 refuses.
//
// It is part of every CE build and off unless ce.ini says commander=1 (CeCmdMapOn): never under a replay, a recording, probes, a virtual clock, a test pilot or a
// network game (kFenced), in multiplayer, or outside a level that is playing. It draws no random number, writes nothing to a career or the options, and allocates
// nothing: its table is static and the game's sets are walked with the game's own iterator objects, which keep their position on the stack.
//
// A test build (-DCE_COMMANDER_TEST, build.py --commander-test) adds, in this file, what the experiments needed and the shipped game does not: the proof switch
// (BEA_PORT_COMMANDER_PROOF=1 lets the layer run under the virtual clock, a tape or probes, as photo mode's proof switch does for it), an order tape, a probe of
// every squad, and the map's diagnostics.
//
// The order tape (BEA_PORT_COMMANDER_TAPE): one line per order, `TICK VERB ARGS`, blank lines and `#` comments allowed. TICK counts the updates in which the level is
// playing and not paused, from 1. A squad is named by its ordinal: the order in which the layer first saw it (creation order, so the same in every run of a level);
// the probe prints each squad's ordinal. Verbs (a squad S, a target squad T, a member index K counted from 0):
//   move S X Y          AI_OFF, then the squad's go-to-point: passive
//   amove S X Y         AI_ON and the go-to-point; the layer puts the goal back when the squad has no target (it loses the goal in contact: use advance)
//   advance S X Y       AI_ONF and the go-to-point: the march that fights on the way
//   attack S T          AI_ON; the squad's target is the squad T and stays until T dies (slot 85, as IScript::Attack hands it)
//   attacku S T K       the same with the squad of the unit K of T as the target (a script's Attack turns a unit into its squad)
//   hold S              go-to-point on its own position, then AI_ONF: stays, aims, fires
//   defend S            AI_ON and AI_DEFENSIVE: home and goal are its position, far targets are ignored
//   fallback S [X Y]    AI_OFF and a go-to-point on the point, or on the nearest own-side safe point, or on the pilot
//   stop S              AI_OFF and a go-to-point on its own position
//   state S on|off|onf|defensive|normal       the squad's own AI state, nothing else
//   goal S X Y          the squad's own go-to-point, nothing else
//   mstate S on|off|onf                       every member unit's AI state
//   unitgoto S K X Y    one member's go-to-point (an order that does not last)
//   unitstate S K on|off|onf                  one member's AI state
//   reassert S on|off   the relaxed squad's goal hook and the goal re-assert of amove for S (default on)
//   guard S on|off      the merge guard for S: an ordered squad is not dissolved into another (default on)
//   retreat S           the script's Retreat on every member (the control for fallback: it removes the units)
//   detach S K          take member K out of its normal squad: a unit with no squad; it is watched from then on
//   watch S K           a W line per probe tick for member K of S, found again by thing number
//   wgoto W X Y         the go-to-point of the W-th watched unit (0 = the first detached or watched)
//   wstate W on|off|onf the AI state of the W-th watched unit
//   god                 the pilot cannot die (a test level needs its player to live; no squad argument)
//   release S           forget the layer's order for S
// Probe (BEA_PORT_COMMANDER_PROBE=PATH; _EVERY=N ticks, default 1; _MEMBERS=1 adds a line per member unit):
//   T tick present-count event-time-bits rng-value
//   Q ordinal class(N|R) side state defensive forced members x y z centre-x centre-y goal-x goal-y target slot83 script verb members-with-target members-able-to-fire
//   M ordinal k   unit fields (thing x y ai-state has-target dying can-fire ai-mode guide-x guide-y guide-mode)
//   W tick        unit fields, in-squad    a watched unit (or `W tick thing gone`)
//   O tick verb args result            an order and what happened to it
//   A tick ordinal                     a commanded squad's centre came within 4 units of its goal
//   P tick ...                         what the player did on the map (CeCmdNote): `P tick map open|close|select|order ...`
//   # ...                              notes: the level, a refusal, the slot-83 tally, a moved generator
#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "world.h"
#include "squad.h"
#include "unit.h"
#include "WorldPhysicsManager.h"
#include "Guide.h"
#include "map.h"
#include "thing.h"
#include "eventmanager.h"
#include "ce_commander.h"
#ifdef CE_COMMANDER_TEST
#include "SeedUniformRandomNumberStream.h"
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CE_COMMANDER_TEST
// The probe, the tape and the dumps are the test build's own files, read and written where the caller asks: the C library's fopen, not the game's (port.h sends
// game files' fopen through the path layer, which refuses absolute paths).
#undef fopen
extern "C" unsigned PortPresentCount(void);		// d3d9hooks.cpp
#endif
extern "C" int CE_SettingGetBool(const char *key, int def);	// ce.cpp: the ce.ini switch

namespace
{
const int kMaxSquads = 512;
const char *const kFenced[] = {"BEA_PORT_CLOCK", "BEA_PORT_TAPE", "BEA_PORT_PROBES", "BEA_PORT_RECORD",
							   "BEA_PORT_PILOT", "BEA_PORT_AUTOPILOT", "BEA_NET_HOST", "BEA_NET_JOIN"};

enum Verb { V_NONE = 0, V_MOVE, V_AMOVE, V_ATTACK, V_HOLD, V_DEFEND, V_FALLBACK, V_STOP, V_ADVANCE };

// CNormalSquadMember (SquadNormal.cpp, no virtuals): the unit's reader, three floats, the squad. The class is private to that file; this is the one layout read,
// and the checks say what it assumes: a 32-bit build, a reader that is one pointer, and the size the game's own constructor allocates (0x14).
struct MemberView { CActiveReader<CUnit> unit; float a, b, c; CNormalSquad *squad; };
static_assert(sizeof(void *) == 4, "the member record below is the game's 32-bit layout");
static_assert(sizeof(CActiveReader<CUnit>) == 4 && sizeof(float) == 4, "a unit reader is one pointer");
static_assert(sizeof(MemberView) == 0x14 && offsetof(MemberView, unit) == 0, "CNormalSquadMember is 0x14 bytes and starts with the unit's reader");

struct Rec
{
	CSquad *squad;				// NULL once the squad is gone
	int thing;					// its thing number: with the pointer, its identity
	bool alive, seen, normal;
	bool ordered, reassert, arrived, guard;
	Verb verb;
	FVector goal;
	unsigned born, died, orderTick, arriveTick;
	int peak;					// the most live units it has had when the map looked
};

bool gConfigured, gEnabled, gMap;
int gDropped;					// squads of this level the table had no room for
Rec gRecs[kMaxSquads];
int gNRecs;
unsigned gTick;
int gLevel = -1;

#ifdef CE_COMMANDER_TEST
const int kMaxLines = 4096;
const char *const kVerbName[] = {"-", "move", "amove", "attack", "hold", "defend", "fallback", "stop", "advance"};

struct Line
{
	unsigned tick;
	char verb[16], word[16];
	int s, t, k;
	float x, y;
	int nargs;
};

// The units a tape asked to watch (detach, watch): found again each tick by thing number, so a unit that died is not read.
struct Watch { int thing; };
Watch gWatch[16];
int gNWatch;

bool gProof, gMembers;
const char *gTapePath, *gProbePath;
unsigned gEvery = 1;
FILE *gOut;
Line gLines[kMaxLines];
int gNLines, gNext;
int gLastState = -1;
bool gMoved;						// the game's generator moved inside the layer: a bug of ours, reported once
unsigned long long gSlot83[4];		// squad-ticks with slot 83 = 0, 1, 2, other

// CGame::mRandomStream is protected: read through a pointer to the member, named by a struct derived from the game.
struct GameAccess : CGame
{
	static CSeedUniformRandomNumberStream *CGame::*Rng() { return &GameAccess::mRandomStream; }
};

unsigned Bits(float f)
{
	unsigned u;
	memcpy(&u, &f, 4);
	return u;
}

int RngValue()
{
	const CSeedUniformRandomNumberStream *stream = GAME.*GameAccess::Rng();
	return stream ? stream->mValue : 0;
}

void Note(const char *fmt, ...)
{
	if (!gOut) return;
	va_list args;
	va_start(args, fmt);
	fputs("# ", gOut);
	vfprintf(gOut, fmt, args);
	fputc('\n', gOut);
	va_end(args);
	fflush(gOut);
}
#endif

bool Present(const char *name)
{
	const char *value = getenv(name);
	return value && *value;
}

bool Fenced()
{
	for (const char *name : kFenced)
		if (Present(name)) return true;
	return false;
}

void ReadConfiguration()
{
	gConfigured = true;
	// Wanted at all, and allowed here: a replay path refuses it (and the ini is not even read) where a fenced variable is set. In a test build the proof
	// switch is the way past the fence, and BEA_PORT_COMMANDER_MAP=1 under it wants the map without an ini.
	bool proofMap = false;
#ifdef CE_COMMANDER_TEST
	const char *proof = getenv("BEA_PORT_COMMANDER_PROOF");
	gProof = proof && !strcmp(proof, "1");
	gTapePath = getenv("BEA_PORT_COMMANDER_TAPE");
	gProbePath = getenv("BEA_PORT_COMMANDER_PROBE");
	if (gTapePath && !*gTapePath) gTapePath = NULL;
	if (gProbePath && !*gProbePath) gProbePath = NULL;
	if (const char *v = getenv("BEA_PORT_COMMANDER_PROBE_EVERY")) { const long n = strtol(v, NULL, 10); if (n > 0 && n < 1000000) gEvery = unsigned(n); }
	gMembers = getenv("BEA_PORT_COMMANDER_PROBE_MEMBERS") && !strcmp(getenv("BEA_PORT_COMMANDER_PROBE_MEMBERS"), "1");
	const bool allowed = gProof || !Fenced();
	const char *mapEnv = getenv("BEA_PORT_COMMANDER_MAP");
	proofMap = gProof && mapEnv && !strcmp(mapEnv, "1");
#else
	const bool allowed = !Fenced();
#endif
	gMap = allowed && (proofMap || CE_SettingGetBool("commander", 0) != 0);
	gEnabled = allowed && (gMap
#ifdef CE_COMMANDER_TEST
						   || gTapePath || gProbePath
#endif
						   );
}

// ---- the squads ---------------------------------------------------------------------------------------------------------------

bool IsNormal(CSquad *squad) { return !strcmp(squad->_GetClassName(), "CNormalSquad"); }

CNormalSquad *AsNormal(Rec &r) { return (CNormalSquad *)r.squad; }
CRelaxedSquad *AsRelaxed(Rec &r) { return (CRelaxedSquad *)r.squad; }

Rec *Find(CSquad *squad, int thing)
{
	for (int i = 0; i < gNRecs; ++i)
		if (gRecs[i].alive && gRecs[i].squad == squad && gRecs[i].thing == thing) return &gRecs[i];
	return NULL;
}

Rec *ByPointer(const void *squad)
{
	for (int i = 0; i < gNRecs; ++i)
		if (gRecs[i].alive && gRecs[i].squad == squad) return &gRecs[i];
	return NULL;
}

Rec *ByOrdinal(int ordinal)
{
	return ordinal >= 0 && ordinal < gNRecs && gRecs[ordinal].alive ? &gRecs[ordinal] : NULL;
}

// The world's squads in list order (creation order): new ones get the next ordinal, the ones not found any more are marked gone. A squad the table has no
// room for is counted (gDropped) and left alone: it is neither listed nor given orders.
void Scan()
{
	for (int i = 0; i < gNRecs; ++i) gRecs[i].seen = false;
	gDropped = 0;
	ListIterator<CSquad> it(&WORLD.GetSquadNB());
	for (CSquad *squad = it.First(); squad; squad = it.Next())
	{
		const int thing = squad->GetThingNumber();
		Rec *r = Find(squad, thing);
		if (!r)
		{
			if (gNRecs >= kMaxSquads) { ++gDropped; continue; }
			r = &gRecs[gNRecs++];
			memset(r, 0, sizeof(*r));
			r->squad = squad;
			r->thing = thing;
			r->normal = IsNormal(squad);
			r->reassert = true;
			r->guard = true;
			r->born = gTick;
		}
		r->alive = true;
		r->seen = true;
	}
	for (int i = 0; i < gNRecs; ++i)
		if (gRecs[i].alive && !gRecs[i].seen) { gRecs[i].alive = false; gRecs[i].squad = NULL; gRecs[i].died = gTick; }
}

// The live units of a squad, in member order.
int Units(Rec &r, CUnit **out, int max)
{
	int n = 0;
	if (r.normal)
	{
		ListIterator<CNormalSquadMember> it(&AsNormal(r)->mMembers);
		for (CNormalSquadMember *member = it.First(); member && n < max; member = it.Next())
			if (CUnit *unit = ((MemberView *)member)->unit.ToRead())
				if (!unit->IsDying()) out[n++] = unit;
	}
	else
	{
		ListIterator<CActiveReader<CUnit> > it(&AsRelaxed(r)->mMembers);
		for (CActiveReader<CUnit> *member = it.First(); member && n < max; member = it.Next())
			if (CUnit *unit = member->ToRead())
				if (!unit->IsDying()) out[n++] = unit;
	}
	return n;
}

FVector Ground(float x, float y)
{
	FVector p(x, y, 0);
	p.Z = MAP.Collide(p);
	return p;
}

FVector Centre(Rec &r)
{
	CUnit *units[256];
	const int n = Units(r, units, 256);
	if (!n) return r.squad->GetPos();
	FVector c(0, 0, 0);
	for (int i = 0; i < n; ++i) c += units[i]->GetPos();
	c /= (float)n;
	return c;
}

FVector Pilot()
{
	CPlayer *player = GAME.GetPlayer(0);
	CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
	return engine ? engine->GetPos() : FVector(0, 0, 0);
}

// ---- orders ---------------------------------------------------------------------------------------------------------------------

void SetMembers(Rec &r, EAIState state, bool stop)
{
	CUnit *units[256];
	const int n = Units(r, units, 256);
	for (int i = 0; i < n; ++i)
	{
		units[i]->SetAIState(state);
		if (stop) units[i]->Stop();
	}
}

// Every member to its own place: an infantry squad has no goal to hold, and a member that was walking keeps its destination through a stance change and a Stop
// (stopped infantry were measured still walking at 1.8 units a second), so each is sent to where it stands.
void Plant(Rec &r)
{
	CUnit *units[256];
	const int n = Units(r, units, 256);
	for (int i = 0; i < n; ++i) units[i]->GoToPoint(units[i]->GetPos(), FALSE);
}

void Hold(Rec &r, Verb verb, const FVector &goal, CThing *target)
{
	r.ordered = true;
	r.verb = verb;
	r.goal = goal;
	r.orderTick = gTick;
	r.arrived = false;
	if (r.normal)
	{
		CNormalSquad *n = AsNormal(r);
		switch (verb)
		{
		case V_MOVE: case V_FALLBACK: n->SetAIState(AI_OFF); n->GoToPoint(goal, FALSE); break;
		case V_ADVANCE: n->SetAIState(AI_ONF); n->GoToPoint(goal, FALSE); break;
		case V_HOLD: n->GoToPoint(goal, FALSE); n->SetAIState(AI_ONF); break;
#ifdef CE_COMMANDER_TEST
		case V_STOP: n->SetAIState(AI_OFF); n->GoToPoint(goal, FALSE); break;
		case V_AMOVE: n->SetAIState(AI_ON); n->GoToPoint(goal, FALSE); break;
		case V_ATTACK: n->SetAIState(AI_ON); n->Unknown85((SINT)target); break;
		case V_DEFEND: n->SetAIState(AI_ON); n->SetAIState(AI_DEFENSIVE); break;
#endif
		default: break;
		}
	}
	else
	{
		switch (verb)
		{
		case V_MOVE: case V_FALLBACK: SetMembers(r, AI_OFF, false); break;
		case V_ADVANCE: SetMembers(r, AI_ONF, false); break;
		case V_HOLD: SetMembers(r, AI_ONF, false); Plant(r); break;
#ifdef CE_COMMANDER_TEST
		case V_STOP: SetMembers(r, AI_OFF, true); Plant(r); break;
		case V_AMOVE: SetMembers(r, AI_ON, false); break;
		case V_DEFEND: SetMembers(r, AI_ONF, false); Plant(r); break;
		case V_ATTACK:
		{
			CUnit *units[256];
			const int n = Units(r, units, 256);
			CUnit *victim = NULL;
			if (target && (target->GetThingType() & THING_TYPE_UNIT)) victim = (CUnit *)target;
			else if (target && (target->GetThingType() & THING_TYPE_UNKNOWN_20000000)) victim = ((CSquad *)target)->Unknown73();	// a squad: its first unit
			if (victim)
				for (int i = 0; i < n; ++i) units[i]->SetSquadTarget(victim);
			break;
		}
#endif
		default: break;
		}
	}
}

void Reset()
{
	gNRecs = 0;
	gTick = 0;
	gLevel = -1;
	gDropped = 0;
#ifdef CE_COMMANDER_TEST
	gNext = 0;
	gNWatch = 0;
	memset(gSlot83, 0, sizeof(gSlot83));
#endif
}

#ifdef CE_COMMANDER_TEST
// ---- the test build's tape and probe ----------------------------------------------------------------------------------------------

EAIState StateWord(const char *w, bool *ok)
{
	*ok = true;
	if (!strcmp(w, "on")) return AI_ON;
	if (!strcmp(w, "off")) return AI_OFF;
	if (!strcmp(w, "onf")) return AI_ONF;
	if (!strcmp(w, "defensive")) return AI_DEFENSIVE;
	if (!strcmp(w, "normal")) return AI_NORMAL;
	*ok = false;
	return AI_ON;
}

CUnit *FindUnit(int thing)
{
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
		if (t->GetThingNumber() == thing && (t->GetThingType() & THING_TYPE_UNIT)) return (CUnit *)t;
	return NULL;
}

void Result(const Line &l, const char *what)
{
	if (!gOut) return;
	fprintf(gOut, "O %u %s S=%d T=%d K=%d X=%.2f Y=%.2f %s %s\n", gTick, l.verb, l.s, l.t, l.k, l.x, l.y, l.word, what);
	fflush(gOut);
}

// The squad a tape names as a target: the squad T, or the squad of the unit K of T (a script's Attack turns a unit into its squad).
bool TargetOf(const Line &l, bool unitTarget, CThing **target)
{
	Rec *t = ByOrdinal(l.t);
	if (!t) return false;
	if (!unitTarget) { *target = t->squad; return true; }
	CUnit *units[256];
	const int n = Units(*t, units, 256);
	if (l.k < 0 || l.k >= n || !units[l.k]->GetSquad()) return false;
	*target = units[l.k]->GetSquad();
	return true;
}

void Apply(const Line &l)
{
	if (!strcmp(l.verb, "god"))
	{
		CPlayer *player = GAME.GetPlayer(0);
		if (player) player->SetIsGod(TRUE);
		Result(l, player ? "ok" : "refused: no player");
		return;
	}
	if (!strcmp(l.verb, "wgoto") || !strcmp(l.verb, "wstate"))
	{
		CUnit *u = (l.s >= 0 && l.s < gNWatch) ? FindUnit(gWatch[l.s].thing) : NULL;
		if (!u) { Result(l, "refused: no such watched unit"); return; }
		if (l.verb[1] == 'g') { if (l.nargs < 3) { Result(l, "refused: arguments"); return; } u->GoToPoint(Ground(l.x, l.y), FALSE); }
		else { bool good = true; const EAIState state = StateWord(l.word, &good); if (!good) { Result(l, "refused: state"); return; } u->SetAIState(state); }
		Result(l, "ok");
		return;
	}
	Rec *r = ByOrdinal(l.s);
	if (!r) { Result(l, "refused: no such squad"); return; }
	const char *v = l.verb;
	bool ok = true;
	if (!strcmp(v, "move") || !strcmp(v, "amove") || !strcmp(v, "advance"))
	{
		if (l.nargs < 3) { Result(l, "refused: arguments"); return; }
		Hold(*r, !strcmp(v, "move") ? V_MOVE : !strcmp(v, "amove") ? V_AMOVE : V_ADVANCE, Ground(l.x, l.y), NULL);
	}
	else if (!strcmp(v, "attack") || !strcmp(v, "attacku"))
	{
		CThing *target = NULL;
		if (!TargetOf(l, v[6] == 'u', &target)) { Result(l, "refused: no such target"); return; }
		Hold(*r, V_ATTACK, r->squad->GetPos(), target);
	}
	else if (!strcmp(v, "hold")) Hold(*r, V_HOLD, r->squad->GetPos(), NULL);
	else if (!strcmp(v, "defend")) Hold(*r, V_DEFEND, r->squad->GetPos(), NULL);
	else if (!strcmp(v, "stop")) Hold(*r, V_STOP, r->squad->GetPos(), NULL);
	else if (!strcmp(v, "fallback"))
	{
		FVector point(0, 0, 0);
		if (l.nargs >= 3) point = Ground(l.x, l.y);
		else
		{
			CUnit *units[1];
			if (Units(*r, units, 1) == 1) point = units[0]->FindNearestSafeSide();
			if (point.X == 0 && point.Y == 0) point = Pilot();
			if (point.X == 0 && point.Y == 0) { Result(l, "refused: no safe point"); return; }
		}
		Hold(*r, V_FALLBACK, point, NULL);
	}
	else if (!strcmp(v, "state"))
	{
		const EAIState state = StateWord(l.word, &ok);
		if (!ok) { Result(l, "refused: state"); return; }
		r->squad->SetAIState(state);
	}
	else if (!strcmp(v, "goal"))
	{
		if (l.nargs < 3) { Result(l, "refused: arguments"); return; }
		r->squad->GoToPoint(Ground(l.x, l.y), FALSE);
	}
	else if (!strcmp(v, "mstate"))
	{
		const EAIState state = StateWord(l.word, &ok);
		if (!ok) { Result(l, "refused: state"); return; }
		SetMembers(*r, state, false);
	}
	else if (!strcmp(v, "unitgoto") || !strcmp(v, "unitstate"))
	{
		CUnit *units[256];
		const int n = Units(*r, units, 256);
		if (l.k < 0 || l.k >= n) { Result(l, "refused: no such member"); return; }
		if (v[4] == 'g') units[l.k]->GoToPoint(Ground(l.x, l.y), FALSE);
		else { const EAIState state = StateWord(l.word, &ok); if (!ok) { Result(l, "refused: state"); return; } units[l.k]->SetAIState(state); }
	}
	else if (!strcmp(v, "reassert")) r->reassert = strcmp(l.word, "off") != 0;
	else if (!strcmp(v, "guard")) r->guard = strcmp(l.word, "off") != 0;
	else if (!strcmp(v, "retreat"))
	{
		CUnit *units[256];
		const int n = Units(*r, units, 256);
		for (int i = 0; i < n; ++i) units[i]->Unknown100();
	}
	else if (!strcmp(v, "detach"))
	{
		CUnit *units[256];
		const int n = Units(*r, units, 256);
		if (!r->normal || l.k < 0 || l.k >= n) { Result(l, "refused: no such member"); return; }
		if (gNWatch < 16) gWatch[gNWatch++].thing = units[l.k]->GetThingNumber();
		AsNormal(*r)->Unknown4e6f70(units[l.k]);
	}
	else if (!strcmp(v, "watch"))
	{
		CUnit *units[256];
		const int n = Units(*r, units, 256);
		if (l.k < 0 || l.k >= n || gNWatch >= 16) { Result(l, "refused: no such member"); return; }
		gWatch[gNWatch++].thing = units[l.k]->GetThingNumber();
	}
	else if (!strcmp(v, "release")) { r->ordered = false; r->verb = V_NONE; }
	else { Result(l, "refused: verb"); return; }
	Result(l, "ok");
}

void Parse(const char *path)
{
	gNLines = gNext = 0;
	FILE *f = fopen(path, "r");
	if (!f) { Note("tape %s: cannot read", path); return; }
	char text[256];
	while (fgets(text, sizeof(text), f) && gNLines < kMaxLines)
	{
		const char *p = text;
		while (*p == ' ' || *p == '\t') ++p;
		if (!*p || *p == '#' || *p == '\n' || *p == '\r') continue;
		Line &l = gLines[gNLines];
		memset(&l, 0, sizeof(l));
		l.s = l.t = l.k = -1;
		char a[24] = "", b[24] = "", c[24] = "", d[24] = "", e[24] = "";
		const int got = sscanf(p, "%u %15s %23s %23s %23s %23s", &l.tick, l.verb, a, b, c, d);
		(void)e;
		if (got < 2) continue;
		l.s = a[0] ? atoi(a) : -1;
		const char *v = l.verb;
		if (!strcmp(v, "move") || !strcmp(v, "amove") || !strcmp(v, "advance") || !strcmp(v, "goal")) { l.x = (float)atof(b); l.y = (float)atof(c); l.nargs = b[0] && c[0] ? 3 : 1; }
		else if (!strcmp(v, "fallback")) { l.x = (float)atof(b); l.y = (float)atof(c); l.nargs = b[0] && c[0] ? 3 : 1; }
		else if (!strcmp(v, "attack")) { l.t = b[0] ? atoi(b) : -1; }
		else if (!strcmp(v, "attacku")) { l.t = b[0] ? atoi(b) : -1; l.k = c[0] ? atoi(c) : -1; }
		else if (!strcmp(v, "state") || !strcmp(v, "mstate") || !strcmp(v, "reassert") || !strcmp(v, "guard")) { strncpy(l.word, b, sizeof(l.word) - 1); }
		else if (!strcmp(v, "detach") || !strcmp(v, "watch")) { l.k = b[0] ? atoi(b) : -1; }
		else if (!strcmp(v, "wgoto")) { l.x = (float)atof(b); l.y = (float)atof(c); l.nargs = b[0] && c[0] ? 3 : 1; }
		else if (!strcmp(v, "wstate")) { strncpy(l.word, b, sizeof(l.word) - 1); }
		else if (!strcmp(v, "unitgoto")) { l.k = b[0] ? atoi(b) : -1; l.x = (float)atof(c); l.y = (float)atof(d); l.nargs = c[0] && d[0] ? 3 : 1; }
		else if (!strcmp(v, "unitstate")) { l.k = b[0] ? atoi(b) : -1; strncpy(l.word, c, sizeof(l.word) - 1); }
		++gNLines;
	}
	fclose(f);
	Note("tape %s: %d lines", path, gNLines);
}

// Each tick, for each squad the tape holds an order for: amove's goal back after a fight (a normal squad that engaged under AI_ON copied its position into its
// goal and forgot the march), and a note when its centre first comes within 4 units of the goal.
void Maintain()
{
	for (int i = 0; i < gNRecs; ++i)
	{
		Rec &r = gRecs[i];
		if (!r.alive || !r.ordered) continue;
		if (r.normal && r.reassert && r.verb == V_AMOVE)
		{
			CNormalSquad *n = AsNormal(r);
			if (!n->mUnknownC4.ToRead() && (n->mUnknownF4 - r.goal).MagnitudeXY() > 0.5f) n->GoToPoint(r.goal, FALSE);
		}
		if (!r.arrived && (r.verb == V_MOVE || r.verb == V_AMOVE || r.verb == V_ADVANCE || r.verb == V_FALLBACK) && (Centre(r) - r.goal).MagnitudeXY() < 4.0f)
		{
			r.arrived = true;
			r.arriveTick = gTick;
			if (gOut) { fprintf(gOut, "A %u %d\n", gTick, i); fflush(gOut); }
		}
	}
}

// Whether the unit's own AI would fire now: the fire step runs for AI_ONF always and for AI_ON while the unit is active (CUnitAI::HandleEvent),
// and it fires when the unit has a target it can fire at (mCanFireAtTarget, refreshed by the fight step).
bool CanFire(CUnit *u)
{
	if (!u->mAI || !u->mAI->mCanFireAtTarget || !u->mAI->mTarget.ToRead()) return false;
	const EAIState state = u->GetAIState();
	return state == AI_ONF || (state == AI_ON && u->mActive);
}

// A unit's probe fields: thing number, x, y, AI state, has-target, dying, can-fire, AI mode, guide destination x y and mode.
void UnitFields(CUnit *u)
{
	const FVector &p = u->GetPos();
	CGuide *guide = u->mGuide;
	fprintf(gOut, "%d %.3f %.3f %d %d %d %d %d %.3f %.3f %d", u->GetThingNumber(), p.X, p.Y, (int)u->GetAIState(), u->GetCurrentTarget() ? 1 : 0,
			u->IsDying() ? 1 : 0, CanFire(u) ? 1 : 0, u->mAI ? (int)u->mAI->mAIMode : -1, guide ? guide->mDestination.X : -1.f,
			guide ? guide->mDestination.Y : -1.f, guide ? (int)guide->GetMode() : -1);
}

void Probe()
{
	if (!gOut) return;
	const bool due = gTick % gEvery == 0;
	for (int i = 0; i < gNRecs; ++i)
	{
		Rec &r = gRecs[i];
		if (!r.alive) continue;
		const int s83 = r.squad->Unknown83();
		gSlot83[s83 == 0 ? 0 : s83 == 1 ? 1 : s83 == 2 ? 2 : 3]++;
	}
	if (!due) return;
	fprintf(gOut, "T %u %u %08x %d\n", gTick, PortPresentCount(), Bits(EVENT_MANAGER.GetTime()), RngValue());
	for (int i = 0; i < gNRecs; ++i)
	{
		Rec &r = gRecs[i];
		if (!r.alive) continue;
		CUnit *units[256];
		const int n = Units(r, units, 256);
		const FVector &pos = r.squad->GetPos();
		const FVector c = Centre(r);
		int state = -1, def = 0, forced = 0, script = 0;
		float gx = -1, gy = -1;
		char target[24] = "-";
		if (r.normal)
		{
			CNormalSquad *q = AsNormal(r);
			state = (int)q->mUnknown120;
			def = q->mUnknown9C;
			forced = q->mUnknownCC;
			script = q->mUnknown84;
			gx = q->mUnknownF4.X;
			gy = q->mUnknownF4.Y;
			if (CThing *t = q->mUnknownC4.ToRead())
			{
				if (t->GetThingType() & THING_TYPE_UNKNOWN_20000000)
				{
					Rec *tr = ByPointer(t);
					snprintf(target, sizeof(target), "S%d", tr ? (int)(tr - gRecs) : -1);
				}
				else
					snprintf(target, sizeof(target), "U%d", t->GetThingNumber());
			}
		}
		else
		{
			CRelaxedSquad *q = AsRelaxed(r);
			def = q->mUnknown9C;
			script = q->mUnknown84;
		}
		int withTarget = 0, firing = 0;
		for (int k = 0; k < n; ++k)
		{
			if (units[k]->GetCurrentTarget()) ++withTarget;
			if (CanFire(units[k])) ++firing;
		}
		fprintf(gOut, "Q %d %c %d %d %d %d %d %.3f %.3f %.3f %.3f %.3f %.3f %.3f %s %d %d %s %d %d\n", i, r.normal ? 'N' : 'R', (int)r.squad->mUnknown7C, state, def, forced,
				n, pos.X, pos.Y, pos.Z, c.X, c.Y, gx, gy, target, r.squad->Unknown83(), script, r.ordered ? kVerbName[r.verb] : "-", withTarget, firing);
		if (gMembers)
			for (int k = 0; k < n; ++k)
			{
				fprintf(gOut, "M %d %d ", i, k);
				UnitFields(units[k]);
				fputc('\n', gOut);
			}
	}
	for (int w = 0; w < gNWatch; ++w)
	{
		CUnit *u = FindUnit(gWatch[w].thing);
		if (!u) { fprintf(gOut, "W %u %d gone\n", gTick, gWatch[w].thing); continue; }
		fprintf(gOut, "W %u ", gTick);
		UnitFields(u);
		fprintf(gOut, " %d\n", u->GetSquad() ? 1 : 0);
	}
	if (gTick % (gEvery * 100) == 0)
		fprintf(gOut, "# slot83 tally (squad-ticks): 0=%llu 1=%llu 2=%llu other=%llu\n", gSlot83[0], gSlot83[1], gSlot83[2], gSlot83[3]);
	fflush(gOut);
}
#endif

}

// ---- the hooks ----------------------------------------------------------------------------------------------------------------------

void CeCommanderUpdate()
{
	if (!gConfigured) ReadConfiguration();
	if (!gEnabled) return;
	const int state = GAME.GetGameState();
#ifdef CE_COMMANDER_TEST
	if (state != gLastState)
	{
		if (gOut)
		{
			Note("game state %d to %d at tick %u, Present %u", gLastState, state, gTick, PortPresentCount());
			Note("slot83 tally (squad-ticks): 0=%llu 1=%llu 2=%llu other=%llu", gSlot83[0], gSlot83[1], gSlot83[2], gSlot83[3]);
		}
		gLastState = state;
	}
#endif
	if (state == GAME_STATE_NOT_RUNNING || state == GAME_STATE_PRE_RUNNING) { Reset(); return; }
	if (state != GAME_STATE_PLAYING || GAME.IsPaused()) return;
	if (GAME.IsMultiplayer() || GAME.GetNPlayers() != 1) return;
#ifdef CE_COMMANDER_TEST
	if (!gOut && gProbePath)
	{
		gOut = fopen(gProbePath, "w");
		if (gOut) { fprintf(gOut, "# commander probe 1\n"); fflush(gOut); }
	}
#endif
	if (gLevel != GAME.GetCurrentLevel())
	{
		gLevel = GAME.GetCurrentLevel();
		gTick = 0;
		gNRecs = 0;
#ifdef CE_COMMANDER_TEST
		gNWatch = 0;
		Note("level %d", gLevel);
		if (gTapePath) Parse(gTapePath);
#endif
	}
#ifdef CE_COMMANDER_TEST
	const int before = RngValue();
#endif
	++gTick;
	Scan();
#ifdef CE_COMMANDER_TEST
	while (gNext < gNLines && gLines[gNext].tick <= gTick) Apply(gLines[gNext++]);
	Maintain();
	Probe();
	if (RngValue() != before && !gMoved)
	{
		gMoved = true;
		Note("the game's generator moved inside the layer at tick %d: a bug", (int)gTick);
	}
#endif
}

bool CeCommanderNoMerge(const void *squad)
{
	if (!gEnabled || !gNRecs) return false;
	Rec *r = ByPointer(squad);
	return r && r->ordered && r->guard;
}

int CeCommanderRelaxed(void *squad, FVector *pos)
{
	if (!gEnabled || !gNRecs) return 0;
	Rec *r = ByPointer(squad);
	if (!r || !r->ordered || !r->reassert) return 0;
	switch (r->verb)
	{
	case V_MOVE: case V_AMOVE: case V_ADVANCE: case V_FALLBACK: *pos = r->goal; return 1;
	case V_HOLD:
#ifdef CE_COMMANDER_TEST
	case V_DEFEND: case V_STOP:
#endif
		return 2;
	default: return 0;
	}
}

// ---- the map's face -----------------------------------------------------------------------------------------------------------------

namespace
{
int PlayerAllegiance()
{
	CPlayer *player = GAME.GetPlayer(0);
	CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
	return engine ? (int)engine->GetAllegiance() : -1;
}

int ToCmd(Verb v)
{
	switch (v)
	{
	case V_MOVE: return CE_CMD_MOVE;
	case V_ADVANCE: return CE_CMD_ADVANCE;
	case V_HOLD: return CE_CMD_HOLD;
	case V_FALLBACK: return CE_CMD_FALLBACK;
	default: return CE_CMD_NONE;
	}
}

// How close the members' centre has to come for a march to count as arrived: a vehicle squad stops within a few units of its goal, a relaxed squad's own
// arrival test is 20 (its members within that of its centre stay).
float ArriveRadius(const Rec &r) { return r.normal ? 6.0f : 20.0f; }

// A unit's type as the map sorts it: ground units it can march (vehicles, mechs, infantry, jeeps), and everything else.
bool Marches(CUnit *u)
{
	const CUnitData *d = u->GetUnitData();
	if (!d || (u->GetThingType() & THING_TYPE_AIR_UNIT)) return false;
	switch (d->mBehaviour)
	{
	case UB_MECH: case UB_GROUND_UNIT: case UB_INFANTRY: case UB_JEEP: return true;
	default: return false;
	}
}

void Describe(int index, Rec &r, int allegiance, CeCmdSquad &s)
{
	CUnit *units[256];
	const int n = Units(r, units, 256);
	bool scripted = r.squad->GetMissionScript() != NULL, objective = false, scanner = false, unmarchable = false, deploys = false;
	float cx = 0, cy = 0;
	for (int k = 0; k < n; ++k)
	{
		if (units[k]->GetMissionScript()) scripted = true;
		if (units[k]->IsObjective()) objective = true;
		if (units[k]->OnScanner()) scanner = true;
		if (!Marches(units[k])) unmarchable = true;
		if (const CUnitData *d = units[k]->GetUnitData()) if (d->mUnknown108 || (d->mName && strstr(d->mName, "Artillery"))) deploys = true;	// artillery keeps its place while its squad's centre jumps to the goal
		cx += units[k]->GetPos().X;
		cy += units[k]->GetPos().Y;
	}
	if (n > r.peak) r.peak = n;
	s.ordinal = index;
	s.vehicles = r.normal;
	s.members = n;
	s.peak = r.peak;
	s.x = n ? cx / (float)n : r.squad->GetPos().X;
	s.y = n ? cy / (float)n : r.squad->GetPos().Y;
	s.friendly = (int)r.squad->mUnknown7C == allegiance;
	s.why = !s.friendly ? CE_WHY_ENEMY : scripted ? CE_WHY_SCRIPTED : r.squad->mUnknown84 ? CE_WHY_SPAWNED : unmarchable ? CE_WHY_AIRCRAFT :
			objective ? CE_WHY_OBJECTIVE : CE_WHY_NONE;
	s.commandable = s.friendly && n > 0 && s.why == CE_WHY_NONE;
	s.deploys = deploys;
	s.unit[0] = 0;
	if (n)
		if (const CUnitData *d = units[0]->GetUnitData())
			if (d->mName) snprintf(s.unit, sizeof(s.unit), "%s", d->mName);
	s.visible = s.friendly || scanner;
	s.verb = r.ordered ? ToCmd(r.verb) : CE_CMD_NONE;
	s.gx = r.goal.X;
	s.gy = r.goal.Y;
	s.arrived = r.ordered && (r.verb == V_MOVE || r.verb == V_ADVANCE || r.verb == V_FALLBACK) &&
				(FVector(s.x, s.y, 0) - FVector(s.gx, s.gy, 0)).MagnitudeXY() < ArriveRadius(r);
}
}

bool CeCmdMapOn()
{
	if (!gConfigured) ReadConfiguration();
	return gMap;
}

int CeCmdUntracked() { return gDropped; }

int CeCmdList(CeCmdSquad *out, int max)
{
	if (!CeCmdMapOn() || !gEnabled) return 0;
	Scan();
	const int allegiance = PlayerAllegiance();
	int count = 0;
	for (int i = 0; i < gNRecs && count < max; ++i)
	{
		if (!gRecs[i].alive) continue;
		CeCmdSquad s;
		Describe(i, gRecs[i], allegiance, s);
		if (s.members > 0) out[count++] = s;
	}
	return count;
}

bool CeCmdIssue(int ordinal, int verb, float x, float y, const char **why)
{
	const char *reason = NULL;
	if (!why) why = &reason;
	*why = NULL;
	Rec *r = CeCmdMapOn() && gEnabled ? ByOrdinal(ordinal) : NULL;
	if (!r) { *why = "that squad is gone"; return false; }
	CeCmdSquad s;
	Describe(ordinal, *r, PlayerAllegiance(), s);
	if (!s.commandable) { *why = "that squad does not take orders"; return false; }
	if (s.deploys && verb != CE_CMD_HOLD) { *why = "artillery deploys where it stands: it takes Hold only"; return false; }
	const char *name = "?";
	switch (verb)
	{
	case CE_CMD_MOVE: name = "move"; Hold(*r, V_MOVE, Ground(x, y), NULL); break;
	case CE_CMD_ADVANCE: name = "advance"; Hold(*r, V_ADVANCE, Ground(x, y), NULL); break;
	case CE_CMD_HOLD: name = "hold"; Hold(*r, V_HOLD, Ground(s.x, s.y), NULL); break;
	case CE_CMD_FALLBACK:
	{
		name = "fallback";
		FVector point(0, 0, 0);
		CUnit *units[1];
		if (Units(*r, units, 1) == 1) point = units[0]->FindNearestSafeSide();
		if (point.X == 0 && point.Y == 0) point = Pilot();
		if (point.X == 0 && point.Y == 0) { *why = "there is no safe place to fall back to"; return false; }
		// The nearest safe side can lie outside the occupied cells (it is where a script's Retreat sends units off the map): the squad is sent to the nearest
		// point 12 units inside them, so it never marches off the map.
		if (SExtents *e = MAP.GetExtents())
			if (e->mMaxX > e->mMinX + 24 && e->mMaxY > e->mMinY + 24)
			{
				if (point.X < float(e->mMinX + 12)) point.X = float(e->mMinX + 12);
				if (point.X > float(e->mMaxX - 12)) point.X = float(e->mMaxX - 12);
				if (point.Y < float(e->mMinY + 12)) point.Y = float(e->mMinY + 12);
				if (point.Y > float(e->mMaxY - 12)) point.Y = float(e->mMaxY - 12);
				point = Ground(point.X, point.Y);
			}
		x = point.X;
		y = point.Y;
		Hold(*r, V_FALLBACK, point, NULL);
		break;
	}
	default: *why = "that is not an order"; return false;
	}
#ifdef CE_COMMANDER_TEST
	if (gOut)
	{
		fprintf(gOut, "O %u %s S=%d T=-1 K=-1 X=%.2f Y=%.2f - map\n", gTick, name, ordinal, x, y);
		fflush(gOut);
	}
#else
	(void)name;
#endif
	return true;
}

#ifdef CE_COMMANDER_TEST
void CeCmdWorldHash(CeCmdHash *out)
{
	unsigned all = 2166136261u, solid = 2166136261u, things = 0, components = 0;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
	{
		const bool component = (t->GetThingType() & THING_TYPE_COMPONENT) != 0;
		++things;
		if (component) ++components;
		const FVector &p = t->GetPos();
		unsigned words[6] = {(unsigned)t->GetThingNumber(), Bits(p.X), Bits(p.Y), Bits(p.Z), 0, 0};
		if (t->GetThingType() & THING_TYPE_UNIT)
		{
			words[4] = Bits(((CUnit *)t)->GetLife());
			words[5] = ((CUnit *)t)->IsDying() ? 1u : 0u;
		}
		unsigned name = 2166136261u;
		for (const char *c = t->_GetClassName(); c && *c; ++c) name = (name ^ (unsigned char)*c) * 16777619u;
		for (unsigned w : words) { all = (all ^ w) * 16777619u; if (!component) solid = (solid ^ w) * 16777619u; }
		all = (all ^ name) * 16777619u;
		if (!component) solid = (solid ^ name) * 16777619u;
	}
	out->all = all;
	out->withoutComponents = solid;
	out->things = things;
	out->components = components;
	out->eventTime = Bits(EVENT_MANAGER.GetTime());
}

void CeCmdWorldDump(const char *path)
{
	FILE *f = fopen(path, "w");
	if (!f) return;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
	{
		const FVector &p = t->GetPos();
		fprintf(f, "%d %s %08x %08x %08x", (int)t->GetThingNumber(), t->_GetClassName(), Bits(p.X), Bits(p.Y), Bits(p.Z));
		if (t->GetThingType() & THING_TYPE_UNIT) fprintf(f, " life %08x dying %d", Bits(((CUnit *)t)->GetLife()), ((CUnit *)t)->IsDying() ? 1 : 0);
		fputc('\n', f);
	}
	fclose(f);
}

void CeCmdNote(const char *fmt, ...)
{
	if (!gOut) return;
	va_list args;
	va_start(args, fmt);
	fprintf(gOut, "P %u %u ", gTick, PortPresentCount());
	vfprintf(gOut, fmt, args);
	fputc('\n', gOut);
	va_end(args);
	fflush(gOut);
}
#endif

#endif
