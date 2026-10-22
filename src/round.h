// round.h: CRound (vtable 005de82c, 72 slots; 0x134 bytes), a CActor, only what matched files need. Its destructor
// is implicit: CMissile's (0050f8d0) inlines the members' and calls ~CActor, and Round.obj emits CRound's own copy
// (004d8370). The constructor from a round definition is 004d81e0.

#ifndef ROUND_H
#define ROUND_H

#include "actor.h"
#include "activereader.h"
#include "ParticleManager.h"

class CRoundData;
class CResourceAccumulator;

DECLARE_THING_CLASS(CRound, CActor)
public:
							CRound(CRoundData *data);						// 004d81e0

	virtual void			HandleEvent(CEvent *event);						// 004d9910
	virtual	void			Shutdown();										// 004d8dc0
	virtual void			Init(CInitThing *init);							// 004d8410
	virtual float			GetMaxVelocity();								// 004d82a0
	virtual float			GetRadius();									// 004d8ac0
	virtual	void			SetThingType(ULONG t);							// 004d8320
	virtual void			Hit(CThing *other_thing, CCollisionReport *report);	// 004d8ae0
	virtual SINT			GetSoundMaterial();								// 004d8340
	virtual BOOL			ClipToGround();									// 004d82e0
	virtual float			Gravity();										// 004db600
	virtual	float			BounceFactor();									// 004d82d0
	virtual BOOL			CanGoUnderWater();								// 004d8330
	virtual BOOL			StartDieProcess();								// 004db130
	virtual void			Move();											// 004d8e40
	virtual	void			DeclareOnGround();								// 004d9dd0
	virtual void			DeclareInWater();								// 004d9ef0
	virtual void			AccumulateResources(CResourceAccumulator *accumulator, DWORD flags = 0);	// 71: empty, folded into the shared 00452da0 (CMissile calls it out of line, 004bac2b); Stuart's name

			void			Unknown4d9f30(SINT mode, CCollisionReport *report, CThing *thing);	// 004d9f30 (Hit passes 3, its report and the thing hit)
			void			Unknown4db630();								// 004db630: arms the round (DeclareInWater 004d9ef3)
			void			SetTarget(CThing *target, BOOL release);		// 004daab0
			void			ReleaseTarget();								// 004dab50 (Shutdown inlines it)
			float			GetExplosionRadius();							// 004db090
			float			GetExplosionUnknown34();						// 004daff0
			CThing			*FindTargetNear();								// 004daba0
			void			Unknown4dac90(CScheduledEvent *reuse);			// 004dac90: chooses the target (Init ends with it; HandleEvent's 4003 passes its event on)
			void			Unknown4db150();								// 004db150: HandleEvent's 4002

	CParticleEffectLink		mEffect;				// +0xe0
	CActiveReader<CThing>	mUnknownE8;				// +0xe8
	CActiveReader<CThing>	mUnknownEC;				// +0xec
	CRoundData				*mData;					// +0xf0 (the constructor's argument)
	float					mStartTime;				// +0xf4
	FVector					mBeamStart;				// +0xf8: a beam's start (Init 004d852e)
	FVector					mDest;					// +0x108 (Init: the init thing's mDest, or where the flight meets the ground)
	SINT					mJumpsPerformed;		// +0x118
	EAllegiance				mAllegiance;			// +0x11c: the init thing's
#ifdef FEARGRID_CPP	// the fear grid reads the landing point through an accessor (CFearGrid::Rebuild 0044c49f..0044c580)
	FVector					GetTargetPos() { return mDest; }
#endif
	BOOL					mUnknown120;			// +0x120: it cannot start dying (StartDieProcess 004db130)
	SINT					mUnknown124;			// +0x124
	SINT					mUnknown128;			// +0x128
	BOOL					mUnknown12C;			// +0x12c: no gravity when the data's +0x6c is set (Gravity 004db60d)
	SINT					mUnknown130;			// +0x130: 1 at construction
};

#endif
