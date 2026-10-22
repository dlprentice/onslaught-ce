// PauseMenu.h: the in-game menu, which the PC options page also shows (reconstructed; CPauseMenu is 0x4c bytes,
// vtable 005de6fc, derived from CGameMenu).

#ifndef PAUSEMENU_H
#define PAUSEMENU_H

#include "game.h"
#include "MenuOptionsList.h"
#include "Event.h"

class CMenuItem;
class CDXTexture;

// RTTI: CGameMenu (vtable 005dc72c)
class CGameMenu : public IController
{
public:
	CGameMenu();									// 004d0e40
	virtual EControlType	GetControlType() { return CONTROL_GAME_MENU; }	// the shared 0052de90
	virtual void	MenuItemChanged(CMenuItem *item) {}	// slot 6: a CScaleMenuItem's value was committed (004a441f)

	void			RelinquishControl();			// 004cdd70: the controllers controlling this let go
};

// an event a confirmation sends when one of its items is chosen (0xc bytes, CPauseMenu::ButtonPressed 004d09e0)
class CMenuEvent : public CEvent
{
public:
	CMenuEvent(CMenuItem *item, CMonitor *to_call, int event);


	CMenuItem		*mItem;							// +0x08
};

// RTTI: CSimpleGameMenu (vtable 005de71c): the confirmation a CPauseMenu raises (0x4c bytes, 004d08fb)
class CSimpleGameMenu : public CGameMenu
{
public:
	// The retail confirmation initializes CGameMenu before looking up its title (004d0917, 004d093f).
	CSimpleGameMenu(DWORD title, float x, float y, BOOL panel, DWORD colour);
	virtual			~CSimpleGameMenu();

	virtual void	ReceiveButtonAction(CController* from_controller, int button, float val);

	virtual BOOL	CanBeControlledWhenInPause() { return TRUE; }

	void			AddItem(CMenuItem *item, CMonitor *to_call, int event);
	void			SendEvent(CMenuItem *item);

	bool			mActive;						// +0x08
	CConfirmMenuOptionsList	mList;					// +0x0c (CPauseMenu::Render 004d1563)
	SPtrSet<CMenuEvent>	mEvents;					// +0x3c
};

// the events a confirmation's items send
enum EPauseMenuEvent
{
	PAUSE_MENU_YES = 2000,
	PAUSE_MENU_NO = 2001
};

class CPauseMenu : public CGameMenu
{
public:
	CPauseMenu(bool options_page = false);			// 004cde60 (CFEPOptions passes TRUE; read as a byte, 004cdea1)
	virtual			~CPauseMenu();

	virtual void	HandleEvent(CEvent *event);
	virtual void	ReceiveButtonAction(CController* from_controller, int button, float val);	// 004d15d0
	virtual BOOL	CanBeControlledWhenInPause() { return TRUE; }
	virtual void	MenuItemChanged(CMenuItem *item);

	WCHAR			*Render();						// 004d11d0: draws the menu and returns its title, if any
	void			Activate(BOOL a);				// 004d0ff0 (Stuart's name)
	void			InitResources();				// 004d0510: the pause menu's textures
	void			DeActivate(BOOL a);				// 004d10b0
	void			ButtonPressed(CMenuItem *item, CController *from_controller);	// 004d0810
	void			Resume(CController *from_controller);	// 004d06e0: back to the game, saving the options

	// the dropdown lists' background (a CMenuMenuItem reloads it when it is missing, 004a3e30)
	static CDXTexture	*mBlankTexture;					// 0082b490
	static void		LoadBlankTexture();				// 004d04d0
	static bool		mOptionsPage;					// 0082b4e8: the constructor's argument

	bool			IsConfirming();					// 004d05c0
	static WCHAR	*GetBindingWarning();			// 004d0de0: while a player has no free binding, or NULL

	CSimpleGameMenu	*mConfirm;						// +0x08
	int				mResult;						// +0x0c: 1 when the options page should show the credits
	BOOL			mActive;						// +0x10: cleared when the menu is left
	SPtrSet<CMenuOptionsList>	mLists;				// +0x14: the pages
	int				mPage;							// +0x24
	float			mActivateTime;					// +0x28
	float			mOpenTime;						// +0x2c
	float			mCloseTime;						// +0x30: -1 until first closed
	float			mFirstRenderTime;				// +0x34
	DWORD			mChosen;						// +0x38: the text of the item last chosen
	CSimpleGameMenu	*mQuitConfirm;					// +0x3c
	CDXTexture		*mCircle1;						// +0x40
	CDXTexture		*mCircle2;						// +0x44
	BOOL			mNoFade;						// +0x48
};

// a hard-coded (Localization) message the display setup leaves (SETUP_HISTORY logs it first, 0052b368)
void	SetSetupMessage(int text);				// 004d1710

#endif
