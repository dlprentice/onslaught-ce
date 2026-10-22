// mcbuggy.h: CMCBuggy (vtable 005dc250, 10 slots, 0xc8 bytes), the motion controller of a wheeled vehicle. Stuart's
// BattleEngine.cpp includes this header, and a commented-out line of his builds one (CMCBuggy(this),
// SetBuggyParameters); the release has no copy. CGroundVehicle::Init builds it on the unit's IRenderableThing when
// the mesh has a "WheelMotion" animation (0047d0a4) and passes the unit data's body motion (0047d0d3). Each frame it
// walks the mesh from its root: the body settles onto the smoothed ground normal, each axle takes the WheelMotion
// frame that puts its wheel nearest the ground, and each wheel spins by how far it has rolled.

#ifndef MCBUGGY_H
#define MCBUGGY_H

#include "Motioncontroller.h"
#include "maths.h"

class CMesh;
class CMeshPart;
class IRenderableThing;

class CMCBuggy : public CMotionController
{
public:
	CMCBuggy(IRenderableThing *thing);												// 00493020
	virtual			~CMCBuggy();														// 1 (004930a0)

	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (004944d0)
	virtual void	AdjustFrame(CMeshPart *part, float *frame);						// 5 (00494940)
	virtual BOOL	Unknown8();														// 8 (00494ae0)

	void			SetBuggyParameters(float body_motion);							// 00493180 (Stuart's name)
	void			Init(CMesh *mesh);												// 00493190
	void			UpdateWheel(FVector pos, FMatrix ori, CMeshPart *part, SINT wheel, SINT frame, float dt);	// 004934f0

	// The mesh optimizer keeps a buggy's moving parts apart (CMeshPart 004bae89, 004bb059; CMesh 004bb216).
	static BOOL		CanMergePart(CMeshPart *part);									// 00494b00: not the body, an axle or a wheel
	static BOOL		HasWheelMotion(CMesh *mesh);									// 00494b50

	FVector			*mWheelPos;					// +0x0c: each wheel's last world position
	float			*mWheelSpin;				// +0x10: each wheel's roll angle
	SINT			mUnknown14;					// +0x14
	SINT			mNumWheels;					// +0x18: its "WheelBase" emitters
	SINT			mNumFrames;					// +0x1c: WheelMotion's frames; 0 until Init
	SINT			mFirstFrame;				// +0x20: WheelMotion's first frame
	FMatrix			*mPartOri;					// +0x24: each part's orientation this frame
	FVector			*mPartPos;					// +0x28
	float			*mPartFrame;				// +0x2c
	FMatrix			*mOldPartOri;				// +0x30: and last frame's
	FVector			*mOldPartPos;				// +0x34
	float			*mOldPartFrame;				// +0x38: -1 until a part has two
	float			mLastTime;					// +0x3c: when the parts were last updated
	FMatrix			mBodyOri;					// +0x40: the body's orientation on the ground
	FVector			mBodyPos;					// +0x70
	FMatrix			mBodyParentOri;				// +0x80
	FVector			mSmoothNormal;				// +0xb0: the ground normal, smoothed
	float			mBodyMotion;				// +0xc0
	FVector			*mWheelOffsets;				// +0xc4: each wheel's offset for each WheelMotion frame
};

#endif
