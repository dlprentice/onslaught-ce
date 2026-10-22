// SquadNormal.cpp: CNormalSquad's lifecycle, controller draft and matched member queries.

#include "common.h"
#include "squad.h"
#include "unit.h"
#include "world.h"
#include "map.h"
#include "eventmanager.h"
#include "game.h"
#include "FearGrid.h"
#include "NormalSquadTargetSelector.h"
#include "engine.h"
#include "Platform.h"

#include <stdio.h>
#include "DXEngine.h"

// FUNCTION: 0x004e6780 init:ZERO_FVECTOR
// FUNCTION: 0x004e67a0 init:ID_FMATRIX

// world.h's COccupancyGrid::UnknownCell as a file-local copy: see the stand-in note on CNormalSquad::Unknown4e7cf0.
static inline BOOL CellOf(COccupancyGrid *grid, const FVector &pos)
{
	SINT x0 = (SINT)pos.X;
	SINT x = x0 >> 1;
	SINT y = (SINT)pos.Y >> 1;
	return grid->mData[(x >> 3) * 256 + y] & (1 << (x % 8));
}

// These 0x14-byte records are constructed inline in slot 67 (004e6ebd..004e6edb).
class CNormalSquadMember
{
public:
	CNormalSquadMember(CUnit *unit, CNormalSquad *squad)
	{
		mUnit = unit;
		mUnknown04 = 0;
		mUnknown08 = 0;
		mSquad = squad;
		mUnknown0C = EVENT_MANAGER.GetTime();
	}

	// Reconstructs the expressions at 004e7d6b and 004e7e21; the wrapper's original name is unknown. The orientation
	// is the squad's member, read as a friend: an accessor is evaluated before an out-of-line constructor, where
	// retail reads the squad pointer again after it (004e7d8a, 004e973d).
	FVector GetPos()
	{
		return mSquad->GetPos() + mSquad->mOrientation * FVector(mUnknown04, mUnknown08, 0);
	}
	FVector GetFormationPos() { return GetPos(); }

	void TeleportToPosition()
	{
		if (mUnit.ToRead())
		{
			mUnit->Teleport(GetPos());
			FVector	p = mUnit->GetPos();
			p.Z = MAP.Collide(p);
			if (p.Z < mSquad->GetPos().Z)
				mUnit->Teleport(p);
		}
	}

	// Called twice in 004e7cf0, including the discarded result at 004e7dce.
	float GetRadius() { return mSquad->Unknown78(); }

	// These member-local wrappers reconstruct the repeated paths in 004e8730/004e8930.
	void MoveToPosition()
	{
		if (mUnit.ToRead())
			mUnit->GoToPoint(GetPos());
	}
	BOOL IsOutOfPosition()
	{
		if (mUnit.ToRead())
		{
			FVector delta = mUnit->GetPos() - GetPos();
			GetRadius();
			float dist = delta.MagnitudeXY();
			if (GetRadius() * 1.5 < dist)
				return TRUE;
		}
		return FALSE;
	}
	BOOL HasWeapon() { return mUnit.ToRead() && mUnit->AnyWeaponUnknown94(); }
	// SetTarget's two steps as two wrappers (names ours): see the stand-in note on CNormalSquad::Unknown4e8930.
	void GuideTo(CUnit *target)
	{
		if (target)
			mUnit->GuideUnknown7(target->GetPos());
	}
	void TargetAI(CUnit *target)
	{
		CUnit	*unit = mUnit.ToRead();
		if (unit->mAI)
			unit->mAI->Unknown4ffdd0(target, FALSE);
	}
	void SetTarget(CUnit *target)
	{
		if (mUnit.ToRead())
		{
			GuideTo(target);
			TargetAI(target);
		}
	}
	BOOL Unknown4e97e0(CNormalSquadMember *other);

	// Reconstructs 004e7e07: missing readers and absent grids pass this test.
	BOOL IsPositionOpen()
	{
		if (mUnit.ToRead() && mSquad->mUnknownD0 && !CellOf(mSquad->mUnknownD0, GetPos()))
			return FALSE;
		return TRUE;
	}

	CActiveReader<CUnit>	mUnit;
	float			mUnknown04;			// x87 loads at 004e7e21 / 004e7e28
	float			mUnknown08;
	float			mUnknown0C;
	CNormalSquad	*mSquad;
};
// Inline, before the constructor: retail emits these in its vtable group, ahead of ??_GCNormalSquad (004e6970..004e6a20).
inline char *CNormalSquad::_GetClassName()	// FUNCTION: 0x004e6970
{
	return "CNormalSquad";
}

inline FVector CNormalSquad::Unknown71()	// FUNCTION: 0x004e6980
{
	return mUnknownF4;
}

inline FVector CNormalSquad::Unknown72()	// FUNCTION: 0x004e69b0
{
	return mUnknown124;
}

inline SINT CNormalSquad::Unknown76()	// FUNCTION: 0x004e69e0
{
	return mUnknownC0;
}

inline CThing *CNormalSquad::Unknown77()	// FUNCTION: 0x004e69f0
{
	return mUnknownC4.ToRead();
}

inline float CNormalSquad::Unknown78()	// FUNCTION: 0x004e6a00
{
	return mUnknown104;
}

inline EAIState CNormalSquad::GetAIState()	// FUNCTION: 0x004e6a10
{
	return mUnknown120;
}

inline void CNormalSquad::SetAIState(EAIState state)	// FUNCTION: 0x004e6a20
{
	if (state == AI_OFF)
	{
		mUnknownC4 = NULL;
		mUnknown120 = AI_OFF;
	}
	else if (state == AI_DEFENSIVE)
	{
		mUnknown9C = 1;
		mUnknown8C = mPos;
		mUnknownF4 = mPos;
	}
	else if (state == AI_NORMAL)
		mUnknown9C = 0;
	else
		mUnknown120 = state;
}

CNormalSquad::CNormalSquad()	// FUNCTION: 0x004e6870
{
	mUnknownB4 = 0;
	mUnknown108 = 0;
	mUnknownBC = 0;
	mUnknownD0 = NULL;
	mUnknown118 = 0;
	mUnknown114 = TRUE;
}

// Reconstructs the repeated eligibility tests at 004e8f97 and 004e90cf.
inline BOOL CNormalSquad::CanMerge()
{
	if (mUnknown10C > 0 && !mUnknown84 && mUnknown114 && !mUnknown80 && !IsShuttingDown())
	{
		ListIterator<CNormalSquadMember> it(&mMembers);
		for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		{
			if (member->mUnit.ToRead() && !member->mUnit->IsOnGround())
				return FALSE;
		}
		return TRUE;
	}
	return FALSE;
}

// FUNCTION: 0x004e6ac0 ??_GCNormalSquad@@UAEPAXI@Z
// FUNCTION: 0x004e6ae0 ??1CNormalSquad@@UAE@XZ

void CNormalSquad::Init(CInitThing *init)	// FUNCTION: 0x004e6bb0
{
	mUnknownB8 = ((CSquadInitThing *)init)->mAmount;
	mUnknown104 = 0;
	mUnknown10C = 0;
	mUnknownC0 = 0;
	mUnknownBC = 0;
	mPos = init->mPos;
	mPos.Z = MAP.Collide(mPos);
	mUnknown120 = AI_ON;
	mUnknown134 = mPos;
	mUnknown124 = mPos;
	SUPERTYPE::Init(init);
	if (mUnknown7C == 1 || mUnknown7C == 6)
		WORLD.mSet30.Append(this);
	if (mUnknown7C == 0 || mUnknown7C == 6)
		WORLD.mSet20.Append(this);
	mUnknownCC = 0;
	mUnknownF4 = mPos;
	mUnknown11C = EVENT_MANAGER.GetTime();
	Unknown4e8100();
	Unknown4e83b0(TRUE);
	Unknown66(NULL);
}

void CNormalSquad::Shutdown()	// FUNCTION: 0x004e6ce0
{
	CNormalSquadMember *member;
	while (member = mMembers.First())
	{
		mMembers.Remove(member);
		mUnknownB4--;
		delete member;
	}
	mUnknownD0 = NULL;
	SUPERTYPE::Shutdown();
}

void CNormalSquad::Unknown86()	// FUNCTION: 0x004e6d40
{
	CNormalSquadMember *member;
	while (member = mMembers.First())
	{
		if (member->mUnit.ToRead())
			member->mUnit->Shutdown();
		mMembers.Remove(member);
		mUnknownB4--;
		delete member;
	}
}

FVector CNormalSquad::Unknown67(CUnit *unit, SINT a, SINT b)	// FUNCTION: 0x004e6da0
{
	if (unit && !unit->GetSquad())
	{
		mUnknown104 = unit->GetRadius();
		mUnknownD0 = unit->GetOccupancyGrid();
		// 005df254 is 0x3d75c290; the expression that produced it remains open.
		mUnknown10C = unit->Unknown111() * 0.06000000238418579f;
		mUnknownC0 = unit->GetThingType();
		if (!b)
		{
			a = mUnknownB4;
			b = mUnknownB4 + 1;
		}
		SINT width = (SINT)ceil(sqrt((double)b));
		SINT row = a / width;
		SINT column = a - row * width;
		float x = (float)(width / 2) * mUnknown104 * 2.0f - (float)column * mUnknown104 * 2.0f;
		float y = (float)row * mUnknown104 * -2.0f;
#line 129
		CNormalSquadMember *member = new(MT_BUBBLE) CNormalSquadMember(unit, this);
		mUnknownB4++;
		mMembers.Add(member);
		unit->SetSquad(this);
		Unknown4e8730();
		return FVector(mPos.X + x, mPos.Y + y, mPos.Z);
	}
	return FVector(0, 0, 0);
}

void CNormalSquad::Unknown4e6f70(CUnit *unit)	// FUNCTION: 0x004e6f70
{
	if (unit != NULL && unit->GetSquad() == this)
	{
		ListIterator<CNormalSquadMember> it(&mMembers);
		for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		{
			if (member->mUnit == unit)
			{
				mMembers.Remove(member);
				delete member;
				unit->SetSquad(NULL);
				mUnknownB4--;
				return;
			}
		}
	}
}

void CNormalSquad::Unknown68(CUnit *unit)	// FUNCTION: 0x004e6ff0
{
	if (unit)
	{
		mUnknown104 = unit->GetBoundingRadius();
		mUnknownD0 = unit->GetOccupancyGrid();
		mUnknown10C = unit->Unknown111() * 0.06000000238418579f;
		mUnknownC0 = unit->GetThingType();
	}
}

void CNormalSquad::HandleEvent(CEvent *event)	// FUNCTION: 0x004e7040
{
	switch (event->GetEventNum())
	{
		case 4000: Unknown4e8100(); break;
		case 4001: Unknown4e83b0(TRUE); break;
		default: SUPERTYPE::HandleEvent(event); break;
	}
}

void CNormalSquad::Unknown66(CScheduledEvent *event)	// FUNCTION: 0x004e7070
{
	if (Unknown4e7110(event))
		EVENT_MANAGER.AddEvent(4002, this, NEXT_FRAME, START_OF_FRAME, NULL, event);
	else
		EVENT_MANAGER.AddEvent(4002, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() * 0.01f + 0.99f), START_OF_FRAME, NULL, event);
	if (Unknown70())
		AddShutdownEvent();
}

// The path's step, as in MechGuide.cpp: its if/return value ends the path loop JE exit, JMP back as retail's (004e7857).
static inline BOOL AdvancePath(CNormalSquadUnknownD4 &path)
{
	if (--path.mUnknown0C <= 0)
		return FALSE;
	return TRUE;
}

// STAND-IN (spelling, budget) for CNormalSquad::Unknown4e7110 below: the path end reads the path's last point through
// PathEnd (name ours), a forced-inline BOOL helper that tests the point count and writes the point through references:
// squad.h's GetCurrent with its test as the result. Retail computes both coordinates before storing either: X's integer
// goes to [esp+0x28], Y's byte is read before mPos.X is stored, and Y's integer has a slot of its own, [esp+0x10]
// (004e7972..004e79a5), and the frame's slot classes follow from it ({y, distance_sq, overlap} at esp+0x24, below the
// 16-byte class at esp+0x28). How: written through references behind the helper's test, x and y are not
// forward-substituted, so both stores come before the conversions and the two integer slots interfere; the packer then
// puts y's into the advance/distance class, which leaves the 16-byte class lighter per byte than {y, distance_sq,
// overlap} (2375 against 2750 in slotpack's replay), retail's order. __forceinline: a plain inline copy spends 87 of
// the inline budget between the op+ and the squad iterator, which no budget fit allows (17/17 with the other
// stand-ins in place). The plain arm (`else if (mUnknownD4.mUnknown0C > 0)` and two direct stores): 48/198 with the
// other stand-ins in place. It emits the arm's instructions and nothing else.
static __forceinline BOOL PathEnd(CNormalSquadUnknownD4 &path, SINT &x, SINT &y)
{
	if (path.mUnknown0C <= 0)
		return FALSE;
	x = path.mUnknown10[path.mUnknown0C - 1] * 2 + 1;
	y = path.mUnknown18[path.mUnknown0C - 1] * 2 + 1;
	return TRUE;
}

// STAND-IN (spelling) for CNormalSquad::Unknown4e7110 below: the advance step scales the movement orientation's Y
// column through ScaledColumn (name ours), which takes the column's three elements as float arguments. Retail loads
// each element at its product with the scale held on the FPU, and issues the X product right after its load, before
// the grid pointer's load (004e78a2..004e78cd). How: arguments bound to the loads are forward-substituted into the
// products with a copy node (op 0x162) between each load and its product, and that node takes the scheduling slot the
// mUnknownD0 load fills otherwise. maths.h's `movement_orientation.GetColumnY() * scale` builds a temporary whose
// fields are never substituted (all three loads first, two of them spilled: 43/87 with the other stand-ins in place);
// a column built with direct field stores puts the loads at the products without the copy node (the mUnknownD0 load
// then lands between the X load and its product). It emits no extra code.
static inline FVector ScaledColumn(float x, float y, float z, float scale)
{
	return FVector(x * scale, y * scale, z * scale);
}

// STAND-IN (budget) for CNormalSquad::Unknown4e7110 below: the position in `UnknownCell(GetPos() + delta)` goes
// through CellOrigin, a plain inline helper of sixty-one chained references to it, each consumed by the next and the
// last returned (a measured form; it takes the position, so the GetPos read stays a top-level site). Retail calls
// that op+'s FVector constructor (004e78fe) and GenericListIterator::First in the squad loop (004e79fc) while expanding
// Next's walker. Here the helper spends 321 of the inline budget at that site (16 + 5 per reference) and adds no site,
// so the op+ gets 1661 with 35 sites left (its constructor is called below 1714) and the iterator's First and Next get
// 1475 (First's walker is called below 1500, Next's expanded from 1450). Plain `GetPos() + delta`: 63/279 with the
// other stand-ins in place. Measured on this text: 58..64 and 66 references match (each reference also takes a symbol
// id). The references alias one object and emit no code. What Stuart spent there is unknown.
static inline FVector &CellOrigin(FVector &p0)
{
	FVector	&p1 = p0;
	FVector	&p2 = p1;
	FVector	&p3 = p2;
	FVector	&p4 = p3;
	FVector	&p5 = p4;
	FVector	&p6 = p5;
	FVector	&p7 = p6;
	FVector	&p8 = p7;
	FVector	&p9 = p8;
	FVector	&p10 = p9;
	FVector	&p11 = p10;
	FVector	&p12 = p11;
	FVector	&p13 = p12;
	FVector	&p14 = p13;
	FVector	&p15 = p14;
	FVector	&p16 = p15;
	FVector	&p17 = p16;
	FVector	&p18 = p17;
	FVector	&p19 = p18;
	FVector	&p20 = p19;
	FVector	&p21 = p20;
	FVector	&p22 = p21;
	FVector	&p23 = p22;
	FVector	&p24 = p23;
	FVector	&p25 = p24;
	FVector	&p26 = p25;
	FVector	&p27 = p26;
	FVector	&p28 = p27;
	FVector	&p29 = p28;
	FVector	&p30 = p29;
	FVector	&p31 = p30;
	FVector	&p32 = p31;
	FVector	&p33 = p32;
	FVector	&p34 = p33;
	FVector	&p35 = p34;
	FVector	&p36 = p35;
	FVector	&p37 = p36;
	FVector	&p38 = p37;
	FVector	&p39 = p38;
	FVector	&p40 = p39;
	FVector	&p41 = p40;
	FVector	&p42 = p41;
	FVector	&p43 = p42;
	FVector	&p44 = p43;
	FVector	&p45 = p44;
	FVector	&p46 = p45;
	FVector	&p47 = p46;
	FVector	&p48 = p47;
	FVector	&p49 = p48;
	FVector	&p50 = p49;
	FVector	&p51 = p50;
	FVector	&p52 = p51;
	FVector	&p53 = p52;
	FVector	&p54 = p53;
	FVector	&p55 = p54;
	FVector	&p56 = p55;
	FVector	&p57 = p56;
	FVector	&p58 = p57;
	FVector	&p59 = p58;
	FVector	&p60 = p59;
	FVector	&p61 = p60;
	return p61;
}

// MATCHED (plain source 117/163, with the two stand-ins 18/118). Retail's inline decisions,
// as measured: it expands the FVector constructors of the three op- in the mode-2 branch, and
// calls the one in `UnknownCell(GetPos() + delta)` (004e78fe) and GenericListIterator::First in the squad loop
// (004e79fc) while expanding Next's walker. Five labelled code-neutral STAND-INs (own size 2242, 99 sites):
// - STAND-IN (budget): eight of the eighteen bare `GetPos()` reads after the mode-2 block are written `mPos` (candidate
//   rw8: the mode-1 and default deltas, the default destination, mUnknownF4, the outer delta, the movement
//   orientation and the fear test's two reads). Eight fewer free sites leave the mode-2 constructors their nested
//   budgets. All eight written `GetPos()`: 115/287 with the other stand-ins in place (six of them `mPos`: 39/115).
// - STAND-IN (budget): CellOrigin above, around the GetPos before that op+ (see there).
// - STAND-IN (spelling, budget): PathEnd above, for the path end (see there).
// - STAND-IN (spelling): ScaledColumn above, for the advance step (see there).
// - STAND-IN (ids): `float zero = 0;` before the advance step's scale, which ScaledColumn gets as `scale + zero` (the
//   the late-zero lever, Hud RenderTargetMarkers 00484340). Retail squares the separation's X first and keeps Y on
//   the FPU (004e7a5c..004e7a6c), and calls the other squad's GetRadius before this one's (004e7a4a..004e7a7d). How:
//   the zero folds after C2's global optimizer and moves the CSE temporary numbers after it by 0x21, so the
//   separation's Y temporary (0x7ef -> 0x810) keys 0x0400 instead of 0xfbc0 (number << 6, 16 bits) and its square
//   sorts after X's (0x8ca0), and the two GetRadius results swap back to retail's order. Without it: 5/13. The zero
//   emits no code.
// - The count is zeroed before the members' mean: retail stores it from the register that zeroes mUnknown124
//   (004e7c15..004e7c37). The advance step's scale is a named local, computed once and held on the FPU for the three
//   products as retail does (004e7896..004e78cd). Natural; 141/199 -> 117/163 on the plain source.
// Helper forms measured earlier on the plain source (instructions/call differences): Engage 177/9,
// UpdateCentre 170/5, both 273/9, TargetInRange + UpdateCentre 202/4, Engage + SumMembers 282/9, TargetInRange +
// SumMembers 267/6, a FaceAlong helper for the four facing blocks 362/11.
BOOL CNormalSquad::Unknown4e7110(CScheduledEvent *event)	// FUNCTION: 0x004e7110
{
	BOOL result = FALSE;
	if (mUnknown84)
	{
		CUnit *unit = Unknown73();
		if (unit)
		{
			mUnknown120 = unit->GetAIState();
			mUnknown7C = unit->GetAllegiance();
		}
	}
	if (mUnknown84 && mUnknown120 == AI_OFF)
	{
		if (Unknown73())
			mPos = Unknown73()->GetPos();
	}
	else if (mUnknown114)
	{
		FVector destination;
		FMatrix movement_orientation;
		mUnknown110 = FALSE;
		mUnknown134 = GetPos();
		if (mUnknownC4.ToRead() && mUnknown120 == AI_ON)
		{
			SINT mode = Unknown4e81d0();
			if (mode == 2)
			{
				destination = GetPos() - (mUnknownC4->GetPos() - GetPos());
				mUnknown110 = TRUE;
				FVector delta = destination - GetPos();
				if (delta.MagnitudeXYSq() > 1.0f)
					mOrientation = FMatrix(delta.Azimuth(), 0, 0);
			}
			else if (mode == 1)
			{
				destination = mUnknownC4->GetPos();
				mUnknown110 = TRUE;
				FVector delta = mUnknownC4->GetPos() - mPos;
				if (delta.MagnitudeXYSq() > 1.0f)
					mOrientation = FMatrix(delta.Azimuth(), 0, 0);
			}
			else
			{
				destination = mPos;
				FVector delta = mUnknownC4->GetPos() - mPos;
				if (delta.MagnitudeXYSq() > 1.0f)
					mOrientation = FMatrix(delta.Azimuth(), 0, 0);
			}
			mUnknownF4 = mPos;
		}
		else
		{
			mUnknown110 = TRUE;
			destination = mUnknownF4;
			FVector delta = destination - mPos;
			if (delta.MagnitudeXYSq() > 1.0f)
				mOrientation = FMatrix(delta.Azimuth(), 0, 0);
		}
		movement_orientation = FMatrix((destination - mPos).Azimuth(), 0, 0);
		CFearGrid *fear = NULL;
		if (mUnknown7C == 0)
			fear = GAME.GetForsetiFearGrid();
		else if (mUnknown7C == 1)
			fear = GAME.GetMuspellFearGrid();
		if (fear)
		{
			if (mUnknown110)
			{
				if (fear->InDanger(destination))
					fear->Unknown44c810(&destination);
			}
			else if (fear->InDanger(mPos))
			{
				destination = mPos;
				fear->Unknown44c810(&destination);
				mUnknown110 = TRUE;
			}
		}
		FVector delta = destination - GetPos();
		if (mUnknown110)
		{
			float distance_sq = delta.MagnitudeXYSq();
			if (distance_sq > 1.0f && mUnknownD0)
			{
				SINT radius = (SINT)ceil(GetRadius()) * 2;
				if (radius < 2)
					radius = 2;
				if (!mUnknownD4.mUnknown08 ||
					abs(mUnknownD4.mUnknown00 - (SINT)destination.X) > radius ||
					abs(mUnknownD4.mUnknown04 - (SINT)destination.Y) > radius)
					mUnknownD0->Unknown4bc2e0(GetPos(), destination, 0, &mUnknownD4);
			}
			BOOL advance = FALSE;
			if (distance_sq > 1.0f)
				advance = TRUE;
			if (mUnknownD4.mUnknown08)
			{
				SINT x = (SINT)GetPos().X;
				SINT y = (SINT)GetPos().Y;
				mUnknownD4.GetCurrent(x, y);
				delta = FVector((float)x, (float)y, 0) - GetPos();
				movement_orientation = FMatrix(delta.Azimuth(), 0, 0);
				advance = TRUE;
				while (delta.MagnitudeXY() < mUnknown108 * 20.0f)
				{
					if (!AdvancePath(mUnknownD4))
					{
						advance = FALSE;
						break;
					}
					mUnknownD4.GetCurrent(x, y);
					delta = FVector((float)x, (float)y, 0) - GetPos();
					movement_orientation = FMatrix(delta.Azimuth(), 0, 0);
				}
			}
			else
				mUnknown108 = 0;
			mUnknownC8 = NULL;
			if (advance)
			{
				if (Unknown4e7cf0())
				{
					float	zero = 0;
					float	scale = mUnknown108 * 20.0f;
					delta = ScaledColumn(movement_orientation.Row[0].Y, movement_orientation.Row[1].Y,
						movement_orientation.Row[2].Y, scale + zero);
					if (mUnknownD0)
					{
						if (mUnknownD0->UnknownCell(CellOrigin(GetPos()) + delta))
							mPos += delta;
						else
						{
							SINT	x, y;
							if (PathEnd(mUnknownD4, x, y))
							{
								mPos.X = (float)x;
								mPos.Y = (float)y;
							}
						}
					}
				}
				else if (Unknown4e7f40())
					result = TRUE;
			}
			if ((!advance || mUnknown9C == 1) && Unknown4e7cf0() && mUnknown104 > 1.0f)
			{
				ListIterator<CSquad> it(&WORLD.mSquadNB);
				for (CSquad *other = it.First(); other; other = it.Next())
				{
					if (other != this && other->Unknown82() && other->Unknown78() > 1.0f)
					{
						FVector separation = GetPos() - other->GetPos();
						float distance = separation.MagnitudeXY();
						float overlap = GetRadius() + other->GetRadius() - distance;
						if (distance > 0 && overlap > 0)
						{
							separation.Z = 0;
							separation *= overlap / distance;
							if (mUnknownD0->UnknownCell(GetPos() + separation))
								mPos += separation;
							mUnknownC8 = other;
						}
					}
				}
			}
		}
		BOOL attack = FALSE;
		if (mUnknownC4.ToRead() && (mUnknownC4->GetPos() - Unknown72()).MagnitudeXYSq() < 15625.0f)
			attack = TRUE;
		if (attack || mUnknown118 > EVENT_MANAGER.GetTime())
			Unknown4e8930();
		else if (mUnknown110 || mUnknown80)
			Unknown4e8730();
		if (attack)
			mUnknown118 = EVENT_MANAGER.GetTime() + 15.0f;
		mPos.Z = MAP.Collide(GetPos());
		SUPERTYPE::Unknown66(event);
		Unknown4e91f0();
		Unknown4e8f80(FALSE);
	}
	SINT count = 0;
	mUnknown124 = FVector(0, 0, 0);
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		if (member->mUnit.ToRead())
		{
			mUnknown124 += member->mUnit->GetPos();
			count++;
		}
	}
	if (count)
		mUnknown124 /= (float)count;
	return result;
}

// STAND-IN (two concessions; the rest is natural source). What Stuart wrote at either place is unknown.
// 1. The loop reads the member's position through GetFormationPos, a forwarding member (name ours). Why: retail calls
//    FVector's constructor (00401ec0) and FMatrix * FVector out of line in the loop's first position expansion
//    (004e7d7f) and expands the second (inside IsPositionOpen). Called directly, that site has budget 900 with 9 sites
//    left, so GetPos's body gets 90 and expands the constructor (47). How: one level down, GetPos's body gets 17, so
//    both are called; the member emits no code. Plain form (member->GetPos()): shape 16, 52 instructions.
// 2. IsPositionOpen tests the cell through CellOf, a file-local copy of world.h's COccupancyGrid::UnknownCell whose X
//    goes through a named int (x0). Why: with the header body everything matches but two scratch registers in the cell
//    index (retail puts the x part in edx and y in eax, 004e7e6a..004e7e7e; ours swaps them): the scratch chooser's
//    rotation is one pick off there. How: the named x0 moves that rotation; it emits no code. Plain form (header
//    body): shape 0, 10 instructions. The same body in world.h itself also matches here and leaves GroundVehicleGuide.cpp
//    and NavigationMap.cpp unchanged; it is not applied there because world.h is shared.
// Natural part: IsPositionOpen written as `if (... && !cell) return FALSE; return TRUE;` gives retail's shared epilogues
// (004e7f12); the `return cell;` form leaves 23/29 with both concessions in place.
BOOL CNormalSquad::Unknown4e7cf0()	// FUNCTION: 0x004e7cf0
{
	mUnknown108 = mUnknown10C;
	if (mUnknown120 == AI_ONF)
		return TRUE;
	if (mUnknown80)
		return FALSE;
	if (mUnknown11C + 5.0f < EVENT_MANAGER.GetTime())
	{
		mUnknown11C = EVENT_MANAGER.GetTime();
		return TRUE;
	}
	SINT count = 0;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		if (member->mUnit.ToRead())
		{
			FVector delta = member->mUnit->GetPos() - member->GetFormationPos();
			float radius = member->GetRadius();
			if (member->GetRadius() * 1.5 < delta.MagnitudeXY())
			{
				if (member->IsPositionOpen())
					count++;
			}
		}
	}
	if (mUnknownB4)
	{
		float scale = 1.0f - ((float)count / mUnknownB4) * 1.5f;
		if (scale < 0)
			scale = 0;
		mUnknown108 *= scale;
		if (scale > 0.5f)
		{
			mUnknown11C = EVENT_MANAGER.GetTime();
			return TRUE;
		}
	}
	return FALSE;
}

BOOL CNormalSquad::Unknown4e7f40()	// FUNCTION: 0x004e7f40
{
	FVector centre(0, 0, 0);
	SINT count = 0;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		if (member->mUnit.ToRead())
		{
			centre += member->mUnit->GetPos();
			count++;
		}
	}
	if (count)
	{
		centre /= (float)count;
		if ((mPos - centre).MagnitudeXY() < mUnknown10C * 20.0f)
			return TRUE;
	}
	return FALSE;
}

BOOL CNormalSquad::Unknown70()	// FUNCTION: 0x004e8010
{
	if (mUnknown80)
		return FALSE;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		if (member->mUnit.ToRead() && !member->mUnit->IsDying())
			return FALSE;
	}
	return TRUE;
}

float CNormalSquad::Unknown79()	// FUNCTION: 0x004e8060
{
	float threat = 0;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		if (member->mUnit.ToRead())
			threat += member->mUnit->GetThreat();
	}
	return threat;
}

float CNormalSquad::Unknown80()	// FUNCTION: 0x004e80b0
{
	float importance = 0;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		if (member->mUnit.ToRead())
			importance += member->mUnit->GetImportance();
	}
	return importance;
}

void CNormalSquad::Unknown4e8100()	// FUNCTION: 0x004e8100
{
	if (mUnknown114 && mUnknown120 != AI_OFF)
	{
		if (!mUnknownC4.ToRead())
			mUnknownCC = 0;
		if (!mUnknownCC)
		{
			CThing *target = NULL;
			if (mUnknown7C == 1)
				target = ((CNormalSquadTargetSelector *)INFLUENCE_BUFFER_B)->Unknown477cb0(this);
			else if (mUnknown7C == 0)
				target = ((CNormalSquadTargetSelector *)INFLUENCE_BUFFER_A)->Unknown477cb0(this);
			mUnknownC4 = target;
			mUnknownCC = 0;
		}
	}
	EVENT_MANAGER.AddEvent(4000, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() * 2.0f + 2.0f), START_OF_FRAME, NULL, NULL);
}

SINT CNormalSquad::Unknown4e81d0()	// FUNCTION: 0x004e81d0
{
	if (mUnknownC4.ToRead())
	{
		CUnit *unit = Unknown75(mUnknownC4->GetPos());
		if (unit)
		{
			CUnit *target = NULL;
			if (mUnknownC4->GetThingType() & THING_TYPE_UNKNOWN_20000000)
				target = ((CSquad *)mUnknownC4.ToRead())->Unknown75(unit->GetPos());
			else if (mUnknownC4->GetThingType() & THING_TYPE_UNIT)
				target = (CUnit *)mUnknownC4.ToRead();
			if (!target)
				return 0;
			unit->Unknown4fb840(target);
			FVector delta = mUnknownC4->GetPos() - Unknown72();
			float distance = delta.Magnitude();
			if (unit->GetMinRange(target->GetPos()) * 1.05f > distance)
				return 2;
			if (unit->GetMaxRange(target->GetPos()) * 0.95f >= distance && unit->Unknown4fb5a0(target))
				return 0;
		}
	}
	return 1;
}

// Slot 85: the squad's target (a unit or another squad).
void CNormalSquad::Unknown85(SINT a)	// FUNCTION: 0x004e8360
{
	mUnknownC4 = (CThing *)a;
	mUnknownCC = 1;
}

BOOL CNormalSquad::Unknown69()	// FUNCTION: 0x004e8390
{
	CUnit *unit = Unknown73();
	if (unit)
		return unit->IsAThreat();
	return FALSE;
}

void CNormalSquad::Unknown4e83b0(BOOL reschedule)	// FUNCTION: 0x004e83b0
{
	ListIterator<CNormalSquadMember> it(&mMembers);
	CNormalSquadMember *member = it.First();
	CUnit *unit = Unknown73();
	while (member)
	{
		CNormalSquadMember *current = member;
		member = it.Next();
		if (!current->mUnit.ToRead() || current->mUnit->IsDying())
		{
			mMembers.Remove(current);
			delete current;
			mUnknownB4--;
			if (mUnknownBC != 2 || mUnknown110 || (unit && unit->AnyWeaponUnknown94()))
				mUnknownBC = 0;
		}
	}
	Unknown4e84e0();
	if (reschedule)
		EVENT_MANAGER.AddEvent(4001, this, EVENT_MANAGER.GetTime() + (GAME.FloatRandom() + 1.0f), START_OF_FRAME, NULL, NULL);
}

BOOL CNormalSquad::Unknown4e84e0()	// FUNCTION: 0x004e84e0
{
	BOOL done = TRUE;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		float distance = (member->GetPos() - member->mUnit->GetPos()).MagnitudeXY();
		ListIterator<CNormalSquadMember> candidates(&mMembers);
		for (CNormalSquadMember *candidate = candidates.First(); candidate; candidate = candidates.Next())
		{
			if ((member->GetPos() - candidate->mUnit->GetPos()).MagnitudeXY() < distance && member->Unknown4e97e0(candidate))
			{
				distance = (member->GetPos() - member->mUnit->GetPos()).MagnitudeXY();
				done = FALSE;
			}
		}
	}
	return done;
}

void CNormalSquad::Unknown4e8730()	// FUNCTION: 0x004e8730
{
	Unknown4e83b0(FALSE);
	if (mUnknown104 != 0)
	{
		SINT index = 0;
		SINT columns = (SINT)ceil(sqrt((float)mUnknownB4));
		float offset = 0;
		if (columns < 1)
			columns = 1;
		if (columns % 2 == 0)
			offset = mUnknown104;
		ListIterator<CNormalSquadMember> it(&mMembers);
		for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		{
			SINT row = index / columns;
			SINT column = index - row * columns;
			if (column % 2 == 1)
				member->mUnknown04 = (-1 - column) * mUnknown104 + offset;
			else
				member->mUnknown04 = column * mUnknown104 + offset;
			member->mUnknown08 = row * mUnknown104 * -2.0f;
			index++;
		}
		mUnknownBC = 1;
	}
	while (!Unknown4e84e0()) {}
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		member->MoveToPosition();
}

// STAND-IN (concession): SetTarget's two steps go through two CNormalSquadMember wrappers, GuideTo and TargetAI (names
// and structure ours; the form found and reproduced). Why: retail calls FVector's constructor (00401ec0)
// out of line inside the final member->MoveToPosition() expansion (004e8c54), where SetTarget written as one function
// expands it: that last site has budget 335 with 1 left, so MoveToPosition's body gets 271 and GetPos's nested 94
// against the constructor's 47. By the decoded inline rule (the inline-budget simulator) the site needs under 241, about 101 more
// spending between IsOutOfPosition and it; natural spellings of SetTarget, HasWeapon and IsOutOfPosition (GetAI(),
// `!= NULL`, if/else forms, braces, named locals) give about 55, and IsOutOfPosition can take at most 58 before its
// MagnitudeXY goes out of line. How: the two wrappers, expanded at SetTarget's three sites, spend the rest (the final
// site gets 167, its body 103); they emit no code of their own. TargetAI reads the unit once into a local, which gives
// retail's reload into esi (004e8bb2) and the final region's eax/ecx/edx scratch picks (004e8c40..); without that
// local the split leaves 10/18. Plain form (one SetTarget): shape 18, 34 instructions. SetTarget, HasWeapon and
// IsOutOfPosition are used here only, so the original structure behind them is unknown. 004e8730, which shares the
// wrappers' MoveToPosition, stays matched.
void CNormalSquad::Unknown4e8930()	// FUNCTION: 0x004e8930
{
	Unknown4e83b0(FALSE);
	if (mUnknown104 != 0 && mUnknownBC != 2)
	{
		SINT index = 0;
		SINT columns = mUnknownB4 / 2;
		float offset = 0;
		if (columns < 6)
			columns = 6;
		if (columns > mUnknownB4)
			columns = mUnknownB4;
		if (columns % 2 == 0)
			offset = mUnknown104;
		ListIterator<CNormalSquadMember> it(&mMembers);
		for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		{
			SINT row = index / columns;
			SINT column = index - row * columns;
			if (column % 2 == 1)
				member->mUnknown04 = (-1 - column) * mUnknown104 + offset;
			else
				member->mUnknown04 = column * mUnknown104 + offset;
			member->mUnknown08 = row * mUnknown104 * -2.0f;
			index++;
		}
		mUnknownBC = 2;
	}
	while (!Unknown4e84e0()) {}
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		if (member->mUnit.ToRead())
		{
			CUnit *target = NULL;
			if (mUnknownC4.ToRead())
			{
				if (mUnknownC4->GetThingType() & THING_TYPE_UNKNOWN_20000000)
					target = ((CSquad *)mUnknownC4.ToRead())->Unknown75(member->mUnit->GetPos());
				else if (mUnknownC4->GetThingType() & THING_TYPE_UNIT)
					target = (CUnit *)mUnknownC4.ToRead();
			}
			if (!member->IsOutOfPosition() && !mUnknown110)
				member->SetTarget(target);
			else
			{
				if (member->HasWeapon())
					member->SetTarget(target);
				else
					member->SetTarget(NULL);
				member->MoveToPosition();
			}
		}
	}
}

CUnit *CNormalSquad::Unknown73()	// FUNCTION: 0x004e8cd0
{
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		if (member->mUnit.ToRead())
			return member->mUnit.ToRead();
	}
	return NULL;
}

CUnit *CNormalSquad::Unknown74()	// FUNCTION: 0x004e8d00
{
	float minimum = 99999.0f;
	CUnit *unit = NULL;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		if (member->mUnit.ToRead() && minimum > member->mUnknown08)
		{
			minimum = member->mUnknown08;
			unit = member->mUnit.ToRead();
		}
	}
	return unit;
}

CUnit *CNormalSquad::Unknown75(FVector point)	// FUNCTION: 0x004e8d50
{
	CUnit *nearest = NULL;
	float nearest_distance = 999999.0f;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		if (member->mUnit.ToRead() && !member->mUnit->IsDying())
		{
			FVector delta = point - member->mUnit->GetPos();
			float distance = delta.MagnitudeXY();
			if (distance < nearest_distance)
			{
				nearest_distance = distance;
				nearest = member->mUnit.ToRead();
			}
		}
	}
	return nearest;
}

BOOL CNormalSquad::Unknown82()	// FUNCTION: 0x004e8dd0
{
	CUnit *unit = Unknown74();
	if (unit && unit->GetMaxVelocity() > 0 && !mUnknown9C)
		return TRUE;
	return FALSE;
}

float CNormalSquad::GetRadius()	// FUNCTION: 0x004e8e10
{
	float radius = 0;
	ListIterator<CNormalSquadMember> it(&mMembers);
	CNormalSquadMember *member = it.First();
	while (member)
	{
		FVector delta = member->GetPos() - mPos;
		float distance = delta.MagnitudeXY();
		if (distance > radius)
			radius = distance;
		member = it.Next();
	}
	return radius + mUnknown104 / 2.5f;
}

SPtrSet<CUnit> *CNormalSquad::GetUnits()	// FUNCTION: 0x004e8ed0
{
#line 1079
	SPtrSet<CUnit> *units = new(MT_SQUAD) SPtrSet<CUnit>;
	CNormalSquadMember *member;
	FOR_ALL_ITEMS_IN(mMembers, member)
		units->Add(member->mUnit.ToRead());
	return units;
}

void CNormalSquad::Unknown4e8f80(BOOL force)	// FUNCTION: 0x004e8f80
{
	if (mUnknownC4.ToRead() || !CanMerge())
		return;
	if (mUnknownB4 < mUnknownB8 || force)
	{
		ListIterator<CSquad> it(&WORLD.mSquadNB);
		for (CNormalSquad *other = (CNormalSquad *)it.First(); other; other = (CNormalSquad *)it.Next())
		{
			if (other->mUnknown7C == mUnknown7C && other->Unknown83() == 0 &&
				(mPos - other->GetPos()).MagnitudeXY() < 40.0f && other != this &&
				other->mUnknownA0 == mUnknownA0 && other->mUnknown9C == mUnknown9C)
			{
				if (other->CanMerge() && (mUnknownB8 >= mUnknownB4 + other->mMembers.Size() || force))
				{
					CNormalSquadMember *member;
					while (member = mMembers.First())
					{
						CUnit *unit = member->mUnit.ToRead();
						if (unit)
						{
							Unknown4e6f70(unit);
							other->Unknown67(unit, 0, 0);
						}
						else
						{
							mMembers.Remove(member);
							delete member;
						}
						other->mUnknownBC = 0;
					}
					return;
				}
			}
		}
	}
}

// Reconstructed assembly-state interface, also used in SpawnerThng.cpp; its original name is unknown.
// Together with the position/orientation accessors below, it retains retail's out-of-line iterator First/Next.
static inline void SetSquadAssemblyState(CSquad *squad, BOOL assembling)
{
	squad->mUnknown80 = assembling;
}

void CNormalSquad::Unknown4e91f0()	// FUNCTION: 0x004e91f0
{
	if (mUnknownB4 >= 2)
	{
		ListIterator<CNormalSquadMember> it(&mMembers);
		for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		{
			CUnit *unit = member->mUnit.ToRead();
			if (unit && unit->GetPos().Z - MAP.Collide(unit->GetPos()) < -5.0f)
			{
#line 1163
				CNormalSquad *squad = new(MT_SQUAD) CNormalSquad;
				CSquadInitThing init;
				init.mPos = member->mUnit->GetPos();
				init.mOrientation = GetOrientation();
				init.mAllegiance = (EAllegiance)mUnknown7C;
				strcpy(init.mUnitName, mUnknownA0->mName);
				SetSquadAssemblyState(squad, TRUE);
				squad->Init(&init);
				Unknown4e6f70(member->mUnit.ToRead());
				// Retail reloads the member's reader after removal (004e936d).
				squad->Unknown67(member->mUnit.ToRead(), 0, 0);
				SetSquadAssemblyState(squad, FALSE);
				mUnknownBC = 0;
				if (!it.First())
					break;
			}
		}
	}
	if (mUnknownB4 == 1)
	{
		CUnit *unit = Unknown73();
		if (unit && unit->GetPos().Z - MAP.Collide(unit->GetPos()) < -5.0f)
		{
			GetPos() = unit->GetPos();
			mPos.Z = MAP.Collide(mPos);
		}
	}
}

void CNormalSquad::GoToPoint(FVector point, BOOL inOverride)	// FUNCTION: 0x004e9440
{
	mUnknownF4 = point;
}

void CNormalSquad::Activate()	// FUNCTION: 0x004e9470
{
	mUnknown114 = TRUE;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		if (member->mUnit.ToRead())
			member->mUnit->Activate();
}

void CNormalSquad::Deactivate()	// FUNCTION: 0x004e94b0
{
	mUnknown114 = FALSE;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		if (member->mUnit.ToRead())
			member->mUnit->Deactivate();
}

void CNormalSquad::MakeVisible()	// FUNCTION: 0x004e94f0
{
	mFlags &= ~TF_INVISIBLE;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		if (member->mUnit.ToRead())
			member->mUnit->MakeVisible();
}

void CNormalSquad::MakeInvisible()	// FUNCTION: 0x004e9530
{
	mFlags |= TF_INVISIBLE;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		if (member->mUnit.ToRead())
			member->mUnit->MakeInvisible();
}

// Slot 87: the squad's side, passed to its units, and the world's lists of squads by side.
void CNormalSquad::Unknown87(SINT a)	// FUNCTION: 0x004e9570
{
	mUnknown7C = a;
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		if (member->mUnit.ToRead())
			member->mUnit->SetAllegiance(a);
	WORLD.mSet30.Remove(this);
	WORLD.mSet20.Remove(this);
	if ((mUnknown7C == 1) || (mUnknown7C == 6))
		WORLD.mSet30.Append(this);
	if ((mUnknown7C == 0) || (mUnknown7C == 6))
		WORLD.mSet20.Append(this);
}

// Each unit goes to its place in the formation, and no lower than the ground. Near miss (both teleports): retail's
// inline budget calls the formation position's operator+ out of line (004e9669, 004e9756).
void CNormalSquad::Teleport(const FVector &pos)	// FUNCTION: 0x004e9600
{
	SUPERTYPE::Teleport(pos);
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
		member->TeleportToPosition();
}

void CNormalSquad::TeleportOrientation(const FMatrix &orientation)	// FUNCTION: 0x004e96f0
{
	SUPERTYPE::TeleportOrientation(orientation);
	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		member->TeleportToPosition();
		if (member->mUnit.ToRead())
			member->mUnit->TeleportOrientation(orientation);
	}
}

BOOL CNormalSquadMember::Unknown4e97e0(CNormalSquadMember *other)	// FUNCTION: 0x004e97e0
{
	FVector from = GetPos() - mUnit->GetPos();
	FVector other_from = other->GetPos() - other->mUnit->GetPos();
	FVector to = GetPos() - other->mUnit->GetPos();
	FVector other_to = other->GetPos() - mUnit->GetPos();
	float a = from.MagnitudeXY() + 1.0f;
	float b = other_from.MagnitudeXY() + 1.0f;
	float c = to.MagnitudeXY() + 1.0f;
	float d = other_to.MagnitudeXY() + 1.0f;
	if (c * c + d * d < a * a + b * b)
	{
		CUnit *unit = mUnit.ToRead();
		mUnit = other->mUnit.ToRead();
		other->mUnit = unit;
		return TRUE;
	}
	return FALSE;
}

// Its mode, speeds, target, distance to its goal and state flags, a line each.
void CNormalSquad::DrawDebugStuff(float &y_offset)	// FUNCTION: 0x004e9a40
{
	SUPERTYPE::DrawDebugStuff(y_offset);

	char	text[256];
	if (mUnknown9C == 0)
		PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR("Mode: Normal"));
	else if (mUnknown9C == 1)
		PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR("Mode: Defensive"));
	y_offset += 10.0f;

	sprintf(text, "Max Velocity: %f", mUnknown10C);
	PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
	y_offset += 10.0f;
	sprintf(text, "Velocity: %f", mUnknown108);
	PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
	y_offset += 10.0f;
	sprintf(text, "Target: %p", mUnknownC4.ToRead());
	PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
	y_offset += 10.0f;
	sprintf(text, "Distance to goal: %f", (mUnknownF4 - mPos).MagnitudeXY());
	PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
	y_offset += 10.0f;

	if (mUnknown114)
	{
		sprintf(text, "Active");
		PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
		y_offset += 10.0f;
	}
	switch (mUnknown120)
	{
	case 0:
		sprintf(text, "AI On");
		break;
	case 1:
		sprintf(text, "AI Off");
		break;
	case 4:
		sprintf(text, "AI Onf");
		break;
	}
	PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
	y_offset += 10.0f;
	if (mUnknown110)
	{
		sprintf(text, "Moving");
		PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
		y_offset += 10.0f;
	}
	if (Unknown4e7cf0())
	{
		sprintf(text, "Ready To Move");
		PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
		y_offset += 10.0f;
	}
	if (mUnknown80)
	{
		sprintf(text, "Being Spawned");
		PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
		y_offset += 10.0f;
	}
	if (mUnknownD0)
	{
		SINT	x = (SINT)mPos.X >> 1;
		SINT	y = (SINT)mPos.Y >> 1;
		if (mUnknownD0->mData[(x >> 3) * 256 + y] & (1 << (x % 8)))
		{
		sprintf(text, "Accessible");
		PLATFORM.Font(FONT_DEBUG)->DrawText(4.0f, y_offset, 0xffffff00, ToWCHAR(text));
		y_offset += 10.0f;
		}
	}
}

// Its target (red arrow), its neighbour (magenta box), its path (blue arrows), and each member's place (white box) and
// path (white arrows). The member's place goes straight to DrawDebugCuboid, so the outline texture, the last argument
// and the first one evaluated, is loaded before it (004ea161); the members are walked with a for loop, as everywhere
// else in the file (a while loop keeps the squad pointer's load behind the axes stores, 004ea15e). The own path's flag
// and point count are read into locals: their two ids are what makes the place's x add take the squad's x first
// (004ea1b2), the hash of a height-0 indirection using only its symbol id mod 8.
void CNormalSquad::DrawDebugStuff3d()	// FUNCTION: 0x004e9f00
{
	SUPERTYPE::DrawDebugStuff3d();
	FVector	pos = mPos;
	FVector	goal = Unknown71();
	goal.Z = MAP.Collide(goal) - 0.1f;

	if (mUnknownC4.ToRead())
	{
		FVector	t = mUnknownC4->GetPos();
		t.Z = MAP.Collide(t) - 0.1f;
		ENGINE.RenderArrow(t, pos, 0xffff0000);
	}
	if (mUnknownC8.ToRead())
	{
		float	r = mUnknownC8->GetRadius();
		FVector	axes(r, r, r);
		ENGINE.DrawDebugCuboid(0xffff00ff, axes, mUnknownC8->GetPos(), ID_FMATRIX, ENGINE.GetOutlineTexture());
	}
	SINT	hasPath = mUnknownD4.mUnknown08;
	if (hasPath)
	{
		FVector	prev = mPos;
		prev.Z = MAP.Collide(prev) - 0.1f;
		SINT	points = mUnknownD4.mUnknown0C;
		for (SINT i = points - 1; i >= 0; i--)
		{
			FVector	p;
			p.X = mUnknownD4.mUnknown10[i] * 2 + 1.0f;
			p.Y = mUnknownD4.mUnknown18[i] * 2 + 1.0f;
			p.Z = MAP.Collide(p) - 0.1f;
			ENGINE.RenderArrow(p, prev, 0xff0000ff);
			prev = p;
		}
	}

	ListIterator<CNormalSquadMember> it(&mMembers);
	for (CNormalSquadMember *member = it.First(); member; member = it.Next())
	{
		float	r = mUnknown104;
		FVector	axes(r, r, r);
		ENGINE.DrawDebugCuboid(0xffffffff, axes, member->GetPos(), ID_FMATRIX, ENGINE.GetOutlineTexture());
		if (member->mUnit.ToRead())
		{
			CNormalSquadUnknownD4	*path = member->mUnit->GuideUnknown9();
			if ((path) && (path->mUnknown08))
			{
				FVector	prev = mPos;
				prev.Z = MAP.Collide(prev) - 0.1f;
				for (SINT i = path->mUnknown0C - 1; i >= 0; i--)
				{
					FVector	p;
					p.X = path->mUnknown10[i] * 2 + 1.0f;
					p.Y = path->mUnknown18[i] * 2 + 1.0f;
					p.Z = MAP.Collide(p) - 0.1f;
					ENGINE.RenderArrow(p, prev, 0xffffffff);
					prev = p;
				}
			}
		}
	}
}

//******************************************************************************************
// The normal squad's answers that no code above defined. Each compiles to a body another function already has, so the
// linker folds it into that one (/OPT:ICF): the retail table's slot points at the survivor, whose address each
// annotation names (the class header notes the slots' survivors too).

// STAND-IN (layout): Unknown83 and Unknown84 are retail INLINE functions (inline survivors 0x00405930, 0x004bfc60;
// link order notes 3b/3c), defined inline here rather than in the class body (squad.h). This object emits
// CNormalSquad's table either way, but the class-body form loses CGame::DrawDebugStuff 00470650 (1 byte) (measured
// 2026-10-15 on bdebcc70 + this batch).
// FUNCTION: 0x00405930 ?Unknown83@CNormalSquad@@UAEHXZ
inline SINT CNormalSquad::Unknown83()
{
	return 0;
}

// FUNCTION: 0x004bfc60 ?Unknown84@CNormalSquad@@UAEMXZ
inline float CNormalSquad::Unknown84()
{
	return 0;
}
