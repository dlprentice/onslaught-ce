// feature.h: CFeature (vtable 005e45e0, 72 slots; 0xf4 bytes), a CActor, only what matched files need. Its inline
// members are emitted by WorldPhysicsManager.obj, whose SpawnFeature constructs one (00510089); the rest of the
// class lives in its own object (Init 0044ca30, Shutdown 0044cbe0).

#ifndef FEATURE_H
#define FEATURE_H

#include "actor.h"
#include "WorldPhysicsManager.h"

DECLARE_THING_CLASS(CFeature, CActor)
public:
	virtual	void			Shutdown();												// 0044cbe0
	virtual void			Init(CInitThing *init);									// 0044ca30
	virtual BOOL			GetCanBeStaticallyShadowed()	{ return TRUE; }		// 10 (the shared 004014a0)
	virtual float			GetMaxVelocity()				{ return 1.0f; }		// 15 (the shared 004de700)
	virtual	void			SetThingType(ULONG t)		 { SUPERTYPE::SetThingType(t | THING_TYPE_UNKNOWN_20 | 0x00500000); }
	virtual CStaticShadow	*GetStaticShadow()				{ return mStaticShadow; }
	virtual void			SetStaticShadow(CStaticShadow *shadow)	{ mStaticShadow = shadow; }
	virtual void			AddShutdownEvent();										// 0044cdb0
	virtual void			Render(DWORD flags = 0);								// 0044d210
	// The bits past CActor's: CFeature ORs 0x80500023 in all (00510114).
	virtual void			Damage(float amount, CThing *inByThis, BOOL inDamageShields = TRUE, int mesh_part_no = -1);	// 0044cd20
	virtual BOOL			ClipToGround();											// 0044d1d0
	virtual BOOL			CanGoUnderWater();										// 00415a90 (identical code folded)
	virtual BOOL			StartDieProcess();										// 0044cd80
	virtual void			Move();													// 0044cc10
	virtual	void			DeclareOnGround();										// 0044d1f0
	virtual BOOL			IsLockable()					{ return mData->mUnknown14; }	// 71: HandleAutoAim calls +0x11c at 0040bbbc
			// IsLockable is the existing slot 71, not a separate nonvirtual method.
			SINT			GetAllegiance() { return mAllegiance; }		// 0040bbca: +0xec

#ifdef FEATURE_CPP	// feature.cpp's view; the other objects keep theirs (declaration counts)
			void			SpawnPickup();											// 0044cee0: now and then, as it falls

	float					mLife;					// +0xe0: from the data (Init 0044cb39)
	CFeatureData			*mData;					// +0xe4
	BOOL					mAboveWater;			// +0xe8: set by Init when the water is below it (0044cb59)
	SINT					mAllegiance;			// +0xec: the init thing's (0044cb30)
#else
	UBYTE					mUnknownActor[0xe4 - sizeof(CActor)];
	CFeatureData			*mData;					// +0xe4
	UBYTE					mUnknowne8[0xec - 0xe8];
	SINT					mAllegiance;			// +0xec (CHud::RenderWorldTargets 004878ae)
#endif
	CStaticShadow			*mStaticShadow;			// +0xf0
};

#endif
