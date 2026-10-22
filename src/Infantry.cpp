// Infantry.cpp: reconstructed from Infantry.obj (00488ac0..00489f3f); Stuart's release has no copy. An infantryman:
// it walks, runs, parachutes in, is thrown by explosions, and falls one of four ways when it dies.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "UnitAI.h"
#include "Guide.h"
#include "collisionseekingthing.h"
#include "cylinder.h"
#include "rtmesh.h"
#include "renderthing.h"
#include "animation.h"
#include "map.h"
#include "game.h"
#include "eventmanager.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"
#include "round.h"
#include "ParticleManager.h"

#include <math.h>

#include "CollisionSeekingInfantryBloke.h"

// FUNCTION: 0x00488ac0 init:ZERO_FVECTOR
// FUNCTION: 0x00488ae0 init:ID_FMATRIX

//******************************************************************************************
// The zero velocity is a named vector: retail gives it a frame slot apart from the turn-rate angles
// (00488c83). FVector::Azimuth uses the VC6 float atan2 wrapper, retaining the column operand setup
// at 00488c3d..00488c43. The complete 528-byte section and all 20 relocations match.
void CInfantryUnit::Init(CInitThing *init)	// FUNCTION: 0x00488bb0
{
	init->mInitCST.mMaxCollision = ECL_APPROX_GEOMETRY_SHAPES;
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_UNIT | THING_TYPE_TREE;
#line 28
	mCollisionSeekingThing = new(MT_CST) CCollisionSeekingInfantryBloke;

	if (init->mOrientationType == DIRECTION_COSINE_MATRIX)
	{
		init->mYaw = init->mOrientation.GetColumnY().Azimuth();
		init->mOrientationType = EULER_ANGLES;
	}
	init->mRoll = 0;
	init->mPitch = 0;

	mMoveMultiplier = 4.0f;
	if ((((CUnitInitThing *)init)->mStats) && (((CUnitInitThing *)init)->mStats->mParachute == 1))
		mMoveMultiplier = 1.0f;

	SUPERTYPE::Init(init);

	FVector	v(0, 0, 0);
	SetVelocity(v);
	SetAnimMode(AM_STAND, TRUE);

	mUnknown12C = CEulerAngles(((CUnitInitThing *)init)->mStats->mTurnRate, 0, 0);
	mMaxShields = mShields = ((CUnitInitThing *)init)->mStats->mGroundShields;
	mShieldRegeneration = ((CUnitInitThing *)init)->mStats->mGroundShieldRegeneration;

#line 70
	mGuide = new(MEMTYPE_GUIDE) CInfantryGuide(this);
	mAI = new(MEMTYPE_AI) CInfantryAI(this, init);
	mUnknown268 = 0;
	mKnockedDown = FALSE;
}

// CInfantryAI's and CCollisionSeekingInfantryBloke's implicit destructors, and the bloke's bases' inline
// constructors, emitted here.
// FUNCTION: 0x00488dc0 ??_GCInfantryAI@@UAEPAXI@Z
// FUNCTION: 0x00488de0 ??1CInfantryAI@@UAE@XZ
// FUNCTION: 0x00488e80 ??_GCCollisionSeekingInfantryBloke@@UAEPAXI@Z
// FUNCTION: 0x00488ea0 ??1CCollisionSeekingInfantryBloke@@UAE@XZ
// FUNCTION: 0x00488ef0 ??0CCollisionSeekingThing@@QAE@XZ
// FUNCTION: 0x00488f00 ??0CHLCollisionDetector@@QAE@XZ

//******************************************************************************************
// A vehicle that runs it down within two seconds of it touching the ground hurts it.
void CInfantryUnit::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x00488f10
{
	if ((other_thing->IsA(THING_TYPE_VEHICLE)) && (!(mFlags & TF_DYING)) && (EVENT_MANAGER.GetTime() - mLastTimeOnGround < 2.0f))
		Damage(10.0f, other_thing, TRUE, -1);
	else
		CUnit::Hit(other_thing, report);
}

//******************************************************************************************
void CInfantryUnit::Shutdown()	// FUNCTION: 0x00488f60
{
	mEffect.Kill();
	CUnit::Shutdown();
}

//******************************************************************************************
// A cylinder as wide as its bounding box.
void CInfantryUnit::InitCollisionSeekingThing(CInitCSThing *init)	// FUNCTION: 0x00488f80
{
	CBoundingBox	*box = GetBoundingBox();
	if (box)
	{
		float	height = box->mAxes.Z;
		float	radius = (float)sqrt((box->mAxes.X * box->mAxes.X) + (box->mAxes.Y * box->mAxes.Y)) * 0.5f;
#line 122
		init->mShape = new(MEMTYPE_GENERAL_VOLUME) CCylinder(radius, height);
	}
	SUPERTYPE::InitCollisionSeekingThing(init);
}

//******************************************************************************************
BOOL CInfantryUnit::Unknown74()	// FUNCTION: 0x00489040
{
	BOOL	r = FALSE;
	if ((mCurrentWeapon) && (!mKnockedDown) && (!(mFlags & TF_DYING)))
	{
		r = SUPERTYPE::Unknown74();
		if (r)
		{
			mUnknown268 = 18;
			SetAnimMode(AM_SHOOT, TRUE);
		}
	}
	return r;
}

//******************************************************************************************
// Walking, running and standing follow its speed; a dead or thrown one only takes the death animations.
BOOL CInfantryUnit::SetAnimMode(EAnimMode am, BOOL reset_frame, BOOL force_looped)	// FUNCTION: 0x00489090
{
	if (!mUnknown110)
		return FALSE;

	if ((!(mFlags & TF_DYING)) && (!mKnockedDown))
	{
		if ((EVENT_MANAGER.GetTime() - mLastTimeOnGround >= 2.0f) && (mAI->mAIMode != 2))
			am = AM_FLY;
		else
		{
			float	speed = GetVelocity().MagnitudeXYSq();
			BOOL	walking = (am == AM_WALK) || (am == AM_STAND) || (am == AM_RUN);
			if (walking)
			{
				if (speed > 0.023f * 0.023f)
					am = AM_RUN;
				else if (speed > 0.008f * 0.008f)
					am = AM_WALK;
				else
					am = AM_STAND;
			}
			else if ((am == AM_SHOOT1) || (am == AM_SHOOT2) || (am == AM_SHOOT))
			{
				if (speed > 0.008f * 0.008f)
					am = AM_SHOOTWALK;
			}
		}
		return CComplexThing::SetAnimMode(am, reset_frame, force_looped);
	}

	if ((am == mRenderThing->GetRTMesh()->GetAnimModeByName("die_forward")) ||
		(am == mRenderThing->GetRTMesh()->GetAnimModeByName("die_right")) ||
		(am == mRenderThing->GetRTMesh()->GetAnimModeByName("die_left")) ||
		(am == mRenderThing->GetRTMesh()->GetAnimModeByName("die_back")) ||
		(am == mRenderThing->GetRTMesh()->GetAnimModeByName("dead_up")) ||
		(am == mRenderThing->GetRTMesh()->GetAnimModeByName("dead_forward")) ||
		(am == mRenderThing->GetRTMesh()->GetAnimModeByName("dead_right")) ||
		(am == mRenderThing->GetRTMesh()->GetAnimModeByName("dead_left")) ||
		(am == mRenderThing->GetRTMesh()->GetAnimModeByName("dead_back")) ||
		(am == mRenderThing->GetRTMesh()->GetAnimModeByName("ateam")))
		CComplexThing::SetAnimMode(am, reset_frame, force_looped);
	return FALSE;
}

//******************************************************************************************
void CInfantryUnit::Move()	// FUNCTION: 0x004892c0
{
	SUPERTYPE::Move();

	// it runs the way it faces
	if ((mUnknown110) && (!(mFlags & TF_DYING)))
	{
		float	speed = mVelocity.MagnitudeXY();
		FVector	v;
		v = FVector(speed * mOrientation.Row[0].Y, speed * mOrientation.Row[1].Y, speed * mOrientation.Row[2].Y);
		v.Z = mVelocity.Z;
		SetVelocity(v);
	}

	if (mUnknown268 > 0)
	{
		mUnknown268 -= (SINT)mMoveMultiplier;
		if (mUnknown268 < 0)
			mUnknown268 = 0;
		return;
	}

	if ((!(mFlags & TF_DYING)) && (!mKnockedDown))
	{
		if ((!IsOnGround()) && (mUnitData->mParachute == 1))
		{
			float	z = mPos.Z;
			float	height = z - MAP.Collide(mPos);
			if (height - COfGHeight() > -2.0f)
				SetAnimMode(AM_HOVER, FALSE, TRUE);
			else
				SetAnimMode(AM_FLY, FALSE, TRUE);
		}
		else
			SetAnimMode(AM_WALK, FALSE, TRUE);
		return;
	}

	// a thrown one lands
	if ((IsOnGround()) && (MAP.Collide(mPos) <= mPos.Z))
	{
		if (GetRenderAnimation() == mRenderThing->GetRTMesh()->GetAnimModeByName("ateam"))
		{
			if (mFlags & TF_DYING)
				SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("dead_forward"), TRUE, TRUE);
			mKnockedDown = FALSE;
		}
	}

	if (mFlags & TF_DYING)
	{
		if (mRemoveTime < EVENT_MANAGER.GetTime())
			CComplexThing::AddShutdownEvent();

		for (SINT n = (SINT)mMoveMultiplier; n > 0; n--)
		{
			mUnknown14C *= 0.97f;
			if (IsOnGround())
				mUnknown14C *= 0.85f;
		}

		if (mEffect.mEffect)
			mEffect.SetPos(mPos);
	}
}

//******************************************************************************************
// An explosion or a round with blast throws it; then it falls away from what hit it.
void CInfantryUnit::Damage(float amount, CThing *inByThis, BOOL inDamageShields, int mesh_part_no)	// FUNCTION: 0x00489650
{
	if (mFlags & TF_DYING)
		return;

	BOOL	thrown = FALSE;
	if (((inByThis->IsA(THING_TYPE_EXPLOSION)) && (((CExplosion *)inByThis)->Unknown67() > 0.05f)) ||
		((inByThis->IsA(THING_TYPE_AMMUNITION)) && (((CRound *)inByThis)->GetExplosionRadius() > 0.05f)) ||
		(inByThis->IsA(THING_TYPE_BATTLE_ENGINE)))
	{
		thrown = TRUE;
		if (!mKnockedDown)
		{
			CComplexThing::SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("ateam"), TRUE, TRUE);
			mAnimation->mFrame = float(GAME.Random() % 65536) / 65536.0f;
		}

		FVector	pos = mPos;
		pos.Z -= 0.5f;
		FVector	d = pos - inByThis->GetPos();
		d.Normalise();
		if (inByThis->IsA(THING_TYPE_EXPLOSION))
			d.Z -= float(GAME.Random() % 65536) * (0.25f / 65536.0f);
		else if (inByThis->IsA(THING_TYPE_BATTLE_ENGINE))
			d.Z = -(float(GAME.Random() % 65536) * (0.3f / 65536.0f) + 0.3f);
		d.Normalise();
		mKnockedDown = TRUE;
		d *= 2.0f;
		SetUnknown14C(d);
	}

	CUnit::Damage(amount, inByThis, inDamageShields, mesh_part_no);

	if ((mFlags & TF_DYING) && (!thrown))
	{
		FVector	d;
		if (inByThis->IsA(THING_TYPE_AMMUNITION))
		{
			FMatrix	&ori = inByThis->GetOrientation();
			d = ori * FVector(0.0f, 1.0f, 0.0f);
		}
		else
			d = mPos - inByThis->GetPos();

		float	angle = d.Azimuth();
		float	yaw = mCurrentOrientation.mYaw;
		if ((yaw < -PI / 2) && (angle > PI / 2))
			angle -= PI * 2;
		else if ((yaw > PI / 2) && (angle < -PI / 2))
			angle += PI * 2;
		float	diff = yaw - angle;

		if ((float)fabs(diff) < PI / 4)
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("die_forward"), TRUE, FALSE);
		else if ((diff >= PI / 4) && (diff < PI * 3 / 4))
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("die_right"), TRUE, FALSE);
		else if ((diff <= -PI / 4) && (diff > -PI * 3 / 4))
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("die_left"), TRUE, FALSE);
		else
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("die_back"), TRUE, FALSE);

		d.Z = 0;
		d.Normalise();
		d *= (float(GAME.Random() % 65536) * (3.0f / 65536.0f) + 1.0f);
		SetUnknown14C(d);
	}

	// STAND-IN (self-copy arm; labelled 2026-10-15, origin not recorded): the AI state is stored again when it
	// already holds 2, which emits no store. Without it: score 11/27/53. Original: unknown.
	if (mAI->mAIMode == 2)
		mAI->mAIMode = 2;
}

//******************************************************************************************
BOOL CInfantryUnit::StartDieProcess()	// FUNCTION: 0x00489b40
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;

	mRemoveTime = EVENT_MANAGER.GetTime() + 15.0f;
	mCollisionSeekingThing->SetCollisionMask(0xffffffff);

	SINT		index = mUnitData->mExplosion;
	CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
	if (explosion)
	{
		CExplosionInitThing	init;
		init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mExplosion);
		init.mAllegiance = (EAllegiance)mAllegiance;
		init.mAttachedTo = this;
		init.mUseAttachedRadius = TRUE;
		if (IsOnGround())
			init.mColType = kCollideGround;
		else
			init.mColType = (IsInWater()) ? kCollideWater : kCollideNothing;
		init.mPos = mPos;
		explosion->Init(&init);
	}

	if (mUnitData->mParticles[4])
	{
		mEffect.Reset();
		PARTICLE_MANAGER.CreateEffect(mUnitData->mParticles[4], &mEffect, ZERO_FVECTOR, 0, 0);
		mEffect.SetPos(mPos);
	}

	DeactivateThrusters();
	return TRUE;
}

//******************************************************************************************
// A death animation that has finished leaves it lying the matching way.
BOOL CInfantryUnit::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x00489de0
{
	EAnimMode	am = GetRenderAnimation();
	if (am != AM_INVALID)
	{
		if (am == mRenderThing->GetRTMesh()->GetAnimModeByName("die_up"))
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("dead_up"), TRUE, TRUE);
		else if (am == mRenderThing->GetRTMesh()->GetAnimModeByName("die_back"))
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("dead_back"), TRUE, TRUE);
		else if (am == mRenderThing->GetRTMesh()->GetAnimModeByName("die_left"))
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("dead_left"), TRUE, TRUE);
		else if (am == mRenderThing->GetRTMesh()->GetAnimModeByName("die_right"))
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("dead_right"), TRUE, TRUE);
		else
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("dead_forward"), TRUE, TRUE);
	}
	return FALSE;
}

//******************************************************************************************
float CInfantryUnit::GetRenderImposterBias()	// FUNCTION: 0x00489ed0
{
	return (mFlags & TF_DYING) ? 0.0f : 1.0f;
}

//******************************************************************************************
void CInfantryUnit::DeclareOnGround()	// FUNCTION: 0x00489ef0
{
	if (mFlags & TF_DYING)
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("dead_forward"), TRUE, TRUE);
	mKnockedDown = FALSE;
	CActor::DeclareOnGround();
}

//******************************************************************************************
// The infantry unit's answers that no code above defined. Each compiles to a body another function already has, so the
// linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address each
// annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x0040c640
void CInfantryUnit::Stop()
{
}

// FUNCTION: 0x004dbc20
int CInfantryUnit::GetImposterFrames()
{
	return 5;
}
