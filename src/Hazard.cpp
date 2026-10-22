// Hazard.cpp: reconstructed from Hazard.obj (0047e440..0047e776); Stuart's release has no copy. A hazard (fire, a
// poisonous pool) damages the units that touch it; it blocks the occupancy grids and plays its effect and noise.

#include "common.h"
#include "hazard.h"
#include "InitThing.h"
#include "WorldPhysicsManager.h"
#include "world.h"
#include "SoundManager.h"
#include "BattleEngine.h"
#include "ThingType.h"

// FUNCTION: 0x0047e440 init:ZERO_FVECTOR
// FUNCTION: 0x0047e460 init:ID_FMATRIX

//******************************************************************************************
void CHazard::Init(CInitThing *init)	// FUNCTION: 0x0047e530
{
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_AMMUNITION | THING_TYPE_STATIONARY;
	init->mInitCST.mCollisionResponse = ECR_PASSIVE;
	init->mInitCST.mMinCollision = ECL_OUTER_SPHERE;
	init->mInitCST.mMaxCollision = ECL_OUTER_SPHERE;
	mData = ((CHazardInitThing *)init)->mData;
	SUPERTYPE::Init(init);

	if (mData->mParticle)
	{
		PARTICLE_MANAGER.CreateEffect(mData->mParticle, &mEffect, ZERO_FVECTOR, 0, 0);
		mEffect.SetPos(mPos);
		mEffect.SetOrientation(mOrientation);
	}
	WORLD.AddToOccupancy((CUnit *)this);
	if (mData->mSound)
		SOUND.PlayEffect(mData->mSound, this, 0.7f, ST_FOLLOWANDDIE, true, 0, 0, -1.0f, true, 1.0f, ST_GAME_SOUND, FALSE);
}

//******************************************************************************************
void CHazard::Shutdown()	// FUNCTION: 0x0047e6e0
{
	SOUND.KillSamplesForThing(this);
	mEffect.Kill();
	WORLD.RemoveFromOccupancy((CUnit *)this);
	SUPERTYPE::Shutdown();
}

//******************************************************************************************
void CHazard::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x0047e710
{
	SUPERTYPE::Hit(other_thing, report);
	if (other_thing->IsA(THING_TYPE_UNIT))
	{
		SINT	part = -1;
		if ((report->mUnknown00) && (report->mNumContacts > 0))
			part = report->mContactParts[0];
		other_thing->Damage(mData->mDamage, this, TRUE, part);
		if (other_thing->IsA(THING_TYPE_BATTLE_ENGINE))
			((CBattleEngine *)other_thing)->HostileEnvironment();
	}
}

//******************************************************************************************
float CHazard::GetRadius()	// FUNCTION: 0x0047e770
{
	return mData->mRadius;
}
