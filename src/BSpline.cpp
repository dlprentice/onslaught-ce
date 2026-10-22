// BSpline.cpp: reconstructed from BSpline.obj (00416d10..0041709f; it has no dynamic initializers); Stuart's release has no copy. The pan cameras'
// uniform B-spline through a set of points: open uniform integer knots, the Cox-de Boor basis evaluated recursively,
// and the curve point as the basis-weighted sum of the points (the same scheme as Paul Bourke's SplineKnots,
// SplineBlend and SplinePoint). The allocation's __LINE__ is the retail one; the #line places it.

#include "common.h"
#include "BSpline.h"
#include "maths.h"

//******************************************************************************************
CBSpline::CBSpline(SPtrSet<FVector> *points, int order)	// FUNCTION: 0x00416d10
	: mPoints(points), mOrder(order)
{
	mNum = points->Size() - 1;

#line 19
	mKnots = new(MEMTYPE_SCRATCHPAD) int[mNum + mOrder + 1];

	for (int j = 0; j <= mNum + mOrder; j++)
	{
		if (j < mOrder)
			mKnots[j] = 0;
		else if (j <= mNum)
			mKnots[j] = j - mOrder + 1;
		else
			mKnots[j] = mNum - mOrder + 2;
	}
}

//******************************************************************************************
// FUNCTION: 0x00416da0 ??_GCBSpline@@UAEPAXI@Z
CBSpline::~CBSpline()
{
	delete [] mKnots;

	if (mPoints)
		mPoints->DeleteAll();
	delete mPoints;

	mKnots = NULL;
	mPoints = NULL;
}

//******************************************************************************************
// The basis function of point i and order k at t.
float CBSpline::BasisFunction(int i, int k, float t)	// FUNCTION: 0x00416e30
{
	if (k == 1)
	{
		if ((mKnots[i] <= t) && (t < mKnots[i + 1]))
			return 1.0f;
		return 0.0f;
	}

	if ((mKnots[i + k - 1] == mKnots[i]) && (mKnots[i + k] == mKnots[i + 1]))
		return 0.0f;

	if (mKnots[i + k - 1] == mKnots[i])
		return (mKnots[i + k] - t) / (mKnots[i + k] - mKnots[i + 1]) * BasisFunction(i + 1, k - 1, t);

	if (mKnots[i + k] == mKnots[i + 1])
		return (t - mKnots[i]) / (mKnots[i + k - 1] - mKnots[i]) * BasisFunction(i, k - 1, t);

	return (t - mKnots[i]) / (mKnots[i + k - 1] - mKnots[i]) * BasisFunction(i, k - 1, t) +
		   (mKnots[i + k] - t) / (mKnots[i + k] - mKnots[i + 1]) * BasisFunction(i + 1, k - 1, t);
}

//******************************************************************************************
// The curve at t, 0 at the first point and 1 at the last.
FVector CBSpline::GetPoint(float t)	// FUNCTION: 0x00416fc0
{
	FVector	result(0, 0, 0);

	t *= mNum - mOrder + 2;

	FVector	*point = mPoints->First();

	for (int i = 0; i <= mNum; i++)
	{
		float	b = BasisFunction(i, mOrder, t);

		result.X += point->X * b;
		result.Y += point->Y * b;
		result.Z += point->Z * b;

		point = mPoints->Next();
	}

	return result;
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x00406d30 ?Next@GenericSPtrSet@@QAEPAXXZ
