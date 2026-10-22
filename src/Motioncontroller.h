// Motioncontroller.h: CMotionController (vtable 005dc778, 10 slots; only what matched files need).

#ifndef MOTIONCONTROLLER_H
#define MOTIONCONTROLLER_H

#include "Monitor.h"
#include "maths.h"

class CMeshPart;

class CMotionController : public CMonitor
{
public:
	CMotionController();											// 004bae30 (CMCBuggy's constructor calls it, 00493023)
	virtual			~CMotionController();							// 004bae50 (CMCBuggy's destructor, 00493165)

	virtual void	Unknown3(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 3 (004bae60: nothing)
	// A part's position and orientation, interpolated to the render frame; world takes them out of the thing's space
	// (CMCBuggy 004944d0, CMCMech 0049be00).
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4
	// Adjusts a part's animation frame (CMeshPart::ResolveFrameNumber 004b2534 calls it with the part and the frame).
	virtual void	AdjustFrame(CMeshPart *part, float *frame);		// 5
	virtual void	Unknown6(CMeshPart *part, SINT *flags);			// 6: bit 0 hides the part from collision (004ac848)
	virtual SINT	Unknown7(CMeshPart *part);						// 7: the part's damage level (004ac852)
	virtual BOOL	Unknown8();										// 8: before a pose is rebuilt (CMeshPose::Update 004b4d6f ignores the result; CMCBuggy's says whether time moved, 00494ae0)
	virtual BOOL	Unknown9();										// 9 (the shared 005019c0: FALSE): the animation loops (EvaluateAnimatedTransform 004b54e6)

#if defined(MOTIONCONTROLLER_CPP) || defined(MESH_CPP)	// the optimizer's callers; the other includers keep their declaration counts
	// The mesh optimizer's tests over every controller's (CMeshPart and CMesh call them).
	static BOOL		CanOptimisePart(CMeshPart *part);				// 004bae70
	static BOOL		CanMergePart(CMeshPart *part);					// 004bb040
	static BOOL		HasMovingParts(class CMesh *mesh);				// 004bb210
#endif
	// Stuart's (BattleEngine.cpp); inline (CBattleEngine::MassiveHackPutUsInRightMesh 004064a1, 004064f8)
	void			SetThing(class IRenderableThing *thing) { mThing = thing; }

	class IRenderableThing	*mThing;								// +0x08
};

// A dropship's motion controller (0x14 bytes; CDropship::Init 00446e96 passes the unit's IRenderableThing).
class CMCDropship : public CMotionController
{
public:
	CMCDropship(class IRenderableThing *thing);		// 00496090
#ifdef MCDROPSHIP_CPP	// MCDropship.cpp's view (vtable 005dc304); the other includers keep their declaration counts
	virtual			~CMCDropship();									// 1 (004960e0)
	virtual void	AdjustFrame(CMeshPart *part, float *frame);		// 5 (00496100): the doors' frame
	virtual BOOL	Unknown8();										// 8 (00496200)

	static BOOL		CanMergePart(CMeshPart *part);					// 00496250: not a door
	static BOOL		HasDoorAnimation(class CMesh *mesh);			// 00496270

	SINT	mLastDoorState;											// +0x0c
	float	mLastDoorOpen;											// +0x10
#else
	UBYTE	mUnknown0C[0x14 - 0x0c];
#endif
};

// A mine's motion controller (0x10 bytes, vtable 005dc3f4): it sinks the mesh's root by the mine's depth,
// interpolated to the render frame.
class CMCMine : public CMotionController
{
public:
	CMCMine(class IRenderableThing *thing);										// 0049c3e0
	virtual			~CMCMine();													// 1 (0049c420)
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (0049c440)
	virtual BOOL	Unknown8();													// 8 (0049c4b0)

	float	mLastDepth;															// +0x0c: the mine's depth when last posed
};

// The Warspite dome's motion controller (0xc bytes, vtable 005dc484; CWarspiteDome::Init 00504918 passes the unit's
// IRenderableThing).
class CMCWarspiteDome : public CMotionController
{
public:
	CMCWarspiteDome(class IRenderableThing *thing);								// 0049ef80
#ifdef MCWARSPITEDOME_CPP	// MCWarspiteDome.cpp's view (vtable 005dc484); the other includers keep their counts
	virtual			~CMCWarspiteDome();											// 1 (0049efc0)
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (0049efe0)
	// Retail 005dc484 slot 8 returns TRUE, unlike CMotionController's FALSE.
	// FUNCTION: 0x004014a0
	virtual BOOL	Unknown8() { return TRUE; }

	// The mesh optimizer keeps the dome's moving parts apart (MotionController.obj 004bae70, 004bb040).
	static BOOL		CanMergePart(CMeshPart *part);								// 0049f600: not the dome, a barrel, the spinner or a gun
	static BOOL		HasDome(class CMesh *mesh);									// 0049f670: a part named as one of those
#endif
};

// A sentinel's motion controller (0x14 bytes, vtable 005dc420): it turns the "X1 turret" part by the unit's yaw and
// the "X1 barrel" part by its pitch, each interpolated to the render frame.
class CMCSentinel : public CMotionController
{
public:
	CMCSentinel(class IRenderableThing *thing);									// 0049c5d0
	virtual			~CMCSentinel();												// 1 (0049c620)
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (0049c640)
	virtual BOOL	Unknown8();													// 8 (the linker folds it into CMCComponent's, 00495770)

	float	mLastYaw;															// +0x0c
	float	mLastPitch;															// +0x10
};

// The hive boss's motion controller (0x78 bytes; CHiveBoss::Init 0047fed8 builds it on the unit itself). MCHiveBoss.cpp
// declares it in full, on CDestructableSegmentsMotionController.
#ifndef MCHIVEBOSS_CPP
class CMCHiveBoss : public CMotionController
{
public:
	CMCHiveBoss(class CHiveBoss *boss);											// 00497090

	UBYTE	mUnknown0C[0x78 - 0x0c];
};
#endif

// A ground vehicle's motion controller when its mesh has no "WheelMotion" (0x14 bytes; CGroundVehicle::Init 0047d110).
class CMCGroundVehicle : public CMotionController
{
public:
	CMCGroundVehicle(class IRenderableThing *thing);							// 00496a50
#ifdef MCGROUNDVEHICLE_CPP	// MCGroundVehicle.cpp's view (vtable 005dc35c); the other includers keep their counts
	virtual			~CMCGroundVehicle();										// 1 (00496aa0)
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (00496ac0)
	virtual BOOL	Unknown8();													// 8 (the linker folds it into CMCComponent's, 00495770)

	static BOOL		CanMergePart(CMeshPart *part);								// 00496f60: not the turret or a barrel

	float	mLastYaw;															// +0x0c
	float	mLastPitch;															// +0x10
#else
	UBYTE	mUnknown0C[0x14 - 0x0c];
#endif
};

#endif
