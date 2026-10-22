// RenderThing.h: CRenderThing, a thing's renderer (vtable 005deaac, 27 slots; only what matched files need).
// Slots from thing.obj's calls: RenderImposter 3 (CThing::RenderImposter 004f3710), GetBoundingBox 4
// (GetBoundingRadius 004f3940), GetRTMesh 9 (CComplexThing::GetAnimModeByName 004f4530) and GetFrameIncrement 14
// (GetRenderThingFrameIncrement 004f3c80). The UnknownN slots are placeholders that keep the order.

#ifndef RENDERTHING_H
#define RENDERTHING_H

#include "renderablething.h"

class CThing;
class CInitThing;
class CBoundingBox;
class CMeshPart;
class CRenderThing;
class CResourceAccumulator;

// render flags (CThing::Render 004f36d0 ORs 2 for a marked objective)
#define RF_HILIGHT 2
#define RF_CLOAKED 0x40		// Stuart's name; CBattleEngine::Render sets it (0040d569)

// A cdecl factory at 005164b0; CThing::InitRenderThing (004f35d0) passes the class id and the IRenderableThing.
CRenderThing	*CreateAndGetRenderThing(int oid, IRenderableThing *thing);
// A cdecl factory at 00516580: the render thing for a CRenderData class id (CFrontEndThing::Init 0046ba05).
CRenderThing	*CreateRenderThing(int rtid);
inline CRenderThing	*SpawnRenderThing(int rtid) { return CreateRenderThing(rtid); }	// Stuart's name (BattleEngine.cpp)

// Stuart's render-thing class ids (only what matched code uses).
enum ERTID
{
	RTID_CRTMesh = 1,				// CUnit::Init and CBattleEngine::Init (004f879f)
};

// What a render thing is initialized from (CRenderThing::Init 004d6a30, CRTMesh::Init 004dc370), 0x41c bytes. Its
// constructor (00403f40) empties four names and zeroes the rest; its destructor (00403f80) frees the pointer array
// at +0x414. Stuart's CRenderData holds one (mInit[1] after mRTID[1], BattleEngine.cpp), so every thing's Init
// builds it through the EH vector constructor (CFEPMultiplayerStart's front-end thing 0046b992, CUnit::Init
// 004f8765, CBattleEngine::Init 00405145). Stuart calls the first name mName.
class CInitRenderThing
{
public:
	CInitRenderThing()
	{
		mName[0] = 0;
		mUnknown100[0] = 0;
		mUnknown200[0] = 0;
		mUnknown300[0] = 0;
		mThing = NULL;
		mUnknown40c = 0;
		mUnknown410 = 0;
		mExtra = NULL;
		mNumExtra = 0;
	}
	~CInitRenderThing()
	{
		for (int i = 0; i < mNumExtra; i++)
			SAFE_DELETE(mExtra[i]);
		SAFE_DELETE_ARRAY(mExtra);
	}

	char				mName[0x100];						// +0x000: empty for the thing's own mesh
	char				mUnknown100[0x100];						// +0x100
	char				mUnknown200[0x100];						// +0x200
	char				mUnknown300[0x100];						// +0x300
	IRenderableThing	*mThing;								// +0x400
	SINT				mUnknown404;							// +0x404
	SINT				mMeshFlags;								// +0x408: passed to CMesh::GetMesh
	SINT				mUnknown40c;							// +0x40c
	SINT				mUnknown410;							// +0x410
	char				**mExtra;								// +0x414: mNumExtra allocations, freed by the destructor
	SINT				mNumExtra;								// +0x418
};

// Stuart's (BattleEngine.cpp): the render-thing class ids and their initializers.
class CRenderData
{
public:
	CRenderData() { mNumRenderThings = 1; }						// CFrontEndThing::Init stores it after mInit (0046b9bf)

	SINT				mUnknown000;							// +0x000 (CUnit::Init's heap copy: mInit at +8, 004f876b)
	SINT				mRTID[1];								// +0x004
	CInitRenderThing	mInit[1];								// +0x008
	SINT				mNumRenderThings;						// +0x424 (004f8779)
};

class CRenderThing
{
public:
	CRenderThing() { mChild = NULL; }							// CRTCutscene's constructor stores +0x10 before its vptr (004dbb64)
	virtual			~CRenderThing() { if (mChild) delete mChild; }			// 0 (inline: CRTMesh's destructor 004dcac2)
	virtual void	Init(class CInitRenderThing *init);			// 1 (004d6a30)
	virtual void	Render(DWORD flags) = 0;					// 2
	virtual void	RenderImposter();							// 3
	virtual CBoundingBox	*GetBoundingBox() { return NULL; }	// 4
	virtual CBoundingBox	*GetPartBoundingBox(int part) { return NULL; }	// 5
	virtual float	GetRTRadius();								// 6
	// 7: inline, defined in RTCutscene.cpp, whose object emits it (004dbb80): it needs the file's ID_FMATRIX, which
	// this header does not include.
	virtual void	GetRTEmitter(char *name, SINT index, FVector &pos, FMatrix &ori, BOOL a, BOOL b);
	// Stuart's position-only form (BattleEngine.cpp): the virtual with TRUE, FALSE (CBattleEngine::Init 00405262).
	FVector			GetRTEmitter(char *name, SINT index) { FVector pos; FMatrix ori; GetRTEmitter(name, index, pos, ori, TRUE, FALSE); return pos; }
	virtual FVector	GetRandomSurfacePoint(SINT frame) { return FVector(0, 0, 0); }	// 8 (inline: RTCutscene.obj emits it, 004dbbe0)
	virtual CMESH	*GetRTMesh() { return NULL; }				// 9
	virtual CMESH	*Unknown10() { return NULL; }								// 10: a mesh (CBuilding::StartDieProcess 00417d6e; CRTMesh's returns NULL)
	virtual void	CreatePolyBuckets();						// 11
	virtual void	Unknown12();								// 12
	virtual void	Unknown13();								// 13
	virtual float	GetFrameIncrement(EAnimMode am, int* realindex) { return 0; }	// 14 (inline: RTCutscene.obj emits it, 004dbd40)
	virtual BOOL	Unknown15() { return TRUE; }								// 15
	virtual BOOL	Unknown16() { return TRUE; }								// 16
	// Stuart's Camera.cpp: rt->GetMovieCameraPosition(&ori,&pos,&fov,thing), rt->GetMovieCameraNumber(thing).
	virtual void	GetMovieCameraPosition(FMatrix *ori, FVector *pos, float *fov, IRenderableThing *thing);	// 17 (004d6a50)
	virtual SINT	GetMovieCameraNumber(IRenderableThing *thing);		// 18
	// The mesh's effect emitters (names starting '_'), by position (CRTMesh 004dd510..004dd5a0).
	virtual SINT	FindEffect(CMeshPart *part)					{ return -1; }		// 19
	virtual bool	GetEffectActive(SINT n)				{ return false; }	// 20
	virtual SINT	GetEffectPart(SINT n)				{ return -1; }		// 21
	virtual SINT	GetEffectId(SINT n)					{ return 0; }		// 22
	virtual void	*GetEffect(SINT n)					{ return NULL; }	// 23
	virtual void	SetEffectActive(SINT n, bool on)	{}					// 24
	virtual void	SetEffect(SINT n, void *effect)		{}					// 25
	virtual void	AccumulateResources(CResourceAccumulator *accumulator, DWORD flags = 0) { if (mChild) mChild->AccumulateResources(accumulator, flags); }	// 26 (inline: RTBuilding.obj emits it, 004db880; Stuart's name and signature, BattleEngine.h:255)

	float			GetCueFactor() { return mCueFactor; }

	float			mCueFactor;									// +0x04
	IRenderableThing	*mThing;								// +0x08 (the init's +0x400; 004d6a41)
	class CMeshPose	*mPose;										// +0x0c (cleared by Init, 004d6a44)
	CRenderThing	*mChild;									// +0x10
};

#endif
