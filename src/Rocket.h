// Rocket.h: CRocket, a rocket standing on its pad that a script launches (RTTI: a CActor, vtable 005dd458, 0x10c
// bytes; oids.obj constructs it and emits its inline members). Reconstructed with Rocket.obj (004d7b10..004d80e8).

#ifndef ROCKET_H
#define ROCKET_H

#include "actor.h"
#include "ParticleManager.h"

class CRocket : public CActor
{
public:
	typedef CActor SUPERTYPE;
	virtual char	*_GetClassName() { return "CRocket"; }				// 7 (004bfc40)
	virtual int		_GetClassID() { return 16; }						// 8 (the shared 0052dbf0); oids.cpp's OID_CRocket

	virtual	void	Shutdown();											// 2 (004d7c60)
	virtual void	Init(CInitThing *init);								// 9 (004d7b10)
	virtual void	AddShutdownEvent();									// 14 (004d7e60): the big explosion
	virtual float	GetMaxVelocity() { return 10.0f; }					// 15
	virtual float	GetRadius() { return 10.0f; }						// 16
	virtual void	Activate();											// 22 (004d8040): the engines light
	virtual void	Deactivate();										// 23 (004d80c0)
	virtual void	Damage(float amount, CThing *inByThis, BOOL inDamageShields = TRUE, int mesh_part_no = -1);	// 40 (004d7e30)
	virtual float	Gravity() { return 0; }								// 45
	virtual void	SetVulnerable(BOOL val) { mVulnerable = val; }		// 56 (004bfc70)
	virtual void	Move();												// 66 (004d7c90): climbs while active
	virtual bool	GetCanBeImpostered() { return false; }

	BOOL				mVulnerable;									// +0xe0
	BOOL				mActive;										// +0xe4: the init's mActive; Activate sets it
	float				mLife;											// +0xe8: 25; the damage it takes before it blows up
	CParticleEffectLink	mEngines[4];									// +0xec: on its "Engine" emitters 1..4
};

#endif
