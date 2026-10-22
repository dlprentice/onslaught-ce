// GillMHead.cpp: reconstructed from GillMHead.obj (0047a640..0047a9df); Stuart's release has no copy. The Gill-M's
// head: a component that opens, attacks while its weapons can fire, and closes again.
// The allocations' __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#include "component.h"
#include "GillMHeadAI.h"

// FUNCTION: 0x0047a640 init:ZERO_FVECTOR
// FUNCTION: 0x0047a660 init:ID_FMATRIX

//******************************************************************************************
void CGillMHead::Init(CInitThing *init)	// FUNCTION: 0x0047a730
{
	SUPERTYPE::Init(init);
	SetAnimMode("idle", TRUE, TRUE);
}

//******************************************************************************************
void CGillMHead::Unknown119(CInitThing *init)	// FUNCTION: 0x0047a760
{
#line 19
	mAI = new(MEMTYPE_AI) CGillMHeadAI(this, init);
}

// CGillMHeadAI's implicit destructor, emitted here with its vtable.
// FUNCTION: 0x0047a7f0 ??_GCGillMHeadAI@@UAEPAXI@Z
// FUNCTION: 0x0047a810 ??1CGillMHeadAI@@UAE@XZ

//******************************************************************************************
// It fires only from idle, and opens as it does.
BOOL CGillMHead::Unknown86()	// FUNCTION: 0x0047a8b0
{
	if (GetRenderAnimation() != GetAnimModeByName("idle"))
		return FALSE;
	if (!SUPERTYPE::Unknown86())
		return FALSE;
	SetAnimMode("open", TRUE, FALSE);
	return TRUE;
}

//******************************************************************************************
// open, attack while a weapon is busy, close, idle. One chain of else-ifs sharing the return: retail saves ebx only
// after the AM_INVALID test (0047a918).
BOOL CGillMHead::FinishedPlayingCurrentAnimation()	// FUNCTION: 0x0047a900
{
	if (GetRenderAnimation() != AM_INVALID)
	{
		if (GetRenderAnimation() == GetAnimModeByName("open"))
			SetAnimMode("attack", TRUE, FALSE);
		else if (GetRenderAnimation() == GetAnimModeByName("attack"))
		{
			if (AnyWeaponUnknown50a290())
				SetAnimMode("close", TRUE, FALSE);
			else
				SetAnimMode("attack", TRUE, FALSE);
		}
		else if (GetRenderAnimation() == GetAnimModeByName("close"))
			SetAnimMode("idle", TRUE, TRUE);
		return FALSE;
	}
	return TRUE;
}

//******************************************************************************************
void CGillMHead::SetAIState(EAIState inAIState)	// FUNCTION: 0x0047a9c0
{
	if (inAIState != AI_ONF)
		SUPERTYPE::SetAIState(inAIState);
}
