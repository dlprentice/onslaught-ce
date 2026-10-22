// component.h: CComponent (0x2c0 bytes, vtable 005e3d40), a unit that is part of another, and the two component
// classes UPhysicsManager::SpawnCharacter makes besides it (0050fa40): CGillMHead (0x2c0, vtable 005e41f8) and
// CTentacle (0x310, vtable 005e3f9c). Stuart's release has no component headers. CComponent adds slots
// 117..119 to CUnit's; its members from SpawnCharacter's inline constructors: a particle effect link +0x258, an
// active reader +0x26c (the unit it belongs to: its maximum velocity is that unit's, 0050fcd0) and three
// vectors +0x27c. The inline members below are the ones WorldPhysicsManager.obj emits (0050fc90..0050fe90).

#ifndef COMPONENT_H
#define COMPONENT_H

#include "unit.h"
#include "activereader.h"

DECLARE_UNIT_CLASS(CComponent, CUnit)
	virtual void	Init(CInitThing *init);									// 9 (00427b80)
			void	Unknown428b50(CUnit *owner, SINT mount);				// 00428b50: attaches it to its owner at a mount (CUnit::Init 004f8d7c)
			float	GetMountedYaw();										// 00428bc0: its parent's yaw plus its own offset
			void	PlayHit();												// 00428cb0: the "hit" animation
			void	RefreshCachedTransform();								// 00428500: its place on its parent
	virtual BOOL	OnScanner() { return mUnknown1F4 != NULL; }			// 105
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_40000000 | THING_TYPE_COMPONENT); }	// 38
	virtual float	GetMaxVelocity() { if (mParent.ToRead()) return mParent->GetMaxVelocity(); return 13.0f; }	// 15
	virtual void	Activate();												// 22 (00428d50)
	virtual	FVector	GetVelocity();											// 27 (004287c0)
	virtual BOOL	ClipToGround();											// 44 (00428c50)
	virtual float	Gravity();												// 45 (00428c40)
	virtual BOOL	CanGoUnderWater();										// 49 (004dfcb0)
	virtual BOOL	StartDieProcess();										// 50 (00428800)
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59 (004289b0)
	virtual void	Move();													// 66 (00428110)
	virtual void	DeclareOnGround();										// 68 (0044d1f0)
	virtual void	DeclareInWater();										// 69 (00428c70)
	virtual float	Unknown73();											// 73 (00428c30)
	virtual void	Unknown77(CEulerAngles *cur, CEulerAngles *target, CEulerAngles *rate, FMatrix &out);	// 77 (00428be0)
	virtual BOOL	Unknown84();											// 84 (00428c90)
	virtual void	GetTargetablePos(FVector *out);								// 90 (00428d30)
	virtual float	Unknown92();											// 92 (004284f0)
	virtual void	Unknown107(float a);									// 107 (00428cf0)
	virtual void	Unknown117();											// 117 (00427cd0)
	virtual void	Unknown118();											// 118 (00427d50)
	virtual void	Unknown119(CInitThing *init);							// 119 (00427dd0)

	// IRenderableThing
	virtual	FVector	GetRenderPos();											// 0 (00428710)
	virtual	FMatrix	GetRenderOrientation();									// 1 (00428770)
	virtual bool	GetCanBeImpostered() { return false; }					// 18 (folded into 00453a80)

	float				mUnknown250;										// +0x250: its own yaw on the mount (RefreshCachedTransform 0042853d)
	float				mUnknown254;										// +0x254: and pitch
	CParticleEffectLink	mEffect;											// +0x258
	SINT				mUnknown260;										// +0x260
	BOOL				mUnknown264;										// +0x264: it has an "Activate" animation to finish (Activate 00428d6e)
	float				mUnknown268;										// +0x268: when it last (de)activated
	CActiveReader<CUnit>	mParent;										// +0x26c
	SINT				mNumber;											// +0x270: its mount number (CUnit::FindChild compares it, 004fd8e9; IScript::GetNumber 0053598f)
	float				mYawOffset;											// +0x274: its yaw relative to its parent's (00428ba3)
	SINT				mUnknown278;										// +0x278: 99999 after Init
	FVector				mUnknown27C[3];										// +0x27c
	UBYTE				mUnknown2AC[0x2bc - 0x2ac];
	BOOL				mUnknown2BC;										// +0x2bc: PlayHit sets it (00428cd7)
};

DECLARE_UNIT_CLASS(CTentacle, CComponent)							// 0x310
	virtual void	Init(CInitThing *init);									// 9 (004f0630)
	virtual void	Activate();												// 22 (004f0e40)
	virtual BOOL	StartDieProcess();										// 50 (004f1050)
	virtual void	Move();													// 66 (004f09b0)
	virtual void	GetTargetablePos(FVector *out);								// 90 (004f0d90)
	virtual void	Unknown117();											// 117 (004f0760)
	virtual void	Unknown118();											// 118 (004f07e0)
	virtual void	Unknown119(CInitThing *init);							// 119 (004f0860)

	// Where its motion controller puts the head (CMCTentacle::UpdateSpline 0049dd6e, 0049dd79).
			void	GetHeadPos(FVector *pos);								// 004f0ba0: mPos + mOrientation * mHeadOffset
			void	GetHeadOri(FMatrix *ori);								// 004f0c50
			float	GetHeadLength();										// 004f1220 (Init inlines it; CTentacleAI 004f1483 calls it)

	// The four fields from +0x2c0 were an FVector here: CEulerAngles' empty inline constructor spends the same budget
	// that leaves CComponent's reader constructor out of line for CTentacle (0050fbcf).
	SINT			mUnknown2C0;											// +0x2c0: 1 after Init
	CEulerAngles	mHeadAngles;											// +0x2c4: (0, PI/2 + 0.25, 0) after Init; GetHeadOri turns by it
	float			mUnknown2D0;											// +0x2d0: PI/2 + 0.25 after Init, as the pitch
	FVector			mHeadOffset;											// +0x2d4: (0, GetHeadLength(), 0.25)
	FVector			mUnknown2E4;											// +0x2e4: the same after Init
	FVector			mUnknown2F4;											// +0x2f4: zero after Init
	SINT			mBonesLeft;												// +0x304: bones still to blow off while it dies
	float			mNextBoneTime;											// +0x308: when the next one goes
	BOOL			mBigArm;												// +0x30c: a "Sentinel Big Arm"
};
DECLARE_UNIT_CLASS(CGillMHead, CComponent)							// 0x2c0
	virtual void	Init(CInitThing *init);									// 9 (0047a730)
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_NAMED_MESH); }	// 38
	virtual void	SetAIState(EAIState inAIState);							// 54 (0047a9c0)
	virtual BOOL	FinishedPlayingCurrentAnimation();						// 59 (0047a900)
	virtual BOOL	Unknown86();											// 86 (0047a8b0)
	virtual void	Unknown119(CInitThing *init);							// 119 (0047a760)
};


#endif
