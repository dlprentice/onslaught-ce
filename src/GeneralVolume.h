// GeneralVolume.h: the collision volumes' base (vtable 005d892c, all slots but the destructor pure). Stuart's release
// has no GeneralVolume.h; the slots are read from the vtables of CMeshCollisionVolume (005d95c8), CSphere
// (005d95e8) and CCylinder (005d88cc). Collision is double-dispatched: Collide (slot 5) calls the other volume's
// CollideWith slot for this volume's type (CMeshCollisionVolume's 004262e0 calls slot 1 with itself, CSphere's
// 00426320 slot 3, CCylinder's 004059a0 slot 2), and GetType (slot 6) returns 0 for a sphere, 2 for a cylinder
// and 3 for a mesh. Dispatch exchanges the first two movement arguments as well as the two volume roles
// (004098cd..004098d8, 004262ed..004262f8); the final report pointer is preserved. The first movement belongs
// to the receiving volume (CMeshCollisionVolume::CollideWithSphere 004ac755).

#ifndef GENERALVOLUME_H
#define GENERALVOLUME_H

#include "maths.h"

class CMeshCollisionVolume;
class CCylinder;
class CSphere;
class CLine;

// The movement a collision test sweeps (CMeshCollisionVolume::CollideWithSphere 004ac755..004ac7c8): the move
// and the position it ends at; the test shortens it and sets +0x20 when it stops against something.
struct CCollisionMove
{
	FVector		mMove;						// +0x00
	FVector		mPos;						// +0x10
	BOOL		mStopped;					// +0x20
};

// The record a collision test fills: up to six contacts, each a normal, part number and distance.
struct CCollisionReport
{
	// Retail zeros +0 before six empty normal-vector constructions (004bd159, 004bdd63, 0050b073).
	// Other report fields are reset by each caller; the vector constructor writes no components.
	CCollisionReport() : mUnknown00(0) {}
	SINT		mUnknown00;					// +0x00: meaning remains open; triangle argument six is the radius, not this field
	UBYTE		mUnknown04[0x08 - 0x04];
	FVector		mNormals[6];				// +0x08: indexed at 004ad421..004ad442; six-contact limit at 004acac4
	SINT		mContactParts[6];			// +0x68
	SINT		mNumContacts;				// +0x80
	FVector		mContactPos;				// +0x84
	SINT		mUnknown94;					// +0x94: set on a hit
	FVector		mRelativeMove;				// +0x98: other movement minus this movement (00426718..00426759)
	SINT		mUnknownA8;					// +0xa8: set on a hit; CollideWithSphere repeats while it is set
	float		mContactDistances[6];		// +0xac
	float		mDistance;					// +0xc4: how far inside the box (CMeshCollisionVolume 004ac2f8)
	BOOL		mStuck;						// +0xc8 (Stuart's; CBattleEngine::Hit 0040735a)
	SINT		mTestTriangles;				// +0xcc: minimum collision response (00426711); zero also skips detailed tests
	SINT		mUnknownD0;					// +0xd0
	FVector		mTriangleNormal;			// +0xd4: normalized triangle cross product stored at 00478fe4..0047900e
	class CMeshPart	*mPart;					// +0xe4: the part tested last
};

class CGeneralVolume
{
public:
	CGeneralVolume() : mPos(0, 0, 0) {}		// the zeroing is in CCylinder's inline copy (0040641a) and CLine's (0040d479)
	virtual			~CGeneralVolume() {}										// 0 (scalar deleting 004059d0)
	virtual BOOL	CollideWithMesh(void *a, CCollisionMove *move, CMeshCollisionVolume *other, CCollisionReport *report) = 0;	// 1
	virtual BOOL	CollideWithCylinder(void *a, CCollisionMove *move, CCylinder *other, CCollisionReport *report) = 0;		// 2
	virtual BOOL	CollideWithSphere(void *a, CCollisionMove *move, CSphere *other, CCollisionReport *report) = 0;			// 3
	virtual BOOL	CollideWithLine(void *a, CCollisionMove *move, CLine *other, CCollisionReport *report) = 0;				// 4
	virtual BOOL	Collide(void *a, CCollisionMove *move, CGeneralVolume *other, CCollisionReport *report) = 0;				// 5
	virtual SINT	GetType() = 0;																		// 6
	BOOL			TestAtOffset(CGeneralVolume *other, FVector offset, CCollisionReport *report);	// 004780f0, descriptive name

	FVector			mPos;					// +0x04
};

#endif
