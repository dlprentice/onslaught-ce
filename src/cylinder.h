// cylinder.h: CCylinder (vtable 005d88cc). Stuart's BattleEngine.cpp makes one as
// new(MEMTYPE_BATTLEENGINE) CCylinder(radius, COfGHeight() * 0.5f). Only what matched code needs: the centre +0x04,
// the radius +0x14 and the height +0x1c (CMeshCollisionVolume::CollideWithCylinder 004abe68..004abeb8).

#ifndef CYLINDER_H
#define CYLINDER_H

#include "GeneralVolume.h"

class CCylinder : public CGeneralVolume
{
public:
	virtual BOOL	CollideWithMesh(void *a, CCollisionMove *move, CMeshCollisionVolume *other, CCollisionReport *report);	// 1 (004059a0)
	virtual BOOL	CollideWithCylinder(void *a, CCollisionMove *move, CCylinder *other, CCollisionReport *report);		// 2 (0043fe20)
	virtual BOOL	CollideWithSphere(void *a, CCollisionMove *move, CSphere *other, CCollisionReport *report);			// 3 (004059a0)
	virtual BOOL	CollideWithLine(void *a, CCollisionMove *move, CLine *other, CCollisionReport *report);				// 4 (00440510)
	virtual BOOL	Collide(void *a, CCollisionMove *move, CGeneralVolume *other, CCollisionReport *report);				// 5 (004059a0)
	virtual SINT	GetType() { return 2; }															// 6 (004059c0)

	// Stuart's (CBattleEngine::SetCollisionShape): the radius and half the height (0x20 bytes, 004063fe..0040642f).
	CCylinder(float radius, float half_height) : mRadius(radius), mRadiusSquared(radius * radius), mHeight(half_height) {}
	CCylinder() {}
	CCylinder(class CBoundingBox *box);		// 0043fde0: box axes +0x10/+0x14 set radius, +0x18 sets half-height

	float		mRadius;					// +0x14
	float		mRadiusSquared;					// +0x18: the radius squared
	float		mHeight;					// +0x1c
};

// Slots 1, 3 and 5 fold to one body in BattleEngine.obj (004059a0), the first object that emits CCylinder's vtable:
// swap the movement records and ask the other volume to test this cylinder.
#include "MeshCollisionVolume.h"
#include "Sphere.h"

// FUNCTION: 0x004059a0
inline BOOL CCylinder::CollideWithMesh(void *a, CCollisionMove *move, CMeshCollisionVolume *other, CCollisionReport *report)
{
	return other->CollideWithCylinder(move, (CCollisionMove *)a, this, report);
}

// FUNCTION: 0x004059a0
inline BOOL CCylinder::CollideWithSphere(void *a, CCollisionMove *move, CSphere *other, CCollisionReport *report)
{
	return other->CollideWithCylinder(move, (CCollisionMove *)a, this, report);
}

// FUNCTION: 0x004059a0
inline BOOL CCylinder::Collide(void *a, CCollisionMove *move, CGeneralVolume *other, CCollisionReport *report)
{
	return other->CollideWithCylinder(move, (CCollisionMove *)a, this, report);
}

#endif
