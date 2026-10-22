// MCTentacle.h: CMCTentacle (vtable 005dc450, 10 slots, 0xec bytes), a tentacle's motion controller. CTentacle builds
// it on its IRenderableThing (004f07a9). Each frame it lays the numbered bones along a cubic Bezier curve from the
// tether through the tether's and the head's control points to the head, which follows the unit's own head position
// and orientation (CTentacle 004f0ba0, 004f0c50). Stuart's release has no copy; names are ours.

#ifndef MCTENTACLE_H
#define MCTENTACLE_H

#include "Motioncontroller.h"
#include "maths.h"

class CMesh;
class CMeshPart;
class IRenderableThing;

class CMCTentacle : public CMotionController
{
public:
	CMCTentacle(IRenderableThing *thing);											// 0049cad0
	virtual			~CMCTentacle();													// 1 (0049cb40)

	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (0049e660)
	virtual void	AdjustFrame(CMeshPart *part, float *frame);						// 5 (0049ead0)
	virtual BOOL	Unknown8();														// 8 (0049ec80)

	void			Init(CMesh *mesh);												// 0049cc40
	void			UpdateBone(FVector pos, FMatrix ori, CMeshPart *part, SINT unused, float dt);	// 0049d280
	void			UpdateSpline(CMesh *mesh);										// 0049dcd0

	// The mesh optimizer keeps a tentacle's moving parts apart, as CMCBuggy's and CMCMech's do.
	static BOOL		CanMergePart(CMeshPart *part);									// 0049eca0
	static BOOL		HasTentacle(CMesh *mesh);										// 0049ed30: a part named "tentacle"

	FMatrix			*mPartOri;					// +0x0c: each part's orientation this frame
	FVector			*mPartPos;					// +0x10
	float			*mPartFrame;				// +0x14
	FMatrix			*mOldPartOri;				// +0x18: and last frame's
	FVector			*mOldPartPos;				// +0x1c
	float			*mOldPartFrame;				// +0x20: -1 until a part has two
	SINT			mUnknown24;					// +0x24
	float			mLastTime;					// +0x28: when the parts were last updated
	BOOL			mInitialised;				// +0x2c
	FVector			mTetherPos;					// +0x30: the "tether" part's rest position
	FVector			mHeadPos;					// +0x40: where the unit puts the head
	FMatrix			mHeadOri;					// +0x50
	FVector			mTetherCPPos;				// +0x80: the "tethercp" part's rest position
	FVector			mHeadCPPos;					// +0x90: the "headcp" part, carried with the head
	CMeshPart		*mHead;						// +0xa0
	CMeshPart		*mTether;					// +0xa4
	CMeshPart		*mHeadCP;					// +0xa8
	CMeshPart		*mTetherCP;					// +0xac
	CMeshPart		*mTentacle;					// +0xb0
	FVector			mTentaclePos;				// +0xb4: the "tentacle" part's rest position
	UBYTE			mUnknownC4[0xdc - 0xc4];
	FVector			*mBonePos;					// +0xdc: each bone's point on the curve
	FMatrix			*mBoneOri;					// +0xe0: each bone's orientation along the curve
	CMeshPart		**mBones;					// +0xe4: "bone01", "bone02", ...
	SINT			mNumBones;					// +0xe8: the highest "Bone" number
};

#endif
