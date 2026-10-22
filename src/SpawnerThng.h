// SpawnerThng.h: CSpawnerThing (actor, 0x46c bytes) and CSpawner (monitor, 0x3f8 bytes).
// RTTI distinguishes actor vtable 005dd16c from monitor vtable 005dee94 and its CMonitor/IListener bases.
// Layouts are measured from Init 004e3010, constructor 004e37f0 and their users; unknowns retain offsets.

#ifndef SPAWNER_THNG_H
#define SPAWNER_THNG_H

#include "Monitor.h"
#include "activereader.h"
#include "InitThing.h"
#include "thing.h"

class CSquad;

// The actor class (0x46c bytes, vtable 005dd16c), as distinct from the CMonitor below.
// Init 004e3010 copies the named values from CSpawnerInitThing; its counters retain their offsets.
DECLARE_THING_CLASS(CSpawnerThing, CComplexThing)
public:
	virtual void Init(CInitThing *init);
	virtual void Shutdown();
	virtual void Activate();
	virtual void Deactivate();
	virtual void HandleEvent(CEvent *event);
	virtual void SetSpawnScript(char *script);

	void Unknown4e3370();				// 004e3370: reached by event 3000

	CActiveReader<CSquad> mSquad;			// +0x07c
	float mUnknown80;
	float mInitialDelay;					// +0x084
	float mDelay;
	float mSquadDelay;
	SINT mSquadSize;
	SINT mAmount;
	SINT mUnknown98;
	SINT mUnknown9C;
	BOOL mUnknownA0;
	BOOL mUnknownA4;
	CUnitInitThing mInit;					// +0x0a8
	char *mSpawnUnit;					// +0x468
};

class CSpawner : public CMonitor
{
public:
	CSpawner(CSpawnerData *data, SINT a);
	virtual ~CSpawner();
	virtual void Shutdown();
	virtual void HandleEvent(CEvent *event);

	char *GetDataName() { return mData->mName; }
	float GetDelay() { return mData->mPostSpawnDelay; }
	float GetUnknown34() { return mData->mPreSpawnDelay; }

	void UpdateSpawnCount();				// 004e3ac0
	BOOL DoSpawn();						// 004e3c60
	void Unknown4e3f90();				// 004e3f90: reached by event 3000
	BOOL Unknown4e43d0();
	BOOL Unknown4e4420();
	BOOL Unknown4e4430();
	BOOL Unknown4e4480(CThing *target);
	BOOL IsSpawnPositionClear(FVector &pos);	// 004e44d0: no other unit within 6 of it (a behaviour-3 unit spawns anyway)

	CActiveReader<CSquad> mSquad;			// +0x008: destructor clears the squad's +0x80 member
	SINT mUnknown0C;
	CUnitInitThing mInit;					// +0x010
	CSpawnerData *mData;					// +0x3d0
	CUnit *mOwner;						// +0x3d4: CUnit::Init stores its receiver
	SINT mUnknown3D8;
	SINT mUnitIndex;						// +0x3dc: GetUnitIndex(mData->mThingName)
	float mUnknown3E0;
	BOOL mUnknown3E4;
	SINT mMount;							// +0x3e8: CUnit::Init stores the spawner-reference mount
	SINT mUnknown3EC;
	SINT mUnknown3F0;
	BOOL mEnabled;						// +0x3f4: CUnit's enable/disable calls
};

#endif
