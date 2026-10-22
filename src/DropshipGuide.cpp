// DropshipGuide.cpp: reconstructed from DropshipGuide.obj (00448840..0044957e); Stuart's release has no copy. A
// dropship's guide: it flies as the air guide does while its doors are shut, turns to its landing yaw while they open,
// and settles onto the ground, levelled to its slope, while it folds and unfolds.

// STAND-IN: this guard selects const/nonconst cross-product
// overloads with grouped consumed scalar fields in maths.h. The borrowed forward
// vector and mutable side vector have different retail multiplication load orders.
// Original overload ownership and source expressions are unknown; revisit if found.
#include "common.h"
#define DROPSHIPGUIDE_CPP
#include "unit.h"
#include "Guide.h"
#include "map.h"
#include "game.h"

#include <math.h>

// FUNCTION: 0x00448840 init:ZERO_FVECTOR
// FUNCTION: 0x00448860 init:ID_FMATRIX

//******************************************************************************************
// The original source is unavailable. Accessors represent the retained unit, door
// and callback accesses; constructor, normalization and publication call boundaries
// are checked against the complete retail section and its references. The ordinary
// reconstruction differed at 87 operand bytes; the retained borrowed-forward and
// publication ownership form differed at 35. The stand-ins below reproduce the
// remaining operand/scheduler choices without additional emitted operations.
static inline void WrapAngle(float &a)
{
	if (a > PI)
		a -= PI_M2;
	if (a < -PI)
		a += PI_M2;
}

static inline float SurfaceHeight(const FVector &pos)
{
	float ground = MAP.Collide(pos);
	if (ground > MAP.GetWaterLevel())
		ground = MAP.GetWaterLevel();
	return ground;
}

// Local reconstruction of lateral projection: the retail x87 reduction at
// 00448b78 and 00448cc9 combines Y and Z before X. Grouping the Y product and
// commuting its and Z's real factors selects their retail load order (35 -> 27
// differing bytes). This helper is a source-control stand-in; original API unknown.
static inline float LateralProjection(const FVector &right, const FVector &offset)
{
	return ((offset.Y * right.Y) + offset.Z * right.Z) + right.X * offset.X;
}

void CDropshipGuide::Unknown3()	// FUNCTION: 0x00448930
{
	if (!GetUnit()->IsDying())
	{
		float	current = GetUnit()->GetCurrentOrientation().mYaw;
		CDropship	*dropship = (CDropship *)GetUnit();
		FVector	pos = GetUnit()->GetPos();
		// Source-width stand-in: float ground left 27 differing bytes; double leaves
		// 16 by removing a scheduling dependency, with no additional emitted round
		// or arithmetic operation. SurfaceHeight still returns float; original local
		// width is unknown. The guarded cross overloads complete the remaining match.
		double	ground = SurfaceHeight(pos);
		float	height = ground - pos.Z;

		FVector	d = GetDestination() - GetUnit()->GetPos();
		float	heading = d.Azimuth();
		float	pitch = 0;
		float	roll = 0;
		FVector	velocity(0, 0, 0);
		FVector	right = GetUnit()->GetOrientation() * FVector(1, 0, 0);

		switch (dropship->GetDoorState())
		{
		case 0:
		case 1:
		{
			switch (GetMode())
			{
			case 0:
				pitch = 0;
				heading = current + PI / 2;
				break;
			case 2:
				heading = heading - PI;
				break;
			}
			if (height < GetUnit()->GetUnitData()->mMinAltitude)
			{
				pitch = -PI / 5;
				heading = current;
			}
			WrapAngle(heading);

			float	r = fabs(AngleDifference(current, heading));
			if (r > PI / 8)
				r = PI / 8;
			if (LateralProjection(right, d) < 0)
				roll = (float)r;
			else
				roll = (float)-r;
			if (r > PI / 6)
				GetUnit()->SetUnknown174(1.0f);
			else
				GetUnit()->SetUnknown174(0);

			FMatrix	&ori = GetUnit()->GetOrientation();
			velocity = ori * FVector(0, 0.001f, 0);
			break;
		}

		case 2:
		{
			heading = dropship->mYawDelta;
			float	r = fabs(AngleDifference(current, heading));
			if (r > PI / 8)
				r = PI / 8;
			if (LateralProjection(right, d) < 0)
				roll = (float)r;
			else
				roll = (float)-r;
			break;
		}

		case 3:
			GetUnit()->SetUnknown174(0);
			heading = current;
			if (height > 10.0f)
			{
				pitch = -0.15f;
				roll = GAME.FloatRandom() * 0.25f - 0.5f;
				velocity = FVector(0, 0, 0.005f);
			}
			else
			{
				FMatrix	m;
				m.MakeRotationYawF(heading);
				const FVector	&fwd = m * FVector(0, 1, 0);
				FVector	normal = MAP.Normal(pos);
				FVector	side = fwd ^ normal;
				FVector	up = side ^ normal;
				up.Normalise();
				pitch = -up.Z;
				FVector	s = side;
				s.Normalise();
				roll = s.Z;

				float	vx = 0, vy = 0;
				if (height < 4.0f)
				{
					FVector	v = GetUnit()->GetVelocity();
					vx = v.X * -0.05f;
					vy = v.Y * -0.05f;
				}
				if ((dropship->GetVelocity().Z > height * (1.0f / 45.0f)) && (height > 0.075f))
					velocity = FVector(vx, vy, height * -0.0005f);
				else if (height <= 0.075f)
					velocity = FVector(vx, vy, 0.002f);
				else
					velocity = FVector(0, 0, 0.005f);
			}
			break;

		case 5:
			GetUnit()->SetUnknown174(0);
			velocity = FVector(0, 0, -0.001f);
			if (height > 5.0f)
			{
				pitch = -0.1f;
				roll = GAME.FloatRandom() * 0.1f - 0.2f;
			}
			else
			{
				FMatrix	m;
				m.MakeRotationYawF(heading);
				const FVector	&fwd = m * FVector(0, 1, 0);
				FVector	normal = MAP.Normal(pos);
				FVector	side = fwd ^ normal;
				FVector	up = side ^ normal;
				up.Normalise();
				pitch = -up.Z;
				FVector	s = side;
				s.Normalise();
				roll = s.Z;
			}
			heading = current;
			break;

		case 4:
			mUnit->SetUnknown14C(FVector(0, 0, 0));
			break;

		}

		if ((dropship->GetDoorState() != 4) && (dropship->GetDoorState() != 6) && (dropship->GetDoorState() != 7))
		{
			mUnit->SetUnknown120(CEulerAngles(heading, pitch, roll));
			GetUnit()->SetUnknown14C(velocity);
		}
	}
	else
	{
		GetUnit()->SetUnknown174(0);
		mUnit->SetUnknown14C(FVector(0, 0, 0));
	}
}
