// FEPDebriefing.h: the page after a level, where each goodie won flies past as a particle effect (reconstructed;
// Stuart's file is not in his release). 0x28 bytes at FRONTEND + 0x338, vtable 005db9c0.

#ifndef FEPDEBRIEFING_H
#define FEPDEBRIEFING_H
#include "maths.h"

#include "frontendpage.h"

class CParticleEffectLink;

#define MAX_DEBRIEF_GOODIES		100

class CFEPDebriefing : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);

	float				mStartTime;			// +0x04
	float				mFade;				// +0x08: 0 while the goodies fly, then up to 1
	SINT				mNumGoodies;		// +0x0c: won this level (at most 99)
	SINT				mNumShown;			// +0x10
	BOOL				mFirstGoodie;		// +0x14: the first ever, which explains itself
	float				mNextTime;			// +0x18
	float				mFastUntil;			// +0x1c: CHEAT speeds the effect up for a moment
	CParticleEffectLink	*mLinks;			// +0x20: MAX_DEBRIEF_GOODIES
	FVector				*mPositions;		// +0x24: MAX_DEBRIEF_GOODIES
};

#endif
