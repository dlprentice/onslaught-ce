// TentacleAI.cpp: reconstructed from TentacleAI.obj (004f1240..004f18a4); Stuart's release has no copy. A tentacle's
// AI moves its head: it strikes at its target, rears up to strike, and settles back.

#include "common.h"
#include "component.h"
#include "UnitAI.h"
#include "eventmanager.h"
#include "game.h"
#include "unit.h"

#include <math.h>

// FUNCTION: 0x004f1240 init:ZERO_FVECTOR
// FUNCTION: 0x004f1260 init:ID_FMATRIX

//******************************************************************************************
// A fighting step, by the head's state: tracking (0) it turns the head to the target, wanders it about and sweeps its
// weapons; rearing (1) it lifts the head, then strikes (2) down and forward, and settles back once the head arrives.
// Only the tracking step sweeps and calls Unknown3: retail's other states jump straight to the event (004f1358,
// 004f13ad, 004f13bd, 004f141c, 004f14c3 -> 004f1862). Retail calls Elevation's Magnitude out of line (004f1525) by the
// inline-budget rule (README). The weapon call's target is read into a local first (`target`): retail's order of the two arguments. `head`
// lives in the block that reads it (retail's 0x68 frame); the head length scales the vectors in place (`v *= ...`),
// which keeps the final FVector constructor inline as retail does; the random wander components are locals of their
// own (retail converts each draw between the calls); case 2's distance is computed before its pitch store, which
// retail schedules between the squares.
void CTentacleAI::Unknown9(CEvent *event)	// FUNCTION: 0x004f1330
{
	CTentacle	*t = (CTentacle *)mForUnit;
	if (t->IsDying())
		return;

	switch (t->mUnknown2C0)
	{
	case 0:
		if (mTarget.ToRead())
		{
			FVector	head;
			t->GetHeadPos(&head);
			FVector	d = mTarget->GetPos() - head;
			t->mHeadAngles.mRoll = d.Azimuth() - t->mCurrentOrientation.mYaw;
			t->mUnknown2D0 = d.Elevation() - t->mCurrentOrientation.mPitch;
		}
		if ((t->mUnknown2E4 - t->mHeadOffset).MagnitudeSq() < 16.0f)
		{
			float	x = GAME.FloatRandom() * 30.0f - 15.0f;
			float	y = GAME.FloatRandom() * 20.0f + 10.0f;
			float	z = -(GAME.FloatRandom() * 40.0f + 3.0f);
			FVector	v(x, y, z);
			v.Normalise();
			v *= t->GetHeadLength();
			t->mUnknown2E4 = v;
		}
		if (!mForUnit->mActive)
			t->mUnknown2C0 = 1;
		if ((mForUnit->mUnitData->mSweeping) && (mTarget.ToRead()))
		{
			if (mForUnit->AnyWeaponUnknown50a290())
			{
				if (fabsf(mSweepYaw - mSweepYawEnd) > 0.001f)
					mSweepYaw += (mSweepYawEnd - mSweepYawStart) * (1.0f / 35.0f);
				if (fabsf(mSweepPitch - mSweepPitchEnd) > 0.001f)
					mSweepPitch += (mSweepPitchEnd - mSweepPitchStart) * (1.0f / 35.0f);
			}
			else
			{
				NewSweep();
				mTarget->GetTargetablePos(&mAimPos);
			}

			FVector	d = mAimPos - mForUnit->GetPos();
			FMatrix	ori(mSweepYaw, mSweepPitch, 0);
			SINT	target = (SINT)mTarget.ToRead();
			mForUnit->WeaponUnknown509140(target, (SINT)&(ori * d + mForUnit->GetPos()));
		}
		Unknown3();
		break;

	case 1:
		t->mUnknown2E4 = FVector(0, 20.0f, 5.0f);
		t->mHeadAngles.mRoll = 0;
		t->mUnknown2D0 = PI / 2 + 0.3f;
		if (mForUnit->mActive)
		{
			t->mUnknown2C0 = 2;
			FVector	dir(0, 20.0f, -40.0f);
			dir.Normalise();
			dir *= t->GetHeadLength();
			t->mUnknown2E4 = dir;
		}
		break;

	case 2:
		float	dist = (t->mUnknown2E4 - t->mHeadOffset).MagnitudeSq();
		t->mUnknown2D0 = PI / 3;
		if (dist < 16.0f)
			t->mUnknown2C0 = 0;
		break;
	}
	EVENT_MANAGER.AddEvent(3000, this, EVENT_MANAGER.GetTime(), START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
// Only its fighting steps (event 3000) reach it.
void CTentacleAI::HandleEvent(CEvent *event)	// FUNCTION: 0x004f1890
{
	if (event->GetEventNum() == 3000)
		Unknown9(event);
}
