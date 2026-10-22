// FEPScreenPos.cpp: the PC's screen-position page (reconstructed from the retail PC executable; not in Stuart's
// release). The position is kept in the career (CCareer::GetScreenPosX/Y).

#include	"Common.h"

#include	"FEPScreenPos.h"
#include	"Frontend.h"
#include	"career.h"

//*********************************************************************************
void	CFEPScreenPos::Shutdown()	// FUNCTION: 0x0040c640
{
}

BOOL	CFEPScreenPos::Init()	// FUNCTION: 0x0051f9f0
{
	mX = mY = 0;

	return TRUE;
}

//*********************************************************************************
void	CFEPScreenPos::ButtonPressed(SINT button, float val)	// FUNCTION: 0x0051fa00
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_UP:
		mY --;
		if (mY < -50)
			mY = -50;
		CAREER.SetScreenPosY(mY);
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_DOWN:
		mY ++;
		if (mY > 64)
			mY = 64;
		CAREER.SetScreenPosY(mY);
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_LEFT:
		mX -= 4;
		if (mX < -63)
			mX = -63;
		CAREER.SetScreenPosX(mX);
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_RIGHT:
		mX += 4;
		if (mX > 64)
			mX = 64;
		CAREER.SetScreenPosX(mX);
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		FRONTEND.SetPage(FEP_OPTIONS, 30);
		FRONTEND.PlaySound(FES_SELECT);
		break;

	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.PlaySound(FES_BACK);
		FRONTEND.SetPage(FEP_OPTIONS, 30);
		mOnEntry.Write();
		break;
	};
}

//*********************************************************************************
void	CFEPScreenPos::Process(EFEPState state)	// FUNCTION: 0x00452b60
{
}

//******************************************************************************************
void	CFEPScreenPos::RenderPreCommon(float transition, EFrontEndPage dest)	// FUNCTION: 0x0051fb60
{
	if (transition == 1.f)
		FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}

//*********************************************************************************
void	CFEPScreenPos::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x0051fb90
{
	if (transition == 1.f)
		FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);

	float tr = RangeTransition(transition, 0.8f, 1.f);

	if (transition == 1.f)
	{
		WCHAR	*text;
		SIZE	s;

		text = ToWCHAR("Adjust Screen Position until display is");
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &s);
		PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(320.f - float(s.cx) * 0.5f, 275.f, 0.1f, 1.f, 1.f, 0xffffffff, text, 1000.f, 0, 8);

		text = ToWCHAR("correctly centered on your television.");
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(text, &s);
		PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(320.f - float(s.cx) * 0.5f, 305.f, 0.1f, 1.f, 1.f, 0xffffffff, text, 1000.f, 0, 8);
	}

	FRONTEND.GetCommonPage()->RenderHelpText(5, transition);
	FRONTEND.GetCommonPage()->RenderHelpText(6, transition);

	FRONTEND.DrawTitleBar(ToWCHAR("Adjust Screen Position"), transition, dest);

	if (transition == 1.f)
		FRONTEND.GetCommonPage()->RenderSmallBEALogo(tr);
}

//*********************************************************************************
void	CFEPScreenPos::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x0051fd50
{
	mStartTime = PLATFORM.GetSysTimeFloat() + 2.f;

	mOnEntry.Read();
	mX = mOnEntry.mX;
	mY = mOnEntry.mY;
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x0051f900 init:ZERO_FVECTOR
// FUNCTION: 0x0051f920 init:ID_FMATRIX
