// BomberGuide.cpp: CBomberGuide's flight (slot 3, 00416970). Not in Stuart's release;
// reconstructed from the retail code. The object's maths constants open it (00416880, 004168a0).

#include "common.h"
#include "mathsconst.h"	// FUNCTION: 0x00416880 init:ZERO_FVECTOR
						// FUNCTION: 0x004168a0 init:ID_FMATRIX
#include "unit.h"
#include "BomberGuide.h"

// An angle brought round into -PI..PI. By reference: retail keeps the heading in memory through it (00416a22..
// 00416a5c), which the same tests written in place do not.
static inline void WrapAngle(float &a)
{
	if (a > PI)
		a -= PI_M2;
	if (a < -PI)
		a += PI_M2;
}

//******************************************************************************************
// The air guide's flight (CAirGuide::Unknown3) held level: it climbs or dives only for what is near and for its
// clearance against the unit's minimum altitude, banks half as far, and keeps no watch on the map's edges.
void CBomberGuide::Unknown3()	// FUNCTION: 0x00416970
{
	if (!GetUnit()->IsDying())
	{
		FVector	d = GetDestination() - GetUnit()->GetPos();
		float	yaw = d.Azimuth();
		float	pitch = 0;
		float	course = GetUnit()->GetVelocity().Azimuth();
		FVector	right = GetUnit()->GetOrientation().GetColumnX();

		switch (GetMode())
		{
		case 0:
			pitch = 0;
			yaw = course + PI / 2;
			break;
		case 2:
			yaw -= PI;
			break;
		}
		WrapAngle(yaw);

		float	roll = fabsf(AngleDifference(course, yaw));
		if (roll > PI / 2)
			roll = PI / 2;
		if ((right * d) < 0)
			roll *= 0.5f;
		else
			roll *= -0.5f;

		CThing	*nearest = GetNearest();
		if (nearest)
		{
			FVector	e = nearest->GetPos() - GetUnit()->GetPos();
			pitch = (e.Z > 0) ? -PI / 6 : PI / 6;
		}

		CUnit	*unit = GetUnit();
		if (mClearance < unit->GetUnitData()->mMinAltitude)
			pitch = -PI / 4;
		else if ((mClearance < unit->GetUnitData()->mMinAltitude * 3.0f) && (pitch > 0))
			pitch = 0;
		else if (mClearance > 50.0f)
			pitch = PI / 4;

		if (fabsf(roll) > 0.1f)
			unit->SetUnknown174(1.0f);
		else
			unit->SetUnknown174(0);
		GetUnit()->SetUnknown120(CEulerAngles(yaw, pitch, roll));
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
