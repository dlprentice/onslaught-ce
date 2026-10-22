// DestroyableSegment.h: the parts of a segmented building or unit that break separately (DestroyableSegment.obj,
// 004423b0..00443ecf, MT_DESTROYABLESEGMENT; Stuart's release has no copy).
//
// A CDestructableSegmentsController keeps one segment per mesh part (its mSegments). Every segment is a CMonitor so it
// can schedule its own breaking (event 3000). Six classes (RTTI, config/classes.tsv), all 12 virtual slots; the
// segment type is slot 5, pure in the base (0055df1f is _purecall). Layout from the constructor 004425a0 and the
// derived constructors inlined in CDestructableSegmentsController::CreateSegment (004449c0).

#ifndef DESTROYABLESEGMENT_H
#define DESTROYABLESEGMENT_H

#include "Monitor.h"
#include "SPtrSet.h"
#include "activereader.h"

class CDestructableSegmentsController;
class CThing;
class CUnit;

// The segment types (CreateSegment's switch and each class's slot 5).
enum EDestroyableSegmentType
{
	DST_CORE = 0,				// "core" parts (CDestroyableCoreSegment 00405930 returns 0)
	DST_SWAP = 1,				// parts with damage levels ("<name>_damaged")
	DST_EXTRA = 2,
	DST_EXTRA_USE_MESH = 3,
	DST_COMPONENT = 4,			// a component unit attached to a part (CDestroyableSegmentComponent 004db8c0)
};

#define SEGMENT_BREAK			3000	// a segment's HandleEvent breaks it (00443464)
#define SEGMENT_CORE_CASCADE	3002	// CDestroyableCoreSegment::HandleEvent (004436db)

class CDestroyableSegment : public CMonitor
{
public:
	CDestroyableSegment(CDestructableSegmentsController *controller, SINT part, CDestroyableSegment *parent, float size);	// 004425a0
	virtual			~CDestroyableSegment();

	virtual void	HandleEvent(CEvent *event);
	virtual void	Damage(float amount, CThing *by);					// 3
	virtual SINT	GetDamageStage() { return 0; }						// 4
	virtual SINT	GetType() = 0;										// 5
	virtual BOOL	CanBreak();											// 6
	virtual float	GetCoreTime() { return 0.0f; }						// 7
	virtual void	Break();											// 8
	virtual void	Update();											// 9
	virtual void	SpawnRubble();										// 10
	virtual void	SetLife(float life, float total);					// 11

	void			RegisterChild(CDestroyableSegment *child);			// 00442700
	void			SpawnPickup(FVector pos);							// 00442710
	float			GetActiveHealth();									// 00442890
	float			GetTotalHealth();									// 00442900
	void			ScheduleChildBreaks();								// 004429a0
	void			ActivateAll();										// 00442a80
	void			DamageChildren(float amount);						// 00442ac0

	SINT				mPart;						// +0x08: the mesh part's number (-1 for a component)
	float				mHealth;					// +0x0c
	float				mMaxHealth;					// +0x10
	float				mLastDamageTime;			// +0x14
	float				mLastDamageAmount;			// +0x18
	BOOL				mVulnerable;					// +0x1c
	CDestroyableSegment	*mParent;					// +0x20
	SPtrSet<CDestroyableSegment>	mChildren;		// +0x24
	float				mSize;						// +0x34: its share of the controller's life
	BOOL				mDestroyed;					// +0x38
	CDestructableSegmentsController	*mController;	// +0x3c
};

class CDestroyableCoreSegment : public CDestroyableSegment
{
public:
	CDestroyableCoreSegment(CDestructableSegmentsController *controller, SINT part, CDestroyableSegment *parent, float size, SINT number);	// 00443480

	virtual void	HandleEvent(CEvent *event);
	virtual void	Damage(float amount, CThing *by);
	virtual SINT	GetType() { return DST_CORE; }
	virtual BOOL	CanBreak();
	virtual float	GetCoreTime() { return mTime; }
	virtual void	Break();
	virtual void	SetLife(float life, float total);

	BOOL			AreChildrenDestroyed();								// 004433f0

	SINT			mNumber;						// +0x40: 1 for the primary core
	float			mTotalTime;						// +0x44
	float			mTime;							// +0x48
	BOOL			mCascading;						// +0x4c
};

class CDestroyableExtraSegment : public CDestroyableSegment
{
public:
	CDestroyableExtraSegment(CDestructableSegmentsController *controller, SINT part, CDestroyableSegment *parent, float size)
		: CDestroyableSegment(controller, part, parent, size), mHasDirection(FALSE) {}

	virtual void	Damage(float amount, CThing *by);
	virtual SINT	GetType() { return DST_EXTRA; }
	virtual void	Break();

	FVector			mDirection;						// +0x40: away from what destroyed it
	BOOL			mHasDirection;					// +0x50
};

class CDestroyableExtraUseMeshSegment : public CDestroyableExtraSegment
{
public:
	CDestroyableExtraUseMeshSegment(CDestructableSegmentsController *controller, SINT part, CDestroyableSegment *parent, float size)
		: CDestroyableExtraSegment(controller, part, parent, size) {}

	virtual SINT	GetType() { return DST_EXTRA_USE_MESH; }
	virtual void	SpawnRubble();
	virtual void	SetLife(float life, float total);
};

// Declared after the extra segments: the declaration order decides the order of the RTTI records (their reverse); retail has
// Component, Swap, ExtraUseMesh, Extra.
class CDestroyableSwapSegment : public CDestroyableSegment
{
public:
	CDestroyableSwapSegment(CDestructableSegmentsController *controller, SINT part, CDestroyableSegment *parent, float size, SINT stages)
		: CDestroyableSegment(controller, part, parent, size), mNumStages(stages), mSwapped(FALSE) {}

	virtual void	Damage(float amount, CThing *by);
	virtual SINT	GetDamageStage();
	virtual SINT	GetType() { return DST_SWAP; }
	virtual void	Break();

	SINT			mNumStages;						// +0x40: the part's damage levels
	BOOL			mSwapped;						// +0x44
};

class CDestroyableSegmentComponent : public CDestroyableSegment
{
public:
	CDestroyableSegmentComponent(CDestructableSegmentsController *controller, CDestroyableSegment *parent, CUnit *component)
		: CDestroyableSegment(controller, -1, parent, 0.0f), mComponent(component) {}

	virtual void	Damage(float amount, CThing *by) {}
	virtual SINT	GetDamageStage() { return 0; }
	virtual SINT	GetType() { return DST_COMPONENT; }
	virtual void	Break();
	virtual void	Update() {}
	virtual void	SpawnRubble() {}

	CActiveReader<CUnit>	mComponent;				// +0x40
};

#endif
