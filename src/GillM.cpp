// GillM.cpp: reconstructed from GillM.obj (004798d0..0047a1af); Stuart's release has no copy. The Gill-M: a sea
// mech that rises out of the ground where it is placed, swings its arms at what is near, and walks back to where it
// came up.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "component.h"
#include "InitThing.h"
#include "game.h"
#include "BattleEngine.h"
#include "MCBattleEngine.h"
#include "GillMAI.h"
#include "Guide.h"
#include "rtmesh.h"
#include "renderthing.h"
#include "map.h"
#include "game.h"
#include "eventmanager.h"
#include "WorldPhysicsManager.h"

#include <string.h>
#include <math.h>

// FUNCTION: 0x004798d0 init:ZERO_FVECTOR
// FUNCTION: 0x004798f0 init:ID_FMATRIX

//******************************************************************************************
// It starts thirty above the ground under it and comes down into place.
void CGillM::Init(CInitThing *init)	// FUNCTION: 0x004799c0
{
	mNextSwingTime = 0;
	mUnknown270 = EVENT_MANAGER.GetTime();
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_TREE | THING_TYPE_UNKNOWN_08000000;
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;

	SUPERTYPE::Init(init);

	float	h = MAP.Collide(mPos) + 30.0f;
	mRising = 1;
	mOldPos.Z = mPos.Z = h;
	mStart = mPos;
}

//******************************************************************************************
void CGillM::InitLegMotion(CInitThing *init)	// FUNCTION: 0x00479a50
{
	if (mRenderThing->GetRTMesh()->GetAnimModeByName("LegMotion") != AM_INVALID)
	{
#line 45
		CMCGillM	*mc = new(MEMTYPE_MOTIONCONTROLLER) CMCGillM(this);
		mMotionController = mc;
		CUnitData	*data = ((CUnitInitThing *)init)->mStats;
		mc->SetMechParameters(data->mBodyMotion, data->mLegSpeed, data->mLegPlacementArea, 3.4f, 0.99f, data->mStandingLegPlacementArea, data->mMaxLegsLifted);
	}
	else
		mMotionController = NULL;
}

// CMCGillM's scalar deleting destructor, emitted here with its vtable.
// FUNCTION: 0x00479b40 ??_GCMCGillM@@UAEPAXI@Z

//******************************************************************************************
void CGillM::InitCockpit(CInitThing *init)	// FUNCTION: 0x00479b60
{
#line 56
	mAI = new(MEMTYPE_AI) CGillMAI(this, init);
}

// CGillMAI's implicit destructor, emitted here with its vtable.
// FUNCTION: 0x00479bf0 ??_GCGillMAI@@UAEPAXI@Z
// FUNCTION: 0x00479c10 ??1CGillMAI@@UAE@XZ

//******************************************************************************************
void CGillM::InitTargeting()	// FUNCTION: 0x00479cb0
{
#line 62
	mGuide = new(MEMTYPE_GUIDE) CTerrainGuide(this);
}

//******************************************************************************************
void CGillM::Move()	// FUNCTION: 0x00479d10
{
	if (mRising == 1)
	{
		mLastTimeOnGround = EVENT_MANAGER.GetTime();
		if (MAP.Collide(mPos) >= mPos.Z)
		{
			mRising = 0;
			mVelocity.Z = 0;
		}
		else
			mVelocity.Z = -0.08f;
		SUPERTYPE::Move();
		return;
	}

	if (mDeployState == 2)
	{
		mLastTimeOnGround = EVENT_MANAGER.GetTime();
		mVelocity.Z = 0.12f;
	}
	SUPERTYPE::Move();
}

//******************************************************************************************
BOOL CGillM::ClipToGround()	// FUNCTION: 0x00479d90
{
	if (mRising == 1)
		return FALSE;
	return mDeployState != 2;
}

//******************************************************************************************
// One arm, left or right at random, plays its hit animation.
void CGillM::Swing()	// FUNCTION: 0x00479db0
{
	if (mNextSwingTime < EVENT_MANAGER.GetTime())
	{
		BOOL	right = FALSE;
		if (float(GAME.Random() % 65536) / 65536.0f > 0.5f)
			right = TRUE;

		ListIterator<CActiveReader<CUnit> >	children(&mChildren);
		for (CActiveReader<CUnit> *c = children.First(); c; c = children.Next())
		{
			CUnit	*unit = c->ToRead();
			if (unit)
			{
				if (right)
				{
					if (!strcmp(unit->mUnitData->mName, "Gill-M Right Arm"))
						((CComponent *)unit)->PlayHit();
				}
				else if (!strcmp(unit->mUnitData->mName, "Gill-M Left Arm"))
					((CComponent *)unit)->PlayHit();
			}
		}

		mNextSwingTime = EVENT_MANAGER.GetTime() + 2.5f;
	}
}

//******************************************************************************************
float CGillM::Gravity()	// FUNCTION: 0x00479ef0
{
	if (mRising == 1)
		return 0.0f;
	return 0.01f;
}

//******************************************************************************************
float CGillM::Unknown72()	// FUNCTION: 0x00479f10
{
	if (mRising == 1)
		return 1.0f;
	return 0.25f;
}

//******************************************************************************************
// The slope of the ground along its heading, between points 1.2 radii ahead and one radius behind. CMap::Collide forwards (map.h),
// whose inline cost puts the back point's FVector constructor (0047a012) and Elevation's Magnitude (0047a063) out of
// line as retail has them; the difference is a named local, which keeps a frame slot of its own (retail's frame is 0x44).
float CGillM::GetGroundPitch()	// FUNCTION: 0x00479f30
{
	if (mRising == 1)
		return -PI / 6;
	if (mDeployState == 2)
		return PI / 8;
	if (mVelocity.MagnitudeSq() > 0)
	{
		FVector	front = mOrientation * FVector(0, GetRadius() * 1.2f, 0) + mPos;
		FVector	back = mOrientation * FVector(0, -GetRadius(), 0) + mPos;
		front.Z = MAP.Collide(front);
		back.Z = MAP.Collide(back);
		FVector	d = front - back;
		return d.Elevation() * 1.6f;
	}
	return 0.0f;
}

//******************************************************************************************
// How far the ground under it tips across its heading.
float CGillM::GetGroundRoll()	// FUNCTION: 0x0047a0b0
{
	FMatrix	ym;
	ym.MakeRotationYawF(mCurrentOrientation.mYaw);
	FVector	forward = ym * FVector(0.0f, 1.0f, 0.0f);
	FVector	normal = MAP.Normal(mPos);
	FVector	side = forward ^ normal;
	side.Normalise();
	return side.Z;
}

//******************************************************************************************
// It walks back to where it came up.
void CGillM::Unknown100()	// FUNCTION: 0x0047a160
{
	if ((mDeployState != 1) && (mDeployState != 2))
	{
		GoToPoint(mStart, FALSE);
		mDeployState = 1;
	}
}

//******************************************************************************************
// The Gill M's slot 116 does nothing; the empty body is folded into the shared one (/OPT:ICF).

// FUNCTION: 0x0040c640
void CGillM::Unknown116()
{
}
