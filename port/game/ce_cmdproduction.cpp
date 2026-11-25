// SPDX-License-Identifier: GPL-3.0-only
#include "common.h"
#include "game.h"
#include "world.h"
#include "unit.h"
#include "thing.h"
#include "SpawnerThng.h"
#include "WorldPhysicsManager.h"
#include "ce_cmdproduction.h"
#include "ce_entity_handles.h"
#include <cstdio>
#include <cstring>

extern "C" bool PortCeFactoryExercise();
bool PortCeExperimental(const char* mode);
bool PortCeLayer() __attribute__((weak));
bool PortCeOneScreenMatch() __attribute__((weak));
namespace {
constexpr int kExerciseLevel = 231;
constexpr const char* kFactoryType = "Forseti Pulse Tank Factory";
constexpr const char* kVehicleType = "AV-14B Sabre Pulse Tank";
constexpr float kAssemblySeconds = 5.0f;
constexpr float kReadyDisplaySeconds = 4.0f;

struct Production {
    const CUnit *vehicle = nullptr;
    bool active = false, selected = false, dispatch = false, hasOwner = false;
    int owner = -1, product = -1, allegiance = -1;
    CeEntityHandle ownerHandle = kCeEntityHandleInvalid;
    CeEntityHandle productHandle = kCeEntityHandleInvalid;
    CSpawner* mount = nullptr;
    CeFactoryStage stage = CE_FACTORY_IDLE;
    float x = 0, y = 0, now = 0, queuedAt = 0, lastSearch = -1;
    bool canQueue = false;
} job;

struct StructureSlot {
    bool active = false;
    bool seen = false;
    bool selected = false;
    bool dispatch = false;
    int thingNumber = 0;
    CeEntityHandle handle = kCeEntityHandleInvalid;
    CUnit* owner = nullptr;
    float x = 0, y = 0;
    float life = 0, maxLife = 0;
    char name[32] = {};
    int optionCount = 0;
    CSpawner* mounts[kCeMaxStructureOptions] = {};
    CeCmdStructureOptionView options[kCeMaxStructureOptions] = {};
    CeFactoryStage stage = CE_FACTORY_IDLE;
    int queuedOption = -1;
    float queuedAt = 0;
    float readyAt = 0;
    int lastProductNumber = 0;
    CeEntityHandle lastProductHandle = kCeEntityHandleInvalid;
    char lastProductName[32] = {};
};

struct StructureRoster {
    bool active = false;
    int level = -1;
    int allegiance = -1;
    float now = 0;
    float lastSearch = -1;
    int count = 0;
    bool readyBell = false;
    StructureSlot slots[kCeMaxStructures] = {};
} gStructures;

bool MatchType(const char* a, const char* b) { return a && std::strcmp(a, b) == 0; }

void CopyLabel(char* dst, unsigned room, const char* src, const char* fallback) {
    if (!dst || room == 0) return;
    const char* text = (src && src[0]) ? src : fallback;
    std::snprintf(dst, room, "%s", text ? text : "");
}

CSpawner* Compatible(CUnit* owner) {
    if (!owner || !owner->GetUnitData() || !MatchType(owner->GetUnitData()->mName, kFactoryType)) return nullptr;
    ListIterator<CSpawner> mounts(&owner->mSpawners);
    for (CSpawner* s = mounts.First(); s; s = mounts.Next()) {
        if (s->mOwner == owner && s->mData && MatchType(s->mData->mThingName, kVehicleType)) return s;
    }
    return nullptr;
}

CUnit* FindFactory() {
    if (job.hasOwner && job.ownerHandle != kCeEntityHandleInvalid) {
        CUnit* u = CeEntityHandleResolveUnit(job.ownerHandle);
        if (u && (u->GetThingType() & (THING_TYPE_BUILDING | THING_TYPE_UNIT)) == (THING_TYPE_BUILDING | THING_TYPE_UNIT) &&
            int(u->GetAllegiance()) == job.allegiance && u->GetLife() > 0 && Compatible(u))
            return u;
        return nullptr;
    }
    ListIterator<CThing> all(&WORLD.GetThingNB());
    for (CThing* t = all.First(); t; t = all.Next()) {
        if ((t->GetThingType() & (THING_TYPE_BUILDING | THING_TYPE_UNIT)) != (THING_TYPE_BUILDING | THING_TYPE_UNIT)) continue;
        CUnit* u = static_cast<CUnit*>(t);
        if (int(u->GetAllegiance()) == job.allegiance && u->GetLife() > 0 && Compatible(u)) return u;
    }
    return nullptr;
}

StructureSlot* FindStructureSlot(int handleId) {
    if (handleId <= 0) return nullptr;
    const CeEntityHandle handle = static_cast<CeEntityHandle>(handleId);
    for (int i = 0; i < gStructures.count; ++i)
        if (gStructures.slots[i].active &&
            (gStructures.slots[i].handle == handle || gStructures.slots[i].thingNumber == handleId))
            return &gStructures.slots[i];
    return nullptr;
}

StructureSlot* FindStructureByMount(const CSpawner* source) {
    if (!source || !gStructures.active) return nullptr;
    CUnit* owner = source->mOwner;
    if (!owner || owner->IsDying() || owner->IsShuttingDown() || owner->GetLife() <= 0 ||
        int(owner->GetAllegiance()) != gStructures.allegiance)
        return nullptr;
    const CeEntityHandle ownerHandle = CeEntityHandleFor(owner);
    if (ownerHandle == kCeEntityHandleInvalid) return nullptr;
    for (int i = 0; i < gStructures.count; ++i) {
        StructureSlot& s = gStructures.slots[i];
        if (!s.active || s.handle != ownerHandle) continue;
        for (int k = 0; k < s.optionCount; ++k)
            if (s.mounts[k] == source) return &s;
    }
    return nullptr;
}

CUnit* FindLiveStructure(int handleId) {
    if (!gStructures.active || handleId == 0) return nullptr;
    CUnit* u = CeEntityHandleResolveUnit(static_cast<CeEntityHandle>(handleId));
    if (!u) return nullptr;
    if ((u->GetThingType() & (THING_TYPE_BUILDING | THING_TYPE_UNIT)) != (THING_TYPE_BUILDING | THING_TYPE_UNIT))
        return nullptr;
    if (!u->IsDying() && !u->IsShuttingDown() && u->GetLife() > 0 && int(u->GetAllegiance()) == gStructures.allegiance)
        return u;
    return nullptr;
}

CSpawner* FindLiveMount(CUnit* owner, int optionIndex) {
    if (!owner || optionIndex < 0 || optionIndex >= kCeMaxStructureOptions) return nullptr;
    int n = 0;
    ListIterator<CSpawner> mounts(&owner->mSpawners);
    for (CSpawner* s = mounts.First(); s && n < kCeMaxStructureOptions; s = mounts.Next()) {
        if (s->mOwner != owner || !s->mData || !s->mData->mThingName || !s->mData->mThingName[0]) continue;
        if (n == optionIndex) return s;
        ++n;
    }
    return nullptr;
}

int CollectMounts(CUnit* owner, CSpawner** outMounts, CeCmdStructureOptionView* outOptions) {
    int n = 0;
    if (!owner) return 0;
    ListIterator<CSpawner> mounts(&owner->mSpawners);
    for (CSpawner* s = mounts.First(); s && n < kCeMaxStructureOptions; s = mounts.Next()) {
        if (s->mOwner != owner || !s->mData || !s->mData->mThingName || !s->mData->mThingName[0]) continue;
        outMounts[n] = s;
        CeCmdStructureOptionView& opt = outOptions[n];
        opt = CeCmdStructureOptionView{};
        CopyLabel(opt.unitName, sizeof(opt.unitName), s->mData->mThingName, "Unit");
        opt.enabled = s->mEnabled != 0;
        opt.spawning = s->Unknown4e4420() != 0;
        const bool exhausted = s->Unknown4e4430() != 0;
        opt.ready = opt.enabled && !exhausted && !opt.spawning;
        if (s->mData->mInfinite) opt.remaining = -1;
        else {
            const int left = int(s->mData->mAmount) - int(s->mUnknown3D8);
            opt.remaining = left > 0 ? left : 0;
        }
        ++n;
    }
    return n;
}

void RefreshStructures(int level, int allegiance, float simulationTime) {
    const bool singleScreen = (!GAME.IsMultiplayer() && GAME.GetNPlayers() == 1) ||
                              (PortCeOneScreenMatch && PortCeOneScreenMatch());
    if (!singleScreen || allegiance < 0) {
        if (gStructures.active) gStructures = StructureRoster();
        return;
    }
    if (gStructures.active && (gStructures.level != level || gStructures.allegiance != allegiance || simulationTime < gStructures.now))
        gStructures = StructureRoster();
    gStructures.active = true;
    gStructures.level = level;
    gStructures.allegiance = allegiance;
    gStructures.now = simulationTime;

    if (gStructures.count == 0 && gStructures.lastSearch >= 0 && simulationTime - gStructures.lastSearch < 0.14f) return;
    gStructures.lastSearch = simulationTime;

    for (int i = 0; i < gStructures.count; ++i) gStructures.slots[i].seen = false;

    ListIterator<CThing> all(&WORLD.GetThingNB());
    for (CThing* t = all.First(); t; t = all.Next()) {
        if ((t->GetThingType() & (THING_TYPE_BUILDING | THING_TYPE_UNIT)) != (THING_TYPE_BUILDING | THING_TYPE_UNIT)) continue;
        CUnit* u = static_cast<CUnit*>(t);
        if (u->IsDying() || u->IsShuttingDown() || u->GetLife() <= 0 || int(u->GetAllegiance()) != allegiance) continue;
        CSpawner* foundMounts[kCeMaxStructureOptions] = {};
        CeCmdStructureOptionView foundOptions[kCeMaxStructureOptions] = {};
        const int mountCount = CollectMounts(u, foundMounts, foundOptions);
        if (mountCount <= 0) continue;

        const CeEntityHandle handle = CeEntityHandleFor(u);
        if (handle == kCeEntityHandleInvalid) continue;
        const int handleId = static_cast<int>(handle);
        StructureSlot* slot = FindStructureSlot(handleId);
        if (!slot) {
            if (gStructures.count >= kCeMaxStructures) continue;
            slot = &gStructures.slots[gStructures.count++];
            *slot = StructureSlot();
            slot->active = true;
            slot->handle = handle;
            slot->thingNumber = handleId;
        }
        slot->seen = true;
        slot->owner = u;
        slot->x = u->GetPos().X;
        slot->y = u->GetPos().Y;
        slot->life = float(u->GetLife());
        slot->maxLife = float(u->GetMaxLife());
        CopyLabel(slot->name, sizeof(slot->name), u->GetUnitData() ? u->GetUnitData()->mName : nullptr, "Factory");
        slot->optionCount = mountCount;
        for (int k = 0; k < mountCount; ++k) {
            slot->mounts[k] = foundMounts[k];
            slot->options[k] = foundOptions[k];
        }
        if (slot->queuedOption >= mountCount) {
            slot->queuedOption = -1;
            if (slot->stage == CE_FACTORY_QUEUED || slot->stage == CE_FACTORY_LAUNCHING)
                slot->stage = CE_FACTORY_BLOCKED;
        }
    }

    // Retire destroyed structures and advance any player-queued build orders.
    int kept = 0;
    for (int i = 0; i < gStructures.count; ++i) {
        StructureSlot& s = gStructures.slots[i];
        if (!s.seen) continue;
        const bool isExerciseOwner = job.active && job.hasOwner && s.handle == job.ownerHandle;
        if (!isExerciseOwner) {
            if (s.stage == CE_FACTORY_READY && simulationTime - s.readyAt >= kReadyDisplaySeconds) {
                s.stage = CE_FACTORY_IDLE;
                s.queuedOption = -1;
            }
            if ((s.stage == CE_FACTORY_QUEUED || s.stage == CE_FACTORY_LAUNCHING) &&
                s.queuedOption >= 0 && s.queuedOption < s.optionCount) {
                CSpawner* mount = s.mounts[s.queuedOption];
                if (!mount || !mount->mEnabled || mount->Unknown4e4430()) {
                    s.stage = CE_FACTORY_BLOCKED;
                    s.queuedOption = -1;
                } else if (s.stage == CE_FACTORY_QUEUED && simulationTime - s.queuedAt >= kAssemblySeconds) {
                    s.stage = CE_FACTORY_LAUNCHING;
                }
                if (s.stage == CE_FACTORY_LAUNCHING && !mount->Unknown4e4420()) {
                    // Explicit Commander build order completed its assembly timer: clear any idle wave delay
                    // so the player's ordered unit deploys cleanly through the native spawner boundary.
                    if (mount->mUnknown3E0 >= simulationTime)
                        mount->mUnknown3E0 = simulationTime - 0.05f;
                    s.dispatch = true;
                    const bool accepted = mount->DoSpawn() != 0;
                    s.dispatch = false;
                    if (!accepted && s.stage == CE_FACTORY_LAUNCHING) {
                        s.stage = CE_FACTORY_BLOCKED;
                        s.queuedOption = -1;
                    }
                }
            }
        }
        if (kept != i) gStructures.slots[kept] = s;
        ++kept;
    }
    for (int i = kept; i < gStructures.count; ++i) gStructures.slots[i] = StructureSlot();
    gStructures.count = kept;
}

int BuildPercentFor(CeFactoryStage stage, float now, float queuedAt) {
    if (stage == CE_FACTORY_LAUNCHING || stage == CE_FACTORY_READY) return 100;
    if (stage != CE_FACTORY_QUEUED) return 0;
    int pct = int(100.f * (now - queuedAt) / kAssemblySeconds);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

void FillStructureView(const StructureSlot& s, CeCmdStructureView& out) {
    out = CeCmdStructureView{};
    out.thingNumber = s.thingNumber;
    out.x = s.x;
    out.y = s.y;
    out.life = s.life;
    out.maxLife = s.maxLife;
    CopyLabel(out.name, sizeof(out.name), s.name, "Factory");
    out.optionCount = s.optionCount;
    for (int k = 0; k < s.optionCount; ++k) out.options[k] = s.options[k];
    const bool isExerciseOwner = job.active && job.hasOwner && s.handle == job.ownerHandle;
    out.isExercise = isExerciseOwner;
    if (isExerciseOwner) {
        out.stage = job.stage;
        out.queuedOption = 0;
        out.percent = BuildPercentFor(job.stage, job.now, job.queuedAt);
        out.selected = job.selected || s.selected;
        out.lastProductNumber = job.productHandle != kCeEntityHandleInvalid ? static_cast<int>(job.productHandle) : job.product;
        CopyLabel(out.lastProductName, sizeof(out.lastProductName), kVehicleType, "Tank");
    } else {
        out.stage = s.stage;
        out.queuedOption = s.queuedOption;
        out.percent = BuildPercentFor(s.stage, gStructures.now, s.queuedAt);
        out.selected = s.selected;
        out.lastProductNumber = s.lastProductNumber;
        CopyLabel(out.lastProductName, sizeof(out.lastProductName), s.lastProductName, "");
    }
}
}

bool CeFactoryScenario() { return job.active; }
void CeFactoryReset() { job = Production(); gStructures = StructureRoster(); }
void CeFactoryTick(int level, int allegiance, float simulationTime) {
    if (level == kExerciseLevel && allegiance == int(kForsetiAllegiance) && PortCeFactoryExercise()) {
        if (job.active && (allegiance != job.allegiance || simulationTime < job.now)) job = Production();
        job.active = true;
        job.allegiance = allegiance;
        job.now = simulationTime;
        if (!(job.lastSearch >= 0 && simulationTime - job.lastSearch < 0.14f)) {
            job.lastSearch = simulationTime;
            CUnit* owner = FindFactory();
            if (!owner) {
                job.mount = nullptr;
                job.canQueue = false;
                if (job.hasOwner) { job.stage = CE_FACTORY_BLOCKED; job.selected = false; }
            } else {
                job.ownerHandle = CeEntityHandleFor(owner);
                job.hasOwner = (job.ownerHandle != kCeEntityHandleInvalid);
                job.owner = job.hasOwner ? static_cast<int>(job.ownerHandle) : owner->GetThingNumber();
                job.mount = Compatible(owner);
                job.x = owner->GetPos().X; job.y = owner->GetPos().Y;
                job.canQueue = job.mount && job.mount->mEnabled && job.mount->Unknown4e43d0();
                if (job.stage == CE_FACTORY_QUEUED && job.now - job.queuedAt >= kAssemblySeconds) {
                    job.stage = CE_FACTORY_LAUNCHING;
                    if (job.mount && job.mount->mEnabled && !job.mount->Unknown4e4430() && !job.mount->Unknown4e4420() &&
                        job.mount->mUnknown3E0 >= simulationTime)
                        job.mount->mUnknown3E0 = simulationTime - 0.05f;
                    job.canQueue = job.mount && job.mount->mEnabled && job.mount->Unknown4e43d0();
                    if (!job.canQueue) { job.stage = CE_FACTORY_BLOCKED; }
                    else {
                        job.dispatch = true;
                        bool accepted = job.mount->DoSpawn() != 0;
                        job.dispatch = false;
                        if (!accepted && job.stage == CE_FACTORY_LAUNCHING) job.stage = CE_FACTORY_BLOCKED;
                    }
                }
            }
        }
    } else if (job.active) {
        job = Production();
    }
    RefreshStructures(level, allegiance, simulationTime);
}

bool CeFactoryRead(CeFactoryView& out) {
    if (!job.active || !job.mount) return false;
    int percent = BuildPercentFor(job.stage, job.now, job.queuedAt);
    out = {job.x,job.y,job.owner,job.product,percent,job.stage,job.selected,job.canQueue};
    return true;
}

// A number or an old address alone is not authority to select a replacement.
bool CeFactoryIsVehicle(CUnit *unit) {
    if (!unit || job.stage != CE_FACTORY_READY || !job.active || unit != job.vehicle) return false;
#ifdef CE_VERSION
    const bool idOk = job.productHandle != kCeEntityHandleInvalid
        ? CeEntityHandleMatches(unit, job.productHandle)
        : (unit->GetThingNumber() == job.product);
#else
    const bool idOk = unit->GetThingNumber() == job.product;
#endif
    return idOk && (unit->GetThingType() & THING_TYPE_UNIT)
        && !unit->IsDying() && unit->GetLife() > 0.f
        && int(unit->GetAllegiance()) == job.allegiance && unit->GetUnitData()
        && MatchType(unit->GetUnitData()->mName, kVehicleType);
}
CUnit *CeFactoryLiveVehicle() {
    if (!job.active || job.stage != CE_FACTORY_READY || !job.vehicle) return nullptr;
    ListIterator<CThing> things(&WORLD.GetThingNB());
    for (CThing *thing = things.First(); thing; thing = things.Next()) {
        if (thing != job.vehicle) continue;
        if (!(thing->GetThingType() & THING_TYPE_UNIT)) return nullptr;
        CUnit *unit = static_cast<CUnit*>(thing);
        return CeFactoryIsVehicle(unit) ? unit : nullptr;
    }
    return nullptr;
}
bool CeFactorySelect() {
    if (!job.active || !job.mount) return false;
    CeCmdStructureClearSelection();
    job.selected = true;
    if (StructureSlot* slot = FindStructureSlot(job.owner)) slot->selected = true;
    return true;
}
void CeFactoryLeaveSelection() {
    job.selected = false;
    CeCmdStructureClearSelection();
}
bool CeFactoryQueue() {
    if (!job.selected || !job.mount || !job.canQueue || job.stage == CE_FACTORY_QUEUED || job.stage == CE_FACTORY_LAUNCHING || job.stage == CE_FACTORY_READY) return false;
    job.stage = CE_FACTORY_QUEUED; job.queuedAt = job.now; job.product = -1; job.productHandle = kCeEntityHandleInvalid; return true;
}
bool CeFactoryCancel() {
    if (!job.selected || job.stage != CE_FACTORY_QUEUED) return false;
    job.stage = CE_FACTORY_CANCELLED; return true;
}

int CeCmdStructureList(CeCmdStructureView* out, int max) {
    if (!gStructures.active || !out || max <= 0) return 0;
    const int n = gStructures.count < max ? gStructures.count : max;
    for (int i = 0; i < n; ++i) FillStructureView(gStructures.slots[i], out[i]);
    return n;
}

bool CeCmdStructureSelected(CeCmdStructureView* out) {
    if (!gStructures.active || !out) return false;
    for (int i = 0; i < gStructures.count; ++i) {
        const StructureSlot& s = gStructures.slots[i];
        const bool isExerciseSel = job.active && job.selected && job.hasOwner && s.handle == job.ownerHandle;
        if (s.selected || isExerciseSel) {
            FillStructureView(s, *out);
            return true;
        }
    }
    return false;
}

bool CeCmdStructureSelect(int thingNumber) {
    if (!gStructures.active) return false;
    StructureSlot* target = FindStructureSlot(thingNumber);
    if (!target) return false;
    for (int i = 0; i < gStructures.count; ++i) gStructures.slots[i].selected = false;
    target->selected = true;
    job.selected = (job.active && job.hasOwner && target->handle == job.ownerHandle);
    return true;
}

void CeCmdStructureClearSelection() {
    for (int i = 0; i < gStructures.count; ++i) gStructures.slots[i].selected = false;
    job.selected = false;
}

bool CeCmdStructureQueue(int thingNumber, int optionIndex) {
    if (!gStructures.active) return false;
    StructureSlot* target = FindStructureSlot(thingNumber);
    if (!target) return false;
    if (job.active && job.hasOwner && target->handle == job.ownerHandle) {
        if (!job.selected) CeFactorySelect();
        return CeFactoryQueue();
    }
    if (optionIndex < 0 || optionIndex >= target->optionCount) return false;
    if (target->stage == CE_FACTORY_QUEUED || target->stage == CE_FACTORY_LAUNCHING) return false;
    // Resolve both the building and its spawner mount live from WORLD rather than trusting cached pointers.
    CUnit* owner = FindLiveStructure(static_cast<int>(target->handle));
    CSpawner* mount = FindLiveMount(owner, optionIndex);
    if (!owner || !mount || !mount->mEnabled || mount->Unknown4e4430() || mount->Unknown4e4420()) return false;
    target->owner = owner;
    target->mounts[optionIndex] = mount;
    target->stage = CE_FACTORY_QUEUED;
    target->queuedOption = optionIndex;
    target->queuedAt = gStructures.now;
    target->lastProductNumber = 0;
    target->lastProductHandle = kCeEntityHandleInvalid;
    CopyLabel(target->lastProductName, sizeof(target->lastProductName), mount->mData->mThingName, "Unit");
    return true;
}

bool CeCmdStructureCancel(int thingNumber) {
    if (!gStructures.active) return false;
    StructureSlot* target = FindStructureSlot(thingNumber);
    if (!target) return false;
    if (job.active && job.hasOwner && target->handle == job.ownerHandle) return CeFactoryCancel();
    if (target->stage != CE_FACTORY_QUEUED) return false;
    target->stage = CE_FACTORY_CANCELLED;
    target->queuedOption = -1;
    return true;
}

bool CeCmdStructureConsumeReady() {
    if (!gStructures.readyBell) return false;
    gStructures.readyBell = false;
    return true;
}

bool CeFactoryMayStart(const CSpawner* source) {
    if (!source) return true;
    if (source == job.mount && job.active) return job.dispatch;
    if (StructureSlot* slot = FindStructureByMount(source)) {
        if ((slot->stage == CE_FACTORY_QUEUED || slot->stage == CE_FACTORY_LAUNCHING) &&
            slot->queuedOption >= 0 && slot->queuedOption < slot->optionCount &&
            slot->mounts[slot->queuedOption] == source)
            return slot->dispatch;
    }
    // Scenario scripts may start a batch during world creation, before the first player tick.
    // Reserve only this authored mount; other factions and other factories keep their usual rules.
    if ((PortCeLayer && !PortCeLayer()) || GAME.GetCurrentLevel() != kExerciseLevel || GAME.IsMultiplayer() || GAME.GetNPlayers() != 1 ||
        !PortCeFactoryExercise() || !PortCeExperimental("commander")) return true;
    if (!source->mData || !source->mOwner || !MatchType(source->mData->mThingName,kVehicleType)) return true;
    CUnit* owner=source->mOwner;
    return owner->GetAllegiance() != kForsetiAllegiance || !owner->GetUnitData() || !MatchType(owner->GetUnitData()->mName,kFactoryType);
}
bool CeFactorySingleBatch(const CSpawner* source) { return job.active && source && source == job.mount && job.stage == CE_FACTORY_READY; }
void CeFactoryProduced(const CSpawner* source, CThing* child) {
    if (!child || !source || !(child->GetThingType() & THING_TYPE_UNIT)) return;
    CUnit* unit = static_cast<CUnit*>(child);
    if (job.active && source == job.mount && job.stage == CE_FACTORY_LAUNCHING) {
        if (int(unit->GetAllegiance()) == job.allegiance && unit->GetUnitData() && MatchType(unit->GetUnitData()->mName, kVehicleType)) {
            job.vehicle = unit; job.product = child->GetThingNumber(); job.productHandle = CeEntityHandleFor(child); job.stage = CE_FACTORY_READY;
            gStructures.readyBell = true;
            return;
        }
    }
    if (StructureSlot* slot = FindStructureByMount(source)) {
        if (slot->stage == CE_FACTORY_LAUNCHING &&
            slot->queuedOption >= 0 && slot->queuedOption < slot->optionCount &&
            slot->mounts[slot->queuedOption] == source &&
            int(unit->GetAllegiance()) == gStructures.allegiance) {
            slot->lastProductHandle = CeEntityHandleFor(child);
            slot->lastProductNumber = slot->lastProductHandle != kCeEntityHandleInvalid ? static_cast<int>(slot->lastProductHandle) : child->GetThingNumber();
            CopyLabel(slot->lastProductName, sizeof(slot->lastProductName),
                      unit->GetUnitData() ? unit->GetUnitData()->mName : nullptr, slot->lastProductName);
            slot->stage = CE_FACTORY_READY;
            slot->readyAt = gStructures.now;
            gStructures.readyBell = true;
        }
    }
}
