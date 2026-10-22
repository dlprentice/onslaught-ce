// AirGuide.cpp: reconstructed from AirGuide.obj (00402060..004029dd); Stuart's release has no copy. A flying unit's
// guide: it heads for its destination (or circles, in mode 0), climbs over what is near, keeps clear of the ground
// and the edges of the map, banks into its turns and flies at four times its top speed.

#include "common.h"
#include "unit.h"
#include "UnitAI.h"
#include "map.h"
#include "mapwho.h"
#include "mapwhoentry.h"
#include "eventmanager.h"
#include "game.h"
#include "AirGuide.h"

// FUNCTION: 0x00402060 init:ZERO_FVECTOR
// FUNCTION: 0x00402080 init:ID_FMATRIX

//******************************************************************************************
CAirGuide::CAirGuide(CUnit *unit) : CGuide(unit)	// FUNCTION: 0x00402150
{
	mClearance = 0;
	mNearest = NULL;
	EVENT_MANAGER.AddEvent(AIR_GUIDE_CLEARANCE, this, NEXT_FRAME);
	EVENT_MANAGER.AddEvent(AIR_GUIDE_NEAREST, this, NEXT_FRAME);
}

// FUNCTION: 0x00402200 ??_GCAirGuide@@UAEPAXI@Z
// FUNCTION: 0x00402220 ??1CAirGuide@@UAE@XZ

//******************************************************************************************
// Matched in accessor style for the inline-budget rule (README): every member access goes through a trivial
// accessor (CGuide::GetUnit, GetDestination, GetMode; CAirGuide::GetClearance, GetNearest; CUnit::GetAI,
// GetDeployState and the SetUnknown174/120/14C setters; CUnitAI::GetMode), the flight code is written out in each
// branch, and the discarded velocity call is Elevation(). That puts the out-of-line FVector constructors and Magnitude
// exactly where retail has them (004022b5, 00402317, 0040233c). The roll is clamped as a float and signed by a
// conditional, which puts retail's float PI/2 (0040253e) and roll's store and reload (0040257e) in place. The named
// height view and nearest-position difference retain the dot-product order and pitch-store scheduling.
void CAirGuide::Unknown3()	// FUNCTION: 0x00402280
{
	if (!GetUnit()->IsDying())
	{
		FVector	d = GetDestination() - GetUnit()->GetPos();
		float	yaw = d.Azimuth();
		// STAND-IN (ids): d.Z through a const reference, as in CDiveBomberGuide; written `atan2(d.Z, ...)` the function is 6 bytes off.
		const float &height = d.Z;
		float	pitch = (float)atan2(height, d.MagnitudeXY());
		float	course = GetUnit()->GetVelocity().Azimuth();
		float	elevation = GetUnit()->GetVelocity().Elevation();
		FVector	right = GetUnit()->GetOrientation().GetColumnX();

		float	heading;
		switch (GetMode())
		{
		case 0:
			heading = course + PI / 2;
			pitch = 0;
			break;
		case 2:
			heading = yaw - PI;
			break;
		default:
			heading = yaw;
			break;
		}
		if (heading > PI)
			heading -= PI_M2;
		if (heading < -PI)
			heading += PI_M2;
		if (pitch > PI)
			pitch -= PI_M2;
		if (pitch < -PI)
			pitch += PI_M2;

		CUnit	*unit = GetUnit();
		if (unit->GetAI()->GetMode() != 2)
		{
			CThing	*nearest = GetNearest();
			if (nearest)
			{
				pitch = -PI / 3;
				if (!((nearest->GetPos() - unit->GetPos()).Z > 0))
					pitch = PI / 3;
			}

			if (GetClearance() < 5.0f)
				pitch = -PI / 4;
			else if ((GetClearance() < 15.0f) && (pitch > 0))
				pitch = 0;
			else if (GetClearance() > 50.0f)
				pitch = PI / 4;

			if ((unit->GetDeployState() != 1) && (unit->GetDeployState() != 2))
			{
				if (unit->GetPos().X < 10.0f)
					heading = -PI / 2;
				else if (unit->GetPos().X > 502.0f)
					heading = PI / 2;
				else if (unit->GetPos().Y < 10.0f)
					heading = 0;
				else if (unit->GetPos().Y > 502.0f)
					heading = PI;
			}
		}

		float	diff = (float)fabs(AngleDifference(course, heading));
		if (diff > PI / 2)
			diff = PI / 2;
		float	roll = ((right * d) < 0) ? diff : -diff;

		if (fabsf(roll) > 0.1f)
			unit->SetUnknown174(1.0f);
		else
			unit->SetUnknown174(0);
		GetUnit()->SetUnknown120(CEulerAngles(heading, pitch, roll));
		float	speed = (GetUnit()->GetMaxVelocity() / 20.0f) * 4.0f;
		FMatrix	&ori = GetUnit()->GetOrientation();
		GetUnit()->SetUnknown14C(FVector(speed * ori.Row[0].Y, speed * ori.Row[1].Y, speed * ori.Row[2].Y));
	}
	else
	{
		GetUnit()->SetUnknown174(0);
		float	speed = (GetUnit()->GetMaxVelocity() / 20.0f) * 4.0f;
		FMatrix	&ori = GetUnit()->GetOrientation();
		GetUnit()->SetUnknown14C(FVector(speed * ori.Row[0].Y, speed * ori.Row[1].Y, speed * ori.Row[2].Y));
	}
}

//******************************************************************************************
// Both events come round again in half a second or so.
static inline void Reschedule(SINT num, CMonitor *guide, CEvent *event)
{
	EVENT_MANAGER.AddEvent(num, guide, GAME.FloatRandom() * 0.5f + EVENT_MANAGER.GetTime() + 0.5f, START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
void CAirGuide::HandleEvent(CEvent *event)	// FUNCTION: 0x004026e0
{
	switch (event->GetEventNum())
	{
	case AIR_GUIDE_CLEARANCE:
		UpdateClearance();
		Reschedule(AIR_GUIDE_CLEARANCE, this, event);
		break;
	case AIR_GUIDE_NEAREST:
		FindNearest();
		Reschedule(AIR_GUIDE_NEAREST, this, event);
		break;
	}
}

//******************************************************************************************
void CAirGuide::FindNearest()	// FUNCTION: 0x004027c0
{
	mNearest = NULL;
	CMapWhoEntry	*entry = MAP_WHO.GetFirstEntryWithinRadius(mUnit->GetPos(), 30.0f);
	float	best = 999999.0f;
	float	radius = mUnit->GetRadius();
	float	radiusSq = radius * radius;
	for (; entry; entry = MAP_WHO.GetNextEntryWithinRadius())
	{
		CThing	*thing = entry->GetThing();
		if ((!thing) || (thing == mUnit) || (thing->GetThingType() & THING_TYPE_AMMUNITION))
			continue;
		float	r = thing->GetRadius();
		FVector	d = thing->GetPos() - mUnit->GetPos();
		float	gap = d.MagnitudeSq() - r * r - radiusSq;
		if ((gap < best) && (gap < 225.0f))
		{
			mNearest = thing;
			best = gap;
		}
	}
}

//******************************************************************************************
void CAirGuide::UpdateClearance()	// FUNCTION: 0x004028e0
{
	CUnit	*unit = mUnit;
	if (((SINT)unit->GetPos().X == mCellX) && ((SINT)unit->GetPos().Y == mCellY))
		return;
	mCellX = (SINT)unit->GetPos().X;
	mCellY = (SINT)unit->GetPos().Y;

	float	z = -unit->GetPos().Z;
	float	sea = -MAP.GetWaterLevel();
	mClearance = 999999.0f;
	SINT	cx = (SINT)unit->GetPos().X;
	SINT	cy = (SINT)unit->GetPos().Y;
	for (SINT y = cy - 20; y <= cy + 20; y += 5)
		for (SINT x = cx - 20; x <= cx + 20; x += 5)
		{
			float	ground = -MAP.GetHeight(x, y);
			if (ground < sea)
				ground = sea;
			float	clearance = z - ground;
			if (clearance < mClearance)
				mClearance = clearance;
		}
}
