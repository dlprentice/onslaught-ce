// CarverGuide.cpp: reconstructed from CarverGuide.obj (00422ea0..0042364a); Stuart's release has no copy. A
// carver's guide: an air guide's flight, which also rolls the carver over while it is being shot at, and which looks
// for what is near out to 45 (rounds from further off).

#include "common.h"
#include "unit.h"
#include "UnitAI.h"
#include "mapwho.h"
#include "mapwhoentry.h"
#include "eventmanager.h"
#include "game.h"
#include "CarverGuide.h"

// FUNCTION: 0x00422ea0 init:ZERO_FVECTOR
// FUNCTION: 0x00422ec0 init:ID_FMATRIX

//******************************************************************************************
CCarverGuide::CCarverGuide(CUnit *unit) : CAirGuide(unit)	// FUNCTION: 0x00422f90
{
}

// FUNCTION: 0x00422fb0 ??_GCCarverGuide@@UAEPAXI@Z
// FUNCTION: 0x00422fd0 ??1CCarverGuide@@UAE@XZ

//******************************************************************************************
// Matched as CAirGuide's slot 3 (the same flight body): retail calls the nested FVector constructors and Magnitude out
// of line (00423066, 004230c8, 004230ed). Written in the air guide's accessor style (GetUnit(), GetDestination(),
// GetMode(), GetNearest(), GetClearance(), the unit's setters) with the forward flight spelled out in both branches,
// which spends the inline budget as retail's source did; the discarded velocity call is Elevation() (its nested
// Magnitude is the out-of-line one) and the roll is clamped as a float and signed by a conditional, as CAirGuide's.
// Its named height view and nearest-position difference also retain the dot-product order and pitch-store scheduling.
void CCarverGuide::Unknown3()	// FUNCTION: 0x00423030
{
	if (!GetUnit()->IsDying())
	{
		FVector	d = GetDestination() - GetUnit()->GetPos();
		float	yaw = d.Azimuth();
		const float &height = d.Z;
		float	pitch = (float)atan2(height, d.MagnitudeXY());
		float	course = GetUnit()->GetVelocity().Azimuth();
		float	climb = GetUnit()->GetVelocity().Elevation();	// unused, as Stuart leaves them (game.cpp's p and my_c, SoundManager's cam)
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
			// climb or dive past the nearest thing
			CThing	*nearest = GetNearest();
			if (nearest)
			{
				pitch = -PI / 3;
				if (!((nearest->GetPos() - unit->GetPos()).Z > 0))
					pitch = PI / 3;
			}

			// keep clear of the ground
			if (GetClearance() < 5.0f)
				pitch = -PI / 4;
			else if ((GetClearance() < 15.0f) && (pitch > 0))
				pitch = 0;
			else if (GetClearance() > 50.0f)
				pitch = PI / 4;

			// turn back from the edges of the map
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

		// bank into the turn, at most PI/2
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

		// roll over when shot at
		CCarver	*carver = (CCarver *)mUnit;
		if (carver->WasAttacked())
		{
			float	r = GetUnit()->mCurrentOrientation.mRoll + PI / 2;
			if (r > PI)
				r -= PI_M2;
			carver->mUnknown120.mRoll = r;
		}
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
void CCarverGuide::HandleEvent(CEvent *event)	// FUNCTION: 0x00423490
{
	switch (event->GetEventNum())
	{
	case AIR_GUIDE_NEAREST:
		FindNearest();
		EVENT_MANAGER.AddEvent(AIR_GUIDE_NEAREST, this, GAME.FloatRandom() * 0.25f + EVENT_MANAGER.GetTime() + 0.25f, START_OF_FRAME, NULL, (CScheduledEvent *)event);
		break;
	default:
		CAirGuide::HandleEvent(event);
		break;
	}
}

//******************************************************************************************
void CCarverGuide::FindNearest()	// FUNCTION: 0x00423510
{
	mNearest = NULL;
	CMapWhoEntry	*entry = MAP_WHO.GetFirstEntryWithinRadius(mUnit->GetPos(), 45.0f);
	float	best = 999999.0f;
	float	radius = mUnit->GetRadius();
	float	radiusSq = radius * radius;
	for (; entry; entry = MAP_WHO.GetNextEntryWithinRadius())
	{
		CThing	*thing = entry->GetThing();
		if ((!thing) || (thing == mUnit))
			continue;
		float	r = thing->GetRadius();
		FVector	d = thing->GetPos() - mUnit->GetPos();
		float	gap = d.X * d.X + (d.Y * d.Y + d.Z * d.Z) - r * r - radiusSq;
		if (gap < best)
		{
			if ((thing->GetThingType() & THING_TYPE_AMMUNITION) && (gap < 2025.0f))
			{
				mNearest = thing;
				best = gap;
			}
			else if (gap < 225.0f)
			{
				mNearest = thing;
				best = gap;
			}
		}
	}
}
