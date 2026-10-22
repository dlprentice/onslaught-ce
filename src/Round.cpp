// Round.cpp: reconstructed from Round.obj (004d80f0..004db84f); Stuart's release has no copy. A round in flight:
// bullets, shells and missiles, from a CRoundData definition.

#include "common.h"
#include "round.h"
#include "WorldPhysicsManager.h"
#include "world.h"
#include "eventmanager.h"
#include "BattleEngine.h"
#include "DebugLog.h"
#include "map.h"
#include "Line.h"
#include "mapwho.h"
#include "InitThing.h"
#include "Sphere.h"
#include "CollisionSeekingRound.h"
#include "ParticleManager.h"
#include "unit.h"
#include "player.h"
#include "GeneralVolume.h"
#include "game.h"
#include "explosion.h"
#include "engine.h"
#include "landscape.h"

// FUNCTION: 0x004d80f0 init:ZERO_FVECTOR
// FUNCTION: 0x004d8110 init:ID_FMATRIX
// FUNCTION: 0x004d8290 ?_GetClassName@CRound@@UAEPADXZ
// FUNCTION: 0x004d8350 ??_GCRound@@UAEPAXI@Z
// FUNCTION: 0x004d8370 ??1CRound@@UAE@XZ

//******************************************************************************************
// Inline and defined before the constructor: retail emits these six in the constructor's vtable group, after
// _GetClassName and ahead of ??_GCRound (004d82a0..004d8340), in this order, which is where VC6 puts inline
// virtuals defined before the vtable's first user; as plain functions they came after ??_G.
inline float CRound::GetMaxVelocity()	// FUNCTION: 0x004d82a0
{
	if (Gravity() != 0)
		return 160.0f;
	return mData->mSpeed;
}

//******************************************************************************************
inline float CRound::BounceFactor()	// FUNCTION: 0x004d82d0
{
	return mData->mBounceFactor;
}

//******************************************************************************************
inline BOOL CRound::ClipToGround()	// FUNCTION: 0x004d82e0
{
	if ((mData->mBounceFactor == 0) && (mData->mGravity == 0) && (mData->mUnknown28 <= 0))
		return FALSE;
	return TRUE;
}

//******************************************************************************************
inline void CRound::SetThingType(ULONG t)	// FUNCTION: 0x004d8320
{
	SUPERTYPE::SetThingType(t | THING_TYPE_AMMUNITION);
}

//******************************************************************************************
inline BOOL CRound::CanGoUnderWater()	// FUNCTION: 0x004d8330
{
	return mData->mCanGoUnderWater;
}

//******************************************************************************************
inline SINT CRound::GetSoundMaterial()	// FUNCTION: 0x004d8340
{
	return mData->mSoundMaterial;
}

//******************************************************************************************
CRound::CRound(CRoundData *data)	// FUNCTION: 0x004d81e0
{
	mData = data;
	mUnknown124 = 0;
	mStartTime = EVENT_MANAGER.GetTime();
	mUnknown120 = FALSE;
	mUnknown12C = FALSE;
	mUnknown130 = 1;
}

//******************************************************************************************
// Out of line in retail (CMissile's slot 71 calls it, 004bac2b); the linker folds it into the shared empty 00452da0.
void CRound::AccumulateResources(CResourceAccumulator *accumulator, DWORD flags)
{
}

//******************************************************************************************
void CRound::Init(CInitThing *init)	// FUNCTION: 0x004d8410
{
	CRoundInitThing	*rinit = (CRoundInitThing *)init;

	mDest = rinit->mDest;
	mJumpsPerformed = rinit->mJumpsPerformed;
	mData = rinit->mRoundData;
	mAllegiance = init->mAllegiance;
	mUnknown128 = 0;

	init->mInitCST.mDesiredCollision = ECL_MESH;
	if (mData->mGillMBreath == TRUE)
	{
		init->mInitCST.mDoOBBForMeshCol = TRUE;
		init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_NAMED_MESH;
	}
	if (mData->mUnknown70)
	{
		init->mInitCST.mMinCollision = ECL_MESH;
		init->mInitCST.mMaxCollision = ECL_MESH;
	}
	else
	{
		init->mInitCST.mMinCollision = ECL_APPROX_GEOMETRY_SHAPES;
		init->mInitCST.mMaxCollision = ECL_APPROX_GEOMETRY_SHAPES;
		init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_AMMUNITION;
	}
	init->mInitCST.mCollisionResponse = ECR_STATIC;
	if (mData->mTreeCollision == 2)
		init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_TREE;

	if (mData->mBeam)
	{
		init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
		SUPERTYPE::Init(init);
		EVENT_MANAGER.AddEvent(4000, this, EVENT_MANAGER.GetTime());
		mBeamStart = GetPos();
	}
	else
	{
		if (!mData->mUnknown6C)
			init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_EXPLOSION;

		if (mData->mRadius > 0)
		{
#line 98
			init->mInitCST.mShape = new (MT_ROUND) CSphere(mData->mRadius);
			init->mInitCST.mIgnoreThing = mUnknownEC.ToRead();
			if (mData->mPassiveCollision)
				init->mInitCST.mCollisionResponse = ECR_PASSIVE;
		}
		else
		{
			// a round with no radius is a line: a seeker the length of its travel

#line 108
			mCollisionSeekingThing = new (MT_CST) CCollisionSeekingRound;
			init->mInitCST.mRoundLength = mData->mLength;
		}
		SUPERTYPE::Init(init);
		EVENT_MANAGER.AddEvent(4000, this, EVENT_MANAGER.GetTime() + rinit->mLifeSpan);

		if (mData->mParticleA)
		{
			CUnit	*launcher = (CUnit *)mUnknownEC.ToRead();
			if (launcher)
				PARTICLE_MANAGER.AddParticle(mData->mParticleA, &mEffect, launcher->mUnitData ? launcher->mUnitData->mFenrir : TRUE);
			else
				PARTICLE_MANAGER.AddParticle(mData->mParticleA, &mEffect, FALSE);
			CParticleEffectLink	*link = &mEffect;
			link->SetPos(GetPos());
			link->SetOrientation(mOrientation);
		}

		// a round that flies straight: find where it meets the ground now
		if ((!(mData->mUnknown28 > 0)) && (!mData->mGroundHugging) && (Gravity() == 0))
		{
			FVector	start = GetPos();
			float	speed = GetVelocity().Magnitude() * GAME_FR;
			FVector	dir = GetVelocity();
			dir.Normalise();
			CLine	line(start, start + dir * (speed * rinit->mLifeSpan));
			FVector	hit;
			if (MAP.Unknown490a40(&line, &hit, 1))
			{
				float	t = (start - hit).Magnitude() / speed;
				EVENT_MANAGER.AddEvent(4001, this, EVENT_MANAGER.GetTime() + t, END_OF_FRAME);
				mDest = hit;
			}
		}
	}

	if (mData->mUnknown58)
		WORLD.mSet60.Add(this);

	Unknown4dac90(NULL);
}

//******************************************************************************************
float CRound::GetRadius()	// FUNCTION: 0x004d8ac0
{
	return (mData->mSpeed / GAME_FR) * 0.5f + mData->mRadius;
}

//******************************************************************************************
// 004e2e60 (SoundMaterials.cpp, its own object between SoundManager.obj and SpawnerThng.obj): the sound of two things' materials meeting
void	PlayImpactSound(CThing *a, CThing *b);

void CRound::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x004d8ae0
{
	SUPERTYPE::Hit(other_thing, report);

	if (mFlags & TF_DYING)
		return;

	if ((report) && (!report->mTestTriangles) && (!mData->mPassiveCollision) && (!other_thing->IsA(THING_TYPE_EXPLOSION)))
		return;

	if (mUnknown124)
		return;

	if ((mData->mUnknown1C < 0) && (other_thing != mUnknownE8.ToRead()))
		return;

	if ((mData->mSmart) && (((CUnit *)other_thing)->mAllegiance == mAllegiance))
		return;

	CThing	*launcher = mUnknownEC.ToRead();
	if ((launcher) && (launcher->IsA(THING_TYPE_BATTLE_ENGINE)) && (other_thing) && (other_thing->IsA(THING_TYPE_UNIT)) &&
		(((CUnit *)other_thing)->mAllegiance != ((CUnit *)launcher)->mAllegiance))
		((CBattleEngine *)launcher)->GetPlayer()->IncStat(PS_ROUNDSHIT);

	// a beam reaches the thing it hit
	if ((mData->mBeam) && (other_thing) && (other_thing->GetCST()) && (other_thing->GetCST()->GetMaxCollisionLevel() < ECL_MESH))
	{
		FVector	to_thing = other_thing->GetPos() - mPos;
		FVector	dir = mVelocity;
		dir.Normalise();
		mPos += dir * to_thing.Magnitude();
	}

	mUnknown124 = 1;

	SINT	part = -1;
	if ((report->mUnknown00) && (report->mNumContacts > 0))
		part = report->mContactParts[0];

	BOOL	was_dying = other_thing->IsDying();
	other_thing->Damage(mData->mUnknown1C, this, TRUE, part);

	if (other_thing->IsA(THING_TYPE_BATTLE_ENGINE))
		((CBattleEngine *)other_thing)->Rearm(mData->mRearm);

	launcher = mUnknownEC.ToRead();
	if ((launcher) && (launcher->IsA(THING_TYPE_UNIT)) && (other_thing->IsA(THING_TYPE_UNIT)) && (other_thing->IsDying()) && (!was_dying))
		((CUnit *)launcher)->ConfirmedKill((CUnit *)other_thing);

	PlayImpactSound(this, other_thing);
	Unknown4d9f30(3, report, other_thing);

	if (mData->mBeam)
	{
		mFlags |= TF_DYING;
		EVENT_MANAGER.AddEvent(2000, this, EVENT_MANAGER.GetTime() + 0.1f);
	}
	else if (!mData->mUnknown60)
		StartDieProcess();

	mUnknown124 = 0;
}

//******************************************************************************************
void CRound::Shutdown()	// FUNCTION: 0x004d8dc0
{
	if (mData->mUnknown58)
		WORLD.mSet60.Remove(this);

	mEffect.Kill();

	if ((mUnknownEC.ToRead()) && (mUnknownEC->IsA(THING_TYPE_BATTLE_ENGINE)))
		((CBattleEngine *)mUnknownEC.ToRead())->LockHit(mUnknownE8.ToRead());

	if ((mUnknownE8.ToRead()) && (mUnknownE8->IsA(THING_TYPE_BATTLE_ENGINE)))
		WORLD.mSet110.Remove(this);
	mUnknownE8 = NULL;

	SUPERTYPE::Shutdown();
}

//******************************************************************************************
// A beam only follows its launcher's gun; anything else goes off inside its proximity, wiggles, falls and flies,
// lets go of a battle engine it can no longer lock (or a target that has died), turns toward its target at no more
// than its turn rate once it may seek, and points along its flight while it falls.
// The proximity test writes the squared length out, so d's fields are the function's own (front-end) symbols and are
// read from memory Z, Y, X as retail (004d8f70..004d8f8a). d.MagnitudeSq() reads them through d's member-call alias:
// Y and Z then become shared loads kept on the x87 stack, and the extra temporaries also turn the release check's
// products (retail X, Z, Y at 004d9307).
// The clamped turn goes into new locals, with the unclamped value copied on its own branch: yaw and pitch then stay on
// the x87 stack through the clamp and are stored at the join as retail (004d960c..004d966a). Clamped in place, they
// are stored at their definitions and reloaded for each test.
// The seek vector d is reused for the speed vector handed to the out-of-line FMatrix * FVector (0040d2c0), so d is a
// memory variable: the seek product is written straight into it while pos - mPos stays on the x87 stack, and the
// frame is retail's 0x118 (written as a temporary, d goes to registers, the difference to memory and the frame is
// 16 bytes short).
// STAND-IN (budget): Stopped() nests the first ReleaseTarget so that it is denied there and called out of line as
// retail's (004d8e63). The beam test, ground hugging, the ground-hit flag, the second wiggle test and the seek block's
// reads (all but mUnknown28 and the release test's mUnknown6C) go through file-local by-value accessors: free inline
// sites that put retail's budget on the late products. They take the round, two own-size nodes cheaper than taking
// mData; Data_mBeam alone takes mData, which puts the own size (1441) inside the window that gives retail's call list
// (1438..1459). The stream-laser test, the proximity and the wiggle block read directly, and the ground-hit flag
// (mUnknown128) is read through Data_m128: each accessor's return value takes one inline symbol id, and this placement
// gives the first wiggle product's vector parameter and row field the ids whose keys put its last row in retail's
// X, Y, Z order (004d913d).
// STAND-IN (ids): the seek vector is normalised through FSeekVector, a member copy of FVector::Normalise whose
// scaling is a forced-inline helper (a forced inline spends no budget). Retail multiplies the reciprocal first
// (m * d.X, 004d9549), so the multiplier's id must be above d.X's. d.X is created by the seek product's inlined FVector
// constructor (0x331), after the product's parameters, and Normalise's own m takes one of the ids those parameters
// free (0x327), so maths.h's Normalise always multiplies d.X first. The helper's parameter is allocated after the
// copy's this, m, the sqrtf argument and the helper's this have taken the four freed ids, so it takes a fresh id above
// d.X. That id would shift every later fresh id by one (the turn product's third row turns, 004d977f), so the release
// test's mUnknown6C reads directly: one accessor return symbol fewer gives the id back.
static inline BOOL Stopped(CRound *r)
{
	if (r->mUnknown120)
	{
		r->mEffect.Kill();
		r->ReleaseTarget();
		return TRUE;
	}
	return FALSE;
}
static inline BOOL Data_mBeam(CRoundData *d) { return d->mBeam; }
static inline SINT Data_m128(CRound *r) { return r->mUnknown128; }
static inline BOOL Data_mGroundHugging(CRound *r) { return r->mData->mGroundHugging; }
static inline float Data_mSeekAngle(CRound *r) { return r->mData->mSeekAngle; }
static inline float Data_mSeekDelay(CRound *r) { return r->mData->mSeekDelay; }
static inline float Data_mSeekTerminationTime(CRound *r) { return r->mData->mSeekTerminationTime; }
static inline float Data_mUnknown1C(CRound *r) { return r->mData->mUnknown1C; }
static inline BOOL Data_mUnknown6C(CRound *r) { return r->mData->mUnknown6C; }
static inline BOOL Data_mWeirdoSeek(CRound *r) { return r->mData->mWeirdoSeek; }
static inline float Data_mWiggle(CRound *r) { return r->mData->mWiggle; }
// STAND-IN (ids): Normalise scaling through a forced-inline helper whose parameter takes an id above d.X (Move).
struct FSeekVector : public FVector
{
	__forceinline void	Scale(float f) { X *= f; Y *= f; Z *= f; }
	void	Normalise() { float m = sqrtf((X * X) + (Y * Y) + (Z * Z)); if (m) Scale(1.0f / m); }
};
void CRound::Move()	// FUNCTION: 0x004d8e40
{
	if (Stopped(this))
		return;

	if (Data_mBeam(mData))
	{
		if (mData->mStreamLaser)
		{
			if ((mUnknownEC.ToRead()) && (mUnknownEC->IsA(THING_TYPE_BATTLE_ENGINE)))
			{
				FVector	dir = mPos - mBeamStart;
				((CBattleEngine *)mUnknownEC.ToRead())->GetBeamPosition(mBeamStart, mOrientation);
			}
			mEffect.SetPos(mBeamStart);
			if (mEffect.mEffect)
				mEffect.mEffect->mUnknown90 = mPos;
		}
		return;
	}

	if (mUnknownE8.ToRead())
	{
		FVector	d = mUnknownE8->GetPos() - mPos;
		if ((d.X * d.X + d.Y * d.Y + d.Z * d.Z) < mData->mProximity * mData->mProximity)
		{
			Unknown4d9f30(0, 0, 0);
			StartDieProcess();
		}
	}

	FMatrix	wiggle, unwiggle;
	if (mData->mWiggle > 0)
	{
		float	pitch = (GAME.FloatRandom() * 2 - 1) * mData->mWiggle;
		float	yaw = (GAME.FloatRandom() * 2 - 1) * mData->mWiggle;
		wiggle = FMatrix(yaw, pitch, 0);
		unwiggle = FMatrix(-yaw, -pitch, 0);
		mVelocity = wiggle * mVelocity;
	}

	mVelocity.Z += Gravity();
	if (Data_mGroundHugging(this))
	{
		if (MAP.Collide(mPos) < mPos.Z)
			mUnknown128 = 1;
		if (Data_m128(this))
			mVelocity.Z = MAP.Collide(mPos) - mPos.Z;
	}

	SUPERTYPE::Move();

	if (Data_mWiggle(this) > 0)
		mVelocity = unwiggle * mVelocity;

	if ((mUnknownE8.ToRead()) && (!(Data_mUnknown1C(this) < 0)))
	{
		if (mUnknownE8->IsA(THING_TYPE_BATTLE_ENGINE))
		{
			if (((mUnknownE8->GetPos() - mPos).MagnitudeSq() < 225.f) && (!((CUnit *)mUnknownE8.ToRead())->CanBeLocked()))
			{
				ReleaseTarget();
				mUnknown130 = 0;
			}
		}
		else if ((mUnknownE8->IsDying()) || (!((CUnit *)mUnknownE8.ToRead())->CanBeLocked()))
			ReleaseTarget();
	}

	if ((!(Data_mUnknown1C(this) < 0)) && (mData->mUnknown28 > 0) && (mUnknownE8.ToRead()))
	{
		float	age = EVENT_MANAGER.GetTime() - mStartTime;
		if ((Data_mSeekDelay(this) < age) && (Data_mSeekTerminationTime(this) > age) && ((!Data_mUnknown6C(this)) || (IsInWater())))
		{
			FMatrix	ori = mOrientation;
			ori.TransposeInPlace();
			FVector	pos;
			((CUnit *)mUnknownE8.ToRead())->GetTargetablePos(&pos);
			if (Data_mUnknown6C(this))
				pos.Z = MAP.GetWaterLevel();
			FVector	d = ori * (pos - mPos);
			static_cast<FSeekVector &>(d).Normalise();	// STAND-IN (ids): FSeekVector

			if ((!Data_mWeirdoSeek(this)) && ((float)cos(Data_mSeekAngle(this)) > d.Y) && (!mData->mUnknown6C))
				ReleaseTarget();
			else if ((!Data_mWeirdoSeek(this)) || ((float)cos(Data_mSeekAngle(this)) > d.Y))
			{
				float	yaw = d.Azimuth();
				float	pitch = (float)atan2(d.Z, d.MagnitudeXY());
				float	y = ((float)fabs(yaw) < mData->mUnknown28) ? yaw : ((yaw > 0) ? mData->mUnknown28 : -mData->mUnknown28);
				float	p = ((float)fabs(pitch) < mData->mUnknown28) ? pitch : ((pitch > 0) ? mData->mUnknown28 : -mData->mUnknown28);
				mOrientation = mOrientation * FMatrix(y, p, 0);
				d.Set(0, GetVelocity().Magnitude(), 0);
				mVelocity = mOrientation * d;
			}
		}
	}

	if ((Gravity() != 0) || (mUnknown128))
		mOrientation = FMatrix(mVelocity.Azimuth(), mVelocity.Elevation(), 0);

	if (mEffect.mEffect)
	{
		mEffect.SetPos(mPos);
		mEffect.SetOrientation(mOrientation);
	}
}

//******************************************************************************************
// 4000 launches: flak and exploding rounds go off at once, a beam traces its line from the muzzle (mBeamStart) and
// lands where it meets the ground or a tenth of a second on; 4001 lands a stopped round, 4002 spawns what it carries
// and 4003 looks for a target again.
void CRound::HandleEvent(CEvent *event)	// FUNCTION: 0x004d9910
{
	switch (event->GetEventNum())
	{
	case 4003:
		Unknown4dac90((CScheduledEvent *)event);
		break;

	case 4002:
		Unknown4db150();
		mUnknown120 = FALSE;
		StartDieProcess();
		break;

	case 4001:
		if ((mData->mBounceFactor == 0) && (!mUnknown120))
		{
			if (!mData->mBeam)
			{
				float	dt = EVENT_MANAGER.GetTime() - ((CScheduledEvent *)event)->GetTime();
				mPos -= mVelocity * 20.f * dt;
			}
			if (MAP.GetWaterLevel() - 0.001f < mPos.Z)
				Unknown4d9f30(2, 0, 0);
			else
				Unknown4d9f30(1, 0, 0);
			if (!mData->mUnknown60)
				StartDieProcess();
		}
		break;

	case 4000:
		if ((mData->mFlak) || (mData->mExplode))
			Unknown4d9f30(0, 0, 0);
		else if (mData->mBeam)
		{
			CLine			line(mBeamStart, mPos + mVelocity);
			CInitCSThing	init;

#line 531
			mCollisionSeekingThing = new(MT_CST) CCSRay;
			if ((mUnknownEC.ToRead()) && (((CUnit *)mUnknownEC.ToRead())->mUnknown24C))
				init.mIgnoreThing = mUnknownEC.ToRead();
			init.mForThing = this;
			init.mNotSeekCollisionWithBF |= THING_TYPE_AMMUNITION;
#line 541
			init.mShape = new(MEMTYPE_GENERAL_VOLUME) CLine(FVector(0, 0, 0), mVelocity);
			init.mMinCollision = ECL_APPROX_GEOMETRY_SHAPES;
			init.mMaxCollision = ECL_APPROX_GEOMETRY_SHAPES;
			init.mDesiredCollision = ECL_MESH;
			init.mCollisionResponse = ECR_STATIC;
			mCollisionSeekingThing->Init(&init);

			FVector	hit;
			if ((!(mFlags & TF_DYING)) && (MAP.Unknown490a40(&line, &hit, FALSE)))
			{
				mPos = hit;
				Unknown4d9f30(1, 0, 0);
				mFlags |= TF_DYING;
				EVENT_MANAGER.AddEvent(2000, this, EVENT_MANAGER.GetTime() + 0.1f);
				return;
			}
			else if (!(mFlags & TF_DYING))
			{
				mPos += mVelocity;
				Unknown4d9f30(0, 0, 0);
				mFlags |= TF_DYING;
				EVENT_MANAGER.AddEvent(2000, this, EVENT_MANAGER.GetTime() + 0.1f);
				return;
			}
		}
		StartDieProcess();
		break;

	default:
		SUPERTYPE::HandleEvent(event);
	}
}

//******************************************************************************************
void CRound::DeclareOnGround()	// FUNCTION: 0x004d9dd0
{
	if (mData->mBounceFactor != 0)
		SUPERTYPE::DeclareOnGround();
	else if (!mData->mUnknown60)
	{
		// land where the last step met the ground
		CLine	line(mOldPos, mPos);
		FVector	hit;
		if (MAP.Unknown490a40(&line, &hit, 0))
			mPos = hit;
		Unknown4d9f30(1, 0, 0);
		StartDieProcess();
	}
}

//******************************************************************************************
void CRound::DeclareInWater()	// FUNCTION: 0x004d9ef0
{
	Unknown4db630();
	SUPERTYPE::DeclareInWater();
	if ((!mData->mCanGoUnderWater) && (!mData->mUnknown6C))
	{
		Unknown4d9f30(2, 0, 0);
		StartDieProcess();
	}
}

//******************************************************************************************
// A file-local copy of maths.h's FMatrix * FMatrix for Unknown4d9f30's air product (a labelled stand-in; name ours):
// row 0's middle sum is written Y, X, Z. Written as maths.h writes it, the copy gives the same code as the operator.
static inline FMatrix MulMM(const FMatrix &a, const FMatrix &m)
{
	return FMatrix(
				   FVector(a.Row[0].X * m.Row[0].X + a.Row[0].Y * m.Row[1].X + a.Row[0].Z * m.Row[2].X,
						   a.Row[0].Y * m.Row[1].Y + a.Row[0].X * m.Row[0].Y + a.Row[0].Z * m.Row[2].Y,
						   a.Row[0].X * m.Row[0].Z + a.Row[0].Y * m.Row[1].Z + a.Row[0].Z * m.Row[2].Z),
				   FVector(a.Row[1].X * m.Row[0].X + a.Row[1].Y * m.Row[1].X + a.Row[1].Z * m.Row[2].X,
						   a.Row[1].X * m.Row[0].Y + a.Row[1].Y * m.Row[1].Y + a.Row[1].Z * m.Row[2].Y,
						   a.Row[1].X * m.Row[0].Z + a.Row[1].Y * m.Row[1].Z + a.Row[1].Z * m.Row[2].Z),
				   FVector(a.Row[2].X * m.Row[0].X + a.Row[2].Y * m.Row[1].X + a.Row[2].Z * m.Row[2].X,
						   a.Row[2].X * m.Row[0].Y + a.Row[2].Y * m.Row[1].Y + a.Row[2].Z * m.Row[2].Y,
						   a.Row[2].X * m.Row[0].Z + a.Row[2].Y * m.Row[1].Z + a.Row[2].Z * m.Row[2].Z));
}

//******************************************************************************************
// The round goes off: in the air (0), on the ground (1), in water (2) or against a thing (3). An oriented explosion
// faces away from what it hit; ground damages the landscape; a jumping round schedules its next jump (4002); a beam
// starts its effect at the muzzle. C2 lays both switches' bodies out by case value whatever their source order: the
// first thing, ground, then air (004da09e, 004da2c6, 004da343); the second ground (falling through), water and thing,
// then air (004da4be, 004da502, 004da6b9).
// The name is tested before the lookup (004d9faa): with the guard VC6 threads the null-name path past the
// init.mBehaviour store (still NULL from the constructor) and the mOriented test, and one zero register (esi) lives on
// after the lookup loop, as in retail.
// The inline budget gives retail's call list with the first switch written thing, ground, air (air written first gave
// 106/300): the air case's matrix then leaves its vector constructor iterator (004da343) and its product's FVector
// constructors out of line. The budget simulation (the inline-budget simulator) then needs six to thirteen more inline sites after
// that matrix, at an own size of 1315 to 1342, so that the thing arms' Elevation calls keep Magnitude out of line
// (004da09e, 004da111) and the second link's SetPos inlines while its SetOrientation is called (004da9f1). Those sites
// are the six GetPos() reads after the first switch and the link's Reset() (own 1340, with the clamp's two tests and
// the last link's calls made on mEffect). Reset() rather than a store to mEffect.mEffect is also why mData is loaded
// again for the descriptor after the ZERO_FVECTOR copy (004da8f8): the store through the inline's this may alias it.
// The thing case's matrix is a named local: VC6 ranks a product's terms by keys from its operands' ids, and the
// unnamed temporary gave two of that product's row sums the other order (004da199, 004da203).
// STAND-IN (spelling, no code): the air case multiplies through MulMM above, a file-local copy of
// maths.h's FMatrix * FMatrix whose row 0 middle sum is written Y, X, Z. VC6 ranks a sum's terms by keys from its
// operands' ids and keeps the written order on ties: retail adds the Z, X and Y terms (004da43d..004da455), ours with
// maths.h's body Z, Y, X. The matrix's column-Y element symbols are consecutive and share their IND bases with columns
// X and Z, so no shift of temporary or symbol numbers flips that sum alone (rank model of the IR after the second rank
// pass); with maths.h's body the function is 4 bytes from retail.
void CRound::Unknown4d9f30(SINT mode, CCollisionReport *report, CThing *thing)	// FUNCTION: 0x004d9f30
{
	CExplosionInitThing	init;
	init.mOriginator = mUnknownEC.ToRead();
	if ((init.mOriginator) && (init.mOriginator->IsA(THING_TYPE_BATTLE_ENGINE)))
		init.mImportant = TRUE;

	if (mData->mUnknown08)
		init.mBehaviour = UPhysicsManager::GetExplosion(mData->mUnknown08);
	if ((init.mBehaviour) && (init.mBehaviour->mOriented))
	{
		switch (mode)
		{
		case kCollideThing:
			if ((report) && (report->mNumContacts >= 1))
			{
				FVector	n = report->mNormals[0];
				init.mYaw = n.Azimuth();
				init.mPitch = n.Elevation() + PI / 2;
				init.mRoll = 0;
			}
			else if (thing)
			{
				FVector	n = mPos - thing->GetPos();
				init.mYaw = n.Azimuth();
				init.mPitch = n.Elevation() + PI / 2;
				init.mRoll = 0;
			}
			else
			{
				init.mOrientationType = DIRECTION_COSINE_MATRIX;
				FMatrix	m(0, -PI / 2, 0);
				init.mOrientation = mOrientation * m;
			}
			break;

		case kCollideGround:
			{
				FVector	n = MAP.Normal(mPos);
				init.mYaw = n.Azimuth();
				init.mPitch = n.Elevation() + PI / 2;
				init.mRoll = 0;
			}
			break;

		case kCollideNothing:
			init.mOrientationType = DIRECTION_COSINE_MATRIX;
			init.mOrientation = MulMM(mOrientation, FMatrix(0, PI / 2, 0));
			break;
		}
	}

	switch (mode)
	{
	case kCollideGround:
		{
			SINT	size = SINT(mData->mUnknown1C) + 2;
			if (size < 0)
				size = 0;
			if (size > 6)
				size = 6;
			ENGINE.AddDamage(GetPos().X, GetPos().Y, size);
		}
		// and on, as in water or against a thing

	case kCollideWater:
	case kCollideThing:
		if (init.mBehaviour)
		{
			CExplosion	*explosion = UPhysicsManager::SpawnExplosion(mData->mUnknown08);
			if (explosion)
			{
				FVector	vel = GetVelocity();
				vel.Normalise();
				init.mPos = GetPos() - vel * 0.1f;
				init.mColType = (ECollisionType)mode;
				if (mUnknownEC.ToRead())
					init.mAllegiance = ((CUnit *)mUnknownEC.ToRead())->GetAllegiance();
				else
					init.mAllegiance = kNeutralAllegiance;
				explosion->Init(&init);
			}
		}
		if ((mData->mJumps > mJumpsPerformed) && (!mUnknown120))
		{
			EVENT_MANAGER.AddEvent(4002, this, EVENT_MANAGER.GetTime() + mData->mJumpDelay);
			mUnknown120 = TRUE;
		}
		break;

	case kCollideNothing:
		if (((mData->mFlak) || (mData->mExplode)) && (init.mBehaviour))
		{
			CExplosion	*explosion = UPhysicsManager::SpawnExplosion(mData->mUnknown08);
			if (explosion)
			{
				FVector	vel = GetVelocity();
				vel.Normalise();
				init.mPos = GetPos() - vel * 0.1f;
				init.mColType = kCollideNothing;
				if (mUnknownEC.ToRead())
					init.mAllegiance = ((CUnit *)mUnknownEC.ToRead())->GetAllegiance();
				else
					init.mAllegiance = kNeutralAllegiance;
				explosion->Init(&init);
			}
		}
		if ((mData->mJumps > mJumpsPerformed) && (!mUnknown120))
		{
			EVENT_MANAGER.AddEvent(4002, this, EVENT_MANAGER.GetTime() + mData->mJumpDelay);
			mUnknown120 = TRUE;
		}
		break;
	}

	if ((mData->mBeam) && (mData->mParticleA) && (!mEffect.mEffect))
	{
		mEffect.Reset();
		PARTICLE_MANAGER.AddParticle(mData->mParticleA, &mEffect);
		CParticleEffectLink	*link = &mEffect;
		link->SetPos(mBeamStart);
		link->SetOrientation(mOrientation);
		if (mData->mStreamLaser)
		{
			if (link->mEffect)
				link->mEffect->mUnknown90 = mBeamStart;
		}
		else if (link->mEffect)
			link->mEffect->mUnknown90 = GetPos();
	}

	if (!mData->mUnknown60)
		mVelocity = FVector(0, 0, 0);

	if ((mEffect.mEffect) && (!mData->mBeam))
	{
		mEffect.SetPos(GetPos());
		mEffect.SetOrientation(mOrientation);
	}
}

//******************************************************************************************
void CRound::SetTarget(CThing *target, BOOL release)	// FUNCTION: 0x004daab0
{
	if (!target)
		return;
	if ((!mData->mUnknown48) && (!(mData->mUnknown1C < 0)))
		return;

	if (release)
		ReleaseTarget();
	mUnknownE8 = target;
	if (target->IsA(THING_TYPE_BATTLE_ENGINE))
		WORLD.mSet110.Add(this);
}

//******************************************************************************************
void CRound::ReleaseTarget()	// FUNCTION: 0x004dab50
{
	if ((mUnknownEC.ToRead()) && (mUnknownEC->IsA(THING_TYPE_BATTLE_ENGINE)))
		((CBattleEngine *)mUnknownEC.ToRead())->LockHit(mUnknownE8.ToRead());

	if ((mUnknownE8.ToRead()) && (mUnknownE8->IsA(THING_TYPE_BATTLE_ENGINE)))
		WORLD.mSet110.Remove(this);
	mUnknownE8 = NULL;
}

//******************************************************************************************
// A live unit other than the target, within the round's reach but not on top of it.
CThing *CRound::FindTargetNear()	// FUNCTION: 0x004daba0
{
	for (CMapWhoEntry *entry = MAP_WHO.GetFirstEntryWithinRadius(mPos, mData->mUnknown90); entry; entry = MAP_WHO.GetNextEntryWithinRadius())
	{
		CThing	*thing = entry->GetThing();
		if ((thing) && (thing != mUnknownE8.ToRead()) && (thing->IsA(THING_TYPE_UNIT)) && (!thing->IsDying()))
		{
			FVector	centre;
			thing->GetCentrePos(centre);
			float	distsq = (centre - mPos).MagnitudeSq();
			float	reach = mData->mUnknown90;
			if ((distsq < reach * reach) && (distsq > 0.25f))
				return thing;
		}
	}
	return NULL;
}

//******************************************************************************************
// A seeking round without a target takes the unit its launcher may target that lies nearest its heading within the
// seek cone (outside it, for the weirdo seekers), then looks again in a quarter to half a second.
// Retail tests the battle-engine flag twice (004dad25..004dad39), the (a && b) || !a form. GetAllegiance() spends the
// inline budget that leaves the list walk's First and Next out of line (00406d20, 00406d30). One SetTarget (the first, here;
// the second matches as well) reads the unit through a const reference: retail's x87 operand order in the inlined
// inv * vector and Normalise (README, how a value is read); a plain local, brackets or no reference leave 6 instructions off.
void CRound::Unknown4dac90(CScheduledEvent *reuse)	// FUNCTION: 0x004dac90
{
	if (mData->mUnknown48 != 1)
		return;

	if ((!mUnknownE8.ToRead()) && (mUnknownEC.ToRead()))
	{
		FMatrix	inv = mOrientation;
		inv.TransposeInPlace();
		float	best = -2.f;
		for (CUnit *u = WORLD.mUnitNB.First(); u; u = WORLD.mUnitNB.Next())
		{
			if (((u->IsA(THING_TYPE_BATTLE_ENGINE)) && (mUnknown130)) || (!u->IsA(THING_TYPE_BATTLE_ENGINE)))
			{
				FVector	d = inv * (u->GetPos() - mPos);
				d.Normalise();
				if (mData->mWeirdoSeek)
				{
					if ((d.Y > best) && ((float)cos(mData->mSeekAngle) > d.Y) && (((CUnit *)mUnknownEC.ToRead())->IsTargetAlligence(u->GetAllegiance())))
					{
						CUnit * const	&target = u;
						SetTarget(target, TRUE);
						best = d.Y;
					}
				}
				else
				{
					if ((d.Y > best) && ((float)cos(mData->mSeekAngle) < d.Y) && (((CUnit *)mUnknownEC.ToRead())->IsTargetAlligence(u->GetAllegiance())))
					{
						SetTarget(u, TRUE);
						best = d.Y;
					}
				}
			}
		}
	}

	if (mUnknownE8.ToRead())
		mDest = mUnknownE8->GetPos();
	else
		mDest = FVector(-1.f, -1.f, -1.f);

	EVENT_MANAGER.AddEvent(4003, this, (GAME.Random() % 65536) * (0.25f / 65536) + EVENT_MANAGER.GetTime() + 0.25f, START_OF_FRAME, NULL, reuse);
}

//******************************************************************************************
float CRound::GetExplosionUnknown34()	// FUNCTION: 0x004daff0
{
	CExplosionData	*explosion = UPhysicsManager::GetExplosion(mData->mUnknown08);
	if (explosion)
		return explosion->mRadius;
	return 0;
}

//******************************************************************************************
float CRound::GetExplosionRadius()	// FUNCTION: 0x004db090
{
	CExplosionData	*explosion = UPhysicsManager::GetExplosion(mData->mUnknown08);
	if (explosion)
		return explosion->mDamage;
	return 0;
}

//******************************************************************************************
BOOL CRound::StartDieProcess()	// FUNCTION: 0x004db130
{
	if (!mUnknown120)
		return SUPERTYPE::StartDieProcess();
	return FALSE;
}

//******************************************************************************************
// A jumping round spawns the next: at the unit it finds within its jump range, or at a random point on the ground
// there, launched from where it is (its height clamped to the ground's less one) with one more jump performed. The
// speed is the Y of a vector the launch orientation turns (retail keeps it in that vector's slot, 004db301); the
// lifespan jitter compares the lifespan times the speed with the squared distance, as retail does (004db56d).
// Binding the distance temporary by reference preserves retail's X, Y, Z square-sum order (004db54f..004db56b).
// The old value local emitted Y, Z, X: native x87 fragment checks found branch differences at explicitly selected
// 24-bit precision; live-game precision and reachability of those authored inputs remain unmeasured. This source
// form reproduces the original arithmetic, not a uniquely recovered spelling. The flak inaccuracy is bracketed so
// it loads first as retail's does (004db59d). The new round's set-up is a block of its own: with init scoped inside
// it, VC6 no longer orders the load of round->mData after init's constructor stores (004db27c).
void CRound::Unknown4db150()	// FUNCTION: 0x004db150
{
	CThing	*target = FindTargetNear();
	FVector	pos;
	if (!target)
	{
		pos.X = (GAME.FloatRandom() - 0.5f) * mData->mUnknown90 + mPos.X;
		pos.Y = (GAME.FloatRandom() - 0.5f) * mData->mUnknown90 + mPos.Y;
		pos.Z = MAP.Collide(pos) + 0.5f;
	}
	else
		((CUnit *)target)->GetTargetablePos(&pos);

	CRound	*round = UPhysicsManager::SpawnRound(mData);
	if (round)
	{
		CRoundInitThing	init;
		init.mJumpsPerformed = mJumpsPerformed + 1;
		FVector	vel(0, 0, 0);
		if (round->mData->mBeam)
			vel.Y = (pos - mPos).Magnitude();
		else
			vel.Y = round->mData->mSpeed / 20.0f;

		init.mPos = mPos;
		float	ground = MAP.Collide(mPos) - 1.f;
		if (init.mPos.Z > ground)
			init.mPos.Z = ground;
		init.mDest = pos;
		FVector	dir = pos - init.mPos;
		init.mOrientation = FMatrix(dir.Azimuth(), dir.Elevation(), 0);
		init.mVelocity = init.mOrientation * vel;
		init.mOrientationType = DIRECTION_COSINE_MATRIX;
		init.mRoundData = mData;
		init.mLifeSpan = mData->mLifeSpan;
		init.mInitialDelay = mData->mJumpDelay;
		round->mUnknownEC = mUnknownEC.ToRead();
		round->SetTarget(target, TRUE);

		const FVector	&delta = pos - init.mPos;
		float	distanceSq = delta.MagnitudeSq();
		if (init.mLifeSpan * vel.Y > distanceSq)
		{
			float	distance = sqrtf(distanceSq);
			float	percent = GAME.FloatRandom() * 100.0f;
			float	error = (round->mData->mFlakInaccuracy) * distance;
			init.mLifeSpan = (((percent / 100.0f) * error) * 2 - error + distance) / vel.Y;
		}
		round->Init(&init);
	}
}

//******************************************************************************************
float CRound::Gravity()	// FUNCTION: 0x004db600
{
	if ((mData->mUnknown6C) && (mUnknown12C))
		return 0;
	return mData->mGravity * 0.025f;
}

//******************************************************************************************
// Into the water: a round that swims runs along just under the surface at its own speed.
// Retail clears the link through this+0xe4 while Kill, AddParticle and SetPos share the pointer in edi: a second pointer
// local used once (the compiler forwards it) gives that store and retail's scratch-register order.
void CRound::Unknown4db630()	// FUNCTION: 0x004db630
{
	if (mUnknown12C)
		return;
	if (!mData->mUnknown6C)
		return;

	mUnknown12C = TRUE;
	mPos.Z = MAP.GetWaterLevel() - 0.05f;

	mVelocity.Normalise();
	mVelocity.Z = 0;
	mVelocity *= mData->mSpeed / GAME_FR;

	CParticleEffectLink	*link = &mEffect;
	link->Kill();
	if (mData->mParticleB)
	{
		CParticleEffectLink	*fresh = &mEffect;
		fresh->mEffect = NULL;
		PARTICLE_MANAGER.AddParticle(mData->mParticleB, link);
		link->SetPos(mPos);
		link->SetOrientation(mOrientation);
	}
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004db8c0 ?_GetClassID@CRound@@UAEHXZ

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004d8a50 ??_GCCollisionSeekingRound@@UAEPAXI@Z

// Out-of-line copies this object emits (the inline budget left the calls in CRound::HandleEvent and 004d9f30):
// FUNCTION: 0x004d8a70 ??1CCollisionSeekingRound@@UAE@XZ
// FUNCTION: 0x004d9d60 ?Initialise@CInitCSThing@@QAEXXZ
// FUNCTION: 0x004d9da0 ??_GCCSRay@@UAEPAXI@Z
// FUNCTION: 0x004d9dc0 ??1CCSRay@@UAE@XZ
// FUNCTION: 0x004daa20 ?GetExplosionIndex@UPhysicsManager@@SAHPAD@Z
