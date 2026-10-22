// Start.cpp: reconstructed from the retail start-position object (004eabd0..004eb1cb); CSpawnPoint (SpawnPoint.cpp)
// is its twin.

#include "common.h"
class CStart;class CBattleEngineInitThing;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#include "Start.h"
#include "BattleEngine.h"
#include "game.h"
#include "world.h"
#include "map.h"
#include "mapwho.h"
#include "ParticleManager.h"
CStart::CStart()	// FUNCTION: 0x004eacc0
{
	mPlayerObject = NULL;
}

CStart::~CStart()	// FUNCTION: 0x004ead90
{
	WORLD.GetStartNB().Remove(this);
}

void CStart::Init(CInitThing *init)	// FUNCTION: 0x004eae10
{
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
	mFlags &= ~TF_IN_MAP_WHO;
	if (MAP.Collide(init->mPos) < init->mPos.Z)
		init->mPos.Z = MAP.Collide(init->mPos);
	SUPERTYPE::Init(init);
	WORLD.GetStartNB().Add(this);
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
	SpawnBattleEngine(FALSE);
}

CBattleEngine *CStart::SpawnBattleEngine(BOOL effects)	// FUNCTION: 0x004eaf20
{
	mPlayerObject = (CBattleEngine *)SpawnThing(OID_CBattleEngine);
	if (mPlayerObject.ToRead())
	{
		mPlayerObject->Init(&mInit);
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
	return mPlayerObject.ToRead();
}

BOOL CStart::Available()	// FUNCTION: 0x004eb130
{
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

// SpawnBattleEngine reads ZERO_FVECTOR at 0083d238, which identifies this object's initializers.
// FUNCTION: 0x004eabd0 init:ZERO_FVECTOR
// FUNCTION: 0x004eabf0 init:ID_FMATRIX
// FUNCTION: 0x004ead50 ?_GetClassName@CStart@@UAEPADXZ
// FUNCTION: 0x004ead60 ?_GetClassID@CStart@@UAEHXZ
// FUNCTION: 0x004ead70 ??_GCStart@@UAEPAXI@Z
