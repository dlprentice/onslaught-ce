// Submarine.cpp: reconstructed from Submarine.obj (004eeb90..004ef24f); Stuart's release has no copy. A submarine:
// it starts 25 below the sea, dives and surfaces through its dive states (the mission script's Dive and Surface
// natives, 005361f0 and 00536210), and trails bubbles while it changes depth.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#include "unit.h"
#include "map.h"
#include "RenderThing.h"
#include "ParticleManager.h"
#include "WorldPhysicsManager.h"
#include "InitThing.h"
#include "UnitAI.h"
#include "Guide.h"
#include "activereader.h"
#include "DebugLog.h"
// FUNCTION: 0x004eeb90 init:ZERO_FVECTOR
// FUNCTION: 0x004eebb0 init:ID_FMATRIX

//******************************************************************************************
void CSubmarine::Init(CInitThing *init)	// FUNCTION: 0x004eec80
{
	mDiveState = 0;

	init->mPos.Z = MAP.GetWaterLevel() + 25.0f;
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_BUILDING | THING_TYPE_TREE;

	float	turn = ((CUnitInitThing *)init)->mStats->mTurnRate;
	mUnknown12C = CEulerAngles(turn, turn / 3.0f, 0);

	SUPERTYPE::Init(init);

#line 29
	mAI = new(MEMTYPE_AI) CSubmarineAI(this, init);
	mGuide = new(MEMTYPE_GUIDE) CSubmarineGuide(this);
}

// The AI's implicit destructors, emitted here with its vtable.
// FUNCTION: 0x004eedc0 ??_GCSubmarineAI@@UAEPAXI@Z
// FUNCTION: 0x004eede0 ??1CSubmarineAI@@UAE@XZ

//******************************************************************************************
void CSubmarine::Move()	// FUNCTION: 0x004eee80
{
	if (Unknown76())
	{
		mVelocity += mUnknown14C * 0.4f;
		mVelocity *= Unknown72();
	}

	// no faster than its top speed across the water
	FVector	v = mVelocity;
	float	speed = v.MagnitudeXY();
	float	max = Unknown111() / 20.0f;
	if (speed > max)
	{
		v *= max / speed;
		mVelocity.X = v.X;
		mVelocity.Y = v.Y;
	}

	SUPERTYPE::Move();

	// sinking
	if ((mFlags & TF_DYING) && (!mUnitData->mUnknown11C))
	{
		mUnknown120.mPitch = PI / 4;
		mVelocity.Z = 0.03f;
		Unknown96();
		if (mPos.Z > 50.0f)
			AddShutdownEvent();
	}

	if ((mDiveState == 1) || (mDiveState == 3))
		SpawnBubbles();
}

//******************************************************************************************
void CSubmarine::Dive()	// FUNCTION: 0x004ef000
{
	if ((mDiveState == 2) || (mDiveState == 3))
	{
		mDiveState = 1;

		ListIterator<CActiveReader<CUnit> >	children(&mChildren);
		for (CActiveReader<CUnit> *child = children.First(); child; child = children.Next())
			if (child->ToRead())
				child->ToRead()->Deactivate();
	}
}

//******************************************************************************************
void CSubmarine::Surface()	// FUNCTION: 0x004ef050
{
	if ((mDiveState == 0) || (mDiveState == 1))
		mDiveState = 3;
}

//******************************************************************************************
BOOL CSubmarine::OnScanner()	// FUNCTION: 0x004ef070
{
	return mDiveState != 0;
}

//******************************************************************************************
void CSubmarine::Stop()	// FUNCTION: 0x004ef080
{
	mUnknown14C = FVector(0, 0, 0);
	mUnknown120 = mCurrentOrientation;
	if (mGuide)
		mGuide->Stop();
}

//******************************************************************************************
void CSubmarine::Unknown4ef0f0()	// FUNCTION: 0x004ef0f0
{
	mDiveState = 2;
}

//******************************************************************************************
void CSubmarine::Unknown96()	// FUNCTION: 0x004ef100
{
	for (SINT i = 0; i < 3; i++)
		SUPERTYPE::Unknown96();
}

//******************************************************************************************
// Bubbles at a random point of its surface that is just under the sea.
void CSubmarine::SpawnBubbles()	// FUNCTION: 0x004ef120
{
	if (!mUnitData)
		return;

	CExplosionData	*e = UPhysicsManager::GetExplosionData(mUnitData->mSmallExplosion);
	if ((e) && (e->mParticleB))
	{
		BOOL	done = FALSE;
		for (SINT i = 0; (!done) && (i < 100); i++)
		{
			FVector	p;
			if (mRenderThing)
				p = mRenderThing->GetRandomSurfacePoint(0);
			else
				p = mPos;
			if ((MAP.GetWaterLevel() < p.Z) && (MAP.GetWaterLevel() + 1.0f > p.Z))
			{
				PARTICLE_MANAGER.CreateEffect(e->mParticleB, NULL, p, 0, 0);
				done = TRUE;
			}
		}
	}
}

//******************************************************************************************
// The submarine's answers that no code above defined. Each compiles to a body another function already has, so the
// linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address each
// annotation names (the class header notes the slots' survivors too).
