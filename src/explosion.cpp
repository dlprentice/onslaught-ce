// explosion.cpp: CExplosion, a blast that grows to its radius and damages what it touches (reconstructed; Stuart's
// file is not in his release). The object also holds the FVector and FMatrix readers the mesh files use.

#include	"Common.h"
#define EXPLOSION_CPP	// before maths.h (FMatrix::Read) and explosion.h (its fields)
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#include	"InitThing.h"
#include	"explosion.h"
#include	"WorldPhysicsManager.h"
#include	"ParticleManager.h"
#include	"SoundManager.h"
#include	"eventmanager.h"
#include	"engine.h"
#include	"world.h"
#include	"map.h"
#include	"unit.h"
#include	"GeneralVolume.h"
#include	"DXMemBuffer.h"

// The per-file maths constants from maths.h come first:
// FUNCTION: 0x0044b840 init:ZERO_FVECTOR
// FUNCTION: 0x0044b860 init:ID_FMATRIX

#define EXPLOSION_EVENT_GROW	3000

//*********************************************************************************
// Matched. The forms that look odd are what VC6 needs: the plain-effect arm is written first (its
// SetPos then expands the first CFoR::SetCurrentPos and calls the second, as retail does; the volumetric arm's sites
// follow it in the inliner's source order); GetRenderThing() is called twice (no `rt` local, a named variable took a
// different register); each sound is tested inside its case.
// STAND-INS (shape / instructions / bytes when written plainly): the splash offset's three products are named floats
// with bracketed initialisers (the DiveBomberGuide form: each adds an IL node, which moves the two mBehaviour loads into
// retail's latency slots; without the brackets 30/30/82, and `pos += FVector(d * ..., ...)` directly is 30/30/82 too), and
// the splash block takes its own orientation reference `o` although `ori` names the same matrix (reusing `ori`:
// 65/203/888). The `FVector pos(0, 0, 0)` that once gave pos.X an early symbol id is not needed any more: a plain
// `FVector pos;` is byte-identical.
void	CExplosion::Init(CInitThing *init)	// FUNCTION: 0x0044b930
{
	CExplosionInitThing	*i = (CExplosionInitThing *)init;

	mOriginator.SetReader(i->mOriginator);

	if (MAP.GetWaterLevel() + 1.5f > i->mPos.Z)
	{
		i->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_EXPLOSION;
		i->mInitCST.mIgnoreThing = i->mAttachedTo;
		mBehaviour = i->mBehaviour;

		if ((i->mAttachedTo) && (i->mUseAttachedRadius))
			mRadius = i->mAttachedTo->GetRadius();
		else
			mRadius = mBehaviour->mRadius;

		mAllegiance = i->mAllegiance;

		if ((mRadius <= 0.f) || (mBehaviour->mDamage <= 0.0015))
		{
			mFlags &= ~TF_IN_MAP_WHO;
			i->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_EVERYTHING;
		}

		i->mInitCST.mMaxCollision = ECL_APPROX_GEOMETRY_SHAPES;
		i->mInitCST.mCollisionResponse = ECR_PASSIVE;
		i->mInitCST.mStartCollideOnNextFrame = FALSE;
		i->mInitCST.mDoOBBForMeshCol = TRUE;

		mColType = i->mColType;

		if (mRadius <= 3.f)
			mCurrentRadius = mRadius;
		else
			mCurrentRadius = 0;

		SUPERTYPE::Init(init);

		CParticleDescriptor	*pd = NULL;
		switch (mColType)
		{
		case kCollideNothing:	pd = mBehaviour->mParticleA;	break;
		case kCollideGround:	pd = mBehaviour->mParticleC;	break;
		case kCollideWater:		pd = mBehaviour->mParticleB;	break;
		case kCollideThing:		pd = mBehaviour->mParticleD;	break;
		}

		if ((mBehaviour->mVolumetric <= 1) || (!i->mAttachedTo) || (!i->mAllowVolumetric))
		{
			if (mColType == kCollideWater)
				mPos.Z = MAP.GetWaterLevel() - 0.15f;

			CParticleEffectLink	link;
			PARTICLE_MANAGER.CreateEffect(pd, &link, ZERO_FVECTOR, 0, i->mImportant);
			link.SetPos(mPos);
			link.SetOrientation(mOrientation);
		}
		else
		{
			// the effects are scattered over what it went off on
			FMatrix	&ori = i->mAttachedTo->GetOrientation();
			FVector	up = ori.GetColumnY();
			float	m = sqrtf(up.X * up.X + up.Y * up.Y + up.Z * up.Z);
			float	angle;
			if (m > 0)
				angle = (float)asin(up.Z / m);
			else
				angle = 0;

			for (SINT n = 0; n < mBehaviour->mVolumetric; n++)
			{
				switch (mColType)
				{
				case kCollideNothing:	pd = mBehaviour->mParticleA;	break;
				case kCollideGround:	pd = mBehaviour->mParticleC;	break;
				case kCollideWater:		pd = mBehaviour->mParticleB;	break;
				case kCollideThing:		pd = mBehaviour->mParticleD;	break;
				}

				FVector	pos;
				if (i->mAttachedTo->GetRenderThing())
					pos = i->mAttachedTo->GetRenderThing()->GetRandomSurfacePoint(0);
				else
					pos = i->mPos;

				// a point under water moves up the thing to the surface, and splashes
				if (MAP.GetWaterLevel() < pos.Z)
				{
					float	c = (float)fabs(cos(angle + PI / 2));
					if (c > 0)
					{
						float	d = -((pos.Z - MAP.GetWaterLevel()) / c);
						FMatrix	&o = i->mAttachedTo->GetOrientation();
						pd = mBehaviour->mParticleB;
						float	x = (d * o.Row[0].Y);
						float	y = (d * o.Row[1].Y);
						float	z = (d * o.Row[2].Y);
						pos += FVector(x, y, z);
						pos.Z -= 0.15f;
					}
				}

				PARTICLE_MANAGER.CreateEffect(pd, NULL, pos, 0, 0);
			}
		}

		switch (mColType)
		{
		case kCollideNothing:
		case kCollideGround:
		case kCollideThing:
			if (mBehaviour->mSoundA)
				SOUND.PlayEffect(mBehaviour->mSoundA, this, 0.7f, ST_SETINITIALPOSITION, false, 0, 0, -1.f, false, 1.f, ST_GAME_SOUND, 0);
			break;
		case kCollideWater:
			if (mBehaviour->mSoundB)
				SOUND.PlayEffect(mBehaviour->mSoundB, this, 0.7f, ST_SETINITIALPOSITION, false, 0, 0, -1.f, false, 1.f, ST_GAME_SOUND, 0);
			break;
		}

		if (mBehaviour->mLight != -1)
		{
			float	size = mRadius * 2;
			if (size < 4.f)
				size = 4.f;
			ENGINE.LogLight(mPos, (eLightType)mBehaviour->mLight, size);
		}

		if ((mBehaviour->mDamage > 0) && (mRadius > 3.f))
			Unknown66();
		else
			AddShutdownEvent();
	}
	else
	{
		// too deep under water to go off
		WORLD.AddThing(this);
		AddShutdownEvent();
	}
}

//*********************************************************************************
// n and parts are declared before the distance: retail stores them between the magnitude's last square and its final add.
// The early return is what keeps ebp saved at entry, as retail does.
void	CExplosion::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x0044bf10
{
	if (mCurrentRadius <= 0)
		return;

	if ((mBehaviour->mSmart) && (other_thing->IsA(THING_TYPE_UNIT)) && (((CUnit *)other_thing)->mAllegiance == mAllegiance))
		return;

	SUPERTYPE::Hit(other_thing, report);

	SINT	n = 0;
	BOOL	parts = FALSE;
	float	dist = (mPos - other_thing->GetPos()).Magnitude() - other_thing->GetRadius();

	if ((other_thing->IsA(THING_TYPE_UNIT)) && (((CUnit *)other_thing)->mSegments) && (report->mUnknown00) && (report->mNumContacts > 0))
		parts = TRUE;

	do
	{
		SINT	part;
		if (parts)
		{
			part = report->mContactParts[n];
			dist = mRadius + report->mContactDistances[n];
		}
		else
			part = -1;

		if (dist < 0)
			dist = 0;

		if (dist <= mCurrentRadius)
		{
			BOOL	was_dying = other_thing->IsDying();

			if (mRadius > 3.f)
				other_thing->Damage(mBehaviour->mDamage / float(mBehaviour->mTime) * (mRadius - dist) / mRadius, this, TRUE, part);
			else
				other_thing->Damage((mRadius - dist) * mBehaviour->mDamage / mRadius, this, TRUE, part);

			CThing	*originator = mOriginator.ToRead();
			if ((originator) && (originator->IsA(THING_TYPE_UNIT)) && (other_thing->IsA(THING_TYPE_UNIT)) && (other_thing->IsDying()) && (!was_dying))
				((CUnit *)originator)->ConfirmedKill((CUnit *)other_thing);
		}
		n++;
	} while ((parts) && (report->mNumContacts > n));
}

//*********************************************************************************
// grows by a step each frame until it reaches its radius, then goes
void	CExplosion::Unknown66()	// FUNCTION: 0x0044c0f0
{
	mCurrentRadius += mRadius / float(mBehaviour->mTime);
	if (mCurrentRadius >= mRadius)
		AddShutdownEvent();
	else
		EVENT_MANAGER.AddEvent(EXPLOSION_EVENT_GROW, this, NEXT_FRAME);
}

//*********************************************************************************
void	CExplosion::HandleEvent(CEvent *event)	// FUNCTION: 0x0044c140
{
	switch (event->GetEventNum())
	{
	case EXPLOSION_EVENT_GROW:
		Unknown66();
		break;
	default:
		SUPERTYPE::HandleEvent(event);
	}
}

//*********************************************************************************
float	CExplosion::GetRadius()	// FUNCTION: 0x0044c160
{
	return mRadius;
}

//*********************************************************************************
float	CExplosion::Unknown67()	// FUNCTION: 0x0044c170
{
	return mBehaviour->mDamage;
}

//*********************************************************************************
void	CExplosion::SetThingType(ULONG t)	// FUNCTION: 0x0044c180
{
	t |= THING_TYPE_EXPLOSION;
	if ((mBehaviour) && (mBehaviour->mDamage > 3.f))
		t |= THING_TYPE_CAN_DESTROY_TREES;
	SUPERTYPE::SetThingType(t);
}

//*********************************************************************************
void	FVector::Read(CDXMemBuffer *file)	// FUNCTION: 0x0044c1c0
{
	float	f;
	file->Read(&f, sizeof(f));	X = f;
	file->Read(&f, sizeof(f));	Y = f;
	file->Read(&f, sizeof(f));	Z = f;
}

//*********************************************************************************
void	FMatrix::Read(CDXMemBuffer *file)	// FUNCTION: 0x0044c210
{
	Row[0].Read(file);
	Row[1].Read(file);
	Row[2].Read(file);
}
