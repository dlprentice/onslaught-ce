// SPDX-License-Identifier: GPL-3.0-only
// The Commander camera fixture supplies one building and one mount. The actual factory
// owner below decides whether that world is eligible, which actions work and what a view shows.
#include <cstring>
#include <vector>

namespace {
enum { THING_TYPE_UNIT = 1, THING_TYPE_BUILDING = 2, kForsetiAllegiance = 1 };
struct Position { float X, Y; };
struct UnitData { const char* mName; };
struct MountData { const char* mThingName; };
}

class CSpawner;
class CThing {
public:
    int kind = 0, number = 0;
    int GetThingType() const { return kind; }
    int GetThingNumber() const { return number; }
};
class CUnit : public CThing {
public:
    bool IsDying() const { return false; }
    int allegiance = kForsetiAllegiance;
    UnitData data = {};
    Position position = {};
    std::vector<CSpawner*> mSpawners;
    UnitData* GetUnitData() { return &data; }
    int GetAllegiance() const { return allegiance; }
    int GetLife() const { return 1; }
    const Position& GetPos() const { return position; }
};
// This camera fixture contains no available delivered vehicle.
bool CePlayUnitCanTakeDelivery() { return false; }
bool CePlayUnitTakeDelivery() { return false; }
int CeCmdDeliveredSquad() { return -1; }

class CSpawner {
public:
    CUnit* mOwner = nullptr;
    MountData* mData = nullptr;
    bool mEnabled = true, ready = true;
    bool Unknown4e43d0() const { return ready; }
    int DoSpawn() { return 0; }  // This camera fixture does not create game units.
};

namespace {
template<class T> class ListIterator {
    const std::vector<T*>& items;
    std::size_t index;
public:
    explicit ListIterator(const std::vector<T*>* source) : items(*source), index(0) {}
    T* First() { index = 0; return Next(); }
    T* Next() { return index < items.size() ? items[index++] : nullptr; }
};
struct CameraFactoryWorld {
    std::vector<CThing*> things;
    std::vector<CThing*>& GetThingNB() { return things; }
} WORLD;
struct CameraFactoryGame {
    int level = -1;
    int GetCurrentLevel() const { return level; }
    bool IsMultiplayer() const { return false; }
    int GetNPlayers() const { return 1; }
} GAME;
bool exercise = false;
CUnit building;
CSpawner mount;
MountData mount_data;
}

extern "C" bool PortCeFactoryExercise() { return exercise; }
#include "ce_cmdproduction.cpp"

void CameraFactoryWorld(bool enabled, bool hasTankMount) {
    CeFactoryReset();
    exercise = enabled;
    GAME.level = kExerciseLevel;
    building = CUnit();
    building.kind = THING_TYPE_BUILDING | THING_TYPE_UNIT;
    building.number = 71;
    building.data.mName = kFactoryType;
    building.position = {217, 223};
    mount = CSpawner();
    mount_data.mThingName = hasTankMount ? kVehicleType : "Authored different mount";
    mount.mOwner = &building;
    mount.mData = &mount_data;
    building.mSpawners.push_back(&mount);
    WORLD.things.assign(1, &building);
    CeFactoryTick(GAME.level, building.allegiance, 1);
}
