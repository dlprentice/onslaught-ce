// SPDX-License-Identifier: GPL-3.0-only
// Engine-integrated 32-bit monotonic CE entity handle side table.
// Zero src/ struct modifications, zero game-heap allocations, zero faithful-replay impact.

#include "ce_entity_handles.h"

#ifdef CE_VERSION

#include "common.h"
#include "world.h"
#include "thing.h"
#include "unit.h"
#include "squad.h"

namespace {

// BSS-resident host side table: zero mThingHeap / mDefaultHeap allocations.
CeEntityHandleTable gEntityHandles;

CThing* FindLiveThingInWorld(uintptr_t addr) {
    if (addr == 0u) return nullptr;
    ListIterator<CThing> things(&WORLD.GetThingNB());
    for (CThing* t = things.First(); t; t = things.Next()) {
        if (reinterpret_cast<uintptr_t>(t) == addr) return t;
    }
    ListIterator<CSquad> squads(&WORLD.GetSquadNB());
    for (CSquad* s = squads.First(); s; s = squads.Next()) {
        if (reinterpret_cast<uintptr_t>(s) == addr) return static_cast<CThing*>(s);
    }
    return nullptr;
}

CSquad* FindLiveSquadInWorld(uintptr_t addr) {
    if (addr == 0u) return nullptr;
    ListIterator<CSquad> squads(&WORLD.GetSquadNB());
    for (CSquad* s = squads.First(); s; s = squads.Next()) {
        if (reinterpret_cast<uintptr_t>(s) == addr) return s;
    }
    return nullptr;
}

uint64_t ThingStamp(const CThing* thing) {
    CThing* mutableThing = const_cast<CThing*>(thing);
    const uint64_t typeBits = static_cast<uint64_t>(static_cast<uint32_t>(mutableThing->GetThingType()));
    const uint64_t numBits = static_cast<uint64_t>(static_cast<uint16_t>(mutableThing->GetThingNumber()));
    return (typeBits << 16) | numBits;
}

bool IsThingDying(CThing* thing) {
    if (!thing) return true;
    if (thing->IsDying() || thing->IsShuttingDown()) return true;
    if (thing->GetThingType() & THING_TYPE_UNIT) {
        CUnit* u = static_cast<CUnit*>(thing);
        if (u->GetLife() <= 0.f) return true;
    }
    return false;
}

}  // namespace

CeEntityHandle CeEntityHandleFor(const CThing* thing) {
    if (!thing) return kCeEntityHandleInvalid;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(thing);
    const CeEntityHandle existing = gEntityHandles.Lookup(addr);
    CThing* live = FindLiveThingInWorld(addr);
    if (!live || IsThingDying(live)) {
        if (existing != kCeEntityHandleInvalid) gEntityHandles.Unregister(addr);
        return kCeEntityHandleInvalid;
    }
    const uint64_t stamp = ThingStamp(live);
    if (existing != kCeEntityHandleInvalid) {
        if (gEntityHandles.StampOf(existing) == stamp) return existing;
        return gEntityHandles.RegisterFresh(addr, stamp);
    }
    return gEntityHandles.Register(addr, stamp);
}

CeEntityHandle CeEntityHandleLookup(const CThing* thing) {
    if (!thing) return kCeEntityHandleInvalid;
    return gEntityHandles.Lookup(reinterpret_cast<uintptr_t>(thing));
}

CThing* CeEntityHandleResolve(CeEntityHandle handle) {
    const uintptr_t addr = gEntityHandles.Resolve(handle);
    if (addr == 0u) return nullptr;
    CThing* live = FindLiveThingInWorld(addr);
    if (!live || IsThingDying(live) || gEntityHandles.StampOf(handle) != ThingStamp(live)) {
        gEntityHandles.Unregister(addr);
        return nullptr;
    }
    return live;
}

CUnit* CeEntityHandleResolveUnit(CeEntityHandle handle) {
    CThing* live = CeEntityHandleResolve(handle);
    if (!live || !(live->GetThingType() & THING_TYPE_UNIT)) return nullptr;
    return static_cast<CUnit*>(live);
}

CSquad* CeEntityHandleResolveSquad(CeEntityHandle handle) {
    const uintptr_t addr = gEntityHandles.Resolve(handle);
    if (addr == 0u) return nullptr;
    CSquad* live = FindLiveSquadInWorld(addr);
    if (!live || IsThingDying(live) || gEntityHandles.StampOf(handle) != ThingStamp(live)) {
        if (live || FindLiveThingInWorld(addr) == nullptr) gEntityHandles.Unregister(addr);
        return nullptr;
    }
    return live;
}

bool CeEntityHandleMatches(const CThing* thing, CeEntityHandle handle) {
    if (!thing || handle == kCeEntityHandleInvalid) return false;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(thing);
    if (!gEntityHandles.Matches(addr, handle)) return false;
    CThing* mutableThing = const_cast<CThing*>(thing);
    if (IsThingDying(mutableThing) || gEntityHandles.StampOf(handle) != ThingStamp(mutableThing)) {
        gEntityHandles.Unregister(addr);
        return false;
    }
    return true;
}

CeEntityHandle CeEntityHandleRegister(CThing* thing) {
    if (!thing || IsThingDying(thing)) return kCeEntityHandleInvalid;
    return gEntityHandles.RegisterFresh(reinterpret_cast<uintptr_t>(thing), ThingStamp(thing));
}

void CeEntityHandleUnregister(const CThing* thing) {
    if (!thing) return;
    gEntityHandles.Unregister(reinterpret_cast<uintptr_t>(thing));
}

void CeEntityHandleUnregisterHandle(CeEntityHandle handle) {
    if (handle == kCeEntityHandleInvalid) return;
    gEntityHandles.UnregisterHandle(handle);
}

void CeEntityHandleResetWorld() {
    gEntityHandles.ResetWorld();
}

uint32_t CeEntityHandleLiveCount() {
    return gEntityHandles.LiveCount();
}

uint64_t CeEntityHandleTotalSpawns() {
    return gEntityHandles.TotalSpawns();
}

uint32_t CeEntityHandleWorldEpoch() {
    return gEntityHandles.WorldEpoch();
}

extern "C" void PortCeEntityHandlesUnitSpawn(CUnit* unit, CInitThing*) {
    if (!unit) return;
    const uintptr_t addr = reinterpret_cast<uintptr_t>(unit);
    if (unit->IsDying() || unit->IsShuttingDown()) {
        gEntityHandles.Unregister(addr);
        return;
    }
    gEntityHandles.RegisterFresh(addr, ThingStamp(unit));
}

extern "C" void PortCeEntityHandlesUnitDestroyed(CUnit* unit) {
    if (unit) gEntityHandles.Unregister(reinterpret_cast<uintptr_t>(unit));
}

extern "C" void PortCeEntityHandlesLevelEnd(int, int) {
    gEntityHandles.ResetWorld();
}

#else

CeEntityHandle CeEntityHandleFor(const CThing*) { return kCeEntityHandleInvalid; }
CeEntityHandle CeEntityHandleLookup(const CThing*) { return kCeEntityHandleInvalid; }
CThing* CeEntityHandleResolve(CeEntityHandle) { return nullptr; }
CUnit* CeEntityHandleResolveUnit(CeEntityHandle) { return nullptr; }
CSquad* CeEntityHandleResolveSquad(CeEntityHandle) { return nullptr; }
bool CeEntityHandleMatches(const CThing*, CeEntityHandle) { return false; }
CeEntityHandle CeEntityHandleRegister(CThing*) { return kCeEntityHandleInvalid; }
void CeEntityHandleUnregister(const CThing*) {}
void CeEntityHandleUnregisterHandle(CeEntityHandle) {}
void CeEntityHandleResetWorld() {}
uint32_t CeEntityHandleLiveCount() { return 0u; }
uint64_t CeEntityHandleTotalSpawns() { return 0u; }
uint32_t CeEntityHandleWorldEpoch() { return 0u; }

extern "C" void PortCeEntityHandlesUnitSpawn(CUnit*, CInitThing*) {}
extern "C" void PortCeEntityHandlesUnitDestroyed(CUnit*) {}
extern "C" void PortCeEntityHandlesLevelEnd(int, int) {}

#endif
