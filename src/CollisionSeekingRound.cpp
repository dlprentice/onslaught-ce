// CollisionSeekingRound.cpp: the projectile's persistent collision seeker.
// Reconstructed from the pristine retail bodies at 00425b50..00426051.

#include "common.h"
#include "CollisionSeekingRound.h"
#include "round.h"
#include "WorldPhysicsManager.h"
#include "Line.h"
#include "DebugLog.h"

//******************************************************************************************
void CCollisionSeekingRound::Init(CInitCSThing *init)	// FUNCTION: 0x00425b50
{
	if (init->mShape)
	{
		LOG.AddMessage("WARNING: Collision seeking round will define its own shape");
		delete init->mShape;
	}

	mRoundLength = init->mRoundLength;
#line 27
	init->mShape = new (MEMTYPE_UNKNOWN) CLine(FVector(0, 0, 0), FVector(0, 0, 0));
	CCSPersistentThing::Init(init);
	mRadius = init->mForThing->GetMaxVelocity() / GAME_FR + mRoundLength;
}

//******************************************************************************************
BOOL CCollisionSeekingRound::Unknown8(CCollisionSeekingThing *other)	// FUNCTION: 0x00425c60
{
	BOOL result = CCSPersistentThing::Unknown8(other);
	if (result)
	{
		CRound *round = (CRound *)mForThing;
		if (other->mForThing == round->mUnknownEC.ToRead())
			return FALSE;
		if ((round->mData->mBounceFactor == 0) && (round->mData->mGravity == 0) &&
			(round->mData->mUnknown28 <= 0) && (!round->mData->mUnknown6C))
		{
			if ((other->mForThing->IsA(THING_TYPE_STATIONARY)) || (other->mForThing->IsA(THING_TYPE_INFANTRY)))
			{
				FVector centre;
				other->mForThing->GetCentrePos(centre);
				float radius = other->mRadius;
				FVector start = mForThing->GetPos() - centre;
				FVector end = start + mForThing->GetVelocity();
				FVector delta = start - end;
				// Retail tests the unbounded trajectory line; it does not clamp this fraction.
				float fraction = (delta * start) / delta.MagnitudeSq();
				FVector closest = start + (end - start) * fraction;
				float distanceSquared = closest.MagnitudeSq();
				if (distanceSquared > radius * radius)
					return FALSE;
			}
		}
	}
	return result;
}

//******************************************************************************************
CGeneralVolume *CCollisionSeekingRound::Unknown7()	// FUNCTION: 0x00425e30
{
	CLine *line = (CLine *)mShape;
	FVector oldPos = mForThing->GetOldPos();
	FVector pos = mForThing->GetPos();
	if (mRoundLength > 0.001f)
	{
		FVector direction = pos - oldPos;
		direction.Normalise();
		float length = mRoundLength;
		*line = CLine(oldPos - pos, direction * length);
	}
	else
		*line = CLine(oldPos - pos, FVector(0, 0, 0));
	return line;
}

// FUNCTION: 0x00425a60 init:ZERO_FVECTOR
// FUNCTION: 0x00425a80 init:ID_FMATRIX
