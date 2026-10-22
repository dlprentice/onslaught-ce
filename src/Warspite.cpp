// Warspite.cpp: reconstructed from Warspite.obj (00504370..005045ae); Stuart's release has no copy.
#include "common.h"
#include "unit.h"		// FUNCTION: 0x00504370 init:ZERO_FVECTOR
						// FUNCTION: 0x00504390 init:ID_FMATRIX
#include "WarspiteAI.h"

//******************************************************************************************
void CWarspite::InitCockpit(CInitThing *init)	// FUNCTION: 0x00504460
{
#line 10
	mAI = new(MEMTYPE_AI) CWarspiteAI(this, init);
}
// FUNCTION: 0x005044f0 ??_GCWarspiteAI@@UAEPAXI@Z
// FUNCTION: 0x00504510 ??1CWarspiteAI@@UAE@XZ
