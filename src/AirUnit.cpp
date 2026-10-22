// AirUnit.cpp: reconstructed from AirUnit.obj (004029e0..00403c3f); Stuart's release has no copy. The flying units'
// shared behaviour: their trail and engine effects, flight, falling when they die, and CBigAirUnit's and
// CSmallAirUnit's Hit. The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "world.h"
#include "map.h"
#include "mesh.h"
#include "RenderThing.h"
#include "ParticleManager.h"
#include "WorldPhysicsManager.h"
#include "InitThing.h"
#include "round.h"
#include "game.h"
#include "DebugLog.h"
#include "Guide.h"

// FUNCTION: 0x004029e0 init:ZERO_FVECTOR
// FUNCTION: 0x00402a00 init:ID_FMATRIX

// FMatrix's rows are built through the vector constructor iterator with FVector's constructor (GetRTEmitter's
// orientation, Init 00402b75).
// FUNCTION: 0x00402d20 ??0FVector@@QAE@XZ

// The position of a mesh emitter. STAND-IN: its orientation is a one-element array. Retail builds it through the vector
// constructor iterator (Init 00402b75, 00402c3d), which CRenderThing's own position-only form does not; a plain
// `FMatrix ori;` local makes Init 22 shape / 34 instruction / 336 byte differences, and ZERO_FVECTOR for the
// comparisons changes the relocations (retail compares with a temporary FVector(0, 0, 0)).
static inline FVector EmitterPos(CRenderThing *rt, char *name, SINT index)
{
	FVector	pos;
	FMatrix	ori[1];
	rt->GetRTEmitter(name, index, pos, ori[0], TRUE, FALSE);
	return pos;
}

//******************************************************************************************
void CAirUnit::Init(CInitThing *init)	// FUNCTION: 0x00402ad0
{
	SUPERTYPE::Init(init);

	float	turn = ((CUnitInitThing *)init)->mStats->mTurnRate;
	mUnknown12C = CEulerAngles(turn, turn, turn);
	mShields = ((CUnitInitThing *)init)->mStats->mAirShields;
	mMaxShields = mShields;
	mShieldRegeneration = ((CUnitInitThing *)init)->mStats->mAirShieldRegeneration;

	// a trail for each "Trail" emitter
	CParticleDescriptor	*trail = mUnitData->mParticles[2];
	if (trail)
	{
		SINT	n = 1;
		while (EmitterPos(mRenderThing, "Trail", n) != FVector(0, 0, 0))
		{
#line 42
			CParticleEffectLink	*link = new(MEMTYPE_PARTICLE) CParticleEffectLink();
			mTrails.Append(link);
			n++;
		}
	}

	// and a flame for each "Engine" emitter
	CParticleDescriptor	*engine = mUnitData->mParticles[5];
	if (engine)
	{
		SINT	n = 1;
		while (EmitterPos(mRenderThing, "Engine", n) != FVector(0, 0, 0))
		{
#line 54
			CParticleEffectLink	*link = new(MEMTYPE_PARTICLE) CParticleEffectLink();
			mEngines.Append(link);
			n++;
		}
	}

	mUnknown258 = 0;
	WORLD.mSet50.Add(this);
}

//******************************************************************************************
void CAirUnit::Shutdown()	// FUNCTION: 0x00402d30
{
	WORLD.mSet50.Remove(this);
	mEffect.Stop(FALSE);

	CParticleEffectLink	*link;
	while ((link = mTrails.First()))
	{
		link->Stop(FALSE);
		mTrails.Remove(link);
		delete link;
	}
	while ((link = mEngines.First()))
	{
		link->Stop(FALSE);
		mEngines.Remove(link);
		delete link;
	}

	SUPERTYPE::Shutdown();
}

//******************************************************************************************
// Whether any corner of the unit's box (shrunk to 80%) is below the ground or the sea by its centre of gravity.
BOOL CAirUnit::IsBoxBelowSurface()	// FUNCTION: 0x00402dd0
{
	CBoundingBox	*box = GetBoundingBox();
	if (!box)
		return FALSE;

	FVector	axes = box->mAxes * 0.8f;
	SINT	corner = 0;
	for (SINT z = 0; z < 2; z++)
	{
		for (SINT x = 0; x < 2; x++)
		{
			for (SINT y = 0; y < 2; y++)
			{
				FVector	p = box->mOrigin;
				if (!z) p.Z += axes.Z; else p.Z -= axes.Z;
				if (!x) p.X += axes.X; else p.X -= axes.X;
				if (!y) p.Y += axes.Y; else p.Y -= axes.Y;
				p = mOrientation * p;
				p += mPos;

				float	h = MAP.Collide(p) - COfGHeight();
				if (p.Z > h)
					return TRUE;
				h = MAP.GetWaterLevel() - COfGHeight();
				if (p.Z > h)
					return TRUE;
				corner++;
			}
		}
	}
	return FALSE;
}

//******************************************************************************************
void CAirUnit::Move()	// FUNCTION: 0x00402fa0
{
	// a dying aircraft tumbles
	if (mFlags & TF_DYING)
	{
		float	turn = mUnitData->mTurnRate / 3.0f;
		mUnknown12C = CEulerAngles(turn, turn, turn);
	}

	mVelocity += mUnknown14C;
	mVelocity.Z += Gravity();

	// big units sink through water slowly
	if ((mUnitData->mUnknown124) && (mPos.Z + GetRadius() * 0.2f > MAP.GetWaterLevel()))
	{
		mVelocity *= 0.95f;
		if (mFlags & TF_DYING)
			Unknown96();
	}
	else
		mVelocity *= Unknown73();

	float	speed = mVelocity.Magnitude();
	if (speed > 0)
	{
		float	max = Unknown111() / 20.0f;
		if (speed > max)
			mVelocity *= max / speed;
	}

	SUPERTYPE::Move();

	if (mUnknown170 + 0.1f < mUnknown174)
		mUnknown170 += 0.1f;
	else if (mUnknown170 - 0.1f > mUnknown174)
		mUnknown170 -= 0.1f;
	else
		mUnknown170 = mUnknown174;
	mUnknown170 *= 1.0f - GetStealth() / 100.0f;

	// the trails
	SINT				n = 1;
	CParticleEffectLink	*link;
	FVector				pos;
	FMatrix				ori;
	for (link = mTrails.First(); link; link = mTrails.Next())
	{
		if ((mUnknown170 > 0) && (!link->mEffect))
			PARTICLE_MANAGER.CreateEffect(mUnitData->mParticles[2], link, ZERO_FVECTOR, 0, 0);

		GetEmitter(24, n, pos, ori);
		link->SetPos(pos);
		link->SetOrientation(mOrientation);
		if ((link->mEffect) && (link->mEffect->mParticle))
			link->mEffect->mParticle->mRadius = mUnknown170;
		n++;
	}

	// the engines, while it moves
	n = 1;
	for (link = mEngines.First(); link; link = mEngines.Next())
	{
		if (Unknown117())
		{
			if (!link->mEffect)
				PARTICLE_MANAGER.CreateEffect(mUnitData->mParticles[5], link, ZERO_FVECTOR, 0, 0);

			GetEmitter(23, n, pos, ori);
			link->SetPos(pos);
			link->SetOrientation(ori);
		}
		else
			link->Stop(FALSE);
		n++;
	}

	// dying: explode once it has taken enough damage, and trail smoke meanwhile
	if (mLife < 0)
	{
		if ((-mUnitData->mUnknownC0 > mLife) && (!mUnitData->mUnknown11C))
		{
			SpawnExplosion();
			AddShutdownEvent();
		}

		if ((mUnknown258) && (!(mFlags & TF_DECLARED_SHUTDOWN)))
		{
			FVector	smoke_pos;
			FMatrix	smoke_ori;
			GetEmitter(25, mUnknown258, smoke_pos, smoke_ori);
			mEffect.SetPos(smoke_pos);
		}
	}
}

//******************************************************************************************
BOOL CAirUnit::StartDieProcess()	// FUNCTION: 0x00403690
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;

	ReleaseChildren();

	CParticleEffectLink	*link;
	while ((link = mTrails.First()))
	{
		mTrails.Remove(link);
		link->Stop(FALSE);
		delete link;
	}
	while ((link = mEngines.First()))
	{
		mEngines.Remove(link);
		link->Stop(FALSE);
		delete link;
	}

	return TRUE;
}

//******************************************************************************************
void CAirUnit::DeclareOnGround()	// FUNCTION: 0x00403730
{
	SUPERTYPE::DeclareOnGround();

	// a dying aircraft explodes where it lands
	if ((mFlags & TF_DYING) && (!mUnitData->mUnknown11C))
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
}

//******************************************************************************************
void CAirUnit::DeclareInWater()	// FUNCTION: 0x00403760
{
	SUPERTYPE::DeclareInWater();

	if ((mFlags & TF_DYING) && (!mUnitData->mUnknown11C) && (!mUnitData->mUnknown124))
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
}

//******************************************************************************************
void CAirUnit::Damage(float amount, CThing *inByThis, BOOL inDamageShields, int mesh_part_no)	// FUNCTION: 0x004037a0
{
	SUPERTYPE::Damage(amount, inByThis, inDamageShields, mesh_part_no);

	// once it is dying, start its smoke at one of its "smoke" emitters
	if ((mLife < 0) && (!(mFlags & TF_DECLARED_SHUTDOWN)) && (!mUnknown258))
	{
		mUnknown258 = 0;
		if (mUnitData->mParticles[4])
		{
			// the player's shots make it an important effect
			BOOL	important = FALSE;
			if ((inByThis) && (inByThis->IsA(THING_TYPE_AMMUNITION)) && (((CRound *)inByThis)->mUnknownEC.ToRead()) &&
				(((CRound *)inByThis)->mUnknownEC.ToRead()->IsA(THING_TYPE_BATTLE_ENGINE)))
				important = TRUE;

			FVector	pos;
			FMatrix	ori;
			SINT	n = 1;
			GetEmitter(25, n, pos, ori);
			while (pos != FVector(0, 0, 0))
				GetEmitter(25, ++n, pos, ori);

			n--;
			if (n)
			{
				if (n == 1)
					mUnknown258 = n;
				else
					mUnknown258 = GAME.Random() % (n - 1) + 1;

				GetEmitter(25, mUnknown258, pos, ori);
				if (pos != FVector(0, 0, 0))
				{
					mEffect.mEffect = NULL;
					PARTICLE_MANAGER.CreateEffect(mUnitData->mParticles[4], &mEffect, ZERO_FVECTOR, 0, important);
					mEffect.SetPos(pos);
				}
			}
		}
	}
}

//******************************************************************************************
BOOL CAirUnit::Unknown117()	// FUNCTION: 0x00403a50
{
	return (mOldPos != mPos) && (!(mFlags & TF_DYING));
}

//******************************************************************************************
void CAirUnit::GoToPoint(FVector point, BOOL inOverride)	// FUNCTION: 0x00403a90
{
	if (mGuide)
	{
		// no lower than the unit's clearance above the ground or the sea
		// Written out: retail copies the water level into a local through the FPU (00403adf), which map.h's
		// GetWaterLevel reproduces.
		float	h = MAP.GetHeight(SINT(point.X), SINT(point.Y));
		float	water = MAP.GetWaterLevel();
		if (h > water)
			h = water;
		h -= mUnitData->mMinAltitude;
		if (point.Z > h)
			point.Z = h;
		mGuide->GoToPoint(point, inOverride);
	}
}

//******************************************************************************************
float CAirUnit::Gravity()	// FUNCTION: 0x00403b60
{
	// a dying aircraft falls faster the smaller it is
	if (mFlags & TF_DYING)
	{
		float	r = GetRadius();
		if (r > 39.0f)
			r = 39.0f;
		return (40.0f - r) * 0.0004f;
	}
	return 0.0f;
}

//******************************************************************************************
void CSmallAirUnit::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x00403ba0
{
	// a dying aircraft explodes on what it flies into
	if ((!mUnitData->mUnknown11C) && (mFlags & TF_DYING) &&
		((other_thing->IsA(THING_TYPE_UNIT)) || (other_thing->IsA(THING_TYPE_TREE | THING_TYPE_CAN_BE_WALKED_ON))))
	{
		SpawnExplosion();
		AddShutdownEvent();
	}

	SUPERTYPE::Hit(other_thing, report);
}

//******************************************************************************************
void CBigAirUnit::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x00403bf0
{
	if ((!mUnitData->mUnknown11C) && (mFlags & TF_DYING) && (other_thing->IsA(THING_TYPE_CAN_BE_WALKED_ON)))
	{
		SpawnExplosion();
		AddShutdownEvent();
	}

	SUPERTYPE::Hit(other_thing, report);
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00403650 ?SetCurrentPos@CFoR@@QAEXABVFVector@@@Z
