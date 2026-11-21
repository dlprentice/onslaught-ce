// SPDX-License-Identifier: GPL-3.0-only
// Authored doubles surround the production dispatcher and order consumers.
#include <cstring>
#include <math.h>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include "ce_commander.h"
#include "ce_cmdoutcome.h"

#define FALSE false
enum EAIState { AI_OFF, AI_ONF, AI_ON };
class FVector {
public:
    float X, Y, Z;
    FVector(float x = 0, float y = 0, float z = 0) : X(x), Y(y), Z(z) {}
    FVector operator-(const FVector &o) const { return FVector(X - o.X, Y - o.Y, Z - o.Z); }
    float MagnitudeXY() const { return sqrtf(X * X + Y * Y); }
};
static void NativeWrite(const char*, bool pointOverride = false);
struct CThing {};
struct CSquad : CThing {
    FVector position;
    const FVector &GetPos() const { return position; }
};
struct CNormalSquad : CSquad {
    int states = 0, destinations = 0;
    EAIState state = AI_OFF;
    FVector goal, mUnknownF4;	// the squad's own goal (the order layer reads it once, before its first order, for Release)
    EAIState GetAIState() const { return state; }
    void SetAIState(EAIState value) { NativeWrite("explicit scope precedes native state"); ++states; state = value; }
    void GoToPoint(const FVector &value, bool overridePoint) { NativeWrite("explicit scope precedes native point", overridePoint); ++destinations; goal = value; }
};
struct CUnit {
    bool individuallyOwned = true;
    int states = 0, stops = 0, destinations = 0;
    EAIState state = AI_OFF;
    FVector position, safe, goal;
    EAIState GetAIState() const { return state; }
    void SetAIState(EAIState value) { NativeWrite("explicit scope precedes native state"); ++states; state = value; }
    void Stop() { ++stops; }
    void GoToPoint(const FVector &value, bool overridePoint) { NativeWrite("explicit scope precedes native point", overridePoint); ++destinations; goal = value; }
    const FVector &GetPos() const { return position; }
    FVector FindNearestSafeSide() const { return safe; }
};
struct SExtents { int mMinX, mMaxX, mMinY, mMaxY; };
struct Terrain {
    int queries = 0;
    float height = 7;
    SExtents extents = {-64, 64, -64, 64};
    float Collide(const FVector &) { ++queries; return height; }
    SExtents *GetExtents() { return &extents; }
} MAP;

#include "ce_commander_boundary_records.inc"

static Rec record;
static CNormalSquad squad;
static CUnit unit;
static CeCmdSquad selection;
static FVector pilot;
static unsigned gTick;
static bool gEnabled, mapOn, recordAvailable;
static int lookupCalls, descriptionCalls, unitCount;
static const int ordinal = 7;

bool CeCmdMapOn() { return mapOn; }
static Rec *ByOrdinal(int value) {
    ++lookupCalls;
    return recordAvailable && value == ordinal ? &record : NULL;
}
static int PlayerAllegiance() { return 0; }
static void Describe(int, Rec &, int, CeCmdSquad &out) {
    ++descriptionCalls;
    out = selection;
}
static CNormalSquad *AsNormal(const Rec &) { return &squad; }
static int Units(Rec &, CUnit **out, int capacity) {
    if (unitCount && capacity) out[0] = &unit;
    return unitCount;
}
static FVector Pilot() { return pilot; }
// A Fall back's home (ce_commander.cpp's Home and Land, which read the world and the height field): authored. The home is a rally at (31, 37); land is
// everywhere inside the occupied cells but the one wet point the fixture names.
static bool homeAvailable;
static FVector wet;
static bool Land(float x, float y) { return x >= -64 && x <= 64 && y >= -64 && y <= 64 && !(x == wet.X && y == wet.Y); }
static bool Home(float, float, FVector *home, FVector *rally) {
    if (!homeAvailable) return false;
    *home = FVector(29, 33, MAP.height); *rally = FVector(31, 37, MAP.height);
    return true;
}
static FVector Centre(Rec &) { return unit.position; }

static int ownershipReleases = 0;
static bool requireReleased = false;
extern bool gExplicitUnitReplacement;
static void Check(bool, const char*);
static void NativeWrite(const char* label, bool pointOverride) {
    if (requireReleased) Check(!unit.individuallyOwned, label);
    if (!gExplicitUnitReplacement)
        Check(!pointOverride, "routine setter cannot claim native override");
}
void CeCmdUnitOrderRelease(CUnit* target) {
    target->individuallyOwned = false;
    ++ownershipReleases;
}

// These definitions are extracted from the production source at test time.
#include "ce_commander_boundary_under_test.inc"

static int checks, failures;
static void Check(bool condition, const char *label) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "%s\n", label); }
}
static bool Same(const FVector &a, const FVector &b) {
    return a.X == b.X && a.Y == b.Y && a.Z == b.Z;
}
static void Reset(bool normal = true) {
    record = Rec{};
    squad = CNormalSquad{};
    unit = CUnit{};
    MAP = Terrain{};
    selection = CeCmdSquad{};
    record.squad = &squad;
    record.normal = normal;
    record.goal = FVector(3, 5, 9);
    record.verb = V_NONE;
    record.orderTick = 11;
    selection.commandable = true;
    selection.x = 13;
    selection.y = 17;
    unit.position = FVector(19, 23, 29);
    unit.safe = FVector(31, 37, 0);
    pilot = FVector(41, 43, 0);
    homeAvailable = true; wet = FVector(1000, 1000, 0);
    gTick = 47;
    gEnabled = mapOn = recordAvailable = true;
    lookupCalls = descriptionCalls = 0;
    unitCount = 1;
    ownershipReleases = 0; requireReleased = false;
}
static void Untouched() {
    Check(!record.ordered && record.verb == V_NONE && record.orderTick == 11,
          "refused order changed the order record");
    Check(Same(record.goal, FVector(3, 5, 9)), "refused order changed the goal");
    Check(!squad.states && !squad.destinations && !unit.states && !unit.stops &&
          !unit.destinations, "refused order reached a squad or member consumer");
    Check(!MAP.queries, "refused order queried terrain");
    Check(!ownershipReleases && unit.individuallyOwned, "refused order released individual ownership");
}
static void Reject(int verb, float x, float y, bool beforeLookup) {
    const char *why = NULL;
    Check(!CeCmdIssue(ordinal, verb, x, y, &why), "malformed order was accepted");
    Check(why && *why, "refused order gave no reason");
    Untouched();
    if (beforeLookup)
        Check(!lookupCalls && !descriptionCalls, "malformed order read selection");
}
static void PointOrders() {
    const float bad[] = {std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity()};
    for (int verb : {CE_CMD_MOVE, CE_CMD_ADVANCE}) {
        for (float value : bad) {
            Reset(); Reject(verb, value, 53, true);
            Reset(); Reject(verb, 59, value, true);
        }
        // Finite coordinates outside the playable map are no longer valid march targets.
        for (float offMap : {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()}) {
            Reset();
            const char *reason = NULL;
            Check(!CeCmdIssue(ordinal, verb, offMap, 0.f, &reason) && reason,
                  "a finite point outside the battlefield is not land");
            Untouched();
        }
        const float edge[] = {-64, 64, -0.0f, 0.0f};
        for (float x : edge) {
            Reset();
            const char *why = "uncleared";
            const float y = 61;
            Check(CeCmdIssue(ordinal, verb, x, y, &why), "finite order was refused");
            Check(!why && MAP.queries == 1, "finite order did not sample terrain once");
            Check(squad.destinations == 1 && squad.states == 1,
                  "point order did not reach the normal squad");
            Check(Same(squad.goal, FVector(x, y, MAP.height)),
                  "normal squad received the wrong destination");
            Check(record.ordered && record.orderTick == gTick &&
                  record.verb == (verb == CE_CMD_MOVE ? V_MOVE : V_ADVANCE),
                  "point order was not recorded");
            Check(squad.state == (verb == CE_CMD_MOVE ? AI_OFF : AI_ONF),
                  "point order changed its firing policy");
        }
    }
    for (int verb : {int(CE_CMD_NONE), -1, std::numeric_limits<int>::max()}) {
        Reset(); Reject(verb, 1, 2, true);
    }
    Reset();
    Check(!CeCmdIssue(ordinal, CE_CMD_MOVE, bad[0], 1, NULL),
          "missing reason pointer accepted malformed input");
    Untouched();
}
static void ShorelineOrders()
{
    for (int verb : {int(CE_CMD_MOVE), int(CE_CMD_ADVANCE)}) {
        Reset();
        const char *why = NULL;
        Check(CeCmdIssue(ordinal, CE_CMD_ADVANCE, 9.f, 12.f, &why), "seed a real land order");
        const FVector accepted = squad.goal, recorded = record.goal;
        const int writes = squad.destinations, transitions = squad.states, orderTick = record.orderTick;
        wet = FVector(21.f, 15.f, 0.f);
        Check(!CeCmdIssue(ordinal, verb, wet.X, wet.Y, &why) &&
              why && std::strcmp(why, "Choose land for ground units.") == 0, "explain an offshore march");
        Check(Same(squad.goal, accepted) && Same(record.goal, recorded) &&
              squad.destinations == writes && squad.states == transitions &&
              record.verb == V_ADVANCE && record.orderTick == orderTick, "a refused destination keeps the current march");
        Check(CeCmdIssue(ordinal, verb, 21.f, 16.f, &why) && !why,
              "the next dry destination still takes the chosen order");
    }
    Reset();
    selection.crossesWater = true;
    wet = FVector(12, 13, 0);
    const char *why = NULL;
    Check(CeCmdIssue(ordinal, CE_CMD_MOVE, wet.X, wet.Y, &why),
          "a water-capable squad keeps its existing offshore orders");

}

static void PositionIndependentOrders() {
    const float bad = std::numeric_limits<float>::quiet_NaN();
    for (bool normal : {true, false}) {
        Reset(normal);
        selection.deploys = true;
        const char *why = "uncleared";
        Check(CeCmdIssue(ordinal, CE_CMD_HOLD, bad, bad, &why),
              "hold incorrectly consumed caller coordinates");
        Check(!why && record.ordered && record.verb == V_HOLD,
              "hold was not recorded");
        Check(Same(record.goal, FVector(selection.x, selection.y, MAP.height)),
              "hold did not choose the selected squad centre");
        if (normal) {
            Check(squad.destinations == 1 && squad.state == AI_ONF,
                  "normal hold did not reach its intended consumer");
            Check(Same(squad.goal, record.goal), "normal hold consumer lost its goal");
        } else {
            Check(unit.states == 1 && unit.state == AI_ONF &&
                  unit.destinations == 1 && Same(unit.goal, unit.position),
                  "relaxed hold did not plant its member at its current position");
        }
    }
    Reset();
    Check(CeCmdIssue(ordinal, CE_CMD_FALLBACK, bad, bad, NULL),
          "fallback incorrectly consumed caller coordinates");
    Check(record.ordered && record.verb == V_FALLBACK && squad.state == AI_OFF,
          "fallback changed its order or firing policy");
    Check(squad.destinations == 1 && Same(squad.goal, FVector(31, 37, MAP.height)),
          "fallback did not deliver its home's rally");
    Reset();
    unit.safe = FVector(-60, -60, 0);
    Check(CeCmdIssue(ordinal, CE_CMD_FALLBACK, bad, bad, NULL) && Same(squad.goal, FVector(31, 37, MAP.height)),
          "fallback went to the game's safe side, not home");
    Reset();
    homeAvailable = false;
    Check(CeCmdIssue(ordinal, CE_CMD_FALLBACK, bad, bad, NULL) &&
          Same(squad.goal, FVector(pilot.X, pilot.Y, pilot.Z)),
          "fallback with no home did not retain its pilot-position alternative");
    Reset();
    homeAvailable = false; wet = FVector(pilot.X, pilot.Y, 0);
    Reject(CE_CMD_FALLBACK, bad, bad, false);
    Reset();
    Check(CeCmdIssue(ordinal, CE_CMD_FALLBACK, 0, 0, NULL) && Same(squad.goal, FVector(31, 37, MAP.height)),
          "fallback given the origin did not go home");
    Reset();
    Check(CeCmdIssue(ordinal, CE_CMD_FALLBACK, 45, 47, NULL) && record.verb == V_FALLBACK && squad.state == AI_OFF &&
          Same(squad.goal, FVector(45, 47, MAP.height)),
          "fallback given a place round its rally did not go there as a fall back");
    Reset();
    wet = FVector(45, 47, 0);
    Check(CeCmdIssue(ordinal, CE_CMD_FALLBACK, 45, 47, NULL) && Same(squad.goal, FVector(31, 37, MAP.height)),
          "fallback given a place in the water did not go to the rally instead");
    Reset();
    Check(CeCmdIssue(ordinal, CE_CMD_FALLBACK, 500, -500, NULL) && Same(squad.goal, FVector(31, 37, MAP.height)),
          "fallback given a place outside the map did not go to the rally");
}
static void EligibilityAndFences() {
    for (int gate = 0; gate != 3; ++gate) {
        Reset();
        if (gate == 0) mapOn = false;
        if (gate == 1) gEnabled = false;
        if (gate == 2) recordAvailable = false;
        Reject(CE_CMD_MOVE, 5, 7, false);
        Check(!descriptionCalls, "unavailable order described a selection");
    }
    Reset();
    selection.commandable = false;
    Reject(CE_CMD_MOVE, 5, 7, false);
    Check(descriptionCalls == 1, "eligibility was not rechecked at execution");
    for (int verb : {CE_CMD_MOVE, CE_CMD_ADVANCE, CE_CMD_FALLBACK}) {
        Reset(); selection.deploys = true; Reject(verb, 5, 7, false);
    }
}
// Release: the squad gets back the state and goal it had before its FIRST order, the layer forgets its order, and a squad without one is refused untouched.
static void Release() {
    for (bool normal : {true, false}) {
        Reset(normal);
        squad.state = AI_ON; squad.mUnknownF4 = FVector(71, 73, 5); unit.state = AI_ON;
        Check(!CeCmdIssue(ordinal, CE_CMD_RELEASE, 0, 0, NULL), "release of a squad with no order was accepted");
        Untouched();
        Check(CeCmdIssue(ordinal, CE_CMD_MOVE, 20, 21, NULL) && record.kept && record.ownState == AI_ON, "the first order did not keep the squad's own state");
        Check(!normal || Same(record.ownGoal, FVector(71, 73, 5)), "the first order did not keep the squad's own goal");
        Check(record.fromMap && record.outcome == CE_OUT_MOVING, "an order from the map is not followed as a march");
        Check(CeCmdIssue(ordinal, CE_CMD_HOLD, 0, 0, NULL) && record.ownState == AI_ON, "a second order overwrote what the squad had before the first");
        const int states = normal ? squad.states : unit.states;
        Check(CeCmdIssue(ordinal, CE_CMD_RELEASE, 0, 0, NULL), "release of an ordered squad was refused");
        Check(!record.ordered && !record.fromMap && record.verb == V_NONE && record.outcome == CE_OUT_NONE, "release left the layer's order in place");
        if (normal) Check(squad.state == AI_ON && squad.states == states + 1 && Same(squad.goal, FVector(71, 73, 5)), "release did not give back the squad's state and goal");
        else Check(unit.state == AI_ON && unit.states == states + 1, "release did not give back the members' state");
        Check(!CeCmdIssue(ordinal, CE_CMD_RELEASE, 0, 0, NULL), "a second release was accepted");
    }
    // artillery takes Hold and Release, and nothing else
    Reset(); selection.deploys = true;
    Check(CeCmdIssue(ordinal, CE_CMD_HOLD, 0, 0, NULL) && CeCmdIssue(ordinal, CE_CMD_RELEASE, 0, 0, NULL), "artillery could not be held and released");
}
static void IndividualOwnership() {
    for (bool normal : {true, false}) {
        for (int verb : {CE_CMD_MOVE, CE_CMD_ADVANCE, CE_CMD_HOLD, CE_CMD_FALLBACK}) {
            Reset(normal); requireReleased = true;
            Check(CeCmdIssue(ordinal, verb, 12, 13, NULL), "ownership command accepted");
            Check(!unit.individuallyOwned && ownershipReleases > 0,
                  "accepted order releases individual ownership");
            Check(!gExplicitUnitReplacement, "explicit scope restored after accepted order");
            unit.individuallyOwned = true; ownershipReleases = 0; requireReleased = false;
            Hold(record, V_MOVE, FVector(14, 15, 0), NULL);
            Check(unit.individuallyOwned && ownershipReleases == 0,
                  "routine Hold preserves individual ownership");
            requireReleased = true;
            Check(CeCmdIssue(ordinal, CE_CMD_RELEASE, 0, 0, NULL), "squad release accepted");
            Check(!unit.individuallyOwned && ownershipReleases > 0,
                  "squad release relinquishes individual ownership");
        }
    }
}

int main() {
    PointOrders(); ShorelineOrders();
    Release();
    PositionIndependentOrders();
    EligibilityAndFences();
    IndividualOwnership();
    if (!failures) std::printf("commander boundary checks passed: %d\n", checks);
    return failures ? 1 : 0;
}
