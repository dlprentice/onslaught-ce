// MCGillM.cpp: CMCGillM's two slots, the GillM's body pitch and roll with the ground (reconstructed; Stuart's file
// is not in his release). CGillM::InitLegMotion builds the controller inline; its vtable is GillM.obj's.

#include	"common.h"
#include	"unit.h"
#include	"BattleEngine.h"
#include	"game.h"
#include	"MCBattleEngine.h"

// The per-file maths constants from maths.h:
// FUNCTION: 0x004962b0 init:ZERO_FVECTOR
// FUNCTION: 0x004962d0 init:ID_FMATRIX

//*********************************************************************************
float	CMCGillM::Unknown11()	// FUNCTION: 0x004963a0
{
	return ((CGillM *)mThing)->GetGroundPitch();
}

//*********************************************************************************
float	CMCGillM::Unknown12()	// FUNCTION: 0x004963c0
{
	return ((CGillM *)mThing)->GetGroundRoll();
}

//******************************************************************************************
// The Gill M's value for slot 13 (as CMCThunderHead's inline 180). It compiles to the thunderhead's body, so the
// linker folds it into that one (/OPT:ICF).

// FUNCTION: 0x004f4820
SINT CMCGillM::Unknown13()
{
	return 180;
}
