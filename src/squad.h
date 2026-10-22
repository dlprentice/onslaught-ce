// squad.h: CSquad (0xa4 bytes, vtable 005def1c) and the two squads UPhysicsManager::SpawnSquad makes (0050f4b0):
// CRelaxedSquad (0xb4, vtable 005e39cc; built inline: CSquad's constructor 004e5da0, then its member set +0xa4)
// and CNormalSquad (0x144, vtable 005df0f4; constructor out of line, 004e6870). Stuart's release has no squad
// headers. CSquad adds slots 66..86 to CComplexThing's 66, most of them pure; they are open and numbered, typed
// from CNormalSquad's and CRelaxedSquad's bodies. Slot 73 returns the first member of the set at +0xa4
// (004e8cd0), which both squads keep there. Slot 67 returns an FVector through the hidden return pointer
// (004e6da0, 004ea780); its two integer arguments control the normal squad's formation placement.
// Slot 66 receives the scheduled event dispatched at 004e65f8 and reuses it at 004e70e2 / 004ea764.

#ifndef SQUAD_H
#define SQUAD_H

#include "thing.h"
#include "SPtrSet.h"
#include "array.h"

class CUnit;
class CScheduledEvent;
class COccupancyGrid;
class CNormalSquadMember;

DECLARE_THING_CLASS(CSquad, CComplexThing)
public:
	CSquad();																// 004e5da0

	virtual void	HandleEvent(CEvent *event);								// 0 (004e65e0)
			void	Unknown4e6640(CUnit *unit, CThing *other);				// 004e6640: a member touched something (CUnit::Hit 004fcc49)
			void	Unknown4e6660(CUnit *attacker);							// 004e6660: a member was shot by attacker (CUnit::Damage 004f9add)
	virtual	void	Shutdown();												// 2 (004e65b0)
	virtual void	Init(CInitThing *init);									// 9 (004e5e70)
	virtual float	GetRadius() = 0;										// 16
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_20000000); }	// 38 (004e5df0)
	virtual void	DrawDebugStuff(float &y_offset);						// 51 (004e66d0)
	virtual void	DrawDebugStuff3d();										// 52 (004e66e0)
	virtual void	SetAIState(EAIState state)								// 54 (004e5e00, squad.obj's copy)
	{
		if (state == AI_DEFENSIVE)
		{
			mUnknown9C = 1;
			mUnknown8C = mPos;
		}
		else if (state == AI_NORMAL)
			mUnknown9C = 0;
	}
	virtual void	IncreaseThingCounter(CInitThing *inInit);				// 58 (004e6200)

	virtual void	Unknown66(CScheduledEvent *event);						// 66 (folded into 00452b60; out of line: both squads call it, 004e7bf6 and 004ea4e2)
	virtual FVector	Unknown67(CUnit *unit, SINT a, SINT b) = 0;			// 67
	virtual void	Unknown68(CUnit *unit) = 0;									// 68
	virtual BOOL	Unknown69() = 0;										// 69
	virtual BOOL	Unknown70() = 0;										// 70
	virtual FVector	Unknown71();											// 71 (0043e9f0: the position)
	virtual FVector	Unknown72() = 0;										// 72
	virtual CUnit	*Unknown73() = 0;										// 73
	virtual CUnit	*Unknown74() = 0;										// 74
	virtual CUnit	*Unknown75(FVector point) = 0;						// 75: 004e8209/004e8249 pass a vector and consume a unit pointer
	virtual SINT	Unknown76() = 0;										// 76
	virtual CThing	*Unknown77() = 0;										// 77
	virtual float	Unknown78() = 0;										// 78
	virtual float	Unknown79() = 0;										// 79
	virtual float	Unknown80() = 0;										// 80
	virtual SPtrSet<CUnit>	*GetUnits() = 0;							// 81: a new set of the squad's units (Stuart's name; CGame::ToggleDebugUnitForward 0046fba1)
	virtual BOOL	Unknown82() = 0;										// 82
	virtual SINT	Unknown83() = 0;										// 83
	virtual float	Unknown84() = 0;										// 84
	virtual void	Unknown85(SINT a) = 0;									// 85
	virtual void	Unknown86() = 0;										// 86

	BOOL			Unknown4e6610();
	BOOL			Unknown4e6680(EAllegiance allegiance);

	SINT			mUnknown7C;
	BOOL			mUnknown80;												// +0x80: IScript::SpawnThing sets it while it adds the first unit
	BOOL			mUnknown84;												// +0x84: IScript::SpawnThing sets it (005371a0)
	float			mUnknown88;												// +0x88: Unknown4e6660 stores event time + 5
	FVector			mUnknown8C;												// +0x8c: SetAIState(AI_DEFENSIVE) copies mPos
	SINT			mUnknown9C;												// +0x9c: Init copies CSquadInitThing::mMode
	CUnitData		*mUnknownA0;												// +0xa0: Init resolves the unit definition by name
};

// The embedded +0xd4 helper: its constructor in 004e6870 builds two byte arrays;
// 004e76ec/004e76fc read their unsigned entries. Its original class name is open. The constructor is inline:
// CGroundVehicleGuide's (0047d5b9..0047d619) inlines it too.
class CNormalSquadUnknownD4
{
public:
	CNormalSquadUnknownD4() : mUnknown10(1), mUnknown18(1)
	{
		mUnknown08 = 0;
		mUnknown0C = 0;
		mUnknown00 = -1;
		mUnknown04 = -1;
	}
	// Reconstructs 004e76e2 and 004e77aa; exhaustion leaves the caller's coordinates unchanged.
	void GetCurrent(SINT &x, SINT &y)
	{
		if (mUnknown0C > 0)
		{
			x = mUnknown10[mUnknown0C - 1] * 2 + 1;
			y = mUnknown18[mUnknown0C - 1] * 2 + 1;
		}
	}
#ifdef NAVIGATION_FULL	// NavigationMap.cpp's view (the path's own functions are in NavigationMap.obj)
	void	Simplify(class COccupancyGrid *grid);							// 004beea0: drops the cells the ends can see past
	// Adds a cell by its centre in world units (inlined twice by COccupancyGrid::BuildPath, 004be26c, 004be362).
	void	AddCell(SINT x, SINT y)
	{
		if (mUnknown0C >= mUnknown10.Size())
		{
			mUnknown10.ReSize(mUnknown10.Size() * 2);
			mUnknown18.ReSize(mUnknown18.Size() * 2);
		}
		mUnknown10[mUnknown0C] = x >> 1;
		mUnknown18[mUnknown0C] = y >> 1;
		mUnknown0C++;
	}
#endif
	SINT			mUnknown00;
	SINT			mUnknown04;
	SINT			mUnknown08;
	SINT			mUnknown0C;
	CArray<UBYTE>	mUnknown10;
	CArray<UBYTE>	mUnknown18;
};

//******************************************************************************************
class CNormalSquad : public CSquad
{
	friend class CNormalSquadMember;
public:
	typedef CSquad SUPERTYPE;
	CNormalSquad();															// 004e6870
	// Implicit virtual destructor: 004e6ae0; explicit empty bodies add non-retail vptr stores.

	virtual char	*_GetClassName();										// 7 (004e6970)
	virtual void	HandleEvent(CEvent *event);								// 0
	virtual	void	Shutdown();												// 2
	virtual void	Init(CInitThing *init);									// 9
	virtual float	GetRadius();											// 16
	virtual void	Teleport(const FVector &pos);							// 20
	virtual void	TeleportOrientation(const FMatrix &orientation);		// 21
	virtual void	Activate();												// 22
	virtual void	Deactivate();											// 23
	virtual void	MakeVisible();											// 32
	virtual void	MakeInvisible();										// 33
	virtual void	DrawDebugStuff(float &y_offset);						// 51
	virtual void	DrawDebugStuff3d();										// 52
	virtual EAIState	GetAIState();										// 53
	virtual void	SetAIState(EAIState inAIState);							// 54
	virtual void	GoToPoint(FVector point, BOOL inOverride = FALSE);		// 61
	virtual void	Unknown66(CScheduledEvent *event);						// 66 (004e7070)
	virtual FVector	Unknown67(CUnit *unit, SINT a, SINT b);				// 67
	virtual void	Unknown68(CUnit *unit);								// 68 (004e6ff0: copies unit properties)
	virtual BOOL	Unknown69();											// 69
	virtual BOOL	Unknown70();											// 70
	virtual FVector	Unknown71();											// 71
	virtual FVector	Unknown72();											// 72
	virtual CUnit	*Unknown73();											// 73
	virtual CUnit	*Unknown74();											// 74
	virtual CUnit	*Unknown75(FVector point);							// 75
	virtual SINT	Unknown76();											// 76
	virtual CThing	*Unknown77();											// 77
	virtual float	Unknown78();											// 78
	virtual float	Unknown79();											// 79
	virtual float	Unknown80();											// 80
	virtual SPtrSet<CUnit>	*GetUnits();								// 81
	virtual BOOL	Unknown82();											// 82
	virtual SINT	Unknown83();											// 83
	virtual float	Unknown84();											// 84
	virtual void	Unknown85(SINT a);										// 85
	virtual void	Unknown86();											// 86
	virtual void	Unknown87(SINT a);										// 87

	void			Unknown4e6f70(CUnit *unit);					// 004e6f70: removes a member record
	BOOL			Unknown4e7110(CScheduledEvent *event);			// 004e7110
	BOOL			Unknown4e7cf0();								// 004e7cf0
	BOOL			Unknown4e7f40();								// 004e7f40
	void			Unknown4e8100();								// 004e8100
	SINT			Unknown4e81d0();								// 004e81d0
	void			Unknown4e83b0(BOOL a);							// 004e83b0
	BOOL			Unknown4e84e0();								// 004e84e0
	void			Unknown4e8730();								// 004e8730
	void			Unknown4e8930();								// 004e8930
	void			Unknown4e8f80(BOOL a);							// 004e8f80
	BOOL			CanMerge();									// expression in 004e8f80
	void			Unknown4e91f0();								// 004e91f0

	SPtrSet<CNormalSquadMember>	mMembers;					// +0xa4: allocated 0x14-byte records, not raw units
	SINT			mUnknownB4;									// +0xb4: incremented by slot 67, decremented on removal
	SINT			mUnknownB8;									// +0xb8: Init's authored amount
	SINT			mUnknownBC;
	ULONG			mUnknownC0;									// +0xc0: copied from a member's thing type
	CActiveReader<CThing>	mUnknownC4;						// +0xc4: target may be a unit or another squad (004e8219)
	CActiveReader<CThing>	mUnknownC8;						// +0xc8: neighboring thing (004e7b44)
	SINT			mUnknownCC;
	COccupancyGrid	*mUnknownD0;								// +0xd0: CUnit::GetOccupancyGrid result
	CNormalSquadUnknownD4	mUnknownD4;
	FVector			mUnknownF4;
	float			mUnknown104;								// +0x104: radius copied from a member
	float			mUnknown108;								// +0x108: 004e7cf9 copies +0x10c; 004e7782 multiplies it through x87
	float			mUnknown10C;
	BOOL			mUnknown110;								// +0x110: whole-dword FALSE/TRUE writes at 004e71d4/004e726b
	BOOL			mUnknown114;
	float			mUnknown118;								// +0x118: x87 writes/reads at 004e7bd7/004e7c48
	float			mUnknown11C;								// +0x11c: event time at Init
	EAIState		mUnknown120;
	FVector			mUnknown124;
	FVector			mUnknown134;
};

//******************************************************************************************
class CRelaxedSquad : public CSquad
{
public:
	typedef CSquad SUPERTYPE;

	virtual char	*_GetClassName() { return "CRelaxedSquad"; }			// 7
	virtual	void	Shutdown();												// 2 (004ea460)
	virtual void	Init(CInitThing *init);									// 9 (004ea440)
	virtual float	GetRadius();											// 16 (004bfc60: 0)
	virtual void	DrawDebugStuff(float &y_offset);						// 51 (004eaa60)
	virtual void	DrawDebugStuff3d();										// 52 (004eaae0)
	virtual void	Unknown66(CScheduledEvent *event);						// 66 (004ea4d0)
	virtual FVector	Unknown67(CUnit *unit, SINT a, SINT b);				// 67 (004ea780)
	virtual void	Unknown68(CUnit *unit);										// 68 (004014c0: nothing)
	virtual BOOL	Unknown69();											// 69 (00405930: FALSE)
	virtual BOOL	Unknown70();											// 70 (004ea840)
	virtual FVector	Unknown72();											// 72 (004ea980)
	virtual CUnit	*Unknown73();											// 73 (004e8cd0)
	virtual CUnit	*Unknown74() { return Unknown73(); }					// 74
	virtual CUnit	*Unknown75(FVector point);							// 75 (004098b0: NULL)
	virtual SINT	Unknown76();											// 76 (00405930: 0)
	virtual CThing	*Unknown77();											// 77 (00405930: 0)
	virtual float	Unknown78();											// 78 (004bfc60: 0)
	virtual float	Unknown79();											// 79 (004bfc60: 0)
	virtual float	Unknown80();											// 80 (004bfc60: 0)
	virtual SPtrSet<CUnit>	*GetUnits();								// 81 (004ea8d0)
	virtual BOOL	Unknown82();											// 82 (00405930: FALSE)
	virtual SINT	Unknown83();											// 83 (004059c0: 2)
	virtual float	Unknown84();											// 84 (004de700: 1)
	virtual void	Unknown85(SINT a);										// 85 (004014c0: nothing)
	virtual void	Unknown86();											// 86 (004ea880)

	SPtrSet<CActiveReader<CUnit> >	mMembers;							// +0xa4: readers of its units, allocated in slot 67 (004ea7b2)
};

#endif
