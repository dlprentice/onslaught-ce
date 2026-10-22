// FEPMultiplayer.h: the PC's two-player level grid, three rows of six (reconstructed; not in Stuart's release).
// 0x68 bytes at FRONTEND + 0x4050, vtable 005db8f8.

#ifndef FEPMULTIPLAYER_H
#define FEPMULTIPLAYER_H

#include "frontendpage.h"

class CFEPMultiplayer : public CFrontEndPage
{
public:
	BOOL	Init();
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);

	void	FindSelectedLevel();

	float	mStartTime;							// +0x04
	float	mXTime;								// +0x08: when mCX last changed
	float	mYTime;								// +0x0c: when mCY last changed
	float	mTransition[6][3];					// +0x10: each level's highlight, 0..1
	float	mPos, mPosDest;						// +0x58, +0x5c
	SINT	mCX;								// +0x60: 0..5
	SINT	mCY;								// +0x64: 0..2
};

#endif
