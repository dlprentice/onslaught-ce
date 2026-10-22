// MechGuide.cpp: reconstructed from MechGuide.obj (004a0930..004a1389); Stuart's release has no copy. A mech's guide
// follows a path over its occupancy grid as the infantry guide does (InfantryGuide.obj), and every half second or
// less looks for the nearest vehicle or mech it is all but touching.

#include "common.h"
#define ENABLE_PROFILER 1	// retail's MechGuide.obj profiles slot 3 (004a0be0)
#include "profile.h"
#include "Array.h"
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
#define MECHGUIDE_FULL
#include "Guide.h"

// FUNCTION: 0x004a0930 init:ZERO_FVECTOR
// FUNCTION: 0x004a0950 init:ID_FMATRIX

//******************************************************************************************
CMechGuide::CMechGuide(CUnit *unit) : CGuide(unit)	// FUNCTION: 0x004a0a20
{
	mGrid = unit->GetOccupancyGrid();
	mTarget = NULL;
	EVENT_MANAGER.AddEvent(2000, this, NEXT_FRAME, START_OF_FRAME, NULL, NULL);
}

// The implicit destructor.
// FUNCTION: 0x004a0b10 ??_GCMechGuide@@UAEPAXI@Z
// FUNCTION: 0x004a0b30 ??1CMechGuide@@UAE@XZ

// The path's step: FALSE once the path is used up. Tested as an if/return value, it ends the caller's path loop as
// retail's does (JE exit, JMP back at 004a0ff9): VC6 jump-threads the jumps of an inlined function's tested return
// value, the loop's back edge included, where a single-return step (`return --n > 0;`) ends it JNE back, JMP exit.
// GroundVehicleGuide.cpp, InfantryGuide.cpp and SquadNormal.cpp keep the same step. All four path loops end this way,
// so it was probably a member of the path class, but a member in squad.h moves CGame::DrawDebugStuff (00470650).
static inline BOOL AdvancePath(CNormalSquadUnknownD4 &path)
{
	if (--path.mUnknown0C <= 0)
		return FALSE;
	return TRUE;
}

//******************************************************************************************
// As the infantry guide's slot 3, profiled: it steps away from what it touches, else follows its path, turns, and
// walks on once facing within 0.2. Retail calls FVector's constructor out of line in the first two operator-s
// (004a0c56, 004a0ccc), by the inline-budget rule (README).
void CMechGuide::Unknown3()	// FUNCTION: 0x004a0bc0
{
	PROFILE_FN(MechGuide);
	if (!GetUnit()->IsDying())
	{
		GetUnit()->mUnknown14C = FVector(0, 0, 0);
		float	current = GetUnit()->mCurrentOrientation.mYaw;
		FVector	d = mDestination - GetUnit()->GetPos();
		BOOL	pushed = FALSE;
		CThing	*target;
		if ((GetUnit()->mAI->mAIMode != 2) && (target = mTarget.ToRead()))
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
			return;

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

		if ((GetUnit()->mAI->mAIMode != 2) && (!pushed))
		{
			if ((d.MagnitudeXYSq() > 1.0f) && (mGrid))
			{
				FVector	dest = mDestination;
				const CNormalSquadUnknownD4 &path = mPath;	// STAND-IN (order; review F 2026-10-15): reading mPath directly loses 004a0bc0
				if ((abs(path.mUnknown00 - (SINT)dest.X) > 1) || (abs(path.mUnknown04 - (SINT)dest.Y) > 1))
					mGrid->Unknown4bc2e0(GetUnit()->GetPos(), mDestination, 1, &mPath);
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
		d.Normalise();
		if (distanceSq < 0.01f)
		{
			mMode = 0;
			return;
		}
		if (((float)fabs(AngleDifference(yaw, current)) > 0.2f) && (distanceSq < 0.1f))
			return;
		if ((mMode != 3) || pushed)
		{
			float	speed = GetUnit()->Unknown111() / 5.0f;
			FVector	forward(0, speed, 0);
			FMatrix	&ori = GetUnit()->GetOrientation();
			GetUnit()->mUnknown14C = ori * forward;
		}
	}
	else
		GetUnit()->Stop();
}

//******************************************************************************************
void CMechGuide::HandleEvent(CEvent *event)	// FUNCTION: 0x004a1200
{
	if (event->GetEventNum() == 2000)
	{
		FindTarget();
		EVENT_MANAGER.AddEvent(2000, this, EVENT_MANAGER.GetTime() + GAME.FloatRandom() * 0.5f, START_OF_FRAME, NULL, (CScheduledEvent *)event);
	}
}

//******************************************************************************************
void CMechGuide::FindTarget()	// FUNCTION: 0x004a1270
{
	mTarget = NULL;
	CMapWhoEntry	*e = MAP_WHO.GetFirstEntryWithinRadius(mUnit->GetPos(), 2.0f);
	float	best = 999999.0f;
	for (; e; e = MAP_WHO.GetNextEntryWithinRadius())
	{
		CThing	*thing = e->GetThing();
		if ((!thing) || (thing == mUnit))
			continue;
		if (thing->GetThingType() & THING_TYPE_COMPONENT)
			continue;
		if (!(thing->GetThingType() & (THING_TYPE_VEHICLE | THING_TYPE_MECH)))
			continue;
		FVector	d = thing->GetPos() - mUnit->GetPos();
		float	gap = d.Magnitude() - mUnit->GetRadius() - thing->GetRadius();
		if ((gap < best) && (gap < 1.0f))
		{
			mTarget = thing;
			best = gap;
		}
	}
}
