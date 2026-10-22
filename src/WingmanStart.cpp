// WingmanStart.cpp: reconstructed from WingmanStart.obj (0050a2b0..0050a744); Stuart's release has no copy. Where a
// level's wingman starts: it sits on the ground and brings in Tara, Billy or Jason, as the player chose.

#include "common.h"
#include "WingmanStart.h"
#include "map.h"
#include "game.h"
#include "WorldPhysicsManager.h"
#include "WorldMeshList.h"

// FUNCTION: 0x0050a2b0 init:ZERO_FVECTOR
// FUNCTION: 0x0050a2d0 init:ID_FMATRIX

// Reconstructed inline interface; the original source name is unknown. The same by-name factory operation
// also occurs in SpawnerThng.cpp. This call nesting reproduces the complete retail Init section: Tara's
// GetUnitIndex remains a call, Billy/Jason call First and Next, and all three GetUnitData walks expand.
static inline CThing *SpawnWingmanByName(char *name)
{
	return UPhysicsManager::SpawnThing(UPhysicsManager::GetUnitIndex(name));
}

//******************************************************************************************
void CWingmanStart::Init(CInitThing *init)	// FUNCTION: 0x0050a3a0
{
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	mFlags &= ~TF_IN_MAP_WHO;
	if (MAP.Collide(init->mPos) < init->mPos.Z)
		init->mPos.Z = MAP.Collide(init->mPos);
	SUPERTYPE::Init(init);

	mInit.mPos = init->mPos;
	mInit.mYaw = init->mYaw;
	mInit.mPitch = init->mPitch;
	mInit.mRoll = init->mRoll;
	mInit.mAllegiance = init->mAllegiance;

	switch (GAME.GetFrontEndSettings()->mWingman)
	{
	case kTaraWingman:
		mWingman = SpawnWingmanByName("Tara Fighter");
		mInit.mStats = UPhysicsManager::GetUnitData("Tara Fighter");
		CWorldMeshList::Add("Tara Fighter");
		break;
	case kBillyWingman:
		mWingman = SpawnWingmanByName("Billy Fighter");
		mInit.mStats = UPhysicsManager::GetUnitData("Billy Fighter");
		CWorldMeshList::Add("Billy Fighter");
		break;
	default:
		mWingman = SpawnWingmanByName("Jason Fighter");
		mInit.mStats = UPhysicsManager::GetUnitData("Jason Fighter");
		CWorldMeshList::Add("Jason Fighter");
		break;
	}

	if (mWingman.ToRead())
		mWingman->Init(&mInit);
}
