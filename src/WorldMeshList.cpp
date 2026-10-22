// WorldMeshList.cpp: reconstructed from WorldMeshList.obj (0050d870..0050de8f); Stuart's release has no copy.
#include "common.h"
#include "WorldMeshList.h"
#include "WorldPhysicsManager.h"
#include "game.h"
#include "InitThing.h"
#include "thing.h"

// FUNCTION: 0x0050d870 init:ZERO_FVECTOR
// FUNCTION: 0x0050d890 init:ID_FMATRIX
// FUNCTION: 0x0050d960 init:mList
// FUNCTION: 0x0050d980 exit:mList
SPtrSet<CWorldMeshListItem>	CWorldMeshList::mList;

//******************************************************************************************
void CWorldMeshList::Clear()	// FUNCTION: 0x0050d9a0
{
	CWorldMeshListItem	*item;

	while (item = mList.First())
	{
		mList.Remove(item);
		delete [] item->mName;
		delete item;
	}
}

//******************************************************************************************
void CWorldMeshList::Add(char *name)	// FUNCTION: 0x0050d9e0
{
	if (GAME.IsRunningResources())
		return;

	CWorldMeshListItem	*item;
	for (item = mList.First(); item; item = mList.Next())
		if (strcmp(item->mName, name) == 0)
			return;

	if (!name)
		return;

	// only names the physics script defines
	CUnitData	*unit = UPhysicsManager::GetUnitData(name);
	if (!unit)
		return;
	mList.Add(new(MEMTYPE_WORLDMESHLIST) CWorldMeshListItem(name));

	// and everything its spawners make
	ListIterator<CSpawnerReference>	refs(&unit->mSpawners);
	for (CSpawnerReference *ref = refs.First(); ref; ref = refs.Next())
	{
		SINT			index = ref->mSpawnerIndex, i = 0;
		CSpawnerData	*spawner;
		for (spawner = UPhysicsManager::mSpawners->First(); spawner; spawner = UPhysicsManager::mSpawners->Next())
		{
			if (i == index)
				break;
			i++;
		}
		if (spawner)
			Add(spawner->mThingName);
	}
}

//******************************************************************************************
void CWorldMeshList::MarkUsed(char *name)	// FUNCTION: 0x0050dc20
{
	CWorldMeshListItem	*item;
	for (item = mList.First(); item; item = mList.Next())
		if (strcmp(item->mName, name) == 0)
		{
			item->mUsed = TRUE;
			return;
		}
}

//******************************************************************************************
// Creates and shuts down one of every listed thing the world did not use.
void CWorldMeshList::SpawnUnused()	// FUNCTION: 0x0050dcb0
{
	CWorldMeshListItem	*item;
	for (item = mList.First(); item; item = mList.Next())
	{
		if (!item->mUsed)
		{
			CThing	*thing = UPhysicsManager::SpawnThing(UPhysicsManager::GetUnitIndex(item->mName));
			if (thing)
			{
				CUnitInitThing	*init = (CUnitInitThing *)SpawnInitThing(OID_CUnit, FALSE);
				init->mStats = UPhysicsManager::GetUnitData(item->mName);
				init->mPos = FVector(256.0f, 256.0f, 0.0f);
				thing->Init(init);
				thing->Shutdown();
				SAFE_DELETE(init);
			}
		}
	}
}
