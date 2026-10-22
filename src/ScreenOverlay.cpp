// ScreenOverlay.cpp: CScreenOverlay (reconstructed; Stuart's file is not in the release). From the retail object,
// 004de3a0..004de8b3.

#include "common.h"
#include "mathsconst.h"
#include "ScreenOverlay.h"
#include "RenderThing.h"
#include "mesh.h"
#include "event.h"
#include "eventmanager.h"

// A cdecl mesh-part renderer at 0054b800 (the DX mesh code before CDXMeshVB). Open question: its name.
void	RenderMeshPart0054b800(CMeshPart *part, IRenderableThing *thing, SINT a);

//******************************************************************************************
CScreenOverlay::CScreenOverlay(char *name)	// FUNCTION: 0x004de3a0
{
	CInitRenderThing	init;
	init.mThing = this;
	strcpy(init.mName, name);
	strcat(init.mName, ".msh");
	strcpy(init.mUnknown100, "");
	init.mMeshFlags = 1;
	init.mUnknown404 = 1;

	mRenderThing = CreateRenderThing(RTID_CRTMesh);
	if (mRenderThing)
		mRenderThing->Init(&init);

	mPos = FVector(0.f, 2.f, 0.f);
	FMatrix	&m = mOrientation;	// STAND-IN (order; review F 2026-10-15): the rows through a reference; direct member stores lose 004de3a0
	m.Row[0] = FVector(1.f, 0.f, 0.f);
	m.Row[1] = FVector(0.f, 1.f, 0.f);
	m.Row[2] = FVector(0.f, 0.f, 1.f);

	mAnimMode = mRenderThing->GetRTMesh()->GetAnimModeByName("Effect");
	if (mRenderThing)
		mFrameIncrement = mRenderThing->GetFrameIncrement(mAnimMode, &mRealAnimIndex);
	else
		mFrameIncrement = 0;
	mFrame = 0;
	mFinished = false;
	mDeleteSelf = false;

	if (EVENT_MANAGER.IsValid() && mAnimMode)
		EVENT_MANAGER.AddEvent(SCREEN_OVERLAY_EVENT, this, -1.f, END_OF_FRAME);
}

//******************************************************************************************
CScreenOverlay::~CScreenOverlay()	// FUNCTION: 0x004de760
{
	SAFE_DELETE(mRenderThing);
}

//******************************************************************************************
void	CScreenOverlay::HandleEvent(CEvent *event)	// FUNCTION: 0x004de7d0
{
	if (event->GetEventNum() == SCREEN_OVERLAY_EVENT)
	{
		CScreenOverlay	*so = this;	// STAND-IN (order; review F 2026-10-15): this through a named pointer; plain member access loses 004de7d0
		so->mFrame += so->mFrameIncrement;
		if (so->mFrame > 0.99f)
		{
			so->mFrame = 0.99f;
			so->mFinished = true;
		}
		if (!so->mFinished)
			EVENT_MANAGER.AddEvent(SCREEN_OVERLAY_EVENT, so, -1.f, END_OF_FRAME);
		if (so->mDeleteSelf)
			delete so;
	}
}

//******************************************************************************************
void	CScreenOverlay::Finish()	// FUNCTION: 0x004de850
{
	mFinished = true;
	mDeleteSelf = true;
}

//******************************************************************************************
void	CScreenOverlay::Render()	// FUNCTION: 0x004de860
{
	if (mRenderThing)
	{
		CMESH	*mesh = mRenderThing->GetRTMesh();
		for (SINT c0 = 0; c0 < mesh->mNumParts; c0++)
			RenderMeshPart0054b800(mesh->mParts[c0], this, 0);
	}
}

//******************************************************************************************
float	CScreenOverlay::GetRenderFrame()	// FUNCTION: 0x004de8b0
{
	return mFrame;
}

// FUNCTION: 0x004de6a0 ?Unknown29@CScreenOverlay@@UAEHXZ
// FUNCTION: 0x004de6b0 ?GetRenderPos@CScreenOverlay@@UAE?AVFVector@@XZ
// FUNCTION: 0x004de6e0 ?GetRenderOrientation@CScreenOverlay@@UAE?AVFMatrix@@XZ
// FUNCTION: 0x004de710 ?GetRenderAnimation@CScreenOverlay@@UAE?AW4EAnimMode@@XZ
// FUNCTION: 0x004de720 ?GetRealAnimIndex@CScreenOverlay@@UAEHXZ
// FUNCTION: 0x004de730 ??_GCScreenOverlay@@UAEPAXI@Z

// FUNCTION: 0x004de2b0 init:ZERO_FVECTOR
// FUNCTION: 0x004de2d0 init:ID_FMATRIX
