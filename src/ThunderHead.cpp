// ThunderHead.cpp: reconstructed from ThunderHead.obj (004f4600..004f4d0f); Stuart's release has no copy. The
// Thunderhead: a mech whose body pitches to face what it shoots, whose flamethrower fires by itself at ground units
// in reach, and whose feet stamp explosions.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "game.h"
#include "BattleEngine.h"
#include "MCBattleEngine.h"
#include "UnitAI.h"
#include "Guide.h"
#include "rtmesh.h"
#include "renderthing.h"
#include "mapwho.h"
#include "weapon.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"

#include <string.h>
#include <math.h>

// FUNCTION: 0x004f4600 init:ZERO_FVECTOR
// FUNCTION: 0x004f4620 init:ID_FMATRIX

//******************************************************************************************
void CThunderHead::Init(CInitThing *init)	// FUNCTION: 0x004f46f0
{
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_UNKNOWN_08000000;
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	SUPERTYPE::Init(init);
	mPitch = 0;
	mTargetPitch = 0;
}

//******************************************************************************************
void CThunderHead::InitLegMotion(CInitThing *init)	// FUNCTION: 0x004f4730
{
	if (mRenderThing->GetRTMesh()->GetAnimModeByName("LegMotion") != AM_INVALID)
	{
#line 32
		CMCThunderHead	*mc = new(MEMTYPE_MOTIONCONTROLLER) CMCThunderHead(this);
		mMotionController = mc;
		CUnitData	*data = ((CUnitInitThing *)init)->mStats;
		mc->SetMechParameters(data->mBodyMotion, data->mLegSpeed, data->mLegPlacementArea, 3.4f, 0.99f, data->mStandingLegPlacementArea, data->mMaxLegsLifted);
	}
	else
		mMotionController = NULL;
}

SINT CMCThunderHead::Unknown13()	// FUNCTION: 0x004f4820
{
	return 180;
}

//******************************************************************************************
void CThunderHead::InitCockpit(CInitThing *init)	// FUNCTION: 0x004f4830
{
#line 43
	mAI = new(MEMTYPE_AI) CUnitAI(this, init);
}

//******************************************************************************************
void CThunderHead::InitTargeting()	// FUNCTION: 0x004f48a0
{
#line 49
	mGuide = new(MEMTYPE_GUIDE) CThunderheadGuide(this, mPos);
}

//******************************************************************************************
// STAND-IN (budget accessors; review F 2026-10-15): the pitch accessors of Move's clamp, as file-statics: unit.h's declaration count is fixed by CGame::DrawDebugStuff
// (it sees the class), and these are size <= 40 inline sites like the members they stand for. By reference, like GetPos:
// a by-value float getter reloads a copy in the compares.
static inline float &TargetPitch(CThunderHead *t) { return t->mTargetPitch; }
static inline void SetTargetPitch(CThunderHead *t, float p) { t->mTargetPitch = p; }

//******************************************************************************************
// Matched in accessor style for the inline-budget rule (README; as CAirGuide::Unknown3). Retail calls FVector::Magnitude
// (inside Elevation) out of line (004f49ce). Here Elevation's body budget is (1456 - 53) / 24 = 58, under Magnitude's size
// 60, because the clamp goes through the pitch accessors above (six free inline sites after Elevation; written out they
// leave 76 and Magnitude expands). The rest is retail's structure: Elevation (the hand-written asin form compiles the
// same), the flamethrower test and the search of its weapon in one loop body (a null weapon leaves the loop without a
// second test), GetPos() and GetAllegiance() as Stuart's BattleEngine.cpp writes them in the same map-who loop, and `<`
// against range - 3 with MagnitudeXY.
void CThunderHead::Move()	// FUNCTION: 0x004f4920
{
	SUPERTYPE::Move();

	mTargetPitch = 0;
	if (!(mFlags & TF_DYING))
	{
		// it looks up or down at its target while it has a child to shoot with
		BOOL								armed = FALSE;
		ListIterator<CActiveReader<CUnit> >	children(&mChildren);
		for (CActiveReader<CUnit> *c = children.First(); c; c = children.Next())
			if (c->ToRead())
				armed = TRUE;

		CUnit	*target = GetCurrentTarget();
		if ((target) && (armed))
		{
			FVector	tpos, mpos;
			target->GetCentrePos(tpos);
			GetCentrePos(mpos);
			FVector	d = tpos - mpos;
			SetTargetPitch(this, d.Elevation());
			if (TargetPitch(this) > PI / 8)
				SetTargetPitch(this, PI / 8);
			else if (TargetPitch(this) < -PI / 8)
				SetTargetPitch(this, -PI / 8);
		}
		else
			SetTargetPitch(this, 0);

		if (mPitch - 0.01f > mTargetPitch)
			mPitch -= 0.01f;
		else if (mPitch + 0.01f < mTargetPitch)
			mPitch += 0.01f;
		else
			mPitch = mTargetPitch;

		// the flamethrower fires at ground units in its reach
		ListIterator<CWeapon>	weapons(&mWeapons);
		for (CWeapon *weapon = weapons.First(); weapon; weapon = weapons.Next())
		{
			if (!stricmp("Thunderhead Flamethrower", weapon->GetName()))
			{
				if (weapon->ReadyToFire())
				{
					float	range = weapon->GetMaxRange(FVector(0, 0, 0));
					for (CMapWhoEntry *e = MAP_WHO.GetFirstEntryWithinRadius(GetPos(), range); e; e = MAP_WHO.GetNextEntryWithinRadius())
					{
						CUnit	*unit = (CUnit *)e->GetThing();
						if ((unit) && (unit != this) && (unit->IsA(THING_TYPE_GROUND_UNIT)) && (IsTargetAlligence(unit->GetAllegiance())) && (unit->IsOnGround()))
						{
							FVector	diff = GetPos() - unit->GetPos();
							if (diff.MagnitudeXY() < range - 3.0f)
							{
								weapon->Fire();
								break;
							}
						}
					}
				}
				break;
			}
		}
	}

	// a foot came down
	CMCMech	*mc = (CMCMech *)mMotionController;
	if ((mc) && (mc->mUnknownD0))
	{
		SINT		index = GetUnitData()->mStompExplosion;
		CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
		if (explosion)
		{
			CExplosionInitThing	init;
			init.mPos = mc->mUnknownD4;
			init.mAllegiance = GetAllegiance();
			init.mBehaviour = UPhysicsManager::GetExplosionData(GetUnitData()->mStompExplosion);
			init.mColType = kCollideNothing;
			init.mAttachedTo = this;
			explosion->Init(&init);
			mc->mUnknownD0 = 0;
		}
	}
}
