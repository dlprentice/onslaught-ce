// DestructableSegmentsController.h: the segments controller a unit may be built from (only what matched files
// need; its object's functions are declared, not yet reconstructed).

#ifndef DESTRUCTABLESEGMENTSCONTROLLER_H
#define DESTRUCTABLESEGMENTSCONTROLLER_H

#include "Motioncontroller.h"

class CThing;
class FVector;

// The segments controller a unit may be built from (its object's functions; only their calls here).
class CDestructableSegmentsController
{
public:
			CDestructableSegmentsController(class CUnit *owner, float a, BOOL b, SINT c);	// 00443fc0 (CBuilding::Init 00417223)
	void	DamageRandomly();												// 00444160
	float	GetLife();														// 00444330
	float	GetLifeUnknown444370();											// 00444370
	float	GetLifeUnknown4443b0();											// 004443b0
	void	GetCentre(FVector *out);										// 00445070
	float	GetLastDamageTime(SINT part);									// 004442d0
	float	GetLastDamageAmount(SINT part);									// 00444300
	void	Unknown4443f0();												// 004443f0
	void	Damage(SINT part, float amount, CThing *by);					// 00444030 (CUnit::Damage 004f9ddc)
	void	Init();															// 00444660 (CUnit::Init 004f9078)
	void	SetSegmentHealth(char *name, float health);						// 00444450 (IScript::SetSegmentHealth 005354b1)
	void	ResetSegmentHealth(char *name, float health);					// 004444b0 (IScript::ResetSegmentHealth 005354f1)
	void	SetAllSegmentsHealth(float health);								// 00444580 (IScript::SetAllSegmentsHealth 00535525)
	void	SetSegmentVulnerable(char *name, BOOL vulnerable);				// 004445b0 (IScript::SetSegmentVulnerable 00534333)
	void	SetAllSegmentsVulnerable(BOOL vulnerable);						// 00444620 (IScript::SetAllSegmentsVulnerable 005343b7)
			~CDestructableSegmentsController();								// 00444000
	class CDestroyableSegment	*FindSegment(char *name);				// 00444520
	class CDestroyableSegment	*CreateSegment(SINT type, class CMeshPart *part, class CDestroyableSegment *parent, float size);	// 004449c0
	void	ProcessNode(class CMeshPart *part, class CDestroyableSegment *parent);	// 00444c10
	SINT	GetDamageStage(SINT part);										// 00444f00
	BOOL	CanUse(SINT part);												// 00444f20
	float	GetCoreFall(SINT part);											// 00445010
	float	GetUnknown14() { return mUnknown14; }									// inline: 004436d0 compares its result as a value (fld/fxch/fcompp at 00443721)

	CUnit *GetOwner() { return mOwner; }

	BOOL	mChanged;														// +0x00: a segment changed (the constructor sets it, 00443ff5)
	class CDestroyableSegment	**mSegments;						// +0x04: one per mesh part (Init 004446e2)
	SINT	mNumSegments;													// +0x08
	class CDestroyableCoreSegment	*mCore;								// +0x0c: the primary core (ProcessNode 00444d6a)
	class CUnit	*mOwner;													// +0x10
	float	mUnknown14;														// +0x14: how far a core falls before it breaks (00443721)
	float	mTotalHealth;													// +0x18
	float	mTotalSize;														// +0x1c: the parts' sizes (ProcessNode 00444da5)
	SINT	mUnknown20;														// +0x20: how many core segments (CreateSegment 004449ee); a building without any drops its controller (CBuilding::Init 00417288)
	BOOL	mUnknown24;														// +0x24 (the constructor's third argument)
	SINT	mUnknown28;														// +0x28: 1 lets a pickup's explosion hit everything (00442828)
	SINT	mUnknown2C;														// +0x2c: 1 keeps a building's imposter from drawing (CBuilding::RenderImposter 0041810a)
};

// The motion controller that animates a segmented unit's parts (vtable 005dc27c; size 0x10).
class CDestructableSegmentsMotionController : public CMotionController
{
public:
			CDestructableSegmentsMotionController(CDestructableSegmentsController *segments);	// 00494c60 (CBuilding::Init 0041726a)
#ifdef MCDESTRUCTABLESEGMENTS_CPP	// MCDestructableSegments.cpp's view; the other includers keep their counts
	virtual			~CDestructableSegmentsMotionController();				// 1 (00494cc0)
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (00494ce0)
	virtual void	Unknown6(CMeshPart *part, SINT *flags);				// 6 (00494fa0)
	virtual SINT	Unknown7(CMeshPart *part);								// 7 (00494ff0)
	virtual BOOL	Unknown8();												// 8 (00495020)
	// Retail 005dc27c slot 9 is TRUE (the non-inline fold 004fdc10); CMCHiveBoss inherits it at 005dc388.
	virtual BOOL	Unknown9();												// 9 (004fdc10)

	// The mesh optimizer keeps a segmented unit's parts apart (CMeshPart 004bae70, 004bb040; names ours).
	static BOOL		CanOptimisePart(CMeshPart *part);						// 00495030
	static BOOL		CanMergePart(CMeshPart *part);							// 00495090
	static BOOL		HasCore(class CMesh *mesh);								// 004950f0
#endif

	CDestructableSegmentsController	*mSegments;								// +0x0c (00494c72)
};

#endif
