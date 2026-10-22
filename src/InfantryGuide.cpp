// InfantryGuide.cpp: reconstructed from InfantryGuide.obj (0048a2d0..0048aebc); Stuart's release has no copy. An
// infantryman's guide follows a path over its occupancy grid as the ground vehicle's does (GroundVehicleGuide.obj),
// and every quarter to half second looks for what it is touching: an enemy vehicle or mech first, else infantry.

#include "common.h"
#include "array.h"
#include "unit.h"
#include "squad.h"
#include "mapwho.h"
#include "mapwhoentry.h"
#include "eventmanager.h"
#include "game.h"
#include "activereader.h"
#include "UnitAI.h"
#include "world.h"
#include "DebugLog.h"
#define INFANTRYGUIDE_FULL
#include "Guide.h"

// FUNCTION: 0x0048a2d0 init:ZERO_FVECTOR
// FUNCTION: 0x0048a2f0 init:ID_FMATRIX

//******************************************************************************************
CInfantryGuide::CInfantryGuide(CUnit *unit) : CGuide(unit)	// FUNCTION: 0x0048a3c0
{
	mGrid = unit->GetOccupancyGrid();
	mTarget = NULL;
	EVENT_MANAGER.AddEvent(2000, this, NEXT_FRAME, START_OF_FRAME, NULL, NULL);
}

static inline BOOL PathDestinationChanged(const CNormalSquadUnknownD4 &path, FVector destination)
{
	return (abs(path.mUnknown00 - (SINT)destination.X) > 1) || (abs(path.mUnknown04 - (SINT)destination.Y) > 1);
}

// The path's step, as in MechGuide.cpp: its if/return value ends the path loop JE exit, JMP back as retail's (0048aa04).
static inline BOOL AdvancePath(CNormalSquadUnknownD4 &path)
{
	if (--path.mUnknown0C <= 0)
		return FALSE;
	return TRUE;
}

//******************************************************************************************
// It steps away from what it touches, else follows its path, turns, and walks on once facing within 0.2.
// Retail calls FVector's constructor out of line in the first two operator-s (0048a5b4, 0048a630), by the inline-budget
// rule (README). The walk publishes ori * forward, as the mech guide's does: the product starts the first multiply
// before it adds 0x14c to the unit (0048ac02). The product is a site more than three scalar products, so one accessor
// after the path's first operator- (0048a8e2) reads mUnit directly, or that operator- calls FVector's constructor
// out of line. Which accessor is not fixed by the bytes: each of five tried gives the same match; it is speed's.
void CInfantryGuide::Unknown3()	// FUNCTION: 0x0048a570
{
	if (!GetUnit()->IsDying())
	{
		float	current = GetUnit()->mCurrentOrientation.mYaw;
		FVector	d = mDestination - GetUnit()->GetPos();
		BOOL	pushed = FALSE;
		CThing	*target;
		if ((GetUnit()->GetAI()->mAIMode != 2) && (target = mTarget.ToRead()))
		{
			if (!(target->GetPos() == GetUnit()->GetPos()))
			{
				d = GetUnit()->GetPos() - target->GetPos();
				d.Normalise();
				d *= 10.0f;
			}
			pushed = TRUE;
		}
		if (d.MagnitudeXYSq() < 0.01f)
		{
			GetUnit()->mUnknown14C = FVector(0, 0, 0);
			return;
		}

		float	yaw;
		switch (mMode)
		{
		case 1:
		case 3:
			yaw = d.Azimuth();
			break;
		case 2:
			yaw = d.Azimuth() - PI;
			break;
		case 0:
			yaw = current;
			break;
		default:
			LOG.AddMessage("ERROR : Unknown Infantry guide state!");
			break;
		}
		GetUnit()->mUnknown120.mYaw = yaw;

		CUnit	*unit = mUnit;
		if ((unit->GetAI()->mAIMode != 2) && (!pushed))
		{
			if ((d.MagnitudeXYSq() > 1.0f) && (mGrid))
			{
				if ((!mPath.mUnknown08) || PathDestinationChanged(mPath, mDestination))
					mGrid->Unknown4bc2e0(unit->GetPos(), mDestination, 1, &mPath);
			}
			if (mPath.mUnknown08)
			{
				SINT	x = (SINT)GetUnit()->GetPos().X;
				SINT	y = (SINT)GetUnit()->GetPos().Y;
				mPath.GetCurrent(x, y);
				d = FVector((float)x, (float)y, 0) - GetUnit()->GetPos();
				yaw = d.Azimuth();
				while (d.MagnitudeXYSq() < 0.5f)
				{
					if (!AdvancePath(mPath))
					{
						d = mDestination - GetUnit()->GetPos();
						yaw = d.Azimuth();
						break;
					}
					mPath.GetCurrent(x, y);
					d = FVector((float)x, (float)y, 0) - GetUnit()->GetPos();
					yaw = d.Azimuth();
				}
				GetUnit()->mUnknown120.mYaw = yaw;
			}
		}

		float	distanceSq = d.MagnitudeXYSq();
		if (distanceSq < 0.01f)
		{
			mMode = 0;
			GetUnit()->mUnknown14C = FVector(0, 0, 0);
			return;
		}
		if (((float)fabs(AngleDifference(yaw, current)) > 0.2f) && (distanceSq < 0.1f))
		{
			GetUnit()->mUnknown14C = FVector(0, 0, 0);
			return;
		}
		if ((mMode == 3) && (!pushed))
		{
			GetUnit()->mUnknown14C = FVector(0, 0, 0);
			return;
		}

		float	speed = mUnit->Unknown111() / 5.0f;
		FVector	forward(0, speed, 0);
		FMatrix	&ori = GetUnit()->GetOrientation();
		GetUnit()->mUnknown14C = ori * forward;
	}
	else
		GetUnit()->Stop();
}

// The implicit destructor.
// FUNCTION: 0x0048a4c0 ??_GCInfantryGuide@@UAEPAXI@Z
// FUNCTION: 0x0048a4e0 ??1CInfantryGuide@@UAE@XZ

//******************************************************************************************
void CInfantryGuide::HandleEvent(CEvent *event)	// FUNCTION: 0x0048ac70
{
	if (event->GetEventNum() == 2000)
	{
		FindTarget();
		EVENT_MANAGER.AddEvent(2000, this, EVENT_MANAGER.GetTime() + GAME.FloatRandom() * 0.25f + 0.25f, START_OF_FRAME, NULL, (CScheduledEvent *)event);
	}
}

//******************************************************************************************
// Near miss (x87 order): in both gap sums retail loads d.Z and d.Y once and squares stack copies, keeping the radius
// on the stack for r*r (0048ad9d, 0048ae4b); this build squares from memory. d * d and written-out orders are no closer.
void CInfantryGuide::FindTarget()	// FUNCTION: 0x0048ace0
{
	mTarget = NULL;
	CMapWhoEntry	*e = MAP_WHO.GetFirstEntryWithinRadius(GetUnit()->GetPos(), 1.0f);
	BOOL	found = FALSE;
	float	best = 999999.0f;
	for (; e; e = MAP_WHO.GetNextEntryWithinRadius())
	{
		CThing	*thing = e->GetThing();
		if ((!thing) || (thing == GetUnit()))
			continue;
		if ((thing->GetThingType() & (THING_TYPE_VEHICLE | THING_TYPE_MECH)) && (((CUnit *)thing)->mAllegiance != GetUnit()->mAllegiance))
		{
			FVector	d = thing->GetPos() - GetUnit()->GetPos();
			float	r = thing->GetRadius();
			float	gap = d.MagnitudeSq();
			gap -= r * r;
			if ((gap < best) && (gap < 1.0f))
			{
				mTarget = thing;
				found = TRUE;
				best = gap;
			}
		}
		else if ((!found) && (thing->GetThingType() & THING_TYPE_INFANTRY) && (!thing->IsDying()))
		{
			FVector	d = thing->GetPos() - GetUnit()->GetPos();
			float	r = thing->GetRadius();
			float	gap = d.MagnitudeSq();
			gap -= r * r;
			if ((gap < best) && (gap < 0.0009f))
			{
				mTarget = thing;
				best = gap;
			}
		}
	}
}
