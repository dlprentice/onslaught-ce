// GroundUnit.cpp: reconstructed from GroundUnit.obj (0047c640..0047cede); Stuart's release has no copy. A unit that
// moves on the ground: it drives with its own acceleration and friction, lights its thrusters in the band of heights
// its definition gives, keeps to its top speed and, if it hovers, bobs.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#define GROUNDUNIT_CPP
#include "unit.h"
#include "InitThing.h"
#include "RenderThing.h"
#include "map.h"
#include "game.h"
#include "eventmanager.h"
#include "ParticleManager.h"
#include "WorldPhysicsManager.h"
#include "Sphere.h"

#include <math.h>

// FUNCTION: 0x0047c640 init:ZERO_FVECTOR
// FUNCTION: 0x0047c660 init:ID_FMATRIX

//******************************************************************************************
void CGroundUnit::Init(CInitThing *init)	// FUNCTION: 0x0047c730
{
	init->mInitCST.mDoOBBForMeshCol = TRUE;
	SUPERTYPE::Init(init);
	mUnknown258 = FALSE;
	mMaxShields = mShields = ((CUnitInitThing *)init)->mStats->mGroundShields;
	mShieldRegeneration = ((CUnitInitThing *)init)->mStats->mGroundShieldRegeneration;

	// a thruster for each "Thruster" emitter the mesh has
	if (mUnitData->mParticles[3])
	{
		for (SINT i = 1; ; i++)
		{
			FVector	pos;
			FMatrix	ori;
			mRenderThing->GetRTEmitter("Thruster", i, pos, ori, TRUE, FALSE);
			if (pos == FVector(0, 0, 0))
				break;
#line 35
			mThrusters.Append(new(MEMTYPE_PARTICLE) CParticleEffectLink);
		}
	}

	mBobPhase = 0;
	mBob = 0;
	mOldBob = 0;
	if ((mUnitData) && (mUnitData->mUnknown10C))
		mBobPhase = GAME.FloatRandom() * PI_M2;
	mThrustersActive = FALSE;
}

//******************************************************************************************
BOOL CGroundUnit::IsOnGround()	// FUNCTION: 0x0047c8b0
{
	return	(EVENT_MANAGER.GetTime() - mLastTimeOnGround < 0.21f);
}

//******************************************************************************************
// Without a shape of its own it collides as a sphere a little inside its bounding radius.
void CGroundUnit::InitCollisionSeekingThing(CInitCSThing *init)	// FUNCTION: 0x0047c8e0
{
	if (!init->mShape)
	{
		float	radius = GetBoundingRadius();
#line 67
		init->mShape = new(MEMTYPE_COLLISION) CSphere(radius * 0.8f);
	}
	CThing::InitCollisionSeekingThing(init);
}

//******************************************************************************************
// STAND-IN (inline budget): the state accessors (GetFlags, GetUnitData) preserve the nested particle-position and
// thruster-iterator call boundaries; either one read as its member is 681 bytes off. The cached velocity is scaled
// before its X/Y components are copied back; the member Z is unchanged. The friction divisor is retail's reciprocal
// constant (0.86956525f is 1 / 1.15f).
void CGroundUnit::Move()	// FUNCTION: 0x0047c970
{
	float	multiplier = GetMoveMultiplier();
	SINT	steps = SINT(multiplier);
	float	gravity = 0;
	if (ObeyGravity())
		gravity = Gravity();

	FVector	accel(0, 0, 0);
	float	friction = Unknown73();
	if (((IsOnGround()) || (IsInWater()) || (!ObeyGravity())) && (Unknown76()))
	{
		friction = Unknown72();
		accel = mUnknown14C * 0.4f;
	}

	if ((!(GetFlags() & TF_DYING)) && (GetUnitData()->mParachute == 1))
	{
		float	height = mPos.Z - MAP.GetHeight(SINT(mPos.X), SINT(mPos.Y)) - COfGHeight();
		if ((height > -GetUnitData()->mMaxThrusterHeight) && (height < -GetUnitData()->mMinThrusterHeight))
		{
			friction /= 1.15f;
			mThrustersActive = TRUE;
			SINT	i = 1;
			ListIterator<CParticleEffectLink>	thrusters(&mThrusters);
			for (CParticleEffectLink *t = thrusters.First(); t; t = thrusters.Next())
			{
				if (!t->mEffect)
					PARTICLE_MANAGER.CreateEffect(GetUnitData()->mParticles[3], t, ZERO_FVECTOR, 0, 0);
				FVector	pos;
				FMatrix	ori;
				GetEmitter(26, i, pos, ori);
				t->SetPos(pos);
				t->SetOrientation(ori);
				i++;
			}
		}
		else if (mThrustersActive)
			DeactivateThrusters();
	}

	accel.Z += gravity;
	for (SINT step = 0; step < steps; step++)
	{
		mVelocity += accel;
		mVelocity *= friction;
	}

	// its top speed along the ground
	FVector	v = mVelocity;
	float	top = Unknown111() / 20.0f;
	float	speed = v.MagnitudeXY();
	if (speed > top)
	{
		v *= top / speed;
		mVelocity.X = v.X;
		mVelocity.Y = v.Y;
	}

	CUnit::Move();

	if ((GetUnitData()->mUnknown10C) || (mUnknown258))
	{
		mOldBob = mBob;
		if ((GetFlags() & TF_DYING))
		{
			mBob = mBobPhase + mBob;
			mBobPhase -= multiplier * 0.005f;
			return;
		}
		if ((IsDeploying()) && (mBobPhase < 0.1f))
		{
			mBobPhase = 0;
			return;
		}
		mBobPhase += multiplier * 0.1f;
		mBob = (float)sin(mBobPhase) * 0.05f;
		if (mBobPhase > PI_M2)
			mBobPhase -= PI_M2;
	}
}

//******************************************************************************************
BOOL CGroundUnit::StartDieProcess()	// FUNCTION: 0x0047ce80
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;
	mBobPhase = 0;
	return TRUE;
}

//******************************************************************************************
// Defined after Move, which still expands it (/Ob2 inlines a later function of the same file).
void CGroundUnit::DeactivateThrusters()	// FUNCTION: 0x0047cea0
{
	ListIterator<CParticleEffectLink>	thrusters(&mThrusters);
	for (CParticleEffectLink *t = thrusters.First(); t; t = thrusters.Next())
		t->Kill();
	mThrustersActive = FALSE;
}
