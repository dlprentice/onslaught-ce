#pragma once

// A single friendly factory in an explicitly selected Commander exercise.
enum CeFactoryStage { CE_FACTORY_IDLE, CE_FACTORY_QUEUED, CE_FACTORY_LAUNCHING,
                      CE_FACTORY_READY, CE_FACTORY_BLOCKED, CE_FACTORY_CANCELLED };
struct CeFactoryView {
    float x, y;
    int owner, product, percent;
    CeFactoryStage stage;
    bool selected, available;
};
void CeFactoryReset();
void CeFactoryTick(int level, int allegiance, float simulationTime);
bool CeFactoryRead(CeFactoryView& out);
bool CeFactorySelect();
bool CeFactoryScenario();
void CeFactoryLeaveSelection();
bool CeFactoryQueue();
bool CeFactoryCancel();

class CUnit;
CUnit *CeFactoryLiveVehicle();
bool CeFactoryIsVehicle(CUnit *unit);

class CSpawner;
class CThing;
// Called at the native spawner boundary; all other spawners keep their usual behavior.
bool CeFactoryMayStart(const CSpawner* source);
void CeFactoryProduced(const CSpawner* source, CThing* child);
bool CeFactorySingleBatch(const CSpawner* source);
