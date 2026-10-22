// MCWarspiteDome.cpp: reconstructed from MCWarspiteDome.obj (0049ee90..0049f725); Stuart's release has no copy. The
// Warspite dome's motion controller turns its dome, barrels, spinner and guns.

#include "common.h"
#define MCWARSPITEDOME_CPP
#include "Motioncontroller.h"
#include "unit.h"
#include "mesh.h"
#include "game.h"
#include <stdlib.h>
#include "DebugLog.h"

#include <string.h>

// FUNCTION: 0x0049ee90 init:ZERO_FVECTOR
// FUNCTION: 0x0049eeb0 init:ID_FMATRIX

//******************************************************************************************
CMCWarspiteDome::CMCWarspiteDome(IRenderableThing *thing)	// FUNCTION: 0x0049ef80
{
	mThing = thing;
}

// FUNCTION: 0x0049efa0 ??_GCMCWarspiteDome@@UAEPAXI@Z

//******************************************************************************************
CMCWarspiteDome::~CMCWarspiteDome()	// FUNCTION: 0x0049efc0
{
	mThing = NULL;
}

//******************************************************************************************
// The dome turns by the unit's yaw, the barrels roll with the spin, the spinner pitches with it and each gun recoils
// along its axis, all interpolated to the render frame; the root sinks by the unit's bob.
void CMCWarspiteDome::GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world)	// FUNCTION: 0x0049efe0
{
	CWarspiteDome	*dome = (CWarspiteDome *)mThing;
	char	*name = part->mName;
	if (!strcmp(name, "dome"))
	{
		FMatrix	old(dome->mUnknownE4, 0, 0);
		FMatrix	now(dome->mUnknownE0, 0, 0);
		*ori = *ori * (old + (now - old) * GAME.GetFrameRenderFraction());
		ori->Normalise();
	}
	else if (!strncmp(name, "barrel", 6))
	{
		FMatrix	old(0, 0, dome->mUnknownF0);
		FMatrix	now(0, 0, dome->mUnknownE8);
		*ori = *ori * (old + (now - old) * GAME.GetFrameRenderFraction());
		ori->Normalise();
	}
	else if (!stricmp(name, "spinner"))
	{
		FMatrix	old(0, dome->mOldSpin, 0);
		FMatrix	now(0, dome->mSpin, 0);
		float	f = GAME.GetFrameRenderFraction();
		*ori = *ori * (old + (now - old) * f);
		ori->Normalise();
	}
	else if (!strnicmp(name, "gun", 3))
	{
		SINT	n = atoi(part->mName + 3) - 1;
		// STAND-IN (order; review F 2026-10-15): copy-initialised; `FVector old(...)` loses 0049efe0
		FVector	old = FVector(dome->mOldRecoil[n], 0, 0);
		FVector	now = FVector(dome->mRecoil[n], 0, 0);
		float	f = GAME.GetFrameRenderFraction();
		*pos = *pos + *ori * (old + (now - old) * f);
	}
	if (!part->mPartNumber)
		pos->Z -= dome->mBob;
}

//******************************************************************************************
BOOL CMCWarspiteDome::CanMergePart(CMeshPart *part)	// FUNCTION: 0x0049f600
{
	char	*name = part->mName;
	if (!stricmp(name, "dome"))
		return FALSE;
	if (!strnicmp(name, "barrel", 6))
		return FALSE;
	if (!stricmp(name, "spinner"))
		return FALSE;
	return strnicmp(name, "gun", 3) != 0;
}

//******************************************************************************************
BOOL CMCWarspiteDome::HasDome(CMesh *mesh)	// FUNCTION: 0x0049f670
{
	for (SINT i = 0; i < mesh->mNumParts; i++)
	{
		if (stricmp(mesh->GetPart(i)->mName, "dome") == 0)
			return TRUE;
		if (strnicmp(mesh->GetPart(i)->mName, "barrel", 6) == 0)
			return TRUE;
		if (stricmp(mesh->GetPart(i)->mName, "spinner") == 0)
			return TRUE;
		if (strnicmp(mesh->GetPart(i)->mName, "gun", 3) == 0)
			return TRUE;
	}
	return FALSE;
}
