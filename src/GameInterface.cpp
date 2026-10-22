// GameInterface.cpp: the in-game menu (GameInterface.obj, 004728b0..004740bf), reconstructed from the retail code;
// Stuart's release has no copy. Two pages of up to six items: the game menu (continue, message log, level briefing,
// restart, quit, options) and the options (god mode, auto-aim, back). The joypad picture ("Interface\Joypad.tga")
// replaces the menu while it is shown.

#include "common.h"
#include "GameInterface.h"
#include "game.h"
#include "player.h"
#include "Texture.h"
#include "frontend.h"
#include "MessageBox.h"
#include "Controller.h"
#include "DXEngine.h"
#include "SpriteRenderer.h"
#include "text.h"
#include "renderinfo.h"
#include "mouse.h"
#include "Camera.h"
#include "Platform.h"

// FUNCTION: 0x004728b0 init:ZERO_FVECTOR
// FUNCTION: 0x004728d0 init:ID_FMATRIX

CGameInterface GAMEINTERFACE;			// 00679fa8
// FUNCTION: 0x004729a0 init:GAMEINTERFACE
// FUNCTION: 0x004729c0 exit:GAMEINTERFACE
// FUNCTION: 0x004729d0 ??0CGameInterface@@QAE@XZ

//******************************************************************************************
// Init and Reset are the same code (the linker folds them; CGame::InitRestartLoop calls Reset, 0046c5b7).
void CGameInterface::Init()	// FUNCTION: 0x004729e0
{
	mUnknown10 = 0;
	mShowJoypad = FALSE;
	mUnknown18 = 0;
	mMenuUp = FALSE;
	mSelection = 0;
	mRenderBackground = TRUE;
	mYBoxOffset = 0;
	mPage = 1;
	for (int i = 0; i < 6; i++)
		mAvailable[i] = TRUE;
}

void CGameInterface::Reset()	// FUNCTION: 0x004729e0
{
	mUnknown10 = 0;
	mShowJoypad = FALSE;
	mUnknown18 = 0;
	mMenuUp = FALSE;
	mSelection = 0;
	mRenderBackground = TRUE;
	mYBoxOffset = 0;
	mPage = 1;
	for (int i = 0; i < 6; i++)
		mAvailable[i] = TRUE;
}

//******************************************************************************************
void CGameInterface::InitResources()	// FUNCTION: 0x00472a10
{
	mJoypad = CDXTexture::FindTexture("Interface\\Joypad.tga", 4, 0, 1, 1, 1);
	mBackground = CDXTexture::FindTexture("hud\\Menu background.tga", 0, 0, 1, 1, 1);
}

//******************************************************************************************
void CGameInterface::Shutdown()	// FUNCTION: 0x00472a50
{
	if (mJoypad)
	{
		mJoypad->Release();
		mJoypad = NULL;
	}
	if (mBackground)
	{
		mBackground->Release();
		mBackground = NULL;
	}
	mJoypad = NULL;

	CMonitor::Shutdown();
}

//******************************************************************************************
void CGameInterface::ToggleMenuDisplay()	// FUNCTION: 0x00472a90
{
	mMenuUp = !mMenuUp;
	if (mMenuUp)
	{
		mSelection = 0;
		while ((!mAvailable[mSelection]) && (mSelection < 6))
			mSelection++;
		mShowJoypad = FALSE;
		ShutdownMouse();
	}
	else
		InitMouse();
}

//******************************************************************************************
// The previous available item, if there is one.
void CGameInterface::MenuUp()
{
	if (!mMenuUp)
		return;

	int		old = mSelection;
	mSelection--;
	BOOL	wrapped = FALSE;

	while ((mSelection < 0) || ((!mAvailable[mSelection]) && (!wrapped)))
	{
		mSelection--;
		if (mSelection < 0)
		{
			wrapped = TRUE;
			mSelection = old;
		}
	}

	if (mSelection < 0)
		mSelection = 0;

	if (mSelection != old)
		CFrontEnd::PlaySound(FES_MOVE);
}

//******************************************************************************************
// The next available item, if there is one.
void CGameInterface::MenuDown()	// FUNCTION: 0x00472ad0
{
	if (!mMenuUp)
		return;

	int		last = 5;
	if (mPage == 1)
		last = 5;
	if (mPage == 2)
		last = 3;

	int		old = mSelection;
	mSelection++;
	BOOL	wrapped = FALSE;

	while ((mSelection > last) || ((!mAvailable[mSelection]) && (!wrapped)))
	{
		mSelection++;
		if (mSelection > last)
		{
			wrapped = TRUE;
			mSelection = old;
		}
	}

	if (mSelection > last)
		mSelection = last;

	if (mSelection != old)
		CFrontEnd::PlaySound(FES_MOVE);
}

//******************************************************************************************
void CGameInterface::SelectItem(CController *from_controller)	// FUNCTION: 0x00472b40
{
	if (!mMenuUp)
		return;

	if (mPage == 1)
	{
		if (mSelection == 0)
		{
			// continue
			from_controller->RelinquishControl();
			GAME.UnPause();
			ToggleMenuDisplay();
		}
		if (mSelection == 1)
		{
			ToggleMenuDisplay();
			from_controller->SetToControl(GAME.GetMessageLog());
			GAME.GetMessageLog()->Activate();
		}
		if (mSelection == 2)
		{
			ToggleMenuDisplay();
			from_controller->SetToControl(GAME.GetLevelBriefingLog());
			GAME.GetLevelBriefingLog()->Activate();
		}
		if (mSelection == 3)
		{
			CFrontEnd::PlaySound(FES_SELECT);
			GAME.SetQuit(QT_RESTART_LEVEL);
		}
		if (mSelection == 4)
		{
			CFrontEnd::PlaySound(FES_SELECT);
			GAME.SetQuit(QT_USER_QUIT_TO_FRONTEND);
		}
		if (mSelection == 5)
		{
			mPage = 2;
			mSelection = 0;
			return;
		}
	}

	if (mPage == 2)
	{
		if (mSelection == 0)
		{
			if (GAME.GetPlayer(0)->IsGod())
				GAME.GetPlayer(0)->SetIsGod(FALSE);
			else
				GAME.GetPlayer(0)->SetIsGod(TRUE);
		}
		if (mSelection == 1)
		{
			if (GAME.GetAllowedAutoAim() == TRUE)
				GAME.SetAllowedAutoAim(FALSE);
			else if (GAME.GetPlayer(0)->GetPreferredCurrentViewMode() != 2)
				GAME.SetAllowedAutoAim(TRUE);
		}
		if (mSelection == 2)
		{
			mPage = 1;
			mSelection = 0;
		}
	}
}

//******************************************************************************************
void CGameInterface::ReceiveButtonAction(CController *from_controller, int button, float val)	// FUNCTION: 0x00472d50
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_UP:
		if (!mShowJoypad)
			MenuUp();
		break;

	case BUTTON_FRONTEND_MENU_DOWN:
		if (!mShowJoypad)
			MenuDown();
		break;

	case BUTTON_PAUSE_MENU_SELECT:
		if (!mShowJoypad)
			SelectItem(from_controller);
		break;

	case BUTTON_HELP:
		if (mShowJoypad == TRUE)
		{
			from_controller->RelinquishControl();
			GAMEINTERFACE.mShowJoypad = !GAMEINTERFACE.mShowJoypad;
			GAME.UnPause();
		}
		break;
	}
}

//******************************************************************************************
// The menu's background at the centre (x, y), w by h, tiled a little past its edges; the engine's default texture
// in black when the background did not load.
void CGameInterface::DrawBackground(float x, float y, float w, float h, DWORD colour)	// FUNCTION: 0x00472e50
{
	CDXTexture	*tex = mBackground;
	if (!tex)
	{
		tex = ENGINE.GetDefaultTexture();
		if (!tex)
			return;
		colour = 0xff000000;
	}

	CSPRITERENDERER::DrawSprite(x, y, 0.001f, tex, SA_CENTRE, SPT_NONE, 1.f, 0.f, colour, w / tex->mWidth, h / tex->mHeight,
								0.f, 1.f + 1.f / w, 0.f, 1.f + 1.f / h);
}

extern bool	gInFrontEnd;					// 00679b40, defined in FrontEnd.cpp: the menu draws as the front end does

//******************************************************************************************
// The joypad picture's labels: where each sits on the picture, and whether it reads from that point (1) or ends there (0).
static char	sJoypadLabels[7][64] = { "Movement", "Aiming", "Fire", "Weapon Select", "Pause", "Transform", "Help" };	// 0062c65c
static SINT	sJoypadLabelX[7] = { 103, 153, 182, 69, 121, 253, 253 };		// 0062c81c
static SINT	sJoypadLabelY[7] = { 202, 202, 27, 27, 58, 126, 86 };			// 0062c838
static SINT	sJoypadLabelFrom[7] = { 0, 1, 1, 0, 1, 1, 1 };				// 0062c854

// the selected item's colour, pulsing red
static inline DWORD	Pulse(SINT alpha)
{
	SINT	p = abs((SINT((float)(PLATFORM.GetSysTimeFloat() * 2.7f) * 100.f) % 256) - 128);
	return ((alpha << 24) | 0x400000) + (p << 16) + ((p / 4) << 8) + (p / 4);
}

//******************************************************************************************
void CGameInterface::Render()	// FUNCTION: 0x00472f10
{
	RENDERINFO.SetFogEnabled(false);

	// the joypad picture fades in and out
	if (mShowJoypad)
	{
		mUnknown10 += 16;
		if (mUnknown10 > 255)
			mUnknown10 = 255;
	}
	else
	{
		mUnknown10 -= 16;
		if (mUnknown10 < 0)
			mUnknown10 = 0;
	}
	if (mUnknown10 != 255)
		GAME.GetCamera(0)->GetShowHUD();

	SINT	fade = mUnknown10;
	if (fade)
	{
		DWORD	col = (SINT(fade * 0.75f) << 24) | 0xffffff;
		if (mJoypad)
		{
		RENDERINFO.SetFogEnabled(false);
		SINT	w = mJoypad->mWidth;
		SINT originX = (PLATFORM.GetWindowWidth() / 2) - (w / 2);
		SINT	h = mJoypad->mHeight;
		SINT originY = (PLATFORM.GetWindowHeight() / 2) - (h / 2);
		float	y = (float)originY;
		float	x = (float)originX;
		CSPRITERENDERER::DrawColouredSprite(x, y, 0.001f, mJoypad, col, 1.f, 1.f, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f);

		for (SINT i = 0; i < 7; i++)
		{
			SIZE	size;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(ToWCHAR(sJoypadLabels[i]), &size);
			size.cx /= 2;
			size.cy /= 2;
			if (sJoypadLabelFrom[i])
				PLATFORM.Font(FONT_NORMAL)->DrawTextScaled(sJoypadLabelX[i] + x, sJoypadLabelY[i] + y, 0.1f, 0.5f, 0.5f, col, ToWCHAR(sJoypadLabels[i]), 0, NULL);
			else
				PLATFORM.Font(FONT_NORMAL)->DrawTextScaled(sJoypadLabelX[i] + x - size.cx, sJoypadLabelY[i] + y, 0.1f, 0.5f, 0.5f, col, ToWCHAR(sJoypadLabels[i]), 0, NULL);
		}
		}
	}

	// the menu fades in and out
	if (mMenuUp)
	{
		mUnknown18 += 32;
		if (mUnknown18 > 255)
			mUnknown18 = 255;
	}
	else
	{
		mUnknown18 -= 32;
		if (mUnknown18 < 0)
			mUnknown18 = 0;
	}

	bool	clicked = false;
	WCHAR	text[64];
	DWORD	col;

	if ((mPage == 1) && (mUnknown18) && (!GAME.IsFreeCameraOn(0)))
	{
		SIZE	size;
		gInFrontEnd = TRUE;
		SINT	alpha = SINT(mUnknown18 * 0.75f);
		float	cy = ENGINE.GetCurrentViewportHeight() - ENGINE.GetCurrentViewportHeight() * 0.5f + 15.f;
		float	viewportX = (float)ENGINE.GetCurrentViewportX();
		float	cx = ENGINE.GetCurrentViewportWidth() * 0.5f + viewportX;
		SINT	back = alpha;
		if (alpha > 0xa0)
			back = 0xa0;
		if (mRenderBackground)
			CSPRITERENDERER::DrawSprite(cx, cy, 0.001f, mBackground, SA_CENTRE, SPT_NONE, 1.f, 0.f, (back << 24) | 0x505050, 0.34f, 0.8f, 0.f, 1.f, 0.f, 1.f);

		SINT	y = mYBoxOffset - (PLATFORM.Font(FONT_NORMAL)->GetHeight() * 5) / 2 + 240;
		wcscpy(text, TEXT.GetStringById(0x130ed7));
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		if (mRenderBackground)
			PLATFORM.Font(FONT_NORMAL)->DrawText(320.f - size.cx / 2, (float)y, (alpha << 24) | 0x505050, text, 0, 0, 1.f);
		y += size.cy;

		if (mSelection == 0)
			col = Pulse(alpha);
		else if (mAvailable[0] == 1)
			col = (alpha << 24) | 0xf0f0f0;
		else
			col = (alpha << 24) | 0x202020;
		wcscpy(text, TEXT.GetStringById(0x41d6e9));
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		if ((mAvailable[0]) || (mRenderBackground == TRUE))
		{
			PLATFORM.Font(FONT_NORMAL)->DrawText(320.f - size.cx / 2, (float)y, col, text, 0, 0, 1.f);
		}
		y += 0x18;
		if (mSelection == 1)
			col = Pulse(alpha);
		else if (mAvailable[1] == 1)
			col = (alpha << 24) | 0xf0f0f0;
		else
			col = (alpha << 24) | 0x202020;
		wcscpy(text, TEXT.GetStringById(0x1f08cea));
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		if ((mAvailable[1]) || (mRenderBackground == TRUE))
		{
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow(320.f - size.cx / 2, (float)y, col, text, 0, 0.f, 1.f, 1.f);
		}
		y += 0x18;
		if (mSelection == 2)
			col = Pulse(alpha);
		else if (mAvailable[2] == 1)
			col = (alpha << 24) | 0xf0f0f0;
		else
			col = (alpha << 24) | 0x202020;
		wcscpy(text, TEXT.GetStringById(0x3fc4f9));
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		if ((mAvailable[2]) || (mRenderBackground == TRUE))
		{
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow(320.f - size.cx / 2, (float)y, col, text, 0, 0.f, 1.f, 1.f);
		}
		y += 0x18;
		if (mSelection == 3)
			col = Pulse(alpha);
		else if (mAvailable[3] == 1)
			col = (alpha << 24) | 0xf0f0f0;
		else
			col = (alpha << 24) | 0x202020;
		wcscpy(text, TEXT.GetStringById(0xcada9));
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		if ((mAvailable[3] == 1) || (mRenderBackground == TRUE))
		{
			if (MouseInRect(320.f - size.cx / 2, (float)y, 320.f + size.cx / 2, (float)(y + size.cy)))
				mSelection = 3;
			if (MouseClickedInRect(320.f - size.cx / 2, (float)y, 320.f + size.cx / 2, (float)(y + size.cy)))
			{
				mSelection = 3;
				clicked = true;
			}
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow(320.f - size.cx / 2, (float)y, col, text, 0, 0.f, 1.f, 1.f);
		}
		y += 0x18;
		if (mSelection == 4)
			col = Pulse(alpha);
		else if (mAvailable[4] == 1)
			col = (alpha << 24) | 0xf0f0f0;
		else
			col = (alpha << 24) | 0x202020;
		wcscpy(text, TEXT.GetStringById(0x7a211));
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		if ((mAvailable[4] == 1) || (mRenderBackground == TRUE))
		{
			if (MouseInRect(320.f - size.cx / 2, (float)y, 320.f + size.cx / 2, (float)(y + size.cy)))
				mSelection = 4;
			if (MouseClickedInRect(320.f - size.cx / 2, (float)y, 320.f + size.cx / 2, (float)(y + size.cy)))
			{
				mSelection = 4;
				clicked = true;
			}
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow(320.f - size.cx / 2, (float)y, col, text, 0, 0.f, 1.f, 1.f);
		}
		y += 0x18;
		if (mSelection == 5)
			col = Pulse(alpha);
		else if (mAvailable[5] == 1)
			col = (alpha << 24) | 0xf0f0f0;
		else
			col = (alpha << 24) | 0x202020;
		wcscpy(text, TEXT.GetStringById(0x265233));
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		if ((mAvailable[5]) || (mRenderBackground == TRUE))
		{
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow(320.f - size.cx / 2, (float)y, col, text, 0, 0.f, 1.f, 1.f);
		}
		y += 0x18;

		RenderMouseCursor();
		gInFrontEnd = FALSE;
	}

	if ((mPage == 2) && (mUnknown18) && (!GAME.IsFreeCameraOn(0)))
	{
		SIZE	size;
		SINT	alpha = SINT(mUnknown18 * 0.75f);
		float	cy = ENGINE.GetCurrentViewportHeight() - ENGINE.GetCurrentViewportHeight() * 0.5f - 10.f;
		float	viewportX = (float)ENGINE.GetCurrentViewportX();
		float	cx = ENGINE.GetCurrentViewportWidth() * 0.5f + viewportX;
		SINT	back = alpha;
		if (alpha > 0xa0)
			back = 0xa0;
		if (mRenderBackground)
			CSPRITERENDERER::DrawSprite(cx, cy, 0.001f, mBackground, SA_CENTRE, SPT_NONE, 1.f, 0.f, (back << 24) | 0x505050, 0.34f, 0.6f, 0.f, 1.f, 0.f, 1.f);

		SINT fontHeight = PLATFORM.Font(FONT_NORMAL)->GetHeight();
		SINT	y = (PLATFORM.GetWindowHeight() / 2) - (fontHeight * 5) / 2;
		y += mYBoxOffset;
		wcscpy(text, TEXT.GetStringById(0x265233));
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		if (mRenderBackground)
			PLATFORM.Font(FONT_NORMAL)->DrawText((float)(PLATFORM.GetWindowWidth() / 2) - (size.cx / 2), (float)y, (alpha << 24) | 0x505050, text, 0, 0, 1.f);
		y += size.cy;

		if (mSelection == 0)
			col = Pulse(alpha);
		else if (mAvailable[0] == 1)
			col = (alpha << 24) | 0xf0f0f0;
		else
			col = (alpha << 24) | 0x202020;
		if (GAME.GetPlayer(0)->IsGod())
			wcscpy(text, TEXT.GetStringById(0x148fe9));
		else
			wcscpy(text, TEXT.GetStringById(0x226f16));
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		if ((mAvailable[0]) || (mRenderBackground == TRUE))
		{
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow((float)(PLATFORM.GetWindowWidth() / 2) - (size.cx / 2), (float)y, col, text, 0, 0.f, 1.f, 1.f);
		}
		y += 0x18;
		if (mSelection == 1)
			col = Pulse(alpha);
		else if (mAvailable[1] == 1)
			col = (alpha << 24) | 0xf0f0f0;
		else
			col = (alpha << 24) | 0x202020;
		if (GAME.GetAllowedAutoAim() == TRUE)
			wcscpy(text, TEXT.GetStringById(0x20df719));
		else
			wcscpy(text, TEXT.GetStringById(0x3ba2e96));
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
		if ((mAvailable[2]) || (mRenderBackground == TRUE))
		{
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow((float)(PLATFORM.GetWindowWidth() / 2) - (size.cx / 2), (float)y, col, text, 0, 0.f, 1.f, 1.f);
		}
		y += 0x18;
		if (mSelection == 2)
			col = Pulse(alpha);
		else if (mAvailable[2] == 1)
			col = (alpha << 24) | 0xf0f0f0;
		else
			col = (alpha << 24) | 0x202020;
		wcscpy(text, TEXT.GetStringById(0x6f9da));
		PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
		if ((mAvailable[3] == 1) || (mRenderBackground == TRUE))
		{
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow((float)(PLATFORM.GetWindowWidth() / 2) - (size.cx / 2), (float)y, col, text, 0, 0.f, 1.f, 1.f);
		}
		y += 0x18;
	}

	// a click picks the item for whichever controller has the menu
	if (clicked)
	{
		if ((GAME.GetController(0)) && (GAME.GetController(0)->GetToControl() == this))
			SelectItem(GAME.GetController(0));
		else if ((GAME.GetController(1)) && (GAME.GetController(1)->GetToControl() == this))
			SelectItem(GAME.GetController(1));
	}

	RENDERINFO.SetFogEnabled(true);
}
