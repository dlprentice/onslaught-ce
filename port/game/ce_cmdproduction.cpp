#include "common.h"
#include "game.h"
#include "world.h"
#include "unit.h"
#include "thing.h"
#include "SpawnerThng.h"
#include "WorldPhysicsManager.h"
#include "ce_cmdproduction.h"
#include <cstring>

extern "C" bool PortCeFactoryExercise();
bool PortCeExperimental(const char* mode);
namespace {
constexpr int kExerciseLevel = 231;
constexpr const char* kFactoryType = "Forseti Pulse Tank Factory";
constexpr const char* kVehicleType = "AV-14B Sabre Pulse Tank";
constexpr float kAssemblySeconds = 5.0f;
struct Production {
    const CUnit *vehicle = nullptr;
    bool active = false, selected = false, dispatch = false;
    int owner = -1, product = -1, allegiance = -1;
    CSpawner* mount = nullptr;
    CeFactoryStage stage = CE_FACTORY_IDLE;
    float x = 0, y = 0, now = 0, queuedAt = 0, lastSearch = -1;
    bool canQueue = false;
} job;

bool MatchType(const char* a, const char* b) { return a && std::strcmp(a, b) == 0; }
CSpawner* Compatible(CUnit* owner) {
    if (!owner || !owner->GetUnitData() || !MatchType(owner->GetUnitData()->mName, kFactoryType)) return nullptr;
    ListIterator<CSpawner> mounts(&owner->mSpawners);
    for (CSpawner* s = mounts.First(); s; s = mounts.Next()) {
        if (s->mOwner == owner && s->mData && MatchType(s->mData->mThingName, kVehicleType)) return s;
    }
    return nullptr;
}
CUnit* FindFactory() {
    ListIterator<CThing> all(&WORLD.GetThingNB());
    for (CThing* t = all.First(); t; t = all.Next()) {
        if ((t->GetThingType() & (THING_TYPE_BUILDING | THING_TYPE_UNIT)) != (THING_TYPE_BUILDING | THING_TYPE_UNIT)) continue;
        if (job.owner >= 0 && t->GetThingNumber() != job.owner) continue;
        CUnit* u = static_cast<CUnit*>(t);
        if (int(u->GetAllegiance()) == job.allegiance && u->GetLife() > 0 && Compatible(u)) return u;
    }
    return nullptr;
}
}

bool CeFactoryScenario() { return job.active; }
void CeFactoryReset() { job = Production(); }
void CeFactoryTick(int level, int allegiance, float simulationTime) {
    if (level != kExerciseLevel || allegiance != int(kForsetiAllegiance) || !PortCeFactoryExercise()) { if (job.active) CeFactoryReset(); return; }
    if (job.active && (allegiance != job.allegiance || simulationTime < job.now)) CeFactoryReset();
    job.active = true;
    job.allegiance = allegiance;
    job.now = simulationTime;
    if (job.lastSearch >= 0 && simulationTime - job.lastSearch < 0.14f) return;
    job.lastSearch = simulationTime;
    CUnit* owner = FindFactory();
    if (!owner) {
        job.mount = nullptr;
        job.canQueue = false;
        if (job.owner >= 0) { job.stage = CE_FACTORY_BLOCKED; job.selected = false; }
        return;
    }
    job.owner = owner->GetThingNumber();
    job.mount = Compatible(owner);
    job.x = owner->GetPos().X; job.y = owner->GetPos().Y;
    job.canQueue = job.mount && job.mount->mEnabled && job.mount->Unknown4e43d0();
    if (job.stage == CE_FACTORY_QUEUED && job.now - job.queuedAt >= kAssemblySeconds) {
        job.stage = CE_FACTORY_LAUNCHING;
        if (!job.canQueue) { job.stage = CE_FACTORY_BLOCKED; return; }
        job.dispatch = true;
        bool accepted = job.mount->DoSpawn() != 0;
        job.dispatch = false;
        if (!accepted && job.stage == CE_FACTORY_LAUNCHING) job.stage = CE_FACTORY_BLOCKED;
    }
}
bool CeFactoryRead(CeFactoryView& out) {
    if (!job.active || !job.mount) return false;
    int percent = job.stage == CE_FACTORY_QUEUED ? int(100*(job.now-job.queuedAt)/kAssemblySeconds) : 0;
    if (job.stage == CE_FACTORY_LAUNCHING || job.stage == CE_FACTORY_READY) percent = 100;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    out = {job.x,job.y,job.owner,job.product,percent,job.stage,job.selected,job.canQueue};
    return true;
}
// A number or an old address alone is not authority to select a replacement.
bool CeFactoryIsVehicle(CUnit *unit) {
    if (!unit || job.stage != CE_FACTORY_READY || !job.active || unit != job.vehicle) return false;
    return unit->GetThingNumber() == job.product && (unit->GetThingType() & THING_TYPE_UNIT)
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
bool CeFactorySelect() { if (!job.active || !job.mount) return false; job.selected = true; return true; }
void CeFactoryLeaveSelection() { job.selected = false; }
bool CeFactoryQueue() {
    if (!job.selected || !job.mount || !job.canQueue || job.stage == CE_FACTORY_QUEUED || job.stage == CE_FACTORY_LAUNCHING || job.stage == CE_FACTORY_READY) return false;
    job.stage = CE_FACTORY_QUEUED; job.queuedAt = job.now; job.product = -1; return true;
}
bool CeFactoryCancel() {
    if (!job.selected || job.stage != CE_FACTORY_QUEUED) return false;
    job.stage = CE_FACTORY_CANCELLED; return true;
}
bool CeFactoryMayStart(const CSpawner* source) {
    if (!source) return true;
    if (source == job.mount && job.active) return job.dispatch;
    // Scenario scripts may start a batch during world creation, before the first player tick.
    // Reserve only this authored mount; other factions and other factories keep their usual rules.
    if (GAME.GetCurrentLevel() != kExerciseLevel || GAME.IsMultiplayer() || GAME.GetNPlayers() != 1 ||
        !PortCeFactoryExercise() || !PortCeExperimental("commander")) return true;
    if (!source->mData || !source->mOwner || !MatchType(source->mData->mThingName,kVehicleType)) return true;
    CUnit* owner=source->mOwner;
    return owner->GetAllegiance() != kForsetiAllegiance || !owner->GetUnitData() || !MatchType(owner->GetUnitData()->mName,kFactoryType);
}
bool CeFactorySingleBatch(const CSpawner* source) { return job.active && source && source == job.mount && job.stage == CE_FACTORY_READY; }
void CeFactoryProduced(const CSpawner* source, CThing* child) {
    if (!child || !job.active || source != job.mount || job.stage != CE_FACTORY_LAUNCHING || !(child->GetThingType() & THING_TYPE_UNIT)) return;
    CUnit* unit = static_cast<CUnit*>(child);
    if (int(unit->GetAllegiance()) != job.allegiance || !unit->GetUnitData() || !MatchType(unit->GetUnitData()->mName,kVehicleType)) return;
    job.vehicle = unit; job.product = child->GetThingNumber(); job.stage = CE_FACTORY_READY;
}
