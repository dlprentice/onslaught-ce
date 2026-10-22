// MenuItem.cpp: the items of the in-game and PC options menus (reconstructed from the retail PC executable; Stuart's
// file is not in his release). The allocations name this file's lines 39 and 173 (004a3141, 004a3468).

#include	"Common.h"

#include	"MenuItem.h"
#include	"MenuOptionsList.h"
#include	"PauseMenu.h"
#include	"Frontend.h"
#include	"FEMessBox.h"
#include	"SpriteRenderer.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"mouse.h"
#include	"text.h"
#include	"Texture.h"
#include	"data\MissionScripts\text\text.stf"

// FUNCTION: 0x004a3010 init:ZERO_FVECTOR
// FUNCTION: 0x004a3030 init:ID_FMATRIX

// the items come from the memory type 0x80, beyond Stuart's list (004a314a)
#define MEMTYPE_MENUITEM	((EMemoryType)0x80)

static WCHAR	gMenuItemText[200];				// 00704878: 0x190 bytes, bounded by this object's own ID_FMATRIX at 00704a08
float			CMenuMenuItem::mRenderOnTopX;
float			CMenuMenuItem::mRenderOnTopY;
CMenuMenuItem	*CMenuMenuItem::mRenderOnTop;
int				gMenuApplyCount;
bool			gMenuChangesPending = false;

// FUNCTION: 0x004a3100
bool	CMenuItem::IsMouseInRect(float tlx, float tly, float brx, float bry) { return CFrontEnd::GetCursorStateInRect(tlx, tly, brx, bry); }
// FUNCTION: 0x004a3120
bool	CMenuItem::MouseClickedInRect(float tlx, float tly, float brx, float bry) { return CFrontEnd::MouseClickedInRect(tlx, tly, brx, bry); }

// FUNCTION: 0x004a3140
CMenuItem	*CMenuItem::Clone()
{
#line 39
	return new (MEMTYPE_MENUITEM) CMenuItem(*this);
}

// FUNCTION: 0x004a3190
WCHAR	*CMenuItem::GetText()
{
	if ((mText == FMV_1_00) || (mText == FMV_1_02))
	{
		wcscpy(gMenuItemText, TEXT.GetString(0x119bdd4));
		wcscat(gMenuItemText, ToWCHAR(" - "));
		wcscat(gMenuItemText, TEXT.GetString(IG_NO));
		return gMenuItemText;
	}
	if ((mText == FMV_1_01A) || (mText == FMV_1_03))
	{
		wcscpy(gMenuItemText, TEXT.GetString(0x119bdd4));
		wcscat(gMenuItemText, ToWCHAR(" - "));
		wcscat(gMenuItemText, TEXT.GetString(0x50cf2));
		return gMenuItemText;
	}

	if (mText == 0)
		return Localization_GetStringById(mPCText);

	return TEXT.GetString(mText);
}

// FUNCTION: 0x004a3260
void	CMenuItem::Render(float x, float y, BOOL selected)
{
	Render(x, y, selected, 0xffffffff, GetText());
}

// FUNCTION: 0x004a3290
void	CMenuItem::Render(float x, float y, BOOL selected, DWORD colour)
{
	Render(x, y, selected, colour, GetText());
}

// FUNCTION: 0x004a32c0
void	CMenuItem::Render(float x, float y, BOOL selected, DWORD colour, WCHAR *text)
{
	DWORD	col = mColour;
	SIZE	size;

	if (mLabel)
	{
		WCHAR	*label = TEXT.GetString(mLabel);
		PLATFORM.Font(FONT_SMALL)->GetTextExtent(label, &size);
		float	lx = x - (size.cx / 2);
		PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(lx, y, colour & 0xff2f5f7f, label, 0, 0, 1.f, 1.f);
		y += 20.f;
	}

	if (selected)
		col = 0xffffcc00;
	if (!mEnabled)
		col = 0x50505050;

	PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);

	float	tx = x - (size.cx / 2);
	float	scale = 1.f;
	if (tx < 5.f)
	{
		scale = (x - 5.f) / (size.cx / 2);
		tx = 5.f;
	}
	PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(tx, y, col & colour, text, 0, 0, scale, 1.f);
}

// FUNCTION: 0x004a3420
int		CMenuItem::GetWidth()
{
	SIZE	size;
	WCHAR	*text = GetText();
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
	return size.cx;
}

// FUNCTION: 0x004a3450
CMenuItem	*CScaleMenuItem::Clone()
{
#line 173
	return new (MEMTYPE_MENUITEM) CScaleMenuItem(*this);
}

CScaleMenuItem::CScaleMenuItem(CScaleMenuItem &item) : CMenuItem(item)
{
	mMenu = item.mMenu.ToRead();
	mValue = item.mValue;
	mArrow = NULL;
	mBlank = NULL;
}

// FUNCTION: 0x004a3510 ??0CScaleMenuItem@@QAE@KHMPAVCGameMenu@@H_N@Z
CScaleMenuItem::CScaleMenuItem(DWORD text, int id, float value, CGameMenu *menu, int steps, bool immediate)
	: CMenuItem(text, id), mArrow(NULL), mBlank(NULL), mImmediate(immediate), mMenu(menu)
{
	mSteps = steps;
	mCommittedValue = mValue = int(float(steps) * value + 0.48f);
}

// FUNCTION: 0x004a3630 ??0CScaleMenuItem@@QAE@W4EPCText@@HMPAVCGameMenu@@H_N@Z
CScaleMenuItem::CScaleMenuItem(EPCText text, int id, float value, CGameMenu *menu, int steps, bool immediate)
	: CMenuItem(text, id), mArrow(NULL), mBlank(NULL), mImmediate(immediate), mMenu(menu)
{
	mSteps = steps;
	mCommittedValue = mValue = int(float(steps) * value + 0.48f);
}

// FUNCTION: 0x004a3730
CScaleMenuItem::~CScaleMenuItem()
{
	if (mArrow)
	{
		mArrow->Release();
		mArrow = NULL;
	}
	if (mBlank)
	{
		mBlank->Release();
		mBlank = NULL;
	}
}

// FUNCTION: 0x004a37c0
void	CScaleMenuItem::Render(float x, float y, BOOL selected)
{
	WCHAR	*text = GetText();
	DWORD	col = mColour;

	if (selected)
		col = 0xffffcc00;
	else
	{
		mValue = mCommittedValue;
		ValueChanged();
	}

	if (!mArrow)
		LoadResources();

	if (!mEnabled)
		col = 0xff505050;

	SIZE	size;
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
	float	tx = x - (size.cx / 2) - 53.f;
	x = tx + size.cx + 2.f;
	PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(tx, y, col, text, 0, 0, 1.f, 1.f);

	RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	RENDERINFO.Apply();

	CSPRITERENDERER::DrawSprite(x + 5.f, y + 8.f, 0.003f, mArrow, SA_CENTRE, SPT_NONE, 1.f, PI, 0xffffffff, 0.3f, 0.3f, 0.f, 1.f, 0.f, 1.f);
	CSPRITERENDERER::DrawSprite(x + 99.f, y + 8.f, 0.003f, mArrow, SA_CENTRE, SPT_NONE, 1.f, 0.f, 0xffffffff, 0.3f, 0.3f, 0.f, 1.f, 0.f, 1.f);

	for (int i = 0; i < mSteps; i++)
	{
		DWORD	c = 0xff505050;

		if ((i < mValue) && (i < mCommittedValue))
			c = 0xff00a000;
		else if ((i < mValue) || (i < mCommittedValue))
		{
			float	f = (float(sin(float(fmod(PLATFORM.GetSysTimeFloat(), 2.0)) * PI)) + 1.f) * 0.5f;
			float	g = 1.f - f;
			float	alpha0 = g * 255.f;
			float	alpha1 = f * 255.f;
			c = (int(alpha0 + alpha1) << 24) + (int(g * 160.f + f * 80.f) << 8) + (int(g * 0.f + f * 80.f) << 16) + int(g * 0.f + f * 80.f);
		}

		CSPRITERENDERER::DrawSprite(x + (float(i) * 80.f) / float(mSteps) + 13.f, y + 8.f, 0.003f, mBlank, SA_8, SPT_NONE, 1.f, 0.f, c,
									(80.f / float(mSteps) - 1.f) / 16.f, 0.7f, 0.f, 1.f, 0.f, 1.f);
	}

	if (selected)
	{
		if (CFrontEnd::MouseClickedInRect(x, y, x + 40.f, y + 16.f))
			ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_LEFT, 1.f);
		else if (CFrontEnd::MouseClickedInRect(x + 60.f, y, x + 120.f, y + 16.f))
			ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_RIGHT, 1.f);
	}
}

// FUNCTION: 0x004a3b10
CMenuMenuItem::CMenuMenuItem(EPCText text, int id, bool deferred) : CMenuItem(text, id)
{
	mDeferred = deferred;
	mExpanded = false;
}

// FUNCTION: 0x004a3b50
void	CMenuMenuItem::Refresh()
{
	mSelection = mValue = GetValue();
}

// FUNCTION: 0x004a3b60
CBOOLMenuItem::CBOOLMenuItem(EPCText text, int id, bool deferred) : CMenuMenuItem(text, id, deferred)
{
}

// FUNCTION: 0x004a3ba0
void	CMenuMenuItem::ClearDeferredRender()
{
	mRenderOnTop = NULL;
}

// FUNCTION: 0x004a3bb0
void	CMenuMenuItem::DeferredRender()
{
	CMenuMenuItem	*item = mRenderOnTop;
	mRenderOnTop = NULL;
	if (item)
		item->RenderList(mRenderOnTopX, mRenderOnTopY, TRUE);
}

// FUNCTION: 0x004a3be0
void	CMenuMenuItem::Render(float x, float y, BOOL selected)
{
	if (selected && !mRenderOnTop)
	{
		mRenderOnTopX = x;
		mRenderOnTopY = y;
		mRenderOnTop = this;
		return;
	}
	RenderList(x, y, FALSE);
}

// FUNCTION: 0x004a3c30
void	CMenuMenuItem::RenderList(float x, float y, BOOL on_top)
{
	WCHAR	text[100];
	int		value = GetValue();

	if (value != mValue)
	{
		mValue = value;
		mSelection = value;
	}

	wcscpy(text, GetText());

	DWORD	col = mColour;
	if (mValue != mSelection)
	{
		float	f = (float(cos(float(fmod(PLATFORM.GetSysTimeFloat(), 2.0)) * (PI * 2.f))) + 1.f) * 0.5f;
		int		c = 255 - int(f * 255.f);
		col = 0xff000000 | (c << 16) | (c << 8) | c;
	}
	if (on_top)
		col = (col & 0xffff0000) + ((((col & 0xff00) * 204) >> 8) & 0xff00);
	if (!mEnabled)
		col = 0xff505050;

	SIZE	size;
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
	float	scale = 1.f;
	float	tx = x - size.cx;
	float	lx = tx + size.cx + 2.f;
	if (tx < 5.f)
	{
		scale = (x - 5.f) / size.cx;
		tx = 5.f;
	}
	PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(tx, y, col, text, 0, 0, scale, 1.f);

	if (mExpanded)
	{
		int		widest = 0;
		int		i;
		for (i = 0; i < GetNumChoices(); i++)
		{
			SIZE	s;
			PLATFORM.Font(FONT_SMALL)->GetTextExtent(GetChoiceText(i), &s);
			if (s.cx > widest)
				widest = s.cx;
		}

		int		top = int(y - ((GetNumChoices() - 1) * size.cy) / 2);
		if (!CPauseMenu::mBlankTexture)
			CPauseMenu::LoadBlankTexture();

		float	ly = float(top);
		float	yscale = 1.f;
		int		n = GetNumChoices();
		float	height = float(n * size.cy);
		if (ly < 0.f)
			ly = 0.f;
		if (height + ly > 480.f)
		{
			if (height > 480.f)
			{
				yscale = 480.f / height;
				ly = 0.f;
			}
			else
				ly = 480.f - height;
		}

		DWORD	back;
		if (FRONTEND.GetCurrentPage() == FEP_OPTIONS)
			back = 0xff283969;
		else
			back = 0xff404040;
		CSPRITERENDERER::DrawSprite(lx, ly, 0.004f, CPauseMenu::mBlankTexture, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, back,
									(widest + 3) / 16.f, (GetNumChoices() * size.cy) * yscale / 16.f, 0.f, 1.f, 0.f, 1.f);

		float	rowh = size.cy * yscale;
		for (i = 0; i < n; i++)
		{
			DWORD	c = 0xff404040;
			if (i == mSelection)
				c = 0xffffffff;

			float	x2 = size.cx + lx + 2.f;
			float	ny = ly + rowh;
			if (CFrontEnd::GetCursorStateInRect(lx + 2.f, ly, x2, ny))
				mSelection = i;

			PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(lx + 2.f, ly, c, GetChoiceText(i), 0, 0, 1.f, yscale);

			float	cy2 = ly + size.cy;
			float	cx2 = size.cx + lx + 2.f;
			if (CFrontEnd::MouseClickedInRect(lx + 2.f, ly, cx2, cy2))
			{
				mSelection = i;
				mExpanded = false;
				if ((!mDeferred) && (mValue != i))
				{
					mValue = i;
					SetValue(i);
				}
				CFrontEnd::PlaySound(FES_SELECT);
			}
			ly = ny;
		}

		if ((!FEMESSBOX.IsInteractive()) && MOUSE_BUTTONS[2].mDown)
		{
			mExpanded = false;
			mSelection = mValue;
			MOUSE_BUTTONS[2].mDown = FALSE;
			CFrontEnd::PlaySound(FES_BACK);
		}
	}
	else
		PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(lx, y, col, GetChoiceText(mSelection), 0, 0, 1.f, 1.f);
}

// FUNCTION: 0x004a40f0
void	CMenuMenuItem::Commit()
{
	if (mSelection != mValue)
	{
		SetValue(mSelection);
		mValue = mSelection;
	}
}

// FUNCTION: 0x004a4110
void	CMenuMenuItem::ReceiveButtonAction(CController *from_controller, int button, float val)
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_DOWN:
	case BUTTON_FRONTEND_MENU_RIGHT:
		CFrontEnd::PlaySound(FES_MOVE);
		mSelection++;
		if (mSelection >= GetNumChoices())
			mSelection = GetNumChoices() - 1;
		mExpanded = true;
		break;
	case BUTTON_FRONTEND_MENU_UP:
	case BUTTON_FRONTEND_MENU_LEFT:
		CFrontEnd::PlaySound(FES_MOVE);
		if (--mSelection < 0)
			mSelection = 0;
		mExpanded = true;
		break;
	case BUTTON_FRONTEND_MENU_SELECT:
		CFrontEnd::PlaySound(FES_SELECT);
		if ((!mExpanded) && (GetNumChoices() < 2))
		{
			CFrontEnd::PlaySound(FES_BACK);
			return;
		}
		mExpanded ^= 1;
		if ((!mExpanded) && (!mDeferred) && (mValue != mSelection))
		{
			mValue = mSelection;
			SetValue(mSelection);
		}
		break;
	case BUTTON_FRONTEND_MENU_BACK:
		if (mExpanded)
		{
			CFrontEnd::PlaySound(FES_BACK);
			mExpanded = false;
			mSelection = mValue;
		}
		break;
	}
}

// FUNCTION: 0x004a4220
WCHAR	*CBOOLMenuItem::GetChoiceText(int n)
{
	if (!n)
		return Localization_GetStringById(6);
	return Localization_GetStringById(5);
}

// FUNCTION: 0x004a4250
CApplyMenuItem::CApplyMenuItem(CMenuOptionsList *list) : CMenuItem(0, 3)
{
	mList = list;
}

// FUNCTION: 0x004a4290
void	CApplyMenuItem::ReceiveButtonAction(CController *from_controller, int button, float val)
{
	if (button == BUTTON_FRONTEND_MENU_SELECT)
	{
		gMenuApplyCount++;

		CMenuItem	*item;
		FOR_ALL_ITEMS_IN(mList->mItems, item)
			item->Commit();
	}
}

// FUNCTION: 0x004a42f0
bool	CMenuMenuItem::HasPendingChange()
{
	return mDeferred && (mValue != mSelection);
}

// FUNCTION: 0x004a4310
void	CApplyMenuItem::Render(float x, float y, BOOL selected)
{
	DWORD	col = 0xffffffff;

	if (gMenuChangesPending)
	{
		float	f = (float(cos(float(fmod(PLATFORM.GetSysTimeFloat(), 2.0)) * (PI * 2.f))) + 1.f) * 0.5f;
		int		c = 255 - int(f * 255.f);
		col = 0xff000000 | (c << 16) | (c << 8) | c;
	}

	CMenuItem::Render(x, y, selected, col, GetText());
}

// FUNCTION: 0x004a43a0
void	CScaleMenuItem::ReceiveButtonAction(CController *from_controller, int button, float val)
{
	BOOL	changed = FALSE;

	switch (button)
	{
	case BUTTON_FRONTEND_MENU_RIGHT:
		mValue++;
		if (mValue > mSteps)
			mValue = mSteps;
		else
		{
			ValueChanged();
			CFrontEnd::PlaySound(FES_MOVE);
		}
		changed = TRUE;
		break;
	case BUTTON_FRONTEND_MENU_LEFT:
		if (--mValue < 0)
			mValue = 0;
		else
		{
			ValueChanged();
			CFrontEnd::PlaySound(FES_MOVE);
		}
		changed = TRUE;
		break;
	case BUTTON_FRONTEND_MENU_SELECT:
		if (mMenu.ToRead())
			mMenu->MenuItemChanged(this);
		mCommittedValue = mValue;
		break;
	}

	if (mImmediate && changed)
	{
		if (mMenu.ToRead())
			mMenu->MenuItemChanged(this);
		mCommittedValue = mValue;
	}
}

// FUNCTION: 0x004a4450
int		CScaleMenuItem::GetWidth()
{
	SIZE	size;
	WCHAR	*text = GetText();
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
	return size.cx + 106;
}

// FUNCTION: 0x004a4480
void	CScaleMenuItem::LoadResources()
{
	mArrow = CDXTexture::FindTexture("FrontEnd\\v2\\FE_Arrow.tga", 4, 0, 1, 0, 1);
	mBlank = CDXTexture::FindTexture("FrontEnd\\v2\\FE_Blank.tga", 4, 0, 1, 0, 1);
}

// FUNCTION: 0x004a44c0
void	CMenuMenuItem::SetSelection(int n)
{
	mSelection = n;
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004a3610 ??_GCScaleMenuItem@@UAEPAXI@Z
