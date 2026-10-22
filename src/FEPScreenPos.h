// FEPScreenPos.h: the PC's screen-position page (reconstructed; 0x18 bytes, FRONTEND + 0x4124, vtable 005db858).

#ifndef FEPSCREENPOS_H
#define FEPSCREENPOS_H

#include "FEPOptions.h"

class CFEPScreenPos : public CFrontEndPage
{
public:
	BOOL	Init();
	// Retail 005db858 slots 1 and 2 use distinct empty bodies from CFrontEndPage.
	void	Shutdown();
	void	Process(EFEPState state);									// 00452b60 (the non-inline fold)
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);

	int		mX, mY;								// +0x04, +0x08
	float	mStartTime;							// +0x0c
	CScreenPosition	mOnEntry;					// +0x10: restored when the page is left with BACK
};

#endif
