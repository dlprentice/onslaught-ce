// MenuItem.h: the items of the in-game and PC options menus (reconstructed from the retail PC executable; Stuart's
// file is not in his release). The class names are the retail RTTI's; CMenuOptionsList (the next object) lays them
// out and CPauseMenu builds them.

#ifndef MENUITEM_H
#define MENUITEM_H

#include "activereader.h"

class CController;
class CDXTexture;
class CGameMenu;
class CMenuOptionsList;

// the ids of Localization_GetStringById, the PC's hard-coded strings (text.cpp 00524830)
enum EPCText
{
	PCT_NONE = 0
};

// RTTI: CMenuItem (vtable 005db440), 0x1c bytes
class CMenuItem
{
public:
	CMenuItem() {}
	CMenuItem(DWORD text, int id) : mId(id), mText(text), mLabel(0), mEnabled(TRUE), mColour(0xffd6d6d6), mPCText(PCT_NONE) {}
	CMenuItem(EPCText text, int id) : mId(id), mText(0), mLabel(0), mEnabled(TRUE), mColour(0xffd6d6d6), mPCText(text) {}
	CMenuItem(CMenuItem &item) { mText = item.mText; mEnabled = item.mEnabled; mColour = item.mColour; mLabel = item.mLabel; }
	// FUNCTION: 0x00453a90 ??_GCMenuItem@@UAEPAXI@Z
	virtual			~CMenuItem() {}

	// FUNCTION: 0x00453a50
	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val) {}
	virtual WCHAR	*GetText();
	// FUNCTION: 0x00453a60
	virtual BOOL	IsEnabled() { return mEnabled; }
	virtual void	Render(float x, float y, BOOL selected);
	virtual int		GetWidth();
	// FUNCTION: 0x00453a70
	virtual int		GetHeight() { return mLabel ? 40 : 20; }
	virtual CMenuItem	*Clone();
	// FUNCTION: 0x00453a80
	virtual bool	IsExpanded() { return false; }
	virtual bool	TakesInput() { return false; }
	virtual bool	HasPendingChange() { return false; }
	virtual void	Commit() {}
	virtual void	Refresh() {}
	virtual void	LoadResources() {}

	void			Render(float x, float y, BOOL selected, DWORD colour);
	void			Render(float x, float y, BOOL selected, DWORD colour, WCHAR *text);

	static bool		IsMouseInRect(float tlx, float tly, float brx, float bry);
	static bool		MouseClickedInRect(float tlx, float tly, float brx, float bry);

	int				mId;							// +0x04
	int				mText;							// +0x08: a text.stf id (signed: CPauseMenu::ButtonPressed 004d083d jg)
	DWORD			mLabel;							// +0x0c: a text.stf id drawn above the item
	BOOL			mEnabled;						// +0x10
	DWORD			mColour;						// +0x14
	EPCText			mPCText;						// +0x18: used when mText is 0
};

// RTTI: CScaleMenuItem (vtable 005dc520), 0x38 bytes: a bar of mSteps segments, moved left and right
class CScaleMenuItem : public CMenuItem
{
public:
	CScaleMenuItem(DWORD text, int id, float value, CGameMenu *menu, int steps, bool immediate);
	CScaleMenuItem(EPCText text, int id, float value, CGameMenu *menu, int steps, bool immediate);
	CScaleMenuItem(CScaleMenuItem &item);
	virtual			~CScaleMenuItem();

	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val);
	virtual void	Render(float x, float y, BOOL selected);
	virtual int		GetWidth();
	virtual CMenuItem	*Clone();
	virtual void	LoadResources();
	virtual void	ValueChanged() {}
	virtual void	ReadValue() {}					// from the setting it edits (CMouseSensitivityMenuItem 004cf000)

	CDXTexture		*mArrow;						// +0x1c
	CDXTexture		*mBlank;						// +0x20
	int				mValue;							// +0x24
	int				mCommittedValue;				// +0x28
	int				mSteps;							// +0x2c
	bool			mImmediate;						// +0x30: tell the menu at every step
	CActiveReader<CGameMenu>	mMenu;				// +0x34: told when the value is committed
};

// RTTI: CMenuMenuItem (vtable 005dc578), 0x28 bytes: a choice from a list, dropped down when selected
class CMenuMenuItem : public CMenuItem
{
public:
	CMenuMenuItem(EPCText text, int id, bool deferred);

	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val);
	virtual void	Render(float x, float y, BOOL selected);
	// FUNCTION: 0x004a40e0
	virtual bool	IsExpanded() { return mExpanded; }
	virtual bool	TakesInput() { return mExpanded; }
	virtual bool	HasPendingChange();
	virtual void	Commit();
	virtual void	Refresh();
	virtual void	SetValue(int value) = 0;
	virtual int		GetValue() = 0;
	virtual int		GetNumChoices() = 0;
	virtual WCHAR	*GetChoiceText(int n) = 0;

	void			RenderList(float x, float y, BOOL on_top);
	void			SetSelection(int n);
#ifdef PAUSEMENU_CPP	// CVideoDetailLevel::GetValue reads the selections through it (004d00c5: before a GetDefault call)
	int				GetSelection() { return mSelection; }
#endif

	static void		ClearDeferredRender();
	static void		DeferredRender();

	int				mValue;							// +0x1c
	int				mSelection;						// +0x20
	bool			mExpanded;						// +0x24
	bool			mDeferred;						// +0x25: commit only through a CApplyMenuItem

	static CMenuMenuItem	*mRenderOnTop;			// 0070486c: the expanded list, drawn after the others
	static float	mRenderOnTopY;					// 00704870
	static float	mRenderOnTopX;					// 00704874
};

// RTTI: CBOOLMenuItem (vtable 005dc5c4), 0x28 bytes
class CBOOLMenuItem : public CMenuMenuItem
{
public:
	CBOOLMenuItem(EPCText text, int id, bool deferred);

	virtual int		GetNumChoices() { return 2; }
	virtual WCHAR	*GetChoiceText(int n);
};

// RTTI: CApplyMenuItem (vtable 005dc610), 0x20 bytes: commits every item of its list
class CApplyMenuItem : public CMenuItem
{
public:
	CApplyMenuItem(CMenuOptionsList *list);

	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val);
	virtual void	Render(float x, float y, BOOL selected);

	CMenuOptionsList	*mList;						// +0x1c
};

extern int		gMenuApplyCount;				// 00704868: counts applies
extern bool		gMenuChangesPending;			// 00704a88: the apply item flashes while set

#endif
