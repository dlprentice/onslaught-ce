// MCComponent.h: CMCComponent, a component's motion controller (MCComponent.cpp; CComponent's sub-component builds it,
// 00427d16).

#ifndef MCCOMPONENT_H
#define MCCOMPONENT_H

#include "Motioncontroller.h"

// A component's motion controller (0x14 bytes, vtable 005dc2d8; CComponent's sub-component 00427d16 builds it).
class CMCComponent : public CMotionController
{
public:
	CMCComponent(IRenderableThing *thing);										// 00495930
	virtual			~CMCComponent();											// 1 (00495980)
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (004959a0)
	virtual BOOL	Unknown8();													// 8 (00495770, MCCannon.obj's copy)

	float	mLastYaw;															// +0x0c
	float	mLastPitch;															// +0x10
};

#endif
