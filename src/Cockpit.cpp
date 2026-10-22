// Cockpit.cpp: the battle engine's cockpit, the mesh drawn around the first-person view, with its shake and its
// walker/jet morphs. Not in Stuart's release: reconstructed from retail (004243c0..0042590f).

#include "common.h"
class CCockpit;class IRenderableThing;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#include "DebugLog.h"		// init:ZERO_FVECTOR, init:ID_FMATRIX
#include "Cockpit.h"
#include "BattleEngine.h"
#include "BattleEngineDataManager.h"
#include "RenderThing.h"
#include "mesh.h"
#include "EventManager.h"
#include "Event.h"
#include "game.h"
#include "map.h"
#include "BattleEngineWalkerPart.h"
// FUNCTION: 0x004243c0 init:ZERO_FVECTOR
// FUNCTION: 0x004243e0 init:ID_FMATRIX

#define COCKPIT_EVENT_PROCESS	2001

// FUNCTION: 0x004244b0
CCockpit::CCockpit(CBattleEngine *engine)
{
	mShakePos = FVector(0.f, 0.f, 0.f);
	CRenderData	data;

	data.mRTID[0] = RTID_CRTMesh;
	data.mInit[0].mThing = this;
	if (engine->GetConfiguration()->mCockpit)
		strcpy(data.mInit[0].mName, engine->GetConfiguration()->mCockpit);

	mRenderThing = SpawnRenderThing(data.mRTID[0]);
	if (mRenderThing)
		mRenderThing->Init(&data.mInit[0]);

	mShouldRender = TRUE;
	mShake[0] = 0;
	mShake[1] = 0;
	mShake[2] = 0;
	mShake[3] = 0;
	mShakeTime = 0;
	mUnknownAC = 0;
	EBattleEngineState state = engine->GetState();
	mOrientation = ID_FMATRIX;
	mBaseOrientation = ID_FMATRIX;

	if (state == BATTLE_ENGINE_STATE_JET)
		mAnimMode = mRenderThing->GetRTMesh()->GetAnimModeByName("jet");
	else
		mAnimMode = mRenderThing->GetRTMesh()->GetAnimModeByName("walk");

	mUnknownE0 = FMatrix(0.f, 0.f, 0.f);
	mBattleEngine = engine;
	EVENT_MANAGER.AddEvent(COCKPIT_EVENT_PROCESS, this, -1.f, END_OF_FRAME);

	EAnimMode am = GetRenderAnimation();
	mFrameIncrement = mRenderThing ? mRenderThing->GetFrameIncrement(am, &mRealAnimIndex) : 0.f;
	mFrame = 0;
	mOldFrame = 0;
	mMorphState = 0;
	mUnknownA4 = 0;
	mUnknownA8 = 0;
}

// FUNCTION: 0x00424710 ??_GCCockpit@@UAEPAXI@Z
// FUNCTION: 0x00424730
CCockpit::~CCockpit()
{
	SAFE_DELETE(mRenderThing);
}

// FUNCTION: 0x004247a0
void CCockpit::AddShockShake(float amount)
{
	float scale = 128.f / amount;
	amount *= 0.5f;

	mShake[0] = (rand() % 128) / scale - amount;
	mShake[1] = (rand() % 128) / scale - amount;
	mShake[2] = (rand() % 128) / scale - amount;
	mShake[3] = (rand() % 32) / scale - amount;
	mShakeTime = 0;

	if (mShake[0] > 0.01f)
		mShake[0] = 0.01f;
	else if (mShake[0] < -0.01f)
		mShake[0] = -0.01f;
	if (mShake[1] > 0.01f)
		mShake[1] = 0.01f;
	else if (mShake[1] < -0.01f)
		mShake[1] = -0.01f;
	if (mShake[2] > 0.01f)
		mShake[2] = 0.01f;
	else if (mShake[2] < -0.01f)
		mShake[2] = -0.01f;
}

// FUNCTION: 0x00424920
void CCockpit::MorphIntoWalkerCockpit()
{
	mAnimMode = mRenderThing->GetRTMesh()->GetAnimModeByName("flytowalk");
	mFrameIncrement = mRenderThing ? mRenderThing->GetFrameIncrement(mAnimMode, &mRealAnimIndex) : 0.f;
	mFrame = 0;
	mOldFrame = 0;
	mMorphState = 1;
}

// FUNCTION: 0x00424990
void CCockpit::MorphIntoJetCockpit()
{
	mAnimMode = mRenderThing->GetRTMesh()->GetAnimModeByName("walktofly");
	mFrameIncrement = mRenderThing ? mRenderThing->GetFrameIncrement(mAnimMode, &mRealAnimIndex) : 0.f;
	mFrame = 0;
	mOldFrame = 0;
	mMorphState = 2;
}

// FUNCTION: 0x00424a00
void CCockpit::HandleEvent(CEvent *event)
{
	switch (event->GetEventNum())
	{
	case 2000:
		return;
	case COCKPIT_EVENT_PROCESS:
		Process();
		break;
	}
}

// FUNCTION: 0x00424a20
void CCockpit::Process()
{
	mOldShakePos = mShakePos;
	mOldOrientation = mOrientation;

	if (!mBattleEngine || mBattleEngine->IsDying())
		return;

	if (mBattleEngine->GetState() == BATTLE_ENGINE_STATE_WALKER)
		ProcessWalker();
	else
		ProcessJet();

	float c = (float)cos(mShakeTime);
	mShakePos = FVector(0.f, 0.f, 0.f);
	mShakePos.X += c * mShake[0];
	mShakePos.Y += c * mShake[1];
	mShakePos.Z += c * mShake[2];

	mShake[0] *= 0.8f;
	mShake[1] *= 0.8f;
	mShake[2] *= 0.8f;
	mShake[3] *= 0.8f;

	if (fabsf(mShake[0]) > 0.001f || fabsf(mShake[1]) > 0.001f || fabsf(mShake[2]) > 0.001f || fabsf(mShake[3]) > 0.001f)
		mShakeTime += 0.8f;

	EVENT_MANAGER.AddEvent(COCKPIT_EVENT_PROCESS, this, -1.f, END_OF_FRAME);
	ProcessAnimation();
}

// FUNCTION: 0x00424be0
void CCockpit::ProcessAnimation()
{
	mOldFrame = mFrame;
	mFrame += mFrameIncrement;
	if (mFrame > 1.f)
	{
		if (mMorphState == 1)
		{
			mAnimMode = mRenderThing->GetRTMesh()->GetAnimModeByName("walk");
			mFrameIncrement = mRenderThing ? mRenderThing->GetFrameIncrement(mAnimMode, &mRealAnimIndex) : 0.f;
			mFrame = 0;
			mMorphState = 0;
		}
		else if (mMorphState == 2)
		{
			mAnimMode = mRenderThing->GetRTMesh()->GetAnimModeByName("fly");
			mFrameIncrement = mRenderThing ? mRenderThing->GetFrameIncrement(mAnimMode, &mRealAnimIndex) : 0.f;
			mFrame = 0;
			mMorphState = 0;
		}
		else
		{
			mFrame = 0;
			mFrameIncrement = 0;
		}
	}
}

// The walker's cockpit bobs with the walk cycle and leans with the ground under it: its pitch and roll ease 0.01 a
// frame towards the slope along and across the engine's heading. The slope is worked out the way Stuart's jet part
// follows the ground (CBattleEngineJetPart::HandleGroundEffect): the heading matrix times (0, 1, 0), and the cross
// product with the normal copied before it is normalised (retail copies it at 00424f61). That spelling
// spends what retail's inline budget spent, so FMatrix(yaw, pitch, roll)'s row default constructors are called out of
// line as in retail (vector_constructor_iterator, 004250a0), as are the heading matrix's second and third row
// constructors (00424da7, 00424dd4).
// FUNCTION: 0x00424ca0
void CCockpit::ProcessWalker()
{
	CBattleEngine *be = mBattleEngine;
	if (!be || be->IsDying())
		return;

	mOrientation = mBaseOrientation;

	float roll = (float)cos(mShakeTime) * mShake[3] - be->GetYawVel();
	float yaw = -be->GetYawVel();
	float pitch = be->GetPitchVel() * -2.f;

	if (be->IsWalking())
	{
		float cycle = mBattleEngine->GetWalkerPart()->GetCurrentWalkCycle();
		float c = (float)cos(cycle);
		roll += c * 0.02f;
		float bob = (float)cos(cycle * 2) * 0.02f;
		if (bob < 0.0)
			bob = 0;
		pitch += bob;
		yaw += c * 0.03f;
	}

	float heading = mBattleEngine->GetCurrentOrientation().mYaw;
	FMatrix ori;
	ori.MakeRotationYawF(heading);
	FVector forward = ori * FVector(0.0f, 1.0f, 0.0f);

	FVector normal;
	if (mBattleEngine->IsOnGround() && !mBattleEngine->IsOnObject())
		normal = MAP.Normal(mBattleEngine->GetPos());
	else
		normal = FVector(0, 0, -1);

	FVector cross = forward ^ normal;
	FVector along = cross ^ normal;
	along.Normalise();
	float targetPitch = along.Z * -0.25f;
	FVector right = cross;
	right.Normalise();
	float targetRoll = right.Z * 0.75f;

	if (targetPitch - 0.01f > mUnknownA4)
		mUnknownA4 += 0.01f;
	else if (targetPitch + 0.01f < mUnknownA4)
		mUnknownA4 -= 0.01f;
	else
		mUnknownA4 = targetPitch;

	if (targetRoll - 0.01f > mUnknownA8)
		mUnknownA8 += 0.01f;
	else if (targetRoll + 0.01f < mUnknownA8)
		mUnknownA8 -= 0.01f;
	else
		mUnknownA8 = targetRoll;

	// Retail 004250ca passes the copied base orientation as operator*'s this, and the rotation as its right operand;
	// the rolled value goes to the constructor's own parameter (00425076..0042508e), as in ProcessJet.
	mOrientation = mOrientation * FMatrix(yaw, pitch + mUnknownA4, roll + mUnknownA8);
}

// FUNCTION: 0x004250f0
void CCockpit::ProcessJet()
{
	CBattleEngine *be = mBattleEngine;
	if (!be || be->IsDying())
		return;

	mOrientation = mBaseOrientation;

	float roll = (float)cos(mShakeTime) * mShake[3] - mBattleEngine->GetYawVel();
	float yaw = -mBattleEngine->GetYawVel();
	float pitch = mBattleEngine->GetPitchVel() * -2.f;

	// In flight the tilts decay towards level.
	float targetPitch = 0;
	float targetRoll = 0;

	if (mUnknownA4 < targetPitch - 0.01f)
		mUnknownA4 += 0.01f;
	else if (mUnknownA4 > targetPitch + 0.01f)
		mUnknownA4 -= 0.01f;
	else
		mUnknownA4 = targetPitch;

	if (mUnknownA8 < targetRoll - 0.01f)
		mUnknownA8 += 0.01f;
	else if (mUnknownA8 > targetRoll + 0.01f)
		mUnknownA8 -= 0.01f;
	else
		mUnknownA8 = targetRoll;

	// The inlined products at 004252c7 onward use the same base-times-rotation order as ProcessWalker. Their sums run
	// in retail's order only with retail's symbol count: the two targets, and the yaw rate read through mBattleEngine
	// in the middle of roll's expression (the inline keeps its result in a symbol), give operator*'s this and the
	// rotation's field symbols retail's ids.
	mOrientation = mOrientation * FMatrix(yaw, pitch + mUnknownA4, roll + mUnknownA8);
}

// FUNCTION: 0x00425410
float CCockpit::GetRenderFrame()
{
	return mOldFrame + (mFrame - mOldFrame) * GAME.GetFrameRenderFraction();
}

// FUNCTION: 0x00425430
FVector CCockpit::GetRenderPos()
{
	FVector pos = mBattleEngine->GetPos() + mShakePos;
	FVector old = mBattleEngine->GetOldPos() + mOldShakePos;
	return old + ((pos - old) * GAME.GetFrameRenderFraction());
}

// FUNCTION: 0x004254f0
FMatrix CCockpit::GetRenderOrientation()
{
	FMatrix ori = mBattleEngine->GetOrientation() * mOrientation;
	FMatrix old = mBattleEngine->GetOldOrientation() * mOldOrientation;
	FMatrix r = old + ((ori - old) * GAME.GetFrameRenderFraction());
	r.Normalise();
	return r;
}

// FUNCTION: 0x004258f0
CRenderThing *CCockpit::GetRenderThing()
{
	return mRenderThing;
}

// FUNCTION: 0x00425900
void CCockpit::AccumulateResources(CResourceAccumulator *accumulator)
{
	if (mRenderThing)
		mRenderThing->AccumulateResources(accumulator);
}

//******************************************************************************************
// The cockpit's IRenderableThing answers that no code above defines. Each compiles to a body another function already
// has, so the linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor (CCockpit's
// IRenderableThing table 005d94ac), whose address each annotation names. Spellings follow Stuart's CThing defaults
// (references/Onslaught thing.h:71-206) where the bodies agree. The start and end positions are the shake position:
// the survivor (CFrontEndThing::GetRenderPos 0044fd70) copies the vector at +4 of the IRenderableThing subobject (+8),
// which is CCockpit +0x0c.

// STAND-IN (layout): the inline-class answers below (marked `inline`) are retail's INLINE functions (their survivors
// are the inline twins 0x004014a0, 0x00405930, 0x00405920, 0x00405940, 0x00453a80, 0x004bfc60, 0x004de700;
// link order notes 3b/3c), defined inline here rather than in the class body; the class-body form
// (Cockpit.h) loses CFEPGoodies::Render 0045e0d0 and CPDTimeline 004c2640 (measured 2026-10-15). GetRenderMesh,
// RenderUseHierarchy and GetRenderSelected are instead inline in Cockpit.h after the class, where BattleEngine.cpp
// sees them: retail's survivors 00405910..00405930 are BattleEngine.obj's copies (their annotations stay here).

// FUNCTION: 0x00405930 ?GetRenderSelected@CCockpit@@UAEHXZ





// FUNCTION: 0x00405930 ?GetRenderColourOffset@CCockpit@@UAEHXZ
inline SINT CCockpit::GetRenderColourOffset()
{
	return 0;
}

// FUNCTION: 0x00405910
inline DWORD CCockpit::GetRenderColour()
{
	return 0xffffffff;
}

// FUNCTION: 0x004de700 ?GetRenderTurn@CCockpit@@UAEMXZ
inline float CCockpit::GetRenderTurn()
{
	return 1;
}

// FUNCTION: 0x004de700 ?GetRenderRadius@CCockpit@@UAEMXZ
inline float CCockpit::GetRenderRadius()
{
	return 1.0f;
}

// FUNCTION: 0x0044fd70
inline FVector CCockpit::GetRenderStartPos()
{
	return mShakePos;
}

// FUNCTION: 0x0044fd70
inline FVector CCockpit::GetRenderEndPos()
{
	return mShakePos;
}

// FUNCTION: 0x00405910 ?GetRenderMesh@CCockpit@@UAEHXZ





// FUNCTION: 0x00405930 ?GetRenderRotateShadow@CCockpit@@UAEHXZ
inline BOOL CCockpit::GetRenderRotateShadow()
{
	return FALSE;
}

// FUNCTION: 0x00405930 ?GetRenderCanGoFlatShaded@CCockpit@@UAEHXZ
inline BOOL CCockpit::GetRenderCanGoFlatShaded()
{
	return FALSE;
}

// FUNCTION: 0x00405920 ?RenderUseHierarchy@CCockpit@@UAEHH@Z





// FUNCTION: 0x004bfc60 ?GetRenderYaw@CCockpit@@UAEMXZ
inline float CCockpit::GetRenderYaw()
{
	return 0;
}

// FUNCTION: 0x004de700 ?GetRenderScale@CCockpit@@UAEMXZ
inline float CCockpit::GetRenderScale()
{
	return 1;
}

// FUNCTION: 0x004014a0 ?GetRenderWithTransforms@CCockpit@@UAEHXZ
inline BOOL CCockpit::GetRenderWithTransforms()
{
	return TRUE;
}

// FUNCTION: 0x00453a80 ?GetCanBeImpostered@CCockpit@@UAE_NXZ
inline bool CCockpit::GetCanBeImpostered()
{
	return false;
}

// FUNCTION: 0x004014a0 ?GetImposterFrames@CCockpit@@UAEHXZ
inline int CCockpit::GetImposterFrames()
{
	return 1;
}

// FUNCTION: 0x00405930 ?GetImposterAnimMode@CCockpit@@UAE?AW4EAnimMode@@XZ
inline EAnimMode CCockpit::GetImposterAnimMode()
{
	return AM_NONE;
}

// FUNCTION: 0x00405930 ?GetBoundingBox@CCockpit@@UAEPAVCBoundingBox@@XZ
inline CBoundingBox *CCockpit::GetBoundingBox()
{
	return NULL;
}

// FUNCTION: 0x004de700 ?GetRenderImposterBias@CCockpit@@UAEMXZ
inline float CCockpit::GetRenderImposterBias()
{
	return 1.0f;
}

// FUNCTION: 0x00405930 ?GetSnowDensity@CCockpit@@UAEHXZ
inline SINT CCockpit::GetSnowDensity()
{
	return 0;
}

// FUNCTION: 0x00405930 ?GetRequiresPolyBucket@CCockpit@@UAEHXZ
inline BOOL CCockpit::GetRequiresPolyBucket()
{
	return FALSE;
}

// FUNCTION: 0x00405940 ?GetShouldDoHitEffect@CCockpit@@UAEHH@Z
inline int CCockpit::GetShouldDoHitEffect(SINT mesh_part_no)
{
	return 0;
}

// FUNCTION: 0x00405930 ?GetRenderImposterNo@CCockpit@@UAEHXZ
inline SINT CCockpit::GetRenderImposterNo()
{
	return 0;
}

// FUNCTION: 0x00405930 ?GetMotionController@CCockpit@@UAEPAVCMotionController@@XZ
inline CMotionController *CCockpit::GetMotionController()
{
	return NULL;
}
