#include "common.h"
#include "unit.h"
#include "squad.h"
#include "Guide.h"
#include "ce_commander.h"
#include "ce_cmdorders.h"
#if !defined(CE_CMDUNIT_ORDERS_FIXTURE_H)
#include "ce_entity_handles.h"
#endif

// The unit order adapter belongs to the CE layer (CE_VERSION), as the rest of the Commander does (ce_commander.h). Patch 0278 calls its two native
// hooks from every build, so a build without the layer (the private, faithful one) gets them inert at the end of this file: nothing is tracked there,
// a guide always takes its point and a thing that goes needs no forgetting.
#ifdef CE_VERSION

#if defined(CE_COMMANDER_TEST) && defined(__linux__)
// A test build keeps the order adapter's own records (its identity table, its watch flag, its collect buffer) in the Commander's sections, which
// the live view's footprint audit leaves out (ce_cmdview_audit.cpp), as ce_cmdmap.cpp's and ce_cmdview.cpp's are.
#pragma clang section bss = "cecmdb" data = "cecmdd"
#endif

namespace {
constexpr unsigned kIdentities = 2048;
constexpr unsigned kOrders = 64;
CeCommandLifetimeTable<kIdentities> gIdentities;
bool gWatching = false;
struct Applying;
Applying* gApplying = NULL;
uint64_t gNextOrderSerial = 1;

struct UnitOrder {
    bool used;
    CeCommandLifetimeKey key;
    uint64_t serial;
    CUnit* unit;
    CGuide* guide;
    const void* squad;
    const void* script;
    int allegiance;
    int aiState;
    unsigned kind;
    CeCmdUnitOrderKind order;
    float point[3] = {};
};
UnitOrder gOrders[kOrders] = {};
unsigned gOrderCount = 0;

bool SameKey(const CeCommandLifetimeKey& a, const CeCommandLifetimeKey& b) {
    return a.world == b.world && a.id == b.id && a.generation == b.generation;
}
uint64_t ThingIdentityId(int number) {
    return static_cast<uint64_t>(static_cast<uint32_t>(number));
}
#if !defined(CE_CMDUNIT_ORDERS_FIXTURE_H)
uint64_t UnitIdentityId(const CUnit* unit, int fallbackNumber) {
    const CeEntityHandle h = CeEntityHandleFor(unit);
    return h != kCeEntityHandleInvalid ? static_cast<uint64_t>(h) : ThingIdentityId(fallbackNumber);
}
uint64_t SquadIdentityId(const void* squad, int fallbackNumber) {
    const CeEntityHandle h = CeEntityHandleFor(static_cast<const CSquad*>(squad));
    return h != kCeEntityHandleInvalid ? static_cast<uint64_t>(h) : ThingIdentityId(fallbackNumber);
}
#else
uint64_t UnitIdentityId(const CUnit*, int fallbackNumber) {
    return ThingIdentityId(fallbackNumber);
}
uint64_t SquadIdentityId(const void*, int fallbackNumber) {
    return ThingIdentityId(fallbackNumber);
}
#endif
void Clear(UnitOrder& order) {
    if (!order.used) return;
    order.used = false;
    if (gOrderCount) --gOrderCount;
}
UnitOrder* Find(const CeCommandLifetimeKey& key) {
    for (unsigned i = 0; i != kOrders; ++i)
        if (gOrders[i].used && SameKey(gOrders[i].key, key)) return &gOrders[i];
    return NULL;
}
CUnit* Resolve(const CeCommandLifetimeKey& key, CeCmdUnitAuthority* authority) {
    CeCommandLifetimeRef ref = {};
    if (!gWatching || !CeCmdUnitOrdersEligible() ||
        !gIdentities.Resolve(key, &ref)) return NULL;
    CUnit* candidate = reinterpret_cast<CUnit*>(ref.address);
    CeCmdUnitAuthority current = {};
    // This query finds the exact pointer in current native membership before
    // reading it. The destruction hook separately rejects reused lifetimes.
    if (!CeCmdUnitAuthorityNow(candidate, &current) || !current.found ||
        UnitIdentityId(candidate, current.number) != key.id ||
        current.kind != ref.kind || candidate->IsDying() ||
        candidate->GetLife() <= 0) return NULL;
    if (authority) *authority = current;
    return candidate;
}
bool Current(const UnitOrder& order, CUnit* unit) {
    CeCommandLifetimeRef ref = {};
    return order.used && unit && gIdentities.Resolve(order.key, &ref) &&
        ref.address == reinterpret_cast<uintptr_t>(unit) &&
        unit == order.unit && !unit->IsShuttingDown() && unit->mGuide == order.guide &&
        order.guide && order.guide->GetUnit() == unit &&
        !unit->IsDying() && unit->GetLife() > 0 &&
        UnitIdentityId(unit, unit->GetThingNumber()) == order.key.id &&
        static_cast<unsigned>(unit->GetThingType()) == order.kind &&
        unit->GetSquad() == order.squad && unit->GetSquad() &&
        !unit->GetSquad()->IsShuttingDown() &&
        unit->GetMissionScript() == order.script &&
        unit->GetAllegiance() == order.allegiance &&
        static_cast<int>(unit->GetAIState()) == order.aiState;
}
UnitOrder* Space(const CeCommandLifetimeKey& key) {
    UnitOrder* existing = Find(key);
    if (existing) return existing;
    for (unsigned i = 0; i != kOrders; ++i)
        if (!gOrders[i].used) return &gOrders[i];
    // Resolve through current membership before reading an old native pointer.
    for (unsigned i = 0; i != kOrders; ++i) {
        CeCmdUnitAuthority authority = {};
        CUnit* unit = Resolve(gOrders[i].key, &authority);
        if (!unit || !authority.commandable || !Current(gOrders[i], unit)) {
            Clear(gOrders[i]);
            return &gOrders[i];
        }
    }
    return NULL;
}
bool Assign(UnitOrder& order, const CeCommandLifetimeKey& key, CUnit* unit,
            CeCmdUnitOrderKind kind, const float point[3]) {
    if (!gNextOrderSerial) return false;
    if (!order.used) ++gOrderCount;
    order.used = true;
    order.key = key;
    order.serial = gNextOrderSerial++;
    order.unit = unit;
    order.guide = unit->mGuide;
    order.squad = unit->GetSquad();
    order.script = unit->GetMissionScript();
    order.allegiance = unit->GetAllegiance();
    order.aiState = static_cast<int>(unit->GetAIState());
    order.kind = static_cast<unsigned>(unit->GetThingType());
    order.order = kind;
    for (unsigned i = 0; i != 3; ++i) order.point[i] = point[i];
    return true;
}
bool Complete(UnitOrder& order, const CeCommandLifetimeKey& key,
              uint64_t serial, CUnit* unit, bool observed) {
    if (!order.used || !SameKey(order.key, key) || order.serial != serial)
        return false;
    if (observed && CeCmdUnitOrdersEligible() && Current(order, unit))
        return true;
    Clear(order);
    return false;
}
struct Applying {
    Applying* previous;
    CGuide* guide;
    CeCommandLifetimeKey key;
    uint64_t serial;
    bool consumed;
    Applying(CGuide* value, const CeCommandLifetimeKey& identity, uint64_t revision)
        : previous(gApplying), guide(value), key(identity), serial(revision),
          consumed(false) { gApplying = this; }
    ~Applying() {
        if (gApplying != this) return;
        CeCommandLifetimeRef ref = {};
        gApplying = previous && gIdentities.Resolve(previous->key, &ref)
            ? previous : NULL;
    }
};
} // namespace

bool CeCmdUnitWatch(CUnit* unit, CeCommandLifetimeKey* key,
                    CeCmdUnitAuthority* authority) {
    if (!key || !CeCmdUnitOrdersEligible()) return false;
    CeCmdUnitAuthority current = {};
    if (!CeCmdUnitAuthorityNow(unit, &current) || !current.found) return false;
    CeCommandLifetimeKey candidate = {};
    if (!gIdentities.Observe(reinterpret_cast<uintptr_t>(unit),
            UnitIdentityId(unit, current.number), current.kind, &candidate))
        return false;
    gWatching = true;
    *key = candidate;
    if (authority) *authority = current;
    return true;
}

uint64_t CeCmdUnitWorld() { return gIdentities.World(); }

bool CeCmdUnitVisibleKey(CUnit* unit, CeCommandLifetimeKey* key) {
    if (!unit || !key || !CeCmdUnitOrdersEligible() || unit->IsShuttingDown() || unit->IsDying() ||
        unit->GetLife() <= 0) return false;
    if (!gIdentities.Observe(reinterpret_cast<uintptr_t>(unit),
            UnitIdentityId(unit, unit->GetThingNumber()),
            static_cast<unsigned>(unit->GetThingType()), key)) return false;
    gWatching = true;
    return true;
}

int CeCmdUnitCollect(CeCmdUnitSnapshot* out, int capacity, bool* complete) {
    if (complete) *complete = false;
    if (!out || capacity <= 0 || !complete || !CeCmdUnitOrdersEligible()) return 0;
    // One native walk produces all current facts; no UI pointer survives it.
    static CeCmdUnitNativeRecord native[1024];
    bool all = false;
    const int limit = capacity < 1024 ? capacity : 1024;
    const int count = CeCmdUnitCollectNative(native, limit, &all);
    if (count < 0 || count > limit) return 0;
    int accepted = 0;
    for (int i = 0; i < count; ++i) {
        const CeCmdUnitNativeRecord& item = native[i];
        const CeCmdUnitAuthority& authority = item.authority;
        if (!item.unit || !authority.found || !authority.squad) { all = false; continue; }
        CeCmdUnitSnapshot result = {};
        if (!gIdentities.Observe(reinterpret_cast<uintptr_t>(item.unit),
                UnitIdentityId(item.unit, authority.number), authority.kind, &result.key)) {
            all = false; continue;
        }
        gWatching = true;
        if (!gIdentities.Observe(reinterpret_cast<uintptr_t>(authority.squad),
                SquadIdentityId(authority.squad, item.squadNumber), item.squadKind, &result.squadKey)) {
            all = false; continue;
        }
        result.ordinal = authority.ordinal;
        result.members = item.members;
        result.why = authority.why;
        result.friendly = authority.friendly;
        result.scripted = authority.scripted;
        result.commandable = authority.commandable;
        result.groundPoint = authority.groundPoint;
        for (unsigned j = 0; j < 3; ++j) result.position[j] = item.position[j];
        for (unsigned j = 0; j < sizeof(result.name); ++j) result.name[j] = item.name[j];
        result.name[sizeof(result.name) - 1] = 0;
        result.life = item.life;
        result.maxLife = item.maxLife;
        UnitOrder* order = Find(result.key);
        if (order && (!authority.commandable || !Current(*order, item.unit))) {
            Clear(*order); order = NULL;
        }
        result.orderOwned = order != NULL;
        out[accepted++] = result;
    }
    *complete = all;
    return accepted;
}

bool CeCmdUnitInspect(const CeCommandLifetimeKey& key, CeCmdUnitSnapshot* out) {
    if (!out) return false;
    static CeCmdUnitSnapshot current[1024];
    bool complete = false;
    const int count = CeCmdUnitCollect(current, 1024, &complete);
    if (!complete) return false;
    for (int i = 0; i < count; ++i)
        if (SameKey(current[i].key, key)) { *out = current[i]; return true; }
    return false;
}

bool CeCmdUnitVisit(const CeCommandLifetimeKey& key,
                    bool (*visitor)(CUnit*, int, void*), void* user) {
    if (!visitor) return false;
    CeCmdUnitAuthority authority = {};
    CUnit* unit = Resolve(key, &authority);
    return unit && visitor(unit, authority.number, user);
}

bool CeCmdUnitOrderRead(const CeCommandLifetimeKey& key, CeCmdUnitOrderView* out) {
    if (!out) return false;
    CeCmdUnitAuthority authority = {};
    CUnit* unit = Resolve(key, &authority);
    if (!unit) {
        if (UnitOrder* stale = Find(key)) Clear(*stale);
        return false;
    }
    CeCmdUnitOrderView result = {};
    result.key = key;
    UnitOrder* order = Find(key);
    if (order && (!authority.commandable || !Current(*order, unit))) {
        Clear(*order);
        order = NULL;
    }
    if (order) {
        result.owned = true;
        result.order = order->order;
        for (unsigned i = 0; i != 3; ++i) result.destination[i] = order->point[i];
    }
    *out = result;
    return true;
}

bool CeCmdUnitMove(const CeCommandLifetimeKey& key, float x, float y) {
    CeCmdUnitAuthority authority = {};
    CUnit* unit = Resolve(key, &authority);
    float point[3] = {};
    if (!unit || !authority.commandable || !authority.groundPoint ||
        !unit->mGuide || unit->mGuide->GetUnit() != unit ||
        !CeCmdUnitGroundPoint(x, y, point)) return false;
    UnitOrder* order = Space(key);
    if (!order) return false;
    if (!Assign(*order, key, unit, CE_UNIT_ORDER_MOVE, point)) return false;
    const uint64_t serial = order->serial;
    Applying applying(unit->mGuide, key, serial);
    unit->GoToPoint(FVector(point[0], point[1], point[2]), TRUE);
    return Complete(*order, key, serial, unit, applying.consumed);
}

bool CeCmdUnitHold(const CeCommandLifetimeKey& key) {
    CeCmdUnitAuthority authority = {};
    CUnit* unit = Resolve(key, &authority);
    if (!unit || !authority.commandable || !authority.groundPoint ||
        !unit->mGuide || unit->mGuide->GetUnit() != unit) return false;
    UnitOrder* order = Space(key);
    if (!order) return false;
    const FVector position = unit->GetPos();
    const float point[3] = {position.X, position.Y, position.Z};
    if (!Assign(*order, key, unit, CE_UNIT_ORDER_HOLD, point)) return false;
    const uint64_t serial = order->serial;
    unit->Stop();
    return Complete(*order, key, serial, unit, true);
}

bool CeCmdUnitReturnAI(const CeCommandLifetimeKey& key) {
    CeCmdUnitAuthority authority = {};
    CUnit* unit = Resolve(key, &authority);
    UnitOrder* order = Find(key);
    if (!order) return false;
    if (!unit) { Clear(*order); return false; }
    const bool current = authority.commandable && Current(*order, unit);
    const UnitOrder previous = *order;
    Clear(*order);
    if (!current) return false;
    // Stop only this temporary guide destination. The unit stays in its current
    // native squad and keeps its current AI state and any later squad order.
    unit->Stop();
    CUnit* after = Resolve(key, &authority);
    return after && !Find(key) && authority.commandable && Current(previous, after);
}

void CeCmdUnitOrderRelease(CUnit* unit) {
    if (!gOrderCount || !unit) return;
    for (unsigned i = 0; i != kOrders; ++i)
        if (gOrders[i].used && gOrders[i].unit == unit) Clear(gOrders[i]);
}

extern "C" bool PortCeCmdGuidePoint(CGuide* guide, bool nativeOverride) {
    if (!gOrderCount || !guide) return true;
    for (unsigned i = 0; i != kOrders; ++i) {
        UnitOrder& order = gOrders[i];
        if (!order.used || order.guide != guide) continue;
        if (!CeCmdUnitOrdersEligible() || !Current(order, guide->GetUnit())) {
            Clear(order);
            continue;
        }
        // Only the adapter's first guide write is admitted by its dispatch.
        // Reentrant callbacks then obey ordinary ownership and override rules.
        if (gApplying && !gApplying->consumed && gApplying->guide == guide &&
            SameKey(gApplying->key, order.key) && gApplying->serial == order.serial) {
            gApplying->consumed = true;
            return true;
        }
        // An explicit native/script override supersedes this temporary order.
        if (nativeOverride) { Clear(order); return true; }
        return false;
    }
    return true;
}

extern "C" void PortCeCmdThingGone(const void* thing) {
    if (!thing) return;
    CeCmdSquadGone(thing);
    if (!gWatching) return;
    gIdentities.Forget(reinterpret_cast<uintptr_t>(thing));
    for (unsigned i = 0; i != kOrders; ++i)
        if (gOrders[i].used &&
            (gOrders[i].unit == thing || gOrders[i].squad == thing))
            Clear(gOrders[i]);
}

void CeCmdUnitOrdersWorldEnd() {
    gWatching = false;
    gApplying = NULL;
    for (unsigned i = 0; i != kOrders; ++i) Clear(gOrders[i]);
    gIdentities.EndWorld();
    gNextOrderSerial = 1;
}

#else

extern "C" bool PortCeCmdGuidePoint(CGuide*, bool) { return true; }
extern "C" void PortCeCmdThingGone(const void*) {}

#endif
