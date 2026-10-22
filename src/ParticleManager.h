// ParticleManager.h: the particle effects (only what matched files need). The descriptor set PARTICLE_SET is at
// 0082b400 (FindByName 004cd7a0); PARTICLE_MANAGER at 009c63e8 (CreateEffect 004cb3d0, five arguments).

#ifndef PARTICLEMANAGER_H
#define PARTICLEMANAGER_H

#include "eventmanager.h"
#include "mathsconst.h"

class CParticleDescriptor;
class CThing;

enum EParticleSet
{
	PARTSET_INGAME = 0,						// CGame::LoadResources (0046cdc7)
	PARTSET_FRONTEND,						// CFrontEndData::Init (00468879)
	PARTSET_UNKNOWN2						// loads the main set too (004cdac9)
};

// The loaded descriptors, a list sorted by name (the retail object is at 0082b400).
class CParticleSet
{
public:
	CParticleSet() { mFirstNode = NULL; mLastNode = NULL; }
	~CParticleSet() {}

	CParticleDescriptor	*Create(char *name, SINT type, BOOL append);	// 004cc020: NULL if the name is taken
	CParticleDescriptor	*FindByName(char *name);			// 004cd7a0
	CParticleDescriptor	*GetPD(char *name) { return FindByName(name); }	// Stuart's name for it (BattleEngine.cpp)
	BOOL				ReadData(class CDXMemBuffer *file);		// 004cd7f0 (its name is in its trace)
	BOOL				LoadAllFromDisk(EParticleSet set);	// 004cda60
	void				DestroyList();						// 004cbff0 (CFrontEndData::Shutdown)
	void				Shutdown();							// Stuart's inline, below (CGame::Shutdown 0046c9bb..0046c9c5)
	void				LinkNode(struct SParticleSetNode *node);	// 004cdba0
	void				UnlinkNode(struct SParticleSetNode *node);	// 004cdbe0

	CParticleDescriptor	*mList;								// +0x00
	struct SParticleSetNode	*mFirstNode;					// +0x04
	struct SParticleSetNode	*mLastNode;						// +0x08
};

// Something the set keeps on a second list, linked at +0x3c and +0x40 (LinkNode 004cdba0).
struct SParticleSetNode
{
	UBYTE				mUnknown00[0x3c];
	SParticleSetNode	*mPrev;								// +0x3c
	SParticleSetNode	*mNext;								// +0x40
};

extern CParticleSet PARTICLE_SET;

class CFoR;
class CParticle;

// A thing's link to a particle effect, kept on a global list (head 0082b3e8): Link 004cb040 pushes it, and the
// destructor unlinks it with Unlink 004cb050 (CHazard's member +0x80, SpawnHazard 005101ad).
class CParticleEffectLink
{
public:
	CParticleEffectLink() { mEffect = NULL; Link(); }
	~CParticleEffectLink() { Unlink(); }	// the out-of-line copy is a jmp (00405d80)
	void	Unlink();						// 004cb050
	void	Link();							// 004cb040
	void	Stop(BOOL now);					// 004cb0b0
	void	Kill() { Stop(FALSE); }
	// Retail clears the link through a member at 00489b40 (CInfantryUnit::StartDieProcess reloads mUnitData after the
	// store, 00489cbd..00489cef). Name open; an operator=(CFoR *) does the same there but costs CPDTimeline 004c2640.
	void	Reset() { mEffect = NULL; }
	BOOL	Full() { return mEffect != NULL; }	// Stuart's (BattleEngine.cpp); not checked yet			// Stuart's name (BattleEngine.cpp: CValidatedFoR::Kill; ~CBattleEngine 00405a92)
	void	SetPos(const FVector &pos);		// below CFoR
	void	SetOrientation(const FMatrix &ori);	// below CFoR

	CParticleEffectLink	*mNext;				// +0x00
	CFoR				*mEffect;			// +0x04
};

// A live particle; the manager keeps them on a list through +0x68 (CFrontEndData::Shutdown 004691cf).
// Stuart's name for the link (BattleEngine.h: CValidatedFoR mSmoke at +0x5f8, 8 bytes before mConfigurationId +0x600).
typedef CParticleEffectLink	CValidatedFoR;

// A particle (0xd8 bytes): 512 of them to a CParticleSystem block, on the manager's free list until one is allocated
// (CParticleManager::AllocateParticle 004cb5c0 sets every field). Its constructor and destructor are empty
// (CParticleSystem's constructor passes 00402d20 and 00459990 to the array iterator).
class CParticle
{
public:
	CParticle() {}
	~CParticle() {}

	void		Destroy();								// 004cae50
	BOOL		AllocateData(SINT size);				// 004caed0: mData, freed first when the descriptor allows it
	FMatrix		&Orientation() { return mOrientation; }
#ifdef PDSIMPLESPRITE_CPP	// CPDMesh::Unknown11's inline budget needs it (its FVector ctor call, 004c8f41); the other objects keep their counts
	CFoR		*GetFoR() { return mFoR; }
#endif

	float		GetLife() { return mLife; }
	BOOL		IsDying() { return (mLife > -2.f) && (mLife < 0.f); }

	CParticle	*mPDPrev;								// +0x00: the descriptor's list (CParticleDescriptor::AddParticle 004c0510)
	CParticle	*mPDNext;								// +0x04
	FMatrix		mOrientation;							// +0x08
	FVector		mPos;									// +0x38
	FVector		mVelocity;								// +0x48
	CFoR		*mFoR;									// +0x58
	class CParticleDescriptor	*mPD;					// +0x5c
	float		mLife;									// +0x60: 100 when allocated; -1 lives for ever
	BOOL		mDead;									// +0x64
	CParticle	*mNext;									// +0x68: the manager's list
	CParticle	*mPrev;									// +0x6c
	SINT		mUnknown70;								// +0x70
	float		mRadius;								// +0x74
	float		mFrame;									// +0x78
	UWORD		mUnknown7C;								// +0x7c
	SWORD		mUnknown7E;								// +0x7e
	SINT		mUnknown80;								// +0x80
	float		mBirthTime;								// +0x84: the manager's time when allocated
	void		*mData;									// +0x88
	class CMesh	*mMesh;									// +0x8c (CBuilding::StartDieProcess 00417d7d)
#ifdef PDSIMPLESPRITE_CPP	// the sprite drawing's view (CPDSimpleSprite::RenderSprites 004c5d50); the others keep the bytes
	DWORD		mColour;								// +0x90: packed for the blend mode
	float		mRenderRadius;							// +0x94
	FVector		mRenderPos;								// +0x98: the position drawn, carried by the frame of reference
	FVector		mAxisX;									// +0xa8: the quad's half axes, for the aligned kinds
	FVector		mAxisY;									// +0xb8
#else
	UBYTE		mUnknown90[0xc8 - 0x90];
#endif
	float		mU0, mV0, mU1, mV1;						// +0xc8: the sprite's tile (CPDSimpleSprite::Unknown9 004c0c17)
};
typedef CParticle	CParticleMesh;						// what a mesh particle flies with (CBuilding::StartDieProcess)

// A frame of reference (0xb8 bytes): what an effect's link points to; the effect's particles move with it. The manager
// makes one per linked effect (CreateEffect 004cb43c) and keeps them on a list (head 0082b3e4) until no particle
// refers to one (CleanupHandles 004caf60).
class CFoR
{
public:
	CFoR();											// ParticleManager.cpp, inlined into CreateEffect
	void		Link();									// likewise: onto the manager's list

	// Move the effect; the first move also sets where it started (CFEPDebriefing::Process 004569e7..00456a95).
	// SpawnBattleEngine 004e4913..004e494a reads back mStartPos when copying it to mOldPos.
	void		SetPos(const FVector &pos)
	{
		if (mOldPos.Z == 10000.f)
		{
			mOldPos = mStartPos = pos;
			SetCurrentPos(pos);
		}
		else
		{
			mOldPos = mPos;
			SetCurrentPos(pos);
		}
	}

	// The tail of SetPos; its out-of-line copy is 00403650 (CUnit::SpawnComponentEffects reaches it through
	// CParticleEffectLink::SetPos, 004fc32c).
	void		SetCurrentPos(const FVector &pos)
	{
		mPos = pos;
		if (mUnknownAC != -1.f)
			mUnknownAC = EVENT_MANAGER.GetTime();
	}

	// a switch: CParticleEffectLink::Stop's dec/je test and case layout (004cb0be..004cb0d1)
	void		Stop(BOOL now)
	{
		switch (now)
		{
		case TRUE:
			mState = 1;
			break;
		default:
			mState = 2;
			break;
		}
	}

	FVector		mPos;									// +0x00
	FMatrix		mOrientation;							// +0x10 (CUnit::SpawnComponentEffects 004fc338)
	FVector		mOldPos;								// +0x40: Z 10000 until first placed
	FMatrix		mOldOrientation;						// +0x50
	FVector		mStartPos;								// +0x80: where it started; DoInterpolation (004cb300) sets it between mOldPos and mPos each frame, the drawn position
	FVector		mUnknown90;								// +0x90
	BOOL		mHasOrientation;						// +0xa0: set with mOrientation (004fc346)
	BOOL		mInUse;									// +0xa4: a particle refers to it this frame
	CParticle	*mParticle;								// +0xa8: that particle (CBuilding::StartDieProcess 00417d2d)
	float		mUnknownAC;								// +0xac: -1 for none
	CFoR		*mNext;									// +0xb0
	UWORD		mState;									// +0xb4: 1 stopping, 2 stopped, 3 gone (CParticleEffectLink::Stop)
	UWORD		mFlags;									// +0xb6
};

// FUNCTION: 0x004c0370
inline void CParticleEffectLink::SetPos(const FVector &pos)
{
	if (mEffect)
		mEffect->SetPos(pos);
}

// A reference after the null check preserves the retail receiver allocation in Mine::Move (004ba7b1) and
// Unit slot 86 (004fbcb0), as well as this out-of-line body. This spelling is reconstructed, not recovered text.
// FUNCTION: 0x004c0450
inline void CParticleEffectLink::SetOrientation(const FMatrix &ori)
{
	if (mEffect)
	{
		CFoR &effect = *mEffect;
		effect.mOrientation = ori;
		effect.mHasOrientation = TRUE;
	}
}

// A block of 512 particles (8 bytes; at most 8, or 12 in multiplayer: AllocateParticle 004cb5f8).
class CParticleSystem
{
public:
	CParticleSystem();									// 004cb0e0
	~CParticleSystem();									// 004cb1b0

	CParticle		*mParticles;						// +0x00
	CParticleSystem	*mNext;								// +0x04
};

class CParticleManager
{
public:
	CParticleManager();									// 0054f740 (DXParticleManager.cpp)
	~CParticleManager();								// 0054f6e0: Shutdown without clearing mSystem
	void	SetRenderStates();							// 0054f760 (DXParticleManager.cpp)
	void	CreateEffect(CParticleDescriptor *pd, CParticleEffectLink *link, FVector pos, BOOL a, BOOL important);	// 004cb3d0
	// Stuart's (BattleEngine.cpp); inline, the file's own ZERO_FVECTOR copied in (CBattleEngine::StartDieProcess 0040c05f)
	// The third argument is CRound::Init's (004d86bb..004d8722: the link is loaded before the flag is pushed).
	void	AddParticle(CParticleDescriptor *pd, CParticleEffectLink *link, BOOL important = FALSE) { CreateEffect(pd, link, ZERO_FVECTOR, 0, important); }
	BOOL	Process(float time, BOOL first);			// 004cb210: TRUE while particles live
	void	DoInterpolation();						// 004cb300 (Stuart's; CDXEngine::Render 0053e8ab)
	void	RenderMeshes();							// 004cbc60
	void	RenderOther();							// 0054f7e0
	// Stuart's full form (DXEngine.cpp's sun: CDXEngine::Render 0053eb6a)
	void	AddParticle(CParticleDescriptor *pd, CParticleEffectLink *link, FVector pos, int a, int b) { CreateEffect(pd, link, pos, a, b); }

	CParticle	*AllocateParticle(CParticleDescriptor *pd, BOOL important);	// 004cb5c0
	void	ProcessParticle(CParticle *p);				// 004cb920: move one, and free it once dead
	BOOL	OnGround(const FVector &pos, float radius, FVector &out);	// 004cba30
	float	GetDistanceSq(CParticle *p);				// 004cba90: to the nearest camera
	void	MoveParticles(CParticle *list);				// 004cbca0
	BOOL	FreeDeadParticles();						// 004cbe30

	void	UnlinkParticle(CParticle *p)
	{
		if (!p->mPrev)
			mParticles = p->mNext;
		else
			p->mPrev->mNext = p->mNext;
		if (p->mNext)
			p->mNext->mPrev = p->mPrev;
	}
	void	FreeParticle(CParticle *p)
	{
		UnlinkParticle(p);
		p->Destroy();
		p->mNext = mFreeParticles;
		mFreeParticles = p;
	}

	static void	ClearOwnerBacklinks();					// 004caf30
	static void	PruneDeadOwnerLinks();					// 004cb080
	static void	CleanupHandles();						// 004caf60
	// Stuart's inline (CGame::InitRestartLoop 0046c591..0046c5a7; CFrontEndData::Init writes the same fields)
	float	GetTime() { return mTime; }
	float	GetFrameLength() { return mFrameLength; }
	BOOL	IsFirstFrame() { return mFirstFrame; }
	void	Init() { mTime = 0; mSystem = NULL; mFreeParticles = NULL; mNumLiveParticles = 0; }
	void	Shutdown();									// Stuart's inline, below (CGame::ShutdownRestartLoop 0046cc75..0046cce2)

	// CFrontEndData::Init and Shutdown (0046885d..0046887a, 004691c3..00469237)
	CParticle		*mParticles;						// +0x00
	SINT			mFrame;								// +0x04
	CParticle		*mFreeParticles;					// +0x08
	CParticleSystem	*mSystem;							// +0x0c
	float			mFrameLength;						// +0x10: this frame's, 0..1
	BOOL			mFirstFrame;						// +0x14
	float			mTime;							// +0x18: the time (Init stores an immediate 0, 0046887a)
	SINT			mNumLiveParticles;							// +0x1c: the live particles
	FVector			mRight;								// +0x20: the billboard axes RenderOther takes from the camera
	FVector			mUp;								// +0x30
};

#include "ParticleTexture.h"
#include "VBufTexture.h"		// CVBufTexture::FreeLevelResources, which CFrontEndData::Shutdown calls beside the particles

inline void CParticleSet::Shutdown() { DestroyList(); CPARTICLETEXTURE::DestroyAll(); }

// The live particles go back on the free list, the owners' links are cleared and the system deleted
// (CFrontEndData::Shutdown 004691c3..00469237 inlines the same).
inline void CParticleManager::Shutdown()
{
	while (mParticles)
	{
		CParticle	*next = mParticles->mNext;

		mParticles->Destroy();
		mParticles->mNext = mFreeParticles;
		mFreeParticles = mParticles;
		mParticles = next;
	}

	ClearOwnerBacklinks();
	PruneDeadOwnerLinks();
	CleanupHandles();

	delete mSystem;
	mSystem = NULL;
}

extern CParticleManager PARTICLE_MANAGER;

#endif
