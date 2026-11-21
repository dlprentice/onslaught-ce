// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Friendly structure production for Commander Mode:
// - Preserves the Level 231 factory exercise contract (PortCeFactoryExercise()).
// - Generalizes selectable friendly production buildings (any live friendly building
//   with CSpawner mounts) across solo missions while Commander Mode is enabled.
enum CeFactoryStage { CE_FACTORY_IDLE, CE_FACTORY_QUEUED, CE_FACTORY_LAUNCHING,
                      CE_FACTORY_READY, CE_FACTORY_BLOCKED, CE_FACTORY_CANCELLED };

static constexpr int kCeMaxStructures = 16;
static constexpr int kCeMaxStructureOptions = 4;

struct CeFactoryView {
    float x, y;
    int owner, product, percent;
    CeFactoryStage stage;
    bool selected, available;
};

struct CeCmdStructureOptionView {
    char unitName[32];
    bool enabled;
    bool ready;
    bool spawning;
    int remaining; // -1 for infinite, >= 0 otherwise
};

struct CeCmdStructureView {
    int thingNumber;
    float x, y;
    float life, maxLife;
    char name[32];
    int optionCount;
    CeCmdStructureOptionView options[kCeMaxStructureOptions];
    CeFactoryStage stage;
    int queuedOption;
    int percent;
    bool selected;
    bool isExercise;
    int lastProductNumber;
    char lastProductName[32];
};

void CeFactoryReset();
void CeFactoryTick(int level, int allegiance, float simulationTime);
bool CeFactoryRead(CeFactoryView& out);
bool CeFactorySelect();
bool CeFactoryScenario();
void CeFactoryLeaveSelection();
bool CeFactoryQueue();
bool CeFactoryCancel();

// Generalized Commander production structures across any solo mission.
int CeCmdStructureList(CeCmdStructureView* out, int max);
bool CeCmdStructureSelected(CeCmdStructureView* out);
bool CeCmdStructureSelect(int thingNumber);
void CeCmdStructureClearSelection();
bool CeCmdStructureQueue(int thingNumber, int optionIndex);
bool CeCmdStructureCancel(int thingNumber);
bool CeCmdStructureConsumeReady();

class CUnit;
CUnit *CeFactoryLiveVehicle();
bool CeFactoryIsVehicle(CUnit *unit);

class CSpawner;
class CThing;
// Called at the native spawner boundary; all other spawners keep their usual behavior.
bool CeFactoryMayStart(const CSpawner* source);
void CeFactoryProduced(const CSpawner* source, CThing* child);
bool CeFactorySingleBatch(const CSpawner* source);
