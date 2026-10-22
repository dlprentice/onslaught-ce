// MenuOptionsList.cpp: a list of menu items, laid out and driven as one menu (reconstructed from the retail PC
// executable; Stuart's file is not in his release).

#include	"Common.h"

#include	"MenuOptionsList.h"
#include	"MenuItem.h"
#include	"PauseMenu.h"
#include	"MessageLog.h"
#include	"Frontend.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"Texture.h"

// FUNCTION: 0x004a44d0 init:ZERO_FVECTOR
// FUNCTION: 0x004a44f0 init:ID_FMATRIX

// FUNCTION: 0x004a45c0
// The blank texture is cleared in the body, after the initializer list: retail stores y before the zeros and the
// panel/colour pair after them (004a45df..004a45f3).
CMenuOptionsList::CMenuOptionsList(WCHAR *title, float x, float y, BOOL panel, DWORD colour)
	: mTitle(title), mSelected(0), mX(x), mY(y), mPanel(panel), mColour(colour)
{
	mBlank = NULL;
}

// FUNCTION: 0x004a4630
void	CMenuOptionsList::Reset()
{
	mSelected = 0;

	CMenuItem	*item;
	FOR_ALL_ITEMS_IN(mItems, item)
		item->Refresh();
}

// FUNCTION: 0x004a4670
void	CMenuOptionsList::AddItem(CMenuItem *item)
{
	mItems.Append(item);
}

// FUNCTION: 0x004a4680
CMenuOptionsList::~CMenuOptionsList()
{
	mItems.DeleteAll();

	if (mBlank)
	{
		mBlank->Release();
		mBlank = NULL;
	}
}

// FUNCTION: 0x004a4730
void	CMenuOptionsList::LoadResources()
{
	mBlank = CDXTexture::FindTexture("FrontEnd\\v2\\FE_Blank.tga", 4, 0, 1, 0, 1);

	CMenuItem	*item;
	FOR_ALL_ITEMS_IN(mItems, item)
		item->LoadResources();
}

// ReceiveButtonAction calls SelectNext at 004a4d59; Render (004a4810) has no direct call to it.
// SelectNext is written in the longer braced style, which puts its own size past the 174 units up to which VC6 offers a
// plain function for inlining (the pragma recipe, 2026-10-15); SelectPrevious (146) is expanded.
// FUNCTION: 0x004a4790
void	CMenuOptionsList::SelectNext()
{
	int		old = mSelected;

	if (mSelected >= mItems.Size() - 1)
	{
		if (mItems.Size() <= 2)
		{
			return;
		}
		mSelected = 0;
	}
	else
	{
		mSelected = mSelected + 1;
	}

	while (mItems.At(mSelected)->IsEnabled() == FALSE)
	{
		mSelected = mSelected + 1;
		if (mSelected >= mItems.Size())
		{
			mSelected = old;
			return;
		}
	}
	CFrontEnd::PlaySound(FES_MOVE);
}

// STAND-IN (frame; review F 2026-10-15): the bare block ending the SIZE's lifetime; without it 004a4810 is lost.
// This reconstructed sizing boundary and its width-only SIZE lifetime reproduce
// the retail iterator calls and frame reuse; the original source spelling is unknown.
static inline void GetMenuPanelSize(CMenuOptionsList *list, float &pw, float &fh)
{
	CMenuItem *item;
	{
		SIZE size;
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(list->mTitle, &size);
		int width = size.cx;
		int widest = 0;
		for (item = list->mItems.First(); item; item = list->mItems.Next())
		{
			int w = item->GetWidth();
			if (w > widest)
				widest = w;
		}
		if (widest > width)
			width = widest;
		width += 16;
		pw = width * 1.1f;
	}

	int ph = 32;
	FOR_ALL_ITEMS_IN(list->mItems, item)
		ph += item->GetHeight();
	fh = ph * 1.1f;
}

// FUNCTION: 0x004a4810
WCHAR	*CMenuOptionsList::Render(CPauseMenu *menu)
{
	CMenuMenuItem::ClearDeferredRender();

	bool	options_page = CPauseMenu::mOptionsPage;
	RENDERINFO.SetFogEnabled(false);

	float	x = mX;
	float	y = mY;

	CMenuItem	*item;
	int		height = 32;
	FOR_ALL_ITEMS_IN(mItems, item)
		height += item->GetHeight();
	height -= 32;

	float	ty = y - height * 0.5f - 15.f;

	if (mPanel)
	{
		float pw, fh;
		GetMenuPanelSize(this, pw, fh);
		GAME.GetMessageLog()->RenderPanel(int(x - pw * 0.5f), int(y - fh * 0.5f), int(pw), int(fh), 1.2f);
	}

	SIZE title_size;
	if ((!options_page) && mTitle)
	{
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(mTitle, &title_size);
		float title_x = x - (title_size.cx / 2);
		PLATFORM.Font(FONT_NORMAL)->DrawText(title_x, ty, 0xff505050, mTitle, 0, 0, 1.f);
		ty += 30.f;
	}

	int		n = 0;
	int		widest = 0;
	FOR_ALL_ITEMS_IN(mItems, item)
	{
		int	w = item->GetWidth();
		if (w > widest)
			widest = w;
	}

	gMenuChangesPending = false;

	if (!mBlank)
		LoadResources();

	bool	clicked = false;
	FOR_ALL_ITEMS_IN(mItems, item)
	{
		if ((!menu->IsConfirming()) || IsConfirmation())
		{
			if (!mItems.At(mSelected)->TakesInput())
			{
				if (CMenuItem::IsMouseInRect(0.f, ty, 640.f, ty + item->GetHeight()))
					mSelected = n;
			}
		}

		BOOL	selected = (n == mSelected);
		LT.SetModulate(0);
		RENDERINFO.SetFogEnabled(false);
		RENDERINFO.Apply();
		item->Render(x, ty, selected);

		if ((!menu->IsConfirming()) || IsConfirmation())
		{
			if (!mItems.At(mSelected)->TakesInput())
			{
				if (CMenuItem::MouseClickedInRect(0.f, ty, 640.f, ty + item->GetHeight()))
				{
					clicked = true;
					mSelected = n;
				}
			}
		}

		if (item->HasPendingChange())
			gMenuChangesPending = true;

		ty += item->GetHeight();
		n++;
	}

	CMenuMenuItem::DeferredRender();

	// Retail 004a4c57 snapshots the result before the confirmation/button callbacks.
	// The callbacks' effect on this title in live retail play is not established here.
	WCHAR *rendered_title = mTitle;

	if (clicked)
	{
		if (IsConfirmation())
		{
			menu->mConfirm->ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_SELECT, 1.f);
			menu->mConfirm->ReceiveButtonAction(NULL, BUTTON_PAUSE_MENU_SELECT, 1.f);
		}
		else
		{
			menu->ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_SELECT, 1.f);
			menu->ReceiveButtonAction(NULL, BUTTON_PAUSE_MENU_SELECT, 1.f);
		}
	}

	return rendered_title;
}

// FUNCTION: 0x004a4cd0
bool	CMenuOptionsList::PassToSelected(CController *from_controller, int button, float val)
{
	if (mItems.At(mSelected)->IsExpanded())
	{
		mItems.At(mSelected)->ReceiveButtonAction(from_controller, button, val);
		return true;
	}
	return false;
}

void	CMenuOptionsList::SelectPrevious()
{
	int		old = mSelected;

	if (mSelected <= 0)
	{
		if (mItems.Size() <= 2)
			return;
		mSelected = mItems.Size() - 1;
	}
	else
		mSelected--;

	while (!mItems.At(mSelected)->IsEnabled())
	{
		if (--mSelected < 0)
		{
			mSelected = old;
			return;
		}
	}
	CFrontEnd::PlaySound(FES_MOVE);
}

// FUNCTION: 0x004a4d20
void	CMenuOptionsList::ReceiveButtonAction(CController *from_controller, int button, float val)
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_UP:
		SelectPrevious();
		break;
	case BUTTON_FRONTEND_MENU_DOWN:
		SelectNext();
		break;
	default:
		mItems.At(mSelected)->ReceiveButtonAction(from_controller, button, val);
		break;
	}
}

// FUNCTION: 0x004a4dd0
void	CMenuOptionsList::SetItemEnabled(int id, BOOL enabled)
{
	CMenuItem	*item;
	FOR_ALL_ITEMS_IN(mItems, item)
	{
		if (item->mText == id)
			item->mEnabled = enabled;
	}
}

// FUNCTION: 0x004a4e10
CConfirmMenuOptionsList::CConfirmMenuOptionsList(WCHAR *title, float x, float y, BOOL panel, DWORD colour)
	: CMenuOptionsList(title, x, y, panel, colour)
{
}

// the implicit destructor, out of line for the scalar deleting destructor (004a4e63); CSimpleGameMenu's destructor
// inlines it and calls ~CMenuOptionsList (004d17e0)
// FUNCTION: 0x004a4e80 ??1CConfirmMenuOptionsList@@UAE@XZ

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004a4610 ??_GCMenuOptionsList@@UAEPAXI@Z
// FUNCTION: 0x004a4e60 ??_GCConfirmMenuOptionsList@@UAEPAXI@Z
