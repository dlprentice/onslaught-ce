#include "common.h"		// FUNCTION: 0x004f7080 init:ZERO_FVECTOR
#include "triangulate.h"	// FUNCTION: 0x004f70a0 init:ID_FMATRIX

CTriangulate::CTriangulate(SINT maxPoints, FVector2 min, FVector2 max, SINT mode)	// FUNCTION: 0x004f7170
{
	mVerts = new (MEMTYPE_UNKNOWN) FVector2[maxPoints];
	mTris = new (MEMTYPE_UNKNOWN) STriangle[maxPoints * 2];
	mMaxVerts = maxPoints;

	switch (mode)
	{
	case 1:
	{
		mVerts[0] = min;
		mVerts[2].X = max.X;
		mVerts[2].Y = min.Y;
		mVerts[5].X = min.X;
		mVerts[5].Y = max.Y;
		mVerts[7] = max;
		mVerts[1] = (mVerts[0] + mVerts[2]) * 0.5f;
		mVerts[3] = (mVerts[0] + mVerts[5]) * 0.5f;
		mVerts[4] = (mVerts[2] + mVerts[7]) * 0.5f;
		mVerts[6] = (mVerts[5] + mVerts[7]) * 0.5f;

		mTris[0].mA = 0;
		mTris[0].mB = 1;
		mTris[0].mC = 3;
		mTris[1].mA = 1;
		mTris[1].mB = 5;
		mTris[1].mC = 3;
		mTris[2].mA = 1;
		mTris[2].mB = 6;
		mTris[2].mC = 5;
		mTris[3].mA = 1;
		mTris[3].mB = 7;
		mTris[3].mC = 6;
		mTris[4].mA = 1;
		mTris[4].mB = 4;
		mTris[4].mC = 7;
		mTris[5].mA = 1;
		mTris[5].mB = 2;
		mTris[5].mC = 4;

		mNumVerts = 8;
		mNumTris = 6;
		break;
	}

	case 0:
		mVerts[0] = min;
		mVerts[1].X = max.X;
		mVerts[1].Y = min.Y;
		mVerts[2].X = min.X;
		mVerts[2].Y = max.Y;
		mVerts[3] = max;

		mTris[0].mA = 0;
		mTris[0].mB = 1;
		mTris[0].mC = 2;
		mTris[1].mA = 1;
		mTris[1].mB = 3;
		mTris[1].mC = 2;

		mNumVerts = 4;
		mNumTris = 2;
		break;
	}
}

//******************************************************************************************
CTriangulate::~CTriangulate()	// FUNCTION: 0x004f7440
{
	delete [] mVerts;
	delete [] mTris;
}

//******************************************************************************************
void	CTriangulate::AddPoint(FVector2 &p)	// FUNCTION: 0x004f7460
{
	SINT		c0 = mNumTris;
	STriangle	*t = mTris;

	while (c0)
	{
		if (SplitTriangle(t, p))
			break;
		t++;
		c0--;
	}

	if (!c0)
	{
		mVerts[mNumVerts] = p;
		mNumVerts++;
	}
}

// STAND-IN (see SplitTriangle): a difference with a local result, the form triangulate.h's operator- had
// before it built its result in place. Name ours.
inline FVector2	SubLocal(const FVector2 &a, const FVector2 &b)	{ FVector2 r; r.X = a.X - b.X; r.Y = a.Y - b.Y; return r; }

//******************************************************************************************
BOOL	CTriangulate::SplitTriangle(STriangle *t, FVector2 &p)	// FUNCTION: 0x004f74b0
{
	if (mNumVerts < mMaxVerts)
	{
		// STAND-IN (ids, operand order of the cross products): VC6 orders a product's operands by symbol id
		// (the rank tool): retail's first cross product needs da.Y ahead of db.X, which no spelling with the three
		// differences made the same way gives. Here da is formed from its components and db and dc are const references
		// to SubLocal's results (the local-result form; with operator-'s constructed result the third product loads da.Y
		// first: score 0/4/10, and every natural declaration form of the three differences is 2..34 off). Original
		// spelling unknown.
		UWORD		b = t->mB;
		FVector2	da(mVerts[t->mA].X - p.X, mVerts[t->mA].Y - p.Y);
		const FVector2	&db = SubLocal(mVerts[b], p);
		UWORD		c = t->mC;
		const FVector2	&dc = SubLocal(mVerts[c], p);

		if (Cross(da, db) >= 0 && Cross(db, dc) >= 0 && Cross(dc, da) >= 0)
		{

			t->mC = mNumVerts;

			mTris[mNumTris].mA = b;
			mTris[mNumTris].mB = c;
			mTris[mNumTris].mC = mNumVerts;
			mNumTris++;

			mTris[mNumTris].mA = c;
			mTris[mNumTris].mB = t->mA;
			mTris[mNumTris].mC = mNumVerts;
			mNumTris++;

			mVerts[mNumVerts] = p;
			mNumVerts++;

			UWORD	a2 = t->mA;
			UWORD	b2 = t->mB;
			CheckEdge(a2, b2);
			CheckEdge(b2, c);
			CheckEdge(c, a2);
			return TRUE;
		}
	}
	return FALSE;
}

//******************************************************************************************
// The two lengths take FVector2's operator- (its constructed result) and Magnitude (its products bracketed, as maths.h's).
// The scheduler (C2 0x107374aa) orders the edge's Y subtraction against the leas of p0 and p1, whose addresses the
// subtraction shares: built in place by the constructor, the X difference reaches the Y load with no delay, so the lea
// and the Y subtraction tie on priority (depth << 13, plus 65536 for a memory operand), and the magnitudes' brackets
// (outgoing 30 -> 34, >> 5) break the tie so that retail's `fsub [eax+ecx*8+4]` (004f76b5) goes before both leas.
//
// The cross products are written out as x1*y2 - x2*y1, which names the members in the order their ids need, and every
// difference is built by the component constructor; both branches match.
void	CTriangulate::CheckEdge(UWORD a, UWORD b)	// FUNCTION: 0x004f7660
{
	STriangle	*t1 = FindEdge(a, b);
	STriangle	*t2 = FindEdge(b, a);

	if (!t1 || !t2)
		return;

	float		edge = (mVerts[t1->mA] - mVerts[t1->mB]).Magnitude();
	float		diagonal = (mVerts[t1->mC] - mVerts[t2->mC]).Magnitude();
	FVector2	&p0 = mVerts[t1->mA];
	FVector2	&p1 = mVerts[t1->mB];
	FVector2	&p2 = mVerts[t1->mC];
	FVector2	&p3 = mVerts[t2->mC];

	if (edge / diagonal < 0.8f)
	{
		if (!(diagonal < edge))
			return;

		FVector2	u1(p2.X - p1.X, p2.Y - p1.Y);
		FVector2	v1(p3.X - p1.X, p3.Y - p1.Y);
		if (u1.X * v1.Y - v1.X * u1.Y <= 0)
			return;
		FVector2	u2(p3.X - p0.X, p3.Y - p0.Y);
		FVector2	v2(p2.X - p0.X, p2.Y - p0.Y);
		if (u2.X * v2.Y - v2.X * u2.Y <= 0)
			return;
	}
	else
	{
		FVector2	a1(p2.X - p1.X, p2.Y - p1.Y);
		FVector2	b1(p3.X - p1.X, p3.Y - p1.Y);
		FVector2	a2(p3.X - p0.X, p3.Y - p0.Y);
		FVector2	b2(p2.X - p0.X, p2.Y - p0.Y);

		float	area1 = a1.X * b1.Y - b1.X * a1.Y;
		float	area2 = a2.X * b2.Y - b2.X * a2.Y;

		if (area1 <= 0)
			return;
		if (area2 <= 0)
			return;

		float	before;
		if (area1 > area2)
			before = area2 / area1;
		else
			before = area1 / area2;

		float	area3 = b1.X * a2.Y - a2.X * b1.Y;
		float	area4 = a1.X * b2.Y - b2.X * a1.Y;

		float	after;
		if (area3 > area4)
			after = area4 / area3;
		else
			after = area3 / area4;

		if (!(before > after))
			return;
	}

	t1->mA = t2->mC;
	t2->mA = t1->mC;
	mChanged = TRUE;
}

//******************************************************************************************
STriangle	*CTriangulate::FindEdge(UWORD a, UWORD b)	// FUNCTION: 0x004f78c0
{
	STriangle	*t = mTris;

	for (SINT c0 = mNumTris; c0; t++, c0--)
	{
		if (t->mA == a && t->mB == b)
			return t;

		if (t->mB == a && t->mC == b)
		{
			UWORD	temp = t->mA;
			t->mA = t->mB;
			t->mB = t->mC;
			t->mC = temp;
			return t;
		}

		if (t->mC == a && t->mA == b)
		{
			UWORD	temp = t->mA;
			t->mA = t->mC;
			t->mC = t->mB;
			t->mB = temp;
			return t;
		}
	}

	return NULL;
}

//******************************************************************************************
void	CTriangulate::Relax()	// FUNCTION: 0x004f7940
{
	mChanged = TRUE;

	for (SINT pass = 10; pass; pass--)
	{
		if (!mChanged)
			break;
		mChanged = FALSE;

		STriangle	*tri = mTris;
		for (SINT c0 = mNumTris; c0; tri++, c0--)
		{
			STriangle	t = *tri;
			CheckEdge(t.mA, t.mB);
			CheckEdge(t.mB, t.mC);
			CheckEdge(t.mC, t.mA);
		}
	}
}
