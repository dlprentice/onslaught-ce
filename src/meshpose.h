// meshpose.h: a render thing's cached pose, one transform per mesh part (0x1c bytes, MEMTYPE_POSE).
// Reconstructed from rtmesh.obj: CRTMesh::Init constructs one (004dc593) and calls Init, whose four
// allocations are tagged with this header's lines 33..36 (004dc5d5..004dc68c). The destructor is emitted
// out of line (004dcb00).

#ifndef MESHPOSE_H
#define MESHPOSE_H

class CMeshPose
{
public:
	CMeshPose()
	{
		mNumParts = 0;
		mPositions = NULL;
		mOrientations = NULL;
		mFrames = NULL;
		mParents = NULL;
		mLastTime = -99999.0f;
		mLastFrame = -9999;
	}
	~CMeshPose()
	{
		SAFE_DELETE(mOrientations);
		SAFE_DELETE(mPositions);
		SAFE_DELETE(mFrames);
		SAFE_DELETE(mParents);
	}

	void	Init(SINT num_parts)
	{
		mNumParts = num_parts;
		mOrientations = new(MEMTYPE_POSE) FMatrix[num_parts];
		mPositions = new(MEMTYPE_POSE) FVector[num_parts];
		mFrames = new(MEMTYPE_POSE) SINT[num_parts];
		mParents = new(MEMTYPE_POSE) SINT[num_parts];
		for (SINT i = 0; i < mNumParts; i++)
			mParents[i] = -1;
	}

	// MeshPose.cpp (004b4ab0..004b512f) builds the pose (declared below Init: Init's allocations carry this
	// header's lines 33..36).
	BOOL	Update(class CMesh *mesh, class IRenderableThing *thing, class CRenderThing *rt, BOOL force);	// 004b4cd0
	void	Build(FVector pos, FMatrix ori, class CMeshPart *part, class IRenderableThing *thing, class CRenderThing *rt, SINT frame);	// 004b4ba0

	FMatrix		*mOrientations;			// +0x00
	FVector		*mPositions;			// +0x04
	SINT		*mFrames;				// +0x08
	SINT		*mParents;				// +0x0c
	SINT		mNumParts;				// +0x10
	SINT		mLastFrame;				// +0x14
	float		mLastTime;				// +0x18
};

#endif
