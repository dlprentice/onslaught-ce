// WarspiteDome.cpp: reconstructed from WarspiteDome.obj (005046f0..00504d5f); Stuart's release has no copy. The
// Warspite's gun dome: a fixed emplacement that spins while it is active, aims its pitch at its target, and kicks back
// three of its six barrels, alternately, each time its pulse laser fires.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "WarspiteDomeAI.h"
#include "Guide.h"
#include "Motioncontroller.h"
#include "world.h"
#include "weapon.h"
#include "eventmanager.h"

#include <string.h>

// FUNCTION: 0x005046f0 init:ZERO_FVECTOR
// FUNCTION: 0x00504710 init:ID_FMATRIX

//******************************************************************************************
void CWarspiteDome::Init(CInitThing *init)	// FUNCTION: 0x005047e0
{
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mMaxCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY | THING_TYPE_BUILDING | THING_TYPE_TREE | THING_TYPE_UNKNOWN_08000000;
	((CUnitInitThing *)init)->mStats->mUnknown1A0 = 1;

	SUPERTYPE::Init(init);

#line 25
	mGuide = new(MEMTYPE_GUIDE) CTerrainGuide(this);
	mAI = new(MEMTYPE_AI) CWarspiteDomeAI(this, init);
	mUnknown12C = CEulerAngles(0, 0, 0);

#line 29
	mMotionController = new(MEMTYPE_MOTIONCONTROLLER) CMCWarspiteDome(this);
	mSpinSpeed = 0;
	mSpin = 0;
	mOldSpin = 0;
	WORLD.AddToOccupancy(this);
	mOtherSide = FALSE;
	for (SINT i = 0; i < 6; i++)
	{
		mRecoil[i] = 0;
		mRecoiling[i] = FALSE;
	}
}

// CWarspiteDomeAI's implicit destructor, emitted here with its vtable.
// FUNCTION: 0x00504990 ??_GCWarspiteDomeAI@@UAEPAXI@Z
// FUNCTION: 0x005049b0 ??1CWarspiteDomeAI@@UAE@XZ

//******************************************************************************************
// The same code as CCannon's; the linker keeps that copy (0041b450).
void CWarspiteDome::Shutdown()
{
	WORLD.RemoveFromOccupancy(this);
	CUnit::Shutdown();
}

//******************************************************************************************
// The same code as CCannon's; the linker keeps that copy (0041b590).
BOOL CWarspiteDome::StartDieProcess()
{
	if (!SUPERTYPE::StartDieProcess())
		return FALSE;
	Remove();
	return TRUE;
}

//******************************************************************************************
void CWarspiteDome::Move()	// FUNCTION: 0x00504a50
{
	SUPERTYPE::Move();
	UpdatePitch();

	if (mActive)
	{
		mOldSpin = mSpin;
		if (mUnknown168 != 2)
			mSpinSpeed += 0.005f;
		mSpinSpeed *= 0.97f;
		mSpin += mSpinSpeed;

		for (SINT i = 0; i < 6; i++)
		{
			mOldRecoil[i] = mRecoil[i];
			if (mRecoiling[i])
			{
				if (mRecoil[i] < 1.0f)
					mRecoil[i] += (1.0f - mRecoil[i]) * 0.9f;
				if (mRecoil[i] >= 1.0f)
				{
					mRecoil[i] = 1.0f;
					mRecoiling[i] = FALSE;
				}
			}
			else
			{
				if (mRecoil[i] > 0)
					mRecoil[i] -= mRecoil[i] * 0.25f;
				if (mRecoil[i] <= 0)
					mRecoil[i] = 0;
			}
		}
	}
}

//******************************************************************************************
// CUnit's pitch tracking (004fb280), run every frame and clamped to its own range.
void CWarspiteDome::UpdatePitch()	// FUNCTION: 0x00504b40
{
	if (mFlags & TF_DYING)
		return;

	CUnit	*target;
	if ((mAI) && (target = mAI->mTarget.ToRead()))
	{
		if (mCurrentWeapon)
		{
			FVector	aim;
			target->GetTargetablePos(&aim);
			if ((mCurrentWeapon->mMode) && (mCurrentWeapon->mMode->mRound))
				mUnknownEC = mCurrentWeapon->CalculateDesiredPitch(aim);
		}
		mUnknown20C = EVENT_MANAGER.GetTime() + 10.0f;
	}
	else if (mUnknown20C < EVENT_MANAGER.GetTime())
		mUnknownEC = mUnknownF4;

	if (mUnknownEC < -PI * 5 / 12)
		mUnknownEC = -PI * 5 / 12;
	else if (mUnknownEC > 5.0f)
		mUnknownEC = 5.0f;
}

//******************************************************************************************
BOOL CWarspiteDome::Unknown117(CWeapon *weapon)	// FUNCTION: 0x00504c40
{
	if (!strcmp(weapon->GetName(), "Warspite Pulse Laser"))
	{
		if (!mOtherSide)
		{
			mRecoiling[0] = TRUE;
			mRecoiling[2] = TRUE;
			mRecoiling[4] = TRUE;
			mOtherSide = TRUE;
		}
		else
		{
			mRecoiling[1] = TRUE;
			mRecoiling[3] = TRUE;
			mRecoiling[5] = TRUE;
			mOtherSide = FALSE;
		}
	}
	return TRUE;
}

//******************************************************************************************
BOOL CWarspiteDome::Unknown76()	// FUNCTION: 0x00504cf0
{
	if (AnyWeaponUnknown50a290())
		return TRUE;
	if ((mUnknown168 || !mActive) && !(mFlags & TF_DYING))
		return FALSE;
	return TRUE;
}

//******************************************************************************************
BOOL CWarspiteDome::Unknown109()	// FUNCTION: 0x00504d30
{
	if (AnyWeaponUnknown50a290())
		return TRUE;
	return mUnknown168 == 0;
}

//******************************************************************************************
// The Warspite dome's answers that no code above defined. Each compiles to a body another function already has, so the
// linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address each
// annotation names (the class header notes the slots' survivors too).
