// MCGroundAttack.h: CMCGroundAttack (0x14 bytes, vtable 005dc330), a ground-attack aircraft's motion controller
// (MCGroundAttack.obj; CGroundAttackAircraft::Init 0047bc41 passes the unit's IRenderableThing).

#ifndef MCGROUNDATTACK_H
#define MCGROUNDATTACK_H

#include "Motioncontroller.h"

class CMCGroundAttack : public CMotionController
{
public:
	CMCGroundAttack(class IRenderableThing *thing);	// 004964d0
#ifdef MCGROUNDATTACK_CPP	// MCGroundAttack.cpp's view; the other includers keep their declaration counts
	virtual			~CMCGroundAttack();												// 1 (00496520)
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (00496540)
	virtual BOOL	Unknown8();														// 8 (004968a0)

	static BOOL		CanMergePart(CMeshPart *part);									// 004968f0: not the turret
	static BOOL		HasTurret(class CMesh *mesh);									// 00496910

	float	mLastYaw;																// +0x0c
	float	mLastBob;																// +0x10
#else
	UBYTE	mUnknown0C[0x14 - 0x0c];
#endif
};

#endif
