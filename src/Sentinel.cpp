// Sentinel.cpp: reconstructed from Sentinel.obj (004de960..004df0af); Stuart's release has no copy. A sentinel: a
// fixed emplacement that wakes from its "inactive" animation when activated, and fires its flamethrower when no
// child sits on its mount.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "component.h"
#include "Motioncontroller.h"
#include "InitThing.h"
#include "UnitAI.h"
#include "Guide.h"
#include "rtmesh.h"
#include "mesh.h"
#include "map.h"
#include "world.h"
#include "weapon.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"

#include <string.h>

// FUNCTION: 0x004de960 init:ZERO_FVECTOR
// FUNCTION: 0x004de980 init:ID_FMATRIX

//******************************************************************************************
void CSentinel::Init(CInitThing *init)	// FUNCTION: 0x004dea50
{
	mUnknown24C = 0;
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY;
	((CUnitInitThing *)init)->mStats->mUnknown1A0 = 1;

	SUPERTYPE::Init(init);

	if (mActive)
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("inactive"), TRUE, TRUE);

#line 32
	mMotionController = new(MEMTYPE_MOTIONCONTROLLER) CMCSentinel(this);
	mGuide = new(MEMTYPE_GUIDE) CTerrainGuide(this);
	mAI = new(MEMTYPE_AI) CSentinelAI(this, init);
	mUnknown12C = CEulerAngles(0, 0, 0);
	mUnknownF4 = 0;
	WORLD.AddToOccupancy(this);
}

// CSentinelAI's implicit destructor, emitted here with its vtable.
// FUNCTION: 0x004dec00 ??_GCSentinelAI@@UAEPAXI@Z
// FUNCTION: 0x004dec20 ??1CSentinelAI@@UAE@XZ

//******************************************************************************************
// The same code as CCannon's; the linker keeps that copy (0041b450).
void CSentinel::Shutdown()
{
	WORLD.RemoveFromOccupancy(this);
	CUnit::Shutdown();
}

//******************************************************************************************
// The flamethrower fires by itself whenever it is ready and nothing is mounted where it fires from.
void CSentinel::Move()	// FUNCTION: 0x004decc0
{
	SUPERTYPE::Move();

	ListIterator<CWeapon>	weapons(&mWeapons);
	for (CWeapon *weapon = weapons.First(); weapon; weapon = weapons.Next())
		if ((!stricmp("Sentinel Flamethrower", weapon->GetName())) && (weapon->ReadyToFire()) && (IsMountFree(weapon)))
			weapon->Fire();
}

//******************************************************************************************
void CSentinel::Activate()	// FUNCTION: 0x004ded30
{
	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("activate"), TRUE, FALSE);
}

//******************************************************************************************
// Once the "activate" animation has played it goes back to "inactive" and is active.
BOOL CSentinel::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x004ded60
{
	EAnimMode	am = GetRenderAnimation();
	if ((am != AM_INVALID) && (am == mRenderThing->GetRTMesh()->GetAnimModeByName("activate")))
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("inactive"), TRUE, TRUE);
		CUnit::Activate();
	}
	return FALSE;
}

//******************************************************************************************
void CSentinel::Unknown113(FVector *out)	// FUNCTION: 0x004dedc0
{
	CMeshPart	*part = (CMeshPart *)mUnknown220;
	FMatrix		ori;
	CMeshPart::EvaluatePose(mRenderThing, this, part, out, &ori, FALSE, TRUE);
}

//******************************************************************************************
BOOL CSentinel::IsMountFree(CWeapon *weapon)	// FUNCTION: 0x004dee00
{
	SINT	mount = -1;
	switch (weapon->mUnknownAC)
	{
	case 2:	mount = 9;	break;
	case 3:	mount = 10;	break;
	case 4:	mount = 11;	break;
	case 5:	mount = 12;	break;
	case 6:	mount = 13;	break;
	case 7:	mount = 14;	break;
	case 8:	mount = 15;	break;
	case 9:	mount = 16;	break;
	}

	ListIterator<CActiveReader<CUnit> >	children(&mChildren);
	for (CActiveReader<CUnit> *c = children.First(); c; c = children.Next())
		if ((c->ToRead()) && (((CComponent *)c->ToRead())->mNumber == mount))
			return FALSE;
	return TRUE;
}

//******************************************************************************************
void CSentinel::Unknown95()	// FUNCTION: 0x004deec0
{
	if (!mUnitData)
		return;

	for (SINT i = 0; i < 5; i++)
	{
		CExplosionInitThing	init;
		if (mRenderThing)
			init.mPos = mRenderThing->GetRandomSurfacePoint(0);
		else
			init.mPos = mPos;
		init.mAllegiance = (EAllegiance)mAllegiance;
		if (MAP.GetWaterLevel() + 2.0f > init.mPos.Z)
		{
			FVector	d = init.mPos - mPos;
			if (d * d < GetRadius() * GetRadius())
			{
				SINT		index = mUnitData->mSmallExplosion;
				CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
				if (explosion)
				{
					init.mAttachedTo = this;
					init.mUseAttachedRadius = FALSE;
					init.mAllowVolumetric = FALSE;
					init.mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
					init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mSmallExplosion);
					init.mColType = kCollideWater;
					if (!(MAP.GetWaterLevel() < init.mPos.Z))
						init.mColType = kCollideNothing;
					explosion->Init(&init);
				}
			}
		}
	}
}

//******************************************************************************************
// The sentinel's answers that no code above defined. Each compiles to a body another function already has, so the
// linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address each
// annotation names (the class header notes the slots' survivors too).
