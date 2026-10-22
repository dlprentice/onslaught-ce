// Cylinder.cpp: reconstructed from the pristine retail cylinder object.
// Every function here matches; the cylinder response does so with labelled stand-ins (FOrderVector).
// Specimen: BEA.exe.original.backup, SHA-256
// 74154bfae14ddc8ecb87a0766f5bc381c7b7f1ab334ed7a753040eda1e1e7750.

#include "common.h"
#include "cylinder.h"
#include "Line.h"
#include "mesh.h"
#include "mathsconst.h"

// The per-file maths pair lies after Cutscene.cpp and before the cylinder constructor.
// FUNCTION: 0x0043fcf0 init:ZERO_FVECTOR
// FUNCTION: 0x0043fd10 init:ID_FMATRIX

// FUNCTION: 0x0043fde0
CCylinder::CCylinder(CBoundingBox *box)
{
	mHeight = box->mAxes.Z;
	mRadiusSquared = box->mAxes.MagnitudeXYSq();
	mRadius = sqrtf(mRadiusSquared);
}

// STAND-IN: FOrderVector, file-local copies of FVector::MagnitudeXYSq, operator*(const FVector &)
// and operator+=, used at four sites of CollideWithCylinder below; it adds no members. The brackets emit no code: each
// is an x87 tuple that raises the bracketed term in VC6's ranking of a sum (height first, then field or temporary
// ids) and takes one optimizer temporary. Retail's orders that maths.h's bodies do not give here: the relative speed
// squares X first (0044004a); the radial approach and the second dot rank their X products first and the second dot
// adds Z before Y (0043ffb1, 00440326); the move's += loads the move first (00440314). The speed copy's fifteen
// brackets on X * X also number the contact normal's Y and Z copies 0x459 and 0x45c (twelve temporaries and one
// 32-id block later than one bracket), so its inlined Normalise ranks Z * Z first (0044020a). Original spellings
// unknown; replace these with natural forms that give the same orders and numbers.
struct FOrderVector : public FVector
{
	float	MagnitudeXYSq() const { return (((((((((((((((X * X))))))))))))))) + (Y * Y); }
	float	Dot(const FVector &a) const { return ((X * a.X)) + (Y * a.Y) + ((Z * a.Z)); }
	void	Add(const FVector &a) { X += a.X; Y += (a.Y); Z += a.Z; }
};

// Matched (stand-ins FOrderVector above and the bracket on the first dot below). The approach tests
// are dot products. The vertical arm zeroes a copy of the separation, so relative's X and Y are still computed there
// (the dead stores at 0043ff72..0043ff84) and reach relativeMove's out-of-line constructor at 0043ffde..0043fff0 from
// both arms; the radial arm zeroes relative.Z. `vertical` declared after the separation keeps its Z store and reload
// (0043ff1a), `(1 - fraction)` at each rewind keeps the scale on the x87 stack (004400d0), and a separate `normal`
// gives the contact normal's operand order (00440202..00440255).
// FUNCTION: 0x0043fe20
BOOL CCylinder::CollideWithCylinder(void *a, CCollisionMove *move, CCylinder *other, CCollisionReport *report)
{
	CCollisionMove *ownMove = (CCollisionMove *)a;
	FVector separation = move->mPos - ownMove->mPos;
	float radius = other->mRadius + mRadius;
	float height = other->mHeight + mHeight;
	BOOL result = FALSE;
	if ((sqrtf(separation.X * separation.X + separation.Y * separation.Y) - radius <= 0) && (fabsf(move->mPos.Z - ownMove->mPos.Z) <= height))
		result = TRUE;
	if (report && report->mTestTriangles && result)
	{
		FVector ownStart = ownMove->mPos - ownMove->mMove;
		FVector otherStart = move->mPos - move->mMove;
		FVector initialSeparation = otherStart - ownStart;
		BOOL vertical = FALSE;
		float verticalDistance = initialSeparation.Z - height;
		// Retail stores Y*Y before adding X*X (0043ff2e..0043ff42), then reuses both stored squares
		// in deep normalization. This direct expression preserves that rounding and the overlap cutoff.
		float radialLength = sqrtf(initialSeparation.X * initialSeparation.X + initialSeparation.Y * initialSeparation.Y);
		float radialDistance = radialLength - radius;
		float distance;
		float approach;
		if (verticalDistance > radialDistance)
		{
			distance = verticalDistance;
			vertical = TRUE;
			FVector relative = move->mMove - ownMove->mMove;
			FVector normal = initialSeparation;
			normal.X = 0;
			normal.Y = 0;
			approach = relative * normal;
		}
		else
		{
			distance = radialDistance;
			FVector relative = move->mMove - ownMove->mMove;
			relative.Z = 0;
			approach = static_cast<const FOrderVector &>(relative).Dot(initialSeparation);	// STAND-IN for relative * initialSeparation
		}
		if (approach > 0)
			return FALSE;
		FVector relativeMove = move->mMove - ownMove->mMove;
		if (distance >= -0.0001f)
		{
			float initialDistance;
			float cc;
			if (vertical)
			{
				initialDistance = -initialSeparation.Z;
				cc = initialDistance * initialDistance - height * height;
			}
			else
			{
				initialDistance = -radialLength;
				cc = initialDistance * initialDistance - radius * radius;
			}
			float speed = sqrtf(static_cast<const FOrderVector &>(relativeMove).MagnitudeXYSq());	// STAND-IN for relativeMove.MagnitudeXYSq()
			float aa = speed * speed;
			float bb = 2 * speed * initialDistance;
			float fraction;
			if (aa == 0)
				fraction = 0;
			else
				fraction = (-bb - sqrtf(bb * bb - 4 * aa * cc)) / (2 * aa);
			if (fraction < 0.0)
				fraction = 0;
			move->mPos -= move->mMove * (1 - fraction);
			ownMove->mPos -= ownMove->mMove * (1 - fraction);
			FVector normal;
			if (!vertical)
			{
				FVector aligned = move->mPos;
				aligned.Z = ownMove->mPos.Z;
				normal = ownMove->mPos - aligned;
			}
			else
			{
				normal = move->mPos - ownMove->mPos;
				normal.X = 0;
				normal.Y = 0;
			}
			normal.Normalise();
			float dot = (normal * move->mMove);	// STAND-IN bracket: its tuple schedules the last faddp before the copy's first store (0044027b)
			report->mNormals[0] = normal;
			if (dot > 0)
				move->mMove = move->mMove + normal * -dot;
			static_cast<FOrderVector &>(move->mPos).Add(move->mMove);	// STAND-IN for move->mPos += move->mMove
			dot = static_cast<const FOrderVector &>(normal).Dot(ownMove->mMove);	// STAND-IN for normal * ownMove->mMove
			if (dot < 0)
				ownMove->mMove = ownMove->mMove + normal * -dot;
			ownMove->mPos += ownMove->mMove;
		}
		else
		{
			FVector normal = initialSeparation;
			normal.Normalise();
			float halfDistance = distance * 0.5f;
			FVector otherCorrection = -(normal * halfDistance);
			FVector ownCorrection = normal * halfDistance;
			otherCorrection.Z = 0;
			ownCorrection.Z = 0;
			move->mPos += otherCorrection;
			ownMove->mPos += ownCorrection;
		}
		ownMove->mStopped = TRUE;
		move->mStopped = TRUE;
		return TRUE;
	}
	return result;
}

// The unary vector negation emitted between the two collision bodies.
// FUNCTION: 0x004404f0 ??GFVector@@QBE?AV0@XZ

// FUNCTION: 0x00440510
BOOL CCylinder::CollideWithLine(void *a, CCollisionMove *move, CLine *other, CCollisionReport *report)
{
	CCollisionMove *ownMove = (CCollisionMove *)a;
	// Retail's source spends a few more expression nodes here than ours did (a named reference before the first vector
	// operator does it), which sends one FVector constructor and one sum out of line later in the function.
	const FVector &pos = move->mPos;
	FVector offset = pos - ownMove->mPos;
	FVector start = offset + other->mStart;
	FVector end = offset + other->mEnd;
	if (((start.Z > mHeight) && (end.Z > mHeight)) || ((start.Z < -mHeight) && (end.Z < -mHeight)))
		return FALSE;
	FVector delta = start - end;
	if (start.MagnitudeSq() > mRadiusSquared)
	{
		if (end.MagnitudeSq() > mRadiusSquared)	// two ifs: `&&` here spills one squared term
		{
			float fraction = (delta * start) / delta.MagnitudeSq();
			if ((fraction < 0.0) || (fraction > 1.0))
				return FALSE;
			FVector closest = start + (end - start) * fraction;
			float closestDistance = closest.MagnitudeXYSq();
			if (closestDistance > mRadiusSquared)
				return FALSE;
		}
	}
	if ((fabsf(start.Z) > mHeight) || (fabsf(end.Z) > mHeight) || report->mTestTriangles)
	{
		float speed = (end - start).Magnitude();
		float distance = -start.Magnitude();
		float aa = speed * speed;
		float bb = 2 * (speed * distance);
		float root = sqrtf(bb * bb - 4 * aa * (distance * distance - mRadiusSquared));
		float firstFraction = (-bb - root) / (2 * aa);
		float lastFraction = (-bb + root) / (2 * aa);
		if (firstFraction < 0)
			firstFraction = 0;
		else if (firstFraction > 1)
			firstFraction = 1;
		if (lastFraction < 0)
			lastFraction = 0;
		else if (lastFraction > 1)
			lastFraction = 1;
		FVector first = start + (-delta) * firstFraction;
		FVector last = start + (-delta) * lastFraction;
		if (((first.Z > mHeight) && (last.Z > mHeight)) || ((first.Z < -mHeight) && (last.Z < -mHeight)))
			return FALSE;
		if (report->mTestTriangles)
		{
			move->mPos = move->mPos + (first - offset);
			move->mStopped = TRUE;
		}
	}
	return TRUE;
}
