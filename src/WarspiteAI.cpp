// WarspiteAI.cpp: reconstructed from WarspiteAI.obj (005045b0..005046eb); Stuart's release has no copy. The file
// name is ours: the object sits between Warspite.obj and WarspiteDome.obj and holds CWarspiteAI's own slot.
#include "common.h"
#include "unit.h"				// FUNCTION: 0x005045b0 init:ZERO_FVECTOR
#include "eventmanager.h"		// FUNCTION: 0x005045d0 init:ID_FMATRIX
#include "collisionseekingthing.h"
#include "WarspiteAI.h"

//******************************************************************************************
void CWarspiteAI::Unknown12()	// FUNCTION: 0x005046a0
{
	CCollisionSeekingThing *cst = mForUnit->GetCST();
	if (cst)
		cst->mIgnoreThing = NULL;
	Unknown6();
	CUnit *unit = mForUnit;
	if (unit->GetMissionScript())
		EVENT_MANAGER.AddEvent(2003, unit, -1.0f);
}
