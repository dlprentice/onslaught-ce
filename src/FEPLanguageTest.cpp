// FEPLanguageTest.cpp: the developers' page that steps through the message-box strings and plays each one's
// spoken sample (reconstructed from the retail PC executable; not in Stuart's release). Process, RenderPreCommon and
// TransitionNotification fold with other pages' identical functions (00452b60, 0051ae50, 00464b10).

#include	"Common.h"

#include	"FEPLanguageTest.h"
#include	"Frontend.h"
#include	"music.h"
#include	"renderinfo.h"
#include	<stdio.h>

// FUNCTION: 0x0051beb0 init:ZERO_FVECTOR
// FUNCTION: 0x0051bed0 init:ID_FMATRIX

//*********************************************************************************
BOOL	CFEPLanguageTest::Init()	// FUNCTION: 0x0051bfa0
{
	mCurrent = 0;
	mDetail = 0;
	mSample = NULL;
	mSampleName[0] = 0;

	return TRUE;
}

//*********************************************************************************
void	CFEPLanguageTest::Shutdown()	// FUNCTION: 0x0051bfc0
{
	delete mSample;
	mSample = NULL;
	mSampleName[0] = 0;
}

//*********************************************************************************
void	CFEPLanguageTest::Process(EFEPState state)
{
}

//*********************************************************************************
// Stop the last sample and play the current string's.
void	CFEPLanguageTest::PlaySample()	// FUNCTION: 0x0051bfe0
{
	if (mSample)
	{
		SOUND.StopSample(mSampleName);
		delete mSample;
		mSample = NULL;
		mSampleName[0] = 0;
	}

	char	*strings = FRONTEND.mTextSets[0].mStrings;
	SINT	sound = FRONTEND.mTextSets[0].GetSoundOffset(mCurrent);

	if (sound != -1)
	{
		sprintf(mSampleName, "%s\\messagebox\\%s.wav", TEXT.GetLanguageName(), strings + sound);
		SOUND.PlayNamedSample(mSampleName, NULL, 1.f);
		mSample = SOUND.GetSample(mSampleName);
	}
}

//*********************************************************************************
void	CFEPLanguageTest::ButtonPressed(SINT button, float val)	// FUNCTION: 0x0051c090
{
	SINT	old = mCurrent;

	switch (button)
	{
	case BUTTON_FRONTEND_MENU_UP:
		mCurrent --;
		if (mCurrent < 0)
		{
			mCurrent = 0;
			mScroll = 0;
			FRONTEND.PlaySound(FES_MOVE);
		}
		else
		{
			mDetail = 0;
			mScroll = 0;
			FRONTEND.PlaySound(FES_MOVE);
		}
		break;

	case BUTTON_FRONTEND_MENU_DOWN:
		mCurrent ++;
		if (mCurrent > TEXT.mNumEntries - 1)
			mCurrent = TEXT.mNumEntries - 1;
		else
			mDetail = 0;
		mScroll = 0;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_SKIP:
		mDetail --;
		if (mDetail < 0)
			mDetail = 0;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_SPECIAL:
		mDetail ++;
		if (mDetail > 40)
			mDetail = 40;
		FRONTEND.PlaySound(FES_MOVE);
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		PlaySample();
		break;

	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.SetPage(FEP_MAIN, 70);
		break;
	};

	float	dy = 0, dz = 0;

	switch (button)
	{
		case BUTTON_CAMERA_MOVE_FORAWRD:	dy += val; break;
		case BUTTON_CAMERA_MOVE_BACKWARDS:	dy += val; break;
		case BUTTON_CAMERA_PITCH_UP:		dz += val; break;
		case BUTTON_CAMERA_PITCH_DOWN:		dz += val; break;
	};

	if (dy != 0)
	{
		float	sign = (dy < 0) ? -1.f : 1.f;

		mScroll += sign * (dy * dy * dy * dy) * 50.f;

		float	step = float(floor(mScroll));
		if (step < 0)
			step += 1.f;

		mCurrent += SINT(step);
		mScroll -= step;

		if (mCurrent < 0)
			mCurrent = 0;
		if (mCurrent > TEXT.mNumEntries - 1)
			mCurrent = TEXT.mNumEntries - 1;
	}

	if (old != mCurrent)
		PlaySample();
}

//*********************************************************************************
void	CFEPLanguageTest::RenderPreCommon(float transition, EFrontEndPage dest)
{
	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}

//*********************************************************************************
// FUNCTION: 0x0051c280
void	CFEPLanguageTest::Render(float transition, EFrontEndPage dest)
{
	FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);

	// the title bar slides in from the right, the title on it
	float	in = 1.f - RangeTransition(transition, 0.75f, 1.f);
	float	bx = float(PLATFORM.GetWindowWidth()) - in * in * -400.f;
	FRONTEND.DrawBar(bx - 150.f, 76.f, 0.05f, 6, 0xaf1f1f1f, 0.7f);

	SIZE	size;
	PLATFORM.Font(FONT_TITLE)->GetTextExtent(ToWCHAR("LANGUAGE TEST"), &size);
	PLATFORM.Font(FONT_TITLE)->DrawTextDynamic(bx - 60.f - float(size.cx) * 0.6f, 76.f - float(size.cy / 2), 0.04f, 0.6f, 1.f, 0xff7f7f7f, ToWCHAR("LANGUAGE TEST"), 1000.f, 0, 0);

	RENDERINFO.SetFogEnabled(false);
	float	logo = RangeTransition(transition, 0.75f, 1.f);
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(logo);

	if (transition == 1.f)
	{
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(130.f, 140.f, 0.15f, 1.f, 1.f, 0xff7f7f7f, ToWCHAR("String:"), 1000.f, 0, 0);
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(130.f, 160.f, 0.15f, 1.f, 1.f, 0xff7f7f7f, ToWCHAR("Source File:"), 1000.f, 0, 0);
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(130.f, 180.f, 0.15f, 1.f, 1.f, 0xff7f7f7f, ToWCHAR("String ID:"), 1000.f, 0, 0);
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(130.f, 210.f, 0.15f, 1.f, 1.f, 0xff7f7f7f, ToWCHAR("Source Text:"), 1000.f, 0, 0);
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(130.f, 310.f, 0.15f, 1.f, 1.f, 0xff7f7f7f, ToWCHAR("Translated:"), 1000.f, 0, 0);
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(130.f, 420.f, 0.15f, 1.f, 1.f, 0xff7f7f7f, ToWCHAR("Sample? :"), 1000.f, 0, 0);

		CText	&source = FRONTEND.mTextSets[0];
		char	buf[300];
		sprintf(buf, "%d / %d", mCurrent + 1, source.mNumEntries);
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(250.f, 140.f, 0.15f, 1.f, 1.f, 0xff7f7f7f, ToWCHAR(buf), 1000.f, 0, 0);

		SINT	i;
		for (i = 0; (i < source.mNumSourceFiles) && (source.mSourceFiles[i].mFirst <= mCurrent); i++)
			;
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(250.f, 160.f, 0.15f, 1.f, 1.f, 0xff7f7f7f, ToWCHAR(source.mSourceFiles[i - 1].mName), 1000.f, 0, 0);
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(250.f, 180.f, 0.15f, 1.f, 1.f, 0xff7f7f7f, ToWCHAR(source.mIDNames[mCurrent]), 1000.f, 0, 0);

		// the two texts, six lines each, scrolled together by mDetail
		SWordWrapFill	fill;
		SINT	n, first, k, most;

		WCHAR	*text = source.mText + source.mFile->mEntries[mCurrent].mText;
		n = PLATFORM.Font(FONT_SMALL)->WordWrap(&fill, text, 240.f);
		SINT	max2 = -1;
		SINT	max1 = -1;
		first = 0;
		if (n > 6)
		{
			first = mDetail;
			most = n - 6;
			if (first > most)
			{
				first = most;
				max1 = most;
			}
			n = 6;
		}
		else
			max1 = 0;
		for (k = 0; k < n; k++)
		{
			PLATFORM.Font(FONT_SMALL)->GetTextExtent(fill.c[first + k], &size);
			PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(250.f, float(k * 16) + 210.f, 0.15f, 1.f, 1.f, 0xff7f7f3f, fill.c[first + k], 1000.f, 0, 0);
		}

		text = TEXT.mText + TEXT.mFile->mEntries[mCurrent].mText;
		n = PLATFORM.Font(FONT_SMALL)->WordWrap(&fill, text, 240.f);
		first = 0;
		if (n > 6)
		{
			first = mDetail;
			most = n - 6;
			if (first > most)
			{
				first = most;
				max2 = most;
			}
			n = 6;
		}
		else
			max2 = 0;
		for (k = 0; k < n; k++)
			PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(250.f, float(k * 16) + 310.f, 0.15f, 1.f, 1.f, 0xff3f7f7f, fill.c[first + k], 1000.f, 0, 0);

		if ((max1 != -1) && (max2 != -1))
		{
			if (max1 > max2)
				mDetail = max1;
			else
				mDetail = max2;
		}

		char	*strings = source.mStrings;
		SINT	sound = source.mFile->mEntries[mCurrent].mSound;
		if (sound != -1)
			text = ToWCHAR(strings + sound);
		else
			text = ToWCHAR("No Sample");
		PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(250.f, 420.f, 0.15f, 1.f, 1.f, 0xff7f7f7f, text, 1000.f, 0, 0);
	}
}

//*********************************************************************************
void	CFEPLanguageTest::TransitionNotification(EFrontEndPage from)
{
	mStartTime = PLATFORM.GetSysTimeFloat() + 2.f;
}
