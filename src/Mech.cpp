// Mech.cpp: reconstructed from Mech.obj (0049f730..004a01ef); Stuart's release has no copy. A mech: it walks on legs
// through a CMCMech, aims from its "body", stamps explosions under its feet and sinks as it dies.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "game.h"
#include "BattleEngine.h"
#include "MCBattleEngine.h"
#include "UnitAI.h"
#include "Guide.h"
#include "cylinder.h"
#include "rtmesh.h"
#include "renderthing.h"
#include "mesh.h"
#include "eventmanager.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"
#include "ParticleManager.h"

#include <stdlib.h>

// FUNCTION: 0x0049f730 init:ZERO_FVECTOR
// FUNCTION: 0x0049f750 init:ID_FMATRIX

//******************************************************************************************
void CMech::Init(CInitThing *init)	// FUNCTION: 0x0049f820
{
	SUPERTYPE::Init(init);
	SetAnimMode(AM_WALK, TRUE);

	float	turn = ((CUnitInitThing *)init)->mStats->mTurnRate;
	mUnknown12C = CEulerAngles(turn, turn, turn);
	mMaxShields = mShields = ((CUnitInitThing *)init)->mStats->mGroundShields;
	mShieldRegeneration = ((CUnitInitThing *)init)->mStats->mGroundShieldRegeneration;

	InitTargeting();
	InitCockpit(init);
	InitLegMotion(init);

	FVector	pos;
	GetTargetablePos(&pos);
	mSinkSpeed = 0;
	mBodyDepth = 0;
	mCockpitHeight = pos.Z - mPos.Z;
	if ((mRenderThing) && (mRenderThing->GetRTMesh()))
	{
		CMeshPart	*body = GetRTMesh()->GetMeshSubpart("body");
		if (body)
			mBodyDepth = body->mBoundingBox->mAxes.Z;
	}
}

//******************************************************************************************
void CMech::InitLegMotion(CInitThing *init)	// FUNCTION: 0x0049f940
{
	if (mRenderThing->GetRTMesh()->GetAnimModeByName("LegMotion") != AM_INVALID)
	{
#line 61
		CMCMech	*mc = new(MEMTYPE_MOTIONCONTROLLER) CMCMech(this);
		mMotionController = mc;
		CUnitData	*data = ((CUnitInitThing *)init)->mStats;
		mc->SetMechParameters(data->mBodyMotion, data->mLegSpeed, data->mLegPlacementArea, 0.4f, 0.9f, data->mStandingLegPlacementArea, data->mMaxLegsLifted);
	}
	else
		mMotionController = NULL;
}

//******************************************************************************************
void CMech::InitCockpit(CInitThing *init)	// FUNCTION: 0x0049fa30
{
#line 72
	mAI = new(MEMTYPE_AI) CMechAI(this, init);
}

//******************************************************************************************
void CMech::InitTargeting()	// FUNCTION: 0x0049faa0
{
#line 78
	mGuide = new(MEMTYPE_GUIDE) CMechGuide(this);
}

//******************************************************************************************
// A cylinder from its bounding box; it does not collide with bit 14's things.
void CMech::InitCollisionSeekingThing(CInitCSThing *init)	// FUNCTION: 0x0049fb00
{
	CBoundingBox	*box = GetBoundingBox();
	if (box)
	{
#line 87
		CCylinder	*shape = new(MEMTYPE_GENERAL_VOLUME) CCylinder(box);
		GetBoundingRadius();
		init->mShape = shape;
	}
	init->mNotSeekCollisionWithBF |= THING_TYPE_INFANTRY;
	SUPERTYPE::InitCollisionSeekingThing(init);
}

//******************************************************************************************
// It walks or stands as it moves.
BOOL CMech::SetAnimMode(EAnimMode am, BOOL reset_frame, BOOL force_looped)	// FUNCTION: 0x0049fba0
{
	float	speed = GetVelocity().MagnitudeXY();
	if ((am == 1) || (am == 3))
		am = (speed > 0.02) ? AM_WALK : AM_STAND;
	return CComplexThing::SetAnimMode(am, reset_frame, force_looped);
}

//******************************************************************************************
void CMech::Move()	// FUNCTION: 0x0049fc10
{
	// a dying mech sinks to its body's depth
	if (mFlags & TF_DYING)
	{
		mSinkSpeed += 0.002f;
		mCockpitHeight += mSinkSpeed;
		if (mCockpitHeight >= -mBodyDepth)
		{
			mCockpitHeight = -mBodyDepth;
			mSinkSpeed *= -0.2f;
		}
		Unknown95();
	}

	// a foot came down
	CMCMech	*mc = (CMCMech *)mMotionController;
	if ((mc) && (mc->mUnknownD0))
	{
		SINT		index = mUnitData->mStompExplosion;
		CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
		if (explosion)
		{
			CExplosionInitThing	init;
			init.mPos = mc->mUnknownD4;
			init.mAllegiance = (EAllegiance)mAllegiance;
			init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mStompExplosion);
			init.mColType = kCollideNothing;
			init.mAttachedTo = this;
			explosion->Init(&init);
			mc->mUnknownD0 = 0;
		}
	}

	SUPERTYPE::Move();
}

//******************************************************************************************
// The type of a mesh part (name ours), declared in mesh.h. Retail's Unknown71 tests the part's type through a call that
// inlines: the inline's parameter binding is one more reference to the loop index in the loop's first block, which makes
// the allocator colour the index before the particle pointer (i in ebp, the pointer in esi with its spill).
inline SINT CMeshPart::GetType() { return mPartType; }

//******************************************************************************************
// As it dies, each part marked to break (type 1) flies off as mesh debris from where it is. The velocity's scaled Z
// stays on x87 through its doubling and bias, as at 004a003d..004a004b.
void CMech::Unknown71(float amount, CThing *by)	// FUNCTION: 0x0049fdb0
{
	if (!mUnitData->mShatter)
		return;
	CParticleDescriptor	*pd = PARTICLE_SET.FindByName("Generic Mesh");
	if (!mRenderThing)
		return;
	CMesh	*mesh = mRenderThing->GetRTMesh();
	if (!mesh)
		return;

	for (SINT i = 0; i < mesh->mNumParts; i++)
	{
		if (mesh->GetPart(i)->GetType() == 1)
		{
			CParticleEffectLink	link;
			PARTICLE_MANAGER.CreateEffect(pd, &link, ZERO_FVECTOR, 0, 0);
			if (link.mEffect)
			{
				CParticle	*particle = link.mEffect->mParticle;
				if (particle)
				{
					FMatrix	ori;
					FVector	pos;
					mesh->GetPart(i)->GetWorldTransforms(0, 0, 0, this, &ori, &pos, false, TRUE, 0);
					FVector	origin = mesh->GetPart(i)->mBoundingBox->mOrigin;
					FVector	centre = ori * origin + pos;
					link.SetPos(centre);
					link.SetOrientation(ori);

					SINT	a = (rand() & 0xff) - 128;
					SINT	b = (rand() & 0xff) - 128;
					SINT	c = (rand() & 0xff) - 128;
					particle->mMesh = mesh;
					particle->mUnknown7E = (SWORD)i;
					FVector	vel;
					vel.Set(float(c) * 0.0003125f, float(b) * 0.0003125f, float(a) * 0.0003125f);
					vel.Z = vel.Z * 2 - 0.04f;
					particle->mVelocity = vel;
					particle->mRadius = mesh->Radius();
				}
			}
		}
	}
}

//******************************************************************************************
BOOL CMech::StartDieProcess()	// FUNCTION: 0x004a00a0
{
	if (mUnitData->mShatter)
	{
		BOOL	r = SUPERTYPE::StartDieProcess();
		SpawnExplosion();
		AddShutdownEvent();
		return r;
	}
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;
	ReleaseChildren();
	EVENT_MANAGER.AddEvent(4004, this, EVENT_MANAGER.GetTime() + 3.5f, 0, NULL, NULL);
	return TRUE;
}

//******************************************************************************************
float CMech::Unknown106()	// FUNCTION: 0x004a0120
{
	return mCockpitHeight + mPos.Z;
}

//******************************************************************************************
// Its "body" part's height over its position.
void CMech::GetTargetablePos(FVector *out)	// FUNCTION: 0x004a0130
{
	if ((mRenderThing) && (mRenderThing->GetRTMesh()))
	{
		CMeshPart	*body = GetRTMesh()->GetMeshSubpart("body");
		if (body)
		{
			*out = mPos;
			FVector	pos;
			FMatrix	ori;
			body->GetWorldTransforms(0, 0, 0, this, &ori, &pos, false, 0, 0);
			out->Z = pos.Z;
			return;
		}
	}
	*out = mPos;
}
