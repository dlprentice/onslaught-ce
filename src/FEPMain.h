// FEPMain.h: the main menu page (reconstructed; 0x24 bytes, FRONTEND + 0x278, vtable 005dbae4). The PC build adds
// four virtual functions after the page's nine: the demo menu (CFEPDemoMain) overrides them.

#ifndef FEPMAIN_H
#define FEPMAIN_H

#include "frontendpage.h"

class CFEPMain : public CFrontEndPage
{
public:
	BOOL	Init();
	// Retail 005dbae4 slot 1 (also inherited by CFEPDemoMain): distinct empty body.
	void	Shutdown();
	void	Process(EFEPState state);
	void	ButtonPressed(SINT button, float val);
	void	RenderPreCommon(float transition, EFrontEndPage dest);
	void	Render(float transition, EFrontEndPage dest);
	void	TransitionNotification(EFrontEndPage from);
	void	ActiveNotification(EFrontEndPage from);

	virtual BOOL	IsActionAvailable(int action);			// 9
	virtual int		GetNumActions();						// 10
	virtual void	DoAction();								// 11
	virtual WCHAR	*GetActionText(int action);				// 12

	void	StartNewCareer();

	float	mStartTime;						// +0x04
	int		mSelected;						// +0x08: -1 is the language row
	int		mCursorTarget;					// +0x0c
	float	mCursorPos;						// +0x10: eases towards the target
	float	mSelectFlash;					// +0x14: -1, or rising from 0 to 1 after a move
	float	mIdleTime;						// +0x18: the attract-mode timer
	float	mLeftArrowFlash;				// +0x1c
	float	mRightArrowFlash;				// +0x20
};

#endif
