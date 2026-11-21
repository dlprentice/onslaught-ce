// SPDX-License-Identifier: GPL-3.0-only
// Commander mode: the small decisions the order layer and the match make, as pure functions, so that a check can hold each of them without a game run
// (port/tools/ce_cmdorders_driver.cpp runs them, and ce_cmdorders_tests.py runs mutants of them). No game types; the C library's square root only.
//
// The verbs are numbered as ce_commander.h's CeCmdVerb numbers them; ce_commander.cpp asserts that the two lists agree.
#ifndef PORT_CE_CMDRULES_H
#define PORT_CE_CMDRULES_H

#include <math.h>

enum
{
	CE_RULE_NONE = 0,
	CE_RULE_MOVE,
	CE_RULE_ADVANCE,
	CE_RULE_HOLD,
	CE_RULE_FALLBACK,
	CE_RULE_RELEASE
};

// A march with a goal to reach: Move, Advance and Fall back.
inline bool CeCmdMarches(int verb) { return verb == CE_RULE_MOVE || verb == CE_RULE_ADVANCE || verb == CE_RULE_FALLBACK; }

// A march that holds its fire on the way (Move and Fall back). Every one of them becomes a Hold where it arrives, so that the squad defends itself there;
// an Advance fights on the way and keeps its goal once there.
inline bool CeCmdHoldsOnArrival(int verb, bool arrived) { return arrived && (verb == CE_RULE_MOVE || verb == CE_RULE_FALLBACK); }

// Several squads given one march stand round its goal, each in a place of its own: Move, Advance and Fall back. Hold keeps each where it is, and Release
// has no place.
inline bool CeCmdSpreads(int verb, int squads) { return squads > 1 && CeCmdMarches(verb); }

// A Commander match (ce_cmdmatch.h): a versus level (the game reports a multiplayer level with its two players) that one person started straight from the
// command line's -level, so that nobody else is at the controls; never a network game. A versus level chosen in the game's own Multiplayer menu has no -level
// (cliLevel is -1) and is two people's game.
inline bool CeCmdMatchRule(bool network, bool multiplayer, int players, int cliLevel, int level)
{
	return !network && multiplayer && players == 2 && cliLevel >= 0 && cliLevel == level;
}

// The match's alerts (ce_cmdmap.cpp's MatchFrame, once an update while the live view is open): what the commander is told when the battle turns
// against him, one alert at a time, the gravest first. A building of his side destroyed; his Battle Engine hurt (below half its life) or badly hurt
// (below a quarter), each said once until the life comes back above that mark; his side's buildings losing life (under attack), said at most once
// in kCeCmdAlertQuiet updates (20 seconds at the game's 20 updates a second). The first update after a reset only records how things stand.
const unsigned kCeCmdAlertQuiet = 400;
enum { CE_ALERT_NONE = 0, CE_ALERT_BUILDING_LOST, CE_ALERT_ENGINE_BADLY_HURT, CE_ALERT_BASE_HIT, CE_ALERT_ENGINE_HURT };
struct CeCmdAlertState
{
	bool primed;
	int buildings;		// the side's live buildings at the last update
	float baseLife;		// their life, summed
	int hurt;			// 0, 1 below half, 2 below a quarter: the mark already said
	unsigned lastBaseHit;	// the update the last "under attack" was said at
};

inline void CeCmdAlertReset(CeCmdAlertState &a) { a.primed = false; a.buildings = 0; a.baseLife = 0.f; a.hurt = 0; a.lastBaseHit = 0; }

inline int CeCmdAlertStep(CeCmdAlertState &a, int buildings, float baseLife, float life, float lifeFull, unsigned tick)
{
	const unsigned quiet = kCeCmdAlertQuiet;
	const int hurt = lifeFull > 0.f && life > 0.f ? (life < lifeFull * 0.25f ? 2 : life < lifeFull * 0.5f ? 1 : 0) : 0;
	if (!a.primed)
	{
		a.primed = true; a.buildings = buildings; a.baseLife = baseLife; a.hurt = hurt; a.lastBaseHit = tick;
		return CE_ALERT_NONE;
	}
	int alert = CE_ALERT_NONE;
	if (buildings < a.buildings) { alert = CE_ALERT_BUILDING_LOST; a.lastBaseHit = tick; }
	else if (hurt > a.hurt) alert = hurt == 2 ? CE_ALERT_ENGINE_BADLY_HURT : CE_ALERT_ENGINE_HURT;
	else if (baseLife < a.baseLife - 0.001f && tick - a.lastBaseHit >= quiet) { alert = CE_ALERT_BASE_HIT; a.lastBaseHit = tick; }
	if (!(hurt > a.hurt && alert == CE_ALERT_BUILDING_LOST)) a.hurt = hurt;		// a mark passed in the update of a loss is said in the next
	a.buildings = buildings;
	a.baseLife = baseLife;
	return alert;
}

// The live view's return in a match (MatchFrame, once an update): when the commander's Battle Engine goes down while the view is open, the view closes
// and waits for it (CE_REOPEN_WAIT: the note says so); once the engine is back in the field and the view could open, it opens (CE_REOPEN_OPEN). A view
// the player closed himself (M or Esc), in the same update the engine went down or before, does not wait. Outside a match, or once the battle is over,
// nothing waits. `wanted` is the waiting, kept by the caller.
enum { CE_REOPEN_NONE = 0, CE_REOPEN_WAIT, CE_REOPEN_OPEN };
inline int CeCmdReopenStep(bool &wanted, bool wasOpen, bool open, bool match, bool pilotUp, bool playerClosed, bool canOpen)
{
	if (!match) { wanted = false; return CE_REOPEN_NONE; }
	if (wasOpen && !open && !pilotUp && !playerClosed) { wanted = true; return CE_REOPEN_WAIT; }
	if (wanted && !open && pilotUp && canOpen) { wanted = false; return CE_REOPEN_OPEN; }
	return CE_REOPEN_NONE;
}

// The open key while the commander's Battle Engine is down in a match: it answers, and it asks for the view (or stops asking) when the engine is back.
// True when the key was the reopen's; false where the open key keeps its ordinary meaning (no match, or the engine is in the field).
inline bool CeCmdReopenKey(bool &wanted, bool match, bool pilotUp)
{
	if (!match || pilotUp) return false;
	wanted = !wanted;
	return true;
}

// Where a Fall back goes: home. The caller lists the places its side holds (each with whether the ground there is land): its live buildings
// (kind 0) and the battle-line nodes it holds (kind 1). The squad goes to the nearest building on land, or where its side has none, to the nearest
// node on land; -1 when no place is on land (the caller then has nowhere to send it). Never the edge a script's Retreat sends units off by.
enum { CE_PLACE_BUILDING = 0, CE_PLACE_NODE = 1 };
struct CeCmdPlace { float x, y; int kind; bool land; };
inline int CeCmdFallbackPick(const CeCmdPlace *places, int n, float sx, float sy)
{
	int best = -1, bestKind = 2;
	float bestD = 0.f;
	for (int i = 0; i < n; ++i)
	{
		if (!places[i].land || places[i].kind < CE_PLACE_BUILDING || places[i].kind > CE_PLACE_NODE) continue;
		const float dx = places[i].x - sx, dy = places[i].y - sy, d = dx * dx + dy * dy;
		if (places[i].kind < bestKind || (places[i].kind == bestKind && d < bestD)) { best = i; bestKind = places[i].kind; bestD = d; }
	}
	return best;
}

// The squads' place at home: `standoff` units out from the place it chose, on the side that faces the battle (towards `towardX, towardY`: the other
// side's base, or where the squad stands when the caller knows none), so that squads gather in front of their base and not on its buildings.
inline void CeCmdFallbackRally(float px, float py, float towardX, float towardY, float standoff, float *rx, float *ry)
{
	const float dx = towardX - px, dy = towardY - py, d = sqrtf(dx * dx + dy * dy);
	const float away = d > 1e-3f && standoff > 0.f ? (standoff < d ? standoff / d : 1.f) : 0.f;		// never past the point it faces
	*rx = px + dx * away;
	*ry = py + dy * away;
}

#endif
