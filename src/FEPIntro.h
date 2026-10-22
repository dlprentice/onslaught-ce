// FEPIntro.h: the title page (reconstructed; 0x1c bytes, FRONTEND + 0x4034, vtable 005e49b4). Its constructor is out
// of line (0051b600).

#ifndef FEPINTRO_H
#define FEPINTRO_H

#include "frontendpage.h"

class CFEPIntro : public CFrontEndPage
{
public:
	CFEPIntro();
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);

	void	Reset();

	float	mStartTime;							// +0x04
	UBYTE	mUnknown08[0x0c - 0x08];
	BOOL	mUnknown0c;							// +0x0c: blocks SELECT and the idle quit
	BOOL	mUnknown10;							// +0x10
	int		mUnknown14;							// +0x14: copied to CLIPARAMS +0x2a0 on the quit answer
	float	mPressStart;						// +0x18: grows from 0 once the page has shown a second
};

#endif
