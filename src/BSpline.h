// BSpline.h: the pan cameras' spline (Stuart's header is not in his release; rebuilt from BSpline.obj,
// 00416d10..0041709f). RTTI: CBSpline (vtable 005d8e00, one slot: the destructor).

#ifndef BSPLINE_H
#define BSPLINE_H
#include "maths.h"

#include "SPtrSet.h"

class CBSpline
{
public:
	CBSpline(SPtrSet<FVector> *points, int order = 3);	// 00416d10 (0x14 bytes; CPlayer::GotoPanView passes 3, 004d2f14)
	virtual ~CBSpline();								// 00416da0
	FVector	GetPoint(float t);						// 00416fc0

protected:
	float	BasisFunction(int i, int k, float t);		// 00416e30

	SPtrSet<FVector>	*mPoints;					// +0x04
	int					*mKnots;					// +0x08: mNum + mOrder + 1 of them
	int					mOrder;						// +0x0c
	int					mNum;						// +0x10: the last point's index
};

#endif
