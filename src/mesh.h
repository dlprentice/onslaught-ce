// mesh.h: meshes and their bounding boxes (only what matched files need).
//
// CBoundingBox: the origin +0x00 and axes +0x10 are read by CThing::GetCentrePos (004f3ac0) and DrawDebugCuboid
// (004f37c0), the radius +0x24 by GetBoundingRadius (004f3940). What +0x20 holds is open: the first matched
// function that reads it settles it. CMesh::GetAnimModeByName is 004aa630 (CComplexThing::GetAnimModeByName,
// 004f4530). The part and box members only serve CThing::GetTopPos, which the retail linker discarded
// (nothing calls it), so their layout is unproven.

#ifndef MESH_H
#define MESH_H

#include "renderablething.h"

class CBoundingBox
{
public:
	// The radius of the sphere round the box (CMeshPart::GenerateSimpleBoundingBox 004aed9a..004aedd2).
	void		CalculateRadius()
	{
		float	r2 = mAxes.MagnitudeSq();
		mRadius = r2;
		if (r2 > 0)
			mRadius = (float)sqrt(r2);
	}

	FVector		mOrigin;				// +0x00
	FVector		mAxes;					// +0x10
	BOOL		mValid;					// +0x20: the part has positions (CMeshPart::GenerateSimpleBoundingBox 004aeba2)
	float		mRadius;				// +0x24
};

class CMesh;
class CPolyBucket;
class CPolyBucketPacking;
class CDXMeshVB;
class CDXTexture;
class CVBufTexture;

// A polygon bucket's packed vertex (6 bytes; CPolyBucket +0x98 points at a record whose +0x00 is the array).
class CPolyBucketVertex
{
public:
	CPolyBucketVertex() {}
	CPolyBucketVertex(FVector *in, CPolyBucketPacking *packing);	// 004d4aa0: returns the constructed receiver
	void		Unpack(FVector *out, CPolyBucketPacking *packing);	// 004d4b30
	SWORD		X, Y, Z;
};

// A mesh texture (0x24 bytes, MEMTYPE_MESHTEXTURE): the texture and its animation frames, five float arrays cut
// from one allocation (SetNumFrames 004adff0, MeshPart.cpp line 107). CMesh::InitAll makes the default one
// (004a525e) from "meshtex\default.tga".
class CMeshTexture
{
public:
	CMeshTexture();									// 004adf80
	~CMeshTexture();								// 004adf90
	void		SetNumFrames(SINT n);				// 004adff0
	void		InitDefault();						// 004ae080
	void		CreateVBuffer();					// 004ae0d0

	CDXTexture		*mTexture;				// +0x00
	CVBufTexture	*mVBuffer;				// +0x04
	SINT			mFrames;				// +0x08
	float			*mTextureBlend;			// +0x0c
	float			*mTextureU;				// +0x10
	float			*mTextureV;				// +0x14
	float			*mTextureUScale;			// +0x18
	float			*mTextureVScale;			// +0x1c
	float			mMaxBlend;					// +0x20
};

// A mesh vertex (0x60 bytes; InitMemory's "DVertices", 004ae8af). Six texture indices sit at +0x30
// (CMesh::OptimiseTextures remaps them, 004a5871..004a58a1).
class CMeshDVertex
{
public:
	CMeshDVertex() {}
	FVector		mNormal;				// +0x00 (CDXMeshVB::BuildStaticVB 0054c33f..0054c351)
	FVector		mSmoothNormal;			// +0x10: averaged over every triangle at the position (EvaluateNormals 004b2499)
	SINT		mPVert;			// +0x20: CPolyBucket::Build 004d4360..004d43e2
	float		mU;						// +0x24
	float		mV;						// +0x28
	DWORD		mDiffuse;				// +0x2c
	SINT		mTexture[6];			// +0x30: indices into the mesh's textures
	SINT		mIndex[6];			// +0x48: each layer's index in its texture's buffer, this frame (the mesh renderer 0054a181)
};

// A mesh triangle (12 bytes; "Triangles", 004ae97a).
class CMeshTriangle
{
public:
	CMeshDVertex	*mVertices[3];		// Build 004d4353..004d4374 dereferences each pointer at +0x20
};

// A mesh part (0x13c bytes, MEMTYPE_MESH). The constructor 004ae4b0 and destructor 004ae640 lay it out; the
// part is numbered +0x88 in the memory tags ("Meshes/%s/Part %d/...").
class CMeshPart
{
public:
	CMeshPart();									// 004ae4b0
	~CMeshPart();								// 004a51f0; MeshPart.cpp can inline the WipeVertexData tail call
	void		WipeVertexData();					// 004ae640
	SINT		GetType();

	BOOL		GetFirstPoly(FVector *pos, float radius, CPolyBucketVertex **out);	// 004ae110
	BOOL		GetNextPoly(CPolyBucketVertex **out);			// 004ae1a0
	BOOL		GetFirstPolyLine(FVector *start, FVector *end, CPolyBucketVertex **out);	// 004ae220
	void		BuildPolyBucket();									// 004ae2b0
	BOOL		GetNextPolyLine(CPolyBucketVertex **out, SINT a);	// 004ae430
	BOOL		InitMemory(SINT vertices, SINT positions, SINT triangles, SINT frames, SINT position_sets);	// 004ae860
	void		GenerateSimpleBoundingBox();								// 004aea50
	void		Read201(class CDXMemBuffer *file, CMesh *mesh, class CMeshTexture *textures, SINT num_textures, SINT unused);	// 004aede0
	void		Read202(class CDXMemBuffer *file, CMesh *mesh, class CMeshTexture *textures, SINT num_textures, SINT unused);	// 004af110
	void		Read203(class CDXMemBuffer *file, CMesh *mesh, class CMeshTexture *textures, SINT num_textures, SINT unused);	// 004af470
	void		Read204(class CDXMemBuffer *file, CMesh *mesh, class CMeshTexture *textures, SINT num_textures, SINT unused, SINT num_layers, UINT version);	// 004afbb0 (2.04..2.07)
	void		EvaluateNormals(bool normals);					// 004b1eb0
	void		HierarchyAdjust(FVector pos, FMatrix ori, CMeshPart *parent);	// 004b0800
	FVector		GetBasisY();										// 004b0c00
	FVector		GetBasisZ();										// 004b0c20
	SINT		FindVertex(FVector pos);					// 004b0c40
	CMeshPart	*GetMeshDataPart();								// 004b0cd0: itself, or +0x124 for type 6
	static CBoundingBox	*ReadBoundingBox(class CChunkReader *c, CBoundingBox *box);	// 004b3180
	// The part's position and orientation for the thing's current frame: from the render thing's cached pose when
	// it has one (in the world unless local), else from GetWorldTransforms. In MeshPose.obj.
	static void	EvaluatePose(class CRenderThing *rt, IRenderableThing *thing, CMeshPart *part, FVector *pos, FMatrix *ori, BOOL local, BOOL force);	// 004b4de0
	// A part's transform for the thing's current frame, relative to its parent (in MeshPose.obj's neighbour 004b5330).
	static void	EvaluateAnimatedTransform(IRenderableThing *thing, FVector *pos, FMatrix *ori, CMeshPart *part, CMeshPart **parent, SINT *frame);	// 004b5330
	static CMeshPart	*Deserialize(class CChunkReader *c, CMeshPart *part, CMesh *mesh);	// 004b27a0 (CMesh::Deserialize 004ab11e)
	void		BuildChain();									// 004b1d30
	FVector		GetRandomVertexPos();							// 004b25d0
	void		GetTransforms(SINT f0, SINT f1, float t, FMatrix *ori, FVector *pos);	// 004b0d00
	void		BuildCache();										// 004b1a40
	SINT		ResolveFrameNumber(float time, SINT anim, float *fraction, class CMotionController *mc);	// 004b24d0
	CMeshPart	*Clone();											// 004b3b70
	void		MergeVertices(CMeshPart *other);							// 004b4250
	void		ReducePolys();									// 004b31f0
	void		GetWorldTransforms(SINT frame, SINT next_frame, float fraction, IRenderableThing *thing, FMatrix *ori, FVector *pos, bool local, bool controller, bool uncached);	// 004b0fb0 (local: a bool, CMeshPart::EvaluatePose 004b510e)

	FMatrix			mOrientation;			// +0x00
	FMatrix			mBaseOrientation;				// +0x30 (copied by Clone 004b3c08)
	FVector			mOffsetPos;				// +0x60
	FVector			mBasePosition;				// +0x70 (copied by Clone 004b3c1d)
	CMeshTriangle	*mTriangles;			// +0x80
	FVector			**mPVertices;			// +0x84: mNumPositionSets arrays of mNumPositions
	SINT			mPartNumber;			// +0x88
	SINT			mPartType;					// +0x8c: polygon buckets for 1 and 3; 6 selects +0x124
	SINT			mNumChildren;			// +0x90
	CMeshPart		**mChildren;			// +0x94
	CMeshPart		*mParent;				// +0x98
	CMeshPart		*mNextMeshPartInChain;		// +0x9c: "<name>_damaged", "<name>_damaged2" ... (BuildChain)
	SINT			mNumChainedParts;		// +0xa0: the levels after this one
	BOOL			mIsChained;			// +0xa4
	SINT			mNumDVertices;			// +0xa8
	SINT			mNumPVertices;			// +0xac
	SINT			mNumTriangles;			// +0xb0
	SINT			mNumAFrames;		// +0xb4
	SINT			mNumVFrames;				// +0xb8
	SINT			mNumHFrames;			// +0xbc
	SINT			mNumBones;				// +0xc0: parts a skinned part's positions follow (LoadVerticesWithBones 004afff7)
	UBYTE			*mVHFrameMap;			// +0xc4: each frame's key
	FVector			*mHFramePositions;			// +0xc8
	FVector			&GetKeyPosition(SINT k) { return mHFramePositions[k]; }
	float			*mHFrameFOVs;					// +0xcc
	CMeshPart		**mBones;				// +0xd0: mNumBones parts, read as indices into the mesh's parts (004b008c)
	float			**mBoneWeights;			// +0xd4: per position, mNumBones weights scaled to sum to 1 (004b0143..)
	SINT			**mBoneSlots;			// +0xd8: per position, the three heaviest bones, -1 for none (004b035b; "Bone slots")
	char			mName[32];				// +0xdc (BuildChain 004b1d6b)
	CBoundingBox	*mBoundingBox;			// +0xfc (CRTMesh's slot 5 reads it, 004db8b0)
	CPolyBucket		*mPolyBucket;			// +0x100
	FVector			*mCachedPosition;		// +0x104: one per cached frame (BuildCache 004b1a40)
	FMatrix			*mCachedOrientation;	// +0x108
	FMatrix			*mHFrameOrientations;		// +0x10c
	FMatrix			&GetHFrameOrientation(SINT k) { return mHFrameOrientations[k]; }
	void			*mUnknown110;			// +0x110: cleared when the part has no orientation cache (Deserialize 004b30e3)
	UBYTE			mUnknown114[0x118 - 0x114];
	SINT			mNumCachedFrames;		// +0x118
	BOOL			mPositionStatic;		// +0x11c: a single key at the origin, so nothing is cached
	BOOL			mOrientationStatic;	// +0x120
	CMeshPart		*mReferenceTo;		// +0x124
	CMesh			*mBaseMesh;					// +0x128
	float			mRadius;				// +0x12c: the farthest position from the part's origin
	float			mCueFactor;			// +0x130: the middle of the box's three half-sizes
	CMeshDVertex		*mDVertices;				// +0x134
	CDXMeshVB		*mMeshVB;					// +0x138
};

// An animation (36 bytes; the mesh's table at +0x18).
class CAnimMode
{
public:
	char		mModeName[16];				// +0x00
	EAnimMode	mMode;					// +0x10
	SINT		mStart;			// +0x14
	SINT		mLast;				// +0x18
	SINT		mFrameScaler;				// +0x1c
	float		mRate;		// +0x20
};

// A frame range drawn by one part (12 bytes; the mesh's table at +0x10).
class CCutsceneSequence
{
public:
	CMeshPart	*mCamera;					// +0x00
	SINT		mStart;			// +0x04
	SINT		mEnd;				// +0x08
};

// An emitter (0x150 bytes; the mesh table at +0x20). Deserialize constructs its orientation rows, then reads
// the record raw. A nonzero saved +0x40 word is a presence marker: a following index word selects mParts[index]
// (004ab275..004ab29d). At runtime +0x40 holds that part pointer, not the serialized index.
class CEmitter
{
public:
	FVector		mPos;					// +0x00: in the part's frame (the old-style CMesh::Load 004a9109)
	FMatrix		mOrientation;			// +0x10: read through FMatrix::Read (004a9117); its rows are the three vectors
	CMeshPart	*mMeshPart;					// +0x40: pointer after loading
	SINT		mTrackingVertex;		// +0x44: the part's position nearest the emitter (004a9365)
	SINT		mSubpartEmitter;				// +0x48: 1 from the new format, 0 from the old (004a85e2, 004a90a7)
	char		mName[256];				// +0x4c
	SINT		mNumber;					// +0x14c
};

// A mesh (0x174 bytes, MEMTYPE_MESH). Every mesh is on one list (mesh.cpp's sFirst 00704ad8, next +0x158) and
// counts its users (+0x170). The list head, the default texture and the report flag are mesh.cpp's own: declared
// here as static members they would take three of the _$E numbers of every file that includes this header, and
// FEPGoodies.obj's initializers (0045ac20, 0045ac30) are numbered without them. +0x08 chains the next mesh (a lower level of detail), whose emitters count after this one's.
class CMesh
{
public:
	CMesh();													// 004a5020
	~CMesh();													// 004a50b0
	static BOOL		InitAll();								// 004a5200
	static void		ReleaseAll();							// 004a52b0
	static void		ShutdownAll();									// 004a52d0
	static void		LoadLevelResources(SINT level);				// 004a53f0 (Stuart's CGame::LoadLevel; CFrontEndData::Init passes -2)
	static void		FreeLevelResources();						// 004a5430 (Stuart's name: CGame::Shutdown 0046ca13)
	void			CheckForAnimations(char *name, CAnimMode *anim);	// 004a5500
	void			OptimiseTextures();							// 004a5670
	BOOL			InitFromFile(char *name, SINT flags);				// 004a5970
	BOOL			InitFromMem(class CDXMemBuffer &file, SINT flags);	// 004a5b70
	static CDXTexture	*FindTexture(char *name);					// 004aa410
	static CMesh	*GetMesh(char *name, SINT flags = 1);		// 004aa6e0 (Stuart: CEngine::LoadAllNamedMeshes 00449e7b)
	static CMesh	*GetMeshByResourceID(SINT id);					// 004ab330
#ifdef MESH_CPP	// only there, so the other objects keep their declaration counts
	void			OptimiseParts();							// 004ab360: merges and removes parts (its trace's name)
#endif
	static CMesh	*Deserialize(class CChunkReader *c, class CChunkReader *data = NULL);	// 004aab90 (CFEPGoodies::Deserialise passes 0: read from c)
	SINT			GetNumEmitters();							// 004aa4e0
	void			GetEmitterNameByID(SINT n, char *name, SINT *id);	// 004aa500
	CMeshPart		*GetEmitterSubpartByID(SINT n);						// 004aa5a0
	CMeshPart		*GetMovieCameraSubpart(SINT frame);				// 004aa5e0
	EAnimMode		GetAnimModeByName(char *name);				// 004aa630
	CAnimMode		*GetAnimModePtr(EAnimMode mode);					// 004aa680
	char			*GetName();									// 004aa6b0: the name +0x24, or "unknown mesh name" off the list
	float			GetFrameIncrement(EAnimMode am, int *realindex);	// 004aa7e0
	CMeshPart		*GetEmitterSubpart(char *name, SINT id);		// 004aa820
	CMeshPart		*GetMeshSubpart(char *name);				// 004aa8a0
	void			BuildAllPolyBuckets();						// 004aa900
	FVector			GetRandomVertexPos(CMeshPart **part);			// 004aa940
	CMeshPart		*GetPart(SINT n) { return mParts[n]; }

	void			AddRef()	{ mRefCount++; }
	void			Release()	{ mRefCount--; }
	float			Radius() { return mRadius; }				// a mesh particle copies it through the FPU (CPDMesh::Unknown9 004c4da8)
	// Stuart's accessor (mesh.h), never emitted. DECLARATION-COUNT CARRIER: CGame::DrawDebugStuff (00470650)
	// orders its two global reads as retail only while CMesh declares exactly one member beyond the retail-bodied set (the
	// inline GetMesh wrapper that stage 3 removed had been that member): none = 0 2 8 there; any one extra declaration, any
	// name, body or not, restores it; three extra (GetNumParts, GetRefCount too) lose two functions in other objects.
	float			CueFactor() { return mCueFactor; }
	// two more of Stuart's CMesh members (mesh.h), declared only, never defined or called. DECLARATION-COUNT CARRIERS
	// with vfw.h out of ltshell.h (Stuart's include set) the witnesses DrawDebugStuff (00470650) and
	// BuildDeviceList (00529350) both need exactly two more CMesh declarations than the names pass left.
	void			BuildEmitterCache();
	void			CalculateBases();
	void			ForceDelete() { delete this; }				// CFEPGoodies::FreeUpGoodyResources (0045cd2e..0045cd45)

	CMeshTexture	*mTextures;				// +0x00 (new[])
	SINT			mNumTextures;			// +0x04
	CMesh			*mNextInChain;				// +0x08: the next level of detail (CRTMesh::Render 004dcd9f)
	SINT			mNumSequences;			// +0x0c
	CCutsceneSequence	*mSequences;			// +0x10
	SINT			mNumAnimModes;				// +0x14
	CAnimMode		*mAnimModes;				// +0x18
	SINT			mNumEmitters;			// +0x1c
	CEmitter	*mEmitters;				// +0x20
	char			mName[300];				// +0x24
	CBoundingBox	*mBoundingBox;			// +0x150
	SINT			mResourceID;				// +0x154
	CMesh			*mNextMesh;					// +0x158
	SINT			mNumParts;				// +0x15c
	CMeshPart		**mParts;				// +0x160
	float			mRadius;				// +0x164
	float			mCueFactor;				// +0x168
	SINT			mLastFrameCollisionGenerated;			// +0x16c
	SINT			mRefCount;				// +0x170
};

#include "MeshCollisionVolume.h"	// Stuart's callers reach CMeshCollisionVolume through mesh.h (BattleEngine.cpp)

#endif
