// MCComponent.cpp: CMCComponent, a component's motion controller (reconstructed; Stuart's file is not in his
// release). The "turret" part follows the unit's yaw and the "barrel" parts its pitch less the ground's slope along
// the turret's heading, each blended to the render frame.

#include	"common.h"

#include	"Motioncontroller.h"
#include	"mesh.h"
#include	"unit.h"
#include	"game.h"
#include	"MCComponent.h"

#include	<string.h>

// The per-file maths constants from maths.h:
// FUNCTION: 0x00495840 init:ZERO_FVECTOR
// FUNCTION: 0x00495860 init:ID_FMATRIX

//*********************************************************************************
CMCComponent::CMCComponent(IRenderableThing *thing)	// FUNCTION: 0x00495930
{
	mThing = thing;
	mLastPitch = mLastYaw = -999.0f;
}

// FUNCTION: 0x00495960 ??_GCMCComponent@@UAEPAXI@Z

//*********************************************************************************
CMCComponent::~CMCComponent()	// FUNCTION: 0x00495980
{
	mThing = NULL;
}

//*********************************************************************************
void	CMCComponent::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x004959a0
{
	CUnit	*unit = (CUnit *)mThing;

	if (!strcmp(part->mName, "turret"))
	{
		FMatrix	old_ori(unit->mUnknownE4, 0, 0);
		FMatrix	new_ori(unit->mUnknownE0, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * GAME.GetFrameRenderFraction());
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
		*ori = *ori * (old_ori + (new_ori - old_ori) * GAME.GetFrameRenderFraction());
		ori->Normalise();
	}
}

//*********************************************************************************
BOOL	CMCComponent::Unknown8()
{
	CUnit	*unit = (CUnit *)mThing;
	if ((unit->mUnknownE4 != unit->mUnknownE0) || (unit->mUnknownE0 != mLastYaw) ||
		(unit->mUnknownF0 != unit->mUnknownE8) || (unit->mUnknownE8 != mLastPitch))
		return TRUE;
	return FALSE;
}

// Inline functions from headers that this object emits: FMatrix's operator- and its product with a float.
// FUNCTION: 0x00495e00 ??GFMatrix@@QBE?AV0@ABV0@@Z
// FUNCTION: 0x00495ed0 ??DFMatrix@@QBE?AV0@M@Z
