// MenuOptionsList.h: a list of menu items, laid out and driven as one menu (reconstructed from the retail PC
// executable; Stuart's file is not in his release). CPauseMenu builds them (PauseMenu::Init 004cdf1f).

#ifndef MENUOPTIONSLIST_H
#define MENUOPTIONSLIST_H

#include "SPtrSet.h"

class CController;
class CMenuItem;
class CPauseMenu;
class CDXTexture;

// RTTI: CMenuOptionsList (vtable 005dc650), 0x30 bytes (004cdee2)
class CMenuOptionsList
{
public:
	CMenuOptionsList(WCHAR *title, float x, float y, BOOL panel, DWORD colour);
	virtual			~CMenuOptionsList();

	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val);
	virtual bool	PassToSelected(CController *from_controller, int button, float val);	// while it is expanded
	virtual bool	IsConfirmation() { return false; }

	void			Reset();
	void			AddItem(CMenuItem *item);
	void			LoadResources();
	void			SelectNext();
	void			SelectPrevious();				// inlined into ReceiveButtonAction
	WCHAR			*Render(CPauseMenu *menu);
	void			SetItemEnabled(int id, BOOL enabled);

	WCHAR			*mTitle;						// +0x04
	SPtrSet<CMenuItem>	mItems;						// +0x08
	int				mSelected;						// +0x18
	float			mX;								// +0x1c
	float			mY;								// +0x20
	CDXTexture		*mBlank;						// +0x24
	BOOL			mPanel;							// +0x28: drawn on a message-log panel
	DWORD			mColour;						// +0x2c
};

// RTTI: CConfirmMenuOptionsList (vtable 005dc664): the yes/no list of a confirmation (CPauseMenu::ButtonPressed
// 004d0947)
class CConfirmMenuOptionsList : public CMenuOptionsList
{
public:
	CConfirmMenuOptionsList(WCHAR *title, float x, float y, BOOL panel, DWORD colour);

	virtual bool	IsConfirmation() { return true; }
};

#endif
