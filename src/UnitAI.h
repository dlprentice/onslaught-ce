// UnitAI.h: CUnitAI (0x60 bytes, vtable 005d8d1c, 12 slots), a unit's AI, and the component AIs that
// CComponent's slot 119 makes. Stuart's release has no AI headers. Its constructor is out of line (004fe710);
// its destructor is inline (the component AIs' implicit destructors store its vtable, 00427fcd).

#ifndef UNITAI_H
#define UNITAI_H

#include "Monitor.h"
#include "activereader.h"

class CUnit;
class CThing;
class CInitThing;

class CUnitAI : public CMonitor
{
public:
	CUnitAI(CUnit *unit, CInitThing *init);			// 004fe710
	virtual			~CUnitAI() {}					// 1 (CUnit's destructor deletes it, 004f8525)
	virtual void	HandleEvent(CEvent *event);		// 0 (004ff330)
	virtual float	Unknown3();						// 3 (004fef40: fires on the target; returns the delay to the next shot)
	virtual void	Unknown4();						// 4 (004ff4f0: refreshes the target and the fire flags)
	virtual BOOL	Unknown5();						// 5 (004fea30: starts following its waypoints)
	virtual BOOL	Unknown6();						// 6 (004febe0: starts fighting)
	virtual BOOL	Unknown7();						// 7 (004ffb60: starts following its launcher's emitter)
	virtual void	Unknown8(CEvent *event);		// 8 (004feac0: event 3001, a waypoint step)
	virtual void	Unknown9(CEvent *event);		// 9 (004fec60: event 3000, a fighting step)
	virtual void	Unknown10(CEvent *event);		// 10 (004ffbb0: event 3002, an emitter step)
	virtual CUnit	*Unknown11();					// 11 (004ff710: picks a target)
			void	Unknown4ffdd0(CUnit *unit, BOOL a);	// 004ffdd0
			CUnit	*GetForUnit() { return mForUnit; }	// Stuart's (his eventmanager.cpp)
			void	NewSweep();						// 004ffe00 (the unit AIs' slot 9s call it)

	CUnit					*mForUnit;				// +0x08 (Stuart's eventmanager.cpp reads it through GetForUnit)
	CActiveReader<CUnit>	mTarget;				// +0x0c
	SINT					mUnknown10;				// +0x10
	SINT					mUnknown14;				// +0x14: CComponent's plain AI clears it (00427f71)
	BOOL					mCanFireAtTarget;				// +0x18: it can fire at the target (CUnit 004fb500)
	BOOL					mTargetInReach;				// +0x1c: the target is in reach (CUnit 004fb5a0)
	SINT	GetMode() { return mAIMode; }
	SINT					mAIMode;				// +0x20: 0 following waypoints, 1 fighting, 2 following an emitter (2 lets the unit ignore gravity)
	CActiveReader<CThing>	mWaypoint;				// +0x24: the waypoint it heads for (the init thing's target)
	CActiveReader<CUnit>	mSpawnedBy;				// +0x28: the unit that launched it (the init thing's mSpawnedBy)
	SINT					mEmitterType;				// +0x2c: that unit's emitter type
	SINT					mEmitterIndex;				// +0x30: the emitter's index
	FVector					mAimPos;				// +0x34: where it aims (the target's slot 90)
	float					mEmitterReleaseTime;				// +0x44: when it stops following the emitter
	// +0x48: a sweeping unit's aim swings from start to end, a 35th of the way each shot (Unknown9 004fedd9..)
	float					mSweepYawStart, mSweepYaw, mSweepYawEnd, mSweepPitchStart, mSweepPitch, mSweepPitchEnd;
};

// The component AIs (vtables 005d96b4, 005d9680, 005d8e08).
class CComponentBomberAI : public CUnitAI
{
public:
	CComponentBomberAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual void	Unknown4();						// 4 (00428e80)
	virtual CUnit	*Unknown11();					// 11 (00428eb0)
};

class CFenrirMainGunAI : public CUnitAI
{
public:
	CFenrirMainGunAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual void	Unknown4();						// 4 (00428e80)
	virtual CUnit	*Unknown11();					// 11 (0044e640)
};

class CRepairPadAI : public CUnitAI
{
public:
	CRepairPadAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual CUnit	*Unknown11();					// 11 (004d6d10: the battle engine standing on the pad)
			BOOL	CanService(class CBattleEngine *be);	// 004d6e00: it needs repair or rearming, and is on a side the pad serves
};

// A dropship's AI (0x64 bytes, vtable 005db1f4; CDropship::Init 00446f2d). Its destructor is emitted in
// Dropship.obj (00447040).
class CDropshipAI : public CUnitAI
{
public:
	CDropshipAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) { mUnknown60 = 0; }
	virtual float	Unknown3();						// 3 (004487e0)
	virtual void	Unknown8(CEvent *event);				// 8 (00448490)
	virtual void	Unknown9(CEvent *event);				// 9 (00448580)

	SINT			mUnknown60;						// +0x60
};

// A submarine's AI (0x60 bytes, vtable 005df404; CSubmarine::Init 004eed4e). Its destructor is emitted in
// Submarine.obj (004eedc0).
class CSubmarineAI : public CUnitAI
{
public:
	CSubmarineAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual void	Unknown8(CEvent *event);				// 8 (004164b0)
	virtual void	Unknown9(CEvent *event);				// 9 (004ef340)
};

// A tentacle's AI (0x60 bytes, vtable 005df498; CTentacle::Unknown119 004f08a7). Its destructor is emitted in
// Tentacle.obj (004f0910).
class CTentacleAI : public CUnitAI
{
public:
	CTentacleAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual void	HandleEvent(CEvent *event);		// 0 (004f1890)
	virtual void	Unknown9(CEvent *event);		// 9 (004f1330)
};

// A sentinel's AI (0x60 bytes, vtable 005dec9c; CSentinel::Init 004deb82). Its destructor is emitted in Sentinel.obj
// (004dec20).
class CSentinelAI : public CUnitAI
{
public:
	CSentinelAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) {}
	virtual float	Unknown3();						// 3 (004df1a0)
};

// An infantryman's AI (0x60 bytes, vtable 005dbf14; CInfantryUnit::Init 00488d82). Its destructor is emitted in
// Infantry.obj (00488de0).
class CInfantryAI : public CUnitAI
{
public:
	CInfantryAI(CUnit *unit, CInitThing *init) : CUnitAI(unit, init) { mUnknown14 = 0; }
	virtual void	Unknown9(CEvent *event);		// 9 (0048a030)
};

// A mech's AI (0x64 bytes; CMech::InitCockpit 0049fa76).
class CMechAI : public CUnitAI
{
public:
	CMechAI(CUnit *unit, CInitThing *init);			// 004a02e0
	virtual void	Unknown9(CEvent *event);		// 9 (004a0450)

	BOOL			mCircleLeft;					// +0x60: which way it circles a target in reach (a coin toss at construction)
};

#endif
