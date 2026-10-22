// FEMessBox.cpp: the front end's message box (reconstructed from the retail PC executable; not in Stuart's release).

#include	"Common.h"

#include	"FEMessBox.h"
#include	"Frontend.h"
#include	"RenderInfo.h"
#include	"mouse.h"

CFEMessBox	FEMESSBOX;						// 00675688

//*********************************************************************************
void	CFEMessBox::Init()	// FUNCTION: 0x0044d320
{
	mCursor = -1;
	mFinalChoice = -1;
	mUnknown1f94 = 0;
	mBackColour = 0xaf000000;
	mTextColour = 0xffffffff;
	mHighlightColour = 0xaf40ff40;
	mGreyColour = 0xff7f7f7f;
	mUnknown0000 = TRUE;
	mTime = 0;
	mTimeout = 0;
	mFadeCount = 0;
	mFadeTime = 0;
	mBeingDisplayed = FALSE;
	mOnDismissPage = FEP_NONE;
}

//*********************************************************************************
void	CFEMessBox::Create(float x, float y, float width, float z, WCHAR *text, CDXBitmapFont *font, UINT timeout, UINT fadetime, BOOL a, EFEMBOption option, UINT question)	// FUNCTION: 0x0044d390
{
	if (mBeingDisplayed)
		return;

	mX = x;
	mCursor = -1;
	mFinalChoice = -1;
	mY = y;
	mUnknown1f90 = a;
	mTimeout = float(timeout);
	mWidth = width;
	mFadeTime = float(fadetime);
	mFont = font;
	mOption = option;
	mUnknown1f94 = 0;
	mZ = z;
	mQuestion = question;
	mTime = 0;
	mFadeCount = 0;
	mFadeState = 0;

	if (fadetime == 0)
	{
		mFadeState = 1;
		mAlpha = 0xff;
	}

	mNumLines = font->WordWrap(&mLines, text, width - 16.f);
	mLineHeight = font->GetHeight();
	mHeight = float(mLineHeight) * mNumLines + 16.f;

	if (mUnknown1f90)
	{
		mHeight += 32.f;
		mOption = FEMB_OPTION_NONE;
	}
	else
	{
		switch (option)
		{
		case FEMB_OPTION_OK:
		case FEMB_OPTION_A_CONTINUE:
			mHeight += float(PLATFORM.Font(FONT_NORMAL)->GetHeight());
			break;

		case FEMB_OPTION_YESNO:
			mHeight += float(PLATFORM.Font(FONT_NORMAL)->GetHeight() * 2);
			if (mOption == FEMB_OPTION_YESNO)
				mCursor = 0;
			break;
		}
	}

	mBeingDisplayed = TRUE;
}

//*********************************************************************************
void	CFEMessBox::Process()	// FUNCTION: 0x0044d560
{
	if (mBeingDisplayed)
	{
		switch (mFadeState)
		{
		case 0:
			mFadeCount += gFrontEndFrameStep * 50.f;
			if (mFadeCount >= mFadeTime)
				mFadeState = 1;
			mAlpha = MakeAlpha(mFadeCount / mFadeTime);
			break;

		case 1:
			if (mUnknown1f90)
			{
				if (mUnknown1f94 >= 1.f)
				{
					mFadeState = 2;
					mAlpha = 0xff;
					break;
				}
			}
			else if (mOption == FEMB_OPTION_NONE)
			{
				mTime += gFrontEndFrameStep * 50.f;
				if (mTime >= mTimeout)
				{
					mFadeCount = 0;
					mFadeState = 2;
					mOnDismissPage = FEP_NONE;
				}
			}
			mAlpha = 0xff;
			break;

		case 2:
			mFadeCount += 1.f;
			if (mFadeCount >= mFadeTime)
			{
				mFadeState = 0;
				mBeingDisplayed = FALSE;
				break;
			}
			mAlpha = MakeAlpha(1.f - (mFadeCount / mFadeTime));
			break;
		}
	}
}

//*********************************************************************************
// A colour's alpha byte scaled by alpha / 256, rounded down, in place (ScaleAlpha's first part; Render ors the colour
// bits in itself).
static inline DWORD ScaledAlphaBits(DWORD col, SINT alpha)
{
	return (((col >> 24) * alpha) & 0xffffff00) * 0x10000;
}

//*********************************************************************************
// The box: its panel and frame, the wrapped lines, then the progress bar or the buttons. The two button cases share
// one bx and one y: retail keeps both cases' bx in one stack slot (0044da11, 0044dc9a) and both y in another (0044da32,
// 0044dcb6), which the line loop's c0 shares (0044d867). With a bx and a y per case the slot packer swaps c0's slot
// with the line pointer's and bottom's.
// STAND-IN: each colour is computed as `c = (mBeingDisplayed) ? ScaledAlphaBits(col, mAlpha) :
// ScaledAlphaBits(col, mAlpha); c |= (col & 0x00ffffff);`, two identical arms. Retail builds each scaled alpha in the
// colour variable's own register and ors the colour bits into it (back at 0044d710: `mov ebx,eax; shr ebx,0x18;
// imul ebx,ecx; ... and bl,0x0; shl ebx,0x10; or ebx,eax`; text edi, highlight ebp, grey eax with its colour in edx).
// With one definition (ScaleAlpha, a named local, in-place statements, a by-reference helper: all compile alike) C2's
// global pass forward-substitutes the scaled alpha into the or; the `* 0x10000` becomes a shift plus a copy that only an
// add folds, so the chain stays a one-block chooser temporary (edx) that is then copied into the variable (`mov ebx,edx`,
// and grey takes edx with its colour in eax): 18/70 shape/bytes with everything else here. The identical arms give the
// variable two front-end definitions, so it is not substituted and the chain runs in its register; mBeingDisplayed is
// known nonzero after the early return, so the test folds and emits no code.
// The brackets around the half extents are a code-neutral fit: one bracketed half extent per axis adds one id each
// before the line loop's CSE temporaries, which puts &mLineHeight's address temporary above &mNumLines' in the operand
// ranks, so retail's products load mLineHeight first (0044d893 `mov ecx,[esi+0x1f60]` with c0, and the six
// mLineHeight * mNumLines); unbracketed, 28/28. Which source Stuart wrote is unknown.
void	CFEMessBox::Render()	// FUNCTION: 0x0044d6f0
{
	if (!mBeingDisplayed)
		return;

	DWORD	back = (mBeingDisplayed) ? ScaledAlphaBits(mBackColour, mAlpha) : ScaledAlphaBits(mBackColour, mAlpha);	// STAND-IN
	back |= (mBackColour & 0x00ffffff);
	float	left = mX - (mWidth * 0.5f);
	DWORD	text = (mBeingDisplayed) ? ScaledAlphaBits(mTextColour, mAlpha) : ScaledAlphaBits(mTextColour, mAlpha);	// STAND-IN
	text |= (mTextColour & 0x00ffffff);
	float	right = mX + (mWidth * 0.5f);
	float	top = mY - (mHeight * 0.5f);
	DWORD	grey = (mBeingDisplayed) ? ScaledAlphaBits(mGreyColour, mAlpha) : ScaledAlphaBits(mGreyColour, mAlpha);	// STAND-IN
	grey |= (mGreyColour & 0x00ffffff);
	float	bottom = mY + (mHeight * 0.5f);
	DWORD	highlight = (mBeingDisplayed) ? ScaledAlphaBits(mHighlightColour, mAlpha) : ScaledAlphaBits(mHighlightColour, mAlpha);	// STAND-IN
	highlight |= (mHighlightColour & 0x00ffffff);

	RENDERINFO.SetFogEnabled(false);

	if (mUnknown0000)
	{
		FRONTEND.DrawPanel(left, top, right, bottom, mZ, back);

		if ((mOption == FEMB_OPTION_OK) || (mOption == FEMB_OPTION_A_CONTINUE) || (mOption == 4))
		{
			if (MouseClickedInRect(left, top, right, bottom))
				ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);
		}

		FRONTEND.DrawBox(left, top, right, bottom, grey, 2.f, mZ);
	}

	SINT	c0;
	SIZE	s;
	float	bx, y;

	for (c0 = 0; c0 < mNumLines; c0 ++)
	{
		mFont->GetTextExtent(mLines.c[c0], &s);
		mFont->DrawTextWithShadow(mX - (s.cx / 2), (mLineHeight * c0) + top + 8.f, text, mLines.c[c0]);
	}

	if (mUnknown1f90)
	{
		FRONTEND.DrawBarGraph(left + mWidth * 0.05f, top + mHeight - 24.f, left + mWidth * 0.95f, top + mHeight - 8.f, mUnknown1f94, 1.f, mZ, grey, back, highlight);
		return;
	}

	switch (mOption)
	{
	case FEMB_OPTION_OK:
	case FEMB_OPTION_A_CONTINUE:
		{
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(FET.GetText(FEX_OK), &s);
			bx = mX - (s.cx / 2) - 4.f;
			y = (mLineHeight * mNumLines) + top + 8.f;
			FRONTEND.DrawPanel(bx, y, bx + s.cx + 8.f, y + s.cy, mZ, highlight);
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow(mX - (s.cx / 2), (mLineHeight * mNumLines) + top + 8.f, text, FET.GetText(FEX_OK));
		}
		break;

	// Retail adds the row and text-block heights on x87 (0044da24, 0044da97, 0044dc27),
	// after the integer products. Preserve that conversion boundary.
	case FEMB_OPTION_YESNO:
		{
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(FET.GetText(mCursor ? FEX_YES : FEX_NO), &s);
			bx = mX - (s.cx / 2) - 4.f;
			y = ((1 - mCursor) * s.cy) + float(mLineHeight * mNumLines) + top + 8.f;
			FRONTEND.DrawPanel(bx, y, bx + s.cx + 8.f, y + s.cy, mZ, highlight);

			for (c0 = 0; c0 < 2; c0 ++)
			{
				y = ((1 - c0) * s.cy) + float(mLineHeight * mNumLines) + top + 8.f;
				if (MouseOverRect(left, y, right, y + s.cy))
				{
					mCursor = c0;
					break;
				}
				if (MouseClickedInRect(left, y, right, y + s.cy))
				{
					FRONTEND.PlaySound(FES_SELECT);
					mCursor = c0;
					mFinalChoice = c0;
					mFadeCount = 0.f;
					mFadeState = 2;
					mOnDismissPage = FEP_NONE;
					break;
				}
			}

			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(FET.GetText(FEX_YES), &s);
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow(mX - (s.cx / 2), (mLineHeight * mNumLines) + top + 8.f, text, FET.GetText(FEX_YES));
			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(FET.GetText(FEX_NO), &s);
			PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow(mX - (s.cx / 2), float(mLineHeight * mNumLines) + s.cy + top + 8.f, text, FET.GetText(FEX_NO));
		}
		break;
	}
}

//*********************************************************************************
void	CFEMessBox::ButtonPressed(SINT button, float val)	// FUNCTION: 0x0044dd60
{
	if ((mFadeState == 1) && (mBeingDisplayed))
	{
		switch (mOption)
		{
		case FEMB_OPTION_A_CONTINUE:
		case 4:
			if (button == BUTTON_FRONTEND_MENU_SELECT)
			{
				FRONTEND.PlaySound(FES_SELECT);
				if (mOnDismissPage != FEP_NONE)
					FRONTEND.SetPage(mOnDismissPage, mOnDismissTime);
				mFinalChoice = 1;
				mFadeCount = 0;
				mFadeState = 2;
				mOnDismissPage = FEP_NONE;
				return;
			}
			break;

		case FEMB_OPTION_OK:
			if (button == BUTTON_FRONTEND_MENU_SELECT)
			{
				FRONTEND.PlaySound(FES_SELECT);
				if (mOnDismissPage != FEP_NONE)
					FRONTEND.SetPage(mOnDismissPage, mOnDismissTime);
				mFadeCount = 0;
				mFadeState = 2;
				mOnDismissPage = FEP_NONE;
			}
			break;

		case FEMB_OPTION_YESNO:
			switch (button)
			{
			case BUTTON_FRONTEND_MENU_UP:
				FRONTEND.PlaySound(FES_MOVE);
				if (mCursor != 1)
					mCursor = 1;
				break;

			case BUTTON_FRONTEND_MENU_DOWN:
				FRONTEND.PlaySound(FES_MOVE);
				if (mCursor != 0)
					mCursor = 0;
				break;

			case BUTTON_FRONTEND_MENU_SELECT:
				FRONTEND.PlaySound(FES_SELECT);
				mFinalChoice = mCursor;
				mFadeCount = 0;
				mFadeState = 2;
				mOnDismissPage = FEP_NONE;
				break;
			}
			break;
		}
	}
}

//*********************************************************************************
BOOL	CFEMessBox::IsInteractive()	// FUNCTION: 0x0044dea0
{
	if ((mBeingDisplayed) && (mOption))
		return TRUE;

	return FALSE;
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x0044d230 init:ZERO_FVECTOR
// FUNCTION: 0x0044d250 init:ID_FMATRIX
