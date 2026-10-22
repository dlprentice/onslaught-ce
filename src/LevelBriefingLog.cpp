// LevelBriefingLog.cpp: the level briefing log, the briefing and objectives shown from the pause menu
// (reconstructed from LevelBriefingLog.obj, 0048f450..0048ffd4; Stuart's release has no copy, only game.h's
// include of LevelBriefingLog.h). It draws on the message log's panel.

#include	"Common.h"

#include	"DebugLog.h"
#include	"MessageBox.h"
#include	"frontend.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"text.h"
#include	"mouse.h"

// CGameMenu's empty constructor, inlined by this object's constructor (0048f55f, 004b8df3); PauseMenu.cpp keeps the
// out-of-line copy CPauseMenu::ButtonPressed calls (004d0e40), so the definition is repeated inline here
inline CGameMenu::CGameMenu() {}

// FUNCTION: 0x0048f450 init:ZERO_FVECTOR
// FUNCTION: 0x0048f470 init:ID_FMATRIX
// FUNCTION: 0x0048f5a0 ??_GCLevelBriefingLog@@UAEPAXI@Z

//*********************************************************************************
CLevelBriefingLog::CLevelBriefingLog()	// FUNCTION: 0x0048f540
	: mActive(FALSE), mFade(0.f)
{
	mBlank = CDXTexture::FindTexture("FrontEnd\\v2\\FE_Blank.tga", 4, 0, 1, 0, 1);
}

//*********************************************************************************
CLevelBriefingLog::~CLevelBriefingLog()	// FUNCTION: 0x0048f5c0
{
	if (mBlank)
	{
		mBlank->Release();
		mBlank = NULL;
	}
}

//*********************************************************************************
void	CLevelBriefingLog::Render(CViewport *viewport)	// FUNCTION: 0x0048f620
{
	if (mFade <= 0.f)
		return;

	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	SetMipFilterNone(0);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ALPHAREF, 8);
	RENDERINFO.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	RENDERINFO.SRS(D3DRS_ZWRITEENABLE, FALSE);
	RENDERINFO.SRS(D3DRS_ZENABLE, FALSE);
	RENDERINFO.Apply();

	// darken the game behind the log
	if (mActive)
	{
		RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		RENDERINFO.Apply();
		CSPRITERENDERER::DrawSprite(0, 0, 0.01f, mBlank, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f, 0xc0101010, 40.f, 30.f, 0.f, 1.f, 0.f, 1.f);
	}

	float ft = 60.f / PLATFORM.GetFPS();

	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.Apply();
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	RENDERINFO.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.Apply();

	// the panel grows from the centre as the log fades in
	CMessageLog *log = GAME.GetMessageLog();
	int x = int(320.f - mFade * 290.f);
	int y = x;
	int w = int(mFade * 580.f);
	int h = int(mFade * 420.f);
	if (GAME.IsMultiplayer())
	{
		h >>= 1;
		y = 140;
	}
	log->RenderPanel(x, y, w, h, mFade);

	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.Apply();
	RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

	if (!mActive)
	{
		mFade -= ft * 0.1f;
		return;
	}

	if (mFade < 1.f)
	{
		mFade += ft * 0.1f;
		if (mFade > 1.f)
			mFade = 1.f;
		return;
	}

	SIZE			s2;
	WCHAR			title[256];
	SWordWrapFill	fill;
	int				c0, c1;
	float			ty = 60.f;

	// the briefing, one paragraph at a time
	DWORD briefing = CFrontEndText::GetBriefingTextId(GAME.GetCurrentLevel());
	if (!GAME.IsMultiplayer())
	{
		for (c0 = 0; c0 < 9; c0 ++)
		{
			WCHAR *text = TEXT.GetFollowingString(briefing, c0);
			PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &s2);
			int num_lines = PLATFORM.Font(FONT_SMALL)->WordWrap(&fill, text, 525.f);
			for (c1 = 0; c1 < num_lines; c1 ++)
			{
				SIZE s;
				PLATFORM.Font(FONT_SMALL)->GetTextExtent(fill.c[c1], &s);
				PLATFORM.Font(FONT_SMALL)->DrawTextShadowed(60.f, ty, 0xffffffff, fill.c[c1]);
				ty += 16.f;
			}
			if (num_lines > 0)
			{
				if (GAME.GetCurrentLevel() == 611)
					ty += 16.f;
				else
					ty += 20.f;
			}
		}
	}

	// the objectives: their texts, then (on a second pass, beside the longest) a mark for each one's state
	wcscpy(title, TEXT.GetString(0xf4213fbf));
	ty = 179.f;
	if (GAME.IsMultiplayer())
		ty = 100.f;

	BOOL found = FALSE;
	for (c0 = 0; c0 < GAME.GetMaxPrimaryObjectives(); c0 ++)
		if (GAME.GetPrimaryObjective(c0)->GetStatus() != MOS_NOT_DEFINED)
			found = TRUE;

	if (found)
		PLATFORM.Font(FONT_SMALL)->DrawTextShadowed(60.f, ty + 60.f, 0xffffffff, title);

	float	top = ty + 16.f;
	int		longest = 0;
	float	scale = 1.f;
	if ((GAME.GetCurrentLevel() == 611) && (TEXT.GetLanguage() == 2))
		scale = 0.97f;

	int pass;
	for (pass = 0; pass < 2; pass ++)
	{
		ty = top;
		for (c0 = 0; c0 < GAME.GetMaxPrimaryObjectives(); c0 ++)
		{
			CMissionObjective *o = GAME.GetPrimaryObjective(c0);
			if (o->GetStatus() == MOS_NOT_DEFINED)
				continue;

			WCHAR *text = TEXT.GetString(o->GetStringID());
			PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &s2);
			if (s2.cx > longest)
				longest = s2.cx;

			if (pass == 0)
			{
				if (scale < 0.99f)
					PLATFORM.Font(FONT_SMALL)->DrawTextScaled(60.f, ty + 60.f, 1.f, scale, scale, 0xffffef8f, text, 0, 0);
				else
					PLATFORM.Font(FONT_SMALL)->DrawTextShadowed(60.f, ty + 60.f, 0xffffef8f, text);
			}
			else
			{
				WCHAR	mark[2];
				mark[1] = 0;
				mark[0] = 0x2510;
				DWORD col = 0xfff0402f;
				if (o->GetStatus() == MOS_COMPLETE)
				{
					mark[0] = 0x250c;
					col = 0xff40f02f;
				}
				if (scale < 0.99f)
					PLATFORM.Font(FONT_SMALL)->DrawTextScaled(float(longest) * scale + 68.f, ty + 60.f, 1.f, scale, scale, col, mark, 0, 0);
				else
					PLATFORM.Font(FONT_SMALL)->DrawTextShadowed(float(longest) + 68.f, ty + 60.f, col, mark);
			}
			ty += scale * 16.f;
		}
	}

	ty += 16.f;

	found = FALSE;
	for (c0 = 0; c0 < GAME.GetMaxSecondaryObjectives(); c0 ++)
		if (GAME.GetSecondaryObjective(c0)->GetStatus() != MOS_NOT_DEFINED)
			found = TRUE;

	if (found)
	{
		wcscpy(title, TEXT.GetString(0xc4afbca7));
		PLATFORM.Font(FONT_SMALL)->DrawTextShadowed(60.f, ty + 60.f, 0xffffffff, title);

		top = ty + 16.f;
		longest = 0;
		for (pass = 0; pass < 2; pass ++)
		{
			ty = top;
			for (c0 = 0; c0 < GAME.GetMaxSecondaryObjectives(); c0 ++)
			{
				CMissionObjective *o = GAME.GetSecondaryObjective(c0);
				if (o->GetStatus() == MOS_NOT_DEFINED)
					continue;

				WCHAR *text = TEXT.GetString(o->GetStringID());
				PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &s2);
				if (s2.cx > longest)
					longest = s2.cx;

				if (pass == 0)
					PLATFORM.Font(FONT_SMALL)->DrawTextShadowed(60.f, ty + 60.f, 0xffffef8f, text);
				else
				{
					WCHAR	mark[2];
					mark[1] = 0;
					mark[0] = 0x2510;
					DWORD col = 0xfff0402f;
					if (o->GetStatus() == MOS_COMPLETE)
					{
						mark[0] = 0x250c;
						col = 0xff40f02f;
					}
					PLATFORM.Font(FONT_SMALL)->DrawTextShadowed(float(longest) + 68.f, ty + 60.f, col, mark);
				}
				ty += 16.f;
			}
		}
	}

	// a right click, or a click on the back arrow, closes the log
	if (MOUSE_BUTTONS[2].mDown)
	{
		MOUSE_BUTTONS[2].mDown = FALSE;
		ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_BACK, 1.f);
	}

	if (CMessageLog::mArrow)
		CSPRITERENDERER::DrawSprite(24.f, 454.f, 0.001f, CMessageLog::mArrow, SA_CENTRE, SPT_NONE, 1.f, PI, 0xffffffff, 1.f, 1.f, 0.f, 1.f, 0.f, 1.f);

	if (MouseClickedInRect(0, 400, 200, 480))
		ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_BACK, 1.f);

	RenderMouseCursor();
}

//*********************************************************************************
void	CLevelBriefingLog::Activate()	// FUNCTION: 0x0048ff90
{
	mActive = TRUE;
	mFade = 0.1f;
}

//*********************************************************************************
void	CLevelBriefingLog::ReceiveButtonAction(CController* from_controller, int button, float val)	// FUNCTION: 0x0048ffa0
{
	if (button == BUTTON_FRONTEND_MENU_BACK)
	{
		CFrontEnd::PlaySound(FES_BACK);
		mActive = FALSE;
		GAME.GetPauseMenu()->Activate(FALSE);
		RelinquishControl();
	}
}
