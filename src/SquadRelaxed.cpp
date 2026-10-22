// SquadRelaxed.cpp: reconstructed from SquadRelaxed.obj (004ea3xx..004eabcf; retail's own name for the file, 00632918);
// Stuart's release has no copy. A relaxed squad keeps no formation: its members gather loosely about their centre,
// which drifts 45 units towards the target its side's selector picks.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "squad.h"
#include "unit.h"
#include "world.h"
#include "map.h"
#include "eventmanager.h"
#include "game.h"
#include "Platform.h"
#include "engine.h"
#include "renderinfo.h"
#include "NormalSquadTargetSelector.h"

#include <stdio.h>

// FUNCTION: 0x004ea350 init:ZERO_FVECTOR
// FUNCTION: 0x004ea370 init:ID_FMATRIX

//******************************************************************************************
void CRelaxedSquad::Init(CInitThing *init)	// FUNCTION: 0x004ea440
{
	SUPERTYPE::Init(init);
	Unknown66(NULL);
}

//******************************************************************************************
void CRelaxedSquad::Shutdown()	// FUNCTION: 0x004ea460
{
	mMembers.DeleteAll();
	SUPERTYPE::Shutdown();
}

//******************************************************************************************
// Each few seconds: forget the members that have gone, move the centre towards the target, and send the idle members
// there.
void CRelaxedSquad::Unknown66(CScheduledEvent *event)	// FUNCTION: 0x004ea4d0
{
	CSquad::Unknown66(event);

	ListIterator<CActiveReader<CUnit> >	members(&mMembers);
	CActiveReader<CUnit>	*next = members.First();
	while (next)
	{
		CActiveReader<CUnit>	*member = next;
		next = members.Next();
		if (!member->ToRead())
		{
			mMembers.Remove(member);
			delete member;
		}
	}

	mPos = Unknown72();

	CThing	*target = NULL;
	if (mUnknown7C == 1)
		target = ((CNormalSquadTargetSelector *)INFLUENCE_BUFFER_B)->Unknown477cb0(this);
	else if (mUnknown7C == 0)
		target = ((CNormalSquadTargetSelector *)INFLUENCE_BUFFER_A)->Unknown477cb0(this);
	if (target)
	{
		FVector	d = target->GetPos() - mPos;
		float	m = d.Magnitude();		// Normalise() written out: its inline copy orders the x87 stack differently
		if (m != 0) { float inv = 1.0f / m; d.X *= inv; d.Y *= inv; d.Z *= inv; }
		d *= 45.0f;
		mPos += d;
	}

	ListIterator<CActiveReader<CUnit> >	it(&mMembers);
	for (CActiveReader<CUnit> *r = it.First(); r; r = it.Next())
	{
		CUnit	*unit = r->ToRead();
		if ((unit) && (!unit->GetCurrentTarget()))
		{
			if ((unit->GetPos() - mPos).MagnitudeXYSq() < 400.0f)
				unit->GoToPoint(unit->GetPos(), FALSE);
			else
				unit->GoToPoint(mPos, FALSE);
		}
	}

	if (Unknown70())
		AddShutdownEvent();
	else
		EVENT_MANAGER.AddEvent(4002, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() * 5.0f + 5.0f), START_OF_FRAME, NULL, event);
}

//******************************************************************************************
// A unit joins: it stands beside the last one, one and a half radii apart. The last component is bracketed,
// a value of its own, as in retail's x87 order (README 'How a value is read').
FVector CRelaxedSquad::Unknown67(CUnit *unit, SINT a, SINT b)	// FUNCTION: 0x004ea780
{
	if ((unit) && (!unit->GetSquad()))
	{
#line 100
		CActiveReader<CUnit>	*member = new(MT_SQUAD) CActiveReader<CUnit>;
		member->SetReader(unit);
		mMembers.Add(member);
		float	offset = unit->GetRadius() * (mMembers.Size() - 1) * 1.5f;
		FVector	side(offset * mOrientation.Row[0].X, offset * mOrientation.Row[1].X, (offset * mOrientation.Row[2].X));
		return side + unit->GetPos();
	}
	return FVector(0, 0, 0);
}

//******************************************************************************************
// It is finished when none of its members is left.
BOOL CRelaxedSquad::Unknown70()	// FUNCTION: 0x004ea840
{
	if (mUnknown80)
		return FALSE;
	ListIterator<CActiveReader<CUnit> >	it(&mMembers);
	for (CActiveReader<CUnit> *r = it.First(); r; r = it.Next())
	{
		if (r->ToRead())
			return FALSE;
	}
	return TRUE;
}

//******************************************************************************************
void CRelaxedSquad::Unknown86()	// FUNCTION: 0x004ea880
{
	CActiveReader<CUnit>	*member;
	while (member = mMembers.First())
	{
		if (member->ToRead())
			member->ToRead()->Shutdown();
		mMembers.Remove(member);
		delete member;
	}
}

//******************************************************************************************
SPtrSet<CUnit> *CRelaxedSquad::GetUnits()	// FUNCTION: 0x004ea8d0
{
#line 160
	SPtrSet<CUnit>	*units = new(MT_SQUAD) SPtrSet<CUnit>;
	ListIterator<CActiveReader<CUnit> >	it(&mMembers);
	for (CActiveReader<CUnit> *r = it.First(); r; r = it.Next())
	{
		if (r->ToRead())
			units->Add(r->ToRead());
	}
	return units;
}

//******************************************************************************************
// The centre of its members, or its own position when it has none.
FVector CRelaxedSquad::Unknown72()	// FUNCTION: 0x004ea980
{
	FVector	centre(0, 0, 0);
	SINT	count = 0;
	ListIterator<CActiveReader<CUnit> >	it(&mMembers);
	for (CActiveReader<CUnit> *r = it.First(); r; r = it.Next())
	{
		CUnit	*unit = r->ToRead();
		if (unit)
		{
			centre += unit->GetPos();
			count++;
		}
	}
	if (count)
		centre /= (float)count;
	else
		centre = mPos;
	return centre;
}

//******************************************************************************************
float CRelaxedSquad::GetRadius()
{
	return 0;
}

//******************************************************************************************
void CRelaxedSquad::DrawDebugStuff(float &y_offset)	// FUNCTION: 0x004eaa60
{
	SUPERTYPE::DrawDebugStuff(y_offset);

	char	text[256];
	sprintf(text, "Relaxed Squad");
	PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
	y_offset += 10.0f;
}

//******************************************************************************************
void CRelaxedSquad::DrawDebugStuff3d()	// FUNCTION: 0x004eaae0
{
	FMatrix	ori = ID_FMATRIX;
	RENDERINFO.SetWorld(FVector(0, 0, 0), ID_FMATRIX);
	float	r = GetRadius();
	FVector	axes(r, r, r);
	FVector	pos = mPos;
	pos.Z = MAP.Collide(pos) - 0.1f;
	ENGINE.DrawDebugCuboid(0xffffff00, axes, pos, ori, ENGINE.GetOutlineTexture());
}

//******************************************************************************************
// The relaxed squad's answers that no code above defined. Each compiles to a body another function already
// has, so the linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address
// each annotation names (the class header notes the slots' survivors too).

// FUNCTION: 0x004e8cd0
CUnit *CRelaxedSquad::Unknown73()
{
	ListIterator<CActiveReader<CUnit> >	it(&mMembers);
	for (CActiveReader<CUnit> *member = it.First(); member; member = it.Next())
	{
		if (member->ToRead())
			return member->ToRead();
	}
	return NULL;
}

// FUNCTION: 0x004098b0
CUnit *CRelaxedSquad::Unknown75(FVector point)
{
	return NULL;
}
