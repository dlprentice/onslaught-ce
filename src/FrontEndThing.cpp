// FrontEndThing.cpp: a mesh the front-end pages show and spin (reconstructed from the retail PC executable;
// Stuart's file is not in his release).

#include	"Common.h"

#include	"FrontEndThing.h"
#include	"eventmanager.h"
#include	"mesh.h"
#include	"mathsconst.h"

// FUNCTION: 0x0046b950
void	CFrontEndThing::Init(SFrontEndThingInit *init)
{
	mPos = init->mPos;
	mOri = init->mOri;

	CRenderData	data;
	data.mRTID[0] = 1;
	data.mInit[0].mThing = this;
	strcpy(data.mInit[0].mName, init->mName);

	mRenderThing = CreateRenderThing(data.mRTID[0]);
	if (mRenderThing)
	{
		mRenderThing->Init(&data.mInit[0]);
		mFrameIncrement = mRenderThing->GetFrameIncrement(AM_WALK, &mAnim);
		mFrame = 0;
	}
	else
	{
		mFrameIncrement = 0;
		mFrame = 0;
		mAnim = 0;
	}
	mForwards = TRUE;
	mCycles = 200;
}

// FUNCTION: 0x0046ba90
CFrontEndThing::~CFrontEndThing()
{
	if (mRenderThing)
		delete mRenderThing;
}

// FUNCTION: 0x0046bab0
void	CFrontEndThing::SetAnimation(char *name)
{
	CMESH	*mesh = mRenderThing->GetRTMesh();
	if (mesh)
	{
		mFrameIncrement = mRenderThing->GetFrameIncrement(mesh->GetAnimModeByName(name), &mAnim);
		mFrame = 0;
	}
}

// FUNCTION: 0x0046baf0
void	CFrontEndThing::Process()
{
	mOri.MakeRotationYawF(EVENT_MANAGER.GetTime() / 5.0f);

	if (mForwards)
	{
		mFrame += mFrameIncrement * 0.6f;
		if (mFrame > 1.f)
		{
			mFrame = 1.f;
			if (--mCycles == 0)
			{
				mCycles = 200;
				mForwards = FALSE;
			}
		}
	}
	else
	{
		mFrame -= mFrameIncrement * 0.6f;
		if (mFrame < 0.f)
		{
			mFrame = 0;
			if (--mCycles == 0)
			{
				mCycles = 200;
				mForwards = TRUE;
			}
		}
	}
}

// FUNCTION: 0x0046bc20
void	CFrontEndThing::Render()
{
	if (mRenderThing)
		mRenderThing->Render(0);
}

// FUNCTION: 0x0046bc30 init:ZERO_FVECTOR
// FUNCTION: 0x0046bc50 init:ID_FMATRIX

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00403f40 ??0CInitRenderThing@@QAE@XZ
// FUNCTION: 0x00403f80 ??1CInitRenderThing@@QAE@XZ
