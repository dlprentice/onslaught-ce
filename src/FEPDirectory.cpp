// FEPDirectory.cpp: the PC's list of save files, for loading one or deleting one (reconstructed from the retail PC
// executable; not in Stuart's release).

#include	"Common.h"

#include	"FEPDirectory.h"
#include	"Frontend.h"
#include	"MemoryCard.h"
#include	"mouse.h"
#include	"renderinfo.h"
#include	<math.h>
#include	<string.h>

// FUNCTION: 0x0051a9a0 init:ZERO_FVECTOR
// FUNCTION: 0x0051a9c0 init:ID_FMATRIX

//*********************************************************************************
BOOL	CFEPDirectory::Init()	// FUNCTION: 0x0051aa90
{
	mCursor = 0;
	mUnknown400c = 0;
	mNumSaves = 0;
	mTime = 0;
	memset(mSaves, 0, sizeof(mSaves));

	return TRUE;
}

//*********************************************************************************
void	CFEPDirectory::Shutdown()	// FUNCTION: 0x0051aac0
{
	for (int i = 0; i < MAX_DIRECTORY_SAVES; i++)
		SAFE_DELETE(mSaves[i]);
}

//*********************************************************************************
void	CFEPDirectory::ButtonPressed(SINT button, float val)	// FUNCTION: 0x0051aaf0
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_DOWN:
		if ((mCursor < MAX_DIRECTORY_SAVES - 1) && (mSaves[mCursor + 1]))
		{
			FRONTEND.PlaySound(FES_SELECT);
			mCursor ++;
		}
		mTime = FRONTEND.GetCounter();
		break;

	case BUTTON_FRONTEND_MENU_UP:
		if (mCursor > 0)
		{
			FRONTEND.PlaySound(FES_SELECT);
			mCursor --;
		}
		mTime = FRONTEND.GetCounter();
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		if (mSaves[mCursor])
		{
			FRONTEND.PlaySound(FES_SELECT);

			if (FRONTEND.GetSaveMode())
			{
				FEMESSBOX.Kill();
				FEMESSBOX.Create(320.f, 240.f, 400.f, FO(100), FET.GetText(FEX_CONFIRM_SAVEGAME_DELETE), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_YESNO, FEMB_Q_DELETE_SAVE);
			}
			else
			{
				FRONTEND.mFEPLoadGame.mSaveGameNumber = mCursor;
				wcscpy(FRONTEND.mFEPLoadGame.mSaveGameName, mSaves[mCursor]);
				FRONTEND.SetPage(FEP_LOADGAME, 0);
			}
		}
		break;

	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.PlaySound(FES_BACK);
		FRONTEND.SetPage(FEP_MAIN, 50);
		break;
	};
}

//*********************************************************************************
void	CFEPDirectory::Process(EFEPState state)	// FUNCTION: 0x0051ac40
{
	if (state == FEPS_INACTIVE)
		return;

	RefreshSaveFileList(state);

	if ((FEMESSBOX.BeingDisplayed()) && (FEMESSBOX.GetQuestion() == FEMB_Q_DELETE_SAVE))
	{
		switch (FEMESSBOX.GetFinalChoice())
		{
		case FEMB_YES:
			if (mSaves[mCursor])
			{
				FRONTEND.StartMCOperation(FET.GetText(FEX_DELETING, FRONTEND.GetMemoryCardNumber()));
				int	result = MEMORYCARD.DeleteSave(FRONTEND.GetMemoryCardNumber(), mCursor, mSaves[mCursor]);
				FRONTEND.EndMCOperation();

				if (result == MCE_SUCCESS)
					return;
			}

			FEMESSBOX.Create(320.f, 240.f, 400.f, FO(100), FET.GetText(FEX_SAVEGAME_COULD_NOT_DELETE), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_OK, 0);
			break;
		};
	}
}

//*********************************************************************************
// Read the card's save names into the list, and free the slots past the end.
void	CFEPDirectory::RefreshSaveFileList(EFEPState state)	// FUNCTION: 0x0051ad30
{
	BOOL	present;

	MEMORYCARD.Update();

	int		card = FRONTEND.GetMemoryCardNumber();

	MEMORYCARD.GetCardInfo(card, &present, NULL, NULL, NULL);

	if ((state == FEPS_ACTIVE) && (!present))
	{
		FRONTEND.SetPage(FEP_MAIN, 50);
		return;
	}

	MEMORYCARD.GetNumSaves(card, &mNumSaves);

	int		i;

	for (i = 0; i < mNumSaves; i++)
	{
		if (!mSaves[i])
#line 208
			mSaves[i] = new (MEMTYPE_FRONTEND) WCHAR[256];

		MEMORYCARD.GetSaveName(card, i, mSaves[i]);
	}

	for (i = mNumSaves; i < MAX_DIRECTORY_SAVES; i++)
		SAFE_DELETE(mSaves[i]);

	if (mCursor >= mNumSaves)
		mCursor = mNumSaves - 1;
	if (mCursor < 0)
		mCursor = 0;
}

//*********************************************************************************
void	CFEPDirectory::RenderPreCommon(float transition, EFrontEndPage dest)	// FUNCTION: 0x0051ae50
{
	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}

//*********************************************************************************
// The list's layout: three read-only floats, each read only by RenderSaveFileList. The four floats after them
// (005e49a0..005e49ac: 5.5, -100, 510, 11) are that function's literals, first emitted by this object.
extern const float	DIR_TOP = 130.f;			// 005e4994
extern const float	DIR_LEFT = 132.f;			// 005e4998
extern const float	DIR_LINE = 24.f;			// 005e499c: a row's height

#define DIR_VISIBLE_ROWS	11

// FUNCTION: 0x0051ae70
WCHAR	*CFEPDirectory::RenderSaveFileList(float transition, EFrontEndPage dest)
{
	float	top = DIR_TOP;
	float	bottom = DIR_LINE * DIR_VISIBLE_ROWS + DIR_TOP + 7.f;
	WCHAR	*clicked = NULL;
	SINT	alpha = SINT(RangeTransition(transition, 0.5f, 1.f) * 255.f);

	FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);

	DWORD	boxcol = 0x80ffffff;
	if (transition > 0.5f)
		boxcol = BlendAlpha2(boxcol, SINT((transition - 0.5f) * 510.f));
	else
		boxcol = 0;

	FRONTEND.DrawPanel(DIR_LEFT - 4.f, DIR_TOP, 530.f, bottom, 0.55f, BlendAlpha2(0xa0000000, alpha));
	FRONTEND.DrawBox(DIR_LEFT - 4.f, DIR_TOP, 530.f, bottom, boxcol, 2.f, 0.5f);

	// keep the cursor in view
	while (mUnknown400c + DIR_VISIBLE_ROWS <= mCursor)
		mUnknown400c++;
	while (mUnknown400c > mCursor)
		mUnknown400c--;

	SINT	i;
	for (i = mUnknown400c; i < MAX_DIRECTORY_SAVES; i++)
	{
		if (!mSaves[i])
			break;
		SINT	row = i - mUnknown400c;
		if (row >= DIR_VISIBLE_ROWS)
			break;

		DWORD	col;
		if (i == mCursor)
		{
			SINT	pulse = SINT((float)cos((FRONTEND.GetCounter() * 3.14f) * (1.f / 30.f)) * -100.f);
			col = 0xff969696 - pulse * 0x010101;
		}
		else
			col = 0xff404040;
		col = BlendAlpha2(col, alpha);

		float	y = float(row) * DIR_LINE + DIR_TOP;

		WCHAR	text[22];
		wcsncpy(text, mSaves[i], 18);
		if (text[17])
			wcscpy(&text[17], ToWCHAR("..."));

		PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(DIR_LEFT, y, FO(100), 1.f, 1.f, col, text, 1000.f, 0, 0);

		if (FRONTEND.MouseOverRect(DIR_LEFT, y, 510.f, y + DIR_LINE))
		{
			mCursor = i;
			mTime = FRONTEND.GetCounter();
		}
		if (FRONTEND.MouseClickedInRect(DIR_LEFT, y, 510.f, y + DIR_LINE))
		{
			mCursor = i;
			mTime = FRONTEND.GetCounter();
			clicked = mSaves[i];
		}
	}

	if (i == 0)
	{
		SIZE	size;
		DWORD	col = 0x80ffffff;
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(FET.GetText(FEX_EMPTY_DIRECTORY), &size);
		float	x = (640.f - float(size.cx)) * 0.5f;
		PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(x, DIR_LINE * 5.5f + DIR_TOP, FO(100), 1.f, 1.f, BlendAlpha2(col, alpha), FET.GetText(FEX_EMPTY_DIRECTORY), 1000.f, 0, 0);
	}
	else
	{
		// the scroll bar
		FRONTEND.DrawBox(510.f, top, 530.f, bottom, boxcol, 2.f, 0.5f);

		float	height = bottom - top;
		float	num = float(mNumSaves);
		float	per = (height - 10.f) / num;
		float	bar_top = float(mUnknown400c) * per + top + 5.f;
		float	bar_bottom = float(i - mUnknown400c) * per + bar_top;
		float	bar = bar_bottom - bar_top;

		DWORD	barcol = 0xffffffff;
		if (transition > 0.5f)
			barcol = BlendAlpha2(barcol, SINT((transition - 0.5f) * 510.f));
		else
			barcol = 0;
		FRONTEND.DrawBox(515.f, bar_top, 525.f, bar_bottom, barcol, 5.f, 0.5f);

		if (FRONTEND.GetCursorStateInRect(510.f, top, 530.f, bottom) && mNumSaves && MOUSE_HELD[0])
		{
			mCursor = SINT(((float(MOUSE_Y) - top - bar * 0.5f) / (height - bar)) * (float(mNumSaves) - 0.1f));
			if (mCursor < 0)
				mCursor = 0;
			if (mCursor >= mNumSaves)
				mCursor = mNumSaves - 1;

			if (mNumSaves > DIR_VISIBLE_ROWS)
				mUnknown400c = SINT(((float(MOUSE_Y) - top) / height) * (float(mNumSaves - DIR_VISIBLE_ROWS) + 0.9f));
			else
				mUnknown400c = 0;
		}

		FRONTEND.GetCommonPage()->RenderHelpText(5, transition);
	}

	FRONTEND.GetCommonPage()->RenderHelpText(1, transition);
	return clicked;
}

//*********************************************************************************
void	CFEPDirectory::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x0051b460
{
	if (RenderSaveFileList(transition, dest))
		ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);

	FRONTEND.DrawTitleBar(FET.GetText(FRONTEND.GetSaveMode() ? FEX_DELETEGAME2 : FEX_SELECTLOADGAME2), transition, dest);

	float tr = RangeTransition(transition, 0.75f, 1.f);
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(tr);
}
