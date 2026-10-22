// TentacleGuide.cpp: reconstructed from TentacleGuide.obj (004f18b0..004f1dcc); Stuart's release has no copy. A
// tentacle's guide eases its head a tenth of the way towards the yaw and pitch its AI sets, each step, and springs the
// head's offset back towards its rest position, faster while it strikes (state 2).

#include "common.h"
#include "component.h"
#include "Guide.h"

#include <math.h>

// FUNCTION: 0x004f18b0 init:ZERO_FVECTOR
// FUNCTION: 0x004f18d0 init:ID_FMATRIX

//******************************************************************************************
// The AI's target yaw is kept in mHeadAngles.mRoll (+0x2cc) and its target pitch in mUnknown2D0 (CTentacleAI
// 004f151f, 004f1556). The inline budget is spent just before the last += (004f1dc3), which is why it stays out of
// line: this function's size is at the limit (569), so a further statement or accessor expands it. The head's offset
// and velocity are read as members, not through references: references let the compiler keep the sums in temporaries
// instead of storing and reloading them as retail does, and the difference is written out in each state.
void CTentacleGuide::Unknown3()	// FUNCTION: 0x004f19a0
{
	CTentacle	*t = (CTentacle *)mUnit;
	CUnit		*parent = t->mParent.ToRead();
	if ((!parent) || (t->IsDying()) || (parent->IsDying()) || (parent->IsDeploying()))
		return;

	CEulerAngles	&head = t->mHeadAngles;
	float	target = head.mRoll;
	if (target > PI)
		target -= PI_M2;
	else if (target < -PI)
		target += PI_M2;

	float	step = (float)fabs(AngleDifference(head.mYaw, target)) * 0.1f;
	if (target > head.mYaw)
		head.mYaw += step;
	else
		head.mYaw -= step;
	if (head.mYaw > PI)
		head.mYaw = PI;
	else if (head.mYaw < -PI)
		head.mYaw = -PI;

	step = (float)fabs(AngleDifference(head.mPitch, t->mUnknown2D0)) * 0.1f;
	if (head.mPitch < t->mUnknown2D0)
	{
		if (t->mUnknown2D0 - head.mPitch > PI)
			head.mPitch -= step;
		else
			head.mPitch += step;
	}
	else
	{
		if (head.mPitch - t->mUnknown2D0 > PI)
			head.mPitch += step;
		else
			head.mPitch -= step;
	}

	switch (t->mUnknown2C0)
	{
	case 2:
		t->mUnknown2F4 += (t->mUnknown2E4 - t->mHeadOffset) * 0.01f;
		if (t->mUnknown2F4.Magnitude() > 0.5f)
		{
			t->mUnknown2F4.Normalise();
			t->mUnknown2F4 *= 0.5f;
		}
		break;
	default:
		t->mUnknown2F4 += (t->mUnknown2E4 - t->mHeadOffset) * 0.001f;
		if (t->mUnknown2F4.Magnitude() > 0.1f)
		{
			t->mUnknown2F4.Normalise();
			t->mUnknown2F4 *= 0.1f;
		}
		break;
	}
	t->mHeadOffset += t->mUnknown2F4;
}
