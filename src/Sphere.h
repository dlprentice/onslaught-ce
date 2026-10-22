// Sphere.h: CSphere (0x1c bytes, vtable 005d95e8): a centre and a radius, and the radius squared.
// CMeshCollisionVolume::CollideWithCylinder (004abe50) builds one on its stack from a radius, then sets its centre
// (the EH state is set before the centre is copied). Stuart's release has no Sphere.h.

#ifndef SPHERE_H
#define SPHERE_H

#include "GeneralVolume.h"

class CSphere : public CGeneralVolume
{
public:
	CSphere(float radius) : mRadius(radius), mRadiusSquared(radius * radius) {}

	virtual BOOL	CollideWithMesh(void *a, CCollisionMove *move, CMeshCollisionVolume *other, CCollisionReport *report);	// 1
	virtual BOOL	CollideWithCylinder(void *a, CCollisionMove *move, CCylinder *other, CCollisionReport *report);		// 2 (004e4d70)
	virtual BOOL	CollideWithSphere(void *a, CCollisionMove *move, CSphere *other, CCollisionReport *report);			// 3 (004e4e00)
	virtual BOOL	CollideWithLine(void *a, CCollisionMove *move, CLine *other, CCollisionReport *report);				// 4 (004e4b90)
	// FUNCTION: 0x00426320
	virtual BOOL	Collide(void *a, CCollisionMove *move, CGeneralVolume *other, CCollisionReport *report)
					{ return other->CollideWithSphere(move, (CCollisionMove *)a, this, report); }	// 5: swap both volume and movement roles
	virtual SINT	GetType() { return 0; }															// 6 (00405930)

	float		mRadius;					// +0x14
	float		mRadiusSquared;				// +0x18
};

// Slot 1 answers as Collide (slot 5) does, the mesh testing the sphere with the roles swapped: the same body, which the
// linker folds with Collide's at 00426320 (/OPT:ICF). It is a header inline like Collide (LINK folds only COMDATs of
// one selection type), emitted with the vtable by each object that needs it, as CCylinder's slots are (cylinder.h).
#include "MeshCollisionVolume.h"

// FUNCTION: 0x00426320
inline BOOL CSphere::CollideWithMesh(void *a, CCollisionMove *move, CMeshCollisionVolume *other, CCollisionReport *report)
{
	return other->CollideWithSphere(move, (CCollisionMove *)a, this, report);
}

#endif
