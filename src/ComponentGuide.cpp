// ComponentGuide.cpp: reconstructed from ComponentGuide.obj (00429180..004294a6); Stuart's release has no copy. A
// component's guide turns it on its mount towards its destination, a tenth of the way (at most its turn rate) each
// step, within its maximum yaw either side.

#include "common.h"
#include "component.h"
#include "Guide.h"

// FUNCTION: 0x00429180 init:ZERO_FVECTOR
// FUNCTION: 0x004291a0 init:ID_FMATRIX

//******************************************************************************************
// The clamp's bracketed read is a value of its own (README 'How a value is read'): retail loads the maximum yaw for it
// after the compare (0042946b), which a named local or the plain read did not.
void CComponentGuide::Unknown3()	// FUNCTION: 0x00429270
{
	CComponent	*c = (CComponent *)mUnit;
	CUnit		*parent = c->mParent.ToRead();
	if ((!parent) || (c->IsDying()) || (parent->IsDying()) || (parent->IsDeploying()))
		return;

	FVector	d = mDestination - mUnit->GetPos();
	float	mount = c->GetMountedYaw();
	float	target;
	if (!mMode)
		target = mount;
	else
		target = d.Azimuth();

	c->mUnknown250 = mount + c->mUnknown250;
	if (c->mUnknown250 > PI)
		c->mUnknown250 -= PI_M2;
	else if (c->mUnknown250 < -PI)
		c->mUnknown250 += PI_M2;

	float	step = (float)fabs(AngleDifference(c->mUnknown250, target)) * 0.1f;
	float	turn = c->mUnitData->mTurnRate;
	if (step > turn)
		step = c->mUnitData->mTurnRate;
	if (target > c->mUnknown250)
	{
		if (target - c->mUnknown250 > PI)
			c->mUnknown250 = c->mUnknown250 - step;
		else
			c->mUnknown250 = step + c->mUnknown250;
	}
	else
	{
		if (c->mUnknown250 - target > PI)
			c->mUnknown250 = step + c->mUnknown250;
		else
			c->mUnknown250 = c->mUnknown250 - step;
	}

	c->mUnknown250 = c->mUnknown250 - mount;
	if (c->mUnknown250 > PI)
		c->mUnknown250 -= PI_M2;
	else if (c->mUnknown250 < -PI)
		c->mUnknown250 += PI_M2;

	if (c->mUnitData->mMaxYaw < c->mUnknown250)
		c->mUnknown250 = (c->mUnitData->mMaxYaw);
	else if (-c->mUnitData->mMaxYaw > c->mUnknown250)
		c->mUnknown250 = -c->mUnitData->mMaxYaw;
}
