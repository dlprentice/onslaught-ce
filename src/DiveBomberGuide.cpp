// DiveBomberGuide.cpp: reconstructed from DiveBomberGuide.obj (00446750..00446c7e); Stuart's release has no copy. A
// dive bomber's guide flies it as the air guide does, with its own dive.

#include "common.h"
#include "unit.h"
#include "DiveBomberGuide.h"
#include "UnitAI.h"
#include "WorldPhysicsManager.h"

#include <math.h>

// FUNCTION: 0x00446750 init:ZERO_FVECTOR
// FUNCTION: 0x00446770 init:ID_FMATRIX

// Forward along the unit's heading at four times its top speed. The components are named, parenthesized products: they
// give the scheduler's first region of Unknown3 retail's node count, so that region ends at the course store (00446900).
static inline FVector FlyVelocity(CGuide *guide)
{
	float	speed = (guide->GetUnit()->GetMaxVelocity() / 20.0f) * 4.0f;
	FMatrix	&ori = guide->GetUnit()->GetOrientation();
	float	x = (speed * ori.Row[0].Y);
	float	y = (speed * ori.Row[1].Y);
	float	z = (speed * ori.Row[2].Y);
	return FVector(x, y, z);
}

//******************************************************************************************
// As the air guide's slot 3 (AirGuide.obj, and in its accessor style for the inline-budget rule), but it circles
// PI/8 off its course, climbs or dives PI/6 past what is near, and keeps between its unit's minimum altitude and three
// times that; its flight velocity is worked out first. The roll is clamped and signed as in AirGuide's slot 3. The
// nearest object's height is the Z of the relative vector (004469ee), and the height reference preserves retail's
// dot-product term order at 00446b64; cancellation can change the bank sign.
void CDiveBomberGuide::Unknown3()	// FUNCTION: 0x00446840
{
	if (!GetUnit()->IsDying())
	{
		FVector	velocity = FlyVelocity(this);
		FVector	d = GetDestination() - GetUnit()->GetPos();
		float	yaw = d.Azimuth();
		// STAND-IN (ids; review F 2026-10-15): d.Z through a const reference; written `atan2(d.Z, ...)` CDiveBomberGuide 00446840 loses its match.
		const float &height = d.Z;
		float	pitch = (float)atan2(height, d.MagnitudeXY());
		float	course = GetUnit()->GetVelocity().Azimuth();
		float	elevation = GetUnit()->GetVelocity().Elevation();
		FVector	right = GetUnit()->GetOrientation().GetColumnX();

		float	heading;
		switch (GetMode())
		{
		case 0:
			heading = course + PI / 8;
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
				pitch = -PI / 6;
				if (!((nearest->GetPos() - unit->GetPos()).Z > 0))
					pitch = PI / 6;
			}

			if (mClearance < unit->mUnitData->mMinAltitude)
				pitch = -PI / 4;
			else if ((unit->mUnitData->mMinAltitude * 3.0f > mClearance) && (pitch > 0))
				pitch = 0;
			else if ((mClearance > 50.0f) && (pitch < 0))
				pitch = 0;

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
		GetUnit()->SetUnknown14C(velocity);
	}
	else
	{
		GetUnit()->SetUnknown174(0);
		float	speed = (GetUnit()->GetMaxVelocity() / 20.0f) * 4.0f;
		FMatrix	&ori = GetUnit()->GetOrientation();
		GetUnit()->SetUnknown14C(FVector(speed * ori.Row[0].Y, speed * ori.Row[1].Y, speed * ori.Row[2].Y));
	}
}
