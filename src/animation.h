// animation.h: CAnimation, a complex thing's animation clock (Animation.obj, 004045e0..00404906; vtable 005d87c8;
// 0x24 bytes). Not in Stuart's release: reconstructed from the retail object. CComplexThing creates it and calls
// SetAnimMode and GetRenderFrame (004f44a0, 004f45c0).

#ifndef ANIMATION_H
#define ANIMATION_H

#include "Monitor.h"

class CComplexThing;

#define ANIMATION_EVENT_PROCESS	3000		// the per-frame step (004046fc)

// The looping modes, AM_WALK..AM_IDLE (00404e0f: out of line at 004048f0).
BOOL	AnimModeLoops(EAnimMode am);

class CAnimation : public CMonitor
{
public:
				CAnimation(CComplexThing *thing);								// 004046d0
	virtual void	HandleEvent(CEvent *event);									// 00404750
	void		Process();														// 00404790
	BOOL		SetAnimMode(EAnimMode am, BOOL reset_frame, BOOL force_looped);	// 00404860
	float		GetRenderFrame();												// 004048c0
	SINT		GetAnimIndex() { return mAnimIndex; }
	EAnimMode	GetAnimMode() { return mAnimMode; }

	float		mFrame;					// +0x08: 0..1 (CInfantryUnit::Damage starts its fall at a random frame, 00489704)
	float		mLastFrame;				// +0x0c: the frame before this step, for render interpolation
	SINT		mAnimIndex;				// +0x10: the render thing's index for the mode
	EAnimMode	mAnimMode;				// +0x14
	float		mFrameIncrement;		// +0x18
	BOOL		mForceLooped;			// +0x1c
	CComplexThing	*mThing;			// +0x20
};

#endif
