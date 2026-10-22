// Geometry.cpp: collision helpers reconstructed from the pristine retail instructions.
// Specimen SHA-256: 74154bfae14ddc8ecb87a0766f5bc381c7b7f1ab334ed7a753040eda1e1e7750.
// The descriptive helper names are reconstruction names; Stuart's release omits this object.
#include "common.h"
#include "Geometry.h"
#include "mathsconst.h"
#include "DebugLog.h"

// Retail data at 0062c98c/90/94. Descriptive names; the values are read from the specimen.
SINT GEOMETRY_OUTSIDE_X = 1;
SINT GEOMETRY_OUTSIDE_Y = 2;
SINT GEOMETRY_OUTSIDE_Z = 4;

// FUNCTION: 0x00478000 init:ZERO_FVECTOR
// FUNCTION: 0x00478020 init:ID_FMATRIX

// FUNCTION: 0x004780f0
BOOL CGeneralVolume::TestAtOffset(CGeneralVolume *other, FVector offset, CCollisionReport *report)
{
	// The line-query caller at 0050b299 supplies the negated volume/world offset.
	// Retail initializes the positions/stopped flags only; movement words are left untouched.
	CCollisionMove own;
	CCollisionMove incoming;
	incoming.mStopped = own.mStopped = FALSE;
	own.mPos = FVector(0, 0, 0);
	incoming.mPos = offset;
	return Collide(&own, &incoming, other, report);
}

// Shared outcode calculation, expanded at 0047816b, 00478204, 004783e8 and 00478478.
// No separate retail function identity is claimed for this reconstruction helper.
inline SINT SegmentOutCode(const float &x, const float &y, const float &z, const float *bounds)
{
	SINT code = 0;
	if (y > bounds[3]) code |= 1;
	else if (y < bounds[1]) code |= 2;
	if (x > bounds[2]) code |= 4;
	else if (x < bounds[0]) code |= 8;
	if (z > bounds[5]) code |= 16;
	else if (z < bounds[4]) code |= 32;
	return code;
}

// `fraction` is a local of each branch: declared once at the top of the loop it gets a stack slot of its own, where retail
// packs it with y and z.
// FUNCTION: 0x00478160
bool ClipSegmentAgainstAABB3D(float *startX, float *startY, float *startZ,
							 float *endX, float *endY, float *endZ, const float *bounds)
{
	SINT first = SegmentOutCode(*startX, *startY, *startZ, bounds);
	SINT second = SegmentOutCode(*endX, *endY, *endZ, bounds);
	while (first | second)
	{
		if (first & second)
			return false;
		SINT code = first ? first : second;
		float x, y, z;
		if (code & 1)
		{
			float fraction = (bounds[3] - *startY) / (*endY - *startY);
			x = *startX + (*endX - *startX) * fraction;
			z = *startZ + (*endZ - *startZ) * fraction;
			y = bounds[3];
		}
		else if (code & 2)
		{
			float fraction = (bounds[1] - *startY) / (*endY - *startY);
			x = *startX + (*endX - *startX) * fraction;
			z = *startZ + (*endZ - *startZ) * fraction;
			y = bounds[1];
		}
		else if (code & 4)
		{
			x = bounds[2];
			float fraction = (bounds[2] - *startX) / (*endX - *startX);
			y = *startY + (*endY - *startY) * fraction;
			z = *startZ + (*endZ - *startZ) * fraction;
		}
		else if (code & 8)
		{
			x = bounds[0];
			float fraction = (bounds[0] - *startX) / (*endX - *startX);
			y = *startY + (*endY - *startY) * fraction;
			z = *startZ + (*endZ - *startZ) * fraction;
		}
		else if (code & 16)
		{
			z = bounds[5];
			float fraction = (bounds[5] - *startZ) / (*endZ - *startZ);
			x = *startX + (*endX - *startX) * fraction;
			y = *startY + (*endY - *startY) * fraction;
		}
		else
		{
			z = bounds[4];
			float fraction = (bounds[4] - *startZ) / (*endZ - *startZ);
			x = *startX + (*endX - *startX) * fraction;
			y = *startY + (*endY - *startY) * fraction;
		}
		if (code == first)
		{
			*startX = x; *startY = y; *startZ = z;
			first = SegmentOutCode(*startX, *startY, *startZ, bounds);
		}
		else
		{
			*endX = x; *endY = y; *endZ = z;
			second = SegmentOutCode(*endX, *endY, *endZ, bounds);
		}
	}
	return true;
}

// Common plane-distance calculation expanded by both swept-triangle projection paths.
// This source helper has no independently identified retail entry.
inline float TrianglePlaneDistance(const FVector &normal, const FVector &point,
								 const FVector &start, const FVector &direction)
{
	float distance = (-normal) * point;
	distance = (normal * start) + distance;
	float denominator = normal * direction;
	if (denominator == 0)
		return -1;
	return -(distance / denominator);
}

// STAND-IN for FVector::operator*(const FVector &) at TestSweptSphereAgainstTriangle's two plane-side dots
// only. The body names a.Z before a.Y. VC6 creates an argument's .Y/.Z field symbols in the order
// the inlined body makes their addresses, so maths.h's X, Y, Z body gives fromStart.Y and fromFront.Y the lower
// ids and the Z product sorts first. Retail adds the Y product first (004786b1, 0047877c); this order gives
// that. The sum is still ranked as a whole, so the written term order changes nothing else here. The same order
// in maths.h matches this function but loses nine others (measured: 5,416 against 5,424), hence this
// file-local copy, a member so that the normal stays the `this` operand as with operator*.
// Original spelling unknown: replace it with a natural form that gives the same field-creation order.
struct FDotVector : public FVector
{
	float	Dot(const FVector &a) const { return (X * a.X) + (Z * a.Z) + (Y * a.Y); }
};

// Matched (one labelled stand-in, FDotVector::Dot above, at 004786b1 and 0047877c): the shared
// calculation preserves retail float32 plane/numerator stores (004787cb/004787ea and 00478a77/00478a96) around
// the out-of-line dot calls. fromStart is a named vector (its own frame slot, 0xcc frame); the snapped plane
// distance is copied into contactDistance, so retail keeps it on the FPU through the snap (00478894..004788c1);
// one valid flag sends both failed edge-hit tests and the farther-contact test to the shared return (00478ba3).
// FUNCTION: 0x00478510
BOOL TestSweptSphereAgainstTriangle(const FVector *a, const FVector *b, const FVector *c,
								   const FVector *pos, const FVector *move, float radius,
								   CCollisionReport *report)
{
	BOOL overlapping = FALSE;
	BOOL valid = TRUE;
	FVector firstEdge = *b - *a;
	FVector secondEdge = *c - *a;
	FVector normal = firstEdge ^ secondEdge;
	normal.Normalise();
	if ((normal * *move) >= 0.01f)
		return FALSE;
	FVector fromStart = *a - *pos;
	if (static_cast<const FDotVector &>(normal).Dot(fromStart) >= 0.01f)	// STAND-IN for normal * fromStart
		return FALSE;
	FVector front = *pos - normal * radius;
	FVector fromFront = *a - front;
	float distance;
	FVector projected;
	if (static_cast<const FDotVector &>(normal).Dot(fromFront) > 0.0001f)	// STAND-IN for normal * fromFront
	{
		distance = TrianglePlaneDistance(normal, *a, front, normal);
		FVector offset = normal * distance;
		projected = front + offset;
	}
	else
	{
		FVector direction = *move;
		direction.Normalise();
		distance = TrianglePlaneDistance(normal, *a, front, direction);
		FVector offset = direction * distance;
		projected = front + offset;
		// The square is spilled to float32 across the out-of-line MagnitudeSq call (00478b21).
		if (distance * distance > move->MagnitudeSq())
			return FALSE;
	}
	// Snapping is after the point calculation and is not repeated after the edge fallback.
	if (fabsf(distance) < 0.0001)
		distance = 0;
	float contactDistance = distance;
	FVector contact = projected;
	if (!PointInsideTrianglePrism(&projected, a, b, c, &normal))
	{
		contact = SelectClosestPointOnTriangleEdges(a, b, c, &projected);
		FVector rawMove = *move;
		FVector rayStart = contact - *pos;
		FVector rayEnd = (contact - rawMove) - *pos;
		contactDistance = RaySphereEntryDistance(&rayStart, &rayEnd, radius);
	}
	if ((contact - *pos).Magnitude() - radius < -0.0001f)
	{
		report->mStuck = TRUE;
		overlapping = TRUE;
	}
	else if ((contactDistance < -0.0001) || (move->Magnitude() < contactDistance))
		valid = FALSE;
	if (valid && report->mUnknownA8 && contactDistance >= report->mDistance && overlapping != TRUE)
		valid = FALSE;
	if (!valid)
		return FALSE;
	report->mDistance = contactDistance;
	report->mUnknownA8 = TRUE;
	report->mUnknown94 = TRUE;
	report->mContactPos = contact;
	report->mTriangleNormal = normal;
	return TRUE;
}

// STAND-IN (operand order): the first two retail prism tests sum Y/Z before X (0047908f..a4, 00479138..4f).
// The third calls the shared dot at 004791cc. This inline reconstruction helper has no separate retail identity; the
// plain `normal * cross` loses TestSegmentAgainstTriangle (243 bytes) and IsDirectionInsideTrianglePrism (129).
static inline float PrismDot(const FVector &normal, const FVector &cross)
{
	return ((normal.Y * cross.Y) + (normal.Z * cross.Z)) + (normal.X * cross.X);
}


// STAND-IN for 00478c20 below (operand order, pool A): retail's facing test sums (Z + X) + Y over delta's copies on the
// FPU stack (00478d1b..00478d2b); maths.h's `normal * delta` sums (Z + Y) + X. Reading delta through this helper's
// reference argument also folds delta.Y/Z at loop 1 (pass 6), which pops the last pool-A ids the inlined prism freed,
// so the prism's first block gets fresh field ids and retail's operand orders (00478e5d..00478e7b). __forceinline
// spends no inline budget (a plain inline costs 54). Emits no code of its own.
static __forceinline float FaceDot(const FVector &n, const FVector &d)
{
	return ((n.X * d.X) + (n.Z * d.Z)) + (n.Y * d.Y);
}

// STAND-IN for 00478c20 below (normalisation): retail squares the normal's Y and Z from stack copies and X from memory
// (00478ca6..00478cbc), so Y and Z are loads through `this` that the CSE pass turns into temporaries. X^2 first needs
// both squares' rank keys (3 * (id << 6) + 0x47, 16-bit) below X^2's 0x3a7. This unbracketed sum puts the two
// temporaries 2 walk nodes apart (Z t2ad, Y t2af once the base is 0x280); maths.h's bracketed Magnitude puts them 3
// apart, which sorts Y^2 before X^2. __forceinline spends no inline budget (a plain inline costs 57).
struct FMagVector : public FVector
{
	__forceinline float	Magnitude() const { return sqrtf(X * X + Y * Y + Z * Z); }
};

// STAND-IN for 00478c20 below (budget): FVector's three-float constructor without its inline cost (maths.h's costs 47).
// delta and contact are built without naming their fields there, so FaceDot's argument and the prism's point fold them
// at loop 1 (see FaceDot).
struct FMakeVector : public FVector
{
	__forceinline FMakeVector(float x, float y, float z) { X = x; Y = y; Z = z; }
};

// Written in components where retail computes in place: the normal's cross product (retail subtracts on demand, sharing
// the subtractions between components: b.Y - a.Y, c.Z - a.Z, b.Z - a.Z, c.Y - a.Y for X, then c.X - a.X and b.X - a.X;
// 00478c32..00478ca2), the scaling, the approach, the plane and the hit distance. The inline PointInsideTrianglePrism
// above is expanded here as in retail (00478e35..00478f92: every operator and nested constructor expanded, only the final
// dot called at 0x40d180), while its out-of-line copy stays 00479020 for the sphere test.
// delta is read only by the facing test; the approach and the contact write (end - start) again. Retail computes the
// three subtractions once, stores them with fst into the dead a, c and end homes and keeps them on the stack for the
// facing test; the approach (Z + Y) + X and the hit point read the stored values (00478d3a..00478d50, 00478df9..
// 00478e2e). That gives dx/dy the four references before the prism that put T6/T1/T3 (b - a) in slots of their own
// (frame 0x4c).
// Matched with the labelled stand-ins FaceDot, FMagVector and FMakeVector above and:
// STAND-IN (budget fit; every spelling emits no code): retail's inline decisions need own size 756..760 with no budget
// spent before the prism (the last cross's constructor expanded, the final dot denied). This text is 757 through the
// named, consumed temporaries facing, parallel, hit, cx/cy/cz and inside; braces on the single-statement returns,
// `report != NULL`, `m != 0.0f` and `inside == FALSE`; and brackets on delta's arguments, the approach's Y + Z group,
// the plane's terms, the hit terms and the contact products. The brackets are IL temporaries too: they lift the CSE
// walk's base from 0x260 to 0x280, which the normalisation's square keys need (FMagVector). Without the named
// temporaries own 723 (44/50); without the brackets own 743 (58/102); with plain helpers 91/293.
// FUNCTION: 0x00478c20
BOOL TestSegmentAgainstTriangle(const FVector *a, const FVector *b, const FVector *c,
								const FVector *start, const FVector *end, CCollisionReport *report)
{
	FVector normal;
	normal.X = (b->Y - a->Y) * (c->Z - a->Z) - (b->Z - a->Z) * (c->Y - a->Y);
	normal.Y = (b->Z - a->Z) * (c->X - a->X) - (b->X - a->X) * (c->Z - a->Z);
	normal.Z = (b->X - a->X) * (c->Y - a->Y) - (b->Y - a->Y) * (c->X - a->X);
	float	m = static_cast<const FMagVector &>(normal).Magnitude();
	if (m != 0.0f)
	{
		m = 1.0f / m;
		normal.X = normal.X * m;
		normal.Y = normal.Y * m;
		normal.Z = normal.Z * m;
	}
	// Retail 00478cff..00478d54 keeps the facing test separate, then recomputes
	// from the stored direction without rounding the dot before its 0.005f test.
	FMakeVector delta((end->X - start->X), (end->Y - start->Y), (end->Z - start->Z));
	float	facing = FaceDot(normal, delta);
	if (facing > 0.0)
	{
		return FALSE;
	}
	float approach = (normal.X * (end->X - start->X)) + ((normal.Y * (end->Y - start->Y)) + (normal.Z * (end->Z - start->Z)));
	float	parallel = fabsf(approach);
	if (parallel < 0.005f)
	{
		return FALSE;
	}
	float plane = ((-normal.X * a->X) - (normal.Y * a->Y)) - (normal.Z * a->Z);
	// Retail adds X/Z/Y after plane (00478d8c..00478da4); grouping preserves cancellation and hit admission.
	float	hit = ((plane + (normal.X * start->X)) + (normal.Z * start->Z)) + (normal.Y * start->Y);
	float distance = -(hit / approach);
	if ((distance < 0) || (distance > 1))
	{
		return FALSE;
	}
	if ((report != NULL) && report->mUnknownA8 && distance > report->mDistance)
		return FALSE;
	// Retail 00478df9..00478e31 adds each product before its float32 contact store.
	// A scaled FVector temporary rounds Y/Z products too early in this caller.
	float	cx = start->X + ((end->X - start->X) * distance);
	float	cy = start->Y + ((end->Y - start->Y) * distance);
	float	cz = start->Z + ((end->Z - start->Z) * distance);
	FMakeVector contact(cx, cy, cz);
	BOOL	inside = PointInsideTrianglePrism(&contact, a, b, c, &normal);
	if (inside == FALSE)
	{
		return FALSE;
	}
	if (report != NULL)
	{
		report->mDistance = distance;
		report->mUnknownA8 = TRUE;
		report->mUnknown94 = TRUE;
		report->mContactPos = contact;
		report->mTriangleNormal = normal;
	}
	return TRUE;
}


// The closest point to a point on the segment from start to end; retail expands it three times into
// SelectClosestPointOnTriangleEdges, whose inline budget sends its FVector operators out of line.
static inline FVector ClosestPointOnSegment(const FVector &start, const FVector &end, const FVector &point)
{
	FVector offset = point - start;
	FVector direction = end - start;
	float lengthSq = direction.MagnitudeSq();
	direction.Normalise();
	float projection = direction * offset;
	if (projection < 0)
		return start;
	if (projection * projection > lengthSq)
		return end;
	else
	{
		direction = direction * projection;
		return start + direction;
	}
}

// FUNCTION: 0x00479020
inline BOOL PointInsideTrianglePrism(const FVector *point, const FVector *a, const FVector *b,
							 const FVector *c, const FVector *normal)
{
	FVector froma = *a - *point;
	FVector edgea = *b - *a;
	FVector crossa = froma ^ edgea;
	if (PrismDot(*normal, crossa) < 0.0)
		return FALSE;
	FVector fromb = *b - *point;
	FVector edgeb = *c - *b;
	FVector crossb = fromb ^ edgeb;
	if (PrismDot(*normal, crossb) < 0.0)
		return FALSE;
	FVector fromc = *c - *point;
	FVector edgec = *a - *c;
	FVector crossc = fromc ^ edgec;
	if ((*normal * crossc) < 0.0)
		return FALSE;
	return TRUE;
}

// Declaring then assigning delta retains the retail temporary and the third distance's out-of-line MagnitudeSq call.
// Product parentheses retain VC6's value boundaries: the last two result-copy loads follow the third square
// (004795c6/004795ca), as in retail. The additions keep their original association.
// FUNCTION: 0x00479200
FVector SelectClosestPointOnTriangleEdges(const FVector *a, const FVector *b,
										const FVector *c, const FVector *point)
{
	// The retail edge order is B->A, A->C, C->B; equal distances keep the earlier edge.
	FVector first = ClosestPointOnSegment(*b, *a, *point);
	FVector second = ClosestPointOnSegment(*a, *c, *point);
	FVector third = ClosestPointOnSegment(*c, *b, *point);
	FVector delta;
	delta = *point - first;
	float secondDistanceSq = (*point - second).MagnitudeSq();
	float thirdDistanceSq = (*point - third).MagnitudeSq();
	float bestDistanceSq = (delta.X * delta.X) + (delta.Y * delta.Y) + (delta.Z * delta.Z);
	FVector result = first;
	if (secondDistanceSq < bestDistanceSq)
	{
		result = second;
		bestDistanceSq = secondDistanceSq;
	}
	if (thirdDistanceSq < bestDistanceSq)
		result = third;
	return result;
}

// FUNCTION: 0x00479630
float RaySphereEntryDistance(const FVector *start, const FVector *end, float radius)
{
	FVector direction = *end - *start;
	direction.Normalise();
	FVector toCentre = -*start;
	float projection = toCentre * direction;
	float discriminant = radius * radius - (toCentre.MagnitudeSq() - projection * projection);
	if (discriminant < 0)
		return -1;
	float root = sqrtf(discriminant);
	float distance = projection - root;
	if (distance < 0)
	{
		if (root + projection > 0)
			return radius - start->Magnitude();
		return -1;
	}
	return distance;
}

// FUNCTION: 0x00479770
float DistanceOutsideBox(const FVector *pos, const FVector *extent)
{
	SINT outside = 0;
	float x = fabsf(pos->X) - fabsf(extent->X);
	if (x > 0)
		outside |= GEOMETRY_OUTSIDE_X;
	float y = fabsf(pos->Y) - fabsf(extent->Y);
	if (y > 0)
		outside |= GEOMETRY_OUTSIDE_Y;
	float z = fabsf(pos->Z) - fabsf(extent->Z);
	if (z > 0)
		outside |= GEOMETRY_OUTSIDE_Z;
	if (outside == 0)
		return 0;
	if (outside == GEOMETRY_OUTSIDE_X)
		return x;
	if (outside == GEOMETRY_OUTSIDE_Y)
		return y;
	if (outside == GEOMETRY_OUTSIDE_Z)
		return z;
	if (outside == (GEOMETRY_OUTSIDE_X | GEOMETRY_OUTSIDE_Y))
		return sqrt((x * x) + (y * y));
	if (outside == (GEOMETRY_OUTSIDE_X | GEOMETRY_OUTSIDE_Z))
		return sqrt((x * x) + (z * z));
	if (outside == (GEOMETRY_OUTSIDE_Y | GEOMETRY_OUTSIDE_Z))
		return sqrt((y * y) + (z * z));
	if (outside == (GEOMETRY_OUTSIDE_X | GEOMETRY_OUTSIDE_Y | GEOMETRY_OUTSIDE_Z))
		// The retail triple-outside path adds z twice (004798a0..004798a7), not z squared.
		return sqrt((x * x) + (y * y) + (z + z));
	LOG.AddMessage("Error: Should not be here in 'DistPointToBox' ");
	return 0;
}
