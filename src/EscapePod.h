// EscapePod.h: CEscapePod, the pod a script launches from a thing's SpawnerF emitter (RTTI: a CActor, vtable
// 005dc830, 0xe8 bytes; IScript::SpawnEscapePod 005371e0; only what matched files need so far).

#ifndef ESCAPEPOD_H
#define ESCAPEPOD_H

#include "actor.h"
#include "ParticleManager.h"

class CEscapePod : public CActor
{
public:
	typedef CActor SUPERTYPE;
	virtual char	*_GetClassName() { return "CEscapePod"; }			// 7 (004bfcc0)
	virtual int		_GetClassID() { return 43; }						// 8 (004bfcd0); oids.h does not name it yet

	virtual void	Init(CInitThing *init);								// 9 (0044aab0)
	virtual float	GetMaxVelocity() { return 10.0f; }					// 15 (oids.obj's copy 004bfc50)
	virtual float	GetRadius() { return 10.0f; }						// 16 (folded with 15)
	virtual float	Gravity() { return 0; }								// 45 (004bfc60)
	virtual void	Move();												// 66 (0044ac20)

	CParticleEffectLink	mTrail;											// +0xe0
};

#endif
