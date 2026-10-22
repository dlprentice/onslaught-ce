// PolyBucket.h: CPolyBucket, a mesh part's triangles bucketed for collision (0xb8 bytes, MEMTYPE 70).
// CMeshPart makes one (BuildPolyBucket 004ae2b0) and searches it.

#ifndef POLYBUCKET_H
#define POLYBUCKET_H

class CMeshPart;
class CChunkReader;
#include "mesh.h"
#include "array.h"

// How the packed vertices unpack (CPolyBucket +0x40): an origin and a scale over 32767.
class CPolyBucketPacking
{
public:
	CPolyBucketPacking() : mOrigin(0.f, 0.f, 0.f), mScale(1.f) {}
	void		Unpack(CPolyBucketVertex *in, FVector &out)
	{
		out.X = in->X * mScale * (1.0f / 32767.0f) + mOrigin.X;
		out.Y = in->Y * mScale * (1.0f / 32767.0f) + mOrigin.Y;
		out.Z = in->Z * mScale * (1.0f / 32767.0f) + mOrigin.Z;
	}

	FVector		mOrigin;					// +0x00
	float		mScale;						// +0x10
};

// FUNCTION: 0x004d4b30
inline void CPolyBucketVertex::Unpack(FVector *out, CPolyBucketPacking *packing)
{
	// The signed packed coordinates become float values before scaling and adding the origin.
	// Each converted coordinate owns both scale steps before its origin is added.
	float x = X;
	x *= packing->mScale;
	x *= 1.0f / 32767.0f;
	out->X = x + packing->mOrigin.X;
	float y = Y;
	y *= packing->mScale;
	y *= 1.0f / 32767.0f;
	out->Y = y + packing->mOrigin.Y;
	float z = Z;
	z *= packing->mScale;
	z *= 1.0f / 32767.0f;
	out->Z = z + packing->mOrigin.Z;
}


// Twelve-byte records: three vertex indices, the last query stamp, and a 32-slice Z mask
// (GetNextPoly 004d4f19..004d4f30; GetRandomTriangle 004d5983..004d59df).
class CPolyBucketTriangle
{
public:
	SWORD		mVertices[3];
	SWORD		mSearchStamp;
	ULONG		mZMask;
};

// The cell's short-index list. Its iterator is shared at 0082b69c, not an instance member
// (First is inlined at 004d4fb3; Next at 004d4f36). The +0c borrowed-data flag is tested by
// its destructor 004d3af0; Load 004d5bf2 sets it for slices of the shared index buffer.
class CPolyBucketIndexList
{
public:
	CPolyBucketIndexList();			// 004d5e20
	~CPolyBucketIndexList();			// 004d3af0
	void ReadBorrowed(CChunkReader *reader, SWORD *indices, SINT &offset);	// inlined at 004d5bd1
	void Add(SWORD index);			// inlined at 004d4983; starts at 10 shorts, then doubles
	SWORD	First()
	{
		if (mEntries <= 0)
			return -1;
		sIterator = 0;
		return mData[0];
	}
	SWORD	Next()
	{
		if (++sIterator >= mEntries)
			return -1;
		return mData[sIterator];
	}

	SWORD		*mData;				// +0x00
	SINT		mCapacity;				// +0x04, Build 004d4986..004d49d3
	SINT		mEntries;			// +0x08
	BOOL		mBorrowedData;		// +0x0c
	static SINT	sIterator;
};

class CPolyBucket
{
public:
	CPolyBucket();											// 004d39d0
	~CPolyBucket();											// 004d3a00
	bool		Build(CMeshPart *part);						// 004d40d0
	static CPolyBucket *Load(CChunkReader *reader);		// 004d59f0
	void		DebugRender();							// 004d5e30; CMeshRenderer calls it at 004b6a18
	SWORD		*StartSearch(FVector *pos, float radius);	// 004d4c00
	SWORD		*GetNextPoly();							// 004d4f00
	SWORD		*StartLineSearch(FVector *start, FVector *end);	// 004d50d0
	BOOL		AdvanceLineSearch();						// 004d5650
	SWORD		*GetNextPolyLine(SINT a);				// 004d57c0
	BOOL		IntersectsCell(float x, float y, float width, float height,
						FVector *start, FVector *end);		// 004d3b10, called by AdvanceLineSearch
	BOOL		TriangleInBucket(FVector *vertices, SINT x, SINT y);	// 004d3ce0, called by Build
	SINT		FindOrAddVertex(FVector *vertex);			// source helper for Build's searches, 004d43fb..004d462a
	void		SetTriangle(SINT index, SINT a, SINT b, SINT c);	// source helper for Build's record stores, 004d4816..004d4854

	SINT		GetRandomTriangle(CPolyBucketVertex **out);	// 004d5930: 1 when it found one
	ULONG		GetZMask(float low, float high);			// inlined in StartSearch and StartLineSearch
	BOOL		IsSearchCellValid();						// inlined in StartLineSearch and AdvanceLineSearch

	FVector				mLineStart;			// +0x00, in grid coordinates
	FVector				mLineEnd;			// +0x10, in grid coordinates
	FVector				mSearchCentre;		// +0x20, relative to mOrigin
	FVector				mOrigin;			// +0x30
	CPolyBucketPacking	mPacking;			// +0x40 (CMeshPart::GetRandomSurfacePoint 004b2675)
	UBYTE				mUnknown54[0x60 - 0x54];
	CPolyBucketIndexList	**mCells;			// +0x60, rows of mWidth cells
	CPolyBucketTriangle	*mTriangles;		// +0x64
	SINT				mWidth;				// +0x68
	SINT				mHeight;			// +0x6c
	float				mCellSizeXY;		// +0x70
	float				mCellSizeZ;			// +0x74
	SINT				mNumTriangles;		// +0x78, Build 004d4195/004d4308
	BOOL				mLoadedCells;		// +0x7c, contiguous rather than separately allocated lists
	CPolyBucketIndexList	*mLoadedLists;		// +0x80
	SWORD				*mLoadedIndices;	// +0x84
	float				mSearchRadius;		// +0x88
	ULONG				mSearchZMask;		// +0x8c
	CPolyBucketIndexList	*mCurrentList;		// +0x90
	CPolyBucketTriangle	*mCurrentTriangle;	// +0x94
	// Reconstructed as the pinned source's COSet (16 bytes, Load allocates it): CArray's items +00 and capacity
	// +04, the entries +08 and the iterator +0c. Build's constructor chain is the template's two levels.
	COSet<CPolyBucketVertex>	*mVertices;			// +0x98
	SINT				mSearchStamp;		// +0x9c; compared to a sign-extended short stamp
	SINT				mMinX, mMinY;		// +0xa0, +0xa4
	SINT				mMaxX, mMaxY;		// +0xa8, +0xac
	SINT				mCurrentX, mCurrentY;	// +0xb0, +0xb4
};

#endif
