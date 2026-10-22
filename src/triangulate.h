// triangulate.h: CTriangulate, a 2D triangle mesh over a rectangle that points are inserted into and whose edges are
// considered for edge flips (reconstructed; its user is CDXBattleLine::BuildMesh 0053a712..0053a7c0).
// The constructor, the split and the edge-quality body (CheckEdge, with a labelled stand-in) match.

#ifndef TRIANGULATE_H
#define TRIANGULATE_H

#include "mathsconst.h"		// the object opens with the maths constants' initializers (004f7080, 004f70a0)

struct FVector2
{
	float	X, Y;

	FVector2() {}
	FVector2(float x, float y) { X = x; Y = y; }
	FVector2	operator+(const FVector2 &b) const { return FVector2(X + b.X, Y + b.Y); }
	FVector2	operator-(const FVector2 &b) const { return FVector2(X - b.X, Y - b.Y); }
	FVector2	operator*(float s) const { return FVector2(X * s, Y * s); }
	float		Magnitude() const { return sqrtf((X * X) + (Y * Y)); }	// bracketed, as FVector::Magnitude (maths.h)
};

inline float	Cross(const FVector2 &a, const FVector2 &b) { return a.X * b.Y - a.Y * b.X; }

struct STriangle
{
	UWORD	mA, mB, mC;
};

class CTriangulate
{
public:
	CTriangulate(SINT maxPoints, FVector2 min, FVector2 max, SINT mode);	// 004f7170
	~CTriangulate();														// 004f7440

	void		AddPoint(FVector2 &p);										// 004f7460
	BOOL		SplitTriangle(STriangle *t, FVector2 &p);					// 004f74b0: p inside t splits it in three
	void		CheckEdge(UWORD a, UWORD b);								// 004f7660: edge-quality routine (matched, labelled stand-in)
	STriangle	*FindEdge(UWORD a, UWORD b);								// 004f78c0: the triangle with edge a-b, rotated to start with it
	void		Relax();													// 004f7940

	FVector2	*mVerts;						// +0x00
	STriangle	*mTris;							// +0x04
	SINT		mNumVerts;						// +0x08
	SINT		mNumTris;						// +0x0c
	SINT		mMaxVerts;						// +0x10
	BOOL		mChanged;						// +0x14: an edge was flipped
};

#endif
