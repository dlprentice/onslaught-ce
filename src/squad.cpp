// squad.cpp: the base CSquad cohort, reconstructed from the pristine PC build.

#include "common.h"
#include "squad.h"
#include "unit.h"
#include "world.h"
#include "eventmanager.h"
#include "MissionScript/vm.h"
#include <d3d9.h>
#include "renderinfo.h"
#include "map.h"

// FUNCTION: 0x004e5cb0 init:ZERO_FVECTOR
// FUNCTION: 0x004e5cd0 init:ID_FMATRIX

CSquad::CSquad()	// FUNCTION: 0x004e5da0
{
	mUnknown80 = FALSE;
	mUnknown84 = FALSE;
}

// FUNCTION: 0x004e5dd0 ?_GetClassName@CSquad@@UAEPADXZ
// FUNCTION: 0x004e5e00 ?SetAIState@CSquad@@UAEXW4EAIState@@@Z
// FUNCTION: 0x004e5e50 ??_GCSquad@@UAEPAXI@Z

// STAND-IN (helper; review F 2026-10-15): Init spawns through this file-local helper where IncreaseThingCounter writes the
// call out; written out in Init too, 004e5e70 and the object's GenericSPtrSet::First/Next copies are lost.
static inline CThing *SpawnThingByName(char *name)
{
	return UPhysicsManager::SpawnThing(UPhysicsManager::GetUnitIndex(name));
}

void CSquad::Init(CInitThing *init)	// FUNCTION: 0x004e5e70
{
	mUnknown88 = 0;
	mUnknown9C = ((CSquadInitThing *)init)->mMode;
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	mFlags &= ~TF_IN_MAP_WHO;
	mUnknown7C = init->mAllegiance;
	mUnknownA0 = UPhysicsManager::GetUnitData(((CSquadInitThing *)init)->mUnitName);
	mPos = init->mPos;
	CUnitInitThing unitInit;
	unitInit.mPos = init->mPos;
	unitInit.mOrientationType = init->mOrientationType;
	unitInit.mYaw = init->mYaw;
	unitInit.mPitch = init->mPitch;
	unitInit.mOrientation = init->mOrientation;
	unitInit.mRoll = init->mRoll;
	unitInit.mMeshNo = init->mMeshNo;
	unitInit.mAllegiance = init->mAllegiance;
	unitInit.mTarget = init->mTarget;
	for (SINT n = 0; n < 256; n++)
	{
		unitInit.mScript[n] = init->mScript[n];
		unitInit.mName[n] = init->mName[n];
		unitInit.mSpawnScript[n] = init->mSpawnScript[n];
	}
	unitInit.mActive = init->mActive;
	unitInit.mAttachScriptsToUnits = init->mAttachScriptsToUnits;
	if (!Unknown83() && !mUnknown84 && !init->mAttachScriptsToUnits)
	{
		unitInit.mScript[0] = 0;
		unitInit.mName[0] = 0;
	}
	else
	{
		init->mScript[0] = 0;
		init->mName[0] = 0;
	}
	SINT amount = ((CSquadInitThing *)init)->mAmount;
	for (SINT i = 0; i < amount; i++)
	{
		CUnit *thing = (CUnit *)SpawnThingByName(((CSquadInitThing *)init)->mUnitName);
		if (thing)
		{
			CUnitInitThing memberInit;
			memberInit.Copy(&unitInit);
			memberInit.mStats = mUnknownA0;
			thing->Init(&memberInit);
			FVector pos = Unknown67(thing, i, ((CSquadInitThing *)init)->mAmount);
			pos.Z = thing->GetPos().Z;
			thing->Teleport(pos);
		}
	}
	SUPERTYPE::Init(init);
	mUnknown8C = GetPos();
	WORLD.GetSquadNB().Append(this);
}

void CSquad::IncreaseThingCounter(CInitThing *init)	// FUNCTION: 0x004e6200
{
	CUnitInitThing unitInit;
	unitInit.mPos = init->mPos;
	unitInit.mOrientationType = init->mOrientationType;
	unitInit.mYaw = init->mYaw;
	unitInit.mPitch = init->mPitch;
	unitInit.mOrientation = init->mOrientation;
	unitInit.mRoll = init->mRoll;
	unitInit.mMeshNo = init->mMeshNo;
	unitInit.mAllegiance = init->mAllegiance;
	unitInit.mTarget = init->mTarget;
	for (SINT n = 0; n < 256; n++)
	{
		unitInit.mScript[n] = init->mScript[n];
		unitInit.mName[n] = init->mName[n];
		unitInit.mSpawnScript[n] = init->mSpawnScript[n];
	}
	unitInit.mActive = init->mActive;
	unitInit.mAttachScriptsToUnits = init->mAttachScriptsToUnits;
	if (!Unknown83() && !mUnknown84 && !init->mAttachScriptsToUnits)
	{
		unitInit.mScript[0] = 0;
		unitInit.mName[0] = 0;
	}
	else
	{
		init->mScript[0] = 0;
		init->mName[0] = 0;
	}
	SINT amount = ((CSquadInitThing *)init)->mAmount;
	for (SINT i = 0; i < amount; i++)
	{
		CUnit *thing = (CUnit *)UPhysicsManager::SpawnThing(UPhysicsManager::GetUnitIndex(((CSquadInitThing *)init)->mUnitName));
		if (thing)
		{
			CUnitInitThing memberInit;
			memberInit.Copy(&unitInit);
			memberInit.mStats = UPhysicsManager::GetUnitData(((CSquadInitThing *)init)->mUnitName);
			thing->IncreaseThingCounter(&memberInit);
			delete thing;
		}
	}
}

void CSquad::Shutdown()	// FUNCTION: 0x004e65b0
{
	WORLD.mSet30.Remove(this);
	WORLD.mSet20.Remove(this);
	WORLD.GetSquadNB().Remove(this);
	SUPERTYPE::Shutdown();
}

void CSquad::HandleEvent(CEvent *event)	// FUNCTION: 0x004e65e0
{
	switch (event->GetEventNum())
	{
		case 4002: Unknown66((CScheduledEvent *)event); break;
		default: SUPERTYPE::HandleEvent(event); break;
	}
}

BOOL CSquad::Unknown4e6610()	// FUNCTION: 0x004e6610
{
	if (mUnknown88 > EVENT_MANAGER.GetTime() && !mUnknown7C)
		return TRUE;
	return FALSE;
}

void CSquad::Unknown4e6640(CUnit *unit, CThing *other)	// FUNCTION: 0x004e6640
{
	if (mMissionScript && other->IsA(THING_TYPE_COMPLEX_THING))
		mMissionScript->Hit((CComplexThing *)other);
}

void CSquad::Unknown4e6660(CUnit *attacker)	// FUNCTION: 0x004e6660
{
	mUnknown88 = EVENT_MANAGER.GetTime() + 5.0f;
}

BOOL CSquad::Unknown4e6680(EAllegiance allegiance)	// FUNCTION: 0x004e6680
{
	switch (allegiance)
	{
		case kForsetiAllegiance:
			if (mUnknown7C == kMuspellAllegiance || mUnknown7C == kIndependentAllegiance)
				return TRUE;
			break;
		case kMuspellAllegiance:
			if (mUnknown7C == kForsetiAllegiance || mUnknown7C == kIndependentAllegiance)
				return TRUE;
			break;
		case kIndependentAllegiance:
			if (mUnknown7C == kMuspellAllegiance || mUnknown7C == kForsetiAllegiance)
				return TRUE;
			break;
	}
	return FALSE;
}

void CSquad::DrawDebugStuff(float &y_offset)	// FUNCTION: 0x004e66d0
{
	SUPERTYPE::DrawDebugStuff(y_offset);
}

void CSquad::DrawDebugStuff3d()	// FUNCTION: 0x004e66e0
{
	RENDERINFO.SetWorld(FVector(0, 0, 0), ID_FMATRIX);
	float radius = GetRadius();
	FVector pos = mPos;
	float height = MAP.Collide(pos);
}

// Nothing; the linker folds it (00452b60).
void CSquad::Unknown66(CScheduledEvent *event)
{
}

inline FVector CSquad::Unknown71()	// FUNCTION: 0x0043e9f0
{
	return mPos;
}
