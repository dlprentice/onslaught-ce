#include <cmath>
#include <cstdio>
#include <cstdlib>

static unsigned checks = 0;
static unsigned releases = 0;
static bool requireReleased = false;
static void Require(bool value, const char* name) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
}
struct FVector {
    float X = 0, Y = 0, Z = 0;
    FVector operator-(const FVector& other) const {
        return {X - other.X, Y - other.Y, Z - other.Z};
    }
    float MagnitudeXY() const { return std::sqrt(X * X + Y * Y); }
};
enum EAIState { AI_OFF, AI_ON, AI_ONF };
enum Verb { V_MOVE, V_FALLBACK, V_ADVANCE, V_HOLD };
enum { CE_OUT_HOLDING, CE_OUT_MOVING, FALSE = 0 };
struct CThing {};
struct CUnit {
    bool token = true;
    unsigned stateWrites = 0, pointWrites = 0, stops = 0;
    EAIState state = AI_ON;
    FVector position{};
    EAIState GetAIState() { return state; }
    FVector GetPos() { return position; }
    void SetAIState(EAIState value) {
        if (requireReleased) Require(!token, "release precedes member state");
        state = value; ++stateWrites;
    }
    void GoToPoint(FVector, bool) {
        if (requireReleased) Require(!token, "release precedes member point");
        ++pointWrites;
    }
    void Stop() { ++stops; }
};
void CeCmdUnitOrderRelease(CUnit* unit) { unit->token = false; ++releases; }
struct CNormalSquad {
    CUnit* members[2]{};
    FVector mUnknownF4{};
    EAIState state = AI_ON;
    FVector GetPos() { return {}; }
    EAIState GetAIState() { return state; }
    void SetAIState(EAIState value) {
        if (requireReleased)
            for (auto* unit : members) Require(!unit->token, "release precedes squad state");
        state = value;
    }
    void GoToPoint(FVector value, bool) {
        if (requireReleased)
            for (auto* unit : members) Require(!unit->token, "release precedes squad point");
        mUnknownF4 = value;
    }
};
struct Rec {
    bool ordered = false, kept = false, normal = true;
    bool fromMap = false, arrived = false;
    CNormalSquad* squad = nullptr;
    FVector ownGoal{}, goal{};
    EAIState ownState = AI_ON;
    Verb verb = V_MOVE;
    unsigned orderTick = 0;
    int track = 0, outcome = 0;
};
static unsigned gTick = 0;
static CNormalSquad* AsNormal(Rec& r) { return r.squad; }
static int Units(Rec& r, CUnit** out, int capacity) {
    int count = 0;
    for (auto* unit : r.squad->members) if (count < capacity) out[count++] = unit;
    return count;
}
static FVector Centre(Rec&) { return {}; }
static void CeTrackStart(int&, float, unsigned) {}

#include "commander_scope.inc"

int main() {
    CUnit first, second, unrelated;
    CNormalSquad squad;
    squad.members[0] = &first; squad.members[1] = &second;
    Rec record; record.squad = &squad;
    const FVector goal{12, 15, 0};
    for (bool normal : {true, false}) {
        record.normal = normal;
        for (Verb verb : {V_MOVE, V_FALLBACK, V_ADVANCE, V_HOLD}) {
            first.token = second.token = true;
            releases = 0; requireReleased = true;
            {
                ExplicitUnitReplacement accepted;
                Require(gExplicitUnitReplacement, "accepted scope enabled");
                Hold(record, verb, goal, nullptr);
            }
            Require(!gExplicitUnitReplacement, "accepted scope restored");
            Require(!first.token && !second.token, "explicit command releases members");
            Require(unrelated.token, "other squad keeps its token");
            Require(releases >= 2, "both affected members notified");
            first.token = second.token = true;
            releases = 0; requireReleased = false;
            Hold(record, verb, goal, nullptr);
            Require(first.token && second.token && releases == 0,
                    "routine follow does not release temporary destinations");
        }
    }
    {
        ExplicitUnitReplacement outer;
        { ExplicitUnitReplacement inner; }
        Require(gExplicitUnitReplacement, "nested scope preserves outer owner");
    }
    Require(!gExplicitUnitReplacement, "last scope restores routine behavior");
    std::printf("checks=%u failures=0 skips=0\n", checks);
}
