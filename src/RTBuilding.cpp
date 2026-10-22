// RTBuilding.cpp: reconstructed from RTBuilding.obj (004db850..004dbb4f); Stuart's release has no copy. A
// building's render thing: the mesh render thing, holding the building's rubble mesh.

#include "common.h"
#include "RTBuilding.h"
#include "mesh.h"
#include "meshrenderer.h"
#include "mathsconst.h"
#include "DebugLog.h"

//******************************************************************************************
CRTBuilding::~CRTBuilding()	// FUNCTION: 0x004db850
{
	if (mRubble)
	{
		mRubble->Release();
		mRubble = NULL;
	}
}

// Inline functions from headers that this object emits; its vtable places them.
// FUNCTION: 0x004db880 ?AccumulateResources@CRenderThing@@UAEXPAVCResourceAccumulator@@K@Z
// FUNCTION: 0x004db8d0 ??_GCRTBuilding@@UAEPAXI@Z

//******************************************************************************************
void CRTBuilding::Init(CInitRenderThing *init)	// FUNCTION: 0x004db8f0
{
	CRTMesh::Init(init);
	mNumRubble = 6;
	mRubble = NULL;	// STAND-IN (retail's own store; review F 2026-10-15): dropping it loses 004db8f0
	mRubble = CMesh::GetMesh(init->mUnknown100, 1);
	if (!mRubble)
	{
		LOG.AddMessage("no rubble found for %s", init->mUnknown100);
		mRubble = CMesh::GetMesh("m-b-rubble.msh", 1);
	}
	mCueFactor = 1.0f;
}

//******************************************************************************************
void CRTBuilding::Unknown28()	// FUNCTION: 0x004db960
{
	if (mThing)
	{
		FVector	pos = ZERO_FVECTOR;
		FMatrix	ori = ID_FMATRIX;
		CMeshRenderer::Render(&pos, &ori, mMesh, mThing, this, 0, true, 0);
	}
}

inline float CRTBuilding::GetRTRadius() { CMESH *m = GetRTMesh(); if (m) return m->mRadius; return 0.0f; }	// FUNCTION: 0x004db9d0
inline CBoundingBox *CRTBuilding::GetBoundingBox() { CMESH *m = GetRTMesh(); if (m) return m->mBoundingBox; return NULL; }	// FUNCTION: 0x004dba20

//******************************************************************************************
CMESH *CRTBuilding::Unknown10()	// FUNCTION: 0x004dba40
{
	if (!mNumRubble)
		return NULL;
	SINT	n = rand() % mNumRubble;
	CMESH	*m = mRubble;
	for (SINT i = 0; (i < n) && (m->mNextInChain); i++)
		m = m->mNextInChain;
	return m;
}

// FUNCTION: 0x004dba70 init:ZERO_FVECTOR
// FUNCTION: 0x004dba90 init:ID_FMATRIX
