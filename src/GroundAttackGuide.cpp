// GroundAttackGuide.cpp: CGroundAttackGuide's flight (slot 3, 0047c200). Not in Stuart's release;
// reconstructed from the retail code. The object's maths constants open it (0047c110, 0047c130).

#include "common.h"
#include "mathsconst.h"	// FUNCTION: 0x0047c110 init:ZERO_FVECTOR
						// FUNCTION: 0x0047c130 init:ID_FMATRIX
#include "unit.h"
#include "UnitAI.h"
#include "GroundAttackGuide.h"

// STAND-IN (regions, ids): forward along the unit's heading at four times its top speed. The helper preserves the
// out-of-line FVector constructor at 0047c247 (written in place the function is 814 bytes off). The named component
// products keep the first scheduling region ending at the negated course store (0047c2c0), before the next velocity call
// is prepared (products straight into the constructor, or without their brackets: 16 bytes off); the bracket round the
// speed's first product is a no-code IL node worth 852 bytes.
static inline FVector FlyVelocity(CGuide *guide)
{
	float	speed = (guide->GetUnit()->GetMaxVelocity() / 20.0f) * 4.0f;
	FMatrix	&ori = guide->GetUnit()->GetOrientation();
	float	x = (speed * ori.Row[0].Y);
	float	y = (speed * ori.Row[1].Y);
	float	z = (speed * ori.Row[2].Y);
	return FVector(x, y, z);
}

// STAND-IN (spelling): sign the turn angle by the destination direction projected onto the aircraft's right axis. This
// consumer retains retail's Z/Y pair followed by X, including each product's load order (0047c539..0047c553); the test
// written in place is 4 bytes off.
static inline float BankIntoTurn(const FVector &right, const FVector &direction, float angle)
{
	return ((direction * right) < 0) ? angle : -angle;
}

//******************************************************************************************
// The air guide's flight (CAirGuide::Unknown3), with its clearance measured against the unit's minimum altitude and
// steeper climbs and dives away from what is near; a dying aircraft stops.
// The flight velocity and the bank sign are the STAND-IN helpers above; the named height view preserves the
// dot-product grouping used to choose the roll (STAND-IN (spelling): `d.Z` straight into atan2 is 6 bytes off).
void CGroundAttackGuide::Unknown3()	// FUNCTION: 0x0047c200
{
	if (!GetUnit()->IsDying())
	{
		FVector	velocity = FlyVelocity(this);
		FVector	d = GetDestination() - GetUnit()->GetPos();
		float	yaw = d.Azimuth();
		const float	&height = d.Z;	// STAND-IN (spelling), see above
		float	pitch = (float)atan2(height, d.MagnitudeXY());
		float	course = GetUnit()->GetVelocity().Azimuth();
		float	elevation = GetUnit()->GetVelocity().Elevation();
		FVector	right = GetUnit()->GetOrientation().GetColumnX();

		switch (GetMode())
		{
		case 0:
			pitch = 0;
			yaw = course + PI / 2;
			break;
		case 2:
			yaw -= PI;
			pitch -= PI;
			break;
		}
		if (yaw > PI)
			yaw -= PI_M2;
		if (yaw < -PI)
			yaw += PI_M2;
		if (pitch > PI)
			pitch -= PI_M2;
		if (pitch < -PI)
			pitch += PI_M2;

		CUnit	*unit = GetUnit();
		if (unit->GetAI()->GetMode() != 2)
		{
			CThing	*nearest = GetNearest();
			if (nearest)
				pitch = (nearest->GetPos().Z - unit->GetPos().Z > 0) ? -PI / 3 : PI / 3;

			if (mClearance < unit->GetUnitData()->mMinAltitude)
				pitch = -PI / 4;
			else if ((mClearance < unit->GetUnitData()->mMinAltitude * 3.0f) && (pitch > 0))
				pitch = 0;
			else if (mClearance > 50.0f)
				pitch = PI / 4;

			if ((unit->GetDeployState() != 1) && (unit->GetDeployState() != 2))
			{
				if (unit->GetPos().X < 10.0f)
					yaw = -PI / 2;
				else if (unit->GetPos().X > 502.0f)
					yaw = PI / 2;
				else if (unit->GetPos().Y < 10.0f)
					yaw = 0;
				else if (unit->GetPos().Y > 502.0f)
					yaw = PI;
			}
		}

		float	diff = (float)fabs(AngleDifference(course, yaw));
		if (diff > PI / 2)
			diff = PI / 2;
		float	roll = BankIntoTurn(right, d, diff);

		if (fabsf(roll) > 0.1f)
			unit->SetUnknown174(1.0f);
		else
			unit->SetUnknown174(0);
		GetUnit()->SetUnknown120(CEulerAngles(yaw, pitch, roll));
		GetUnit()->SetUnknown14C(velocity);
	}
	else
	{
		GetUnit()->SetUnknown174(0);
		GetUnit()->SetUnknown14C(FVector(0, 0, 0));
	}
}
