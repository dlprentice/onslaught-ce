// MeshPart.cpp: reconstructed from MeshPart.obj (004ade50..004b4aaf); Stuart's release has no copy. The allocations'
// __LINE__ values are the retail ones; the #line directives place them, because the retail file's layout is unknown.
#include "common.h"
#include "mesh.h"
#include "membuffer.h"
#include "DXMemBuffer.h"
#include "mathsconst.h"
#include <d3d9.h>
#include "Texture.h"
#include "VBufTexture.h"
#include "PolyBucket.h"
#include "DXMeshVB.h"
#include "DebugLog.h"
#include "chunker.h"
#include "Motioncontroller.h"
#include "game.h"
#include "CLIParams.h"

// FUNCTION: 0x004a51f0
CMeshPart::~CMeshPart()
{
	WipeVertexData();
}

// GetWorldTransforms's last results, one set for the part's own space (local) and one for the world, each good for the
// render frame and animation frame it was made in. CMeshPart's constructor clears them (004ae5b5..004ae5bf).
static SINT		sWorldCacheRender;
static SINT		sLocalCacheRender;
static SINT		sWorldCacheFrame;
static SINT		sLocalCacheFrame;
static FMatrix	sWorldCacheOri;
static FMatrix	sLocalCacheOri;
static FVector	sWorldCachePos;
static FVector	sLocalCachePos;
static IRenderableThing	*sCacheThing = NULL;
static CMeshPart	*sCachePart = NULL;


// FUNCTION: 0x004ade50 init:ZERO_FVECTOR
// FUNCTION: 0x004ade70 init:ID_FMATRIX
// 004adf40, 004adf50, 004adf60 and 004adf70 (a lone ret each) are the four cache vectors' and matrices' empty
// initializers; with no relocation to name them they are placed by their .CRT$XCU order (initorder:K).
// FUNCTION: 0x004adf40 initorder:3
// FUNCTION: 0x004adf50 initorder:4
// FUNCTION: 0x004adf60 initorder:5
// FUNCTION: 0x004adf70 initorder:6

//******************************************************************************************
CMeshTexture::CMeshTexture()	// FUNCTION: 0x004adf80
{
	mFrames = 0;
}

//******************************************************************************************
CMeshTexture::~CMeshTexture()	// FUNCTION: 0x004adf90
{
	if (mFrames)
	{
		SAFE_DELETE(mTextureBlend);
		mTextureU = NULL;
		mTextureV = NULL;
		mTextureUScale = NULL;
		mTextureVScale = NULL;
		mFrames = 0;
	}
	if (mTexture)
	{
		mTexture->Release();
		mTexture = NULL;
	}
	if (mVBuffer)
	{
		mVBuffer->Release();
		mVBuffer = NULL;
	}
}

//******************************************************************************************
#line 86
void CMeshTexture::SetNumFrames(SINT n)	// FUNCTION: 0x004adff0
{
	if (mFrames)
	{
		SAFE_DELETE(mTextureBlend);
		mTextureU = NULL;
		mTextureV = NULL;
		mTextureUScale = NULL;
		mTextureVScale = NULL;
	}

	mFrames = n;
	if (!n)
	{
		mTextureBlend = NULL;
		mTextureU = NULL;
		mTextureV = NULL;
		mTextureUScale = NULL;
		mTextureVScale = NULL;
		return;
	}
	mTextureBlend = new(MEMTYPE_MESHTEXTURE) float[mFrames * 5];
	mTextureU = mTextureBlend + mFrames;
	mTextureV = mTextureBlend + mFrames * 2;
	mTextureUScale = mTextureBlend + mFrames * 3;
	mTextureVScale = mTextureBlend + mFrames * 4;
}

//******************************************************************************************
void CMeshTexture::InitDefault()	// FUNCTION: 0x004ae080
{
	mTexture = NULL;
	SetNumFrames(1);
	mTextureU[0] = 0;
	mTextureV[0] = 0;
	mTextureUScale[0] = 1.0f;
	mTextureVScale[0] = 1.0f;
	mTextureBlend[0] = 1.0f;
	mMaxBlend = 1.0f;
	mVBuffer = NULL;
}

//******************************************************************************************
void CMeshTexture::CreateVBuffer()	// FUNCTION: 0x004ae0d0
{
	mVBuffer = CVBufTexture::GetOrCreate(mTexture, 0);
	mVBuffer->SetVBFormat(FVF_GTEX_FLAGS, D3DUSAGE_WRITEONLY, sizeof(CFVF_GTEX), D3DPT_TRIANGLELIST, D3DPOOL_MANAGED);
	mVBuffer->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY, 2, D3DPOOL_MANAGED);
}

//******************************************************************************************
BOOL CMeshPart::GetFirstPoly(FVector *pos, float radius, CPolyBucketVertex **out)	// FUNCTION: 0x004ae110
{
	if (!mPolyBucket)
		return FALSE;
	SWORD	*tri = mPolyBucket->StartSearch(pos, radius);
	if (!tri)
		return FALSE;
	out[0] = &(*mPolyBucket->mVertices)[tri[0]];
	out[1] = &(*mPolyBucket->mVertices)[tri[1]];
	out[2] = &(*mPolyBucket->mVertices)[tri[2]];
	return TRUE;
}

//******************************************************************************************
BOOL CMeshPart::GetNextPoly(CPolyBucketVertex **out)	// FUNCTION: 0x004ae1a0
{
	SWORD	*tri;
	if ((mPolyBucket) && (tri = mPolyBucket->GetNextPoly()))
	{
		out[0] = &(*mPolyBucket->mVertices)[tri[0]];
		out[1] = &(*mPolyBucket->mVertices)[tri[1]];
		out[2] = &(*mPolyBucket->mVertices)[tri[2]];
		return TRUE;
	}
	return FALSE;
}

//******************************************************************************************
BOOL CMeshPart::GetFirstPolyLine(FVector *start, FVector *end, CPolyBucketVertex **out)	// FUNCTION: 0x004ae220
{
	if (!mPolyBucket)
		return FALSE;
	SWORD	*tri = mPolyBucket->StartLineSearch(start, end);
	if (!tri)
		return FALSE;
	out[0] = &(*mPolyBucket->mVertices)[tri[0]];
	out[1] = &(*mPolyBucket->mVertices)[tri[1]];
	out[2] = &(*mPolyBucket->mVertices)[tri[2]];
	return TRUE;
}

//******************************************************************************************
#line 226
void CMeshPart::BuildPolyBucket()	// FUNCTION: 0x004ae2b0
{
	if (mPolyBucket)
		return;
	if ((mPartType != 1) && (mPartType != 3))
		return;

	mPolyBucket = new(MEMTYPE_POLYBUCKET) CPolyBucket;
	if (!mPolyBucket)
		return;

	CMeshPart	*copy = Clone();
	if (stricmp(mBaseMesh->GetName(), "boss-fenrir.msh") && stricmp(mBaseMesh->GetName(), "tempbuilding3.msh"))
	{
		if (CLIPARAMS.mBuildResources)
			copy->ReducePolys();
	}
	else
		LOG.AddMessage("Ignoring mesh '%s' for poly bucket reduction", mBaseMesh->GetName());

	if (!mPolyBucket->Build(copy))
		SAFE_DELETE(mPolyBucket);

	for (SINT i = 0; i < copy->mNumChildren; i++)
		copy->mChildren[i] = NULL;
	delete copy;
}

//******************************************************************************************
BOOL CMeshPart::GetNextPolyLine(CPolyBucketVertex **out, SINT a)	// FUNCTION: 0x004ae430
{
	SWORD	*tri;
	if ((mPolyBucket) && (tri = mPolyBucket->GetNextPolyLine(a)))
	{
		out[0] = &(*mPolyBucket->mVertices)[tri[0]];
		out[1] = &(*mPolyBucket->mVertices)[tri[1]];
		out[2] = &(*mPolyBucket->mVertices)[tri[2]];
		return TRUE;
	}
	return FALSE;
}

//******************************************************************************************
#line 311
CMeshPart::CMeshPart()	// FUNCTION: 0x004ae4b0
{
	mDVertices = NULL;
	mPVertices = NULL;
	mTriangles = NULL;
	mBones = NULL;
	mBoneSlots = NULL;
	mBoneWeights = NULL;
	mVHFrameMap = NULL;
	mHFramePositions = NULL;
	mCachedOrientation = NULL;
	mHFrameOrientations = NULL;
	mHFrameFOVs = NULL;
	mNextMeshPartInChain = NULL;
	mNumChainedParts = 0;
	mIsChained = FALSE;
	mNumTriangles = 0;
	mNumDVertices = 0;
	mNumPVertices = 0;
	mNumVFrames = 0;
	mNumAFrames = 0;
	mOffsetPos = ZERO_FVECTOR;
	mOrientation = ID_FMATRIX;
	mRadius = 0.5f;
	mNumChildren = 0;
	mParent = NULL;
	mNumBones = 0;
	mPartNumber = 0;
	mPolyBucket = NULL;
	mReferenceTo = NULL;
	sLocalCacheRender = sWorldCacheRender = 99999;
	sCacheThing = NULL;
	mNumCachedFrames = 0;
	mCachedPosition = NULL;
	mPositionStatic = FALSE;
	mOrientationStatic = FALSE;

	mBoundingBox = new(MEMTYPE_MESH) CBoundingBox;


	mMeshVB = new(MEMTYPE_MESH) CDXMeshVB;
	mMeshVB->mPart = this;
}

//******************************************************************************************
void CMeshPart::WipeVertexData()	// FUNCTION: 0x004ae640
{
	SAFE_DELETE(mCachedPosition);
	SAFE_DELETE(mCachedOrientation);
	if (mNumChildren > 0)
		SAFE_DELETE(mChildren);
	if (mDVertices)
		delete [] mDVertices;
	if (mPVertices)
	{
		for (SINT i = 0; i < mNumAFrames; i++)
			if (mPVertices[i])
				delete [] mPVertices[i];
		delete [] mPVertices;
	}
	if (mTriangles)
		delete [] mTriangles;
	if (mVHFrameMap)
		delete [] mVHFrameMap;
	if (mHFrameOrientations)
		delete [] mHFrameOrientations;
	if (mHFramePositions)
		delete [] mHFramePositions;
	if (mHFrameFOVs)
		delete [] mHFrameFOVs;
	if (mBones)
		delete [] mBones;
	if (mBoneWeights)
	{
		for (SINT i = 0; i < mNumPVertices; i++)
			delete [] mBoneWeights[i];
		delete [] mBoneWeights;
	}
	if (mBoneSlots)
	{
		for (SINT i = 0; i < mNumPVertices; i++)
			delete [] mBoneSlots[i];
		delete [] mBoneSlots;
	}
	if (mPolyBucket)
		delete mPolyBucket;
	SAFE_DELETE(mBoundingBox);
	SAFE_DELETE(mMeshVB);
}

//******************************************************************************************
#line 512
BOOL CMeshPart::InitMemory(SINT vertices, SINT positions, SINT triangles, SINT frames, SINT position_sets)	// FUNCTION: 0x004ae860
{
	mNumDVertices = vertices;
	mNumPVertices = positions;
	mNumTriangles = triangles;
	mNumVFrames = frames;
	mNumAFrames = position_sets;

#line 520
	mDVertices = new(MEMTYPE_MESH) CMeshDVertex[vertices];
	if (!mDVertices)
		return FALSE;

#line 534
	mPVertices = new(MEMTYPE_MESH) FVector*[mNumAFrames];
	if (!mPVertices)
		return FALSE;

	for (SINT i = 0; i < mNumAFrames; i++)
	{
#line 544
		mPVertices[i] = new(MEMTYPE_MESH) FVector[mNumPVertices];
		if (!mPVertices[i])
			return FALSE;
	}

#line 552
	mTriangles = new(MEMTYPE_MESH) CMeshTriangle[mNumTriangles];
	if (!mTriangles)
		return FALSE;

	MEM_MANAGER.TagMem(mDVertices, mNumDVertices * sizeof(CMeshDVertex), "Meshes/%s/Part %d/DVertices", mBaseMesh->mName, mPartNumber);
	MEM_MANAGER.TagMem(mPVertices, mNumAFrames * mNumPVertices * sizeof(FVector), "Meshes/%s/Part %d/PVertices", mBaseMesh->mName, mPartNumber);
	MEM_MANAGER.TagMem(mTriangles, mNumTriangles * sizeof(CMeshTriangle), "Meshes/%s/Part %d/Triangles", mBaseMesh->mName, mPartNumber);
	return TRUE;
}

// |f| (the bounding box's half-sizes, 004aec9a..004aece2).
inline float PositiveOf(float f)
{
	return (f > 0) ? f : -f;
}

//******************************************************************************************
// The box around the first position set, the farthest position from the part's origin and the median half-size.
void CMeshPart::GenerateSimpleBoundingBox()	// FUNCTION: 0x004aea50
{
	if (!mPVertices)
		return;

	mBoundingBox->mValid = FALSE;
	float	radius = 0;
	float	min_x, max_x, min_y, max_y, min_z, max_z;
	min_x = 999999.0f;
	min_y = 999999.0f;
	min_z = 999999.0f;
	max_x = -999999.0f;
	max_y = -999999.0f;
	max_z = -999999.0f;

	FVector	*p = mPVertices[0];
	for (SINT i = 0; i < mNumPVertices; p++, i++)
	{
		FVector	v = *p;
		if (v.X < min_x)
			min_x = v.X;
		if (v.Y < min_y)
			min_y = v.Y;
		if (v.Z < min_z)
			min_z = v.Z;
		if (v.X > max_x)
			max_x = v.X;
		if (v.Y > max_y)
			max_y = v.Y;
		if (v.Z > max_z)
			max_z = v.Z;

		float	r = (v + mOffsetPos).MagnitudeSq();
		if (r > radius)
			radius = r;
		mBoundingBox->mValid = TRUE;
	}

	mBoundingBox->mOrigin = FVector(max_x + min_x, max_y + min_y, max_z + min_z) * 0.5f;
	mBoundingBox->mAxes = FVector(min_x - max_x, min_y - max_y, min_z - max_z) * 0.5f;
	mBoundingBox->mAxes.X = PositiveOf(mBoundingBox->mAxes.X);
	mBoundingBox->mAxes.Y = PositiveOf(mBoundingBox->mAxes.Y);
	mBoundingBox->mAxes.Z = PositiveOf(mBoundingBox->mAxes.Z);

	FVector	&a = mBoundingBox->mAxes;
	mCueFactor = a.Z;
	if (((a.X <= a.Y) && (a.Y <= a.Z)) || ((a.Z <= a.Y) && (a.Y <= a.X)))
		mCueFactor = a.Y;
	if (((a.Y <= a.X) && (a.X <= a.Z)) || ((a.Z <= a.X) && (a.X <= a.Y)))
		mCueFactor = a.X;

	mRadius = (float)sqrt(radius);
	if (!mBoundingBox->mValid)
	{
		mBoundingBox->mOrigin.Set(0, 0, 0);
		mBoundingBox->mAxes.Set(0, 0, 0);
		mRadius = 0;
	}

	mBoundingBox->CalculateRadius();
}

// m * v for Read202 (004af110) only, each product bracketed as in FMatrix * FVector. The operator itself leaves 202 apart
// (shape 18/20/61: row 0's terms come out Y, Z, X where retail has X, Y, Z; the key skip's spellings change nothing). Read201 and
// Merge's normal-loop inverse product match with the operator (review 2026-10-14).
inline FVector RowProducts(const FMatrix &m, const FVector &v)
{
	return FVector((m.Row[0].X * v.X) + (m.Row[0].Y * v.Y) + (m.Row[0].Z * v.Z),
				   (m.Row[1].X * v.X) + (m.Row[1].Y * v.Y) + (m.Row[1].Z * v.Z),
				   (m.Row[2].X * v.X) + (m.Row[2].Y * v.Y) + (m.Row[2].Z * v.Z));

}

//******************************************************************************************
// A version 2.01 part's vertices (one texture each, no normals), positions (stored relative to the part and
// turned by its orientation) and triangles (wound the other way). Near miss: the position loop's counter updates
// are scheduled before the transform's stores (0054efd9) where retail has them after (004af02c).
void CMeshPart::Read201(CDXMemBuffer *file, CMesh *mesh, CMeshTexture *textures, SINT num_textures, SINT unused)	// FUNCTION: 0x004aede0
{
	SINT	i;
	for (i = 0; i < mNumDVertices; i++)
	{
		file->Read(&mDVertices[i].mPVert, 4);
		file->Read(&mDVertices[i].mU, 4);
		file->Read(&mDVertices[i].mV, 4);
		mDVertices[i].mV = -mDVertices[i].mV;
		file->Read(&mDVertices[i].mDiffuse, 4);
		SINT	texture;
		file->Read(&texture, 4);
		if ((texture < -2) || (texture >= num_textures))
			texture = -1;
		for (SINT t = 0; t < 6; t++)
			mDVertices[i].mTexture[t] = -1;
		if (texture == -1)
			mDVertices[i].mTexture[0] = 0;
		else
			mDVertices[i].mTexture[0] = texture;
		mDVertices[i].mNormal = FVector(1.0f, 0, 0);
	}

	for (SINT s = 0; s < mNumAFrames; s++)
	{
		SINT	j;
		for (j = 0; j < mNumPVertices; j++)
			mPVertices[s][j].Read(file);
		for (j = 0; j < mNumPVertices; j++)
		{
			mPVertices[s][j] -= mOffsetPos;
			mPVertices[s][j] = mOrientation.Transpose() * mPVertices[s][j];
		}
	}

	for (i = 0; i < mNumTriangles; i++)
	{
		SINT	a, b, c;
		file->Read(&a, 4);
		file->Read(&b, 4);
		file->Read(&c, 4);
		mTriangles[i].mVertices[0] = &mDVertices[a];
		mTriangles[i].mVertices[1] = &mDVertices[c];
		mTriangles[i].mVertices[2] = &mDVertices[b];
	}
	EvaluateNormals(true);
}

//******************************************************************************************
// A version 2.02 part (as 2.01, with a frame table it skips): the same near miss as 2.01's.
// Its vertices (one texture each, no normals), positions (stored relative to the part and
// turned by its orientation) and triangles (wound the other way). 
void CMeshPart::Read202(CDXMemBuffer *file, CMesh *mesh, CMeshTexture *textures, SINT num_textures, SINT unused)	// FUNCTION: 0x004af110
{
	SINT	i;
	for (i = 0; i < mNumDVertices; i++)
	{
		file->Read(&mDVertices[i].mPVert, 4);
		file->Read(&mDVertices[i].mU, 4);
		file->Read(&mDVertices[i].mV, 4);
		mDVertices[i].mV = -mDVertices[i].mV;
		file->Read(&mDVertices[i].mDiffuse, 4);
		SINT	texture;
		file->Read(&texture, 4);
		if ((texture < -2) || (texture >= num_textures))
			texture = -1;
		for (SINT t = 0; t < 6; t++)
			mDVertices[i].mTexture[t] = -1;
		if (texture == -1)
			mDVertices[i].mTexture[0] = 0;
		else
			mDVertices[i].mTexture[0] = texture;
		mDVertices[i].mNormal = FVector(1.0f, 0, 0);
	}

	SINT	key;
	for (i = 0; i < mNumVFrames; i++)
		file->Read(&key, 4);

	for (SINT s = 0; s < mNumAFrames; s++)
	{
		SINT	j;
		for (j = 0; j < mNumPVertices; j++)
			mPVertices[s][j].Read(file);
		for (j = 0; j < mNumPVertices; j++)
		{
			mPVertices[s][j] -= mOffsetPos;
			mPVertices[s][j] = RowProducts(mOrientation.Transpose(), mPVertices[s][j]);
		}
	}

	for (i = 0; i < mNumTriangles; i++)
	{
		SINT	a, b, c;
		file->Read(&a, 4);
		file->Read(&b, 4);
		file->Read(&c, 4);
		mTriangles[i].mVertices[0] = &mDVertices[a];
		mTriangles[i].mVertices[1] = &mDVertices[c];
		mTriangles[i].mVertices[2] = &mDVertices[b];
	}
	EvaluateNormals(true);
}

//******************************************************************************************
// STAND-IN (explicit reconstruction concession, 2026-10-03). Read203 needs the plain X-column value
// inside a transpose operation: the shared named-float GetColumnX is not expanded here. The two local readers
// below reproduce retail's column construction without changing the shared maths methods other callers use.
// This local result type inherits FMatrix's rows and adds no members. Its multiply is the same three dot products
// as FMatrix::operator*, with a parenthesis pair moved from the Z product to Y in rows 1 and 2. The arithmetic
// operations and associations are unchanged; these redundant parentheses affect VC6's IL operand ordering.
// Each row change alone leaves 9 differing bytes; together they match all 1856 section bytes and 25 relocations.
// Adding both pairs without removing the Z pairs instead changes later triangle address expressions and fails.
// The local type/helper ownership and parentheses are ours: Stuart's original maths source is lost. VC6 emits
// one additional constructor COMDAT for this type, called by Read203. Its entire 96-byte body and empty
// outgoing reference graph independently match the existing retail FMatrix constructor at 00401f10; the matched
// call resolves to that same implementation. Only the caller and its FPO metadata reference the alias. These
// concessions are revisitable when the original interfaces are recovered. Removing the local reconstruction restores the recorded Read203 near miss.
class ReadMeshPartMatrix : public FMatrix
{
public:
	ReadMeshPartMatrix(const FVector &x, const FVector &y, const FVector &z) : FMatrix(x, y, z) {}	// FUNCTION: 0x00401f10 ??0ReadMeshPartMatrix@@QAE@ABVFVector@@00@Z
	FVector operator*(const FVector &v) const
	{
		return FVector((Row[0].X * v.X) + (Row[0].Y * v.Y) + (Row[0].Z * v.Z),
				(Row[1].X * v.X) + ((Row[1].Y * v.Y)) + Row[1].Z * v.Z,
				(Row[2].X * v.X) + ((Row[2].Y * v.Y)) + Row[2].Z * v.Z);
	}
};

inline FVector ReadMeshPartColumnX(const FMatrix &m)
{
	return FVector(m.Row[0].X, m.Row[1].X, m.Row[2].X);
}

inline ReadMeshPartMatrix ReadMeshPartTranspose(const FMatrix &m)
{
	return ReadMeshPartMatrix(ReadMeshPartColumnX(m), m.GetColumnY(), m.GetColumnZ());
}

// A version 2.03 (or later old-format) part: as 2.02, then per-corner normals. A vertex takes the first normal a
// triangle gives it; a corner whose normal differs uses (or makes) a copy of the vertex with that normal.
// The split-vertex search sets a flag at the match, as Read204's does: retail jumps from the match straight to
// the found arm (004af965). The position sets go through i, as in Read204: retail's frame has i and s in one
// slot class, and with one counter every named local's slot is retail's.
// Matched through the explicitly labelled local column/transpose/multiply reconstruction above. The position
// transform uses its matrix result directly; the plain column boundary preserves the two constructor calls
// retail makes at 004af68a and 004af6a1, while the redistributed parentheses preserve the two Y-before-X
// product orders at 004af6eb and 004af707 without changing the later pointer sums.
#line 820
void CMeshPart::Read203(CDXMemBuffer *file, CMesh *mesh, CMeshTexture *textures, SINT num_textures, SINT unused)	// FUNCTION: 0x004af470
{
	SINT i; for (i = 0; i < mNumDVertices; i++)
	{
		file->Read(&mDVertices[i].mPVert, 4);
		file->Read(&mDVertices[i].mU, 4);
		file->Read(&mDVertices[i].mV, 4);
		mDVertices[i].mV = -mDVertices[i].mV;
		file->Read(&mDVertices[i].mDiffuse, 4);
		SINT	texture;
		file->Read(&texture, 4);
		if ((texture < -2) || (texture >= num_textures))
			texture = -1;
		for (SINT t = 0; t < 6; t++)
			mDVertices[i].mTexture[t] = -1;
		if (texture == -1)
			mDVertices[i].mTexture[0] = 0;
		else
			mDVertices[i].mTexture[0] = texture;
		mDVertices[i].mNormal = FVector(-10.0f, 0, 0);
	}

	SINT	key;
	for (i = 0; i < mNumVFrames; i++)
		file->Read(&key, 4);

	for (i = 0; i < mNumAFrames; i++)
	{
		SINT j; for (j = 0; j < mNumPVertices; j++)
			mPVertices[i][j].Read(file);
		for (j = 0; j < mNumPVertices; j++)
		{
			mPVertices[i][j] -= mOffsetPos;
			mPVertices[i][j] = ReadMeshPartTranspose(mOrientation) * mPVertices[i][j];
		}
	}

#line 898
	CMeshDVertex	*extra = new(MEMTYPE_MESH) CMeshDVertex[mNumDVertices * 5];
	SINT		num_extra = 0;
	for (i = 0; i < mNumTriangles; i++)
	{
		SINT	a, b, c;
		file->Read(&a, 4);
		file->Read(&b, 4);
		file->Read(&c, 4);
		mTriangles[i].mVertices[0] = &mDVertices[a];
		mTriangles[i].mVertices[1] = &mDVertices[c];
		mTriangles[i].mVertices[2] = &mDVertices[b];

		CMeshDVertex	*next = &extra[num_extra];
		for (SINT k = 0; k < 3; k++)
		{
			CMeshDVertex	**corner;
			switch (k)
			{
				case 0:	corner = &mTriangles[i].mVertices[0];	break;
				case 1:	corner = &mTriangles[i].mVertices[2];	break;
				case 2:	corner = &mTriangles[i].mVertices[1];	break;
			}
			FVector	normal;
			file->Read(&normal, 12);
			normal.Z = -normal.Z;

			CMeshDVertex	*v = *corner;
			if (v->mNormal.X == -10.0f)		// still -10: no normal yet
				v->mNormal = normal;
			else if (!(v->mNormal == normal))
			{
				BOOL	found = FALSE;
				SINT e; for (e = 0; e < num_extra; e++)
					if ((extra[e].mPVert == v->mPVert) && (extra[e].mTexture[0] == v->mTexture[0]) && (extra[e].mNormal == normal))
					{
						found = TRUE;
						break;
					}
				if (found)
					*corner = &mDVertices[mNumDVertices + e];
				else
				{
					*next = *v;
					next->mNormal = normal;
					*corner = &mDVertices[mNumDVertices + num_extra];
					num_extra++;
					next++;
				}
			}
		}
	}

	if (num_extra > 0)
	{
		CMeshDVertex	*old = mDVertices;
#line 988
		mDVertices = new(MEMTYPE_MESH) CMeshDVertex[mNumDVertices + num_extra];
		memcpy(mDVertices, old, mNumDVertices * sizeof(CMeshDVertex));
		memcpy(&mDVertices[mNumDVertices], extra, num_extra * sizeof(CMeshDVertex));
		for (i = 0; i < mNumTriangles; i++)
		{
			mTriangles[i].mVertices[0] = &mDVertices[mTriangles[i].mVertices[0] - old];
			mTriangles[i].mVertices[1] = &mDVertices[mTriangles[i].mVertices[1] - old];
			mTriangles[i].mVertices[2] = &mDVertices[mTriangles[i].mVertices[2] - old];
		}
		delete [] old;
		mNumDVertices += num_extra;
	}
	delete [] extra;
	EvaluateNormals(false);
}

//******************************************************************************************
// Versions 2.04 to 2.07 (CMesh::Load 004a841d): vertices with up to six texture layers, the positions without the
// older versions' transform, a skinned part's bones and weights, and a normal per triangle corner (a vertex whose
// corners disagree is split, as in 2.03).
// The search through the split vertices sets a flag at the match: VC6 then jumps from the match straight to the found
// arm and from the loop's end to the new vertex, as retail does (004b05b0, 004b05bc). Tested as `e < num_extra` after
// the loop, ours tested e again on the break edge, which also kept num_extra in a register where retail reloads it.
// The position sets are read with i, the function's counter, as retail's frame needs: with a counter of their own the
// stack-slot packer's list puts i and s apart and four of retail's slot classes come out differently.
// STAND-IN: the vertex count is added under a second `if (num_extra > 0)` after the
// copy-and-remap block, not inside it. The second test emits no code: VC6 threads both ways into it (false where the
// first test failed, true at the block's end), so the code is one block's. It changes the order of the delete block's
// predecessors. VC6 threads the triangle loop's guard (004b041c, num_extra still 0) past the num_extra test to the
// delete block; label references are prepended and the flow graph lists a label's jumps oldest first, so with one block
// the num_extra test's block comes first, and with the second test the num_extra test's jump is threaded after the
// guard's and the guard's block comes first. With over 100 blocks the allocator splits a web by regions (C2
// 0x107216f1): a block joins its first predecessor's region if they share a free register. When the triangle offset
// takes edi, the zero web is split; with the num_extra test first the delete block joins the loop's region, the guard
// block gets a kill at its end, and the zero piece of the null test, the zero stores and the guard stops short of the
// offset's copy in the preheader (one block: `xor ecx,ecx` at 004b03f7 and a second `xor edi,edi`, shape 1/13). With
// the guard first the delete block joins the guard's region, and the piece reaches the copy and takes the offset's
// register, retail's single `xor edi,edi`. A patched C2 that only moves the guard's edge to the front gives the same
// match, as does splitting the block after `delete [] old` instead. The original spelling is unknown.
void CMeshPart::Read204(CDXMemBuffer *file, CMesh *mesh, CMeshTexture *textures, SINT num_textures, SINT unused, SINT num_layers, UINT version)	// FUNCTION: 0x004afbb0
{
	SINT	i;
	for (i = 0; i < mNumDVertices; i++)
	{
		file->Read(&mDVertices[i].mPVert, 4);
		file->Read(&mDVertices[i].mU, 4);
		file->Read(&mDVertices[i].mV, 4);
		mDVertices[i].mV = -mDVertices[i].mV;
		// ABGR in the file, ARGB in the vertex
		SINT	colour;
		file->Read(&colour, 4);
		UINT	alpha = (UINT)colour >> 24;
		mDVertices[i].mDiffuse = (((((alpha << 8) + (colour & 0xff)) << 8) + ((colour >> 8) & 0xff)) << 8) + ((colour >> 16) & 0xff);
		SINT	t;
		for (t = 0; t < 6; t++)
			mDVertices[i].mTexture[t] = -1;

		for (t = 0; t < num_layers; t++)
		{
			SINT	texture;
			float	speed, u, v, w, h;
			file->Read(&texture, 4);
			if ((version == MKID("2.05")) || (version == MKID("2.06")))
			{
				file->Read(&speed, 4);
				u = 0;
				v = 0;
				w = 1.0f;
				h = 1.0f;
			}
			else
				speed = 1.0f;
			if (version == MKID("2.06"))
			{
				file->Read(&u, 4);
				file->Read(&v, 4);
				file->Read(&w, 4);
				file->Read(&h, 4);
			}

			if ((texture < -2) || (texture >= num_textures))
				texture = -1;
			if (texture == -1)
			{
				// both arms index with t: retail's zero store goes through the same dword index as the -1 (004afdfc,
				// 004afe14), where mTextures[0] takes the byte offset and moves the whole section
				if (t == 0)
					mDVertices[i].mTexture[t] = 0;
				else
					mDVertices[i].mTexture[t] = -1;
				continue;
			}

			if (version == MKID("2.07"))
				speed = mesh->mTextures[texture].mMaxBlend;
			if ((t == 4) && (mesh->mTextures[texture].mTexture == mesh->mTextures[mDVertices[i].mTexture[0]].mTexture))
				speed = 0;
			if (speed > 0)
			{
				mDVertices[i].mTexture[t] = texture;
				if ((version == MKID("2.05")) || (version == MKID("2.06")))
				{
					mesh->mTextures[texture].mTextureBlend[0] = speed;
					mesh->mTextures[texture].mTextureU[0] = u;
					mesh->mTextures[texture].mTextureV[0] = v;
					mesh->mTextures[texture].mTextureUScale[0] = w;
					mesh->mTextures[texture].mTextureVScale[0] = h;
					mesh->mTextures[texture].mMaxBlend = speed;
				}
			}
			else
				mDVertices[i].mTexture[t] = -1;
		}
		mDVertices[i].mNormal = FVector(-10.0f, 0, 0);
	}

	SINT	key;
	for (i = 0; i < mNumVFrames; i++)
		file->Read(&key, 4);
	for (i = 0; i < mNumAFrames; i++)
		for (SINT j = 0; j < mNumPVertices; j++)
			mPVertices[i][j].Read(file);

	if (mPartType == 3)
	{
		file->Read(&mNumBones, 4);
		char	message[256];
		sprintf(message, "Got %d bones\n", mNumBones);
		TRACE(message);
#line 1174
		mBones = new(MEMTYPE_MESH) CMeshPart *[mNumBones];
#line 1176
		mBoneWeights = new(MEMTYPE_MESH) float *[mNumPVertices];
#line 1178
		mBoneSlots = new(MEMTYPE_MESH) SINT *[mNumPVertices];
		for (i = 0; i < mNumBones; i++)
		{
			SINT	index;
			file->Read(&index, 4);
			mBones[i] = mesh->mParts[index];
		}

		for (SINT p = 0; p < mNumPVertices; p++)
		{
#line 1196
			mBoneWeights[p] = new(MEMTYPE_MESH) float[mNumBones];
			float	total = 0;
			SINT	b;
			for (b = 0; b < mNumBones; b++)
			{
				file->Read(&mBoneWeights[p][b], 4);
				total += mBoneWeights[p][b];
			}
			if ((total != 1.0f) && (total != 0))
			{
				float	check = 0;
				for (b = 0; b < mNumBones; b++)
				{
					mBoneWeights[p][b] *= 1.0f / total;
					check += mBoneWeights[p][b];
				}
			}

#line 1221
			mBoneSlots[p] = new(MEMTYPE_MESH) SINT[3];
#line 1223
			float	*weights = new(MEMTYPE_MESH) float[mNumBones];
			for (b = 0; b < mNumBones; b++)
				weights[b] = mBoneWeights[p][b];
			for (SINT k = 0; k < 3; k++)
			{
				float	best = 0;
				SINT	best_bone = 0;
				for (b = 0; b < mNumBones; b++)
				{
					if (best < weights[b])
					{
						best = weights[b];
						best_bone = b;
					}
				}
				if (best == 0)
				{
					if (k == 0)
						mBoneSlots[p][0] = -1;
					else
						mBoneSlots[p][k] = mBoneSlots[p][k - 1];
				}
				else
					mBoneSlots[p][k] = best_bone;
				weights[best_bone] -= 1.0f / 3.0f;
			}
			if (weights)
				delete [] weights;
		}
	}

#line 1262
	CMeshDVertex	*extra = new(MEMTYPE_MESH) CMeshDVertex[mNumDVertices * 5];
	SINT		num_extra = 0;
	for (i = 0; i < mNumTriangles; i++)
	{
		SINT	a, b, c;
		file->Read(&a, 4);
		file->Read(&b, 4);
		file->Read(&c, 4);
		mTriangles[i].mVertices[0] = &mDVertices[a];
		mTriangles[i].mVertices[1] = &mDVertices[c];
		mTriangles[i].mVertices[2] = &mDVertices[b];

		CMeshDVertex	*next = &extra[num_extra];
		for (SINT k = 0; k < 3; k++)
		{
			CMeshDVertex	**corner;
			switch (k)
			{
				case 0:	corner = &mTriangles[i].mVertices[0];	break;
				case 1:	corner = &mTriangles[i].mVertices[2];	break;
				case 2:	corner = &mTriangles[i].mVertices[1];	break;
			}
			FVector	normal;
			normal.Read(file);

			CMeshDVertex	*vertex = *corner;
			if (vertex->mNormal.X == -10.0f)		// still -10: no normal yet
				vertex->mNormal = normal;
			else if (!(vertex->mNormal == normal))
			{
				SINT	e;
				BOOL	found = FALSE;
				for (e = 0; e < num_extra; e++)
					if ((extra[e].mPVert == vertex->mPVert) && (extra[e].mTexture[0] == vertex->mTexture[0]) && (extra[e].mNormal == normal))
					{
						found = TRUE;
						break;
					}
				if (found)
					*corner = &mDVertices[mNumDVertices + e];
				else
				{
					*next = *vertex;
					next->mNormal = normal;
					*corner = &mDVertices[mNumDVertices + num_extra];
					num_extra++;
					next++;
				}
			}
		}
	}

	if (num_extra > 0)
	{
		CMeshDVertex	*old = mDVertices;
#line 1353
		mDVertices = new(MEMTYPE_MESH) CMeshDVertex[mNumDVertices + num_extra];
		memcpy(mDVertices, old, mNumDVertices * sizeof(CMeshDVertex));
		memcpy(&mDVertices[mNumDVertices], extra, num_extra * sizeof(CMeshDVertex));
		delete [] old;
		for (i = 0; i < mNumTriangles; i++)
		{
			mTriangles[i].mVertices[0] = &mDVertices[mTriangles[i].mVertices[0] - old];
			mTriangles[i].mVertices[1] = &mDVertices[mTriangles[i].mVertices[1] - old];
			mTriangles[i].mVertices[2] = &mDVertices[mTriangles[i].mVertices[2] - old];
		}
	}
	if (num_extra > 0)	// STAND-IN: a second test, threaded away (see above)
		mNumDVertices += num_extra;
	delete [] extra;
	EvaluateNormals(false);
}

//******************************************************************************************
// The file holds every part's transform and keys in model space; this makes them relative to the parent's (pos and
// ori are the parent's, from its own keys when it has one per key frame). Children go first, while this part's
// transform is still in model space (CMesh::Load calls it on the root, 004a9503).
// Near miss (18 bytes): two copies of a product into the part schedule one load before, not after, the address or
// final add.
void CMeshPart::HierarchyAdjust(FVector pos, FMatrix ori, CMeshPart *parent)	// FUNCTION: 0x004b0800
{
	SINT	i;
	for (i = 0; i < mNumChildren; i++)
		if (mChildren[i])
			mChildren[i]->HierarchyAdjust(mOffsetPos, mOrientation, this);

	mOffsetPos -= pos;
	mOffsetPos = ori.Transpose() * mOffsetPos;
	mOrientation = mOrientation * ori.Transpose();

	for (i = 0; i < mNumHFrames; i++)
	{
		if (parent && (parent->mNumHFrames == i))
		{
			ori = parent->mHFrameOrientations[i];
			pos = parent->mHFramePositions[i];
		}
		mHFramePositions[i] -= pos;
		mHFramePositions[i] = ori.Transpose() * mHFramePositions[i];
		mHFrameOrientations[i] = ori.Transpose() * mHFrameOrientations[i];
	}
}

//******************************************************************************************
FVector CMeshPart::GetBasisY()	// FUNCTION: 0x004b0c00
{
	return FVector(mOrientation.Row[0].Y, mOrientation.Row[1].Y, mOrientation.Row[2].Y);
}

FVector CMeshPart::GetBasisZ()	// FUNCTION: 0x004b0c20
{
	return FVector(mOrientation.Row[0].Z, mOrientation.Row[1].Z, mOrientation.Row[2].Z);
}

//******************************************************************************************
SINT CMeshPart::FindVertex(FVector pos)	// FUNCTION: 0x004b0c40
{
	if ((!mPVertices) || (!mPVertices[0]))
		return 0;

	SINT	best = 0;
	float	best_dist = 99999.0f;
	for (SINT i = 0; i < mNumPVertices; i++)
	{
		float	dist = (mPVertices[0][i] - pos).Magnitude();
		if (dist < best_dist)
		{
			best_dist = dist;
			best = i;
		}
	}
	return best;
}

//******************************************************************************************
CMeshPart *CMeshPart::GetMeshDataPart()	// FUNCTION: 0x004b0cd0
{
	if ((mPartType == 1) || (mPartType == 3))
		return this;
	if (mPartType == 6)
		return mReferenceTo;
	return NULL;
}

//******************************************************************************************
void CMeshPart::GetTransforms(SINT f0, SINT f1, float t, FMatrix *ori, FVector *pos)	// FUNCTION: 0x004b0d00
{
	if (f0 >= mNumVFrames)
		f0 = mNumVFrames - 1;
	if (f1 >= mNumVFrames)
		f1 = mNumVFrames - 1;
	UBYTE	k0 = mVHFrameMap[f0];
	UBYTE	k1 = mVHFrameMap[f1];

	*pos = GetKeyPosition(k0) + (GetKeyPosition(k1) - GetKeyPosition(k0)) * t;
	*ori = GetHFrameOrientation(k0) + (GetHFrameOrientation(k1) - GetHFrameOrientation(k0)) * t;
}

//******************************************************************************************
// The cached frames' readers, file-local: a declaration in CMeshPart, with a body or without, moves CGame::
// DrawDebugStuff 00470650 and CFEPGoodies::Render 0045e0d0. And the identity a part without a parent starts from, row
// by row: GetWorldTransforms expands MakeIdentity, but each row's FVector is constructed out of line (004b16ed..004b1765),
// since VC6 gives an expanded helper's calls only a share of the inline budget left at that point.
static inline FVector &GetCachedPosition(FVector *frames, SINT frame) { return frames[frame]; }
static inline FMatrix &GetCachedOrientation(FMatrix *frames, SINT frame) { return frames[frame]; }

static inline void MakeIdentity(FMatrix &m)
{
	m.Row[0] = FVector(1.0f, 0, 0);
	m.Row[1] = FVector(0, 1.0f, 0);
	m.Row[2] = FVector(0, 0, 1.0f);
}

//******************************************************************************************
// The part's orientation and position at a point between two animation frames: relative to the thing when local,
// else in the world. With no motion controller in the way the part's cached frames serve (from the nearest ancestors
// that have their own); otherwise the parent's transform is built first and this part's key applied to it. The
// last local and world results are kept for the render frame (the ancestors' calls overwrite them).
// The parent's transform is declared between the cache test and the cached frames. Before the cached path its
// FMatrix's empty row constructors still fit the inline budget (declared after it, ours called the vector constructor
// iterator, which retail never does), and parent_ori * *pos goes out of line as in retail (0040d2c0). After the cache
// test, the constructor's loop keeps VC6's optimizer from carrying the cache test's outcome for mc into the second
// test: declared at the top, the first !mc test jumped past the second one and the cache misses past its mc test,
// where retail tests mc again after both (004b1104) and keeps local in edx from its first test (004b1013). The cached
// frames are read through their readers, as GetTransforms reads its keys, pp and op are declared together, local is
// compared with false as it is with true, and the identity rows come from MakeIdentity: with these VC6's inline
// decisions give retail's out-of-line calls in retail's order.
// The cache's part and thing are stored once after the cached path's two arms, and the thing once after the final
// pair: with that, thing is in ebx and pos in ebp as in retail, frame is reloaded before local on the edge into the
// second test, this is spilled to 0x68, and the cache stores keep retail's order (004b1679..004b16a3).
// The cached position goes through a delta from a reference to the next frame's: dp.Y and dp.Z's later reads take two
// table-time ids (loop 1 forwards them, no code), moving every CSE address temporary after them by +2 mod 4, so the
// cached MM dots {ZYX,YZX,XZY}, {XZY,XYZ,ZXY}, {XZY,XYZ,ZXY} (004b1402..) and MV rows' ZYX (004b153b..) are retail's;
// the reference keeps b's add before the two loads (with dp alone it is scheduled after them).
void CMeshPart::GetWorldTransforms(SINT frame, SINT next_frame, float fraction, IRenderableThing *thing, FMatrix *ori, FVector *pos, bool local, bool controller, bool uncached)	// FUNCTION: 0x004b0fb0
{
	SINT	render = GAME.GetRenderFrameNumber();
	CMotionController	*mc = (thing && controller) ? thing->GetMotionController() : NULL;

	if (thing && (thing == sCacheThing) && (sCachePart == this) && !mc)
	{
		if (local == false)
		{
			if ((render == sWorldCacheRender) && (frame == sWorldCacheFrame))
			{
				*ori = sWorldCacheOri;
				*pos = sWorldCachePos;
				return;
			}
		}
		else if (local == true)
		{
			if ((render == sLocalCacheRender) && (frame == sLocalCacheFrame))
			{
				*ori = sLocalCacheOri;
				*pos = sLocalCachePos;
				return;
			}
		}
	}

	FMatrix	parent_ori;
	FVector	parent_pos;
	if (!mc && !uncached && (mNumCachedFrames > 0))
	{
		CMeshPart	*pp = this, *op = this;
		while (pp->mPositionStatic)
			pp = pp->mParent;
		while (op->mOrientationStatic)
			op = op->mParent;

		SINT	p0 = frame, p1 = next_frame, o0 = frame, o1 = next_frame;
		if (frame >= pp->mNumCachedFrames)
			p0 = pp->mNumCachedFrames - 1;
		if (next_frame >= pp->mNumCachedFrames)
			p1 = pp->mNumCachedFrames - 1;
		if (frame >= op->mNumCachedFrames)
			o0 = op->mNumCachedFrames - 1;
		if (next_frame >= op->mNumCachedFrames)
			o1 = op->mNumCachedFrames - 1;

		FVector	&b = GetCachedPosition(pp->mCachedPosition, p1);
		FVector	dp = b - GetCachedPosition(pp->mCachedPosition, p0);
		*pos = GetCachedPosition(pp->mCachedPosition, p0) + dp * fraction;
		*ori = GetCachedOrientation(op->mCachedOrientation, o0) + (GetCachedOrientation(op->mCachedOrientation, o1) - GetCachedOrientation(op->mCachedOrientation, o0)) * fraction;

		if (local == false)
		{
			if (thing)
			{
				FMatrix	m = thing->GetRenderOrientation();
				*ori = m * *ori;
				*pos = m * *pos + thing->GetRenderPos();
			}
			sWorldCacheOri = *ori;
			sWorldCachePos = *pos;
			sWorldCacheFrame = frame;
			sWorldCacheRender = render;
		}
		else
		{
			sLocalCacheOri = *ori;
			sLocalCachePos = *pos;
			sLocalCacheFrame = frame;
			sLocalCacheRender = render;
		}
		sCachePart = this;
		sCacheThing = thing;
		return;
	}

	if (!mParent)
	{
		MakeIdentity(parent_ori);
		parent_pos.Set(0, 0, 0);
	}
	else
		mParent->GetWorldTransforms(frame, next_frame, fraction, thing, &parent_ori, &parent_pos, local, controller, uncached);

	GetTransforms(frame, next_frame, fraction, ori, pos);

	if (thing)
	{
		mc = thing->GetMotionController();
		if (mc && controller)
			mc->Unknown3(this, &parent_pos, &parent_ori, local);
		if ((local == false) && !mPartNumber)
		{
			parent_pos += thing->GetRenderPos();
			parent_ori = parent_ori * thing->GetRenderOrientation();
		}
	}
	parent_pos += parent_ori * *pos;
	parent_ori = parent_ori * *ori;
	if (thing)
	{
		mc = thing->GetMotionController();
		if (mc && controller)
			mc->GetPartTransform(this, &parent_pos, &parent_ori, local);
	}

	*pos = parent_pos;
	*ori = parent_ori;
	if (local == false)
	{
		sWorldCacheOri = parent_ori;
		sWorldCachePos = parent_pos;
		sWorldCacheFrame = frame;
		sWorldCacheRender = render;
	}
	else
	{
		sLocalCacheOri = parent_ori;
		sLocalCachePos = parent_pos;
		sLocalCacheFrame = frame;
		sLocalCacheRender = render;
	}
	sCacheThing = thing;
}

//******************************************************************************************
void CMeshPart::BuildCache()	// FUNCTION: 0x004b1a40
{
	mNumCachedFrames = 1;
	if (mNumHFrames > 1)
		mNumCachedFrames = mNumVFrames;
	for (CMeshPart *p = mParent; p; p = p->mParent)
		if (p->mNumHFrames > 1)
			mNumCachedFrames = mNumVFrames;

	if ((mNumHFrames == 1) && (mParent))
	{
		FMatrix	*m = mHFrameOrientations;
		if ((m->Row[0].X == 1.0f) && (m->Row[0].Y == 0) && (m->Row[0].Z == 0) &&
			(m->Row[1].X == 0) && (m->Row[1].Y == 1.0f) && (m->Row[1].Z == 0) &&
			(m->Row[2].X == 0) && (m->Row[2].Y == 0) && (m->Row[2].Z == 1.0f))
			mOrientationStatic = TRUE;

		FVector	*v = mHFramePositions;
		if ((v->X == 0) && (v->Y == 0) && (v->Z == 0))
			mPositionStatic = TRUE;
	}

	char	message[256];
	sprintf(message, "Mesh %s part %s cached %d frames\n", mBaseMesh->mName, mName, mNumCachedFrames);
	TRACE(message);

	if (!mPositionStatic)
#line 1811
		mCachedPosition = new(MEMTYPE_MESHCACHE) FVector[mNumCachedFrames];
	if (!mOrientationStatic)
#line 1815
		mCachedOrientation = new(MEMTYPE_MESHCACHE) FMatrix[mNumCachedFrames];

	for (SINT i = 0; i < mNumCachedFrames; i++)
	{
		FVector	pos;
		FMatrix	ori;
		GetWorldTransforms(i, i, 0, NULL, &ori, &pos, TRUE, FALSE, TRUE);
		if (!mOrientationStatic)
			mCachedOrientation[i] = ori;
		if (!mPositionStatic)
			mCachedPosition[i] = pos;
	}
}

//******************************************************************************************
// Links the parts named "<this part>_damaged", "_damaged2" ... in order of their numbers.
void CMeshPart::BuildChain()	// FUNCTION: 0x004b1d30
{
	CMeshPart	*levels[20];
	char		message[256];
	memset(levels, 0, sizeof(levels));
	SINT		count = 0;

	for (SINT i = 0; i < mBaseMesh->mNumParts; i++)
	{
		if (strnicmp(mName, mBaseMesh->GetPart(i)->mName, strlen(mName)) == 0)
		{
			char	*suffix = mBaseMesh->GetPart(i)->mName + strlen(mName);
			if ((strlen(suffix) >= 8) && (strnicmp(suffix, "_damaged", 8) == 0))
			{
				SINT	n = 1;
				if (strlen(suffix) > 8)
					n = atoi(suffix + 8);
				if (levels[n])
					sprintf(message, "Duplicate damage number for part %s!", mBaseMesh->GetPart(i)->mName);
				levels[n] = mBaseMesh->GetPart(i);
				count++;
			}
		}
	}

	CMeshPart	*part = this;
	for (SINT j = 0; j < 20; j++)
	{
		if (levels[j])
		{
			part->mNextMeshPartInChain = levels[j];
			part->mNumChainedParts = count;
			levels[j]->mIsChained = TRUE;
			part = levels[j];
			count--;
		}
	}
}

//******************************************************************************************
// Each vertex's two normals from the faces around it: +0x10 from every triangle using its position, the normal
// itself (when normals is set) from the triangles using the vertex. Parts of more than 10000 vertices are skipped.
// Each accumulation copies the returned vector (004b20a4..004b2109 and 004b220f..004b226d), including its fourth
// dword. Reading the vertices and positions through their owning arrays preserves the triangle-loop admission
// and gives VC6 the budget to expand every helper; caching those lookups and using += loses these boundaries.
void CMeshPart::EvaluateNormals(bool normals)	// FUNCTION: 0x004b1eb0
{
	if (mNumDVertices > 10000)
		return;

	for (SINT i = 0; i < mNumDVertices; i++)
	{
		FVector	vertex_normal = ZERO_FVECTOR;
		FVector	position_normal = ZERO_FVECTOR;
		SINT	vertex_faces = 0;
		SINT	position_faces = 0;
		for (SINT t = 0; t < mNumTriangles; t++)
		{
			CMeshTriangle	*tri = &mTriangles[t];
			if ((tri->mVertices[0]->mPVert == mDVertices[i].mPVert) || (tri->mVertices[1]->mPVert == mDVertices[i].mPVert) || (tri->mVertices[2]->mPVert == mDVertices[i].mPVert))
			{
				FVector	e1; e1 = mPVertices[0][tri->mVertices[0]->mPVert] - mPVertices[0][tri->mVertices[2]->mPVert];
				FVector	e2; e2 = mPVertices[0][tri->mVertices[0]->mPVert] - mPVertices[0][tri->mVertices[1]->mPVert];
				FVector	n = e2 ^ e1;
				n.Normalise();
				position_normal = position_normal + n;
				position_faces++;
			}
			if ((tri->mVertices[0] == &mDVertices[i]) || (tri->mVertices[1] == &mDVertices[i]) || (tri->mVertices[2] == &mDVertices[i]))
			{
				FVector	e1; e1 = mPVertices[0][tri->mVertices[0]->mPVert] - mPVertices[0][tri->mVertices[2]->mPVert];
				FVector	e2; e2 = mPVertices[0][tri->mVertices[0]->mPVert] - mPVertices[0][tri->mVertices[1]->mPVert];
				FVector	n = e2 ^ e1;
				n.Normalise();
				vertex_normal = vertex_normal + n;
				vertex_faces++;
			}
		}

		if (normals)
		{
			if (!vertex_faces)
				mDVertices[i].mNormal = FVector(1.0f, 0, 0);
			else if (vertex_normal.Magnitude() < 0.001f)
				mDVertices[i].mNormal = FVector(1.0f, 0, 0);
			else
			{
				vertex_normal.Normalise();
				mDVertices[i].mNormal = vertex_normal;
			}
		}
		if (!position_faces)
			mDVertices[i].mSmoothNormal = FVector(1.0f, 0, 0);
		else
		{
			position_normal.Normalise();
			mDVertices[i].mSmoothNormal = position_normal;
		}
	}
}

//******************************************************************************************
// The fraction is stored and f returned once, after both paths. The fraction pointer is
// then loaded once, in a shared tail that follows both paths in the IL, so the chooser's picks for the fallback path
// (the vtable, &frame, then the pointer) run eax, edx, eax as retail's; written in each path, the animated path's load
// moved the cursor one step too far. VC6 copies the small store-and-return tail back into each path before scheduling,
// so each path keeps its own `fstp [eax]; mov eax,esi`. Each path keeps its own frame (one shared frame: 15/49).
SINT CMeshPart::ResolveFrameNumber(float time, SINT anim, float *fraction, CMotionController *mc)	// FUNCTION: 0x004b24d0
{
	SINT	f;
	float	part;
	if ((mBaseMesh->mNumAnimModes) && (mNumVFrames > 1) && (anim >= 0))
	{
		CAnimMode	*a = &mBaseMesh->mAnimModes[anim];
		float		frame = time;
		frame *= a->mFrameScaler;
		frame += a->mStart;
		if (mc)
			mc->AdjustFrame(this, &frame);
		f = ((SINT)floor(frame)) % mNumVFrames;
		part = frame - floorf(frame);
	}
	else
	{
		float	frame = 0;
		if (mc)
			mc->AdjustFrame(this, &frame);
		f = (SINT)floor(frame);
		part = frame - floorf(frame);
	}
	*fraction = part;
	return f;
}

//******************************************************************************************
FVector CMeshPart::GetRandomVertexPos()	// FUNCTION: 0x004b25d0
{
	FVector	v(0, 0, 0);
	if (!mPolyBucket)
	{
		LOG.AddMessage("FATAL ERROR: can't get ranom vertex (no poly bucket defined)");
		return v;
	}

	CPolyBucketVertex	*verts[3];
	if (mPolyBucket->GetRandomTriangle(verts) == 1)
	{
		switch (GAME.Random() % 3)
		{
			case 0:
				mPolyBucket->mPacking.Unpack(verts[0], v);
				break;
			case 1:
				mPolyBucket->mPacking.Unpack(verts[1], v);
				break;
			case 2:
				mPolyBucket->mPacking.Unpack(verts[2], v);
				break;
		}
	}
	return v;
}

//******************************************************************************************
// The orientation-cache bytes read so far (00704da8).
static SINT		sOrientationCacheBytes = 0;

//******************************************************************************************
// Each chunk's part index has its own scope. This reproduces retail's dead-argument-slot reuse for the vertex
// size, allocation temporaries and final collision index (004b2acc, 004b2da1, 004b3101).
// A part from the level resources: the object itself, then its geometry, links (as part numbers), bounding box,
// animation keys, bones, polygon bucket, frame caches and vertex buffers. Allocated arrays are marked in the
// stored object by non-null pointers (the orientation arrays by "HORI" and "CORI").
#line 2413
CMeshPart *CMeshPart::Deserialize(CChunkReader *c, CMeshPart *part, CMesh *mesh)	// FUNCTION: 0x004b27a0
{
	CBoundingBox	*box = part->mBoundingBox;
	CDXMeshVB		*vb = part->mMeshVB;
	part->mBoundingBox = NULL;

	c->GetNext();
	c->GetNext();
	if (c->GetSize() != sizeof(CMeshPart))
		TRACE("Size=%d, sizeof=%d\n", c->GetSize(), sizeof(CMeshPart));
	c->Read(part, c->GetSize(), 1);
	part->mBaseMesh = mesh;
	part->InitMemory(part->mNumDVertices, part->mNumPVertices, part->mNumTriangles, part->mNumVFrames, part->mNumAFrames);

	if (part->mNumVFrames > 0)
#line 2425
		part->mVHFrameMap = new(MEMTYPE_MESH) UBYTE[part->mNumVFrames];

	if (part->mNumHFrames > 0)
	{
		if (part->mHFramePositions)
#line 2431
			part->mHFramePositions = new(MEMTYPE_MESH) FVector[part->mNumHFrames];
		if (part->mHFrameFOVs)
#line 2433
			part->mHFrameFOVs = new(MEMTYPE_MESH) float[part->mNumHFrames];
		MEM_MANAGER.TagMem(part->mHFramePositions, part->mNumHFrames * sizeof(FVector), "Meshes/%s/Part %d/Position data", part->mBaseMesh->mName, part->mPartNumber);
		MEM_MANAGER.TagMem(part->mHFrameFOVs, part->mNumHFrames * sizeof(float), "Meshes/%s/Part %d/FOV data", part->mBaseMesh->mName, part->mPartNumber);
	}

	SINT	i;
	if (part->mNumBones > 0)
	{
		if (part->mBones)
#line 2442
			part->mBones = new(MEMTYPE_MESH) CMeshPart *[part->mNumBones];
		if (part->mNumPVertices > 0)
		{
			if (part->mBoneWeights)
#line 2446
				part->mBoneWeights = new(MEMTYPE_MESH) float *[part->mNumPVertices];
			if (part->mBoneSlots)
#line 2448
				part->mBoneSlots = new(MEMTYPE_MESH) SINT *[part->mNumPVertices];
			for (i = 0; i < part->mNumPVertices; i++)
			{
#line 2451
				part->mBoneWeights[i] = new(MEMTYPE_MESH) float[part->mNumBones];
				part->mBoneSlots[i] = new(MEMTYPE_MESH) SINT[3];
			}
			MEM_MANAGER.TagMem(part->mBoneWeights, part->mNumBones * part->mNumPVertices * sizeof(float), "Meshes/%s/Part %d/Bone weights", part->mBaseMesh->mName, part->mPartNumber);
			MEM_MANAGER.TagMem(part->mBoneSlots, part->mNumPVertices * 3 * sizeof(SINT), "Meshes/%s/Part %d/Bone slots", part->mBaseMesh->mName, part->mPartNumber);
		}
	}

	if (part->mNumDVertices > 0)
	{
		c->GetNext();
		SINT	size;
		c->Read(&size, 4, 1);
		if (size != sizeof(CMeshDVertex))
			TRACE("Size=%d, sizeof=%d\n", size, sizeof(CMeshDVertex));
		for (i = 0; i < part->mNumDVertices; i++)
			c->Read(&part->mDVertices[i], size, 1);
	}

	if (part->mNumTriangles > 0)
	{
		c->GetNext();
		for (i = 0; i < part->mNumTriangles; i++)
		{
			SINT	v[3];
			c->Read(v, 4, 3);
			part->mTriangles[i].mVertices[0] = &part->mDVertices[v[0]];
			part->mTriangles[i].mVertices[1] = &part->mDVertices[v[1]];
			part->mTriangles[i].mVertices[2] = &part->mDVertices[v[2]];
		}
	}

	if (part->mNumPVertices > 0)
	{
		c->GetNext();
		for (i = 0; i < part->mNumAFrames; i++)
			c->Read(part->mPVertices[i], 16, part->mNumPVertices);
	}

	if (part->mNumChildren > 0)
	{
		c->GetNext();
#line 2526
		part->mChildren = new(MEMTYPE_MESH) CMeshPart *[part->mNumChildren];
		for (i = 0; i < part->mNumChildren; i++)
		{
			SINT	index;
			c->Read(&index, 4, 1);
			part->mChildren[i] = mesh->GetPart(index);
		}
	}
	if (part->mParent)
	{
		c->GetNext();
		SINT	index;
		c->Read(&index, 4, 1);
		part->mParent = mesh->GetPart(index);
	}
	if (part->mNextMeshPartInChain)
	{
		c->GetNext();
		SINT	index;
		c->Read(&index, 4, 1);
		part->mNextMeshPartInChain = mesh->GetPart(index);
	}

	c->GetNext();
	part->mBoundingBox = ReadBoundingBox(c, box);

	if (part->mNumVFrames > 0)
	{
		c->GetNext();
		c->Read(part->mVHFrameMap, 1, part->mNumVFrames);
	}

	if (part->mNumHFrames > 0)
	{
		if (part->mHFrameOrientations)
		{
			if (c->GetNext() == MKID("HORI"))
			{
#line 2602
				part->mHFrameOrientations = new(MEMTYPE_MESH) FMatrix[part->mNumHFrames];
				c->Read(part->mHFrameOrientations, sizeof(FMatrix), part->mNumHFrames);
			}
		}
		if (part->mHFramePositions)
		{
			c->GetNext();
			c->Read(part->mHFramePositions, 16, part->mNumHFrames);
		}
		if (part->mHFrameFOVs)
		{
			c->GetNext();
			c->Read(part->mHFrameFOVs, 4, part->mNumHFrames);
		}
	}

	if (part->mNumBones > 0)
	{
		c->GetNext();
		for (i = 0; i < part->mNumBones; i++)
		{
			SINT	index;
			c->Read(&index, 4, 1);
			((CMeshPart **)part->mBones)[i] = mesh->GetPart(index);
		}
		if (part->mBoneWeights)
		{
			c->GetNext();
			for (i = 0; i < part->mNumPVertices; i++)
				c->Read(part->mBoneWeights[i], 4, part->mNumBones);
		}
		if (part->mBoneSlots)
		{
			c->GetNext();
			for (i = 0; i < part->mNumPVertices; i++)
				c->Read(part->mBoneSlots[i], 4, 3);
		}
	}

	if (part->mPolyBucket)
		part->mPolyBucket = CPolyBucket::Load(c);

	if (part->mCachedPosition)
	{
#line 2704
		part->mCachedPosition = new(MEMTYPE_MESHCACHE) FVector[part->mNumCachedFrames];
		c->GetNext();
		c->Read(part->mCachedPosition, 16, part->mNumCachedFrames);
		MEM_MANAGER.TagMem(part->mCachedPosition, part->mNumCachedFrames * sizeof(FVector), "Meshes/%s/Part %d/Position cache", part->mBaseMesh->mName, part->mPartNumber);
	}

	if (part->mCachedOrientation)
	{
		if (c->GetNext() == MKID("CORI"))
		{
			sOrientationCacheBytes += part->mNumCachedFrames * sizeof(FMatrix);
#line 2742
			part->mCachedOrientation = new(MEMTYPE_MESHCACHE) FMatrix[part->mNumCachedFrames];
			c->Read(part->mCachedOrientation, sizeof(FMatrix), part->mNumCachedFrames);
			MEM_MANAGER.TagMem(part->mCachedOrientation, part->mNumCachedFrames * sizeof(FMatrix), "Meshes/%s/Part %d/Orientation cache", part->mBaseMesh->mName, part->mPartNumber);
		}
	}
	else
		part->mUnknown110 = NULL;

	if (part->mReferenceTo)
	{
		c->GetNext();
		SINT	index;
		c->Read(&index, 4, 1);
		part->mReferenceTo = mesh->GetPart(index);
	}

	sLocalCacheRender = sWorldCacheRender = 99999;
	sCacheThing = NULL;
	part->mMeshVB = vb;
	vb->Load(c, part->mPartType == 3);
	return part;
}

// Retail calls ReadBoundingBox out of line from Deserialize (004b2cf1), which no source order reproduced;
// whether the original used this pragma is open.
#pragma auto_inline(off)
#line 2853
CBoundingBox *CMeshPart::ReadBoundingBox(CChunkReader *c, CBoundingBox *box)	// FUNCTION: 0x004b3180
{
	c->GetNext();
	if (!box)
		box = new(MEMTYPE_MESH) CBoundingBox;
	c->Read(&box->mOrigin, 16, 1);
	c->Read(&box->mAxes, 16, 1);
	c->Read(&box->mValid, 4, 1);
	c->Read(&box->mRadius, 4, 1);
	return box;
}
#pragma auto_inline(on)

//******************************************************************************************
// Every part's polygons saved so far, of the total left (00704dac, 00704db0).
static SINT		sPolysSaved = 0;
static SINT		sPolysTotal = 0;

// The neighbouring-face normal needs its own square association: retail 004b3727..004b3745
// adds Y*Y and Z*Z before X*X, then stores the magnitude as a float. Constructing this
// local vector directly from the edges preserves the cross product's extended intermediates.
// This is a reconstruction helper; its original type and spelling are unknown.
struct MeshSideNormal : public FVector
{
	MeshSideNormal(const FVector &a, const FVector &b)
		: FVector(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - b.Z * a.X, a.X * b.Y - a.Y * b.X) {}

	void Normalise()
	{
		float m = sqrtf((X * X) + ((Y * Y) + (Z * Z)));
		if (m)
		{
			m = 1.0f / m;
			X *= m;
			Y *= m;
			Z *= m;
		}
	}
};

//******************************************************************************************
// Collapses positions onto a neighbour while the part keeps a quarter of them: the cost of moving a position onto a
// neighbour is the edge length (at least 1) times the curvature of the faces round the position, as seen from the
// faces that share the edge; the cheapest goes if it costs less than 0.2 (0.3 for parts of more than 300
// positions) and every neighbour is in at least two of the position's faces. Then drops the triangles that folded.
// Earlier direct-field form (superseded below): the frame matched. Retail uses ONE local counter for the first triangle loop and
// the two neighbour loops of a position (the packer replay on the real conflict graph reproduces all 19 of retail's
// size-4 slot classes only when the triangle loop's variable and the best-neighbour loop's are the same symbol), and the
// final triangle loop has its own. The normal statement reads mTriangles[faces[f]] directly, one named local fewer,
// which puts the final loop's `mTriangles[t]` sum in retail's order. Left: the two address sums `mVertices[v]` of the
// removal loop, where retail puts the loaded pointer before the induction temporary ([eax+edx+0x20], ours [edx+eax+0x20]);
// the temporaries' ids land on the wrong side of the 16-bit rank key (a temporary beats the INDIR key 0x8007 only when
// id & 0x3ff < 0x200). They need 213 to about 400 more temporaries created after the vector math (ids reset per function,
// so it is this function's own statements): the side normal without its side local flips the final loop's sum alone too
// but moves the vector math's own sums when both are written out; dead FVector locals before v flip only the final
// loop's. No natural construct was found in that earlier experiment.
// STAND-IN: final triangle reads use a CPP-local borrowed MeshTriangleView/GetVertex operation.
// Its original source name and ownership are unknown. In the otherwise identical guarded-offset form,
// direct array reads miss three SIB operand encodings (2432-byte section, 33 relocations); the inlined
// accessor resolves those three bytes without a new call or emitted operation. A flat pointer view does
// not resolve them. Replace this source-context concession if evidence-backed direct source preserves
// the complete section and every relocation.
// The vertex remap loop uses its native 96-byte offset, initialized after the positive-count guard,
// with the original dynamic count reads and conditional neighbour writes. The view borrows the actual triangle; no casts or shared-header/layout changes.
// Review 2026-10-14: direct reads `mTriangles[t].mVertices[k]->mPositionIndex` score 0/6/17 bytes (three base/index swaps in the SIB operands: a
// temporary against the INDIR key), a triangle pointer or reference 7/9/295, a local for the third index 4/52/53.
class MeshTriangleView
{
 const CMeshTriangle &mTriangle;
public:
 MeshTriangleView(const CMeshTriangle &triangle) : mTriangle(triangle) {}
 CMeshDVertex *GetVertex(SINT index) const { return mTriangle.mVertices[index]; }
};
void CMeshPart::ReducePolys()	// FUNCTION: 0x004b31f0
{
	SINT	num_positions = mNumPVertices;
	if (num_positions < 32)
		return;

	SINT	removed = 0;
	SINT	degenerate = 0;
	float	threshold = 0.2f;
	if (num_positions > 300)
		threshold = 0.3f;

#line 2893
	SINT	*neighbours = new(MEMTYPE_MESH) SINT[mNumPVertices];
#line 2895
	SINT	*faces = new(MEMTYPE_MESH) SINT[mNumTriangles];

	for (SINT p = 0; p < mNumPVertices; p++)
	{
		SINT	num_neighbours = 0;
		SINT	num_faces = 0;
		SINT	i;				// the one counter of the triangle loop and the two neighbour loops below
		for (i = 0; i < mNumTriangles; i++)
		{
			if ((mTriangles[i].mVertices[0]->mPVert == p) || (mTriangles[i].mVertices[1]->mPVert == p) || (mTriangles[i].mVertices[2]->mPVert == p))
			{
				faces[num_faces++] = i;
				BOOL	have0 = (mTriangles[i].mVertices[0]->mPVert == p);
				BOOL	have1 = (mTriangles[i].mVertices[1]->mPVert == p);
				BOOL	have2 = (mTriangles[i].mVertices[2]->mPVert == p);
				for (SINT n = 0; n < num_neighbours; n++)
				{
					if (neighbours[n] == mTriangles[i].mVertices[0]->mPVert)
						have0 = TRUE;
					if (neighbours[n] == mTriangles[i].mVertices[1]->mPVert)
						have1 = TRUE;
					if (neighbours[n] == mTriangles[i].mVertices[2]->mPVert)
						have2 = TRUE;
				}
				if (!have0)
					neighbours[num_neighbours++] = mTriangles[i].mVertices[0]->mPVert;
				if (!have1)
					neighbours[num_neighbours++] = mTriangles[i].mVertices[1]->mPVert;
				if (!have2)
					neighbours[num_neighbours++] = mTriangles[i].mVertices[2]->mPVert;
			}
		}

		BOOL	closed = TRUE;
		for (i = 0; i < num_neighbours; i++)
		{
			SINT	count = 0;
			for (SINT f = 0; f < num_faces; f++)
			{
				CMeshTriangle	*tri = &mTriangles[faces[f]];
				if ((tri->mVertices[0]->mPVert == neighbours[i]) || (tri->mVertices[1]->mPVert == neighbours[i]) || (tri->mVertices[2]->mPVert == neighbours[i]))
					count++;
			}
			if (count < 2)
				closed = FALSE;
		}

		if ((mNumPVertices >= num_positions / 4) && closed)
		{
			SINT	best = -1;
			float	best_cost = 1.0f;
			for (i = 0; i < num_neighbours; i++)
			{
				SINT	other = neighbours[i];
				float	curvature = 0;
				for (SINT f = 0; f < num_faces; f++)
				{
					FVector	normal = (mPVertices[0][mTriangles[faces[f]].mVertices[2]->mPVert] - mPVertices[0][mTriangles[faces[f]].mVertices[0]->mPVert]) ^ (mPVertices[0][mTriangles[faces[f]].mVertices[1]->mPVert] - mPVertices[0][mTriangles[faces[f]].mVertices[0]->mPVert]);
					normal.Normalise();
					float	least = 1.0f;
					SINT	sides = 0;
					for (SINT g = 0; g < num_faces; g++)
					{
						CMeshTriangle	*side = &mTriangles[faces[g]];
						if ((side->mVertices[0]->mPVert == other) || (side->mVertices[1]->mPVert == other) || (side->mVertices[2]->mPVert == other))
						{
							MeshSideNormal	side_normal(mPVertices[0][side->mVertices[2]->mPVert] - mPVertices[0][side->mVertices[0]->mPVert], mPVertices[0][side->mVertices[1]->mPVert] - mPVertices[0][side->mVertices[0]->mPVert]);
							side_normal.Normalise();
							float	c = (1.0f - (normal * side_normal)) * 0.5f;
							if (c < least)
								least = c;
							sides++;
						}
					}
					if (sides == 1)
						least = 1.0f;
					if (least > curvature)
						curvature = least;
				}
				float	length = (mPVertices[0][p] - mPVertices[0][other]).Magnitude();
				if (length < 1.0f)
					length = 1.0f;
				float	cost = length * curvature;
				if (cost < best_cost)
				{
					best_cost = cost;
					best = i;
				}
			}

			if ((best_cost < threshold) && (best != -1))
			{
				removed++;
				SINT	v;
				// STAND-IN (ids; see the note above MeshTriangleView): the removal loop walks a byte offset. The natural `for (v...) mDVertices[v]` scores
				// 0/8/18 bytes (the two address sums swap base and index), a pointer local 49/261/1498.
				v = 0;
				if (mNumDVertices > 0)
				{
					SINT byteOffset = 0;
					do
					{
					if (((CMeshDVertex *)((UBYTE *)mDVertices + byteOffset))->mPVert == p)
						((CMeshDVertex *)((UBYTE *)mDVertices + byteOffset))->mPVert = neighbours[best];
					if (((CMeshDVertex *)((UBYTE *)mDVertices + byteOffset))->mPVert > p)
						((CMeshDVertex *)((UBYTE *)mDVertices + byteOffset))->mPVert--;

						v++;
						byteOffset += sizeof(CMeshDVertex);
					} while (v < mNumDVertices);
				}
				for (v = p + 1; v < mNumPVertices; v++)
					mPVertices[0][v - 1] = mPVertices[0][v];
				mNumPVertices--;
				p--;
			}
		}
	}
	if (neighbours)
		delete [] neighbours;
	if (faces)
		delete [] faces;

	for (SINT t = 0; t < mNumTriangles; t++)
	{
		SINT	a = MeshTriangleView(mTriangles[t]).GetVertex(0)->mPVert;
		SINT	b = MeshTriangleView(mTriangles[t]).GetVertex(1)->mPVert;
		if ((a == b) || (a == MeshTriangleView(mTriangles[t]).GetVertex(2)->mPVert) || (b == MeshTriangleView(mTriangles[t]).GetVertex(2)->mPVert))
		{
			degenerate++;
			for (SINT u = t + 1; u < mNumTriangles; u++)
			{
				mTriangles[u - 1].mVertices[0] = mTriangles[u].mVertices[0];
				mTriangles[u - 1].mVertices[1] = mTriangles[u].mVertices[1];
				mTriangles[u - 1].mVertices[2] = mTriangles[u].mVertices[2];
			}
			mNumTriangles--;
			t--;
		}
	}

	char	message[256];
	sprintf(message, "Part %s removed %d of %d verts\n", mName, removed, mNumDVertices);
	TRACE(message);
	sprintf(message, "Removed %d of %d polys\n", degenerate, mNumTriangles);
	TRACE(message);
	sPolysSaved += degenerate;
	sPolysTotal += mNumTriangles;
	sprintf(message, "Total polys saved : %d of %d\n", sPolysSaved, sPolysTotal);
	TRACE(message);
}

//******************************************************************************************
// A copy of the part with its own geometry, keys and skinning tables; the children, bone parts and vertex buffer
// are shared or rebuilt by the caller.
CMeshPart *CMeshPart::Clone()	// FUNCTION: 0x004b3b70
{
	SINT	i;
#line 3112
	CMeshPart	*part = new(MEMTYPE_MESH) CMeshPart;

	part->mPartNumber = mPartNumber;
	part->mPartType = mPartType;
	part->mRadius = mRadius;
	part->mCueFactor = mCueFactor;
	part->mBaseMesh = mBaseMesh;
	part->mBaseOrientation = mBaseOrientation;
	part->mBasePosition = mBasePosition;
	part->mReferenceTo = mReferenceTo;
	part->mBoundingBox->mAxes = mBoundingBox->mAxes;
	part->mBoundingBox->mOrigin = mBoundingBox->mOrigin;
	part->mBoundingBox->mRadius = mBoundingBox->mRadius;
	part->mBoundingBox->mValid = mBoundingBox->mValid;
	part->mCueFactor = mCueFactor;
	part->mIsChained = mIsChained;
	part->mNextMeshPartInChain = mNextMeshPartInChain;
	strcpy(part->mName, mName);

	part->mNumChildren = mNumChildren;
#line 3135
	part->mChildren = new(MEMTYPE_MESH) CMeshPart *[mNumChildren];
	for (i = 0; i < mNumChildren; i++)
		part->mChildren[i] = mChildren[i];

	part->InitMemory(mNumDVertices, mNumPVertices, mNumTriangles, mNumVFrames, mNumAFrames);
	memcpy(part->mDVertices, mDVertices, mNumDVertices * sizeof(CMeshDVertex));
	for (i = 0; i < mNumAFrames; i++)
		memcpy(part->mPVertices[i], mPVertices[i], mNumPVertices * sizeof(FVector));
	for (i = 0; i < mNumTriangles; i++)
	{
		part->mTriangles[i].mVertices[0] = &part->mDVertices[mTriangles[i].mVertices[0] - mDVertices];
		part->mTriangles[i].mVertices[1] = &part->mDVertices[mTriangles[i].mVertices[1] - mDVertices];
		part->mTriangles[i].mVertices[2] = &part->mDVertices[mTriangles[i].mVertices[2] - mDVertices];
	}

	part->mNumHFrames = mNumHFrames;
	if (mNumHFrames > 0)
	{
		if (mHFrameOrientations)
		{
#line 3160
			part->mHFrameOrientations = new(MEMTYPE_MESH) FMatrix[part->mNumHFrames];
			memcpy(part->mHFrameOrientations, mHFrameOrientations, mNumHFrames * sizeof(FMatrix));
		}
		if (mHFramePositions)
		{
#line 3167
			part->mHFramePositions = new(MEMTYPE_MESH) FVector[part->mNumHFrames];
			memcpy(part->mHFramePositions, mHFramePositions, mNumHFrames * sizeof(FVector));
		}
		if (mHFrameFOVs)
		{
#line 3173
			part->mHFrameFOVs = new(MEMTYPE_MESH) float[part->mNumHFrames];
			memcpy(part->mHFrameFOVs, mHFrameFOVs, mNumHFrames * sizeof(float));
		}
	}
	if (mNumVFrames > 0)
	{
#line 3180
		part->mVHFrameMap = new(MEMTYPE_MESH) UBYTE[mNumVFrames];
		memcpy(part->mVHFrameMap, mVHFrameMap, mNumVFrames);
	}

	part->mNumBones = mNumBones;
	if (mBones)
	{
#line 3188
		part->mBones = new(MEMTYPE_MESH) CMeshPart *[part->mNumBones];
		for (i = 0; i < mNumBones; i++)
			part->mBones[i] = mBones[i];
		if (mNumPVertices > 0)
		{
			if (mBoneWeights)
			{
#line 3199
				part->mBoneWeights = new(MEMTYPE_MESH) float *[mNumPVertices];
				memcpy(part->mBoneWeights, mBoneWeights, mNumPVertices * sizeof(float *));
			}
			if (mBoneSlots)
			{
#line 3205
				part->mBoneSlots = new(MEMTYPE_MESH) SINT *[mNumPVertices];
				memcpy(part->mBoneSlots, mBoneSlots, mNumPVertices * sizeof(SINT *));
			}
			for (i = 0; i < mNumPVertices; i++)
			{
#line 3211
				part->mBoneWeights[i] = new(MEMTYPE_MESH) float[part->mNumBones];
				part->mBoneSlots[i] = new(MEMTYPE_MESH) SINT[3];
				memcpy(part->mBoneWeights[i], mBoneWeights[i], mNumBones * sizeof(float));
				memcpy(part->mBoneSlots[i], mBoneSlots[i], 3 * sizeof(SINT));
			}
		}
	}
	else
	{
		part->mBones = NULL;
		part->mBoneWeights = NULL;
		part->mBoneSlots = NULL;
	}
	return part;
}

// STAND-IN for RowProducts at three of Merge's four products. RowProducts itself stays as it is
// (Read201 and 202 match with it); these Merge-only copies differ only in how row 0's sum is grouped and
// bracketed. Retail adds row 0 as Z, Y, X in the position loop (004b450f) and as Y, X, Z in the normal loop
// (004b4760); rows 1 and 2 keep RowProducts' order. VC6 ranks a flat sum by its terms' keys (other_ori's field
// ids and the position/normal address temporaries), which give X, Z, Y and Y, Z, X there; a bracketed pair is one
// operand of height 2 that is added first, and its two terms keep their key order (Z before Y, Y before X). Each
// grouped copy leaves one product unbracketed so that the position loop's first scheduling region (81-node cap)
// keeps the Inverse call at its end (004b45f0..004b462c). The position loop's inverse product carries one bracket
// more (row 0 as a group), after that call; without it the loop-2 header's registers (004b4739) and the triangle
// loop's address operands (004b4983..004b4a0b) come out swapped. Original spelling unknown: replace these with a
// natural form that gives the same orders.
inline FVector RowProductsZYX(const FMatrix &m, const FVector &v)
{
	return FVector(((m.Row[0].Y * v.Y) + (m.Row[0].Z * v.Z)) + m.Row[0].X * v.X,
				   (m.Row[1].X * v.X) + (m.Row[1].Y * v.Y) + (m.Row[1].Z * v.Z),
				   (m.Row[2].X * v.X) + (m.Row[2].Y * v.Y) + (m.Row[2].Z * v.Z));
}

inline FVector RowProductsGrouped(const FMatrix &m, const FVector &v)
{
	return FVector(((m.Row[0].X * v.X) + (m.Row[0].Y * v.Y) + (m.Row[0].Z * v.Z)),
				   (m.Row[1].X * v.X) + (m.Row[1].Y * v.Y) + (m.Row[1].Z * v.Z),
				   (m.Row[2].X * v.X) + (m.Row[2].Y * v.Y) + (m.Row[2].Z * v.Z));
}

inline FVector RowProductsYXZ(const FMatrix &m, const FVector &v)
{
	return FVector(((m.Row[0].Y * v.Y) + (m.Row[0].X * v.X)) + m.Row[0].Z * v.Z,
				   (m.Row[1].X * v.X) + (m.Row[1].Y * v.Y) + (m.Row[1].Z * v.Z),
				   (m.Row[2].X * v.X) + (m.Row[2].Y * v.Y) + (m.Row[2].Z * v.Z));
}

//******************************************************************************************
// Takes a copy of other's geometry into this part, moved from other's rest transform into this one's. The old arrays
// are not freed.
// Three of the four products go through the Merge-only copies above (labelled stand-in), the normal loop's inverse product is the
// operator (shape 40 -> 28 against the operator for the three). Matched: reference locals for the copied position and
// normal, a named loop-1 result and the triangle loop's own counter are natural source; the copies supply row 0's
// order in both loops.
void CMeshPart::MergeVertices(CMeshPart *other)	// FUNCTION: 0x004b4250
{
	char	message[256];
	sprintf(message, "Merging %s into %s\n", other->mName, mName);
	TRACE(message);

	SINT	num_positions = mNumPVertices + other->mNumPVertices;
	SINT	num_vertices = mNumDVertices + other->mNumDVertices;
	SINT	num_triangles = mNumTriangles + other->mNumTriangles;
#line 3243
	FVector	*positions = new(MEMTYPE_MESH) FVector[num_positions];
#line 3246
	CMeshDVertex	*vertices = new(MEMTYPE_MESH) CMeshDVertex[num_vertices];
#line 3249
	CMeshTriangle	*triangles = new(MEMTYPE_MESH) CMeshTriangle[num_triangles];

	SINT	i;
	for (i = 0; i < mNumPVertices; i++)
		positions[i] = mPVertices[0][i];
	for (i = 0; i < mNumDVertices; i++)
		vertices[i] = mDVertices[i];
	for (i = 0; i < mNumTriangles; i++)
	{
		triangles[i].mVertices[0] = &vertices[mTriangles[i].mVertices[0] - mDVertices];
		triangles[i].mVertices[1] = &vertices[mTriangles[i].mVertices[1] - mDVertices];
		triangles[i].mVertices[2] = &vertices[mTriangles[i].mVertices[2] - mDVertices];
	}

	FMatrix	other_ori, ori;
	FVector	other_pos, pos;
	other->GetWorldTransforms(0, 0, 0, NULL, &other_ori, &other_pos, true, 0, 1);
	GetWorldTransforms(0, 0, 0, NULL, &ori, &pos, true, 0, 1);

	for (i = 0; i < other->mNumPVertices; i++)
	{
		FVector	&op = other->mPVertices[0][i];
		FVector	p = (other_pos + RowProductsZYX(other_ori, op)) - pos;	// STAND-IN copies, see above
		FVector	r = RowProductsGrouped(ori.Inverse(), p);
		positions[mNumPVertices + i] = r;
	}
	for (i = 0; i < other->mNumDVertices; i++)
	{
		vertices[mNumDVertices + i] = other->mDVertices[i];
		FVector	&vn = vertices[mNumDVertices + i].mNormal;
		FVector	n = RowProductsYXZ(other_ori, vn);	// STAND-IN copy, see above
		vertices[mNumDVertices + i].mNormal = ori.Inverse() * n;
		vertices[mNumDVertices + i].mPVert += mNumPVertices;
	}
	for (SINT t = 0; t < other->mNumTriangles; t++)
	{
		triangles[mNumTriangles + t].mVertices[0] = &vertices[mNumDVertices + (other->mTriangles[t].mVertices[0] - other->mDVertices)];
		triangles[mNumTriangles + t].mVertices[1] = &vertices[mNumDVertices + (other->mTriangles[t].mVertices[1] - other->mDVertices)];
		triangles[mNumTriangles + t].mVertices[2] = &vertices[mNumDVertices + (other->mTriangles[t].mVertices[2] - other->mDVertices)];
	}

	mNumPVertices = num_positions;
	mNumDVertices = num_vertices;
	mNumTriangles = num_triangles;
	mDVertices = vertices;
	mPVertices[0] = positions;
	mTriangles = triangles;
}
