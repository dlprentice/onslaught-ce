// MCGroundVehicle.cpp: CMCGroundVehicle, the motion controller of a ground vehicle without wheel motion
// (reconstructed; Stuart's file is not in his release). The "turret" part follows the unit's yaw and the "barrel"
// parts its pitch, less the slope of the ground under the turret's heading; the hull sits down by the unit's drop.

#include	"common.h"

#define MCGROUNDVEHICLE_CPP
#include	"Motioncontroller.h"
#include	"mesh.h"
#include	"unit.h"

#include	<string.h>

// The per-file maths constants from maths.h:
// FUNCTION: 0x00496960 init:ZERO_FVECTOR
// FUNCTION: 0x00496980 init:ID_FMATRIX

//*********************************************************************************
CMCGroundVehicle::CMCGroundVehicle(IRenderableThing *thing)	// FUNCTION: 0x00496a50
{
	mThing = thing;
	mLastPitch = mLastYaw = -999.0f;
}

// FUNCTION: 0x00496a80 ??_GCMCGroundVehicle@@UAEPAXI@Z

//*********************************************************************************
CMCGroundVehicle::~CMCGroundVehicle()	// FUNCTION: 0x00496aa0
{
	mThing = NULL;
}

//*********************************************************************************
void	CMCGroundVehicle::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x00496ac0
{
	CGroundVehicle	*unit = (CGroundVehicle *)mThing;
	float			frac = unit->GetFractionTime();

	if (!strcmp(part->mName, "turret"))
	{
		FMatrix	old_ori(unit->mUnknownE4, 0, 0);
		FMatrix	new_ori(unit->mUnknownE0, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * frac);
		ori->Normalise();
	}
	else if (!strncmp(part->mName, "barrel", 6))
	{
		// the ground's slope along the turret's heading
		FMatrix	heading = unit->GetOrientation() * FMatrix(unit->mUnknownE4, 0, 0);
		FVector	up = heading * FVector(0, 1, 0);
		float	slope = up.Elevation();

		FMatrix	old_ori(0, unit->mUnknownF0 - unit->mUnknownF4 - slope, 0);
		FMatrix	new_ori(0, unit->mUnknownE8 - unit->mUnknownF4 - slope, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * frac);
		ori->Normalise();
	}

	if (!part->mPartNumber)
	{
		float	drop = unit->mUnknown254;
		pos->Z -= drop + (unit->mUnknown250 - drop) * frac;
	}

	mLastYaw = unit->mUnknownE0;
	mLastPitch = unit->mUnknownE8;
}

//*********************************************************************************
// The same code as CMCComponent's; the linker keeps that copy (00495770).
BOOL	CMCGroundVehicle::Unknown8()
{
	CUnit	*unit = (CUnit *)mThing;
	if ((unit->mUnknownE4 != unit->mUnknownE0) || (unit->mUnknownE0 != mLastYaw) ||
		(unit->mUnknownF0 != unit->mUnknownE8) || (unit->mUnknownE8 != mLastPitch))
		return TRUE;
	return FALSE;
}

//*********************************************************************************
BOOL	CMCGroundVehicle::CanMergePart(CMeshPart *part)	// FUNCTION: 0x00496f60
{
	if (!stricmp(part->mName, "turret"))
		return FALSE;
	return strnicmp(part->mName, "barrel", 6) != 0;
}
