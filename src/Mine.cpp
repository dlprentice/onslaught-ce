// Mine.cpp: reconstructed from Mine.obj (004ba060..004ba9ef); Stuart's release has no copy. A mine: it lies on the
// ground, goes off a short while after an enemy comes within reach, and explodes.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "Motioncontroller.h"
#include "InitThing.h"
#include "map.h"
#include "mapwho.h"
#include "eventmanager.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"
#include "ParticleManager.h"
#include "MissionScript/IScript.h"

// FUNCTION: 0x004ba060 init:ZERO_FVECTOR
// FUNCTION: 0x004ba080 init:ID_FMATRIX

//******************************************************************************************
// It lies flat on the ground under it, facing its yaw. The forward vector is the matrix times (0, 1, 0), as in
// CGillM::GetGroundRoll: retail builds that argument, where GetColumnY() builds none. The side copy is taken after the
// pitch is stored, where retail loads it (004ba34a).
void CMine::Init(CInitThing *init)	// FUNCTION: 0x004ba150
{
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY;

	FMatrix	ori;
	ori.MakeRotationYawF(init->mYaw);
	FVector	forward = ori * FVector(0.0f, 1.0f, 0.0f);
	FVector	normal = MAP.Normal(init->mPos);
	forward = forward ^ normal;
	FVector	up = forward ^ normal;
	up.Normalise();
	float	pitch = -up.Z;
	init->mPitch = pitch;
	FVector	side = forward;
	side.Normalise();
	init->mRoll = side.Z;

	SUPERTYPE::Init(init);

#line 31
	mMotionController = new(MEMTYPE_MOTIONCONTROLLER) CMCMine(this);
	mUnknown12C = CEulerAngles(0.f, 0.f, 0.f);
	mTriggerTime = 0;
	mTriggerEffect = NULL;
	if (MAP.GetWaterLevel() > mPos.Z)
		mOnLand = FALSE;
	else
		mOnLand = TRUE;
}

//******************************************************************************************
void CMine::Shutdown()	// FUNCTION: 0x004ba490
{
	if (mTriggerEffect)
	{
		mTriggerEffect->Stop(FALSE);
		delete mTriggerEffect;
	}
	CUnit::Shutdown();
}

//******************************************************************************************
// The admitted particle frame is bound by reference inside SetOrientation. Together with GetAllegiance(), this
// reproduces the complete retail section, including the EAX reload at 004ba7b1 (799-byte body, 800-byte section).
void CMine::Move()	// FUNCTION: 0x004ba4d0
{
	if (mTriggerTime > 0)
	{
		if (mTriggerTime < EVENT_MANAGER.GetTime())
			StartDieProcess();
	}
	else
	{
		for (CMapWhoEntry *e = MAP_WHO.GetFirstEntryWithinRadius(mPos, 1.0f); e; e = MAP_WHO.GetNextEntryWithinRadius())
		{
			CUnit	*unit = (CUnit *)e->GetThing();
			if ((unit) && (unit != this) && (unit->IsA(THING_TYPE_GROUND_UNIT)) && (IsTargetAlligence(unit->GetAllegiance())))
			{
				FVector	d = mPos - unit->GetPos();
				if (d.MagnitudeXY() < 1.5f)
				{
					mTriggerTime = EVENT_MANAGER.GetTime() + mUnitData->mDetonationDelay;
					if (mUnitData->mParticles[1])
					{
						FVector	pos;
						FMatrix	ori;
						GetEmitter(28, 1, pos, ori);
						if ((pos.X != 0) || (pos.Y != 0) || (pos.Z != 0))
						{
#line 88
							mTriggerEffect = new(MEMTYPE_PARTICLE) CParticleEffectLink;
							mTriggerEffect->mEffect = NULL;
							PARTICLE_MANAGER.CreateEffect(mUnitData->mParticles[1], mTriggerEffect, ZERO_FVECTOR, 0, 0);
							mTriggerEffect->SetPos(pos);
							mTriggerEffect->SetOrientation(ori);
						}
					}
					break;
				}
			}
		}
	}
	SUPERTYPE::Move();
}

//******************************************************************************************
// It explodes, and goes a moment later.
void CMine::Unknown117()	// FUNCTION: 0x004ba7f0
{
	ReleaseChildren();
	ClearSpawners();
	if (mMissionScript)
	{
		mMissionScript->Died();
		delete mMissionScript;
		mMissionScript = NULL;
	}

	if (mUnitData)
	{
		SINT				index = mUnitData->mExplosion;
		CExplosion			*explosion = UPhysicsManager::SpawnExplosion(index);
		CExplosionInitThing	init;
		init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mExplosion);
		init.mAllegiance = (EAllegiance)mAllegiance;
		init.mAttachedTo = this;
		if (IsOnGround())
			init.mColType = kCollideGround;
		else
			init.mColType = (IsInWater()) ? kCollideWater : kCollideNothing;
		if (explosion)
		{
			init.mPos = mPos;
			explosion->Init(&init);
		}
	}

	EVENT_MANAGER.AddEvent(2000, this, Unknown112() + EVENT_MANAGER.GetTime());
}

//******************************************************************************************
// An enemy unit that runs into it sets it off.
void CMine::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x004ba990
{
	if ((other_thing->IsA(THING_TYPE_UNIT)) && (IsTargetAlligence(((CUnit *)other_thing)->GetAllegiance())))
		StartDieProcess();
	else
		SUPERTYPE::Hit(other_thing, report);
}

//******************************************************************************************
BOOL CMine::StartDieProcess()	// FUNCTION: 0x004ba9d0
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;
	Unknown117();
	return TRUE;
}

//******************************************************************************************
// A mine never shows on the scanner. Returning FALSE compiles to a body others already have, so the linker folds it
// into that one (/OPT:ICF).

