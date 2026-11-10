// SPDX-License-Identifier: GPL-3.0-only
// Authored doubles surround the production dispatcher and order consumers.
#include <math.h>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include "ce_commander.h"

#define FALSE false
enum EAIState { AI_OFF, AI_ONF };
class FVector {
public:
    float X, Y, Z;
    FVector(float x = 0, float y = 0, float z = 0) : X(x), Y(y), Z(z) {}
};
struct CThing {};
struct CSquad : CThing {};
struct CNormalSquad : CSquad {
    int states = 0, destinations = 0;
    EAIState state = AI_OFF;
    FVector goal;
    void SetAIState(EAIState value) { ++states; state = value; }
    void GoToPoint(const FVector &value, bool) { ++destinations; goal = value; }
};
struct CUnit {
    int states = 0, stops = 0, destinations = 0;
    EAIState state = AI_OFF;
    FVector position, safe, goal;
    void SetAIState(EAIState value) { ++states; state = value; }
    void Stop() { ++stops; }
    void GoToPoint(const FVector &value, bool) { ++destinations; goal = value; }
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
    gTick = 47;
    gEnabled = mapOn = recordAvailable = true;
    lookupCalls = descriptionCalls = 0;
    unitCount = 1;
}
static void Untouched() {
    Check(!record.ordered && record.verb == V_NONE && record.orderTick == 11,
          "refused order changed the order record");
    Check(Same(record.goal, FVector(3, 5, 9)), "refused order changed the goal");
    Check(!squad.states && !squad.destinations && !unit.states && !unit.stops &&
          !unit.destinations, "refused order reached a squad or member consumer");
    Check(!MAP.queries, "refused order queried terrain");
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
        const float edge[] = {-64, 64, -0.0f, 0.0f,
                              std::numeric_limits<float>::lowest(),
                              std::numeric_limits<float>::max()};
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
          "fallback did not deliver its selected safe destination");
    Reset();
    unit.safe = FVector();
    Check(CeCmdIssue(ordinal, CE_CMD_FALLBACK, bad, bad, NULL) &&
          Same(squad.goal, FVector(pilot.X, pilot.Y, MAP.height)),
          "fallback did not retain its pilot-position alternative");
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
int main() {
    PointOrders();
    PositionIndependentOrders();
    EligibilityAndFences();
    if (!failures) std::printf("commander boundary checks passed: %d\n", checks);
    return failures ? 1 : 0;
}
