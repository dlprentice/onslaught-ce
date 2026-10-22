// rttree.h: CRTTree, a tree's render thing (vtable 005deb9c, 28 slots). Reconstructed from RTTree.obj
// (004dd5c0..004de09f). Near the camera a tree is its mesh, lit by the level's sun and anti-sun; farther out it is
// an imposter (and, beyond those, one of the landscape's tree quads, CDXTrees).

#ifndef RTTREE_H
#define RTTREE_H

#include "RenderThing.h"
#include "mesh.h"

class CRTTree : public CRenderThing
{
public:
	virtual			~CRTTree();										// 004ddfd0
	virtual void	Init(CInitRenderThing *init);					// 004dd7b0
	virtual void	Render(DWORD flags);							// 004dd960
	virtual void	RenderImposter();								// 004dd850
	virtual CBoundingBox	*GetBoundingBox();						// 004de060: inline, RTTree.cpp
	virtual float	GetRTRadius()		{ return mMesh->mRadius; }	// 004de050
	virtual CMESH	*GetRTMesh();								// 004de070: inline, RTTree.cpp
	virtual SINT	Unknown27()			{ return 2; }			// 27 (the shared 004059c0)

	CMESH			*mMesh;					// +0x14
	class CDXImposter	*mImposter;			// +0x18
	SINT			mUnknown1c;				// +0x1c: 2 after Init
	BOOL			mImposterDrawn;			// +0x20: set by RenderImposter
	float			mRadius;				// +0x24
	bool			mUnknown28;				// +0x28
	float			mMaxSlope;				// +0x2c: tan of the steepest view (in height over distance) an imposter can take
	SWORD			mSlot;					// +0x30: its quad in the landscape's tree buffers, -1 for none (CDXTrees)
};

#endif
