// SPDX-License-Identifier: GPL-3.0-only
// Runs ce_cmdsel.h, ce_cmdoutcome.h and ce_cmdrules.h (ce_cmdorders_tests.py compiles and runs this, against the headers and against mutants of them): the selection
// keeps its order and its lead, refuses what it cannot hold, and drops what is gone; the numbered groups keep copies; the drag's rectangle takes marks
// whichever way it was dragged; and a standing order's word follows the squad's distance from its goal and whether it can fire.
#include <ce_cmdsel.h>
#include <ce_cmdoutcome.h>
#include <ce_cmdrules.h>

#include <stdio.h>

static int failures;

static void expect(bool ok, const char *what)
{
	if (ok) return;
	printf("FAIL %s\n", what);
	++failures;
}

static void selection()
{
	CeCmdSel s;
	CeSelClear(s);
	expect(s.n == 0 && CeSelLead(s) == -1, "an empty selection has no lead");
	expect(CeSelAdd(s, 7) && CeSelAdd(s, 3) && CeSelAdd(s, 11), "three squads join");
	expect(s.n == 3 && CeSelLead(s) == 7, "the first one picked is the lead");
	expect(s.ids[0] == 7 && s.ids[1] == 3 && s.ids[2] == 11, "squads stay in the order they were picked");
	expect(!CeSelAdd(s, 3) && s.n == 3, "a squad joins once");
	expect(!CeSelAdd(s, -1) && s.n == 3, "a number that is no squad's does not join");
	expect(CeSelHas(s, 11) && !CeSelHas(s, 12), "membership is by number");
	expect(CeSelRemove(s, 7) && s.n == 2 && CeSelLead(s) == 3 && s.ids[1] == 11, "when the lead leaves the next one leads and the rest keep their order");
	expect(!CeSelRemove(s, 7) && s.n == 2, "a squad that is not in cannot leave");
	expect(!CeSelToggle(s, 3) && s.n == 1 && CeSelLead(s) == 11, "a Shift click on a picked squad drops it");
	expect(CeSelToggle(s, 3) && s.n == 2 && s.ids[1] == 3, "a Shift click on another squad adds it at the end");
	CeSelOnly(s, 5);
	expect(s.n == 1 && CeSelLead(s) == 5, "a plain click picks that squad alone");
	CeSelOnly(s, -1);
	expect(s.n == 0, "a plain click on nothing picks nothing");

	for (int i = 0; i < kCeSelMax; ++i) expect(CeSelAdd(s, 100 + i), "the selection fills");
	expect(!CeSelAdd(s, 999) && s.n == kCeSelMax, "a full selection takes no more");
	expect(!CeSelToggle(s, 998) && s.n == kCeSelMax && !CeSelHas(s, 998), "a Shift click on a full selection adds nothing");
	const int gone = CeSelPrune(s, [](int id) { return id % 2 == 0; });
	expect(gone == kCeSelMax / 2 && s.n == kCeSelMax / 2, "half the squads are gone and drop out");
	bool ordered = true;
	for (int i = 0; i < s.n; ++i)
		if (s.ids[i] != 100 + 2 * i) ordered = false;
	expect(ordered, "the squads that stay keep their order");
	expect(CeSelPrune(s, [](int) { return true; }) == 0 && s.n == kCeSelMax / 2, "nothing drops when everything is still there");
	expect(CeSelPrune(s, [](int) { return false; }) == kCeSelMax / 2 && s.n == 0 && CeSelLead(s) == -1, "everything drops when nothing is");
}

static void groups()
{
	CeCmdGroups g;
	CeGroupsClear(g);
	CeCmdSel s, back;
	CeSelClear(s);
	CeSelClear(back);
	CeSelAdd(back, 42);
	expect(!CeGroupRecall(g, 1, back) && back.n == 1 && back.ids[0] == 42, "an empty group leaves the selection as it is");
	CeSelAdd(s, 4);
	CeSelAdd(s, 9);
	expect(CeGroupStore(g, 1, s), "a selection is kept under a number");
	CeSelAdd(s, 20);
	expect(CeGroupRecall(g, 1, back) && back.n == 2 && back.ids[0] == 4 && back.ids[1] == 9, "the group is a copy: a later pick does not change it");
	expect(!CeGroupStore(g, kCeSelGroups, s) && !CeGroupStore(g, -1, s), "a number outside the groups keeps nothing");
	expect(!CeGroupRecall(g, kCeSelGroups, back) && !CeGroupRecall(g, -1, back) && back.n == 2, "and recalls nothing");
	expect(CeGroupStore(g, 9, s) && CeGroupRecall(g, 9, back) && back.n == 3, "the last group works as the first");
	expect(CeGroupRecall(g, 1, back) && back.n == 2, "groups do not share their squads");
	CeSelClear(s);
	expect(CeGroupStore(g, 1, s) && !CeGroupRecall(g, 1, back), "keeping an empty selection empties the group");
	CeGroupsClear(g);
	expect(!CeGroupRecall(g, 9, back), "clearing empties every group");
}

static void box()
{
	expect(!CeBoxIsDrag(100, 100, 103, 97, 6), "a press and a release three pixels apart is a click");
	expect(CeBoxIsDrag(100, 100, 106, 100, 6) && CeBoxIsDrag(100, 100, 100, 94, 6), "six pixels along either axis is a drag");
	expect(CeBoxIsDrag(100, 100, 94, 100, 6) && CeBoxIsDrag(100, 100, 100, 106, 6), "in either direction");
	expect(CeBoxHas(10, 10, 50, 40, 30, 20), "a mark inside the rectangle is taken");
	expect(CeBoxHas(50, 40, 10, 10, 30, 20) && CeBoxHas(10, 40, 50, 10, 30, 20) && CeBoxHas(50, 10, 10, 40, 30, 20), "whichever way it was dragged");
	expect(CeBoxHas(10, 10, 50, 40, 10, 10) && CeBoxHas(10, 10, 50, 40, 50, 40), "a mark on the edge is taken");
	expect(!CeBoxHas(10, 10, 50, 40, 9, 20) && !CeBoxHas(10, 10, 50, 40, 51, 20), "a mark left or right of it is not");
	expect(!CeBoxHas(10, 10, 50, 40, 30, 9) && !CeBoxHas(10, 10, 50, 40, 30, 41), "nor one above or below");
}

static void outcome()
{
	const unsigned stuck = 300;
	CeCmdTrack t = {0.f, 0u, false};
	// holding
	CeTrackStart(t, 0.f, 10);
	expect(CeTrackStep(t, false, 0.f, 6.f, 0, 11, stuck) == CE_OUT_HOLDING, "a quiet hold is Holding");
	expect(CeTrackStep(t, false, 50.f, 6.f, 2, 12, stuck) == CE_OUT_FIGHTING, "a hold that can fire is Fighting");
	expect(CeTrackStep(t, false, 0.f, 6.f, 0, 100000, stuck) == CE_OUT_HOLDING, "a hold is never Stuck");
	// a march that gets there
	CeTrackStart(t, 100.f, 0);
	expect(CeTrackStep(t, true, 100.f, 6.f, 0, 1, stuck) == CE_OUT_MOVING, "a new march is Moving");
	expect(CeTrackStep(t, true, 60.f, 6.f, 0, 200, stuck) == CE_OUT_MOVING, "a march that gets nearer is Moving");
	expect(CeTrackStep(t, true, 60.f, 6.f, 0, 499, stuck) == CE_OUT_MOVING, "it has not stopped for long enough yet");
	expect(CeTrackStep(t, true, 60.f, 6.f, 0, 500, stuck) == CE_OUT_STUCK, "a march that came no nearer for the whole wait is Stuck");
	expect(CeTrackStep(t, true, 59.5f, 6.f, 0, 501, stuck) == CE_OUT_STUCK, "half a unit is not progress");
	expect(CeTrackStep(t, true, 58.f, 6.f, 0, 502, stuck) == CE_OUT_MOVING, "a unit and more is: it is Moving again");
	expect(CeTrackStep(t, true, 6.f, 6.f, 0, 600, stuck) == CE_OUT_MOVING, "at the radius it is not there yet");
	expect(CeTrackStep(t, true, 5.9f, 6.f, 0, 601, stuck) == CE_OUT_ARRIVED, "inside the radius it has Arrived");
	expect(CeTrackStep(t, true, 30.f, 6.f, 0, 5000, stuck) == CE_OUT_ARRIVED, "and stays Arrived when it drifts, however long");
	expect(CeTrackStep(t, true, 30.f, 6.f, 1, 5001, stuck) == CE_OUT_FIGHTING, "an arrived squad that can fire is Fighting");
	expect(CeTrackStep(t, true, 30.f, 6.f, 0, 5002, stuck) == CE_OUT_ARRIVED, "and Arrived again when the fight is over");
	// a new order after an arrival starts afresh
	CeTrackStart(t, 40.f, 6000);
	expect(!t.arrived && CeTrackStep(t, true, 40.f, 6.f, 0, 6001, stuck) == CE_OUT_MOVING, "a new order forgets that the last one arrived");
	// a fight on the way
	CeTrackStart(t, 100.f, 0);
	expect(CeTrackStep(t, true, 100.f, 6.f, 3, 290, stuck) == CE_OUT_FIGHTING, "a march that can fire is Fighting");
	expect(CeTrackStep(t, true, 100.f, 6.f, 3, 1000, stuck) == CE_OUT_FIGHTING, "a long fight is never Stuck");
	expect(CeTrackStep(t, true, 100.f, 6.f, 0, 1299, stuck) == CE_OUT_MOVING, "after the fight the wait starts again");
	expect(CeTrackStep(t, true, 100.f, 6.f, 0, 1300, stuck) == CE_OUT_STUCK, "and runs out as before");
	// a new order starts afresh
	CeTrackStart(t, 40.f, 2000);
	expect(!t.arrived && CeTrackStep(t, true, 40.f, 6.f, 0, 2001, stuck) == CE_OUT_MOVING, "a new order forgets the old one's end");
	expect(CeTrackStep(t, true, 80.f, 6.f, 0, 2299, stuck) == CE_OUT_MOVING && CeTrackStep(t, true, 80.f, 6.f, 0, 2300, stuck) == CE_OUT_STUCK,
		   "a squad pushed away from its goal is Stuck after the wait, measured from the order");
}

// Which orders march, which of them hold where they arrive, which spread, and which level is a Commander match.
static void rules()
{
	expect(CeCmdMarches(CE_RULE_MOVE) && CeCmdMarches(CE_RULE_ADVANCE) && CeCmdMarches(CE_RULE_FALLBACK), "Move, Advance and Fall back march");
	expect(!CeCmdMarches(CE_RULE_HOLD) && !CeCmdMarches(CE_RULE_RELEASE) && !CeCmdMarches(CE_RULE_NONE), "Hold, Release and no order do not");
	expect(CeCmdHoldsOnArrival(CE_RULE_MOVE, true), "a Move that has arrived becomes a Hold");
	expect(CeCmdHoldsOnArrival(CE_RULE_FALLBACK, true), "a Fall back that has arrived becomes a Hold");
	expect(!CeCmdHoldsOnArrival(CE_RULE_MOVE, false) && !CeCmdHoldsOnArrival(CE_RULE_FALLBACK, false), "not before it has arrived");
	expect(!CeCmdHoldsOnArrival(CE_RULE_ADVANCE, true), "an Advance keeps its own order where it arrives");
	expect(!CeCmdHoldsOnArrival(CE_RULE_HOLD, true) && !CeCmdHoldsOnArrival(CE_RULE_RELEASE, true), "a Hold or a Release is not turned into another Hold");
	expect(CeCmdSpreads(CE_RULE_MOVE, 2) && CeCmdSpreads(CE_RULE_ADVANCE, 5) && CeCmdSpreads(CE_RULE_FALLBACK, 2), "several squads on a march spread out");
	expect(!CeCmdSpreads(CE_RULE_MOVE, 1) && !CeCmdSpreads(CE_RULE_FALLBACK, 1), "one squad goes to the point itself");
	expect(!CeCmdSpreads(CE_RULE_HOLD, 4) && !CeCmdSpreads(CE_RULE_RELEASE, 4), "a Hold or a Release has no place to spread round");
	expect(CeCmdMatchRule(false, true, 2, 851, 851), "a versus level started by -level is a match");
	expect(!CeCmdMatchRule(false, true, 2, -1, 851), "a versus level from the Multiplayer menu is two people's game");
	expect(!CeCmdMatchRule(false, true, 2, 851, 852), "a later level than the one -level started is not");
	expect(!CeCmdMatchRule(true, true, 2, 851, 851), "a network game is never one");
	expect(!CeCmdMatchRule(false, false, 1, 232, 232), "a campaign level started by -level is not a match");
	expect(!CeCmdMatchRule(false, false, 2, 232, 232), "nor a level the game does not call multiplayer, whatever its player count");
	expect(!CeCmdMatchRule(false, true, 1, 851, 851) && !CeCmdMatchRule(false, true, 3, 851, 851), "a match has the level's two players");
	expect(CeCmdMatchRule(false, true, 2, 0, 0), "level 0 counts as a level");
	expect(CeCmdMatchRule(false, true, 2, -1, 851, true), "a menu-chosen versus level with Solo Skirmish is a one-screen match");
	expect(CeCmdMatchRule(false, true, 2, -1, 850, true) && CeCmdMatchRule(false, true, 2, -1, 899, true), "Solo Skirmish admits the full 850..899 range");
	expect(!CeCmdMatchRule(false, true, 2, -1, 849, true) && !CeCmdMatchRule(false, true, 2, -1, 900, true), "Solo Skirmish stays within 850..899");
	expect(!CeCmdMatchRule(true, true, 2, -1, 851, true), "a network game is never a Solo Skirmish match");
	expect(!CeCmdMatchRule(false, false, 2, -1, 851, true), "a non-multiplayer level is never a Solo Skirmish match");
	expect(!CeCmdMatchRule(false, true, 1, -1, 851, true), "Solo Skirmish requires a 2-player multiplayer level");
	expect(!CeCmdMatchRule(false, true, 2, 851, 851, false, true), "a consumed CLI -level no longer activates a Commander match");
	expect(CeCmdMatchRule(false, true, 2, 851, 851, true, true), "Solo Skirmish still works after CLI -level is consumed");

	// CLI -level latch: does not stick across non-matching levels or after match end
	CeCmdCliLevelLatch latch;
	CeCmdCliLevelReset(latch);
	expect(!CeCmdCliLevelCheck(latch, 850, 850), "CLI -level 850 active on initial matching level 850");
	expect(CeCmdCliLevelCheck(latch, 850, 851), "CLI -level 850 consumed once a different level loads");
	expect(CeCmdCliLevelCheck(latch, 850, 850), "CLI -level 850 stays consumed if returning to 850 later");
	CeCmdCliLevelReset(latch);
	expect(!CeCmdCliLevelCheck(latch, 850, 850), "fresh latch admits initial -level 850");
	CeCmdCliLevelConsume(latch);
	expect(CeCmdCliLevelCheck(latch, 850, 850), "explicit level end consumes CLI -level 850");

	// Allegiance fallback on P1 Battle Engine death: preserves last valid side for current level, resets on level change
	int cachedSide = -1, cachedLevel = -1;
	expect(CeCmdResolveAllegiance(cachedSide, cachedLevel, 851, true, 0) == 0, "live P1 caches allegiance 0");
	expect(CeCmdResolveAllegiance(cachedSide, cachedLevel, 851, false, -1) == 0, "dead P1 keeps cached allegiance 0 instead of -1");
	expect(CeCmdResolveAllegiance(cachedSide, cachedLevel, 852, false, -1) == -1, "new level resets cached allegiance");
	expect(CeCmdResolveAllegiance(cachedSide, cachedLevel, 852, true, 1) == 1 &&
	       CeCmdResolveAllegiance(cachedSide, cachedLevel, 852, false, -1) == 1, "Muspell allegiance cached across P1 death");
}

// The match's alerts: one at a time, the gravest first, each mark of the engine's life once, "under attack" at most once in the quiet time, which is
// 20 seconds of updates (the call site gives no interval of its own: the rule's constant is the only one).
static void alerts()
{
	const unsigned q = kCeCmdAlertQuiet;
	expect(q == 400, "the quiet time is 400 updates, 20 seconds");
	CeCmdAlertState a;
	CeCmdAlertReset(a);
	expect(CeCmdAlertStep(a, 2, 100.f, 25.f, 25.f, 10) == CE_ALERT_NONE, "the first update only records how things stand");
	expect(CeCmdAlertStep(a, 2, 100.f, 25.f, 25.f, 11) == CE_ALERT_NONE, "nothing changed, nothing said");
	expect(CeCmdAlertStep(a, 2, 99.f, 25.f, 25.f, 10 + q - 1) == CE_ALERT_NONE, "a base hit inside the quiet time after the start is not said yet");
	expect(CeCmdAlertStep(a, 2, 98.f, 25.f, 25.f, 10 + q) == CE_ALERT_BASE_HIT, "a base that loses life is under attack");
	expect(CeCmdAlertStep(a, 2, 97.f, 25.f, 25.f, 11 + q) == CE_ALERT_NONE, "and is not said again at once");
	expect(CeCmdAlertStep(a, 2, 96.5f, 25.f, 25.f, 10 + 2 * q - 1) == CE_ALERT_NONE, "nor one update before the quiet time is over");
	expect(CeCmdAlertStep(a, 2, 96.f, 25.f, 25.f, 10 + 2 * q) == CE_ALERT_BASE_HIT, "but is again once the quiet time is over");
	const unsigned t0 = 10 + 3 * q;
	expect(CeCmdAlertStep(a, 2, 96.f, 25.f, 25.f, t0) == CE_ALERT_NONE, "a base that keeps its life is not under attack");
	expect(CeCmdAlertStep(a, 2, 96.f, 12.4f, 25.f, t0 + 1) == CE_ALERT_ENGINE_HURT, "below half its life the engine is hurt");
	expect(CeCmdAlertStep(a, 2, 96.f, 12.0f, 25.f, t0 + 2) == CE_ALERT_NONE, "said once");
	expect(CeCmdAlertStep(a, 2, 96.f, 6.0f, 25.f, t0 + 3) == CE_ALERT_ENGINE_BADLY_HURT, "below a quarter it is badly hurt");
	expect(CeCmdAlertStep(a, 2, 96.f, 5.0f, 25.f, t0 + 4) == CE_ALERT_NONE, "said once too");
	expect(CeCmdAlertStep(a, 2, 96.f, 25.f, 25.f, t0 + 5) == CE_ALERT_NONE, "a mended engine says nothing");
	expect(CeCmdAlertStep(a, 2, 96.f, 12.f, 25.f, t0 + 6) == CE_ALERT_ENGINE_HURT, "and a new hurt is said again");
	expect(CeCmdAlertStep(a, 2, 96.f, 0.f, 25.f, t0 + 7) == CE_ALERT_NONE, "a destroyed engine is not hurt (its own note says it is down)");
	expect(CeCmdAlertStep(a, 1, 60.f, 25.f, 25.f, t0 + 8) == CE_ALERT_BUILDING_LOST, "a building destroyed is said, before the base hit it came with");
	expect(CeCmdAlertStep(a, 1, 59.f, 25.f, 25.f, t0 + 9) == CE_ALERT_NONE, "and starts the quiet time of the base");
	CeCmdAlertReset(a);
	CeCmdAlertStep(a, 2, 100.f, 25.f, 25.f, 0);
	expect(CeCmdAlertStep(a, 1, 50.f, 5.f, 25.f, 1) == CE_ALERT_BUILDING_LOST, "a loss and a badly hurt engine in one update: the loss first");
	expect(CeCmdAlertStep(a, 1, 50.f, 5.f, 25.f, 2) == CE_ALERT_ENGINE_BADLY_HURT, "and the engine in the next");
	expect(CeCmdAlertStep(a, 1, 50.f, 25.f, 0.f, 3) == CE_ALERT_NONE, "an engine of unknown full life is never hurt");
}

// The live view's return in a match: it waits when the engine goes down under an open view, opens once the engine is back, and never for a view the
// player closed; the open key while the engine is down asks for it and asks again to stop.
static void reopen()
{
	bool w = false;
	expect(CeCmdReopenStep(w, true, true, true, true, false, true) == CE_REOPEN_NONE && !w, "an open view with the engine in the field: nothing");
	expect(CeCmdReopenStep(w, true, false, true, false, false, false) == CE_REOPEN_WAIT && w, "the view closed as the engine went down: it waits");
	expect(CeCmdReopenStep(w, false, false, true, false, false, false) == CE_REOPEN_NONE && w, "and keeps waiting while the engine is down");
	expect(CeCmdReopenStep(w, false, false, true, false, false, true) == CE_REOPEN_NONE && w, "even where the view could open (a dying engine is still there)");
	expect(CeCmdReopenStep(w, false, false, true, true, false, false) == CE_REOPEN_NONE && w, "the engine back but the view cannot open yet: still waiting");
	expect(CeCmdReopenStep(w, false, false, true, true, false, true) == CE_REOPEN_OPEN && !w, "the engine back and the view can open: it opens");
	expect(CeCmdReopenStep(w, false, false, true, true, false, true) == CE_REOPEN_NONE, "once");
	w = false;
	expect(CeCmdReopenStep(w, true, false, true, false, true, false) == CE_REOPEN_NONE && !w, "a view the player closed as the engine went down does not wait");
	expect(CeCmdReopenStep(w, true, false, true, true, false, true) == CE_REOPEN_NONE && !w, "nor one closed with the engine in the field");
	w = true;
	expect(CeCmdReopenStep(w, false, false, false, true, false, true) == CE_REOPEN_NONE && !w, "the battle over: nothing waits");
	w = false;
	expect(CeCmdReopenStep(w, true, false, false, false, false, false) == CE_REOPEN_NONE && !w, "and outside a match nothing ever does");
	w = true;
	expect(CeCmdReopenStep(w, false, true, true, true, false, true) == CE_REOPEN_NONE && w, "an open view is not opened again");
	w = false;
	expect(CeCmdReopenKey(w, true, false) && w, "the open key while the engine is down asks for the view");
	expect(CeCmdReopenKey(w, true, false) && !w, "and again stops asking");
	expect(!CeCmdReopenKey(w, true, true) && !w, "with the engine in the field it is the ordinary open key");
	expect(!CeCmdReopenKey(w, false, false) && !w, "and outside a match too");
}

// Where a Fall back goes: the nearest building of its side on land, else the nearest node it holds on land, never water; the rally faces the battle.
static void fallback()
{
	const CeCmdPlace places[] = {
		{100.f, 100.f, CE_PLACE_BUILDING, true}, {300.f, 300.f, CE_PLACE_BUILDING, true}, {200.f, 205.f, CE_PLACE_NODE, true},
		{210.f, 210.f, CE_PLACE_BUILDING, false}, {205.f, 200.f, CE_PLACE_NODE, false}};
	expect(CeCmdFallbackPick(places, 5, 280.f, 270.f) == 1, "the nearest building of the side");
	expect(CeCmdFallbackPick(places, 5, 120.f, 90.f) == 0, "whichever it is");
	expect(CeCmdFallbackPick(places, 5, 205.f, 205.f) == 1 || CeCmdFallbackPick(places, 5, 205.f, 205.f) == 0, "a building before a nearer node");
	expect(CeCmdFallbackPick(places, 5, 212.f, 212.f) != 3, "never a place in the water, however near");
	expect(CeCmdFallbackPick(places + 2, 3, 206.f, 201.f) == 0, "with no building on land, the node on land");
	expect(CeCmdFallbackPick(places + 3, 2, 0.f, 0.f) == -1, "and nowhere when nothing is on land");
	expect(CeCmdFallbackPick(places, 0, 0.f, 0.f) == -1, "or nothing is listed");
	const CeCmdPlace odd[] = {{50.f, 50.f, 7, true}, {400.f, 400.f, CE_PLACE_NODE, true}};
	expect(CeCmdFallbackPick(odd, 2, 50.f, 50.f) == 1, "a place of no known kind is not home");
	float x = 0.f, y = 0.f;
	CeCmdFallbackRally(100.f, 100.f, 100.f, 400.f, 24.f, &x, &y);
	expect(x > 99.99f && x < 100.01f && y > 123.99f && y < 124.01f, "the rally stands 24 units out towards the battle");
	CeCmdFallbackRally(100.f, 100.f, 110.f, 100.f, 24.f, &x, &y);
	expect(x > 104.99f && x < 105.01f, "standoff clamps to at most half the distance on small maps");
	CeCmdFallbackRally(100.f, 100.f, 100.f, 100.f, 24.f, &x, &y);
	expect(x == 100.f && y == 100.f, "with nothing to face, the place itself");
}

int main()
{
	selection();
	groups();
	box();
	outcome();
	rules();
	alerts();
	reopen();
	fallback();
	printf(failures ? "FAIL\n" : "PASS\n");
	return failures ? 1 : 0;
}
