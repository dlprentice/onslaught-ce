// GroundVehicleGuide.cpp: reconstructed from GroundVehicleGuide.obj (0047d4a0..0047e18d); Stuart's release has no
// copy. A ground vehicle's guide: it plans a path over its occupancy grid, follows the path's cells, and when the
// cell ahead is blocked turns to the first open neighbour, then drives forward (or backward in mode 2).

#include "common.h"
#include "Array.h"
#include "unit.h"
#include "UnitAI.h"
#include "world.h"
#include "GroundVehicleGuide.h"

// FUNCTION: 0x0047d4a0 init:ZERO_FVECTOR
// FUNCTION: 0x0047d4c0 init:ID_FMATRIX

//******************************************************************************************
CGroundVehicleGuide::CGroundVehicleGuide(CUnit *unit) : CGuide(unit)	// FUNCTION: 0x0047d590
{
	mGrid = unit->GetOccupancyGrid();
}

// The implicit destructors: the path's (its constructor's unwinding calls it) and the guide's.
// FUNCTION: 0x0047d650 ??_GCGroundVehicleGuide@@UAEPAXI@Z
// FUNCTION: 0x0047d670 ??1CNormalSquadUnknownD4@@QAE@XZ
// FUNCTION: 0x0047d6d0 ??1CGroundVehicleGuide@@UAE@XZ

//******************************************************************************************
// Existing guide/unit accessors and direct initialization reproduce retail's initial FVector constructor
// (0047d79c) and Magnitude (0047d850) calls. The path predicate and caller-owned position preserve its path call.
// STAND-IN: original yaw ownership/spelling is unknown (Stuart's release has no copy). Retail independently reads
// the same unchanged yaw float at 0047dbf8 and 0047dc57 before two FCOS instructions. The neighbour cosine alone
// uses a volatile view of that existing scalar below, preserving the fresh input load and stopping VC6's reuse of
// the first cosine. It emits no additional read, store or call; the complete 2640-byte section and all 35 references
// match, including the 0xa4 frame and all switch-table edges. The ordinary second cos(yaw) leaves 1352 differing
// positions and frame 0xa0. Replace this revisitable compiler stand-in only with a supported natural form that
// preserves the whole target and every sibling reference graph. No shared type or compiler setting is changed.
static inline BOOL PathDestinationChanged(const CNormalSquadUnknownD4 &path, FVector destination)
{
	return (abs(path.mUnknown00 - (SINT)destination.X) > 1) || (abs(path.mUnknown04 - (SINT)destination.Y) > 1);
}

// The path's step, as in MechGuide.cpp: its if/return value ends the path loop JE exit, JMP back as retail's (0047db2a).
static inline BOOL AdvancePath(CNormalSquadUnknownD4 &path)
{
	if (--path.mUnknown0C <= 0)
		return FALSE;
	return TRUE;
}

void CGroundVehicleGuide::Unknown3()	// FUNCTION: 0x0047d750
{
	if (GetUnit()->IsDying())
	{
		GetUnit()->Stop();
		return;
	}

	FVector	d = GetDestination() - GetUnit()->GetPos();
	float	distanceSq = d.MagnitudeXYSq();
	GetUnit()->SetUnknown14C(FVector(0, 0, 0));
	if ((distanceSq < 0.5f) || (!GetMode()) || (GetUnit()->IsDeploying()))
		return;

	float	yaw = d.Azimuth();
	float	current = GetUnit()->GetCurrentOrientation().mYaw;
	if (GetUnit()->GetAI()->GetMode() == 2)
		GetUnit()->mUnknown120.mPitch = d.Elevation();

	if ((GetMode() == 1) && (GetUnit()->GetAI()->GetMode() != 2))
	{
		if (distanceSq > 1.0f)
		{
			if (mGrid)
			{
				FVector	pos = GetUnit()->GetPos();
				if ((!mPath.mUnknown08) || PathDestinationChanged(mPath, GetDestination()) || (!mGrid->UnknownCell(pos)))
					mGrid->Unknown4bc2e0(GetUnit()->GetPos(), GetDestination(), 1, &mPath);
			}
		}

		if (mPath.mUnknown08)
		{
			SINT	x = (SINT)GetUnit()->GetPos().X;
			SINT	y = (SINT)GetUnit()->GetPos().Y;
			mPath.GetCurrent(x, y);
			d = FVector((float)x, (float)y, 0) - GetUnit()->GetPos();
			while (d.MagnitudeXYSq() < 0.5f)
			{
				if (!AdvancePath(mPath))
				{
					d = GetDestination() - GetUnit()->GetPos();
					break;
				}
				mPath.GetCurrent(x, y);
				d = FVector((float)x, (float)y, 0) - GetUnit()->GetPos();
			}
			yaw = d.Azimuth();

			FVector	pos = GetUnit()->GetPos();
			if ((mGrid->UnknownCell(pos)) && (!mGrid->UnknownCell(pos + FVector((float)sin(-yaw), (float)cos(yaw), 0))))
			{
				// the cell ahead is blocked: try the four neighbours, nearest the heading first
				SINT	order[4];
				float	s = (float)sin(yaw);
				// STAND-IN (volatile view): the cosine reads yaw from memory through a volatile view; the plain `cos(yaw)` is 1352 bytes off.
				float	c = (float)cos(*(const volatile float *)&yaw);
				if (fabsf(c) > fabsf(s))
				{
					if (c > 0)
					{
						order[0] = 1;
						order[3] = 3;
					}
					else
					{
						order[0] = 3;
						order[3] = 1;
					}
					if (s > 0)
					{
						order[1] = 2;
						order[2] = 4;
					}
					else
					{
						order[1] = 4;
						order[2] = 2;
					}
				}
				else
				{
					if (s > 0)
					{
						order[0] = 2;
						order[3] = 4;
					}
					else
					{
						order[0] = 4;
						order[3] = 2;
					}
					if (c > 0)
					{
						order[1] = 1;
						order[2] = 3;
					}
					else
					{
						order[1] = 3;
						order[2] = 1;
					}
				}

				float	a;
				for (SINT i = 0; i < 4; i++)
				{
					FVector	dir;
					switch (order[i])
					{
					case 1:
						a = 0;
						dir = FVector(0, 1.0f, 0);
						break;
					case 3:
						a = PI;
						dir = FVector(0, -1.0f, 0);
						break;
					case 2:
						a = PI / 2;
						dir = FVector(-1.0f, 0, 0);
						break;
					case 4:
						a = -PI / 2;
						dir = FVector(1.0f, 0, 0);
						break;
					}
					if (mGrid->UnknownCell(pos + dir))
					{
						yaw = a;
						i = 4;
					}
				}
			}
		}
	}

	GetUnit()->mUnknown120.mYaw = yaw;
	if (fabs(AngleDifference(yaw, current)) > 0.6f)
		return;

	if (GetUnit()->GetAI()->GetMode() != 2)
	{
		FVector	pos = GetUnit()->GetPos();
		if ((mGrid->UnknownCell(pos)) && (!mGrid->UnknownCell(pos + FVector((float)sin(-current), (float)cos(current), 0))))
			return;
	}
	if (GetMode() == 3)
		return;

	FVector	v;
	if (GetMode() == 1)
		v = FVector(0, (GetUnit()->Unknown111() / 20.0f) * 4.0f, 0);
	else
		v = FVector(0, (GetUnit()->Unknown111() / 20.0f) * -4.0f, 0);
	GetUnit()->SetUnknown14C(GetUnit()->GetOrientation() * v);
}

// FUNCTION: 0x0048a4b0
inline CNormalSquadUnknownD4 *CGroundVehicleGuide::GetPath() { return &mPath; }
