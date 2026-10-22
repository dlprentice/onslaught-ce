// RTCutscene.cpp: reconstructed from the retail cutscene renderer. Its arrays and virtual slots are established
// by Init 004dbd80 and vtable 005dea38; allocation line numbers below are the retail __LINE__ values.

#include "common.h"
#include "Cutscene.h"
#include "mesh.h"
#include "meshrenderer.h"
#include "DebugLog.h"

CRTCutscene::CRTCutscene()	// FUNCTION: 0x004dbb60
{
	mNumMeshes = 0;
}

SINT CRTCutscene::Unknown27()	// FUNCTION: 0x004dbc20
{
	return 5;
}

// FUNCTION: 0x004dbc30 ??_GCRTCutscene@@UAEPAXI@Z

CRTCutscene::~CRTCutscene()	// FUNCTION: 0x004dbc50
{
	Unknown13();
	SAFE_DELETE(mMeshes);
	for (SINT n = 0; n < mNumMeshes; n++)
		delete mNames[n];
	delete mNames;
}

void CRTCutscene::Init(CInitRenderThing *init)	// FUNCTION: 0x004dbd80
{
	CRenderThing::Init(init);
	mNumMeshes = init->mNumExtra;
#line 41
	mMeshes = new(MEMTYPE_CUTSCENE) CMESH *[mNumMeshes];
	mNames = new(MEMTYPE_CUTSCENE) char *[mNumMeshes];
	for (SINT n = 0; n < mNumMeshes; n++)
	{
		mMeshes[n] = NULL;
#line 50
		mNames[n] = new(MEMTYPE_CUTSCENE) char[256];
		strcpy(mNames[n], init->mExtra[n]);
	}
	mCueFactor = 1.0f;
	mActive = false;
	mCurrentIndex = -1;
}

void CRTCutscene::Unknown12()	// FUNCTION: 0x004dbe50
{
	if (!mActive)
	{
		for (SINT n = 0; n < mNumMeshes; n++)
			mMeshes[n] = CMESH::GetMesh(mNames[n], 1);
		mActive = true;
	}
}

void CRTCutscene::Unknown13()	// FUNCTION: 0x004dbe90
{
	if (mActive)
	{
		SAFE_DELETE(mMeshes);
		mActive = false;
		mCurrentIndex = -1;
	}
}

void CRTCutscene::Render(DWORD flags)	// FUNCTION: 0x004dbec0
{
	if (mActive)
	{
		if (mCurrentIndex == -1)
			TRACE("Attempt to render cutscene without selecting mesh!\n");
		else
		{
			CMESH *mesh = GetRTMesh();
			FVector pos = ZERO_FVECTOR;
			FMatrix ori = ID_FMATRIX;
			CUTSCENE_RENDERING = TRUE;		// set around the mesh render call (004dbf2a, 004dbf54)
			CMeshRenderer::Render(&pos, &ori, mesh, mThing, this, 0, true, 0);
			CUTSCENE_RENDERING = FALSE;
		}
	}
}

void CRTCutscene::SetCurrentIndex(SINT index)	// FUNCTION: 0x004dbf70
{
	mCurrentIndex = index;
}

CMESH *CRTCutscene::GetRTMesh()	// FUNCTION: 0x004dbf80
{
	if (!mThing)
		return NULL;
	if (!mActive)
		return NULL;
	if (mCurrentIndex == -1)
		return NULL;
	return mMeshes[mCurrentIndex];
}

float CRTCutscene::GetRTRadius()	// FUNCTION: 0x004dbfb0
{
	return 0;
}

float CRTCutscene::GetFrameIncrement(EAnimMode am, int *realindex)	// FUNCTION: 0x004dbfc0
{
	CMESH *mesh = GetRTMesh();
	if (mesh)
		return mesh->GetFrameIncrement(am, realindex);
	return 0;
}

void CRTCutscene::GetMovieCameraPosition(FMatrix *ori, FVector *pos, float *fov, IRenderableThing *thing)	// FUNCTION: 0x004dbff0
{
	CMESH *mesh = GetRTMesh();
	if (mesh)
	{
		SINT frame;
		if (thing && mesh->mNumAnimModes)
		{
			SINT anim = thing->GetRealAnimIndex();
			if (anim >= 0)
			{
				CAnimMode *a = &mesh->mAnimModes[anim];
				double time = thing->GetRenderFrame() * a->mFrameScaler;
				frame = (SINT)floor(time) + a->mStart;
				// Retail keeps the second floor call even though the fraction is unused (004dc05d).
				float fraction = time - floor(time);
			}
			else
				frame = 0;
		}
		else
			frame = 0;

		CMeshPart *part = mesh->GetMovieCameraSubpart(frame);
		if (part)
		{
			SINT f = frame % part->mNumVFrames;
			part->GetWorldTransforms(f, f, 0, thing, ori, pos, false, TRUE, 0);
			*fov = part->mHFrameFOVs[part->mVHFrameMap[f]];
			return;
		}
	}

	ori->Row[0] = FVector(1.0f, 0.0f, 0.0f);
	ori->Row[1] = FVector(0.0f, 1.0f, 0.0f);
	ori->Row[2] = FVector(0.0f, 0.0f, 1.0f);
	pos->X = 0;
	pos->Y = 0;
	pos->Z = 0;
	*fov = 90.0f;
}

// Render reads these exact per-file constants (004dbef4..004dbf24).
// FUNCTION: 0x004dc190 init:ZERO_FVECTOR
// FUNCTION: 0x004dc1b0 init:ID_FMATRIX

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004dbc10 ?FindEffect@CRenderThing@@UAEHPAVCMeshPart@@@Z
// FUNCTION: 0x004dbd50 ??_GCRenderThing@@UAEPAXI@Z
// FUNCTION: 0x004dbbe0 ?GetRandomSurfacePoint@CRenderThing@@UAE?AVFVector@@H@Z
// FUNCTION: 0x004dbd20 ??1CRenderThing@@UAE@XZ
// FUNCTION: 0x004dbd40 ?GetFrameIncrement@CRenderThing@@UAEMW4EAnimMode@@PAH@Z

// CRenderThing's slot 7 (RenderThing.h): no emitter, so the origin and this file's identity.
inline void CRenderThing::GetRTEmitter(char *name, SINT index, FVector &pos, FMatrix &ori, BOOL a, BOOL b)	// FUNCTION: 0x004dbb80
{
	pos = FVector(0, 0, 0);
	ori = ID_FMATRIX;
}
