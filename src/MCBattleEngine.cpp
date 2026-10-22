// MCBattleEngine.cpp: reconstructed from MCBattleEngine.obj (00492e20..00492f22); Stuart's release has no copy. Only the
// battle engine's pitch slot is out of line; the rest of CMCBattleEngine is inline in MCBattleEngine.h.

#include "common.h"
#include "game.h"
#include "BattleEngine.h"
#include "MCBattleEngine.h"

// FUNCTION: 0x00492e20 init:ZERO_FVECTOR
// FUNCTION: 0x00492e40 init:ID_FMATRIX

float CMCBattleEngine::Unknown11()	// FUNCTION: 0x00492f10
{
	return ((CBattleEngine *)mThing)->CalculatePitch();
}

// Multiplayer battle engines trace the ground under a foot. Out of line: retail folds it into the identical
// CBattleEngine::GetRequiresPolyBucket (0040ac30), and LINK folds an out-of-line body only with out-of-line ones.
BOOL CMCBattleEngine::Unknown14()
{
	if (GAME.IsMultiplayer())
		return TRUE;
	return FALSE;
}
