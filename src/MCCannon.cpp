// MCCannon.cpp: CMCCannon, a cannon emplacement's motion controller (reconstructed; Stuart's file is not in his
// release). The "turret" part follows the unit's yaw and the "barrel" parts its pitch, each blended to the render
// frame, and the whole sits down by the cannon's drop.

#include	"common.h"

#define MCCANNON_CPP
#include	"Motioncontroller.h"
#include	"mesh.h"
#include	"unit.h"
#include	"MCCannon.h"

#include	<string.h>

// The per-file maths constants from maths.h:
// FUNCTION: 0x00495140 init:ZERO_FVECTOR
// FUNCTION: 0x00495160 init:ID_FMATRIX

//*********************************************************************************
CMCCannon::CMCCannon(IRenderableThing *thing)	// FUNCTION: 0x00495230
{
	mThing = thing;
	mLastPitch = mLastYaw = -999.0f;
}

// FUNCTION: 0x00495260 ??_GCMCCannon@@UAEPAXI@Z

//*********************************************************************************
CMCCannon::~CMCCannon()	// FUNCTION: 0x00495280
{
	mThing = NULL;
}

//*********************************************************************************
void	CMCCannon::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x004952a0
{
	CCannon	*unit = (CCannon *)mThing;
	float	frac = unit->GetFractionTime();

	if (!strcmp(part->mName, "turret"))
	{
		FMatrix	old_ori(unit->mUnknownE4, 0, 0);
		FMatrix	new_ori(unit->mUnknownE0, 0, 0);
		*ori = *ori * (old_ori + (new_ori - old_ori) * frac);
		ori->Normalise();
	}
	else if (!strncmp(part->mName, "barrel", 6))
	{
		float	pitch = unit->mCurrentOrientation.mPitch;
		FMatrix	old_ori(0, unit->mUnknownF0 - unit->mUnknownF4 - pitch, 0);
		FMatrix	new_ori(0, unit->mUnknownE8 - unit->mUnknownF4 - pitch, 0);
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
BOOL	CMCCannon::Unknown8()
{
	CUnit	*unit = (CUnit *)mThing;
	if ((unit->mUnknownE4 != unit->mUnknownE0) || (unit->mUnknownE0 != mLastYaw) ||
		(unit->mUnknownF0 != unit->mUnknownE8) || (unit->mUnknownE8 != mLastPitch))
		return TRUE;
	return FALSE;
}

//*********************************************************************************
BOOL	CMCCannon::HasTurret(CMesh *mesh)	// FUNCTION: 0x004957d0
{
	for (SINT i = 0; i < mesh->mNumParts; i++)
	{
		if (!stricmp(mesh->GetPart(i)->mName, "turret"))
			return TRUE;
		if (!strnicmp(mesh->GetPart(i)->mName, "barrel", 6))
			return TRUE;
	}
	return FALSE;
}
