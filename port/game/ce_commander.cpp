// SPDX-License-Identifier: GPL-3.0-only
// Commander mode, the order layer. It keeps a table of the level's squads and gives them the orders the commander map (ce_cmdmap.cpp) asks for, the way the
// mission scripts give theirs: a state (AI_OFF, AI_ONF) and a go-to-point on the squad, or on each member of an infantry squad. The verbs are the ones
// measured to hold against the squads' own AI: Move (AI_OFF and a goal: passive, and the goal is never overwritten), Advance (AI_ONF and a goal: fights on the
// way; AI_ON loses the goal as soon as the squad has a target, which is always while an enemy exists), Hold (AI_ONF at the members' centre: it stays and fires) and
// Fall back (Move to the nearest own-side safe point). An order from the map is followed once an update (Follow): what the squad is doing is worked out
// (ce_cmdoutcome.h) and a Move or a Fall back that has arrived becomes a Hold where it stands; Release puts back the state and goal the squad had before its
// first order. A Stop that left the squad passive (AI_OFF at the members' centre) was measured too and stays in the test
// module: a squad that does not defend itself is a trap for the player. Infantry squads have no goal of their own, so patch 0160 lets this layer set the
// squad's centre, or leave the members where they are; a squad with an order must not be dissolved into another, which patch 0161 refuses.
//
// It is part of every CE build and off unless ce.ini says commander=1 (CeCmdMapOn): never under a replay, a recording, probes, a virtual clock, a test pilot or a
// network game (kFenced), in a multiplayer level that is not a Commander match (ce_cmdmatch.h: a versus level one person plays), or outside a level that is playing. It draws no random number, writes nothing to a career or the options, and allocates
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
//   harm F              the pilot's Battle Engine takes F damage (no squad argument)
//   harmbase F [K]      the commander's side's live buildings take F damage each (or only the K-th of them, 0 the first the world lists)
//   release S           forget the layer's order for S
//   ui WORD ARGS        what the player does on the open map, with an exact pointer (ce_cmdmap.cpp, CeCmdTestUi): click S | shiftclick S | order X Y (a right
//                       click: Advance) | move X Y (Ctrl and a right click) | advance X Y (A, then a click) | box X0 Y0 X1 Y1 (a drag between two world points) |
//                       at X Y BUTTON (a window point; BUTTON 0 left, 1 middle, 2 right). The result names the window point used.
//                    marks lists the drawn member targets without selecting anything.
// Probe (BEA_PORT_COMMANDER_PROBE=PATH; _EVERY=N ticks, default 1; _MEMBERS=1 adds a line per member unit):
//   T tick present-count event-time-bits rng-value
//   Q ordinal class(N|R) side state defensive forced members x y z centre-x centre-y goal-x goal-y target slot83 script verb members-with-target members-able-to-fire
//   M ordinal k   unit fields (thing x y ai-state has-target dying can-fire ai-mode guide-x guide-y guide-mode)
//   W tick        unit fields, in-squad    a watched unit (or `W tick thing gone`)
//   O tick verb args result            an order and what happened to it
//   A tick ordinal                     a commanded squad's centre came within 4 units of its goal
//   P tick ...                         what the player did on the map (CeCmdNote): `P tick map open|close|select|order ...`
//   # ...                              notes: the level, a refusal, the slot-83 tally, a moved generator
// With BEA_PORT_COMMANDER_PROBE_MATCH=1, after each tick's Q lines, what the map shows and how the battle stands:
//   S ordinal friendly commandable why deploys vehicles members peak verb arrived outcome unit-name    a squad as the map describes it (CeCmdSquad;
//                       why is CeCmdWhy, outcome the word for what it is doing under its order, 0 for none)
//   E tick game-state match result side nodes held lost my-buildings their-buildings my-guns their-guns my-units their-units pilot life x y heading-x heading-y
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
#include "InfluenceMap.h"
#include "ce_commander.h"
#include "ce_cmdorders.h"
#include "ce_cmdproduction.h"
#include "ce_cmdmatch.h"
#include "ce_cmdoutcome.h"
#include "ce_cmdrules.h"
static_assert(CE_RULE_MOVE == CE_CMD_MOVE && CE_RULE_ADVANCE == CE_CMD_ADVANCE && CE_RULE_HOLD == CE_CMD_HOLD && CE_RULE_FALLBACK == CE_CMD_FALLBACK &&
				  CE_RULE_RELEASE == CE_CMD_RELEASE, "ce_cmdrules.h numbers the verbs as ce_commander.h does");
#ifdef CE_COMMANDER_TEST
#include "SeedUniformRandomNumberStream.h"
#endif

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CE_COMMANDER_TEST) && defined(__linux__)
// A test build keeps the order layer's own records (its squad table above all, which the live view refreshes every frame) in the Commander's
// sections, which the view's footprint audit leaves out (ce_cmdview_audit.cpp), as ce_cmdmap.cpp's, ce_cmdview.cpp's and ce_cmdorders.cpp's are.
#pragma clang section bss = "cecmdb" data = "cecmdd"
#endif

#ifdef CE_COMMANDER_TEST
// The probe, the tape and the dumps are the test build's own files, read and written where the caller asks: the C library's fopen, not the game's (port.h sends
// game files' fopen through the path layer, which refuses absolute paths).
#undef fopen
extern "C" unsigned PortPresentCount(void);		// d3d9hooks.cpp
#endif
extern "C" int CE_SettingGetBool(const char *key, int def);	// ce.cpp: the ce.ini switch
bool PortCeExperimental(const char *mode);	// ce.cpp: the ce.ini [Experimental] lines, each read once

namespace
{
bool gExplicitUnitReplacement = false;
struct ExplicitUnitReplacement {
    bool previous;
    ExplicitUnitReplacement() : previous(gExplicitUnitReplacement) {
        gExplicitUnitReplacement = true;
    }
    ~ExplicitUnitReplacement() { gExplicitUnitReplacement = previous; }
};

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
	bool fromMap;				// the standing order came from the map (CeCmdIssue): it is followed (Follow) and every march of it ends in Hold
	CeCmdTrack track;			// how its march is going (ce_cmdoutcome.h)
	int outcome;				// CeCmdOutcome, for the map's card
	bool kept;					// ownState and ownGoal hold what the squad had before its first order (Release puts them back)
	EAIState ownState;
	FVector ownGoal;
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
	float ui[4];		// the numbers of a ui line (nargs of them)
};

// The units a tape asked to watch (detach, watch): found again each tick by thing number, so a unit that died is not read.
struct Watch { int thing; };
Watch gWatch[16];
int gNWatch;

bool gProof, gMembers, gMatchLines;
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
	gMatchLines = getenv("BEA_PORT_COMMANDER_PROBE_MATCH") && !strcmp(getenv("BEA_PORT_COMMANDER_PROBE_MATCH"), "1");
	const bool allowed = gProof || !Fenced();
	const char *mapEnv = getenv("BEA_PORT_COMMANDER_MAP");
	proofMap = gProof && mapEnv && !strcmp(mapEnv, "1");
#else
	const bool allowed = !Fenced();
#endif
	// The map is one of the unfinished features: ce.ini's commander=1 (the line under [Experimental], or the same line from 0.1.13), read once in ce.cpp.
	gMap = allowed && (proofMap || PortCeExperimental("commander"));
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

// Land: a point inside the height field whose ground stands above the water (the height grows downwards, as the live picture's colours read it).
bool Land(float x, float y)
{
	if (!isfinite(x) || !isfinite(y) || x < 0.f || y < 0.f || x >= float(MAP.mGridWidth - 1) || y >= float(MAP.mGridHeight - 1)) return false;
	return MAP.GetHeight(SINT(x), SINT(y)) <= MAP.GetWaterLevel();
}

int PlayerAllegiance();		// below

// A Fall back's home (ce_cmdrules.h's CeCmdFallbackPick and CeCmdFallbackRally): of the commander's side's live buildings and the battle-line nodes it
// holds, the nearest building on land (else node), and the rally 24 units out from it towards the other side's buildings (or towards the squad, where
// the other side has none), or nearer it where that is water. Kept inside the occupied cells. Reads only; false when the side holds no place on land.
bool Home(float sx, float sy, FVector *home, FVector *rally)
{
	const int side = PlayerAllegiance();
	if (side != 0 && side != 1) return false;
	const int kMax = 192;
	CeCmdPlace places[kMax];
	int n = 0, others = 0;
	float ox = 0.f, oy = 0.f;
	ListIterator<CThing> it(&WORLD.GetThingNB());
	for (CThing *t = it.First(); t; t = it.Next())
	{
		if (!(t->GetThingType() & THING_TYPE_BUILDING) || !(t->GetThingType() & THING_TYPE_UNIT)) continue;
		CUnit *b = (CUnit *)t;
		const int allegiance = (int)b->GetAllegiance();
		if (b->IsDying() || (allegiance != 0 && allegiance != 1)) continue;
		const float x = b->GetPos().X, y = b->GetPos().Y;
		if (allegiance != side) { ox += x; oy += y; ++others; continue; }
		if (n < kMax) places[n++] = CeCmdPlace{x, y, CE_PLACE_BUILDING, Land(x, y)};
	}
	if (INFLUENCEMAP)
	{
		ListIterator<CInfluenceNode> nodes(&INFLUENCEMAP->mNodes);
		for (CInfluenceNode *node = nodes.First(); node && n < kMax; node = nodes.Next())
		{
			const int holder = node->mSide == 2 ? 0 : node->mSide == 1 ? 1 : -1;		// as the level's script counts the battle line (ce_cmdmatch.cpp)
			if (holder == side) places[n++] = CeCmdPlace{node->mX, node->mY, CE_PLACE_NODE, Land(node->mX, node->mY)};
		}
	}
	const int pick = CeCmdFallbackPick(places, n, sx, sy);
	if (pick < 0) return false;
	const float tx = others ? ox / float(others) : sx, ty = others ? oy / float(others) : sy;
	float rx = places[pick].x, ry = places[pick].y;
	for (float standoff = 24.f; standoff > 0.f; standoff -= 8.f)
	{
		float cx, cy;
		CeCmdFallbackRally(places[pick].x, places[pick].y, tx, ty, standoff, &cx, &cy);
		if (Land(cx, cy)) { rx = cx; ry = cy; break; }
	}
	if (SExtents *e = MAP.GetExtents())
		if (e->mMaxX > e->mMinX + 24 && e->mMaxY > e->mMinY + 24)
		{
			if (rx < float(e->mMinX + 12)) rx = float(e->mMinX + 12);
			if (rx > float(e->mMaxX - 12)) rx = float(e->mMaxX - 12);
			if (ry < float(e->mMinY + 12)) ry = float(e->mMinY + 12);
			if (ry > float(e->mMaxY - 12)) ry = float(e->mMaxY - 12);
		}
	*home = Ground(places[pick].x, places[pick].y);
	*rally = Ground(rx, ry);
	return true;
}

// Whether the unit's own AI would fire now: the fire step runs for AI_ONF always and for AI_ON while the unit is active (CUnitAI::HandleEvent),
// and it fires when the unit has a target it can fire at (mCanFireAtTarget, refreshed by the fight step). Reads only.
bool CanFire(CUnit *u)
{
	if (!u->mAI || !u->mAI->mCanFireAtTarget || !u->mAI->mTarget.ToRead()) return false;
	const EAIState state = u->GetAIState();
	return state == AI_ONF || (state == AI_ON && u->mActive);
}

// ---- orders ---------------------------------------------------------------------------------------------------------------------

void ReleaseMemberOrders(Rec& r) {
    if (!gExplicitUnitReplacement) return;
    CUnit* members[256];
    const int count = Units(r, members, 256);
    for (int i = 0; i < count; ++i) CeCmdUnitOrderRelease(members[i]);
}

void SetMembers(Rec &r, EAIState state, bool stop)
{
    ReleaseMemberOrders(r);
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
    // Release temporary per-unit destinations before an accepted explicit
    // squad command writes native state or points. Routine Follow is unscoped.
    ReleaseMemberOrders(r);
	// What the squad had before its first order, for Release: a vehicle squad's own state and goal, an infantry squad's members' state (it has no goal of its own).
	if (!r.ordered && !r.kept)
	{
		r.kept = true;
		r.ownGoal = r.squad->GetPos();
		r.ownState = AI_ON;
		if (r.normal) { r.ownState = AsNormal(r)->GetAIState(); r.ownGoal = AsNormal(r)->mUnknownF4; }
		else
		{
			CUnit *units[1];
			if (Units(r, units, 1) == 1) r.ownState = units[0]->GetAIState();
		}
	}
	r.ordered = true;
	r.fromMap = false;
	r.verb = verb;
	r.goal = goal;
	r.orderTick = gTick;
	r.arrived = false;
	CeTrackStart(r.track, (Centre(r) - goal).MagnitudeXY(), gTick);
	r.outcome = verb == V_HOLD ? CE_OUT_HOLDING : CE_OUT_MOVING;
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

void Follow();		// below, after the map's face (it needs the arrival radius)

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
    ExplicitUnitReplacement replacingUnits;
	if (!strcmp(l.verb, "god"))
	{
		CPlayer *player = GAME.GetPlayer(0);
		if (player) player->SetIsGod(TRUE);
		Result(l, player ? "ok" : "refused: no player");
		return;
	}
	if (!strcmp(l.verb, "harm"))		// harm F: the commander's Battle Engine takes F damage (the match's alerts and the view's return after a loss)
	{
		CPlayer *player = GAME.GetPlayer(0);
		CBattleEngine *engine = player ? player->GetBattleEngine() : NULL;
		if (!engine || l.nargs < 1 || !isfinite(l.x) || l.x <= 0.f) { Result(l, engine ? "refused: arguments" : "refused: no Battle Engine"); return; }
		engine->Damage(l.x, engine);
		Result(l, "ok");
		return;
	}
	if (!strcmp(l.verb, "harmbase"))		// harmbase F [K]: the commander's side's buildings take F damage (the match's base alerts)
	{
		const int side = PlayerAllegiance();
		if (side < 0 || l.nargs < 1 || !isfinite(l.x) || l.x <= 0.f) { Result(l, side < 0 ? "refused: no Battle Engine" : "refused: arguments"); return; }
		int seen = 0, hit = 0;
		ListIterator<CThing> it(&WORLD.GetThingNB());
		for (CThing *th = it.First(); th; th = it.Next())
		{
			if (!(th->GetThingType() & THING_TYPE_BUILDING) || !(th->GetThingType() & THING_TYPE_UNIT)) continue;
			CUnit *b = (CUnit *)th;
			if (b->IsDying() || (int)b->GetAllegiance() != side) continue;
			if (l.k < 0 || seen == l.k) { b->Damage(l.x, b); ++hit; }
			++seen;
		}
		Result(l, hit ? "ok" : "refused: no such building");
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
	if (!strcmp(l.verb, "ui"))
	{
		char said[96];
		CeCmdTestUi(l.word, l.ui, l.nargs, said, sizeof(said));
		Result(l, said);
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
		const int got = sscanf(p, "%u %15s %23s %23s %23s %23s %23s", &l.tick, l.verb, a, b, c, d, e);
		if (got < 2) continue;
		if (!strcmp(l.verb, "ui"))
		{
			strncpy(l.word, a, sizeof(l.word) - 1);
			const char *const number[4] = {b, c, d, e};
			for (l.nargs = 0; l.nargs < 4 && number[l.nargs][0]; ++l.nargs) l.ui[l.nargs] = (float)atof(number[l.nargs]);
			++gNLines;
			continue;
		}
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
		else if (!strcmp(v, "harm")) { l.s = -1; l.x = (float)atof(a); l.nargs = a[0] ? 1 : 0; }
		else if (!strcmp(v, "harmbase")) { l.s = -1; l.x = (float)atof(a); l.nargs = a[0] ? 1 : 0; l.k = b[0] ? atoi(b) : -1; }
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

// A unit's probe fields: thing number, x, y, AI state, has-target, dying, can-fire, AI mode, guide destination x y and mode.
void UnitFields(CUnit *u)
{
	const FVector &p = u->GetPos();
	CGuide *guide = u->mGuide;
	fprintf(gOut, "%d %.3f %.3f %d %d %d %d %d %.3f %.3f %d", u->GetThingNumber(), p.X, p.Y, (int)u->GetAIState(), u->GetCurrentTarget() ? 1 : 0,
			u->IsDying() ? 1 : 0, CanFire(u) ? 1 : 0, u->mAI ? (int)u->mAI->mAIMode : -1, guide ? guide->mDestination.X : -1.f,
			guide ? guide->mDestination.Y : -1.f, guide ? (int)guide->GetMode() : -1);
}

void MatchLines();		// below, after the map's face (it writes the squads as the map describes them)

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
	if (gMatchLines) MatchLines();
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
	// A Commander match has its one screen from the level's first update, before the opening pan is drawn (ce_cmdmatch.cpp).
	if (state == GAME_STATE_PRE_RUNNING || state == GAME_STATE_PANNING || state == GAME_STATE_PLAYING) CeCmdMatchUpdate();
	if (state == GAME_STATE_NOT_RUNNING || state == GAME_STATE_PRE_RUNNING) { Reset(); CeFactoryReset(); return; }
	if (state != GAME_STATE_PLAYING || GAME.IsPaused()) return;
	if (!CeCmdAdmits(!GAME.IsMultiplayer() && GAME.GetNPlayers() == 1)) return;
#ifdef CE_COMMANDER_TEST
	if (!gOut && gProbePath)
	{
		gOut = fopen(gProbePath, "w");
		if (gOut) { fprintf(gOut, "# commander probe 1\n"); fflush(gOut); }
	}
#endif
    CeFactoryTick(GAME.GetCurrentLevel(), PlayerAllegiance(), EVENT_MANAGER.GetTime());
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
	int before = RngValue();
#endif
	++gTick;
	Scan();
#ifdef CE_COMMANDER_TEST
	while (gNext < gNLines && gLines[gNext].tick <= gTick)
	{
		const Line &l = gLines[gNext++];
		// The test verbs that damage the game's things (harm, harmbase) run the game's own damage, which draws from its generator as any hit does: the check
		// below is made up to them and starts again after them, so that what they draw is not reported as the layer's and the check still sees the rest.
		const bool damage = !strcmp(l.verb, "harm") || !strcmp(l.verb, "harmbase");
		if (damage && RngValue() != before && !gMoved)
		{
			gMoved = true;
			Note("the game's generator moved inside the layer at tick %d: a bug", (int)gTick);
		}
		Apply(l);
		if (damage && RngValue() != before) { Note("tape %s at tick %d: the game's own damage drew from its generator", l.verb, (int)gTick); before = RngValue(); }
	}
	Maintain();
#endif
	Follow();
#ifdef CE_COMMANDER_TEST
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

// A march that has come no nearer its goal for this many updates (ten seconds), with nobody in it able to fire, is stuck.
const unsigned kStuckTicks = 200;

// Once an update, for each squad with an order from the map: what it is doing (ce_cmdoutcome.h), and a Move or a Fall back that has arrived becomes a Hold
// where the squad stands, so that it defends itself there. Orders from a test build's tape are left as they are.
void Follow()
{
	for (int i = 0; i < gNRecs; ++i)
	{
		Rec &r = gRecs[i];
		if (!r.alive || !r.ordered || !r.fromMap) continue;
		CUnit *units[256];
		const int n = Units(r, units, 256);
		if (!n) continue;
		FVector c(0, 0, 0);
		int engaged = 0;
		for (int k = 0; k < n; ++k)
		{
			c += units[k]->GetPos();
			if (CanFire(units[k])) ++engaged;
		}
		c /= (float)n;
		const int verb = ToCmd(r.verb);
		r.outcome = CeTrackStep(r.track, CeCmdMarches(verb), (c - r.goal).MagnitudeXY(), ArriveRadius(r), engaged, gTick, kStuckTicks);
		if (CeCmdHoldsOnArrival(verb, r.track.arrived))		// the rule: ce_cmdrules.h
		{
			Hold(r, V_HOLD, Ground(c.X, c.Y), NULL);
			r.fromMap = true;
			r.outcome = engaged ? CE_OUT_FIGHTING : CE_OUT_HOLDING;
#ifdef CE_COMMANDER_TEST
			if (gOut) { fprintf(gOut, "O %u hold S=%d T=-1 K=-1 X=%.2f Y=%.2f - arrived\n", gTick, i, c.X, c.Y); fflush(gOut); }
#endif
		}
	}
}

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
	bool crossesWater = n > 0;
	float cx = 0, cy = 0, lifeSum = 0.f, maxLifeSum = 0.f;
	for (int k = 0; k < n; ++k)
	{
		if (units[k]->GetMissionScript()) scripted = true;
		if (units[k]->IsObjective()) objective = true;
		if (units[k]->OnScanner()) scanner = true;
		if (!Marches(units[k])) unmarchable = true;
		if (!(units[k]->GetThingType() & (THING_TYPE_NAVAL | THING_TYPE_AIR_UNIT))) crossesWater = false;
		if (const CUnitData *d = units[k]->GetUnitData()) if (d->mUnknown108 || (d->mName && strstr(d->mName, "Artillery"))) deploys = true;	// artillery keeps its place while its squad's centre jumps to the goal
		cx += units[k]->GetPos().X;
		cy += units[k]->GetPos().Y;
		const float uLife = units[k]->GetLife();
		const float uMax = units[k]->GetMaxLife();
		if (uLife > 0.f) lifeSum += uLife;
		if (uMax > 0.f) maxLifeSum += uMax;
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
	s.crossesWater = crossesWater;
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
	s.outcome = r.ordered && r.fromMap ? r.outcome : CE_OUT_NONE;
	s.selected = false;
	s.life = lifeSum;
	s.maxLife = maxLifeSum;
}
}

#ifdef CE_COMMANDER_TEST
namespace
{
void MatchLines()
{
	const int allegiance = PlayerAllegiance();
	for (int i = 0; i < gNRecs; ++i)
	{
		if (!gRecs[i].alive) continue;
		CeCmdSquad s;
		Describe(i, gRecs[i], allegiance, s);
		if (s.members <= 0) continue;
		fprintf(gOut, "S %d %d %d %d %d %d %d %d %d %d %d %s\n", s.ordinal, s.friendly ? 1 : 0, s.commandable ? 1 : 0, s.why, s.deploys ? 1 : 0, s.vehicles ? 1 : 0,
				s.members, s.peak, s.verb, s.arrived ? 1 : 0, s.outcome, s.unit[0] ? s.unit : "-");
	}
	CeCmdMatchState m;
	CeCmdMatchRead(&m);
	fprintf(gOut, "E %u %d %d %d %d %d %d %d %d %d %d %d %d %d %d %.3f %.3f %.3f %.5f %.5f %.1f %.1f %.1f %.1f\n", gTick, m.gameState, m.match ? 1 : 0, m.result, m.side,
			m.nodes, m.held, m.lost, m.buildings[0], m.buildings[1], m.guns[0], m.guns[1], m.units[0], m.units[1], m.pilot ? 1 : 0, m.life, m.x, m.y, m.headingX,
			m.headingY, m.baseX0, m.baseY0, m.baseX1, m.baseY1);
}
}
#endif

bool CeCmdMapOn()
{
	if (!gConfigured) ReadConfiguration();
	return gMap;
}

int CeCmdDrawnSquad(const CSquad *squad)
{
	Rec *r = squad ? ByPointer(squad) : NULL;
	return r && r->thing == r->squad->GetThingNumber() ? int(r - gRecs) : -1;
}

int CeCmdDeliveredSquad() {
    CUnit *unit = CeFactoryLiveVehicle();
    CSquad *squad = unit ? unit->GetSquad() : nullptr;
    const int ordinal = CeCmdDrawnSquad(squad);
    Rec *record = ordinal >= 0 ? ByOrdinal(ordinal) : nullptr;
    if (!record || !record->alive || !record->normal) return -1;
    CUnit *members[2] = {};
    if (Units(*record, members, 2) != 1 || members[0] != unit) return -1;
    CeCmdSquad info{};
    Describe(ordinal, *record, PlayerAllegiance(), info);
    return info.commandable ? ordinal : -1;
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

bool CeCmdMarchLand(float x, float y)
{
	return Land(x, y);
}

bool CeCmdIssue(int ordinal, int verb, float x, float y, const char **why)
{
    ExplicitUnitReplacement replacingUnits;
	const char *reason = NULL;
	if (!why) why = &reason;
	*why = NULL;
	// Only point orders consume caller coordinates. Hold chooses its own place and a fall back its own destination, unless it is given a finite point
	// that is not the origin (the map's places for several squads round the safe point they chose); reject malformed point orders before any lookup,
	// selection description or terrain query can have side effects. The reasons are the sentences the map shows.
	switch (verb) {
	case CE_CMD_MOVE:
	case CE_CMD_ADVANCE:
		if (!isfinite(x) || !isfinite(y)) {
			*why = "Point at the ground.";
			return false;
		}
		break;
	case CE_CMD_HOLD:
	case CE_CMD_FALLBACK:
	case CE_CMD_RELEASE:
		break;
	default:
		*why = "That is not an order.";
		return false;
	}
	Rec *r = CeCmdMapOn() && gEnabled ? ByOrdinal(ordinal) : NULL;
	if (!r) { *why = "That squad is lost."; return false; }
	CeCmdSquad s;
	Describe(ordinal, *r, PlayerAllegiance(), s);
	if (!s.commandable) { *why = "That squad takes no orders."; return false; }
	if (s.deploys && verb != CE_CMD_HOLD && verb != CE_CMD_RELEASE) { *why = "Artillery can only hold."; return false; }
	if (verb == CE_CMD_RELEASE && !r->ordered) { *why = "Already on its own orders."; return false; }
	if ((verb == CE_CMD_MOVE || verb == CE_CMD_ADVANCE) && !s.crossesWater && !CeCmdMarchLand(x, y)) {
		*why = "Choose land for ground units.";
		return false;
	}
	const char *name = "?";
	switch (verb)
	{
	case CE_CMD_MOVE: name = "move"; Hold(*r, V_MOVE, Ground(x, y), NULL); break;
	case CE_CMD_ADVANCE: name = "advance"; Hold(*r, V_ADVANCE, Ground(x, y), NULL); break;
	case CE_CMD_HOLD: name = "hold"; Hold(*r, V_HOLD, Ground(s.x, s.y), NULL); break;
	case CE_CMD_RELEASE:
		// The layer forgets its order, and the squad gets back the state (and, for a vehicle squad, the goal) it had before its first order. A goal or a
		// waypoint its own AI or a script gave it later is not known here.
		name = "release";
        ReleaseMemberOrders(*r);
		x = s.x;
		y = s.y;
		if (r->kept)
		{
			if (r->normal)
			{
				CNormalSquad *n = AsNormal(*r);
				if (r->ownGoal.X != 0.f || r->ownGoal.Y != 0.f) n->GoToPoint(r->ownGoal, FALSE);
				n->SetAIState(r->ownState);
			}
			else SetMembers(*r, r->ownState, false);
		}
		r->ordered = false;
		r->fromMap = false;
		r->verb = V_NONE;
		r->arrived = false;
		r->outcome = CE_OUT_NONE;
		break;
	case CE_CMD_FALLBACK:
	{
		// Home (Home: the nearest building of the commander's side, else a battle-line node it holds, the rally out in front of it on land), or a place the
		// map chose round that rally for one of several squads, kept where it is land. The pilot is the last resort; never the edge a script's Retreat
		// sends units off the map by.
		name = "fallback";
		FVector home(0, 0, 0), point(0, 0, 0);
		const bool homed = Home(s.x, s.y, &home, &point);
		if (isfinite(x) && isfinite(y) && (x != 0.f || y != 0.f) && Land(x, y)) point = Ground(x, y);
		else if (!homed)
		{
			point = Pilot();
			if ((point.X == 0 && point.Y == 0) || !Land(point.X, point.Y)) { *why = "There is nowhere to fall back to."; return false; }
			home = point;
		}
		x = point.X;
		y = point.Y;
#ifdef CE_COMMANDER_TEST
		CeCmdNote("map fallback %d home %.1f %.1f goal %.1f %.1f from %.1f %.1f height %.3f water %.3f", ordinal, home.X, home.Y, x, y, s.x, s.y,
				  MAP.GetHeight(SINT(x), SINT(y)), MAP.GetWaterLevel());
#endif
		Hold(*r, V_FALLBACK, point, NULL);
		break;
	}
	default: *why = "That is not an order."; return false;
	}
	if (verb != CE_CMD_RELEASE) r->fromMap = true;		// followed from now on (Follow): a Move or a Fall back holds where it arrives
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

namespace {
bool UnitAuthority(CUnit* unit, Rec& record, const CeCmdSquad& squad,
                   int allegiance, CeCmdUnitAuthority* out) {
    // A reader can outlive a membership transfer. It is not command authority.
    if (!unit || !record.alive || !record.squad || record.squad->IsShuttingDown() || unit->IsShuttingDown() ||
        unit->GetSquad() != record.squad || unit->IsDying() ||
        unit->GetLife() <= 0) return false;
    *out = CeCmdUnitAuthority{};
    out->found = true;
    out->squad = record.squad;
    out->ordinal = squad.ordinal;
    out->number = unit->GetThingNumber();
    out->kind = static_cast<unsigned>(unit->GetThingType());
    out->friendly = squad.friendly && (int)unit->GetAllegiance() == allegiance;
    out->scripted = squad.why == CE_WHY_SCRIPTED || unit->GetMissionScript();
    out->why = !out->friendly ? CE_WHY_ENEMY :
        out->scripted ? CE_WHY_SCRIPTED : squad.why;
    out->commandable = out->friendly && !out->scripted && squad.commandable;
    const unsigned refused = THING_TYPE_AIR_UNIT | THING_TYPE_NAVAL |
        THING_TYPE_BUILDING | THING_TYPE_COMPONENT |
        THING_TYPE_EMPLACEMENT | THING_TYPE_MECH;
    out->groundPoint = (out->kind &
        (THING_TYPE_INFANTRY | THING_TYPE_GROUND_UNIT)) != 0 &&
        (out->kind & refused) == 0;
    return true;
}
}

void CeCmdSquadGone(const void* departed) {
    if (!departed) return;
    for (int i = 0; i < gNRecs; ++i) {
        Rec& record = gRecs[i];
        if (record.squad != departed) continue;
        record.alive = false;
        record.squad = NULL;
    }
}

bool CeCmdUnitAuthorityNow(CUnit* unit, CeCmdUnitAuthority* out) {
    if (!out) return false;
    *out = CeCmdUnitAuthority{};
    if (!unit || !CeCmdUnitOrdersEligible() || !CeCmdMapOn() || !gEnabled)
        return false;
    Scan();
    const int allegiance = PlayerAllegiance();
    for (int i = 0; i < gNRecs; ++i) {
        if (!gRecs[i].alive || !gRecs[i].squad || gRecs[i].squad->IsShuttingDown()) continue;
        CUnit* members[256];
        const int count = Units(gRecs[i], members, 256);
        for (int j = 0; j < count; ++j) {
            if (members[j] != unit) continue;
            CeCmdSquad squad = {};
            Describe(i, gRecs[i], allegiance, squad);
            if (UnitAuthority(unit, gRecs[i], squad, allegiance, out)) return true;
        }
    }
    return false;
}

int CeCmdUnitCollectNative(CeCmdUnitNativeRecord* out, int capacity, bool* complete) {
    if (complete) *complete = false;
    if (!out || capacity <= 0 || !complete || !CeCmdUnitOrdersEligible() ||
        !CeCmdMapOn() || !gEnabled) return 0;
    Scan();
    const int allegiance = PlayerAllegiance();
    int accepted = 0;
    bool all = gDropped == 0;
    for (int i = 0; i < gNRecs; ++i) {
        Rec& record = gRecs[i];
        if (!record.alive || !record.squad || record.squad->IsShuttingDown()) continue;
        CeCmdSquad squad = {};
        Describe(i, record, allegiance, squad);
        if (!squad.friendly) continue;
        CUnit* members[256];
        const int count = Units(record, members, 256);
        // The bounded native reader cannot prove completeness at its limit.
        if (count == 256) all = false;
        for (int j = 0; j < count; ++j) {
            CeCmdUnitAuthority authority = {};
            CUnit* unit = members[j];
            if (!UnitAuthority(unit, record, squad, allegiance, &authority) ||
                !authority.friendly) continue;
            bool duplicate = false;
            for (int k = 0; k < accepted; ++k)
                if (out[k].unit == unit) duplicate = true;
            if (duplicate) continue;
            if (accepted == capacity) { all = false; continue; }
            CeCmdUnitNativeRecord& item = out[accepted++];
            item = CeCmdUnitNativeRecord{};
            item.unit = unit;
            item.authority = authority;
            item.squadNumber = record.squad->GetThingNumber();
            item.squadKind = static_cast<unsigned>(record.squad->GetThingType());
            item.members = squad.members;
            const FVector position = unit->GetPos();
            item.position[0] = position.X;
            item.position[1] = position.Y;
            item.position[2] = position.Z;
            const CUnitData* data = unit->GetUnitData();
            snprintf(item.name, sizeof(item.name), "%s",
                data && data->mName ? data->mName : "Unit");
            item.life = unit->GetLife();
            item.maxLife = unit->GetMaxLife();
        }
    }
    *complete = all;
    return accepted;
}

bool CeCmdUnitGroundPoint(float x, float y, float out[3]) {
    if (!out || !isfinite(x) || !isfinite(y) || !CeCmdMarchLand(x, y))
        return false;
    const FVector point = Ground(x, y);
    if (!isfinite(point.X) || !isfinite(point.Y) || !isfinite(point.Z))
        return false;
    out[0] = point.X;
    out[1] = point.Y;
    out[2] = point.Z;
    return true;
}

void CeCmdCountLiveUnits(int pilotAlleg, int *outFriendly, int *outTotal) {
    int liveFriendly = 0, liveTotal = 0;
    ListIterator<CUnit> units(&WORLD.GetUnitNB());
    for (CUnit *u = units.First(); u; u = units.Next()) {
        if (u->IsDying() || u->IsShuttingDown() || u->GetLife() <= 0.f) continue;
        ++liveTotal;
        if (int(u->GetAllegiance()) == pilotAlleg) ++liveFriendly;
    }
    if (outFriendly) *outFriendly = liveFriendly;
    if (outTotal) *outTotal = liveTotal;
}

#endif
