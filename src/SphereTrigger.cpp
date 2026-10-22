// SphereTrigger.cpp: reconstructed from the retail object; its bodies are 004e5500..004e5838.
// The particle's name and the allocation source line below are present in those instructions.

#include "common.h"
#include "SphereTrigger.h"
#include "unit.h"

void CSphereTrigger::Init(CInitThing *init)	// FUNCTION: 0x004e5500
{
	mRadius = ((CSphereTriggerInitThing *)init)->mRadius;
	init->mInitCST.mNotSeekCollisionWithBF = THING_TYPE_STATIONARY;
	init->mInitCST.mMaxCollision = ECL_OUTER_SPHERE;
	init->mInitCST.mDesiredCollision = ECL_OUTER_SPHERE;
	init->mInitCST.mCollisionResponse = ECR_PASSIVE;
	mContainedTime = -1.0f;
	SUPERTYPE::Init(init);
}

void CSphereTrigger::Activate()	// FUNCTION: 0x004e5540
{
	if (IsObjective())
	{
		mEffect.Kill();
		PARTICLE_MANAGER.AddParticle(PARTICLE_SET.GetPD("Sphere Trigger Effect"), &mEffect, ZERO_FVECTOR, 0, 1);
		mEffect.SetPos(mPos);
	}
}

void CSphereTrigger::Deactivate()	// FUNCTION: 0x004e5680
{
	mEffect.Kill();
}

void CSphereTrigger::Shutdown()	// FUNCTION: 0x004e5690
{
	mContained.DeleteAll();
	Deactivate();
	SUPERTYPE::Shutdown();
}

// The first unit hit in a new frame clears the previous containment readers and records the new time.
static inline void BeginContainmentFrame(CSphereTrigger *trigger)
{
	if (trigger->mContainedTime != EVENT_MANAGER.GetTime())
	{
		trigger->mContained.DeleteAll();
		trigger->mContainedTime = EVENT_MANAGER.GetTime();
	}
}

void CSphereTrigger::Hit(CThing *other_thing, CCollisionReport *report)	// FUNCTION: 0x004e5700
{
	SUPERTYPE::Hit(other_thing, report);
	if (other_thing->IsA(THING_TYPE_UNIT))
	{
		BeginContainmentFrame(this);
#line 83
		mContained.Append(new(MEMTYPE_SPHERETRIGGER) CActiveReader<CUnit>((CUnit *)other_thing));
	}
}

// Activate's ZERO_FVECTOR reference is 0083d120, established by 004e5410.
// FUNCTION: 0x004e5410 init:ZERO_FVECTOR
// FUNCTION: 0x004e5430 init:ID_FMATRIX
