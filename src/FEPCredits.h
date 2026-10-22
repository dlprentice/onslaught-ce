// FEPCredits.h: the credits page (reconstructed; 0xc bytes, FRONTEND + 0x4118, vtable 005db880).

#ifndef FEPCREDITS_H
#define FEPCREDITS_H

#include "frontendpage.h"
#include "Credits.h"


class CFEPCredits : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);

	float	mStartTime;							// +0x04
	BOOL	mFinished;							// +0x08
};

#endif
