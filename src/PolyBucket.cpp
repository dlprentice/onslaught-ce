// PolyBucket.cpp: retail bucket construction, queries, ownership and debug drawing.
#include "common.h"
#include "PolyBucket.h"
#include "DebugLog.h"
#include "game.h"
#include "Geometry.h"
#include "chunker.h"
#include "DXEngine.h"
#include "renderinfo.h"
#include "VBufTexture.h"

// The retail line walk is shared by all buckets (0082b688..0082b69c); it is not reentrant.
static float sLineX = 0.0f, sLineY = 0.0f;
static SINT sNumLineSteps = 0;
static BOOL sPendingDiagonalStep = FALSE;
static SINT sLineStep = 0;
SINT CPolyBucketIndexList::sIterator = 0;
static float sLineStepX = 0.1f;	// 006316b4
static float sLineStepY = 0.1f;	// 006316b8

// FUNCTION: 0x004d38e0 init:ZERO_FVECTOR
// FUNCTION: 0x004d3900 init:ID_FMATRIX
// FUNCTION: 0x004d4b90 ??KFVector@@QBE?AV0@M@Z
// FUNCTION: 0x004d4bc0 ??8FVector@@QBEHABV0@@Z
// Folded empty constructor; Build naturally emits it for the initial packed-vertex array.
// FUNCTION: 0x00402d20 ??0CPolyBucketVertex@@QAE@XZ
// FUNCTION: 0x004d61b0 ?Add@?$COSet@VCPolyBucketVertex@@@@QAEHABVCPolyBucketVertex@@@Z
// Retail Build calls this copy at 004d4592; standalone COSet::Add inlines it.
// Supply the callable specialization here without changing that inline body.
// This reconstruction boundary does not establish retail's instantiation spelling.
// FUNCTION: 0x004d6210 ?ReSize@?$CArray@VCPolyBucketVertex@@@@QAEXH@Z
template class CArray<CPolyBucketVertex>;
// COSet's destructor (CArray's inlined) folds with an existing array destructor (14 bytes).
// FUNCTION: 0x005447d0 ??1?$COSet@VCPolyBucketVertex@@@@QAE@XZ

// FUNCTION: 0x004d39d0
CPolyBucket::CPolyBucket()
{
	mCells = NULL;
	mTriangles = NULL;
	mNumTriangles = 0;
	mSearchStamp = 0;
	mVertices = NULL;
	mLoadedCells = FALSE;
}

// FUNCTION: 0x004d3a00
CPolyBucket::~CPolyBucket()
{
	if (mCells)
	{
		if (mLoadedCells)
		{
			SAFE_DELETE_ARRAY(mLoadedLists);
			SAFE_DELETE_ARRAY(mLoadedIndices);
		}
		else
			for (SINT i = 0; i < mWidth * mHeight; ++i)
				delete mCells[i];
	}
	delete [] mCells;
	delete [] mTriangles;
	delete mVertices;
}

// FUNCTION: 0x004d3af0
CPolyBucketIndexList::~CPolyBucketIndexList()
{
	if (!mBorrowedData)
		delete [] mData;
}

// FUNCTION: 0x004d3b10
BOOL CPolyBucket::IntersectsCell(float x, float y, float width, float height, FVector *start, FVector *end)
{
	float right = x + width;
	if (start->X > right && end->X > right)
		return FALSE;
	if (start->X < x && end->X < x)
		return FALSE;
	float top = y + height;
	if (start->Y > top && end->Y > top)
		return FALSE;
	if (start->Y < y && end->Y < y)
		return FALSE;
	FVector diff = *end - *start;
	if (diff.X != 0.0)
	{
		float slope = diff.Y / diff.X;
		float first = (x - start->X) * slope + start->Y;
		float last = (right - start->X) * slope + start->Y;
		if (first >= top && last >= top)
			return FALSE;
		if (first < y && last < y)
			return FALSE;
	}
	if (diff.Y != 0.0)
	{
		float slope = diff.X / diff.Y;
		float first = (y - start->Y) * slope + start->X;
		float last = (top - start->Y) * slope + start->X;
		if (first >= right && last >= right)
			return FALSE;
		if (first < x && last < x)
			return FALSE;
	}
	return TRUE;
}

// FUNCTION: 0x004d3ce0
BOOL CPolyBucket::TriangleInBucket(FVector *vertices, SINT x, SINT y)
{
	FVector triangle[3];
	triangle[0] = vertices[0];
	triangle[0].Z = 0.f;
	triangle[1] = vertices[1];
	triangle[1].Z = 0.f;
	triangle[2] = vertices[2];
	triangle[2].Z = 0.f;
	FVector first = triangle[0] - triangle[1];
	FVector second = triangle[2] - triangle[1];
	if ((first ^ second).Z > 0.f)
	{
		FVector temp = triangle[2];
		triangle[2] = triangle[1];
		triangle[1] = temp;
	}
	SINT edgeIndex = 0;
	while (1)
	{
		FVector start = triangle[edgeIndex];
		FVector end = triangle[(edgeIndex + 1) % 3];
		FVector edge = end - start;
		SINT cornerIndex = 0;
		while (1)
		{
			FVector corner;
			switch (cornerIndex)
			{
			case 0: corner = FVector(x * mCellSizeXY, y * mCellSizeXY, 0.f); break;
			case 1: corner = FVector((x + 1) * mCellSizeXY, y * mCellSizeXY, 0.f); break;
			case 2: corner = FVector(x * mCellSizeXY, (y + 1) * mCellSizeXY, 0.f); break;
			case 3: corner = FVector((x + 1) * mCellSizeXY, (y + 1) * mCellSizeXY, 0.f); break;
			}
			FVector side = edge ^ (corner - start);
			if (side.Z >= 0.f)
				break;
			if (++cornerIndex >= 4)
				return FALSE;
		}
		if (++edgeIndex >= 3)
			return TRUE;
	}
}


// Source helper for the three reverse searches expanded in Build; no independent entry is claimed.
// Source helper for the searches' test: the unpacked vertex against the one being added.
static inline BOOL PackedVertexIs(CPolyBucketVertex *packed, CPolyBucketPacking *packing, FVector *vertex)
{
	FVector unpacked;
	packed->Unpack(&unpacked, packing);
	return unpacked == *vertex;
}

inline SINT CPolyBucket::FindOrAddVertex(FVector *vertex)
{
	SINT count = mVertices->Size();
	for (SINT i = 0; i < count; ++i)
	{
		if (PackedVertexIs(&(*mVertices)[count - i - 1], &mPacking, vertex))
			return count - i - 1;
	}
	return mVertices->Add(CPolyBucketVertex(vertex, &mPacking));
}

// Source helper for the owned-list growth expanded in Build. Loaded lists only borrow their data.
inline void CPolyBucketIndexList::Add(SWORD index)
{
	++mEntries;
	if (mEntries > mCapacity)
	{
		if (!mData)
		{
#line 118
			mData = new(MEMTYPE_POLYBUCKET) SWORD[10];
			mCapacity = 10;
		}
		else
		{
			mData = (SWORD *)MEM_MANAGER.ReAlloc(mData, sizeof(SWORD) * mCapacity * 2);
			mCapacity *= 2;
		}
	}
	mData[mEntries - 1] = index;
}

// Source helper for a triangle record's stores (Build 004d4816..004d4854), inferred rather than proven: each store
// re-reads mTriangles as retail does, the record's all-slices mask (which Build overwrites with GetZMask straight
// after) belongs here, and as an inline site after the third search it is one of the five that set the searches'
// budgets (see Build).
inline void CPolyBucket::SetTriangle(SINT index, SINT a, SINT b, SINT c)
{
	mTriangles[index].mVertices[0] = (SWORD)a;
	mTriangles[index].mVertices[1] = (SWORD)b;
	mTriangles[index].mVertices[2] = (SWORD)c;
	mTriangles[index].mZMask = 0xffffffff;
	mTriangles[index].mSearchStamp = 0;
}

// Only the cell table allocation has an explicit null check; degenerate triangle slots stay unused. The caller
// builds a freshly constructed bucket. Inline budget (VC6 units): the vertex set is Stuart's COSet, so its
// constructor chain is two levels and CArray's constructor inlines with the vector constructor iterator called.
// This matches retail's code, not proof of the original container or helper spelling.
// The three FindOrAddVertex budgets fall from the first (packed-vertex constructor inlined, COSet::Add called) to
// the second (Add inlined, calling ReSize) to the third (both called) only with five inline sites after the third
// search (the four floorf of the cell bounds and SetTriangle) and Build's own size 1394..1438 (it is 1436; the
// `maxZ = minZ` copy and the unbraced cell loops keep it there). The degenerate test reads the positions in full:
// retail loads the second vertex first and the third only after the first test. The triangle loop nests its body
// under that test instead of `continue` (as CInfluenceMap::Build's thing loops): the latch forms its own split
// region, which places the spill and reload code as retail's.
// STAND-IN: the doubled brackets in the two grid-size floorf arguments are bracket tuples that number mHeight's
// address temporary above mWidth's (the memset's mHeight * mWidth operand order) while mWidth's stays above y's
// (the cell index's mWidth * y). Without them, or with the plain floor, one of the two orders flips.
// Original spelling is unknown; replace with a supported natural form only if the complete match survives.
// FUNCTION: 0x004d40d0
bool CPolyBucket::Build(CMeshPart *part)
{
	if (!part->mNumTriangles)
		return false;
#line 332
	mVertices = new(MEMTYPE_POLYBUCKET) COSet<CPolyBucketVertex>(1);
	mNumTriangles = part->mNumTriangles;
	CBoundingBox *box = part->mBoundingBox;
	float width = box->mAxes.X * 2.f;
	float height = box->mAxes.Y * 2.f;
	mPacking.mOrigin = box->mOrigin;
	mPacking.mScale = box->mAxes.X + 1.f;
	float scale = box->mAxes.Y + 1.f;
	if (scale > mPacking.mScale)
		mPacking.mScale = scale;
	scale = box->mAxes.Z + 1.f;
	if (scale > mPacking.mScale)
		mPacking.mScale = scale;
	mCellSizeXY = 1.f;
	mWidth = (SINT)floorf((width / mCellSizeXY)) + 1;		// STAND-IN: doubled brackets (see above)
	mHeight = (SINT)floorf((height / mCellSizeXY)) + 1;
#line 359
	mCells = new(MEMTYPE_POLYBUCKET) CPolyBucketIndexList *[mHeight * mWidth];
	if (!mCells)
		return false;
	memset(mCells, 0, sizeof(CPolyBucketIndexList *) * mHeight * mWidth);
	mOrigin = box->mOrigin - box->mAxes;
	// Retail constant 005de7ac is the float 0x3d0366b9 (the stored reciprocal of 31.9f).
	mCellSizeZ = (box->mAxes.Z * 2.f) / 31.9f;
#line 379
	mTriangles = new(MEMTYPE_POLYBUCKET) CPolyBucketTriangle[mNumTriangles];
	for (SINT triangleIndex = 0; triangleIndex < part->mNumTriangles; ++triangleIndex)
	{
		CMeshTriangle *triangle = &part->mTriangles[triangleIndex];
		if ((triangle->mVertices[0]->mPVert != triangle->mVertices[1]->mPVert) &&
			(triangle->mVertices[0]->mPVert != triangle->mVertices[2]->mPVert) &&
			(triangle->mVertices[1]->mPVert != triangle->mVertices[2]->mPVert))
		{
			FVector vertices[3];
			vertices[0] = part->mPVertices[0][triangle->mVertices[0]->mPVert];
			vertices[1] = part->mPVertices[0][triangle->mVertices[1]->mPVert];
			vertices[2] = part->mPVertices[0][triangle->mVertices[2]->mPVert];
			SINT packedA = FindOrAddVertex(&vertices[0]);
			SINT packedB = FindOrAddVertex(&vertices[1]);
			SINT packedC = FindOrAddVertex(&vertices[2]);
			vertices[0] -= mOrigin;
			vertices[1] -= mOrigin;
			vertices[2] -= mOrigin;
			float minZ = vertices[0].Z;
			float maxZ = minZ;
			if (vertices[1].Z < minZ) minZ = vertices[1].Z;
			if (vertices[2].Z < minZ) minZ = vertices[2].Z;
			if (vertices[1].Z > maxZ) maxZ = vertices[1].Z;
			if (vertices[2].Z > maxZ) maxZ = vertices[2].Z;
			float minX = 100000000000.f;
			float minY = 100000000000.f;
			float maxX = -100000000000.f;
			float maxY = -100000000000.f;
			for (SINT v = 0; v < 3; ++v)
			{
				if (minX > vertices[v].X) minX = vertices[v].X;
				if (vertices[v].Y < minY) minY = vertices[v].Y;
				if (vertices[v].X > maxX) maxX = vertices[v].X;
				if (vertices[v].Y > maxY) maxY = vertices[v].Y;
			}
			SINT minCellX = (SINT)floorf(minX / mCellSizeXY);
			SINT minCellY = (SINT)floorf(minY / mCellSizeXY);
			SINT maxCellX = (SINT)floorf(maxX / mCellSizeXY);
			SINT maxCellY = (SINT)floorf(maxY / mCellSizeXY);
			if (minCellX < 0) minCellX = 0;
			if (minCellY < 0) minCellY = 0;
			if (maxCellX >= mWidth) maxCellX = mWidth - 1;
			if (maxCellY >= mHeight) maxCellY = mHeight - 1;
			SetTriangle(triangleIndex, packedA, packedB, packedC);
			mTriangles[triangleIndex].mZMask = GetZMask(minZ, maxZ);
			BOOL placed = FALSE;
			for (SINT y = minCellY; y <= maxCellY; ++y)
				for (SINT x = minCellX; x <= maxCellX; ++x)
					if (TriangleInBucket(vertices, x, y))
					{
						CPolyBucketIndexList *cell = mCells[y * mWidth + x];
						if (!cell)
						{
	#line 473
							cell = new(MEMTYPE_POLYBUCKET) CPolyBucketIndexList;
							mCells[y * mWidth + x] = cell;
						}
						cell->Add((SWORD)triangleIndex);
						placed = TRUE;
					}
			if (!placed)
				LOG.AddMessage("Warning: Triangle wasn't placed in polybucket (part_num %d)", part->mPartNumber);
		}
	}
	mVertices->Finalise();
	return true;
}

// This cast-and-correct floor is inlined throughout both query paths (e.g. 004d4cbc..004d4ce0).
inline SINT BucketFloor(float value)
{
	SINT result = (SINT)value;
	if (value - result < 0.f)
		--result;
	return result;
}

// The same slice-mask calculation appears in StartSearch and StartLineSearch. Keep the division by the
// slice size and the all-bits zero-size case, even though both callers first reject very small sizes.
inline ULONG CPolyBucket::GetZMask(float low, float high)
{
	if (mCellSizeZ == 0.0)
		return 0xffffffff;
	float bottom = low / mCellSizeZ;
	float top = high / mCellSizeZ;
	SINT first = BucketFloor(bottom);
	SINT last = BucketFloor(top);
	if (first < 0)
		first = 0;
	if (last > 31)
		last = 31;
	ULONG mask = 0;
	for (SINT i = first; i <= last; ++i)
		mask |= 1 << i;
	return mask;
}

// The cell is read through references: after AdvanceLineSearch stores the new cell, retail's inlined check tests
// copies of the stored values (004d5710 mov eax,ebx; 004d5727 mov eax,edi) and keeps x in ebx, y in edi, where a
// direct read of the members tests the registers themselves.
inline BOOL CPolyBucket::IsSearchCellValid()
{
	const SINT &x = mCurrentX;
	const SINT &y = mCurrentY;
	if (x < 0 || x >= mWidth || y < 0 || y >= mHeight)
	{
		LOG.AddMessage("WARNING:  search in invalid poly bucket sector");
		return FALSE;
	}
	return TRUE;
}

// FUNCTION: 0x004d4aa0
CPolyBucketVertex::CPolyBucketVertex(FVector *in, CPolyBucketPacking *packing)
{
	FVector relative = (*in - packing->mOrigin) / packing->mScale;
	X = (SWORD)(relative.X * 32767.f);
	Y = (SWORD)(relative.Y * 32767.f);
	Z = (SWORD)(relative.Z * 32767.f);
}

// FUNCTION: 0x004d4c00
SWORD *CPolyBucket::StartSearch(FVector *pos, float radius)
{
	if (!mCells)
		return NULL;
	if (mCellSizeZ < 0.0001f)
		return NULL;
	++mSearchStamp;
	mSearchRadius = radius;
	mSearchCentre = *pos - mOrigin;
	mSearchZMask = GetZMask(mSearchCentre.Z - radius, radius + mSearchCentre.Z);
	if (!mSearchZMask)
		return NULL;
	mMinX = BucketFloor((mSearchCentre.X - radius) / mCellSizeXY);
	if (mMinX >= mWidth)
		return NULL;
	mMinY = BucketFloor((mSearchCentre.Y - radius) / mCellSizeXY);
	if (mMinY >= mHeight)
		return NULL;
	mMaxX = BucketFloor((radius + mSearchCentre.X) / mCellSizeXY);
	if (mMaxX < 0)
		return NULL;
	mMaxY = BucketFloor((radius + mSearchCentre.Y) / mCellSizeXY);
	if (mMaxY < 0)
		return NULL;
	if (mMinX < 0)
		mMinX = 0;
	if (mMinY < 0)
		mMinY = 0;
	if (mMaxX >= mWidth)
		mMaxX = mWidth - 1;
	if (mMaxY >= mHeight)
		mMaxY = mHeight - 1;
	mCurrentY = mMinY;
	mCurrentX = mMinX;
	mCurrentList = mCells[mCurrentY * mWidth + mCurrentX];
	mCurrentTriangle = NULL;
	if (mCurrentList)
	{
		SINT index = mCurrentList->First();
		if (index == -1)
			mCurrentTriangle = NULL;
		else
			mCurrentTriangle = &mTriangles[index];
	}
	return GetNextPoly();
}

// FUNCTION: 0x004d4f00
SWORD *CPolyBucket::GetNextPoly()
{
	if (mCells)
	{
		while (1)
		{
			if (mCurrentTriangle)
			{
				if (mCurrentTriangle->mSearchStamp != mSearchStamp &&
					(mCurrentTriangle->mZMask & mSearchZMask))
					break;
				SINT index = mCurrentList->Next();
				if (index == -1)
					mCurrentTriangle = NULL;
				else
					mCurrentTriangle = &mTriangles[index];
			}
			else if (mCurrentX < mMaxX)
			{
				++mCurrentX;
				mCurrentList = mCells[mCurrentY * mWidth + mCurrentX];
				if (mCurrentList)
				{
					SINT index = mCurrentList->First();
					if (index == -1)
						mCurrentTriangle = NULL;
					else
						mCurrentTriangle = &mTriangles[index];
				}
			}
			else if (mCurrentY < mMaxY)
			{
				++mCurrentY;
				mCurrentX = mMinX;
				mCurrentList = mCells[mCurrentY * mWidth + mCurrentX];
				if (mCurrentList)
				{
					SINT index = mCurrentList->First();
					if (index == -1)
						mCurrentTriangle = NULL;
					else
						mCurrentTriangle = &mTriangles[index];
				}
			}
			else
				return NULL;
		}
		mCurrentTriangle->mSearchStamp = (SWORD)mSearchStamp;
		SWORD *result = mCurrentTriangle->mVertices;
		SINT index = mCurrentList->Next();
		if (index == -1)
			mCurrentTriangle = NULL;
		else
			mCurrentTriangle = &mTriangles[index];
		return result;
	}
	return NULL;
}

// FUNCTION: 0x004d50d0
SWORD *CPolyBucket::StartLineSearch(FVector *start, FVector *end)
{
	if (!mCells)
		return NULL;
	if (mCellSizeZ < 0.0001f)
		return NULL;
	++mSearchStamp;
	mLineStart = *start - mOrigin;
	mLineEnd = *end - mOrigin;
	mLineStart.X /= mCellSizeXY;
	mLineEnd.X /= mCellSizeXY;
	if (mLineStart.X < 0.f && mLineEnd.X < 0.f)
		return NULL;
	if (mLineStart.X > (float)mWidth && mLineEnd.X > (float)mWidth)
		return NULL;
	mLineStart.Y /= mCellSizeXY;
	mLineEnd.Y /= mCellSizeXY;
	if (mLineStart.Y < 0.f && mLineEnd.Y < 0.f)
		return NULL;
	if (mLineStart.Y > (float)mHeight && mLineEnd.Y > (float)mHeight)
		return NULL;
	mLineStart.Z /= mCellSizeZ;
	mLineEnd.Z /= mCellSizeZ;
	if (mLineStart.Z < 0.f && mLineEnd.Z < 0.f)
		return NULL;
	if (mLineStart.Z > 32.f && mLineEnd.Z > 32.f)
		return NULL;
	float bounds[6] = {0.f, 0.f, (float)mWidth - 0.0001f, (float)mHeight - 0.0001f, 0.f, 31.9999f};
	if (!ClipSegmentAgainstAABB3D(&mLineStart.X, &mLineStart.Y, &mLineStart.Z,
			&mLineEnd.X, &mLineEnd.Y, &mLineEnd.Z, bounds))
		return NULL;
	float low = mLineStart.Z > mLineEnd.Z ? mLineEnd.Z : mLineStart.Z;
	float high = mLineStart.Z >= mLineEnd.Z ? mLineStart.Z : mLineEnd.Z;
	mSearchZMask = GetZMask(low * mCellSizeZ, high * mCellSizeZ);
	if (!mSearchZMask)
		return NULL;
	mCurrentTriangle = NULL;
	float dx = mLineEnd.X - mLineStart.X;
	float dy = mLineEnd.Y - mLineStart.Y;
	if (mWidth < dx || mHeight < dy)
		LOG.AddMessage("WARNING: diff outside poly bucket range");
	if (fabs(dx) > fabs(dy))
	{
		sLineStepX = 1.f;
		if (dx <= 0.0)
			sLineStepX = -1.f;
		if (dy != 0.f)
			sLineStepY = dy / (float)fabs(dx);
		else
			sLineStepY = 0.f;
		sNumLineSteps = BucketFloor((float)fabs(dx)) + 1;
	}
	else
	{
		sLineStepY = 1.f;
		if (dy <= 0.0)
			sLineStepY = -1.f;
		if (dx != 0.f)
			sLineStepX = dx / (float)fabs(dy);
		else
			sLineStepX = 0.f;
		sNumLineSteps = BucketFloor((float)fabs(dy)) + 1;
	}
	sLineX = mLineStart.X;
	sLineY = mLineStart.Y;
	mCurrentX = BucketFloor(sLineX);
	mCurrentY = BucketFloor(sLineY);
	sPendingDiagonalStep = FALSE;
	sLineStep = 0;
	if (!IsSearchCellValid())
		return NULL;
	return GetNextPolyLine(0);
}

// FUNCTION: 0x004d5650
BOOL CPolyBucket::AdvanceLineSearch()
{
	while (1)
	{
		if (!sPendingDiagonalStep)
		{
			if (++sLineStep >= sNumLineSteps)
				return FALSE;
			sLineX += sLineStepX;
			sLineY += sLineStepY;
		}
		sPendingDiagonalStep = FALSE;
		SINT x = BucketFloor(sLineX);
		SINT y = BucketFloor(sLineY);
		if (x == mCurrentX || y == mCurrentY)
		{
			mCurrentX = x;
			mCurrentY = y;
			if (IsSearchCellValid())
				return TRUE;
		}
		else if (IntersectsCell((float)x, (float)mCurrentY, 1.f, 1.f, &mLineStart, &mLineEnd))
		{
			mCurrentX = x;
			sPendingDiagonalStep = TRUE;
			return TRUE;
		}
		else
		{
			mCurrentY = y;
			sPendingDiagonalStep = TRUE;
			return TRUE;
		}
	}
}

// FUNCTION: 0x004d57c0
SWORD *CPolyBucket::GetNextPolyLine(SINT stopAfterCell)
{
	if (!mCells)
		return NULL;
	while (1)
	{
		if (mCurrentTriangle)
		{
			SINT index = mCurrentList->Next();
			if (index == -1)
				mCurrentTriangle = NULL;
			else
				mCurrentTriangle = &mTriangles[index];
			if (mCurrentTriangle)
			{
				if (mCurrentTriangle->mSearchStamp != mSearchStamp &&
					(mCurrentTriangle->mZMask & mSearchZMask))
				{
					mCurrentTriangle->mSearchStamp = (SWORD)mSearchStamp;
					return mCurrentTriangle->mVertices;
				}
			}
			else if (stopAfterCell || !AdvanceLineSearch())
				return NULL;
		}
		else
		{
			mCurrentList = mCells[mCurrentY * mWidth + mCurrentX];
			if (mCurrentList)
			{
				SINT index = mCurrentList->First();
				if (index == -1)
					mCurrentTriangle = NULL;
				else
					mCurrentTriangle = &mTriangles[index];
			}
			if (mCurrentTriangle)
			{
				if (mCurrentTriangle->mSearchStamp != mSearchStamp &&
					(mCurrentTriangle->mZMask & mSearchZMask))
				{
					mCurrentTriangle->mSearchStamp = (SWORD)mSearchStamp;
					return mCurrentTriangle->mVertices;
				}
			}
			else if (stopAfterCell || !AdvanceLineSearch())
				return NULL;
		}
	}
}

// FUNCTION: 0x004d5930
SINT CPolyBucket::GetRandomTriangle(CPolyBucketVertex **out)
{
	for (SINT attempt = 0; attempt < 1000; ++attempt)
	{
		SINT x = GAME.Random() % mWidth;
		SINT y = GAME.Random() % mHeight;
		CPolyBucketIndexList *cell = mCells[y * mWidth + x];
		if (cell && cell->mEntries > 0)
		{
			CPolyBucketTriangle *triangle = &mTriangles[cell->mData[GAME.Random() % cell->mEntries]];
			if (triangle)
			{
				out[0] = &(*mVertices)[triangle->mVertices[0]];
				out[1] = &(*mVertices)[triangle->mVertices[1]];
				out[2] = &(*mVertices)[triangle->mVertices[2]];
				return 1;
			}
		}
	}
	return 0;
}

inline void CPolyBucketIndexList::ReadBorrowed(CChunkReader *reader, SWORD *indices, SINT &offset)
{
	reader->GetNext();
	SINT count;
	reader->Read(&count, 4, 1);
	mBorrowedData = TRUE;
	mEntries = count;
	mData = &indices[offset];
	offset += mEntries;
	reader->Read(mData, 2, mEntries);
}

// Load the serialized bucket and rebuild its owned arrays. Cell lists borrow slices of the shared index buffer.
// A size mismatch is only logged before reading the recorded size. Each loop owns its counter; a completed list
// advances the consumed-list count before the cell table receives it.
// FUNCTION: 0x004d59f0
CPolyBucket *CPolyBucket::Load(CChunkReader *reader)
{
#line 1136
	CPolyBucket *bucket = new(MEMTYPE_POLYBUCKET) CPolyBucket;
	reader->GetNext();
	reader->GetNext();
	if (reader->GetSize() != sizeof(CPolyBucket))
		TRACE("Size=%d, sizeof=%d\n", reader->GetSize(), sizeof(CPolyBucket));
	reader->Read(bucket, reader->GetSize(), 1);
#line 1164
	bucket->mCells = new(MEMTYPE_POLYBUCKET) CPolyBucketIndexList *[bucket->mHeight * bucket->mWidth];
	SINT numLists = 0;
	SINT numIndices = 0;
	reader->GetNext();
	reader->Read(&numLists, 4, 1);
	reader->Read(&numIndices, 4, 1);
#line 1177
	bucket->mLoadedLists = new(MEMTYPE_POLYBUCKET) CPolyBucketIndexList[numLists];
	bucket->mLoadedIndices = new(MEMTYPE_POLYBUCKET) SWORD[numIndices];
	bucket->mLoadedCells = TRUE;
	SINT listIndex = 0;
	SINT indexOffset = 0;
	for (SINT i = 0; i < bucket->mHeight * bucket->mWidth; ++i)
	{
		bucket->mCells[i] = NULL;
		reader->GetNext();
		SINT present;
		reader->Read(&present, 4, 1);
		if (present == 1)
		{
			reader->GetNext();
			CPolyBucketIndexList *list = &bucket->mLoadedLists[listIndex];
			list->ReadBorrowed(reader, bucket->mLoadedIndices, indexOffset);
			++listIndex;
			bucket->mCells[i] = list;
		}
	}
	reader->GetNext();
	SINT numTriangles;
	reader->Read(&numTriangles, 4, 1);
	reader->GetNext();
#line 1226
	bucket->mTriangles = new(MEMTYPE_POLYBUCKET) CPolyBucketTriangle[numTriangles];
	for (SINT j = 0; j < numTriangles; ++j)
	{
		CPolyBucketTriangle *triangle = &bucket->mTriangles[j];
		reader->GetNext();
		reader->Read(&triangle->mZMask, 4, 1);
		reader->Read(triangle->mVertices, 6, 1);
		reader->Read(&triangle->mSearchStamp, 2, 1);
	}
	reader->GetNext();
	SINT numVertices;
	reader->Read(&numVertices, 4, 1);
#line 1242
	bucket->mVertices = new(MEMTYPE_POLYBUCKET) COSet<CPolyBucketVertex>(numVertices);
	for (SINT k = 0; k < numVertices; ++k)
	{
		reader->GetNext();
		CPolyBucketVertex vertex;
		reader->Read(&vertex, 6, 1);
		bucket->mVertices->Add(vertex);
	}
	bucket->mCurrentTriangle = NULL;
	return bucket;
}

// FUNCTION: 0x004d5e20
CPolyBucketIndexList::CPolyBucketIndexList()
{
	mData = NULL;
	mCapacity = 0;
	mEntries = 0;
	mBorrowedData = FALSE;
}

// Draw both windings of every triangle occurrence, retaining the unpacked point values until upload.
// FUNCTION: 0x004d5e30
void CPolyBucket::DebugRender()
{
	RENDERINFO.SRS(RS_ALPHABLENDENABLE, TRUE);
	RENDERINFO.SRS(RS_SRCBLEND, BLEND_SRCALPHA);
	RENDERINFO.SRS(RS_DESTBLEND, BLEND_INVSRCALPHA);
	RENDERINFO.Apply();
	CVBufTexture *vb = CVBufTexture::GetTextureByTex(ENGINE.GetDefaultTexture());
	vb->SetupV(FVF_GTEX_FLAGS, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC,
			sizeof(CFVF_GTEX), D3DPT_TRIANGLELIST, D3DPOOL_DEFAULT);
	vb->SetupI(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 2, D3DPOOL_DEFAULT);
	for (SINT cellIndex = 0; cellIndex < mWidth * mHeight; ++cellIndex)
	{
		CPolyBucketIndexList *cell = mCells[cellIndex];
		if (cell)
		{
			for (SINT index = cell->First(); index >= 0; index = cell->Next())
			{
				CPolyBucketTriangle *triangle = &mTriangles[index];
				FVector a, b, c;
				(*mVertices)[triangle->mVertices[0]].Unpack(&a, &mPacking);
				(*mVertices)[triangle->mVertices[1]].Unpack(&b, &mPacking);
				(*mVertices)[triangle->mVertices[2]].Unpack(&c, &mPacking);
				// Retail leaves the normal fields unwritten and emits each cell occurrence.
				CFVF_GTEX verts[3];
				verts[0].mPos = a;
				verts[1].mPos = b;
				verts[2].mPos = c;
				verts[0].mU = 0.f;
				verts[0].mV = 0.f;
				verts[0].mDiffuse = 0xffffffff;
				verts[1].mU = 1.f;
				verts[1].mV = 0.f;
				verts[1].mDiffuse = 0xffffffff;
				verts[2].mU = 1.f;
				verts[2].mV = 1.f;
				verts[2].mDiffuse = 0xffffffff;
				SINT base = vb->AddVertices(verts, 3);
				UWORD indices[6];
				indices[0] = base;
				indices[1] = base + 1;
				indices[2] = base + 2;
				indices[3] = base + 2;
				indices[4] = base + 1;
				indices[5] = base;
				vb->AddIndices(indices, 6);
			}
		}
	}
	vb->Draw(true);
	SAFE_RELEASE(vb);
	RENDERINFO.SRS(RS_SRCBLEND, BLEND_SRCALPHA);
	RENDERINFO.SRS(RS_DESTBLEND, BLEND_INVSRCALPHA);
	RENDERINFO.Apply();
}

//******************************************************************************************
// A string copy of at most n characters, the terminator included (CDebugLog::Printf 0044185c).
char *StrCopyN(char *dst, char *src, SINT n)	// FUNCTION: 0x004d6240
{
	char	*start = dst;
	while (n > 0)
	{
		char	ch = *src;
		*dst = ch;
		dst++;
		src++;
		if (!ch)
			break;
		n--;
	}
	return start;
}
// STAND-IN (link layout): retail's 6112 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_PolyBucket[6112] = { 0 };
