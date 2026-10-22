// Plane.cpp: reconstructed from Plane.obj (004d18e0..004d20cf); Stuart's release has no copy. A plane: a small air
// unit launched with its engines burning, that opens its wings to attack and crashes when it hits anything.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "AirGuide.h"
#include "PlaneAI.h"
#include "renderthing.h"
#include "rtmesh.h"
#include "game.h"
#include "eventmanager.h"
#include "WorldPhysicsManager.h"
#include "ParticleManager.h"

// FUNCTION: 0x004d18e0 init:ZERO_FVECTOR
// FUNCTION: 0x004d1900 init:ID_FMATRIX

// The position of a mesh emitter. Unlike AirUnit.cpp's, the orientation is a plain local: retail builds no rows
// through the vector constructor iterator here (Init 004d1b0b..004d1b23).
static inline FVector EmitterPos(CRenderThing *rt, char *name, SINT index)
{
	FVector	pos;
	FMatrix	ori;
	rt->GetRTEmitter(name, index, pos, ori, TRUE, FALSE);
	return pos;
}

//******************************************************************************************
void CPlane::Init(CInitThing *init)	// FUNCTION: 0x004d19d0
{
	init->mInitCST.mMaxCollision = ECL_APPROX_GEOMETRY_SHAPES;
	SUPERTYPE::Init(init);

#line 19
	mGuide = new(MEMTYPE_GUIDE) CAirGuide(this);
	mAI = new(MEMTYPE_AI) CPlaneAI(this, init);

	// launched planes burn for a moment first
	SINT	mode = mAI->mAIMode;
	if (mode == 2)
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("launch"), TRUE, TRUE);
		mWingState = 0;
		mLaunchTime = EVENT_MANAGER.GetTime() + 2.5f;
	}
	else
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("launch"), TRUE, TRUE);
		mWingState = 1;
		mLaunchTime = 0;
	}

	// a flame for each "Engine" emitter
	if (mUnitData->mParticles[8])
	{
		SINT	n = 1;
		while (EmitterPos(mRenderThing, "Engine", n) != FVector(0, 0, 0))
		{
#line 42
			CParticleEffectLink	*link = new(MEMTYPE_PARTICLE) CParticleEffectLink();
			mThrusters.Append(link);
			n++;
		}
	}

	mDeathRoll = (float(GAME.Random() % 65536) / 65536.0f > 0.5f) ? 0.8f : -0.8f;
}

// CPlaneAI's implicit destructor, emitted here with its vtable.
// FUNCTION: 0x004d1c10 ??_GCPlaneAI@@UAEPAXI@Z
// FUNCTION: 0x004d1c30 ??1CPlaneAI@@UAE@XZ

//******************************************************************************************
void CPlane::Move()	// FUNCTION: 0x004d1cd0
{
	if (mFlags & TF_DYING)
	{
		mUnknown12C.mRoll = 0.2f;
		float	roll = mDeathRoll + mCurrentOrientation.mRoll;
		if (roll > PI)
			roll -= PI * 2;
		else if (roll < PI)
			roll += PI * 2;
		mUnknown120.mRoll = roll;
		mUnknown120.mPitch = PI / 2;
	}
	else
	{
		if (mLaunchTime > EVENT_MANAGER.GetTime())
		{
			SINT								n = 1;
			ListIterator<CParticleEffectLink>	links(&mThrusters);
			for (CParticleEffectLink *link = links.First(); link; link = links.Next())
			{
				if (!link->mEffect)
					PARTICLE_MANAGER.CreateEffect(mUnitData->mParticles[8], link, ZERO_FVECTOR, 0, 0);
				FVector	pos;
				FMatrix	ori;
				GetEmitter(23, n, pos, ori);
				link->SetPos(pos);
				link->SetOrientation(GetOrientation());
				n++;
			}
			mThrustersActive = TRUE;
		}
		else if (mThrustersActive)
		{
			ListIterator<CParticleEffectLink>	links(&mThrusters);
			for (CParticleEffectLink *link = links.First(); link; link = links.Next())
				link->Kill();
			mThrustersActive = FALSE;
		}

		if ((mAI->mAIMode != 2) && (!mWingState))
			mWingState = 1;
	}

	SUPERTYPE::Move();

	float	speed = mVelocity.Magnitude();
	FVector	v;
	v = FVector(speed * mOrientation.Row[0].Y, speed * mOrientation.Row[1].Y, speed * mOrientation.Row[2].Y);
	SetVelocity(v);
}

//******************************************************************************************
// It crashes into anything but a friendly aircraft.
void CPlane::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x004d1f10
{
	if (!mUnitData->mUnknown11C)
	{
		if ((!other_thing->IsA(THING_TYPE_AIR_UNIT)) || (other_thing->IsA(THING_TYPE_BATTLE_ENGINE)) ||
			(((CUnit *)other_thing)->mAllegiance != mAllegiance))
		{
			if ((other_thing->IsA(THING_TYPE_UNIT)) || (other_thing->IsA(THING_TYPE_TREE | THING_TYPE_CAN_BE_WALKED_ON)))
			{
				if (other_thing->IsA(THING_TYPE_UNIT))
					((CUnit *)other_thing)->ConfirmedKill(this);
				SpawnExplosion();
				AddShutdownEvent();
			}
		}
	}
	SUPERTYPE::Hit(other_thing, report);
}

//******************************************************************************************
void CPlane::OpenWings()	// FUNCTION: 0x004d1f90
{
	if (mWingState == 1)
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("wingopen"), TRUE, FALSE);
		mWingState = 2;
	}
}

//******************************************************************************************
void CPlane::CloseWings()	// FUNCTION: 0x004d1fd0
{
	if (mWingState == 4)
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("wingclose"), TRUE, FALSE);
		mWingState = 3;
	}
}

//******************************************************************************************
BOOL CPlane::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x004d2010
{
	if (GetRenderAnimation() != AM_INVALID)
	{
		switch (mWingState)
		{
		case 2:
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("attack"), TRUE, TRUE);
			mWingState = 4;
			break;
		case 3:
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("launch"), TRUE, TRUE);
			mWingState = 1;
			break;
		}
	}
	return FALSE;
}

//******************************************************************************************
// Without "Retreat" it dies when it touches the ground.
void CPlane::DeclareOnGround()	// FUNCTION: 0x004d20a0
{
	SUPERTYPE::DeclareOnGround();
	if (!mUnitData->mUnknown11C)
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
}

//******************************************************************************************
// The plane's answers (as CCarver's and CGroundAttackAircraft's, Carver.cpp and GroundAttackAircraft.cpp) that no code
// above defined. Each compiles to a body another function already has, so the linker folds it into that one (/OPT:ICF):
// the retail table's slot points at the survivor, whose address each annotation names (the class header notes the
// slots' survivors too).

// FUNCTION: 0x00422750
void CPlane::Stop()
{
	mGuide->Stop();
}

// FUNCTION: 0x0047bf60
void CPlane::DeclareInWater()
{
	SUPERTYPE::DeclareInWater();
	if ((!mUnitData->mUnknown11C) && (!mUnitData->mUnknown124))
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
}
