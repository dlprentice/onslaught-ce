// MessageLog.h: the in-game message log (only what matched files need). GAME.GetMessageLog() is at 008a9d88.

#ifndef MESSAGELOG_H
#define MESSAGELOG_H

#include "PauseMenu.h"

class CMessage;
class CDXTexture;

// RTTI: CMessageLog (vtable 005dc70c, 7 slots) derives from CGameMenu: the messages already shown, listed from the
// pause menu (MessageLog.cpp)
class CMessageLog : public CGameMenu
{
public:
	virtual void	ReceiveButtonAction(CController* from_controller, int button, float val);	// 004b9ec0
	virtual BOOL	CanBeControlledWhenInPause() { return TRUE; }	// the shared 004014a0
	virtual EControlType	GetControlType();						// the shared 004dbc20 (MessageLog.cpp)

	CMessageLog();								// 004b8dd0 (0x40 bytes: CGame::InitRestartLoop 0046c6c6)
	virtual			~CMessageLog();				// 004b8f00
	void			InitResources();				// 004b8e70
	void			AddMessage(CMessage *message);	// 004b8ef0 (CMessageBox::HandleEvent 004b8239)
	void			Activate();						// 004b9ea0: shown from the pause menu (004d0d31)
	void			Render(class CViewport *viewport);	// 004b93f0 (CDXEngine::PostRender 0053ee0d)
	// a framed panel; CMenuOptionsList::Render draws its background with it (004a49a7)
	void			RenderPanel(int x, int y, int width, int height, float alpha);	// 004b9010
	// one message's card; returns its height, drawing nothing when measure is set
	int				RenderMessageCard(CMessage *message, float x, float y, float alpha, BOOL measure);	// 004b9a80

	static CDXTexture	*mArrow;						// 00807418: FE_Arrow.tga, the back arrow (CLevelBriefingLog::Render draws it too)

	CDXTexture		*mEndCurve;						// +0x08: the panel's corners
	CDXTexture		*mBlank;						// +0x0c: the panel's edges and middle
	CDXTexture		*mHeadFrame;					// +0x10
	CDXTexture		*mHeadFrameMask;				// +0x14
	SPtrSet<CMessage>	mMessages;					// +0x18
	BOOL			mActive;						// +0x28
	float			mScroll;						// +0x2c
	float			mScrollTarget;					// +0x30
	BOOL			mCanScrollDown;					// +0x34: set by Render when the list runs past the panel
	int				mTop;							// +0x38: the first message shown
	int				mSelected;						// +0x3c
};

#endif
