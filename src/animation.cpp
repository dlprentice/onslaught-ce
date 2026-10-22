// animation.cpp: CAnimation (reconstructed; Stuart's file is not in the release). From the retail object,
// 004045e0..00404906.

#include "common.h"
#include "thing.h"
#include "animation.h"
#include "game.h"
#include "event.h"
#include "eventmanager.h"

//******************************************************************************************
BOOL	AnimModeLoops(EAnimMode am)	// FUNCTION: 0x004048f0
{
	return am > AM_NONE && am <= AM_IDLE;
}

//******************************************************************************************
CAnimation::CAnimation(CComplexThing *thing)	// FUNCTION: 0x004046d0
{
	mThing = thing;
	mFrame = 0;
	mLastFrame = 0;
	mAnimIndex = -1;
	mAnimMode = AM_NONE;
	mFrameIncrement = 0;
	mForceLooped = FALSE;
	EVENT_MANAGER.AddEvent(ANIMATION_EVENT_PROCESS, this, NEXT_FRAME);
}

//******************************************************************************************
void	CAnimation::HandleEvent(CEvent *event)	// FUNCTION: 0x00404750
{
	Process();
	EVENT_MANAGER.AddEvent(ANIMATION_EVENT_PROCESS, this, NEXT_FRAME, START_OF_FRAME, NULL, (CScheduledEvent *)event);
}

//******************************************************************************************
void	CAnimation::Process()	// FUNCTION: 0x00404790
{
	mLastFrame = mFrame;
	if (mAnimMode == AM_NONE || mAnimMode == AM_INVALID)
		return;

	mFrame += mFrameIncrement;
	if (mFrame < 1.0)
		return;

	if (AnimModeLoops(mAnimMode) || mForceLooped == TRUE)
		mFrame -= 1.0;
	else if (mThing->FinishedPlayingCurrentAnimation() && mFrame >= 1.f)
	{
		mFrameIncrement = mThing->GetRenderThingFrameIncrement(AM_STAND, &mAnimIndex);
		mFrame = 0;
		mAnimMode = AM_STAND;
		if (mFrameIncrement <= 0.0001)
			mFrameIncrement = 1.f;
	}

	if (mFrame >= 1.f)
		mFrame = 1.f;
}

//******************************************************************************************
BOOL	CAnimation::SetAnimMode(EAnimMode am, BOOL reset_frame, BOOL force_looped)	// FUNCTION: 0x00404860
{
	if (am != mAnimMode || reset_frame)
	{
		mForceLooped = force_looped;
		float	inc = mThing->GetRenderThingFrameIncrement(am, &mAnimIndex);
		if (reset_frame)
			mFrame = 0;
		mFrameIncrement = inc;
		mAnimMode = am;
		if (mFrameIncrement <= 0.0001)
			mFrameIncrement = 1.f;
	}
	return TRUE;
}

//******************************************************************************************
float	CAnimation::GetRenderFrame()	// FUNCTION: 0x004048c0
{
	if (mLastFrame > mFrame)
		return mFrame;
	return (mFrame - mLastFrame) * GAME.GetFrameRenderFraction() + mLastFrame;
}

// FUNCTION: 0x004045e0 init:ZERO_FVECTOR
// FUNCTION: 0x00404600 init:ID_FMATRIX
