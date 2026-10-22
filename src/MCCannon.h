// MCCannon.h: CMCCannon, a cannon emplacement's motion controller (MCCannon.cpp; CCannon::Init builds it, and the mesh
// optimizer's tests in MotionController.cpp ask HasTurret).

#ifndef MCCANNON_H
#define MCCANNON_H

#include "Motioncontroller.h"

class CMesh;

// A cannon's motion controller (0x14 bytes, vtable 005dc2ac; CCannon::Init 0041b2fe passes the unit's
// IRenderableThing).
class CMCCannon : public CMotionController
{
public:
	CMCCannon(IRenderableThing *thing);											// 00495230
	virtual			~CMCCannon();												// 1 (00495280)
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (004952a0)
	virtual BOOL	Unknown8();													// 8 (the linker folds it into CMCComponent's, 00495770)

	static BOOL		HasTurret(CMesh *mesh);										// 004957d0: a "turret" or "barrel" part

	float	mLastYaw;															// +0x0c
	float	mLastPitch;															// +0x10
};

#endif
