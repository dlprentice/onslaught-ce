// GameInterface.h: the in-game interface (only what matched files need so far). A controller takes it with
// SetToControl(&GAMEINTERFACE), so it is an IController at offset 0.

#ifndef GAMEINTERFACE_H
#define GAMEINTERFACE_H

#include "Controller.h"

class CGameInterface : public IController
{
public:
	virtual void	ReceiveButtonAction(CController* from_controller, int button, float val);	// 3 (00472d50)
	virtual BOOL	CanBeControlledWhenInPause() { return TRUE; }	// 4 (folded into 004014a0)
	virtual EControlType	GetControlType() { return CONTROL_GAMEINTERFACE; }	// 5 (folded into 00405ee0)

	void	Init();								// 004729e0
	void	Reset();							// 004729e0: the same code as Init, folded by the linker (CGame::InitRestartLoop 0046c5b7)
	void	InitResources();					// 00472a10
	void	Shutdown();							// 2 (00472a50)
	void	ToggleMenuDisplay();				// 00472a90
	void	MenuUp();							// inlined into ReceiveButtonAction (00472d6f)
	void	MenuDown();							// 00472ad0
	void	SelectItem(CController *from_controller);	// 00472b40 (name open)
	void	DrawBackground(float x, float y, float w, float h, DWORD colour);	// 00472e50 (name open)
	void	Render();							// 00472f10 (CDXEngine::PostRender 0053efd1)
	void	DrawBox(float x, float y, float width, float height, DWORD colour);	// 00472e50: default.tga stretched, centred on x, y (name open)

	// Stuart's inlines (CGame::Update 0046ed71..0046ed9d)
	BOOL	IsMenuUp() { return mMenuUp; }
	void	SetNotAvailable(int item) { mAvailable[item] = FALSE; }
	void	SetRenderBackgroud(bool on) { mRenderBackground = on; }
	void	SetYBoxoffset(int y) { mYBoxOffset = y; }

	class CDXTexture	*mBackground;				// +0x08: "hud\Menu background.tga"
	class CDXTexture	*mJoypad;					// +0x0c: "Interface\Joypad.tga"
	int		mUnknown10;							// +0x10
	bool	mShowJoypad;						// +0x14: the joypad picture replaces the menu (BUTTON_HELP puts it away)
	int		mUnknown18;							// +0x18
	bool	mMenuUp;							// +0x1c
	int		mSelection;							// +0x20
	bool	mRenderBackground;					// +0x24
	int		mYBoxOffset;						// +0x28
	BOOL	mAvailable[6];						// +0x2c
	int		mPage;								// +0x44: 1 the game menu, 2 the options
};

extern CGameInterface GAMEINTERFACE;			// 00679fa8

#endif
