// FEPCredits.cpp: the credits page (reconstructed from the retail PC executable; not in Stuart's release).

#include	"Common.h"

#include	"FEPCredits.h"
#include	"Frontend.h"
#include	"music.h"

//*********************************************************************************
BOOL	CFEPCredits::Init()
{
	return TRUE;
}

//*********************************************************************************
void	CFEPCredits::Shutdown()
{
}

//*********************************************************************************
void	CFEPCredits::ButtonPressed(SINT button, float val)	// FUNCTION: 0x0051a7f0
{
	if (button == BUTTON_FRONTEND_MENU_BACK)
	{
		FRONTEND.PlaySound(FES_BACK);
		FRONTEND.SetPage(FEP_OPTIONS, 30);
		MUSIC.PlaySelection(MUS_FRONTEND, TRUE);
	}
}

//*********************************************************************************
void	CFEPCredits::Process(EFEPState state)	// FUNCTION: 0x0051a820
{
	if (state == FEPS_ACTIVE)
	{
		if (mFinished)
		{
			FRONTEND.SetPage(FEP_OPTIONS, 30);
			MUSIC.PlaySelection(MUS_FRONTEND, TRUE);
			mFinished = FALSE;
		}

		FRONTEND.ProcessMouseClickInRect(0.f, 0.f, 640.f, 480.f, BUTTON_FRONTEND_MENU_BACK);
	}
}

//*********************************************************************************
void	CFEPCredits::RenderPreCommon(float transition, EFrontEndPage dest)	// FUNCTION: 0x0051a880
{
	if (transition == 1.f)
		FRONTEND.DrawStandardVideoBackground(1.f, 0x3fffffff, dest);
}

//*********************************************************************************
void	CFEPCredits::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x0051a8b0
{
	int alpha = int(RangeTransition(transition, 0.75f, 1.f) * 255.f);
	if (alpha < 0)
		alpha = 0;
	else if (alpha > 255)
		alpha = 255;

	if (!CREDITS.RenderCredits(PLATFORM.GetSysTimeFloat() - mStartTime, alpha))
		mFinished = TRUE;

	if (transition == 1.f)
		FRONTEND.GetCommonPage()->RenderSmallBEALogo(1.f);
}

//*********************************************************************************
void	CFEPCredits::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x0051a970
{
	mStartTime = PLATFORM.GetSysTimeFloat() + 2.f;
	MUSIC.PlaySelection(MUS_CREDITS, TRUE);
	mFinished = FALSE;
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x0051a700 init:ZERO_FVECTOR
// FUNCTION: 0x0051a720 init:ID_FMATRIX
