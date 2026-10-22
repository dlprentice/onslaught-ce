// MCThunderHead.cpp: reconstructed from MCThunderHead.obj (0049ed80..0049ee86); Stuart's release has no copy. The
// Thunderhead's motion controller tilts its body by the unit's pitch.

#include "common.h"
#include "game.h"
#include "BattleEngine.h"
#include "MCBattleEngine.h"
#include "unit.h"
#include "DebugLog.h"

// FUNCTION: 0x0049ed80 init:ZERO_FVECTOR
// FUNCTION: 0x0049eda0 init:ID_FMATRIX

//******************************************************************************************
float CMCThunderHead::Unknown11()	// FUNCTION: 0x0049ee70
{
	return ((CThunderHead *)mThing)->mPitch;
}
