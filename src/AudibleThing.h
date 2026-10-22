// AudibleThing.h: IAudibleThing (vtable 005df6b8: CMonitor's three slots, then four pure sound queries).

#ifndef AUDIBLETHING_H
#define AUDIBLETHING_H
#include "maths.h"

#include "Monitor.h"

class CSample;
class CSoundEvent;

class IAudibleThing : public CMonitor
{
public:
	// no destructor of its own: CMessage's destructor goes straight to ~CMonitor without restoring this vtable (004b71ae)
	virtual	FVector	GetSoundPos() = 0;
	virtual	FMatrix	GetSoundOrientation() = 0;
	virtual FVector	GetSoundVelocity() = 0;
	virtual void	SampleFinishedPlaying(CSoundEvent* event) = 0;
};

#endif
