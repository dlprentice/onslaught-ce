// Dropship.cpp: reconstructed from Dropship.obj (00446c80..0044839f); Stuart's release has no copy. The dropship: a
// big aircraft that finds a clear place to land (its anchor), folds its wings, opens its door for what it carries and
// takes off again; its AI (DropshipAI.cpp) drives the door states. The allocations' __LINE__s are the retail ones; the
// #lines place them.

#include "common.h"
#include "unit.h"
#include "world.h"
#include "map.h"
#include "mapwho.h"
#include "mapwhoentry.h"
#include "mesh.h"
#include "RenderThing.h"
#include "ParticleManager.h"
#include "WorldPhysicsManager.h"
#include "InitThing.h"
#include "Motioncontroller.h"
#include "UnitAI.h"
#include "Guide.h"
#include "Line.h"
#include "tree.h"
#include "explosion.h"
#include "DebugLog.h"

// FUNCTION: 0x00446c80 init:ZERO_FVECTOR
// FUNCTION: 0x00446ca0 init:ID_FMATRIX

CParticleDescriptor	*CDropship::sThrusterDust;

//******************************************************************************************
void CDropship::Init(CInitThing *init)	// FUNCTION: 0x00446d70
{
	mHasAnchor = FALSE;
	mAnchorFixed = FALSE;

	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_ANIMAL | THING_TYPE_BUILDING | THING_TYPE_FEATURE | THING_TYPE_TREE | THING_TYPE_UNKNOWN_08000000;
	init->mInitCST.mMaxCollision = ECL_MESH;

	SUPERTYPE::Init(init);

	float	turn = mUnitData->mTurnRate;
	mUnknown12C = CEulerAngles(turn, turn * 0.25f, turn * 0.25f);

	// on the ground it starts with its door closed, in the air with its wings flat
	if (MAP.Collide(mPos) <= init->mPos.Z)
	{
		mDoorState = 6;
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("doorclosed"), TRUE, TRUE);
	}
	else
	{
		mDoorState = 0;
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("wingflat"), TRUE, TRUE);
	}

#line 44
	mMotionController = new(MEMTYPE_MOTIONCONTROLLER) CMCDropship(this);
	mGuide = new(MEMTYPE_GUIDE) CDropshipGuide(this);
	mAI = new(MEMTYPE_AI) CDropshipAI(this, init);

	// a thruster for each "Thruster" emitter
	if (mUnitData->mParticles[3])
	{
		SINT	n = 1;
		while (mRenderThing->GetRTEmitter("Thruster", n) != FVector(0, 0, 0))
		{
#line 55
			CParticleEffectLink	*link = new(MEMTYPE_PARTICLE) CParticleEffectLink();
			mThrusters.Append(link);
			n++;
		}
	}

	mUnknown29C = 0;
	mUnknown298 = 0;
	mYawDelta = 0;
	mUnknown2A4 = 0;
	mUnknown2A8 = 0;
	sThrusterDust = PARTICLE_SET.FindByName("Thruster Dust Effect");
}

// The AI's implicit destructors, emitted here with its vtable.
// FUNCTION: 0x00447040 ??_GCDropshipAI@@UAEPAXI@Z
// FUNCTION: 0x00447060 ??1CDropshipAI@@UAE@XZ

//******************************************************************************************
void CDropship::Shutdown()	// FUNCTION: 0x00447100
{
	WORLD.RemoveFromOccupancy(this);
	SUPERTYPE::Shutdown();
}

//******************************************************************************************
// STAND-IN, in the accessor style of the guides (inline-budget rule, README): IsDying, GetDoorState (all thirteen
// reads), GetUnitData, GetCurrentOrientation, GetRenderThing and GetYawDelta stand for the real accesses. They are free
// inline sites in the budget's divisor, which keeps SpawnThrusterDust out of line in both thruster loops as retail has it
// (004472b2, 004473f9). Measured on Move (shape / instructions / bytes): any one site can be a direct read, but whole
// families cannot: GetDoorState 63/115/1774, IsDying 17/17/487 (its first site, `if (IsDying())`, alone gives the same),
// GetRenderThing 65/117/1774, GetUnitData 0/4/9 (its SpawnExplosion site alone), and GetCurrentOrientation plus
// GetYawDelta plus four GetUnitData sites together 63/115/1774: the number of sites matters, not which.
// The named distance vector gives the battle engine test retail's spilled Z (0044779b; `(a - b).Magnitude()` inline is
// 33/33/642), and CMap::GetWaterLevel's copy through a local (map.h) gives the water level its fld/fst copy (0044790d);
// the ground select through a clamped temporary keeps the ground on the FPU stack (0044794e..00447972; a ternary or an
// if/else is 14/24/241, assigning the ground and then overriding it 13/23/202).
// Each thruster loop declares its own link: the extra symbols move the speed's field temporaries up eleven ids, past
// the 16-bit wrap of their products' rank keys, so the sum takes its terms Z, X, Y as retail does (00447816..00447849).
void CDropship::Move()	// FUNCTION: 0x00447120
{
	if (mUnknown168 == 1)
	{
		mUnknown168 = 0;
		mUnknown1E8 = 1;
	}

	// dying: roll over and fall
	if (IsDying())
	{
		if (!IsInWater())
		{
			float	roll = GetCurrentOrientation().mRoll + 0.8f;
			mUnknown12C.mRoll = 0.2f;
			if (roll > PI)
				roll -= PI_M2;
			mUnknown120.mRoll = roll;
			mUnknown120.mPitch = 2.0f;
		}
		if (MAP.Collide(mPos) + 1.0f < mPos.Z)
			DeclareOnGround();
	}

	SUPERTYPE::Move();

	// the thrusters fire while it lands and takes off
	if ((!(IsDying())) && ((GetDoorState() == 5) || (GetDoorState() == 2) || (GetDoorState() == 3) || (GetDoorState() == 6) || (GetDoorState() == 7)))
	{
		SINT	n = 1;
		ListIterator<CParticleEffectLink>	thrusters(&mThrusters);
		for (CParticleEffectLink *link = thrusters.First(); link; link = thrusters.Next())
		{
			if (!link->mEffect)
				PARTICLE_MANAGER.CreateEffect(GetUnitData()->mParticles[8], link, ZERO_FVECTOR, 0, 0);
			FVector	pos;
			FMatrix	ori;
			GetEmitter(26, n, pos, ori);
			link->SetPos(pos);
			link->SetOrientation(ori);
			n++;
			SpawnThrusterDust(pos, ori);
		}
		mUnknown298 = TRUE;
	}
	else if (mUnknown298)
	{
		ListIterator<CParticleEffectLink>	thrusters(&mThrusters);
		for (CParticleEffectLink *link = thrusters.First(); link; link = thrusters.Next())
			link->Stop(FALSE);
		mUnknown298 = FALSE;
	}

	// and idle while its door is open
	if ((GetDoorState() == 4) && (!(IsDying())))
	{
		SINT	n = 1;
		ListIterator<CParticleEffectLink>	thrusters(&mThrusters);
		for (CParticleEffectLink *link = thrusters.First(); link; link = thrusters.Next())
		{
			if (!link->mEffect)
				PARTICLE_MANAGER.CreateEffect(GetUnitData()->mParticles[3], link, ZERO_FVECTOR, 0, 0);
			FVector	pos;
			FMatrix	ori;
			GetEmitter(26, n, pos, ori);
			link->SetPos(pos);
			link->SetOrientation(ori);
			n++;
			SpawnThrusterDust(pos, ori);
		}
		mUnknown29C = TRUE;
	}
	else if (mUnknown29C)
	{
		ListIterator<CParticleEffectLink>	thrusters(&mThrusters);
		for (CParticleEffectLink *link = thrusters.First(); link; link = thrusters.Next())
			link->Stop(FALSE);
		mUnknown29C = FALSE;
	}

	mUnknown2A8 = mUnknown2A4;

	if (GetDoorState() == 6)
	{
		// opening its door, until the door's emitter reaches the ground
		CAnimMode	*anim = GetRenderThing()->GetRTMesh()->mAnimModes;
		SINT		i = 0;
		BOOL		found = FALSE;
		while ((!found) && (i < GetRenderThing()->GetRTMesh()->mNumAnimModes))
		{
			if (!stricmp("dooropening", anim->mModeName))
				found = TRUE;
			else
			{
				anim++;
				i++;
			}
		}
		if (found)
		{
			FVector	pos;
			FMatrix	ori;
			GetEmitter(27, 1, pos, ori);
			float	ground = MAP.Collide(pos) - 0.1f;
			if ((pos.Z < ground) && (mUnknown2A4 < 1.0f))
				mUnknown2A4 += anim->mRate;
			else
				mDoorState = 4;
			if (mUnknown2A4 > 1.0f)
				mUnknown2A4 = 1.0f;
		}
		else
			mDoorState = 4;
	}
	else if (GetDoorState() == 7)
	{
		CAnimMode	*anim = GetRenderThing()->GetRTMesh()->mAnimModes;
		SINT		i = 0;
		BOOL		found = FALSE;
		while ((!found) && (i < GetRenderThing()->GetRTMesh()->mNumAnimModes))
		{
			if (!stricmp("doorclosing", anim->mModeName))
				found = TRUE;
			else
			{
				anim++;
				i++;
			}
		}
		if (found)
		{
			mUnknown2A4 += anim->mRate;
			if (mUnknown2A4 >= 1.0f)
				Unknown120();
		}
		else
			Unknown120();
	}
	else if (GetDoorState() == 0)
		mUnknown2A4 = 0;
	else if (GetDoorState() == 2)
	{
		// turning to land: fold its wings once it faces the way it lands
		float	target = GetYawDelta();
		float	yaw = GetCurrentOrientation().mYaw;
		if ((yaw < -PI / 2) && (target > PI / 2))
			target -= PI_M2;
		else if ((yaw > PI / 2) && (target < -PI / 2))
			target += PI_M2;
		if (fabs(yaw - target) < 0.1f)
			Unknown119();
	}
	else if (GetDoorState() == 3)
	{
		// landing: flatten the trees under it and batter any battle engine beneath
		for (CMapWhoEntry *e = MAP_WHO.GetFirstEntryWithinRadius(mPos, 25.0f); e; e = MAP_WHO.GetNextEntryWithinRadius())
		{
			CThing	*t = e->GetThing();
			if (!t)
				continue;
			if (t->IsA(THING_TYPE_TREE))
			{
				if (!((CTree *)t)->mFalling)
				{
					FVector	d = t->GetPos() - mPos;
					if (d.MagnitudeSq() < 100.0f)
					{
						d.Z = 0;
						d.Normalise();
						((CTree *)t)->CreateFallingTree(d);
					}
				}
			}
			else if (t->IsA(THING_TYPE_BATTLE_ENGINE))
			{
				if (mPos.Z - 2.0f < t->GetPos().Z)
				{
					FVector	d = t->GetPos() - mPos;
					float	dist = d.Magnitude();
					if (dist < 7.0f)
						t->Damage(0.9f / dist, this);
				}
			}
		}
	}
	else if (GetDoorState() != 5)
	{
		// at its top speed it flies straight along its heading
		float	speed = mVelocity.Magnitude();
		float	max = GetUnitData()->mUnknownB4 * 0.03125f;
		if (speed >= max)
			SetVelocity(mOrientation * FVector(0, speed, 0));
		if ((GetDoorState() == 0) && (AllSpawnersReady()))
			Unknown100();
	}

	// a dying dropship that hits the ground or the sea explodes
	if ((IsDying()) && (IsBoxBelowSurface()))
	{
		for (SINT i = 0; i < 4; i++)
		{
			CExplosionInitThing	init;
			init.mPos = GetRenderThing()->GetRandomSurfacePoint(0);
			float	water = MAP.GetWaterLevel();
			if (water + 1.0f > init.mPos.Z)
			{
				float	ground = MAP.Collide(init.mPos);
				float	z = ground;
				if (water < ground)
					z = water;
				init.mPos.Z = z;
				CExplosion	*explosion = UPhysicsManager::SpawnExplosion(GetUnitData()->mSmallExplosion);
				if (explosion)
				{
					init.mAllegiance = (EAllegiance)mAllegiance;
					init.mBehaviour = UPhysicsManager::GetExplosionData(GetUnitData()->mSmallExplosion);
					init.mColType = (ground > water) ? kCollideWater : kCollideGround;
					explosion->Init(&init);
				}
			}
		}
	}
}

//******************************************************************************************
// Turn to land facing away from its heading.
void CDropship::Unknown118()	// FUNCTION: 0x00447a40
{
	if ((mHasAnchor) && ((mAnchorFixed) || (IsAnchorClear())))
	{
		mDoorState = 2;
		mYawDelta = mCurrentOrientation.mYaw - PI;
		if (mYawDelta > PI)
			mYawDelta -= PI_M2;
		else if (mYawDelta < -PI)
			mYawDelta += PI_M2;
	}
}

//******************************************************************************************
void CDropship::Unknown119()	// FUNCTION: 0x00447ac0
{
	mDoorState = 3;
	mHasAnchor = FALSE;
	WORLD.AddToOccupancy(this);
	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("wingfolded"), TRUE, FALSE);
}

//******************************************************************************************
void CDropship::Unknown120()	// FUNCTION: 0x00447b10
{
	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("wingunfolded"), TRUE, FALSE);
	mDoorState = 5;
	WORLD.RemoveFromOccupancy(this);
}

//******************************************************************************************
void CDropship::DeclareInWater()	// FUNCTION: 0x00447b50
{
	CActor::DeclareInWater();
}

//******************************************************************************************
BOOL CDropship::IsAtAnchor()	// FUNCTION: 0x00447b60
{
	if ((mHasAnchor) && ((mPos - mAnchor).MagnitudeXY() < 10.0f))
		return TRUE;
	return FALSE;
}

//******************************************************************************************
// A clear place to land: where it is, or the first clear point on a spiral out from it. One offset vector serves
// throughout: its Y is the spiral's radius, and the orientation products fold its zero X and Z away (00447bd0, 00447c96).
FVector CDropship::GetAnchor()	// FUNCTION: 0x00447bb0
{
	if (!mAnchorFixed)
	{
		FVector	offset(0, 0, 0);
		if (!mHasAnchor)
		{
			mAnchor = mOrientation * offset + mPos;
			mHasAnchor = TRUE;
		}

		SINT	i = 0;
		while ((!IsAnchorClear()) && (i < 8000))
		{
			offset.Y = i * 0.03125f;
			FMatrix	ori(SINT(i % 32) * (2 * PI / 32), 0, 0);
			i++;
			mAnchor = ori * offset + mPos;
		}
		if (i >= 8000)
			mHasAnchor = FALSE;
	}
	return mAnchor;
}

//******************************************************************************************
// Nothing grounded near the anchor, and every occupancy cell under it marked free (retail returns FALSE on a clear
// bit, 00447f19). The near test is retail's: the comparison's flag plus 25 is never zero, so any grounded unit within
// the search radius blocks.
// STAND-INS: the anchor reference stands for retail keeping &mAnchor in ebp from the start (00447d5b; reading mAnchor
// directly is 8/50/179). The squared distance is written out on a local (retail squares Y before X, 00447de3, which
// MagnitudeXYSq() does not: 8/8/15), and this unit's own radius product is bracketed, which calls its GetRadius before
// the other's (00447df9; unbracketed 0/16/19).
BOOL CDropship::IsAnchorClear()	// FUNCTION: 0x00447d50
{
	FVector	&anchor = mAnchor;
	for (CMapWhoEntry *e = MAP_WHO.GetFirstEntryWithinRadius(anchor, GetRadius() * 2); e; e = MAP_WHO.GetNextEntryWithinRadius())
	{
		CThing	*t = e->GetThing();
		if ((t->IsA(THING_TYPE_UNIT)) && (t != this) && (MAP.Collide(t->GetPos()) - t->GetPos().Z < 5.0f))
		{
			FVector	delta = t->GetPos() - anchor;
			if ((delta.X * delta.X + delta.Y * delta.Y <= t->GetRadius() * t->GetRadius() + (GetRadius() * GetRadius())) + 25.0f)
				return FALSE;
		}
	}

	if ((mDoorState != 2) && (mDoorState != 3))
	{
		COccupancyGrid	*grid = WORLD.mOccupancy[2];
		SINT	r = (SINT(GetRadius()) + 3) >> 1;
		SINT	cx = SINT(anchor.X) >> 1;
		SINT	cy = SINT(anchor.Y) >> 1;
		for (SINT y = cy - r; y < cy + r; y++)
		{
			if ((y < 0) || (y >= 256))
				continue;
			for (SINT x = cx - r; x < cx + r; x++)
			{
				if ((x < 0) || (x >= 256))
					continue;
				if (!(grid->mData[(x >> 3) * 256 + y] & (1 << (x % 8))))
					return FALSE;
			}
		}
	}
	return TRUE;
}

//******************************************************************************************
void CDropship::Unknown93()	// FUNCTION: 0x00447f50
{
	mHasAnchor = mAnchorFixed = TRUE;
	mDeployState = 0;
	mAnchor = mPos;
	Unknown118();
}

//******************************************************************************************
BOOL CDropship::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x00447fa0
{
	EAnimMode	anim = GetRenderAnimation();
	if (anim != AM_INVALID)
	{
		if (anim == mRenderThing->GetRTMesh()->GetAnimModeByName("dooropening"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("dooropen"), TRUE, TRUE);
			mDoorState = 4;
			return FALSE;
		}
		if (anim == mRenderThing->GetRTMesh()->GetAnimModeByName("doorclosing"))
		{
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("doorclosed"), TRUE, TRUE);
			Unknown120();
			return FALSE;
		}
		if (anim == mRenderThing->GetRTMesh()->GetAnimModeByName("wingfolded"))
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("doorclosed"), TRUE, TRUE);
		else if (anim == mRenderThing->GetRTMesh()->GetAnimModeByName("wingunfolded"))
			SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("wingflat"), TRUE, TRUE);
		return FALSE;
	}
	return TRUE;
}

//******************************************************************************************
BOOL CDropship::CanStayOpen()	// FUNCTION: 0x004480c0
{
	if (mAnchorFixed)
		return TRUE;
	if ((!AllSpawnersReady()) && (GetCurrentTarget()) && (CanFireAt(GetCurrentTarget(), TRUE)))
		return TRUE;
	return FALSE;
}

//******************************************************************************************
void CDropship::Unknown448110()	// FUNCTION: 0x00448110
{
	mDoorState = 6;
}

//******************************************************************************************
void CDropship::Unknown448120()	// FUNCTION: 0x00448120
{
	mDoorState = 7;
	mUnknown2A4 = 1.0f - mUnknown2A4;
}

//******************************************************************************************
BOOL CDropship::Unknown117()	// FUNCTION: 0x00448140
{
	if (mDoorState == 0)
		return SUPERTYPE::Unknown117();
	return FALSE;
}

//******************************************************************************************
// Its door is open. Slot 99 has the same body; the linker keeps one copy.
BOOL CDropship::Unknown97()	// FUNCTION: 0x00448160
{
	return mDoorState == 4;
}

BOOL CDropship::Unknown99()
{
	return mDoorState == 4;
}

//******************************************************************************************
// Dust where a thruster's line down meets the ground, when that is within 10.
void CDropship::SpawnThrusterDust(const FVector &pos, const FMatrix &ori)	// FUNCTION: 0x00448170
{
	float	h = MAP.Collide(pos) - pos.Z;
	if (h < 10.0f)
	{
		CParticleEffectLink	link;
		CLine	line(pos, pos + ori * FVector(0, 100.0f, 0));
		FVector	hit;
		MAP.Unknown490a40(&line, &hit, 0);
		hit.Z += h * 0.2f;
		PARTICLE_MANAGER.CreateEffect(sThrusterDust, &link, ZERO_FVECTOR, 0, 0);
		link.SetPos(hit);
	}
}

//******************************************************************************************
float CDropship::Gravity()	// FUNCTION: 0x00448360
{
	if (mFlags & TF_DYING)
	{
		float	r = GetRadius();
		if (r > 39.0f)
			r = 39.0f;
		return (40.0f - r) * 0.0001f;
	}
	return 0.0f;
}

//******************************************************************************************
// The dropship's answers (its ground behaviour is CSimpleBuilding's, SimpleBuilding.cpp) that no code above defined.
// Each compiles to a body another function already has, so the linker folds it into that one (/OPT:ICF): the retail
// table's slot points at the survivor, whose address each annotation names (the class header notes the slots' survivors
// too).

// FUNCTION: 0x004dfcb0
BOOL CDropship::ClipToGround()
{
	if (IsDying())
		return FALSE;
	return TRUE;
}

// FUNCTION: 0x004dfc60
void CDropship::DeclareOnGround()
{
	CActor::DeclareOnGround();		// retail's body calls CActor's directly (004dfc60 -> 00402000), as CInfantryUnit::DeclareOnGround does
	if (mFlags & TF_DYING)
	{
		SpawnExplosion();
		AddShutdownEvent();
	}
	SetVelocity(FVector(0, 0, 0));
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00426340 ??_GCLine@@UAEPAXI@Z
