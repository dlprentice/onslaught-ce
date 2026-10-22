// SpawnPoint.cpp: reconstructed from the retail respawn-point object (004e46c0..004e4a99).

#include "common.h"
#include "SpawnPoint.h"
#include "BattleEngine.h"
#include "game.h"
#include "world.h"
#include "map.h"
#include "mapwho.h"
#include "ParticleManager.h"

void CSpawnPoint::Init(CInitThing *init)	// FUNCTION: 0x004e46c0
{
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	mFlags &= ~TF_IN_MAP_WHO;
	if (MAP.Collide(init->mPos) < init->mPos.Z)
		init->mPos.Z = MAP.Collide(init->mPos);
	SUPERTYPE::Init(init);
	WORLD.GetSpawnPointNB().Add(this);
	mPlayerNumber = ((CStartInitThing *)init)->mPlayerNumber;
	if (GAME.GetFrontEndSettings()->mInvertSides)
	{
		if (mPlayerNumber == 1)
			mPlayerNumber = 2;
		else if (mPlayerNumber == 2)
			mPlayerNumber = 1;
	}
	mInit.mPos = init->mPos;
	mInit.mYaw = init->mYaw;
	mInit.mPitch = init->mPitch;
	mInit.mRoll = init->mRoll;
	mInit.mAllegiance = init->mAllegiance;
	if (mPlayerNumber == 1)
		mInit.mConfigurationId = GAME.GetFrontEndSettings()->mPlayer1Configuration;
	else
		mInit.mConfigurationId = GAME.GetFrontEndSettings()->mPlayer2Configuration;
	mInit.mPlaneMode = ((CStartInitThing *)init)->mPlaneMode;
	mActive = TRUE;
}

void CSpawnPoint::Shutdown()	// FUNCTION: 0x004e47c0
{
	WORLD.GetSpawnPointNB().Remove(this);
	SUPERTYPE::Shutdown();
}

CBattleEngine *CSpawnPoint::SpawnBattleEngine(BOOL effects)	// FUNCTION: 0x004e47e0
{
	CBattleEngine *engine = (CBattleEngine *)SpawnThing(OID_CBattleEngine);
	if (engine)
	{
		engine->Init(&mInit);
		mInit.mInitCST.Initialise();
	}
	if (effects)
	{
		CParticleEffectLink effect;
		CParticleDescriptor *descriptor;
		if (mInit.mPlaneMode)
			descriptor = PARTICLE_SET.GetPD("BE Respawn Air Effect");
		else
			descriptor = PARTICLE_SET.GetPD("BE Respawn Ground Effect");
		PARTICLE_MANAGER.AddParticle(descriptor, &effect);
		effect.SetPos(mPos);
	}
	return engine;
}

BOOL CSpawnPoint::Available()	// FUNCTION: 0x004e49f0
{
	if (!mActive)
		return FALSE;
	CMapWhoEntry *entry = MAP_WHO.GetFirstEntryWithinRadius(mPos, 1.0f);
	while (entry)
	{
		CThing *thing = entry->GetThing();
		if (thing && thing->IsA(THING_TYPE_UNIT) && !thing->IsA(THING_TYPE_INFANTRY | THING_TYPE_COMPONENT))
		{
			if ((thing->GetPos() - mPos).MagnitudeSq() < 16.0f)
				return FALSE;
		}
		entry = MAP_WHO.GetNextEntryWithinRadius();
	}
	return TRUE;
}

// SpawnBattleEngine reads ZERO_FVECTOR at 0083d0a0, which identifies this object's initializers.
// FUNCTION: 0x004e45d0 init:ZERO_FVECTOR
// FUNCTION: 0x004e45f0 init:ID_FMATRIX
