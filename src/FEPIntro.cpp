// FEPIntro.cpp: the title page (reconstructed from the retail PC executable; not in Stuart's release).

#include	"Common.h"

#include	"FEPIntro.h"
#include	"Frontend.h"
#include	"cliparams.h"
#include	"MemoryCard.h"
#include	"reconnectinterface.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"FrontEndText.h"
#include	<math.h>



int		gIntroUnknown = 0;							// 0089bb68: when set, the page never times out (and draws a text panel)
float	gIntroTextScroll = 0.0f;						// 0089bb6c: the panel's scroll (nothing in the executable writes it)
char	gIntroText[0x4000];						// 00897b28: the panel's text, lines split at '\n' (likewise)


//*********************************************************************************
CFEPIntro::CFEPIntro()	// FUNCTION: 0x0051b600
{
	mUnknown10 = FALSE;
}

//*********************************************************************************
#pragma auto_inline(off)
void	CFEPIntro::Reset()	// FUNCTION: 0x0051b610
{
	mUnknown0c = FALSE;
	FEMESSBOX.Kill();
	mUnknown10 = TRUE;

	if (!PLAYABLE_DEMO)
	{
		FEMESSBOX.Kill();
		mUnknown0c = FALSE;
	}
}
#pragma auto_inline(on)

//*********************************************************************************
BOOL	CFEPIntro::Init()	// FUNCTION: 0x0051b640
{
	mUnknown0c = FALSE;
	mUnknown14 = 1;
	Reset();

	return TRUE;
}

//*********************************************************************************
void	CFEPIntro::Shutdown()
{
}

//*********************************************************************************
void	CFEPIntro::ButtonPressed(SINT button, float val)	// FUNCTION: 0x0051b660
{
	if ((button == BUTTON_FRONTEND_MENU_SELECT) && (!mUnknown0c))
	{
		CLIPARAMS.mAttractMode = FALSE;

		if (GAME.mSupervisorMode)
		{
			if (PLAYABLE_DEMO)
				FRONTEND.SetPage(FEP_DEMOMAIN, 50);
			else
				FRONTEND.SetPage(FEP_MAIN, 50);
		}
		else
			FRONTEND.SetPage(FEP_E3_LEVEL_SELECT, 1);

		FRONTEND.PlaySound(FES_SELECT);
	}
}

//*********************************************************************************
void	CFEPIntro::Process(EFEPState state)	// FUNCTION: 0x0051b6b0
{
	if (state == FEPS_ACTIVE)
	{
		if (MEMORYCARD.Update())
			Reset();

		if ((CLIPARAMS.mAutoConfigTest) && (PLATFORM.GetSysTimeFloat() - mStartTime > 3.f))
			ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);

		if ((!mUnknown0c) && (!gIntroUnknown))
		{
			if (PLATFORM.GetSysTimeFloat() - mStartTime > 30.f)
				FRONTEND.SetQuit(-3);
		}
		else
		{
			mStartTime = PLATFORM.GetSysTimeFloat();
			mPressStart = 0.f;
		}

		if ((mPressStart == 0.f) && (PLATFORM.GetSysTimeFloat() - mStartTime > 1.f))
			mPressStart = 0.00001f;
		if (mPressStart != 0.f)
			mPressStart += gFrontEndFrameStep * 2.f;

		if (FEMESSBOX.BeingDisplayed())
		{
			switch (FEMESSBOX.GetQuestion())
			{
			case 0:
				switch (FEMESSBOX.GetFinalChoice())
				{
				case FEMB_YES:
					CLIPARAMS.mStressTest = mUnknown14;
					FRONTEND.SetQuit(-3);
					break;
				}
				// fall through
			case 10:
				switch (FEMESSBOX.GetFinalChoice())
				{
				case FEMB_YES:
					mUnknown0c = FALSE;
					mUnknown10 = TRUE;
					FEMESSBOX.Kill();
					break;
				}
				break;
			}
		}

		FRONTEND.ProcessMouseClickInRect(0.f, 0.f, float(PLATFORM.GetWindowWidth()), float(PLATFORM.GetWindowWidth()), BUTTON_FRONTEND_MENU_SELECT);
	}
}

//*********************************************************************************
// FUNCTION: 0x0051b840
void	CFEPIntro::Render(float transition, EFrontEndPage dest)
{
	if (transition == 1.f)
	{
		// the splash eases in from its large size
		float	t = mPressStart;
		if (t > 1.f)
			t = 1.f;
		float	s = (((float)cos(t * PI) + 1.f) * 0.5f) * 0.75f + 0.46875f;
		CSPRITERENDERER::DrawColouredSprite(558.f - s * 238.f - 126.4375f, 18.f - s * -222.f - -117.9375f, FO(960), FED.GetTexture(FET_FE_SPLASH1), 0xffffffff, s, s, SA_CENTRE);

		// "press start", flashing, with a black outline
		if ((mPressStart > 4.f) && (fmodf(mPressStart, 4.f) < 2.f))
		{
			WCHAR	*text = Localization_GetStringById(0x77);
			SIZE	size;
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &size);
			PLATFORM.Font(FONT_NORMAL)->DrawTextScaled(320.f - (float(size.cx) * 0.5f) - 1.f, 401.f, FO(100), 1.f, 1.f, 0xff000000, text, 0, 0);
			PLATFORM.Font(FONT_NORMAL)->DrawTextScaled(320.f - (float(size.cx) * 0.5f) + 1.f, 401.f, FO(100), 1.f, 1.f, 0xff000000, text, 0, 0);
			PLATFORM.Font(FONT_NORMAL)->DrawTextScaled(320.f - (float(size.cx) * 0.5f) - 1.f, 399.f, FO(100), 1.f, 1.f, 0xff000000, text, 0, 0);
			PLATFORM.Font(FONT_NORMAL)->DrawTextScaled(320.f - (float(size.cx) * 0.5f) + 1.f, 399.f, FO(100), 1.f, 1.f, 0xff000000, text, 0, 0);
			PLATFORM.Font(FONT_NORMAL)->DrawTextScaled(320.f - (float(size.cx) * 0.5f), 400.f, FO(100), 1.f, 1.f, 0xffffffff, text, 0, 0);
		}

		RENDERINFO.SetFogEnabled(false);

		// the Lost Toys logo slides in
		float	r = RangeTransition(mPressStart, 4.f, 5.f);
		float	off = (1.f - r) * (1.f - r) * 400.f;
		CSPRITERENDERER::DrawColouredSprite(124.f - off, -6.f, 0.101f, FED.GetTexture(FET_LOST_TOYS), 0x3f000000, 1.f, 1.f);
		CSPRITERENDERER::DrawColouredSprite(120.f - off, -10.f, FO(100), FED.GetTexture(FET_LOST_TOYS), 0xffffffff, 1.f, 1.f);

		// the title zooms down to its size, with a flash when it lands
		CTEXTURE	*title = FED.GetTexture(FET_BEA_TITLE2);
		float	p = (PLATFORM.GetSysTimeFloat() - mStartTime) * 1.2f;
		if (p > 2.f)
		{
			float	flash = -1.f;
			float	zoom = ((0.5f - p) + 2.f) * 10.f;
			if (zoom < 1.f)
			{
				flash = 1.f - zoom;
				zoom = 1.f;
			}

			SINT	a = SINT((1.f / zoom) * 255.f);
			DWORD	black = 0x9f000000;
			DWORD	shadow = BlendAlpha2(black, a);
			DWORD	white = 0xffffffff;
			float	sc = zoom * 0.5f;
			CSPRITERENDERER::DrawColouredSprite(252.f, 292.f, 0.05f, title, shadow, sc, sc, SA_CENTRE);
			CSPRITERENDERER::DrawColouredSprite(248.f, 292.f, 0.05f, title, shadow, sc, sc, SA_CENTRE);
			CSPRITERENDERER::DrawColouredSprite(252.f, 288.f, 0.05f, title, shadow, sc, sc, SA_CENTRE);
			CSPRITERENDERER::DrawColouredSprite(248.f, 288.f, 0.05f, title, shadow, sc, sc, SA_CENTRE);
			CSPRITERENDERER::DrawColouredSprite(250.f, 290.f, 0.04f, title, BlendAlpha2(white, a), sc, sc, SA_CENTRE);

			if ((flash < 3.f) && (flash > 0.f))
			{
				float	f2 = flash * 2.f;
				SINT	fa = SINT((1.f / (f2 * 2.f)) * 128.f);
				sc = f2 * 0.5f;		// the title's scale variable again: one frame slot for both (retail esp+0x8 class)
				CSPRITERENDERER::DrawColouredSprite(250.f, 290.f, 0.02f, title, BlendAlpha2(white, fa), sc, sc, SA_CENTRE);
			}
		}

		// the text panel
		if (gIntroUnknown)
		{
			CSPRITERENDERER::DrawColouredSprite(0.f, 0.f, 0.f, FED.GetTexture(FET_FE_SPLASH1), 0xff000000, 10.f, 10.f);

			float	y = 64.f - gIntroTextScroll;
			char	*c = gIntroText;
			if (*c)
			{
				do
				{
					WCHAR	line[0x1000];
					SINT	n = 0;

					if (*c == '\n')
						c++;
					for (; (*c != '\n') && (*c); c++)
						line[n++] = WCHAR(*c);
					line[n] = 0;

					PLATFORM.Font(FONT_SMALL)->DrawText(64.f, y, 0xffffffff, line, 0, 0, 1.f);
					y += 16.f;
				} while (*c);
			}
		}
	}
}

//*********************************************************************************
void	CFEPIntro::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x0051be70
{
	mStartTime = PLATFORM.GetSysTimeFloat();
	FRONTEND.mPlayer0ControllerPort = -1;
	RECONNECT_INTERFACE[0].Reset(0);
	RECONNECT_INTERFACE[1].Reset(0);
	mPressStart = 0.f;
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x0051b510 init:ZERO_FVECTOR
// FUNCTION: 0x0051b530 init:ID_FMATRIX
