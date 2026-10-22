// Sphere.cpp: sphere collision tests reconstructed from the pristine retail object.
// Specimen: BEA.exe.original.backup, SHA-256
// 74154bfae14ddc8ecb87a0766f5bc381c7b7f1ab334ed7a753040eda1e1e7750.
// All three collision bodies match; the sphere-to-sphere response uses labelled order stand-ins.

#include "common.h"
#include "Sphere.h"
#include "cylinder.h"
#include "Line.h"
#include "mathsconst.h"

// The initializer pair immediately precedes Sphere's collision bodies, after SpawnPoint.cpp.
// FUNCTION: 0x004e4aa0 init:ZERO_FVECTOR
// FUNCTION: 0x004e4ac0 init:ID_FMATRIX

//******************************************************************************************
// Exact section and relocation match. The closest point's squared distance has a float local before the
// inclusive tangency test; the earlier materialization/inlining discrepancy is closed by the current source.
BOOL CSphere::CollideWithLine(void *a, CCollisionMove *move, CLine *other, CCollisionReport *report)	// FUNCTION: 0x004e4b90
{
	CCollisionMove *ownMove = (CCollisionMove *)a;
	BOOL result = FALSE;
	FVector offset = move->mPos - ownMove->mPos;
	FVector start = offset + other->mStart;
	FVector end = offset + other->mEnd;
	if ((start.MagnitudeSq() < mRadiusSquared) || (end.MagnitudeSq() < mRadiusSquared))
		return TRUE;

	FVector delta = start - end;
	float fraction = (delta * start) / delta.MagnitudeSq();
	if ((fraction >= 0.0) && (fraction <= 1.0))
	{
		FVector closest = start + (end - start) * fraction;
		float closestDistance = closest.MagnitudeSq();
		if (closestDistance <= mRadiusSquared)
			result = TRUE;
	}
	return result;
}

//******************************************************************************************
BOOL CSphere::CollideWithCylinder(void *a, CCollisionMove *move, CCylinder *other, CCollisionReport *report)	// FUNCTION: 0x004e4d70
{
	CCylinder cylinder(mRadius, mRadius);
	return cylinder.CollideWithCylinder(a, move, other, report);
}

//******************************************************************************************
// The initial squared length is written out, products bracketed, as no inline call: an inline MagnitudeXYSq/MagnitudeSq
// spends 43 more of the budget, and retail then still inlines the deep branch's Normalise (00406d50 stays uncalled).
// Its terms are read Z, Y, X: the reads number initialSeparation's field symbols, and VC6 sorts the sum by them, so this
// gives retail's grouping (X*X + Y*Y) + Z*Z (004e4f2d) and retail's orders in both magnitudes of the separation
// (004e4fc5, 004e52ca); read X, Y, Z it is 36 bytes. Native-corpus notes on these groupings: 148 of 4512
// complete-entry cases (x87 PC24/53/64) separate XY-then-Z from a plain MagnitudeSq. The inline budget is exact at the
// deep branch's Normalise (103 left for 103), so any change of own size or of an inline site moves the code. The radius
// split, the named initialDistance and the reused dot (as CCylinder writes it) are natural steps.
//
//
// STAND-IN (spellings, no code): FOrderVector holds file-local copies of FVector's Magnitude, dot product and += that
// differ from maths.h only in bracketing, term order and (Dot2) being static, each used once below. VC6 sorts a sum's
// terms by rank key (the rank tool: a temporary's key is its number << 6, an IND's its base's low byte << 8 plus 7,
// a product's L + 2R + 0x47); a bracketed term or pair is added first.
// - Mag, the relative speed: X*X first, then Z, Y (004e4fab). The keys give Z, Y, X, because relativeMove.X's field
//   symbol comes from the free list operator- has just filled.
// - Dot, normal * move->mMove: the X, Z pair, then Y (004e5169). The keys give X, Y, Z.
// - Add1, move->mPos += move->mMove: the bracketed Z source is loaded first (004e520e).
// - Dot2, normal * ownMove->mMove: the Y, Z pair, then X (004e5217). It is static because the inliner frees a member's
//   this before its argument but a static's parameters last-first: normal's parameter id (0x1ae) stays on top of the
//   free list, passes through the operator* and operator+ below, and becomes the last +='s this. Its key class (6 mod 8)
//   then outranks the own move's base (a, id 5), so that += loads the position first (004e52ad).
struct FOrderVector : public FVector
{
	float	Mag() const { return sqrtf(((X * X)) + (Y * Y) + (Z * Z)); }
	float	Dot(const FVector &a) const { return ((X * a.X) + (Z * a.Z)) + (Y * a.Y); }
	static float	Dot2(const FVector &n, const FVector &a) { return ((n.Y * a.Y) + (n.Z * a.Z)) + (n.X * a.X); }
	void	Add1(const FVector &a) { X += a.X; Y += a.Y; Z += (a.Z); }
};

BOOL CSphere::CollideWithSphere(void *a, CCollisionMove *move, CSphere *other, CCollisionReport *report)	// FUNCTION: 0x004e4e00
{
	CCollisionMove *ownMove = (CCollisionMove *)a;
	FVector separation = move->mPos - ownMove->mPos;
	float combinedRadius = mRadius + other->mRadius;
	BOOL result = separation.Magnitude() - combinedRadius <= 0;
	if (report->mTestTriangles && result)
	{
		FVector initialSeparation = (move->mPos - move->mMove) - (ownMove->mPos - ownMove->mMove);
		float distanceSquared = (initialSeparation.Z * initialSeparation.Z) + (initialSeparation.Y * initialSeparation.Y) + (initialSeparation.X * initialSeparation.X);
		float radius = mRadius;
		radius += other->mRadius;
		FVector relativeMove = move->mMove - ownMove->mMove;
		float initialDistance = sqrtf(distanceSquared);
		if (initialDistance - radius >= -0.0001f)
		{
			float speed = static_cast<const FOrderVector &>(relativeMove).Mag();	// STAND-IN, see above
			float distance = -initialSeparation.Magnitude();
			float aa = speed * speed;
			float bb = 2 * (speed * distance);
			float fraction;
			if (aa == 0)
				fraction = 0;
			else
				fraction = (-bb - sqrtf(bb * bb - 4 * aa * (distance * distance - combinedRadius * combinedRadius))) / (2 * aa);
			if (fraction < 0.0)
				fraction = 0;

			move->mPos -= move->mMove * (1 - fraction);
			ownMove->mPos -= ownMove->mMove * (1 - fraction);
			FVector normal = move->mPos - ownMove->mPos;
			normal.Z = 0;
			normal.Normalise();
			float dot = static_cast<const FOrderVector &>(normal).Dot(move->mMove);	// STAND-IN, see above
			if (dot < 0)
				move->mMove = move->mMove + normal * -dot;
			static_cast<FOrderVector &>(move->mPos).Add1(move->mMove);	// STAND-IN, see above
			dot = FOrderVector::Dot2(normal, ownMove->mMove);	// STAND-IN, see above
			if (dot > 0)
				ownMove->mMove = ownMove->mMove + normal * -dot;
			ownMove->mPos += ownMove->mMove;
		}
		else
		{
			FVector normal = initialSeparation;
			float distance = normal.Magnitude();
			normal.Normalise();
			float overlap = mRadius + other->mRadius - distance;
			if (overlap < 0.0)
				return FALSE;
			float halfOverlap = overlap * 0.5f;
			FVector otherCorrection = normal * halfOverlap;
			FVector ownCorrection = -(normal * halfOverlap);
			otherCorrection.Z = 0;
			ownCorrection.Z = 0;
			move->mPos -= move->mMove;
			move->mPos += otherCorrection;
			ownMove->mPos -= ownMove->mMove;
			ownMove->mPos += ownCorrection;
		}
		ownMove->mStopped = TRUE;
		move->mStopped = TRUE;
		return TRUE;
	}
	return result;
}
