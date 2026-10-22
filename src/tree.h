// tree.h: CTree, a tree (tree.obj, 004f5e40..004f707f; RTTI vtable 005dd9d8, 0x4c bytes: SpawnThing allocates it
// as MT_TREE_THING, 004bf1fa; CGame::LoadLevel logs its size). A hit or damaged tree falls: it allocates a falling
// state (0xc0 bytes) and schedules its own updates (event 3001). Stuart's release has no copy.

#ifndef TREE_H
#define TREE_H

#include "thing.h"
#include "ltshell.h"

// The state of a falling tree (0xc0 bytes; its constructor 004f5f60).
class CFallingTree
{
public:
	CFallingTree(const FMatrix &ori, float height, const FVector &dir);	// 004f5f60

	float		mHeight;							// +0x00
	float		mAngle;								// +0x04
	float		mSpeed;								// +0x08: 0.02 at first
	FVector		mDir;								// +0x0c
	FVector		mAxis;								// +0x1c: the fall's axis, normalised
	FMatrix		mOrientation;						// +0x2c
	FMatrix		mStartOrientation;					// +0x5c
	FMatrix		mUnknown8C;							// +0x8c
	float		mStartTime;							// +0xbc: -1 until it lands
};

DECLARE_THING_CLASS(CTree, CThing)
public:
	virtual			~CTree();											// 004f63c0
	virtual void	HandleEvent(CEvent *event);							// 004f7050
	virtual void	Init(CInitThing *init);								// 004f6080
	virtual void	InitCollisionSeekingThing(CInitCSThing *init);		// 004f6480
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_TREE | THING_TYPE_STATIONARY); }
	virtual void	Hit(CThing *other_thing, CCollisionReport *report);	// 004f6aa0
	virtual void	Damage(float amount, CThing *inByThis, BOOL inDamageShields = TRUE, int mesh_part_no = -1);	// 004f68e0
	virtual SINT	GetSoundMaterial() { return 0x66; }
	virtual void	Render(DWORD flags)
	{
		if (mFalling && RenderFalling(flags))
			return;
		LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
		CThing::Render(flags);
		LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	}
	virtual	FMatrix	GetRenderOrientation();								// 004f6560
	virtual	BOOL	GetRequiresPolyBucket() { return FALSE; }
	virtual int		GetRenderImposterNo();									// 004f6540

	UBYTE			GetShadowSize();									// 004f6430 (CEngine::BuildLevelSpecifics 0044a15e)
	void			CreateFallingTree(const FVector &dir);				// 004f69b0: it falls away along dir (CDropship::Move 0044775b)
	void			UpdateFallingTree();								// 004f6b80
	bool			RenderFalling(DWORD flags);							// 004f6fd0: a fallen tree fades out

	SINT			mUnknown3C;											// +0x3c
	SINT			mVariation;											// +0x40: one of the 32 orientations (Init 004f61da)
	float			mLife;												// +0x44
#ifdef TREE_CPP	// UpdateFallingTree reads the falling state through it (its inline budget: retail calls FVector's constructor out of line there)
	CFallingTree	*GetFalling() { return mFalling; }
#endif
	CFallingTree	*mFalling;											// +0x48: CreateFallingTree does nothing once it is set (004f69cb)
};

#endif
