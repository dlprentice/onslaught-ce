// FEPCommon.cpp: the page drawn under every other page (reconstructed from the retail PC executable; not in
// Stuart's release): the background video, the corner logo, the scrolling Forseti text, the selection marker and
// the help arrow.

#include	"Common.h"

#include	"FEPCommon.h"
#include	"Frontend.h"
#include	"ltshell.h"
#include	"RenderInfo.h"
#include	"SpriteRenderer.h"
#include	"reconnectinterface.h"
#include	"tweak.h"

//*********************************************************************************
BOOL	CFEPCommon::Init()	// FUNCTION: 0x00452b00
{
	mBuffer = NULL;
	FEV.Open("data\\video\\FEBack128.vid", 128, 128, 0, TRUE, 0);

	return TRUE;
}

//*********************************************************************************
void	CFEPCommon::Shutdown()	// FUNCTION: 0x00452b30
{
	FEV.CloseVideo();
	SAFE_DELETE(mBuffer);
}

//*********************************************************************************
void	CFEPCommon::Process(EFEPState state)	// FUNCTION: 0x00452b60
{
}

//*********************************************************************************
void	CFEPCommon::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x00452b70
{
	DWORD	col = 0x3fffffff;

	if ((FRONTEND.GetCurrentPage() == FEP_INTRO) || (FRONTEND.GetCurrentPage() == FEP_LOADGAME))
		return;

	if ((FRONTEND.GetTransitionTo() == FEP_LOADGAME) || (FRONTEND.GetTransitionTo() == FEP_CREDITS))
		col = (SINT(RangeTransition(transition, 1.f, 0.f) * 63.f) << 24) | 0xffffff;
	else if ((FRONTEND.GetTransitionFrom() == FEP_LOADGAME) || (FRONTEND.GetTransitionFrom() == FEP_CREDITS))
		col = (SINT(Clamped(transition, 0.f, 1.f) * 63.f) << 24) | 0xffffff;

	FRONTEND.DrawLine(123.f, 0.f, 123.f, float(PLATFORM.GetWindowHeight()), col, 1.f, 0.95f, 1.f);
	FRONTEND.DrawLine(0.f, 180.f, float(PLATFORM.GetWindowWidth()), 180.f, col, 1.f, 0.95f, 1.f);
}

//*********************************************************************************
void	CFEPCommon::RenderVideoBackground(float scale, DWORD colour, float cx, float cy)	// FUNCTION: 0x00452ce0
{
	if (cx == -9999999.f)
	{
		cx = float(PLATFORM.GetWindowWidth()) * 0.5f;
		cy = float(PLATFORM.GetWindowHeight()) * 0.5f;
	}

	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

	FEV.Render(cx, cy, 0.96f, float(PLATFORM.GetWindowWidth()) * scale * 0.0087890625f, float(PLATFORM.GetWindowHeight()) * scale * 0.0079101566f, colour, TRUE);
}

//*********************************************************************************
void	CFEPCommon::RenderAfter(float transition, EFrontEndPage dest)	// FUNCTION: 0x00452da0
{
}

//*********************************************************************************
void	CFEPCommon::StartVideo(int a)	// FUNCTION: 0x00452db0
{
	FEV.Open("data\\video\\FEBack128.vid", 128, 128, 0, TRUE, 0);
}

//*********************************************************************************
void	CFEPCommon::StopVideo()	// FUNCTION: 0x00452de0
{
	FEV.CloseVideo();
}

//*********************************************************************************
void	CFEPCommon::RenderSmallBEALogo(float transition)	// FUNCTION: 0x00452df0
{
	if (transition > 0.f)
	{
		SINT alpha = SINT(transition * 255.f);

		CSPRITERENDERER::DrawColouredSprite(65.f + FRONTEND.GetShadowOffsetX(), 35.f + FRONTEND.GetShadowOffsetY(), FO(45), FED.GetTexture(FET_SMALL_BEA_LOGO), BlendAlpha2(0x3f000000, alpha), 0.84f, 0.84f);

		RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
		RENDERINFO.Apply();

		CSPRITERENDERER::DrawColouredSprite(60.f, 25.f, FO(999), FED.GetTexture(FET_SMALL_BEA_LOGO), BlendAlpha2(0xffafcfff, alpha), 0.8f, 0.8f);

		RENDERINFO.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		RENDERINFO.Apply();

		// the shine across it
		float x = 256.f - fmodf(FRONTEND.GetCounter() * 0.6f, 512.f) - 80.f;

		FRONTEND.EnableAdditiveAlpha();

		DWORD c = DWORD(alpha * 0x7f) >> 8;

		CSPRITERENDERER::DrawColouredSprite(x, 100.f, FO(998), FED.GetTexture(FET_LOGO_SHINE), c * 0x010101 + 0xff000000, 1.f, 2.f, SA_CENTRE);
		CSPRITERENDERER::DrawColouredSprite(x + 512.f, 100.f, FO(998), FED.GetTexture(FET_LOGO_SHINE), c * 0x010101 + 0xff000000, 1.f, 2.f, SA_CENTRE);

		FRONTEND.EnableModulateAlpha();
	}
}

//*********************************************************************************
void	CFEPCommon::RenderSmallForsetiText(float transition)	// FUNCTION: 0x00452fd0
{
	float y = 90.f - float(fmod(FRONTEND.GetCounter() * 0.3f, 180.f));
	SINT alpha = SINT(transition * 255.f);

	for (int c0 = 0; c0 < 4; c0 ++)
	{
		CSPRITERENDERER::DrawColouredSprite(86.f, y, FO(900), FED.GetTexture(FET_FORSETI_TEXT), BlendAlpha2(0x3fffffff, alpha), 0.5f, 0.5f, SA_CENTRE);
		y += 180.f;
	}
}

//*********************************************************************************
CTweakSLONG	NOSCROLL_BACKGROUND("NOSCROLL_BACKGROUND", 0);	// 006777b8
// FUNCTION: 0x00453070 init:NOSCROLL_BACKGROUND
// FUNCTION: 0x00453090 exit:NOSCROLL_BACKGROUND
// FUNCTION: 0x004530a0 ??1CTweakSLONG@@QAE@XZ

//*********************************************************************************
void	CFEPCommon::RenderSelectionMarker(float x, float y, float scale, SINT alpha)	// FUNCTION: 0x004530b0
{
	float sy = 240.f - (y * scale);
	float sx;

	if (NOSCROLL_BACKGROUND.mValue)
		sx = 300.f;
	else
		sx = 320.f - (x * scale);

	CSPRITERENDERER::DrawColouredSprite(sx, sy, FO(955), FED.GetTexture(FET_SELECTION_MARKER), BlendAlpha2(0xffbfbfff, alpha), scale, scale, SA_CENTRE);
}

//*********************************************************************************
void	CFEPCommon::RenderHelpText(SINT help, float transition)	// FUNCTION: 0x00453140
{
	if (RECONNECT_INTERFACE[0].GetState() != CReconnectInterface::OK)
		return;
	if ((RECONNECT_INTERFACE[1].GetState() != CReconnectInterface::OK) && (FRONTEND.GetCurrentPage() == FEP_MULTIPLAYER_START))
		return;

	BOOL	back = FALSE;
	WCHAR	*text = ToWCHAR("Unknown Help String");

	switch (help)
	{
	case 0:
		text = Localization_GetStringById(0x2b);
		break;
	case 1:
		text = Localization_GetStringById(0x2c);
		back = TRUE;
		break;
	case 2:
		text = Localization_GetStringById(0x2d);
		back = TRUE;
		break;
	case 4:
		text = Localization_GetStringById(0x2e);
		break;
	case 5:
		text = Localization_GetStringById(0x2f);
		break;
	case 3:
	case 6:
		text = Localization_GetStringById(0x30);
	default:
		if ((help > 0) && ((help <= 3) || (help == 6)))
			back = TRUE;
		break;
	}

	if (FEMESSBOX.IsInteractive())
		return;

	float	ofs = 1.f - transition;
	ofs = ofs * ofs * 3000.f;

	SIZE	s;
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &s);

	float	x;
	if (back)
		x = 6.f - ofs;
	else
		x = (634.f - s.cx) + ofs;

	FRONTEND.GetCursorStateInRect(x, 455.f, x + s.cx, s.cy + 455.f);

	float	yaw;
	SINT	button;
	if (back)
	{
		x += 16.f;
		yaw = PI;
		button = BUTTON_FRONTEND_MENU_BACK;
	}
	else
	{
		x = ofs + 618.f;
		yaw = 0.f;
		button = BUTTON_FRONTEND_MENU_SELECT;
	}

	if ((SINT(FRONTEND.GetCounter()) % 64) < 50)
		CSPRITERENDERER::DrawColouredSprite(x, 455.f, FO(100), FED.GetTexture(FET_HELP_ARROW), 0xffffffff, 1.f, 1.f, SA_CENTRE, SPT_NONE, 1.f, yaw);

	FRONTEND.ProcessMouseClickInRect(x - 40.f, 415.f, x + 40.f, 495.f, button);
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x00452a10 init:ZERO_FVECTOR
// FUNCTION: 0x00452a30 init:ID_FMATRIX
