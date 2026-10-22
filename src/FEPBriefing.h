// FEPBriefing.h: the mission briefing page (reconstructed; 0x14 bytes, FRONTEND + 0x324, vtable 005db9e8).

#ifndef FEPBRIEFING_H
#define FEPBRIEFING_H

#include "frontendpage.h"

class CFEPBriefing : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);
	void	ActiveNotification(EFrontEndPage from);
	void	DeActiveNotification();

	float	mStartTime;							// +0x04
	float	mTextFade;							// +0x08: rises while active, falls while leaving
	BOOL	mVideoOpen;							// +0x0c
	float	mTimer;								// +0x10
};

#endif
