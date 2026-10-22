// FEPBriefing.cpp: the mission briefing page (reconstructed from the retail PC executable; not in Stuart's release).

#include	"Common.h"

#include	"FEPBriefing.h"
#include	"Frontend.h"
#include	"cliparams.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	<math.h>

// the briefing video's size relative to the full-size one (a smaller video is shown larger)
float	gBriefingVideoScaleX = 1.f;				// 00628fe8
float	gBriefingVideoScaleY = 1.f;				// 00628fec

//*********************************************************************************
BOOL	CFEPBriefing::Init()	// FUNCTION: 0x00451b70
{
	mTimer = 0;
	mVideoOpen = FALSE;

	return TRUE;
}

//*********************************************************************************
void	CFEPBriefing::Shutdown()
{
}

//*********************************************************************************
void	CFEPBriefing::Process(EFEPState state)	// FUNCTION: 0x00451b80
{
	mTimer += 0.01f;

	if (state == FEPS_ACTIVE)
		mTextFade += 0.05f;
	else if (state == FEPS_TRANSITIONING_FROM)
		mTextFade -= 0.15f;

	if (mTextFade < 0.f)
		mTextFade = 0.f;
	if (mTextFade > 1.f)
		mTextFade = 1.f;

	// the autoconfig test presses on once the video has started
	if ((state == FEPS_ACTIVE) && (CLIPARAMS.mAutoConfigTest) && (FEV.mUnknown00[11] == 0xff) && (mTextFade >= 1.f))
		ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);
}

//*********************************************************************************
void	CFEPBriefing::ButtonPressed(SINT button, float val)	// FUNCTION: 0x00451c20
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_SELECT:
		FRONTEND.PlaySound(FES_SELECT);
		FRONTEND.SetPage(FEP_BE_CONFIG, 25);
		break;

	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.PlaySound(FES_BACK);
		if (GAME.mSupervisorMode)
			FRONTEND.SetPage(FEP_LEVEL_SELECT, 30);
		else
			FRONTEND.SetPage(FEP_E3_LEVEL_SELECT, 30);
		FRONTEND.GetCommonPage()->StartVideo(0);
		mVideoOpen = FALSE;
		break;
	}
}

//*********************************************************************************
void	CFEPBriefing::RenderPreCommon(float transition, EFrontEndPage dest)	// FUNCTION: 0x00451c90
{
	SINT	alpha;
	float	scale;

	if ((dest == FEP_LEVEL_SELECT) || (dest == FEP_E3_LEVEL_SELECT))
	{
		alpha = SINT(RangeTransition(transition, 0.25f, 1.f) * 255.f);
		scale = transition * 0.25f + 1.f;
		transition = -200.f;
	}
	else
	{
		alpha = 0xff;
		scale = 1.25f;
		transition = cosf(transition * PI) * 100.f - 100.f;
	}

	FRONTEND.GetCommonPage()->RenderSelectionMarker(transition, 0.f, scale, alpha);
}

//*********************************************************************************
// The video scale values become pixel dimensions before rendering, and the video/text stages share their ordinate.
// Layout classifies the current selection directly; text-resource lookups own a later snapshot of that selection.
// FUNCTION: 0x00451d50
void	CFEPBriefing::Render(float transition, EFrontEndPage dest)
{
	// STAND-IN (layout; review F 2026-10-15): the condition three times; one block loses 00451d50
	if ((transition == 1.f) || (dest == FEP_BE_CONFIG))
		FRONTEND.GetCommonPage()->RenderSmallForsetiText(1.f);
	if ((transition == 1.f) || (dest == FEP_BE_CONFIG))
		CSPRITERENDERER::DrawColouredSprite(285.f, 225.f, FO(900), FED.GetTexture(FET_METAL_RING_TRANS_FROM_LEVSEL2), 0xffffffff, 1.6f, 1.6f, SA_CENTRE);
	if ((transition == 1.f) || (dest == FEP_BE_CONFIG))
		FRONTEND.GetCommonPage()->RenderSmallBEALogo(1.f);

	// the video's box slides across to the configuration page, or eases in from the right
	float	x, y;
	if (dest == FEP_BE_CONFIG)
	{
		y = 252.f;
		x = ((-cosf(transition * PI) - 1.f) * 0.5f) * 600.f + 480.f;
	}
	else
	{
		float	f = 1.f - RangeTransition(transition, 0.25f, 0.75f);
		x = 480.f - (1.f - (1.f - f * f)) * 700.f;
		f = 1.f - RangeTransition(transition, 0.25f, 1.f);
		y = (1.f - (1.f - f * f)) * 400.f + 252.f;
	}
	CSPRITERENDERER::DrawColouredSprite(x, y, FO(100), FED.GetTexture(FET_AVI_BOX), 0xffffffff, 1.f, 1.f, SA_CENTRE);

	FRONTEND.DrawLine(floorf(x) - 103.f, 0.f, floorf(x) - 103.f, float(PLATFORM.GetWindowHeight()), 0x3fffffff, 1.f, 0.1f, 1.f);
	FRONTEND.DrawLine(floorf(x) + 103.f, 0.f, floorf(x) + 103.f, float(PLATFORM.GetWindowHeight()), 0x3fffffff, 1.f, 0.1f, 1.f);

	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

	if ((transition == 1.f) && (mVideoOpen))
	{
		float	sx = gBriefingVideoScaleX;
		float	sy = gBriefingVideoScaleY;
		SINT	a = MakeAlpha(mTextFade);
		float	cx = float(PLATFORM.GetWindowWidth()) * (x - 103.f + 3.f) / 640.f;
		float	cy = float(PLATFORM.GetWindowHeight()) * (y - 77.f + 2.f) / 480.f;
		sx = float(PLATFORM.GetWindowWidth()) * sx / 640.f;
		sy = float(PLATFORM.GetWindowHeight()) * sy / 480.f;
		FEV.Render(cx, cy, 0.1f, sx, sy, BlendAlpha2(0xffffffff, a), FALSE);
	}

	// the level's name and briefing, revealed over time
	if ((transition > 0.75f) && (PLATFORM.GetSysTimeFloat() < mStartTime))
		mStartTime = PLATFORM.GetSysTimeFloat();

	float	reveal = (PLATFORM.GetSysTimeFloat() - mStartTime) * 0.5f;
	y = 164.f;
	if (((FRONTEND.mSelectedLevel == 611) || (FRONTEND.mSelectedLevel == 612)) && (TEXT.mLanguage == 2))
		y = 148.f;

	float	c = RangeTransition(transition, 0.75f, 1.f);
	SINT	alpha = SINT(c * 255.f);
	float	f = 1.f - c;
	float	off = f * f * 200.f;

	SINT	level = FRONTEND.mSelectedLevel;
	WCHAR	*text = FET.GetLevelName(level);
	SIZE	size;
	DWORD	white = 0xffffffff;
	PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(180.f, 118.f, 0.01f, 0.7f, 1.f, BlendAlpha2(white, alpha), text, reveal, 0, 0);

	SWordWrapFill	fill;
	for (SINT n = 0; n < 9; n++)
	{
		text = FET.GetBriefingText(level, n);
		SINT	lines = PLATFORM.Font(FONT_SMALL)->WordWrap(&fill, text, 300.f);
		if (lines > 0)
		{
			float	lx = 80.f - off;
			for (SINT i = 0; i < lines; i++)
			{
				PLATFORM.Font(FONT_SMALL)->GetTextExtent(fill.c[i], &size);
				float	lineReveal = reveal;	// STAND-IN (order; review F 2026-10-15): passing reveal itself loses 00451d50
				PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(lx, y, 0.01f, 1.f, 1.f, BlendAlpha2(FET.GetBriefingTextColour(level, n), alpha), fill.c[i], lineReveal * 2.f, 0, 0);
				reveal -= 0.025f;
				y += 16.f;
			}
		}
		y += 10.f;
	}

	RENDERINFO.SetFogEnabled(false);
	LT.SetModulate(0);

	FRONTEND.GetCommonPage()->RenderHelpText(4, transition);
	FRONTEND.GetCommonPage()->RenderHelpText(1, transition);
	FRONTEND.DrawTitleBar(FET.GetText(FEX_MISSION_BRIEFING), transition, dest);
}

//*********************************************************************************
void	CFEPBriefing::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x00452430
{
	mStartTime = PLATFORM.GetSysTimeFloat() + 10000.f;
	mTextFade = 0.f;
}

//*********************************************************************************
void	CFEPBriefing::ActiveNotification(EFrontEndPage from)	// FUNCTION: 0x00452460
{
	char	name[200];
	int		level;

	// the briefing for a level's variants is the first variant's
	switch (FRONTEND.mSelectedLevel)
	{
	case 100:	level = 100;	break;
	case 110:	level = 110;	break;
	case 200:	level = 200;	break;
	case 211:	level = 211;	break;
	case 212:	level = 211;	break;
	case 221:	level = 221;	break;
	case 222:	level = 221;	break;
	case 231:	level = 231;	break;
	case 232:	level = 231;	break;
	case 300:	level = 300;	break;
	case 311:	level = 311;	break;
	case 312:	level = 311;	break;
	case 321:	level = 321;	break;
	case 322:	level = 321;	break;
	case 331:	level = 331;	break;
	case 332:	level = 331;	break;
	case 400:	level = 400;	break;
	case 411:	level = 411;	break;
	case 412:	level = 411;	break;
	case 421:	level = 421;	break;
	case 422:	level = 421;	break;
	case 431:	level = 431;	break;
	case 432:	level = 431;	break;
	case 500:	level = 500;	break;
	case 511:	level = 511;	break;
	case 512:	level = 512;	break;
	case 521:	level = 521;	break;
	case 522:	level = 521;	break;
	case 523:	level = 523;	break;
	case 524:	level = 523;	break;
	case 600:	level = 600;	break;
	case 611:	level = 611;	break;
	case 612:	level = 611;	break;
	case 621:	level = 621;	break;
	case 622:	level = 621;	break;
	case 700:	level = 700;	break;
	case 710:	level = 710;	break;
	case 720:	level = 720;	break;
	case 731:	level = 731;	break;
	case 732:	level = 731;	break;
	case 741:	level = 741;	break;
	case 742:	level = 741;	break;
	case 800:	level = 800;	break;
	}

	// the full-size video if there is one, else the smaller one (drawn larger), else the quarter-size one
	sprintf(name, "data\\video\\Briefings\\PC_%03d_exact.vid", level);
	gBriefingVideoScaleX = 1.f;
	gBriefingVideoScaleY = 1.f;
	FILE	*f = fopen(name, "rb");
	if (f)
		fclose(f);
	else
	{
		sprintf(name, "data\\video\\Briefings\\PC_%03d.vid", level);
		gBriefingVideoScaleX = 0.78515625f;
		gBriefingVideoScaleY = 0.58203125f;
		f = fopen(name, "rb");
		if (f)
			fclose(f);
		else
			sprintf(name, "data\\video\\Briefings\\PC_%03d_25.vid", level);
	}

	FEV.Open(name, SINT(201.f / gBriefingVideoScaleX), SINT(149.f / gBriefingVideoScaleY), 0, TRUE, 0);
	mVideoOpen = TRUE;
}

//*********************************************************************************
void	CFEPBriefing::DeActiveNotification()
{
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x00451a80 init:ZERO_FVECTOR
// FUNCTION: 0x00451aa0 init:ID_FMATRIX
