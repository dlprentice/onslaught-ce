// collisionseekingthing.h: CCollisionSeekingThing (vtable 005d9608) and CCSPersistentThing (005df6d8); only what
// matched files need.
//
// CThing::InitCollisionSeekingThing (004f39c0) builds a CCSPersistentThing inline: 0x38 bytes, a
// CHLCollisionDetector member at +0x24 (vtable 005dbf78). Its destructor (004f3a70) destroys that member, then
// calls ~CCollisionSeekingThing (004263f0). Slot 4 answers whether the seeker is persistent (NULL in the base;
// CCSPersistentThing returns itself), which CThing::GetCSPT (004f3d10) calls.

#ifndef COLLISIONSEEKINGTHING_H
#define COLLISIONSEEKINGTHING_H

#include "Monitor.h"
#include "mapwhoentry.h"

class CInitCSThing;
class CCSPersistentThing;
class CThing;

// vtable 005d9608 (11 slots): CMonitor's three, then Init, IsPersistentCSThing, MovedSector, ...
class CCollisionSeekingThing : public CMonitor
{
public:
	virtual			~CCollisionSeekingThing();
	virtual void	Init(CInitCSThing *init);									// 3
	virtual CCSPersistentThing	*IsPersistentCSThing() { return NULL; }			// 4
	virtual void	MovedSector(CSector &from, CSector &to) {}					// 5
	virtual void	Unknown6(CCollisionSeekingThing *other);					// 6 (004264a0, ret 4)
	virtual class CGeneralVolume	*Unknown7() { return mShape; }				// 7 (004de070)
	virtual BOOL	Unknown8(CCollisionSeekingThing *other);					// 8 (00426900, ret 4)
	virtual void	SetShape(class CGeneralVolume *shape);						// 9 (Stuart's name; CBattleEngine::SetCollisionShape 00406444)
	virtual SINT	Unknown10(const CSector &sector);							// 10 (00426920, integer return, ret 4)
	void			SetCollisionMask(ULONG mask);								// 00426480
	// Stuart's; inline (CBattleEngine::MassiveHackPutUsInRightMesh 0040653f)
	class CGeneralVolume	*GetCollisionMeshVolume() { return mCollisionMeshVolume; }
	// Init packs the maximum level into bits 4..5 (00426177..0042617f); Round::Hit reads those bits at 004d8bcb.
	SINT GetMaxCollisionLevel() { return (mCollisionFlags & 0x30) >> 4; }
	// Used accessor form reproduces both x87 loads at 00426796..0042679c; no standalone body is claimed.
	float GetRadius() { return mRadius; }

	CThing			*mForThing;											// +0x08
	ULONG			mCollisionFlags;										// +0x0c, packed by Init 00426163..00426197
	ULONG			mNotSeekCollisionWithBF;								// +0x10
	class CGeneralVolume	*mShape;										// +0x14, approximate collision volume
	class CGeneralVolume	*mCollisionMeshVolume;								// +0x18
	float			mRadius;												// +0x1c
	CThing			*mIgnoreThing;										// +0x20
};

// vtable 005dbf78 (3 slots)
class CHLCollisionDetector : public CMonitor
{
public:
	virtual void	HandleEvent(CEvent *event);									// 004812d0
	void			Init(CCSPersistentThing *thing);								// 00480a30
	void			MovedSector(CSector &from, CSector &to);					// 00481060
	void			CheckCollision(CCSPersistentThing *other);					// 00480c90
	void			CheckCandidate(CCSPersistentThing *other);					// 00480db0
	void			ScanSector(class CMapWhoNode *node);						// 00480e10
	void			ScheduleCollisionCheck(CCSPersistentThing *other);			// 00480ed0

	CCSPersistentThing	*mThing;				// +0x08: set by Init (00480a3e)
	class CScheduledEvent	*mEvent;			// +0x0c: retained/reused by HandleEvent (004812e6)
	BOOL			mCheckingCollision;			// +0x10: guards immediate collision-check recursion
	static SINT		mChecksDone;				// 0067a540; incremented by CheckCollision (00480c90)
};

// vtable 005df6d8 (11 slots)
class CCSPersistentThing : public CCollisionSeekingThing
{
public:
	virtual void	HandleEvent(CEvent *event);									// 00426a20
	virtual void	Init(CInitCSThing *init);									// 004269b0
	virtual CCSPersistentThing	*IsPersistentCSThing() { return this; }
	virtual void	MovedSector(CSector &from, CSector &to);					// 00426a00

	CHLCollisionDetector	mCollisionDetector;								// +0x24
};

// vtable 005de980: the ray has no fields beyond the 0x24-byte base (CRound allocation 004d9b0e).
class CCSRay : public CCollisionSeekingThing
{
public:
	virtual void Init(CInitCSThing *init);									// 00426a40
};

#endif
