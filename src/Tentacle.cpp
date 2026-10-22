// Tentacle.cpp: reconstructed from Tentacle.obj (004f0540..004f1237); Stuart's release has no copy. A tentacle: a
// component whose bones follow its head (CMCTentacle), and which blows its bones off one by one as it dies.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "weapon.h"	// retail's .data has this TU's kWeaponCoolRate copy (weapon.h reached it; the include path is ours)
#include "component.h"
#include "MCTentacle.h"
#include "mesh.h"
#include "InitThing.h"
#include "UnitAI.h"
#include "Guide.h"
#include "eventmanager.h"
#include "WorldPhysicsManager.h"
#include "explosion.h"
#include "map.h"
#include "ParticleManager.h"
#include "ParticleSet.h"
#include <stdio.h>
#include <string.h>

// FUNCTION: 0x004f0540 init:ZERO_FVECTOR
// FUNCTION: 0x004f0560 init:ID_FMATRIX

//******************************************************************************************
void CTentacle::Init(CInitThing *init)	// FUNCTION: 0x004f0630
{
	((CUnitInitThing *)init)->mStats->mUnknown1A0 = 1;
	mHeadAngles.mYaw = 0;
	mHeadAngles.mPitch = PI / 2 + 0.25f;
	mHeadAngles.mRoll = 0;
	mUnknown2D0 = PI / 2 + 0.25f;
	mBigArm = FALSE;
	if (!stricmp(((CUnitInitThing *)init)->mStats->mName, "Sentinel Big Arm"))
		mBigArm = TRUE;
	mHeadOffset = FVector(0, GetHeadLength(), 0.25f);
	mNextBoneTime = 0;
	mUnknown2E4 = mHeadOffset;
	mUnknown2F4 = FVector(0, 0, 0);
	mUnknown2C0 = 1;
	mBonesLeft = 1;

	SUPERTYPE::Init(init);
}

//******************************************************************************************
void CTentacle::Unknown117()	// FUNCTION: 0x004f0760
{
#line 47
	mMotionController = new(MEMTYPE_MOTIONCONTROLLER) CMCTentacle(this);
}

//******************************************************************************************
void CTentacle::Unknown118()	// FUNCTION: 0x004f07e0
{
#line 53
	mGuide = new(MEMTYPE_GUIDE) CTentacleGuide(this);
}

//******************************************************************************************
void CTentacle::Unknown119(CInitThing *init)	// FUNCTION: 0x004f0860
{
#line 60
	mAI = new(MEMTYPE_GUIDE) CTentacleAI(this, init);	// the guide's memory type (retail pushes 0x17, 004f0881)
}

// CTentacleAI's implicit destructor, emitted here with its vtable.
// FUNCTION: 0x004f08f0 ??_GCTentacleAI@@UAEPAXI@Z
// FUNCTION: 0x004f0910 ??1CTentacleAI@@UAE@XZ

//******************************************************************************************
// While it dies, one bone a tenth of a second blows off, from the last; then the tentacle goes.
void CTentacle::Move()	// FUNCTION: 0x004f09b0
{
	if (mFlags & TF_DYING)
	{
		if (EVENT_MANAGER.GetTime() > mNextBoneTime)
		{
			CMesh				*mesh = mRenderThing->GetRTMesh();
			CExplosionInitThing	init;
			char				name[8];
			sprintf(name, "Bone%02d", mBonesLeft);
			for (SINT i = 0; i < mesh->mNumParts; i++)
			{
				CMeshPart	*part = mesh->mParts[i];
				if (!stricmp(part->mName, name))
				{
					mesh->mParts[i]->GetWorldTransforms(0, 0, 0, this, &init.mOrientation, &init.mPos, false, TRUE, 0);
					break;
				}
			}

			SINT		index = mUnitData->mSmallExplosion;
			CExplosion	*explosion = UPhysicsManager::SpawnExplosion(index);
			if (explosion)
			{
				init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mSmallExplosion);
				init.mAllegiance = (EAllegiance)mAllegiance;
				init.mAttachedTo = this;
				init.mUseAttachedRadius = FALSE;
				init.mColType = kCollideNothing;
				explosion->Init(&init);
			}

			mNextBoneTime = EVENT_MANAGER.GetTime() + 0.1f;
			if (!--mBonesLeft)
			{
				SpawnExplosion();
				AddShutdownEvent();
			}
		}
		CUnit::Move();
	}
	else
		SUPERTYPE::Move();
}

//******************************************************************************************
void CTentacle::GetHeadPos(FVector *pos)	// FUNCTION: 0x004f0ba0
{
	*pos = mPos + mOrientation * mHeadOffset;
}

//******************************************************************************************
void CTentacle::GetHeadOri(FMatrix *ori)	// FUNCTION: 0x004f0c50
{
	FMatrix	a;
	a.MakeRotationF(-PI / 2, -PI / 2, 0);
	FMatrix	b(mHeadAngles.mYaw, mHeadAngles.mPitch - PI / 2, 0);
	*ori = mOrientation * b * a;
}

//******************************************************************************************
// The head's position, through GetHeadPos (natural-form review 2026-10-15; the same code as the earlier two-reference body).
// Written out with the members it is 22 instructions off: inlining GetHeadPos reads them through its own parameter.
void CTentacle::GetTargetablePos(FVector *out)	// FUNCTION: 0x004f0d90
{
	GetHeadPos(out);
}

//******************************************************************************************
// The effect sits at the head: GetHeadPos is expanded here, and its rotated offset (FMatrix * FVector) stays out of line
// (0040d2c0) under the inline budget, as retail's.
void CTentacle::Activate()	// FUNCTION: 0x004f0e40
{
	if (mActive)
		return;

	SUPERTYPE::Activate();

	CParticleDescriptor	*pd = PARTICLE_SET.FindByName("Tentacle Activation Effect");
	if (pd)
	{
		FVector				pos;
		GetHeadPos(&pos);
		CParticleEffectLink	link;
		PARTICLE_MANAGER.CreateEffect(pd, &link, ZERO_FVECTOR, 0, 0);
		if (MAP.Collide(pos) < pos.Z)
			pos.Z = MAP.Collide(pos);
		link.SetPos(pos);
	}
}

//******************************************************************************************
BOOL CTentacle::StartDieProcess()	// FUNCTION: 0x004f1050
{
	if (!CUnit::StartDieProcess())	// retail calls CUnit's directly (004f1059), skipping CComponent's
		return FALSE;

	mBonesLeft = ((CMCTentacle *)mMotionController)->mNumBones;

	SINT				index = mUnitData->mExplosion;
	CExplosion			*explosion = UPhysicsManager::SpawnExplosion(index);
	CExplosionInitThing	init;
	init.mBehaviour = UPhysicsManager::GetExplosionData(mUnitData->mExplosion);
	init.mAttachedTo = this;
	init.mAllegiance = (EAllegiance)mAllegiance;
	init.mUseAttachedRadius = FALSE;
	init.mColType = kCollideNothing;
	if (explosion)
	{
		GetHeadPos(&init.mPos);
		explosion->Init(&init);
	}

	ReleaseChildren();
	return TRUE;
}

//******************************************************************************************
float CTentacle::GetHeadLength()	// FUNCTION: 0x004f1220
{
	return mBigArm ? 28.0f : 21.5f;
}
