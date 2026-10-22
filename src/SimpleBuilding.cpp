// SimpleBuilding.cpp: reconstructed from SimpleBuilding.obj (004df950..004dfd40); Stuart's release has no copy. A
// simple building stands until it dies; then it sinks under gravity, bursting into small explosions, and when it
// reaches the ground it explodes and goes.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "RenderThing.h"
#include "map.h"
#include "world.h"
#include "staticshadows.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"

// FUNCTION: 0x004df950 init:ZERO_FVECTOR
// FUNCTION: 0x004df970 init:ID_FMATRIX

//******************************************************************************************
void CSimpleBuilding::Init(CInitThing *init)	// FUNCTION: 0x004dfa40
{
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY | THING_TYPE_UNKNOWN_08000000;
	init->mInitCST.mUseFixedMeshTransforms = TRUE;
	mStaticShadow = NULL;
	SUPERTYPE::Init(init);
	StickToGround();
	mAI = NULL;
	WORLD.AddToOccupancy(this);
}

//******************************************************************************************
// Only a dying building moves: it sinks, and a small explosion bursts somewhere on it each frame. Near miss: retail
// copies the water level through the FPU (004dfb74) and keeps the unit data in other registers.
void CSimpleBuilding::Move()	// FUNCTION: 0x004dfaa0
{
	if (!(mFlags & TF_DYING))
		return;

	mVelocity.Z += Gravity();
	if (MAP.Collide(mPos) + 0.5f < mPos.Z)
		DeclareOnGround();

	CExplosionInitThing	init;
	init.mPos = mRenderThing->GetRandomSurfacePoint(0);
	float	ground = MAP.Collide(init.mPos);
	float	water = MAP.GetWaterLevel();
	SINT	index = mUnitData->mSmallExplosion;
	CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
	if (explosion)
	{
		init.mAllegiance = (EAllegiance)mAllegiance;
		init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mSmallExplosion);
		if (init.mPos.Z > water)
			init.mColType = kCollideWater;
		else
		{
			init.mColType = kCollideGround;
			if (!(init.mPos.Z > ground))
				init.mColType = kCollideNothing;
		}
		explosion->Init(&init);
	}
	CUnit::Move();
}

//******************************************************************************************
// On the ground a dying building explodes and goes; any building stops there.
void CSimpleBuilding::DeclareOnGround()	// FUNCTION: 0x004dfc60
{
	SUPERTYPE::DeclareOnGround();
	if (mFlags & TF_DYING)
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
	SetVelocity(FVector(0, 0, 0));
}

//******************************************************************************************
// Near miss: retail works on the flags' low byte, sign-extended (004dfcb5).
// IsDying's if/else inlines to retail's byte test (not, movsx, and 4, shr 2).
BOOL CSimpleBuilding::ClipToGround()	// FUNCTION: 0x004dfcb0
{
	if (IsDying())
		return FALSE;
	return TRUE;
}

//******************************************************************************************
float CSimpleBuilding::Gravity()	// FUNCTION: 0x004dfcc0
{
	if (mFlags & TF_DYING)
		return 0.002f;
	return 0;
}

//******************************************************************************************
BOOL CSimpleBuilding::StartDieProcess()	// FUNCTION: 0x004dfce0
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;
	STATICSHADOWS.UpdateVisibility(this, TRUE);
	return TRUE;
}

//******************************************************************************************
// Never below the water.
void CSimpleBuilding::StickToGround()	// FUNCTION: 0x004dfd10
{
	CActor::StickToGround();
	if (MAP.GetWaterLevel() < mPos.Z)
	{
		mPos.Z = MAP.GetWaterLevel();
		mOldPos.Z = MAP.GetWaterLevel();
	}
}

//******************************************************************************************
// The simple building's answers (its building behaviour is CBuilding's, Building.cpp) that no code above defined. Each
// compiles to a body another function already has, so the linker folds it into that one (/OPT:ICF): the retail table's
// slot points at the survivor, whose address each annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x00417680
CStaticShadow *CSimpleBuilding::GetStaticShadow()
{
	return mStaticShadow;
}

// FUNCTION: 0x00417690
void CSimpleBuilding::SetStaticShadow(CStaticShadow *shadow)
{
	mStaticShadow = shadow;
}

// FUNCTION: 0x00417660
void CSimpleBuilding::SetThingType(ULONG t)
{
	SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_40000000 | THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_BUILDING | THING_TYPE_STATIONARY);
}

// FUNCTION: 0x00417870
void CSimpleBuilding::Shutdown()
{
	WORLD.RemoveFromOccupancy(this);
	STATICSHADOWS.UpdateVisibility(this, TRUE);
	SUPERTYPE::Shutdown();
}

// The second pair of maths constants, after the code (see mathsconst2.h).
#include "mathsconst2.h"	// FUNCTION: 0x004dfd50 init:ZERO_FVECTOR2
						// FUNCTION: 0x004dfd70 init:ID_FMATRIX2

// The object's fifth .CRT$XCU initializer is a lone `ret` (004dfd40, coded before the second pair): a static whose
// class has an empty constructor. Open question: which static; nothing in the matched code reads one.
static FVector	sUnknown4dfd40;
// FUNCTION: 0x004dfd40 initorder:5
