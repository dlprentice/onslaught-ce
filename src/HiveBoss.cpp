// HiveBoss.cpp: reconstructed from HiveBoss.obj (0047fd40..0048069f); Stuart's release has no copy. The hive boss: a
// hovering fortress whose core's rings turn at speeds its mission script sets, and which keeps its distance from the
// player's battle engine.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "MissionScript/DataType.h"	// the full values; vm.h's stubs step aside
#define ISCRIPT_FULL
#include "unit.h"
#include "InitThing.h"
#include "Guide.h"
#include "Motioncontroller.h"
#include "DestructableSegmentsController.h"
#include "DestroyableSegment.h"
#include "renderthing.h"
#include "map.h"
#include "MissionScript/IScript.h"
#include "game.h"
#include "player.h"
#include "BattleEngine.h"
#include "eventmanager.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"
#include "DebugLog.h"

// FUNCTION: 0x0047fd40 init:ZERO_FVECTOR
// FUNCTION: 0x0047fd60 init:ID_FMATRIX

//******************************************************************************************
void CHiveBoss::Init(CInitThing *init)	// FUNCTION: 0x0047fe30
{
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_CAN_BE_WALKED_ON | THING_TYPE_TREE | THING_TYPE_UNKNOWN_08000000;
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;

#line 33
	mSegments = new(MT_DESTROYABLESEGMENT) CDestructableSegmentsController(this, 10.0f, FALSE, 1);
	mMotionController = new(MEMTYPE_MOTIONCONTROLLER) CMCHiveBoss(this);

	SUPERTYPE::Init(init);

	mCore = mSegments->FindSegment("core2");
#line 40
	mGuide = new(MEMTYPE_GUIDE) CHiveBossGuide(this);

	mCoreAngles[0] = 0;
	mCoreAngles[1] = 0;
	mCoreAngles[2] = 0;
	mCoreAngles[3] = 0;
	mCoreAngles[4] = 0;
	mCoreAngles[5] = 0;
	mMinHeight = -1.0f;
	mCoreSpeeds[0] = 0;
	mCoreSpeeds[1] = 0;
	mCoreSpeeds[2] = 0;
	mCoreSpeeds[3] = 0;
	mCoreSpeeds[4] = 0;
	mCoreSpeeds[5] = 0;
	mSafeDist = 30.0f;
	mRotateSpeed = 0;
	mUnknown12C = CEulerAngles(10.0f, 0.f, 0.f);
}

//******************************************************************************************
// The whole boss flashes when its core was hit this frame.
int CHiveBoss::GetShouldDoHitEffect(SINT mesh_part_no)	// FUNCTION: 0x00480000
{
	if ((mSegments) && (mCore) && (mCore->mLastDamageTime == EVENT_MANAGER.GetTime()))
	{
		LOG.AddMessage("!!all flash!!");
		return 100;
	}
	return SUPERTYPE::GetShouldDoHitEffect(mesh_part_no);
}

//******************************************************************************************
// Explosions do not hurt it.
void CHiveBoss::Damage(float amount, CThing *inByThis, BOOL inDamageShields, int mesh_part_no)	// FUNCTION: 0x00480050
{
	if (!inByThis->IsA(THING_TYPE_EXPLOSION))
		SUPERTYPE::Damage(amount, inByThis, inDamageShields, mesh_part_no);
}

//******************************************************************************************
// Where it wants to be: its safe distance from the player's battle engine, when that is over water.
FVector CHiveBoss::Unknown110()	// FUNCTION: 0x00480080
{
	CBattleEngine	*be = NULL;
	if (GAME.GetPlayer(0))
		be = GAME.GetPlayer(0)->GetBattleEngine();
	FVector			r = mPos;
	if (be)
	{
		FVector	bpos = be->GetPos();
		FVector	d; d = mPos - bpos;	// declared then assigned, Stuart's occasional habit (thing.cpp:462, :482); direct initialisation loses 00480080
		d.Z = 0;
		d.Normalise();
		FVector offset = d * mSafeDist;
		if (MAP.Collide(bpos + offset) < MAP.GetWaterLevel())
			r = bpos + offset;
	}
	return r;
}

//******************************************************************************************
// The yaw sum's brackets keep it a value (README: parentheses are a value barrier): retail's scheduling of the core-angle
// copies after it; without them the function is 8 instructions off.
void CHiveBoss::Move()	// FUNCTION: 0x00480220
{
	mUnknown120.mYaw = (mRotateSpeed + mCurrentOrientation.mYaw);

	mOldCoreAngles[0] = mCoreAngles[0];
	mOldCoreAngles[1] = mCoreAngles[1];
	mOldCoreAngles[2] = mCoreAngles[2];
	mOldCoreAngles[3] = mCoreAngles[3];
	mOldCoreAngles[4] = mCoreAngles[4];
	mOldCoreAngles[5] = mCoreAngles[5];

	mCoreAngles[0] += mCoreSpeeds[0];
	mCoreAngles[1] += mCoreSpeeds[1];
	mCoreAngles[2] += mCoreSpeeds[2];
	mCoreAngles[3] += mCoreSpeeds[3];
	mCoreAngles[4] += mCoreSpeeds[4];
	mCoreAngles[5] += mCoreSpeeds[5];

	SUPERTYPE::Move();
}

//******************************************************************************************
// Its script hears of its death.
BOOL CHiveBoss::StartDieProcess()	// FUNCTION: 0x004802f0
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;
	if (mMissionScript)
		EVENT_MANAGER.AddEvent(5000, this, -1.0f, 0, NULL, NULL);
	return TRUE;
}

//******************************************************************************************
// The collision type is one conditional expression, as CUnit's slot 95: the if form stored mColType before retail's early
// pop of ebx (00480491).
void CHiveBoss::Unknown95()	// FUNCTION: 0x00480340
{
	if (!mUnitData)
		return;

	CExplosionInitThing	init;
	if (mRenderThing)
		init.mPos = mRenderThing->GetRandomSurfacePoint(0);
	else
		init.mPos = mPos;
	init.mAllegiance = (EAllegiance)mAllegiance;
	if (MAP.GetWaterLevel() + 2.0f > init.mPos.Z)
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
			init.mColType = (MAP.GetWaterLevel() < init.mPos.Z) ? kCollideWater : kCollideNothing;
			explosion->Init(&init);
		}
	}
}

//******************************************************************************************
void CHiveBoss::SetVar(CStringDataType *name, CDataType *data)	// FUNCTION: 0x004804c0
{
	if (name->Matches("hb_maxvelx"))
		((CHiveBossGuide *)mGuide)->mMaxVelocity[0] = data->GetFloat();
	else if (name->Matches("hb_maxvely"))
		((CHiveBossGuide *)mGuide)->mMaxVelocity[1] = data->GetFloat();
	else if (name->Matches("hb_maxvelz"))
		((CHiveBossGuide *)mGuide)->mMaxVelocity[2] = data->GetFloat();
	else if (name->Matches("hb_rotate_speed"))
		mRotateSpeed = data->GetFloat();
	else if (name->Matches("hb_core_top_inner_rotate_speed"))
		mCoreSpeeds[1] = data->GetFloat();
	else if (name->Matches("hb_core_top_outer_rotate_speed"))
		mCoreSpeeds[0] = data->GetFloat();
	else if (name->Matches("hb_core_mid_inner_rotate_speed"))
		mCoreSpeeds[3] = data->GetFloat();
	else if (name->Matches("hb_core_mid_outer_rotate_speed"))
		mCoreSpeeds[2] = data->GetFloat();
	else if (name->Matches("hb_core_bot_inner_rotate_speed"))
		mCoreSpeeds[5] = data->GetFloat();
	else if (name->Matches("hb_core_bot_outer_rotate_speed"))
		mCoreSpeeds[4] = data->GetFloat();
	else if (name->Matches("hb_safe_dist"))
		mSafeDist = data->GetFloat();
	else if (name->Matches("hb_min_height_above_ground"))
		mMinHeight = data->GetFloat();
	else
		SUPERTYPE::SetVar(name, data);
}

//******************************************************************************************
void CHiveBoss::GetTargetablePos(FVector *out)	// FUNCTION: 0x00480690
{
	GetCentrePos(*out);
}

//******************************************************************************************
// The hive boss's event handler (as CBuilding's, Building.cpp; both derive from CUnit). It compiles to CBuilding's
// body, so the linker folds it into that one (/OPT:ICF).

// FUNCTION: 0x00417df0
void CHiveBoss::HandleEvent(CEvent *event)
{
	if ((event->GetEventNum() == 5000) && (mMissionScript))
	{
		mMissionScript->Died();
		delete mMissionScript;
		mMissionScript = NULL;
	}
	else
		SUPERTYPE::HandleEvent(event);
}
