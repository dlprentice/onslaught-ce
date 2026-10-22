// MCBattleEngine.h: CMCBattleEngine, the battle engine's walker motion controller (vtable 005d88ec, 15 slots, 0xf0
// bytes), and its base CMCMech (vtable 005dc3b4). Stuart's headers are not in the release; CBattleEngine::Init
// builds one inline (004053c7..0040540d: CMCMech's constructor on the engine's IRenderableThing, then the vtable).

#ifndef MCBATTLEENGINE_H
#define MCBATTLEENGINE_H

#include "Motioncontroller.h"
#include "maths.h"

class IRenderableThing;
class CBattleEngine;
class CMesh;
class CMeshPart;
class CEvent;

// What every mech sharing a mesh shares (0x14 bytes; the first to Init builds it, the last destructor frees it).
struct SMechShared
{
	SMechShared() { mLegFrames = NULL; mLegOris = NULL; mToeOffsets = NULL; mPartTypes = NULL; mPartLegs = NULL; }	// 00498cbf..00498cca

	float	**mLegFrames;					// +0x00: each LegMotion frame's value for each leg
	FMatrix	**mLegOris;						// +0x04: each LegMotion frame's orientation for each leg
	FVector	**mToeOffsets;					// +0x08: each toe's offset for each ToeMotion frame
	SINT	*mPartTypes;					// +0x0c: each part's kind: 1 body, 2 leg, 3 foot, 4 toe, 5 turret, 0 other
	SINT	*mPartLegs;						// +0x10: the leg (or toe) number a part's name carries
};

class CMCMech : public CMotionController
{
public:
	CMCMech(IRenderableThing *thing);						// 004983b0
	virtual void	HandleEvent(CEvent *event);				// 0 (00498870)
	virtual			~CMCMech();								// 1 (00498530; the deleting destructor is 00498510)
	virtual void	GetPartTransform(CMeshPart *part, FVector *pos, FMatrix *ori, bool world);	// 4 (0049be00)
	virtual void	AdjustFrame(CMeshPart *part, float *frame);	// 5 (0049c1d0)
	virtual BOOL	Unknown8();								// 8 (0049c240)
	virtual BOOL	Unknown10() { return TRUE; }			// 10 (the shared 004014a0)
	virtual float	Unknown11() { return 0; }				// 11 (the shared 004bfc60)
	virtual float	Unknown12() { return 0; }				// 12 (the shared 004bfc60)
	virtual SINT	Unknown13() { return 90; }				// 13 (00405a00, emitted in BattleEngine.obj)
	virtual BOOL	Unknown14() { return FALSE; }			// 14 (the shared 00405930): trace the ground under a foot

	void	SetMechParameters(float a, float b, float c, float d, float e, float f, int g);	// 00498bf0 (Stuart's name)
	// Stuart's name; inline (CBattleEngine::Init stores +0x2c directly, 00405486)
	void	SetAnimationBlend(float blend) { mAnimationBlend = blend; }
	void	ProcessMovement(BOOL force, BOOL fixed_step = FALSE);	// 004988b0 (Stuart's name; CBattleEngine 0040652f passes TRUE, 0)
	void	Init(CMesh *mesh);								// 00498c40
	float	GetFootHeight(FVector *pos, SINT leg, BOOL no_trace);	// 00499bc0 (UpdateBone passes the leg, 0049a883)
	// Stuart's name (CMCBattleEngine's in his calls); CMCMech's, taking the movement by reference (CBattleEngine::Move 0040944b)
	void	AddMovement(const FVector &movement);			// 00499d60
	void	UpdateBone(FVector *pos, FMatrix *ori, CMeshPart *part, SINT *leg, SINT *frame, float dt, SINT flags);	// 00499e30
	void	UpdateBoneHierarchyRecursive(FVector pos, FMatrix ori, CMeshPart *part, SINT leg, SINT unused, float dt, SINT flags);	// 0049bd50

	// The mesh optimizer keeps a mech's moving parts apart (CMeshPart 004bae70, CMesh 004bb210).
	static BOOL		CanMergePart(CMeshPart *part);			// 0049c250: not the body, a leg, a foot, a toe or the turret
	static BOOL		HasLegMotion(CMesh *mesh);				// 0049c2d0

	static CMCMech	*sFirst;								// 00704650: every mech, through mNext

	float			mParamD;			// +0x0c: 0.4
	float			mParamE;			// +0x10: 0.9
	FVector			*mLegPos;			// +0x14: each leg's
	BOOL			*mLegA;				// +0x18: the foot's landing was reported (UpdateBone stores 1, 0049ac81)
	float			*mLegB;				// +0x1c: the foot's lift while it steps
	SINT			*mLegC;				// +0x20: the step's phase in degrees, 0 when planted (0..180; 0049a948)
	FVector			*mLegTarget;		// +0x24
	SINT			mUnknown28;			// +0x28
	float			mAnimationBlend;	// +0x2c
	FMatrix			*mPartOri;			// +0x30: each part's orientation this frame
	FVector			*mPartPos;			// +0x34
	float			*mPartFrame;		// +0x38
	FMatrix			*mOldPartOri;		// +0x3c: and last frame's
	FVector			*mOldPartPos;		// +0x40
	float			*mOldPartFrame;		// +0x44: -1 until a part has two
	SINT			mUnknown48;			// +0x48
	float			mLastTime;			// +0x4c: when the bones were last processed
	BOOL			mUsed;				// +0x50: a transform was asked for since then
	BOOL			mWasUsed;			// +0x54
	FMatrix			mBodyOri;			// +0x58: the body's lean, which the legs undo (UpdateBone 0049a763, 0049ae22)
	FVector			mUnknown88;			// +0x88
	float			mParamA;			// +0x98: 0
	float			mParamB;			// +0x9c: 200 (400 for the battle engine)
	float			mParamC;			// +0xa0: 1
	float			mParamF;			// +0xa4: 0.05
	SINT			mNumLegs;			// +0xa8: its "Footbase" emitters
	SINT			mNumFrames;			// +0xac: LegMotion's frames; 0 until Init
	SINT			mFirstFrame;		// +0xb0: LegMotion's first
	SINT			mToeFirstFrame;		// +0xb4: ToeMotion's first and last frames
	SINT			mToeLastFrame;		// +0xb8
	SINT			*mToeFrames;		// +0xbc: each leg's toes' frames
	SINT			mNumToes;			// +0xc0: each leg's "toestop" emitters
	SINT			mParamG;			// +0xc4: 2
	BOOL			mNeedsUpdate;		// +0xc8
	SINT			mProcessCount;		// +0xcc
	SINT			mUnknownD0;			// +0xd0
	FVector			mUnknownD4;			// +0xd4
	SMechShared		*mShared;			// +0xe4
	CMesh			*mMesh;				// +0xe8
	CMCMech			*mNext;				// +0xec
};

// A GillM's motion controller (vtable 005dbc74), built inline by CGillM::InitLegMotion (00479ab9). Its body pitches and
// rolls with the ground (CGillM 00479f30, 0047a0b0).
class CMCGillM : public CMCMech
{
public:
	CMCGillM(IRenderableThing *thing) : CMCMech(thing) {}
	// Retail 005dbc74 slot 10 overrides CMCMech's TRUE with FALSE.
	// FUNCTION: 0x00405930
	virtual BOOL	Unknown10() { return FALSE; }
	virtual float	Unknown11();										// 11 (004963a0)
	virtual float	Unknown12();										// 12 (004963c0)
	virtual SINT	Unknown13();										// 13 (004f4820)
};

// A Thunderhead's motion controller (vtable 005df890), built inline by CThunderHead::InitLegMotion (004f4799). Its
// slot 13 is inline; ThunderHead.obj emits it and the linker folds CMCGillM's into it (004f4820).
class CMCThunderHead : public CMCMech
{
public:
	CMCThunderHead(IRenderableThing *thing) : CMCMech(thing) {}
	// Retail 005df890 slot 10 overrides CMCMech's TRUE with FALSE.
	// FUNCTION: 0x00405930
	virtual BOOL	Unknown10() { return FALSE; }
	virtual float	Unknown11();										// 11 (0049ee70)
	virtual SINT	Unknown13();									// 13 (out of line, ThunderHead.cpp: retail folds CMCGillM::Unknown13 into it)
};

// Built inline by CBattleEngine::Init on the engine's IRenderableThing (004053f4..0040540d); its vtable 005d88ec is
// emitted in BattleEngine.obj, and its destructor is a jump to CMCMech's (00405a10).
class CMCBattleEngine : public CMCMech
{
public:
	CMCBattleEngine(CBattleEngine *engine) : CMCMech(engine) {}

	virtual BOOL	Unknown10() { return FALSE; }						// 10 (00405930)
	virtual float	Unknown11();										// 11 (00492f10: the engine's CalculatePitch)
	virtual BOOL	Unknown14();										// 14 (MCBattleEngine.cpp; folded into 0040ac30)
};

#endif
