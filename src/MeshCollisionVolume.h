// MeshCollisionVolume.h: CMeshCollisionVolume (0x28 bytes, vtable 005d95c8), a collision volume made of a render
// thing's mesh. Stuart's release has no MeshCollisionVolume.h; Stuart's BattleEngine.cpp calls SetRenderThing.
// CCollisionSeekingThing::Init (00426278..004262bb) builds one inline: +0x04 zeroed, the thing's render thing
// +0x14, +0x18 zero, two values from the init thing (+0x30, +0x24) at +0x1c and +0x20, and +0x24 NULL.

#ifndef MESHCOLLISIONVOLUME_H
#define MESHCOLLISIONVOLUME_H

#include "GeneralVolume.h"
#include "Geometry.h"

class CRenderThing;
class CMeshPart;
class CMesh;

// A part's cached transform (0x74 bytes; CMeshCollisionVolume::CachePartTransform 004ad600): the orientation, its
// transpose, the position and the frame it was taken at (-1: none yet).
struct CMeshCollisionPartTransform
{
	FMatrix		mOrientation;				// +0x00
	FMatrix		mInverse;					// +0x30
	FVector		mPos;						// +0x60
	float		mFrame;						// +0x70
};

class CMeshCollisionVolume : public CGeneralVolume
{
public:
	CMeshCollisionVolume(CRenderThing *rt, BOOL useFixedTransforms, BOOL ignoreAnimation)
		: mRenderThing(rt), mUnknown18(0), mUnknown1C(ignoreAnimation), mUnknown20(useFixedTransforms), mTransforms(NULL) {}
	virtual			~CMeshCollisionVolume();												// 0 (004ac0e0)
	virtual BOOL	CollideWithMesh(void *a, CCollisionMove *move, CMeshCollisionVolume *other, CCollisionReport *report);	// 1 (MeshCollisionVolume.cpp; folded into 004098b0)
	virtual BOOL	CollideWithCylinder(void *a, CCollisionMove *move, CCylinder *other, CCollisionReport *report);		// 2 (004abe50)
	virtual BOOL	CollideWithSphere(void *a, CCollisionMove *move, CSphere *other, CCollisionReport *report);			// 3 (004ac6e0)
	virtual BOOL	CollideWithLine(void *a, CCollisionMove *move, CLine *other, CCollisionReport *report);				// 4 (004ad830)
	// FUNCTION: 0x004262e0
	virtual BOOL	Collide(void *a, CCollisionMove *move, CGeneralVolume *other, CCollisionReport *report)
					{ return other->CollideWithMesh(move, (CCollisionMove *)a, this, report); }		// 5: swap both volume and movement roles
	virtual SINT	GetType() { return 3; }															// 6 (00405ee0)

	BOOL			TestSweptSphereAgainstBox(CMeshPart *part, CMeshPart *collision, const FVector *pos, const FVector *move,
						const float *radius, CCollisionReport *report);						// 004ac140
	BOOL			TestSweptSphereAgainstPart(CMeshPart *part, CMeshPart *collision, const FVector *pos, const FVector *move,
						const float *radius, CCollisionReport *report);						// 004ac4a0
	BOOL			ResolveContact(CCollisionReport *report, FVector a, FVector b, float radius, FVector *start, FVector *move);	// 004acf30
	void			CachePartTransform(CMesh *mesh, SINT part, float frame);				// 004ad600
	void			SetRenderThing(CRenderThing *rt);										// 004ad7f0

	CRenderThing	*mRenderThing;			// +0x14
	SINT			mUnknown18;				// +0x18: cleared by each collision test
	SINT			mUnknown1C;				// +0x1c
	SINT			mUnknown20;				// +0x20
	CMeshCollisionPartTransform	*mTransforms;	// +0x24: one per mesh part (MEMTYPE 0x6c); freed by the destructor and SetRenderThing
};

#endif
