// Line.h: CLine (0x34 bytes, vtable 005d8bfc), a collision volume for a line from mStart to mEnd. Stuart's release
// has no Line.h; his callers build one from two points (CLine ray(pos, pos + dir), BattleEngine.cpp). The
// Endpoint ctor 0040d470 zeros base XYZ; copy ctor 004098e0 copies all four base words and both endpoints.

#ifndef LINE_H
#define LINE_H

#include "GeneralVolume.h"

class CLine : public CGeneralVolume
{
public:
	CLine(const FVector &start, const FVector &end) : mStart(start), mEnd(end) {}

	virtual BOOL	CollideWithMesh(void *a, CCollisionMove *move, CMeshCollisionVolume *other, CCollisionReport *report);	// 1
	virtual BOOL	CollideWithCylinder(void *a, CCollisionMove *move, CCylinder *other, CCollisionReport *report);		// 2
	virtual BOOL	CollideWithSphere(void *a, CCollisionMove *move, CSphere *other, CCollisionReport *report);			// 3
	virtual BOOL	CollideWithLine(void *a, CCollisionMove *move, CLine *other, CCollisionReport *report);				// 4
	virtual BOOL	Collide(void *a, CCollisionMove *move, CGeneralVolume *other, CCollisionReport *report);				// 5
	virtual SINT	GetType() { return 1; }																	// 6 (inline; survivor 004014a0)

	FVector		mStart;						// +0x14
	FVector		mEnd;						// +0x24
};

#endif
