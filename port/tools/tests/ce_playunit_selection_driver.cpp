// Source-bound selected-control adapter checks with an authored world.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>
#include "ce_playunit.h"
#include "ce_playunit_rules.h"
#include "selection_facts.inc"

enum { THING_TYPE_UNIT = 7, THING_TYPE_OTHER = 9 };
struct CThing {
    int number = 1;
    int type = THING_TYPE_UNIT;
    mutable int reads = 0;
    int GetThingType() const { ++reads; return type; }
    int GetThingNumber() const { ++reads; return number; }
};
struct CUnit : CThing { CePlayUnitFacts facts = Tank(); };
struct CPlayer {};
struct CBattleEngine {};
struct CCONTROLLER {};
struct AuthoredWorld {
    std::vector<CThing*> things;
    std::vector<CThing*>& GetThingNB() { return things; }
} WORLD;
template<class T> struct ListIterator {
    std::vector<T*>* items; std::size_t index = 0;
    explicit ListIterator(std::vector<T*>* v) : items(v) {}
    T* First() { index = 0; return items->empty() ? nullptr : (*items)[0]; }
    T* Next() { ++index; return index < items->size() ? (*items)[index] : nullptr; }
};
bool gConfigured = true, gWanted = true, gate = true, owns = false;
bool ready = true, mapOpen = false;
int takes = 0, readsConfig = 0;
CUnit* taken = nullptr;
CPlayer player; CBattleEngine engine; CCONTROLLER controller;
std::uint64_t gSelectionWorld = 1;
void ReadConfiguration() { ++readsConfig; gConfigured = true; }
bool Allowed(bool option) { return option && gate; }
bool CePlayUnitOwnsPilot() { return owns; }
bool PortCeCmdMapOpen() { return mapOpen; }
bool Ready(CPlayer** p, CBattleEngine** e, CCONTROLLER** c) {
    if (!ready) return false;
    *p = &player; *e = &engine; *c = &controller; return true;
}
const char* NotReadySentence() { return "Pilot is not ready"; }
CePlayUnitFacts Facts(CUnit* unit, CBattleEngine*) { return unit->facts; }
bool Take(CPlayer*, CBattleEngine*, CCONTROLLER*, CUnit* unit, int) {
    ++takes; taken = unit; return true;
}
#include "selection_actual.inc"

int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "selection control failed at line %d\n", __LINE__); return 1; } } while (0)
int main() {
    CUnit first, selected; first.number = 3; selected.number = 8;
    WORLD.things = {&first, &selected};
    const char* reason = nullptr;
    const auto epoch = CePlayUnitSelectionWorld();
    mapOpen = true;
    CHECK(CePlayUnitCanTakeSelection(epoch, &selected, selected.number, &reason));
    CHECK(reason == nullptr && takes == 0);
    CHECK(!CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    CHECK(reason != nullptr && takes == 0);
    mapOpen = false;
    CHECK(CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    CHECK(takes == 1 && taken == &selected && reason == nullptr);
    const int committed = takes;
    CHECK(!CePlayUnitCanTakeSelection(epoch, nullptr, 8, &reason));
    CUnit absent; absent.number = selected.number;
    CHECK(!CePlayUnitCanTakeSelection(epoch, &absent, absent.number, &reason));
    CHECK(absent.reads == 0);
    CHECK(!CePlayUnitCanTakeSelection(epoch, &selected, selected.number + 1, &reason));
    CHECK(!CePlayUnitCanTakeSelection(0, &selected, selected.number, &reason));
    CHECK(!CePlayUnitCanTakeSelection(epoch + 1, &selected, selected.number, &reason));
    selected.type = THING_TYPE_OTHER;
    CHECK(!CePlayUnitCanTakeSelection(epoch, &selected, selected.number, &reason));
    selected.type = THING_TYPE_UNIT;
    CHECK(CePlayUnitCanTakeSelection(epoch, &selected, selected.number));
    selected.facts.alive = false;
    CHECK(!CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    selected.facts = Tank(); selected.facts.friendly = false;
    CHECK(!CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    selected.facts = Tank(); selected.facts.scripted = true;
    CHECK(!CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    selected.facts = Tank(); selected.facts.objective = true;
    CHECK(!CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    selected.facts = Tank(); selected.facts.family = CE_PLAYUNIT_FAMILY_AIRCRAFT;
    selected.facts.groundUnit = false;
    CHECK(!CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    selected.facts = Tank();
    gate = false;
    CHECK(!CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    gate = true; gWanted = false;
    CHECK(!CePlayUnitCanTakeSelection(epoch, &selected, selected.number, &reason));
    gWanted = true; owns = true;
    CHECK(!CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    owns = false; ready = false;
    CHECK(!CePlayUnitCanTakeSelection(epoch, &selected, selected.number, &reason));
    CHECK(reason != nullptr);
    ready = true;
    CHECK(takes == committed);
    CHECK(CePlayUnitCanTakeSelection(epoch, &selected, selected.number));
    WORLD.things = {&first};
    CHECK(!CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    CHECK(takes == committed);
    CUnit replacement; replacement.number = selected.number;
    WORLD.things.push_back(&replacement);
    CHECK(!CePlayUnitTakeSelection(epoch, &selected, selected.number, &reason));
    CHECK(CePlayUnitCanTakeSelection(epoch, &replacement, replacement.number));
    replacement.number += 1;
    CHECK(!CePlayUnitTakeSelection(epoch, &replacement, selected.number, &reason));
    SelectionWorldEnded();
    CHECK(CePlayUnitSelectionWorld() != epoch && CePlayUnitSelectionWorld() != 0);
    CHECK(!CePlayUnitTakeSelection(epoch, &replacement, replacement.number, &reason));
    CHECK(CePlayUnitCanTakeSelection(CePlayUnitSelectionWorld(), &replacement, replacement.number));
    gSelectionWorld = (std::numeric_limits<std::uint64_t>::max)();
    SelectionWorldEnded();
    CHECK(CePlayUnitSelectionWorld() != 0);
    gConfigured = false;
    CHECK(CePlayUnitCanTakeSelection(CePlayUnitSelectionWorld(), &replacement, replacement.number));
    CHECK(gConfigured && readsConfig == 1 && takes == committed);
    std::printf("selected-control checks=%d failures=0 skips=0\n", checks);
    return 0;
}
