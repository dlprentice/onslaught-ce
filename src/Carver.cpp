// Carver.cpp: reconstructed from Carver.obj (00422350..004229a7); Stuart's release has no copy. The carver: a small
// flier that fades out of sight (its stealth), opens its wings to attack and dies on hitting anything solid. The
// allocations' __LINE__s are the retail ones; the #line places them.

#include "common.h"
#include "unit.h"
#include "InitThing.h"
#include "RenderThing.h"
#include "mesh.h"
#include "meshrenderer.h"
#include "eventmanager.h"
#include "CarverAI.h"
#include "CarverGuide.h"

// FUNCTION: 0x00422350 init:ZERO_FVECTOR
// FUNCTION: 0x00422370 init:ID_FMATRIX

//******************************************************************************************
void CCarver::Init(CInitThing *init)	// FUNCTION: 0x00422440
{
	init->mInitCST.mMaxCollision = ECL_APPROX_GEOMETRY_SHAPES;
	SUPERTYPE::Init(init);
	mUnknown12C.mRoll = 0.5f;

#line 22
	mGuide = new(MEMTYPE_GUIDE) CCarverGuide(this);
	mAI = new(MEMTYPE_AI) CCarverAI(this, init);

	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("launch"), TRUE, TRUE);
	mWingState = 0;
	mStealth = 0;
	mUnknown284 = 0;
	mAttackedTime = -100.0f;
}

// The AI's implicit destructors, emitted here with its vtable.
// FUNCTION: 0x00422560 ??_GCCarverAI@@UAEPAXI@Z
// FUNCTION: 0x00422580 ??1CCarverAI@@UAE@XZ

//******************************************************************************************
void CCarver::Move()	// FUNCTION: 0x00422620
{
	if (mFlags & TF_DYING)
	{
		float	roll = mCurrentOrientation.mRoll + 0.8f;
		mUnknown12C.mRoll = 0.2f;
		if (roll > PI)
			roll -= PI_M2;
		mUnknown120.mRoll = roll;
		mUnknown120.mPitch = PI / 2;
	}
	SUPERTYPE::Move();

	if (mUnknown284 - 1.0f > mStealth)
		mStealth += 1.0f;
	else if (mUnknown284 + 1.0f < mStealth)
		mStealth -= 1.0f;
	else
		mStealth = mUnknown284;

	// it flies where it faces
	float	speed = mVelocity.Magnitude();
	FVector	v;
	v = FVector(speed * mOrientation.Row[0].Y, speed * mOrientation.Row[1].Y, speed * mOrientation.Row[2].Y);
	SetVelocity(v);
}

//******************************************************************************************
void CCarver::Stop()	// FUNCTION: 0x00422750
{
	mGuide->Stop();
}

//******************************************************************************************
void CCarver::OpenWings()	// FUNCTION: 0x00422760
{
	if (!mWingState)
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("wingopen"), TRUE, FALSE);
		mWingState = 1;
	}
}

//******************************************************************************************
void CCarver::CloseWings()	// FUNCTION: 0x004227a0
{
	if (mWingState == 3)
	{
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("wingclose"), TRUE, FALSE);
		mWingState = 2;
	}
}

//******************************************************************************************
void CCarver::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x004227e0
{
	if ((!mUnitData->mUnknown11C) && ((other_thing->GetThingType() & THING_TYPE_UNIT) || (other_thing->GetThingType() & THING_TYPE_TREE)))
		AddShutdownEvent();
	SUPERTYPE::Hit(other_thing, report);
}

//******************************************************************************************
BOOL CCarver::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x00422820
{
	if (GetRenderAnimation() == AM_INVALID)
		return FALSE;
	switch (mWingState)
	{
	case 2:
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("launch"), TRUE, TRUE);
		mWingState = 0;
		return FALSE;
	case 1:
		SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("attack"), TRUE, TRUE);
		mWingState = 3;
		break;
	}
	return FALSE;
}

//******************************************************************************************
void CCarver::Render(DWORD flags)	// FUNCTION: 0x004228b0
{
	if (mStealth > 0)
	{
		MESH_ALPHA = (SINT)(255.0f-(mStealth/100.0f*255.0f));	// Stuart's spelling (BattleEngine.cpp): the constant is one shared COMDAT in retail
		flags |= 0x40;
	}
	CThing::Render(flags);
	MESH_ALPHA = 255;
}

//******************************************************************************************
BOOL CCarver::OnScanner()	// FUNCTION: 0x00422910
{
	if (mStealth > 0)
		return FALSE;
	return TRUE;
}

//******************************************************************************************
void CCarver::SetAttacked()	// FUNCTION: 0x00422930
{
	mAttackedTime = EVENT_MANAGER.GetTime();
}

//******************************************************************************************
BOOL CCarver::WasAttacked()	// FUNCTION: 0x00422940
{
	if (mAttackedTime + 1.0f > EVENT_MANAGER.GetTime())
		return TRUE;
	return FALSE;
}

//******************************************************************************************
BOOL CCarver::CanBeLocked()	// FUNCTION: 0x00422970
{
	if (mStealth > 0)
		return FALSE;
	if (mAttackedTime + 3.0f > EVENT_MANAGER.GetTime())
		return FALSE;
	return TRUE;
}

//******************************************************************************************
// The carver's slot 108 answer. Returning FALSE compiles to a body others already have, so the linker folds it into
// that one (/OPT:ICF).

