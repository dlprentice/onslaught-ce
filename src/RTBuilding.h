// RTBuilding.h: CRTBuilding, a building's render thing (vtable 005de9c0, 29 slots, 0x5c bytes: CreateRenderThing
// 0051661c allocates it and stores only mChild and the vptr). Reconstructed from RTBuilding.obj (004db850..004dba40).
// It is the mesh render thing with the building's rubble mesh (the init's second name) and one more slot.

#ifndef RTBUILDING_H
#define RTBUILDING_H

#include "rtmesh.h"

class CRTBuilding : public CRTMesh
{
public:
	virtual			~CRTBuilding();									// 004db850 (??_G 004db8d0)
	virtual void	Init(CInitRenderThing *init);					// 1 (004db8f0)
	// The mesh's box and radius, through GetRTMesh (004dba20, 004db9d0).
	virtual CBoundingBox	*GetBoundingBox();	// 4
	virtual float	GetRTRadius();			// 6
	virtual CMESH	*Unknown10();									// 10 (004dba40): the rubble, at a random level of detail
	virtual BOOL	Unknown27() { return 4; }						// 27 (the shared 004db8c0; CRTMesh's returns 1)
	virtual void	Unknown28();									// 28 (004db960): the mesh drawn at the origin

	SINT			mUnknown50;				// +0x50
	CMESH			*mRubble;				// +0x54: its user count held while this has it
	SINT			mNumRubble;				// +0x58: 6, the levels Unknown10 picks from
};

#endif
