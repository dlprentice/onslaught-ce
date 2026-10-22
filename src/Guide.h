// Guide.h: CGuide (10 slots), what steers a unit towards the points its AI picks (CUnit +0x208; unit.cpp knows it as
// CUnitGuide), and the guides the unit objects build. Stuart's release has no guide header. The constructor is out of
// line (0047e290); the subclasses' constructors are inline and their vtables are emitted where they are built.

#ifndef GUIDE_H
#define GUIDE_H

#include "Monitor.h"

class CUnit;
class CNormalSquadUnknownD4;

class CGuide : public CMonitor
{
public:
	CGuide(CUnit *unit);							// 0047e290
	virtual void	Unknown3() = 0;					// 3
	virtual void	GoToPoint(FVector point, BOOL inOverride);	// 4 (0047e2d0)
	virtual void	Unknown5(FVector v);			// 5 (0047e310)
	virtual void	Unknown6(FVector v);			// 6 (0047e340)
	virtual void	Unknown7(FVector v);			// 7 (0047e370)
	virtual void	Stop();							// 8 (0047e3d0)
	virtual CNormalSquadUnknownD4	*GetPath() { return NULL; }	// 9 (the shared 00405930; CUnit 004fe480 forwards it to CNormalSquad's debug view)

	FVector	mDestination;							// +0x08: CUnit::Move deploys within 4 of it (004fb070)
	CUnit	*GetUnit() { return mUnit; }
	FVector	&GetDestination() { return mDestination; }
	SINT	GetMode() { return mMode; }
	CUnit	*mUnit;									// +0x18
	SINT	mMode;									// +0x1c: 0 stopped, 1 going to a point, 2 and 3 set by slots 5..7
};

class CComponentGuide : public CGuide				// 0x20 bytes, vtable 005d9654
{
public:
	CComponentGuide(CUnit *unit) : CGuide(unit) {}
	virtual void	Unknown3();						// 3 (00429270)
};

class CDropshipGuide : public CGuide				// 0x20 bytes, vtable 005db228 (CDropship::Init 00446ee5)
{
public:
	CDropshipGuide(CUnit *unit) : CGuide(unit) {}
	virtual void	Unknown3();						// 3 (00448930)
};

class CSubmarineGuide : public CGuide				// 0x20 bytes (CSubmarine::Init 004eed8e)
{
public:
	CSubmarineGuide(CUnit *unit);					// 004ef570
	virtual void	Unknown3();						// 3 (004ef590)
};

class CTentacleGuide : public CGuide				// 0x20 bytes, vtable 005df46c (CTentacle::Unknown118 004f0827)
{
public:
	CTentacleGuide(CUnit *unit) : CGuide(unit) {}
	virtual void	Unknown3();						// 3 (004f19a0)
};

class CTerrainGuide : public CGuide				// 0x20 bytes, vtable 005df4ec (CSentinel::Init 004deb44)
{
public:
	CTerrainGuide(CUnit *unit);						// 004f1ec0
	virtual void	Unknown3();						// 3 (004f1ee0)
};

class CInfantryGuide : public CGuide				// 0x48 bytes (CInfantryUnit::Init 00488cf4), vtable 005dbfa8
{
public:
	CInfantryGuide(CUnit *unit);					// 0048a3c0
	virtual void	HandleEvent(CEvent *event);		// 0 (0048ac70: event 2000, a target scan every quarter to half second)
	virtual void	Unknown3();						// 3 (0048a570)
#ifdef INFANTRYGUIDE_FULL	// InfantryGuide.cpp's view, with the path (squad.h); the others keep their declaration counts
	virtual CNormalSquadUnknownD4	*GetPath() { return &mPath; }	// 9 (folded with the ground vehicle and mech guides', 0048a4b0)
			void	FindTarget();					// 0048ace0: the nearest enemy vehicle or mech it touches, else infantry

	class COccupancyGrid	*mGrid;					// +0x20: the unit's grid (CUnit 004fd380)
	CNormalSquadUnknownD4	mPath;					// +0x24
	CActiveReader<CThing>	mTarget;				// +0x44
#else
	UBYTE	mUnknown20[0x48 - 0x20];
#endif
};

// The hive boss's guide (0x2c bytes, vtable 005dbe08); CHiveBoss::Init builds it inline (0047ff34) and SetVar sets its
// top speeds ("hb_maxvelx".."hb_maxvelz").
class CHiveBossGuide : public CGuide
{
public:
	CHiveBossGuide(CUnit *unit) : CGuide(unit) { mMaxVelocity[2] = mMaxVelocity[1] = mMaxVelocity[0] = 0.02f; }
	virtual void	Unknown3();						// 3 (00480790)
	virtual void	GoToPoint(FVector point, BOOL inOverride);	// 4 (00480910: the destination only)

	float	mMaxVelocity[3];						// +0x20
};

class CThunderheadGuide : public CGuide			// 0x30 bytes, vtable 005df8d4 (CThunderHead::InitTargeting 004f4900)
{
public:
	CThunderheadGuide(CUnit *unit, FVector home);	// 004f4e00
	virtual void	Unknown3();						// 3 (004f4e40)

	FVector	mHome;									// +0x20: where it was placed; it strays at most 70 from it (004f5163)
};

class CMechGuide : public CGuide					// 0x48 bytes (CMech::InitTargeting 0049fae1)
{
public:
	CMechGuide(CUnit *unit);						// 004a0a20
	virtual void	Unknown3();						// 3 (004a0bc0)
#ifdef MECHGUIDE_FULL	// MechGuide.cpp's view, with the path (squad.h); the others keep their declaration counts
	virtual void	HandleEvent(CEvent *event);		// 0 (004a1200: event 2000, a target scan every half second or less)
	virtual CNormalSquadUnknownD4	*GetPath() { return &mPath; }	// 9 (folded with the infantry guide's, 0048a4b0)
			void	FindTarget();					// 004a1270: the nearest vehicle or mech within 1 of touching

	class COccupancyGrid	*mGrid;					// +0x20: the unit's grid (CUnit 004fd380)
	CNormalSquadUnknownD4	mPath;					// +0x24
	CActiveReader<CThing>	mTarget;				// +0x44
#else
	UBYTE	mUnknown20[0x48 - 0x20];
#endif
};

#endif
