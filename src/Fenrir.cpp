// Fenrir.cpp: reconstructed from Fenrir.obj (0044dec0..0044e545); Stuart's release has no copy. The Fenrir, a flying
// fortress: it flies as fast as its surviving engines allow, fires its flamethrower when it can, and on dying waits,
// plays its "die" animation and bursts into small explosions before it goes.

#include "common.h"
#define FENRIR_CPP
#include "MissionScript/DataType.h"	// the full values; vm.h's stubs step aside
#include "unit.h"
#include "component.h"
#include "InitThing.h"
#include "rtmesh.h"
#include "mesh.h"
#include "map.h"
#include "weapon.h"
#include "eventmanager.h"
#include "SoundManager.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"

#include <string.h>

// FUNCTION: 0x0044dec0 init:ZERO_FVECTOR
// FUNCTION: 0x0044dee0 init:ID_FMATRIX

#define FENRIR_DIE_EVENT	7000

// scripteventnb.h brings vm.h's stubs, which DataType.h's full values replace here.
class CScriptEventNB
{
public:
	void	PostEvent(char *name);			// 00538b70
};
extern CScriptEventNB SCRIPT_EVENT_NB;		// 0089c590

//******************************************************************************************
void CFenrir::Init(CInitThing *init)	// FUNCTION: 0x0044dfb0
{
	mDieDelay = 2.0f;
	mDying = FALSE;
	SUPERTYPE::Init(init);
	mUnknown284 = mUnitData->mUnknownB4;
	mBurningSound = SOUND.GetEffectByName("Fenrir Inside Burning", 0);
	mEngineSound = SOUND.GetEffectByName("Fenrir Engines", 0);
}

//******************************************************************************************
void CFenrir::Move()	// FUNCTION: 0x0044e020
{
	if (mLife < -0.01f)
		mLife = -0.01f;

	ListIterator<CWeapon>	weapons(&mWeapons);
	for (CWeapon *weapon = weapons.First(); weapon; weapon = weapons.Next())
		if (!stricmp("Fenrir Flamethrower", weapon->GetName()))
		{
			if (weapon->ReadyToFire())
				weapon->Fire();
			break;
		}

	// its engines are the components on mounts 24..27
	SINT	engines = 0;
	for (CActiveReader<CUnit> *child = mChildren.First(); child; child = mChildren.Next())
	{
		CComponent	*c = (CComponent *)child->ToRead();
		if ((c) && (c->mNumber >= 24) && (c->mNumber <= 27))
			engines++;
	}
	mUnknown284 = (engines * 0.25f) * mUnitData->mUnknownB4;

	if (!engines)
	{
		SOUND.StopSample(mEngineSound->mSample, this);
		if (!SOUND.IsEffectPlaying(mBurningSound, this))
			SOUND.PlayEffect(mBurningSound, this, 0.7f, ST_FOLLOWANDDIE, true, 0, 0, -1.0f, true, 1.0f, ST_GAME_SOUND, FALSE);
	}
	else
	{
		SOUND.StopSample(mBurningSound->mSample, this);
		if (!SOUND.IsEffectPlaying(mEngineSound, this))
			SOUND.PlayEffect(mEngineSound, this, 0.7f, ST_FOLLOWANDDIE, true, 0, 0, -1.0f, true, 1.0f, ST_GAME_SOUND, FALSE);
	}
	SUPERTYPE::Move();
}

//******************************************************************************************
// It starts dying once: the event comes after its die delay.
BOOL CFenrir::StartDieProcess()	// FUNCTION: 0x0044e1c0
{
	if (mFlags & TF_DYING)
		return FALSE;
	mFlags |= TF_DYING;
	EVENT_MANAGER.AddEvent(mDieDelay, FENRIR_DIE_EVENT, this, 0, NULL, NULL);
	return TRUE;
}

//******************************************************************************************
void CFenrir::SetVar(CStringDataType *name, CDataType *data)	// FUNCTION: 0x0044e200
{
	if (name->Matches("fenrir_diedelay"))
		mDieDelay = data->GetFloat();
	else
		CComplexThing::SetVar(name, data);
}

//******************************************************************************************
void CFenrir::HandleEvent(CEvent *event)	// FUNCTION: 0x0044e240
{
	if (event->GetEventNum() != FENRIR_DIE_EVENT)
	{
		SUPERTYPE::HandleEvent(event);
		return;
	}
	mDying = TRUE;
	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("die"), TRUE, FALSE);
	for (SINT i = 0; i < 50; i++)
		SpawnSurfaceExplosion();
	SCRIPT_EVENT_NB.PostEvent("fenrir blowing up");
	mFlags &= ~TF_DYING;
	CAirUnit::StartDieProcess();
}

//******************************************************************************************
// Gone once its "die" animation has played.
BOOL CFenrir::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x0044e2c0
{
	if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("die"))
		AddShutdownEvent();
	return FALSE;
}

//******************************************************************************************
// The surface point through an if/else (the FVector stays in registers) and the explosion's index read once for the
// spawn decide the scheduling and registers (the same fixes as CUnit's slot 95, 004f9260).
void CFenrir::SpawnSurfaceExplosion()	// FUNCTION: 0x0044e300
{
	if (!mUnitData)
		return;

	CExplosionInitThing	init;
	float	frame = GetRenderFrame();
	SINT	anim = GetRealAnimIndex();
	CMeshPart	*part = mRenderThing->GetRTMesh()->mParts[0];
	float	fraction;
	SINT	f = (anim >= 0) ? part->ResolveFrameNumber(frame, anim, &fraction, GetMotionController()) : 0;
	if (mRenderThing)
		init.mPos = mRenderThing->GetRandomSurfacePoint(f);
	else
		init.mPos = mPos;
	init.mAllegiance = (EAllegiance)mAllegiance;
	if (init.mPos.Z < MAP.GetWaterLevel())
	{
		SINT	index = mUnitData->mSmallExplosion;
		CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
		if (explosion)
		{
			init.mAttachedTo = this;
			init.mUseAttachedRadius = FALSE;
			init.mAllowVolumetric = FALSE;
			if (mDying == TRUE)
				init.mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
			init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mSmallExplosion);
			init.mColType = kCollideWater;
			if (init.mPos.Z <= MAP.GetWaterLevel())
				init.mColType = kCollideNothing;
			explosion->Init(&init);
		}
	}
}

//******************************************************************************************
// Slot 95: the mesh's frame is worked out (and dropped), then two small explosions.
void CFenrir::Unknown95()	// FUNCTION: 0x0044e4e0
{
	float	frame = GetRenderFrame();
	SINT	anim = GetRealAnimIndex();
	CMeshPart	*part = mRenderThing->GetRTMesh()->mParts[0];
	float	fraction;
	if (anim >= 0)
		part->ResolveFrameNumber(frame, anim, &fraction, GetMotionController());
	for (SINT i = 0; i < 2; i++)
		SpawnSurfaceExplosion();
}
