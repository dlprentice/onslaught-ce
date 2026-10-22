// FEPMain.cpp: the main menu page (reconstructed from the retail PC executable; not in Stuart's release).

#include	"Common.h"

#include	"FEPMain.h"
#include	"Frontend.h"
#include	"MemoryCard.h"
#include	"career.h"
#include	"cliparams.h"
#include	"music.h"
#include	"text.h"
#include	"ParticleManager.h"
#include	<stdio.h>
#include	"Platform.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	"TransitionHelpers.h"
#include	"SpriteRenderer.h"
int VERSION_MINOR = 0; int VERSION_BUILD = 0;	// 00679980, 00679984: initialized and defined before Process, so they precede its local `started` (placement evidence)
//*********************************************************************************
void	CFEPMain::Shutdown()	// FUNCTION: 0x0040c640
{
}

BOOL	CFEPMain::Init()	// FUNCTION: 0x004621b0
{
	mSelectFlash = -1.f;
	mLeftArrowFlash = 0.f;
	mRightArrowFlash = 0.f;

	return TRUE;
}

//*********************************************************************************
int		CFEPMain::GetNumActions()	// FUNCTION: 0x004621d0
{
	return 7;
}

//*********************************************************************************
BOOL	CFEPMain::IsActionAvailable(int action)	// FUNCTION: 0x004621e0
{
	switch (action)
	{
	case -1:
	case 0:
	case 2:
	case 4:
	case 5:
	case 6:
		return TRUE;

	case 3:
		return FRONTEND.NumControllersPresent() >= 2;

	case 1:
	case 8:
		return CAREER.InProgress();
	}

	return FALSE;
}

//*********************************************************************************
void	CFEPMain::ButtonPressed(SINT button, float val)	// FUNCTION: 0x00462250
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_UP:
		if (gAmericanText)
		{
			if (mSelected > 0)
			{
				do
				{
					mSelected --;
				} while (!IsActionAvailable(mSelected));
				mSelectFlash = 0;
			}
			if (mSelected < 0)
				mSelected = 0;
		}
		else
		{
			if (mSelected > -1)
			{
				do
				{
					mSelected --;
				} while (!IsActionAvailable(mSelected));
				mSelectFlash = 0;
			}
			if (mSelected < -1)
				mSelected = -1;
		}
		mCursorTarget = mSelected;
		FRONTEND.PlaySound(FES_MOVE);
		mIdleTime = 0;
		break;

	case BUTTON_FRONTEND_MENU_DOWN:
		if (mSelected < GetNumActions() - 1)
		{
			do
			{
				mSelected ++;
			} while (!IsActionAvailable(mSelected));
			mSelectFlash = 0;
		}
		if (mSelected >= GetNumActions())
			mSelected = GetNumActions() - 1;
		mCursorTarget = mSelected;
		FRONTEND.PlaySound(FES_MOVE);
		mIdleTime = 0;
		break;

	case BUTTON_FRONTEND_MENU_LEFT:
		if (mSelected == -1)
		{
			int language = TEXT.GetLanguage() - 1;
			if (language < 0)
				language = 4;
			FRONTEND.SetLanguage(language);
			FRONTEND.PlaySound(FES_MOVE);
			mLeftArrowFlash = 1.f;
		}
		mIdleTime = 0;
		break;

	case BUTTON_FRONTEND_MENU_RIGHT:
		if (mSelected == -1)
		{
			int language = TEXT.GetLanguage() + 1;
			if (language == 5)
				language = 0;
			FRONTEND.SetLanguage(language);
			FRONTEND.PlaySound(FES_MOVE);
			mRightArrowFlash = 1.f;
		}
		mIdleTime = 0;
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		DoAction();
		break;
	};
}

//*********************************************************************************
void	CFEPMain::StartNewCareer()
{
	CAREER.Blank();
	FRONTEND.mFEPVirtualKeyboard.SetDefaultName();
	FRONTEND.SetSaveMode(TRUE);
	FRONTEND.mFEPDevSelect.Open();
	FRONTEND.SetSuccessFEP(FEP_LEVEL_SELECT, 0);
	FRONTEND.SetAutoSave(CFrontEnd::AUTO_SAVE_NOT);
	FRONTEND.mSelectedLevel = 100;
	FRONTEND.mFEPLevelSelect.SyncToSelectedLevel();
}

//*********************************************************************************
void	CFEPMain::DoAction()	// FUNCTION: 0x004623e0
{
	switch (mSelected)
	{
	case 0:
		if (CAREER.InProgress())
			FEMESSBOX.Create(320.0f, 240.0f, 300.0f, FO(100), FET.GetText(FEX_ABANDONCG), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_YESNO, FEMB_Q_NEW_CAREER);
		else
			StartNewCareer();
		break;

	case 1:
		FRONTEND.SetPage(FEP_LEVEL_SELECT, 70);
		break;

	case 2:
		if (CAREER.InProgress())
			FEMESSBOX.Create(320.0f, 240.0f, 300.0f, FO(100), FET.GetText(FEX_ABANDONCG), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_YESNO, FEMB_Q_LOAD_CAREER);
		else
		{
			FRONTEND.SetSaveMode(FALSE);
			FRONTEND.mFEPDevSelect.Open();
			FRONTEND.SetSuccessFEP(FEP_LEVEL_SELECT, 0);
		}
		break;

	case 8:
		FRONTEND.SetSaveMode(TRUE);
		if (FRONTEND.GetAutoSave() == CFrontEnd::AUTO_SAVE_NORMAL)
		{
			FRONTEND.SetSaveMode(TRUE);
			FRONTEND.SetPage(FEP_SAVEGAME, 0);
			FRONTEND.SetSuccessFEP(FEP_MAIN, 70);
		}
		else
		{
			FRONTEND.mFEPDevSelect.Open();
			FRONTEND.SetSuccessFEP(FEP_MAIN, 70);
		}
		break;

	case 3:
		FRONTEND.SetPage(FEP_MULTIPLAYER, 70);
		break;

	case 4:
		FRONTEND.SetPage(FEP_GOODIES, 70);
		break;

	case 5:
		FRONTEND.SetPage(FEP_OPTIONS, 70);
		break;

	case 6:
		FEMESSBOX.Kill();
		FEMESSBOX.Create(320.f, 240.f, 400.f, FO(100), Localization_GetStringById(0xe4), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_YESNO, FEMB_Q_QUIT);
		FEMESSBOX.SetOnDismiss(FEP_MAIN, 0);
		break;
	}

	FRONTEND.PlaySound(FES_SELECT);
	mIdleTime = 0;
}

//*********************************************************************************
void	CFEPMain::Process(EFEPState state)	// FUNCTION: 0x00462640
{
	if (state != FEPS_INACTIVE)
		MEMORYCARD.Update();

	if (state == FEPS_ACTIVE)
	{
		// fireworks once the game is complete
		if ((CAREER.GetNodeFromWorldNo(800)->mComplete) || (CAREER.GetNodeFromWorldNo(741)->mComplete))
		{
			FED.mCamera.mPos = FVector(0.f, -20.f, 0.f);
			FED.mCamera.mOrientation = ID_FMATRIX;
			FED.mCamera.mZoom = 1.f;
			FRONTEND.UpdateCamera();

			if ((rand() % 50) == 0)
			{
				CParticleDescriptor *pd = PARTICLE_SET.FindByName("Firework Explosion Blue Effect");
				int z = (rand() & 255) - 128;
				int x = (rand() & 255) - 128;
				PARTICLE_MANAGER.CreateEffect(pd, NULL, FVector(float(x) * 0.078125f, 0.f, float(z) * 0.0390625f - 5.f), 0, 0);
			}
		}

		if (CLIPARAMS.mAutoConfigTest)
		{
			static bool started = false;

			if (!started)
			{
				mCursorTarget = mSelected = 1;
				DoAction();
				started = true;
			}
			else
			{
				mCursorTarget = mSelected = 7;
				DoAction();
			}
		}

		while ((mSelected > 0) && (!IsActionAvailable(mSelected)))
			mSelected --;
		while ((mSelected < GetNumActions() - 1) && (!IsActionAvailable(mSelected)))
			mSelected ++;
	}

	// the language changed: save it with the career and the options
	if ((FEMESSBOX.BeingDisplayed()) && (FEMESSBOX.GetQuestion() == FEMB_Q_LANGUAGE) && (FEMESSBOX.mFadeState == 1))
	{
		if (CLIPARAMS.mMusic)
			MUSIC.Stop();
		SOUND.ReloadLanguageSampleBank();

		int		size = CAREER.SizeOfSaveGame();
#line 148
		char	*buffer = new (MEMTYPE_SCRATCHPAD) char[size];
		CAREER.Save(buffer);

		if (FRONTEND.mFEPSaveGame.mSaveGameName[0])
		{
			int save;
			if (MEMORYCARD.CreateSave(FRONTEND.GetMemoryCardNumber(), FRONTEND.mFEPSaveGame.mSaveGameName, &save, TRUE) == MCE_SUCCESS)
				MEMORYCARD.WriteSave(FRONTEND.GetMemoryCardNumber(), save, FRONTEND.mFEPSaveGame.mSaveGameName, buffer, size);
		}

		CFEPOptions::WriteDefaultOptionsFile(buffer, size);
		delete [] buffer;

		if (CLIPARAMS.mMusic)
			MUSIC.PlaySelection(MUS_FRONTEND);
	}

	if ((FEMESSBOX.BeingDisplayed()) && (FEMESSBOX.GetQuestion() == FEMB_Q_QUIT))
	{
		switch (FEMESSBOX.GetFinalChoice())
		{
		case FEMB_YES:
			FRONTEND.SetQuit(-1);
			break;

		case FEMB_NO:
			FEMESSBOX.mFadeCount = 0.f;
			FEMESSBOX.mFadeState = 2;
			FEMESSBOX.mOnDismissPage = FEP_NONE;
			break;
		}
	}

	if (mSelectFlash != -1.f)
	{
		mSelectFlash += 0.05f;
		if (mSelectFlash > 1.f)
			mSelectFlash = 1.f;
	}

	mLeftArrowFlash -= 0.1f;
	if (mLeftArrowFlash < 0.f)
		mLeftArrowFlash = 0.f;
	mRightArrowFlash -= 0.1f;
	if (mRightArrowFlash < 0.f)
		mRightArrowFlash = 0.f;

	if (FEMESSBOX.BeingDisplayed())
	{
		switch (FEMESSBOX.GetQuestion())
		{
		case FEMB_Q_NEW_CAREER:
			if (FEMESSBOX.GetFinalChoice() == FEMB_YES)
				StartNewCareer();
			break;

		case FEMB_Q_LOAD_CAREER:
			if (FEMESSBOX.GetFinalChoice() == FEMB_YES)
			{
				FRONTEND.SetSaveMode(FALSE);
				FRONTEND.mFEPDevSelect.Open();
				FRONTEND.SetSuccessFEP(FEP_LEVEL_SELECT, 70);
			}
			break;
		}
	}

	if (!FEMESSBOX.BeingDisplayed())
	{
		mIdleTime += gFrontEndFrameStep * 50.f;
		if (state == FEPS_ACTIVE)
		{
			if (!CLIPARAMS.mAttractMode)
			{
				if (mIdleTime > 165000.f)
					FRONTEND.SetPage(FEP_INTRO, 0);
			}
			else
				FRONTEND.SetPage(FEP_INTRO, 0);
		}
	}

	if ((state == FEPS_ACTIVE) && (mSelected != -1) && (SOUND.LanguageChanged(FALSE)))
		FEMESSBOX.Create(320.0f, 240.0f, 300.0f, FO(100), TEXT.GetStringById(0x3848a7), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_NONE, FEMB_Q_LANGUAGE);

	float target = float(mCursorTarget);
	mCursorPos += (target - mCursorPos) * 0.2f;
	if (fabsf(target - mCursorPos) < 0.05f)
		mCursorPos = target;
}

//*********************************************************************************
// STAND-IN (spelling): the centre is FEPGoodies' magic-offset pattern (its RenderPreCommon keeps it commented out) with
// both offsets 0: retail keeps the shared `magic * scale` product in dest's slot (the store of 0 at 00462c01) and subtracts
// it from memory twice; the plain centre (window size / 2) is 121 bytes off. The alpha is worked out inside the call's
// colour argument, after x (00462c4d).
void	CFEPMain::RenderPreCommon(float transition, EFrontEndPage dest)	// FUNCTION: 0x00462b70
{
	float t = RangeTransition(transition, 0.5f, 1.f);
	if (dest == FEP_INTRO)
		t = 1.f;

	float u = 2.f - t;
	float scale = ((u * u * u) - 1.f) * 0.87f + 1.f;

	float	x, y;

	float	magic_x;
	float	magic_y;

	magic_x = 0.f;
	magic_y = 0.f;

	x = (PLATFORM.GetWindowWidth()  / 2.f) + (magic_x);
	y = (PLATFORM.GetWindowHeight() / 2.f) + (magic_y);

	x -= magic_x * (scale);
	y -= magic_y * (scale);

	float v = 1.f - u;

	FRONTEND.GetCommonPage()->RenderVideoBackground(scale, (int((1.f - v * v) * 255.f) * 0x3f0000) | 0xffffff, x, y);
}

//*********************************************************************************
WCHAR	*CFEPMain::GetActionText(int action)	// FUNCTION: 0x00462c90
{
	switch (action)
	{
	case 0:		return FET.GetText(FEX_NEW_GAME);
	case 1:		return FET.GetText(FEX_CONTINUE_GAME);
	case 2:		return FET.GetText(FEX_LOAD_GAME);
	case 8:		return FET.GetText(FEX_SAVE_GAME);
	case 3:		return FET.GetText(FEX_MULTIPLAYER);
	case 4:		return FET.GetText(FEX_GOODIES);
	case 5:		return FET.GetText(FEX_OPTIONS);
	default:	return FET.GetText(FEX_QUIT);
	}
}

int			VERSION_MAJOR = 1;						// 00629410: the version (ltshell.cpp reads it from the file version)
//			VERSION_MINOR: 00679980 (defined after the includes)
//			VERSION_BUILD: 00679984
extern bool	gInFrontEnd;							// 00679b40 (FrontEnd.cpp)

//*********************************************************************************
// The menu: the Forseti strip scrolling behind, the actions (the one selected on a bar), the language row with its flag
// and arrows, the title's brackets and symbols (which assemble when the intro hands over), the selected action's symbol,
// the version, the title and its passing shine, and a white flash as the page arrives.
void	CFEPMain::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x00462d40
{
	RENDERINFO.SetFogEnabled(false);

	// STAND-IN (frame slots): y is the strip's position here and the menu rows' below: one variable (retail keeps both at
	// esp+0x10; a separate strip variable is 124 bytes off). `scale` below is likewise one slot (esp+0x14).
	float	y = 175.f - (float)fmod(FRONTEND.GetCounter() * 0.3f, 350.0);
	DWORD	col = BlendAlpha2(0x3fffffff, SINT(RangeTransition(transition, 0.75f, 1.f) * 255.f));
	CSPRITERENDERER::DrawColouredSprite(458.f, y, 0.9f, FED.GetTexture(FET_FORSETI_TEXT), col, 1.f, 1.f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(458.f, y + 350.f, 0.9f, FED.GetTexture(FET_FORSETI_TEXT), col, 1.f, 1.f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(458.f, y + 700.f, 0.9f, FED.GetTexture(FET_FORSETI_TEXT), col, 1.f, 1.f, SA_CENTRE);

	// the actions, from the language row when it is offered
	y = 268.f;
	float	scale;		// the arrows' flash, the brackets and symbols, and the white flash all size with it (one slot, esp+0x14)
	SINT	alpha = MakeAlpha(RangeTransition(transition, 0.75f, 1.f));
	int		start = -1;
	if (gAmericanText)
	{
		start = 0;
		y = 304.f;
	}
	for (int i = start; i < GetNumActions(); i++)
	{
		if (i != -1)
		{
			SIZE	size;
			WCHAR	*text = GetActionText(i);
			PLATFORM.Font(FONT_SMALL)->GetTextExtent(text, &size);
			BOOL	available = IsActionAvailable(i);
			if (i == mSelected)
			{
				alpha = MakeAlpha(RangeTransition(transition, 0.8f, 1.f));
				DWORD	bc = 0x7f000000;
				CSPRITERENDERER::DrawColouredSprite(219.f, y, 0.33f, FED.GetTexture(FET_BEA_TITLE_TEXT_BOX), BlendAlpha2(bc, alpha), (float(size.cx) + 30.f) * (1.f / 256.f), 1.f, SA_CENTRE);
			}

			DWORD	c = 0xff4f4f4f;
			if (!IsActionAvailable(i))
				c = 0x7f1f1f1f;
			if (i == mSelected)
				c = 0xffff6f3f;
			float	tx = 219.f - float(size.cx) * 0.5f;
			c = BlendAlpha2(BlendAlpha2(c, alpha), 255);
			float	ty = y - size.cy / 2;

			if ((transition > 0.9f) && (IsActionAvailable(i)))
			{
				if (CFrontEnd::GetCursorStateInRect(tx, ty, size.cx + tx, size.cy + ty))
					mSelected = i;
				if (CFrontEnd::MouseClickedInRect(tx, ty, size.cx + tx, size.cy + ty))
				{
					mSelected = i;
					ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);
				}
			}

			PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(tx, ty, 0.32f, 1.f, 1.f, c, text, 1000.f, 0, 0);
			RENDERINFO.SetFogEnabled(false);
			y += 20.f;
		}
		else
		{
			// the language row: the flag between its arrows
			DWORD	c = 0xff7f7f7f;
			if (mSelected == -1)
				c = 0xffdfdfdf - SINT(sinf((FRONTEND.GetCounter() * 3.14f) * (1.f / 30.f)) * -32.f) * 0x10101;
			c = BlendAlpha2(BlendAlpha2(c, alpha), 255);

			if ((transition > 0.9f) && (IsActionAvailable(-1)))
			{
				if (CFrontEnd::GetCursorStateInRect(119.f, y - 20.f, 319.f, y + 20.f))
					mSelected = -1;
			}
			if (CFrontEnd::MouseClickedInRect(119.f, y - 20.f, 199.f, y + 20.f))
			{
				mSelected = -1;
				ButtonPressed(BUTTON_FRONTEND_MENU_LEFT, 1.f);
			}
			if (CFrontEnd::MouseClickedInRect(239.f, y - 20.f, 319.f, y + 20.f))
			{
				mSelected = -1;
				ButtonPressed(BUTTON_FRONTEND_MENU_RIGHT, 1.f);
			}

			CSPRITERENDERER::DrawColouredSprite(219.f, y, 0.32f, FED.GetTexture(FET_FLAG_UK + TEXT.GetLanguage()), c, 0.65f, 0.25f, SA_CENTRE);

			if (((SINT(FRONTEND.GetCounter()) % 64) < 50) || (mLeftArrowFlash > 0.f))
			{
				DWORD	ac = 0xffffffff;
				BlendAlpha(ac, alpha);
				if (mSelected != -1)
					BlendAlpha(ac, 64);
				CSPRITERENDERER::DrawColouredSprite(154.f, y, 0.32f, FED.GetTexture(FET_HELP_ARROW), BlendAlpha2(ac, 255), 0.8f, 0.8f, SA_CENTRE, SPT_NONE, 1.f, -PI);
				DWORD	fc = 0xffffffff;
				BlendAlpha(fc, SINT(mLeftArrowFlash * 255.f));
				BlendAlpha(fc, alpha);
				scale = ((1.f - mLeftArrowFlash) + 1.f) * 0.8f;
				CSPRITERENDERER::DrawColouredSprite(154.f, y, 0.32f, FED.GetTexture(FET_HELP_ARROW), BlendAlpha2(fc, 255), scale, scale, SA_CENTRE, SPT_NONE, 1.f, -PI);
			}
			if (((SINT(FRONTEND.GetCounter()) % 64) < 50) || (mRightArrowFlash > 0.f))
			{
				DWORD	ac = 0xffffffff;
				BlendAlpha(ac, alpha);
				if (mSelected != -1)
					BlendAlpha(ac, 64);
				CSPRITERENDERER::DrawColouredSprite(284.f, y, 0.32f, FED.GetTexture(FET_HELP_ARROW), BlendAlpha2(ac, 255), 0.8f, 0.8f, SA_CENTRE, SPT_NONE, 1.f, 0.f);
				DWORD	fc = 0xffffffff;
				BlendAlpha(fc, SINT(mRightArrowFlash * 255.f));
				BlendAlpha(fc, alpha);
				scale = ((1.f - mRightArrowFlash) + 1.f) * 0.8f;
				CSPRITERENDERER::DrawColouredSprite(284.f, y, 0.32f, FED.GetTexture(FET_HELP_ARROW), BlendAlpha2(fc, 255), scale, scale, SA_CENTRE, SPT_NONE, 1.f, 0.f);
			}
			y += 36.f;
		}
	}
	RENDERINFO.SetFogEnabled(false);

	// the title's brackets: they close in as the intro hands over, and fly apart as the page leaves
	SINT	a = 255;
	scale = 1.25f;
	float	yaw = 0.f;
	if (transition < 1.f)
	{
		if (dest == FEP_INTRO)
		{
			if (transition < 0.2f)
			{
				float	f = RangeTransition(transition, 0.f, 0.2f);
				a = MakeAlpha(f);
				scale = f;
				yaw = -((1.f - f) * 0.3f);
			}
			else if (transition < 0.4f)
				scale = 1.f;
			else if (transition < 0.6f)
				scale = RangeTransition(transition, 0.4f, 0.6f) * 0.25f + 1.f;
		}
		else
		{
			float	f = 1.f - RangeTransition(transition, 0.5f, 1.f);
			a = MakeAlpha(1.f - f);
			scale = f * 10.f + 1.25f;
			yaw = f * -0.5f;
		}
	}
	CSPRITERENDERER::DrawColouredSprite(224.f + FRONTEND.GetShadowOffsetX(), 349.f + FRONTEND.GetShadowOffsetX(), 0.35f, FED.GetTexture(FET_BEA_TITLE_BRACKET01), BlendAlpha2(0x3f000000, a), scale * 1.05f, scale * 1.05f, SA_CENTRE, SPT_NONE, 1.f, yaw);
	col = 0xffffffff;
	BlendAlpha(col, a);
	CSPRITERENDERER::DrawColouredSprite(219.f, 344.f, 0.3f, FED.GetTexture(FET_BEA_TITLE_BRACKET01), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, yaw);

	a = 255;
	yaw = 0.f;
	bool	drawBracket2 = true;
	if ((transition < 1.f) && (dest == FEP_INTRO))
	{
		if (transition < 0.2f)
		{
			float	f = RangeTransition(transition, 0.f, 0.2f);
			a = MakeAlpha(f);
			scale = f;
			yaw = (1.f - f) * 0.3f;
		}
		else if (transition < 0.6f)
			scale = 1.f;
		else if (transition < 0.8f)
		{
			float	f = 1.f - RangeTransition(transition, 0.6f, 0.8f);
			a = MakeAlpha(f);
			scale = f;
			yaw = -((1.f - f) * 0.3f);
		}
		else
			drawBracket2 = false;
		if (drawBracket2)
		{
			CSPRITERENDERER::DrawColouredSprite(224.f + FRONTEND.GetShadowOffsetX(), 349.f + FRONTEND.GetShadowOffsetX(), 0.35f, FED.GetTexture(FET_BEA_TITLE_BRACKET02), BlendAlpha2(0x3f000000, a), scale * 1.05f, scale * 1.05f, SA_CENTRE, SPT_NONE, 1.f, yaw);
			col = 0xffffffff;
			BlendAlpha(col, a);
			CSPRITERENDERER::DrawColouredSprite(219.f, 344.f, 0.3f, FED.GetTexture(FET_BEA_TITLE_BRACKET02), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, yaw);
		}
	}

	// the title's symbol, the same way a little later
	a = 255;
	scale = 1.25f;
	yaw = 0.f;
	if (transition < 1.f)
	{
		if (dest == FEP_INTRO)
		{
			if (transition < 0.1f)
				a = 0;
			else if (transition < 0.3f)
			{
				float	f = RangeTransition(transition, 0.1f, 0.3f);
				a = MakeAlpha(f);
				scale = f;
				yaw = -(f * 0.3f);
			}
			else if (transition < 0.5f)
				scale = 1.f;
			else if (transition < 0.7f)
				scale = RangeTransition(transition, 0.5f, 0.7f) * 0.25f + 1.f;
		}
		else
		{
			float	f = 1.f - RangeTransition(transition, 0.75f, 1.f);
			a = MakeAlpha(1.f - f);
			scale = f * 1.4f + 1.25f;
			yaw = -f;
		}
	}
	CSPRITERENDERER::DrawColouredSprite(462.f + FRONTEND.GetShadowOffsetX(), 365.f + FRONTEND.GetShadowOffsetY(), 0.35f, FED.GetTexture(FET_BEA_TITLE_SYMBOL_BRACKET01), BlendAlpha2(0x3f000000, a), scale * 1.05f, scale * 1.05f, SA_CENTRE, SPT_NONE, 1.f, yaw);
	col = 0xffffffff;
	BlendAlpha(col, a);
	CSPRITERENDERER::DrawColouredSprite(457.f, 355.f, 0.3f, FED.GetTexture(FET_BEA_TITLE_SYMBOL_BRACKET01), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, yaw);

	a = 255;
	yaw = 0.f;
	bool	drawSymbolBracket2 = true;
	if ((transition < 1.f) && (dest == FEP_INTRO) && (!(transition < 0.1f)))
	{
		if (transition < 0.3f)
		{
			float	f = RangeTransition(transition, 0.1f, 0.3f);
			a = MakeAlpha(f);
			scale = f;
			yaw = -(f * 0.3f);
		}
		else if (transition < 0.7f)
			scale = 1.f;
		else if (transition < 0.9f)
		{
			float	f = 1.f - RangeTransition(transition, 0.7f, 0.9f);
			a = MakeAlpha(f);
			scale = f;
			yaw = -(f * 0.3f);
		}
		else
			drawSymbolBracket2 = false;
		if (drawSymbolBracket2)
		{
			CSPRITERENDERER::DrawColouredSprite(462.f + FRONTEND.GetShadowOffsetX(), 365.f + FRONTEND.GetShadowOffsetY(), 0.35f, FED.GetTexture(FET_BEA_TITLE_SYMBOL_BRACKET02), BlendAlpha2(0x3f000000, a), scale * 1.05f, scale * 1.05f, SA_CENTRE, SPT_NONE, 1.f, yaw);
			col = 0xffffffff;
			BlendAlpha(col, a);
			CSPRITERENDERER::DrawColouredSprite(457.f, 355.f, 0.3f, FED.GetTexture(FET_BEA_TITLE_SYMBOL_BRACKET02), col, scale, scale, SA_CENTRE, SPT_NONE, 1.f, yaw);
		}
	}

	// the selected action's symbol
	{
		int		tex = -1;
		switch (mSelected)
		{
		case -1:	tex = FET_BEA_TITLE_NAV_SYMBOL_LANGUAGE01;		break;
		case 0:		tex = FET_BEA_TITLE_NAV_SYMBOL_NEWGAME01;		break;
		case 1:		tex = FET_BEA_TITLE_NAV_SYMBOL_CONTINUEGAME01;	break;
		case 2:		tex = FET_BEA_TITLE_NAV_SYMBOL_LOADGAME01;		break;
		case 8:		tex = FET_BEA_TITLE_NAV_SYMBOL_SAVEGAME01;		break;
		case 3:		tex = FET_BEA_TITLE_NAV_SYMBOL_MULTIPLAYER01;	break;
		case 4:		tex = FET_BEA_TITLE_NAV_SYMBOL_GOODIES01;		break;
		case 5:		tex = FET_BEA_TITLE_NAV_SYMBOL_OPTIONS01;		break;
		case 6:		tex = FET_BEA_TITLE_NAV_SYMBOL_QUITGAME01;		break;
		}
		if (tex != -1)
		{
			a = MakeAlpha(RangeTransition(transition, 0.8f, 1.f));
			CSPRITERENDERER::DrawColouredSprite(462.f + FRONTEND.GetShadowOffsetX(), 365.f + FRONTEND.GetShadowOffsetY(), 0.34f, FED.GetTexture(tex), BlendAlpha2(0x3f000000, a), 1.05f, 1.05f, SA_CENTRE);
			CSPRITERENDERER::DrawColouredSprite(457.f, 355.f, 0.1f, FED.GetTexture(tex), BlendAlpha2(0xffffffff, a), 1.f, 1.f, SA_CENTRE);
		}
	}

	// the version, bottom left
	float	v = RangeTransition(transition, 0.75f, 1.f);
	char	version[100];
	sprintf(version, "V%1d.%02d", VERSION_MAJOR, VERSION_MINOR);
	gInFrontEnd = false;
	PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(0.f, float(PLATFORM.GetWindowHeight() - 16), 0.01f, 1.f, 1.f, (SINT(v * 255.f) << 24) | 0x102025, ToWCHAR(version), 1000.f, 0, 0);
	gInFrontEnd = true;
	RENDERINFO.SetFogEnabled(false);

	// the title, and the shine that passes over it
	a = SINT(RangeTransition(transition, 0.75f, 1.f) * 255.f);
	if (dest == FEP_INTRO)
		a = 255;
	CSPRITERENDERER::DrawColouredSprite(325.f + FRONTEND.GetShadowOffsetX(), 140.f + FRONTEND.GetShadowOffsetY(), 0.1f, FED.GetTexture(FET_BEA_TITLE2), BlendAlpha2(0x3f000000, a), 1.05f, 1.05f, SA_CENTRE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_ALWAYS);
	RENDERINFO.Apply();
	CSPRITERENDERER::DrawColouredSprite(320.f, 130.f, 0.999f, FED.GetTexture(FET_BEA_TITLE2), BlendAlpha2(0xffafcfff, a), 1.f, 1.f, SA_CENTRE);
	LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
	RENDERINFO.Apply();

	float	sx = FRONTEND.GetCounter() * 0.6f;
	sx = (float)fmod(sx, 512.0);
	sx = 256.f - sx;
	sx += 65.f;
	FRONTEND.EnableAdditiveAlpha();
	DWORD	shine = 0xff000000 + (UINT(a * 127) >> 8) * 0x10101;
	CSPRITERENDERER::DrawColouredSprite(sx, 120.f, 0.998f, FED.GetTexture(FET_LOGO_SHINE), shine, 1.f, 2.f, SA_CENTRE);
	CSPRITERENDERER::DrawColouredSprite(sx + 512.f, 120.f, 0.998f, FED.GetTexture(FET_LOGO_SHINE), shine, 1.f, 2.f, SA_CENTRE);
	FRONTEND.EnableModulateAlpha();

	// a white flash as the page arrives
	float	d = (float)fabs(transition - 0.75f);
	if ((d < 0.15f) && (dest != FEP_INTRO))
	{
		scale = PLATFORM.GetWindowWidth() * (1.f / 32.f);
		CSPRITERENDERER::DrawColouredSprite(0.f, 0.f, 0.001f, FED.GetTexture(FET2_WHITE), BlendAlpha2(0xffffffff, SINT((1.f - d * (1.f / 0.15f)) * 128.f)), scale, scale);
	}
}

//*********************************************************************************
void	CFEPMain::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x004644d0
{
	mSelectFlash = -1.f;
	mStartTime = PLATFORM.GetSysTimeFloat() + 1.f;

	if ((mSelected == 0) && (CAREER.InProgress()))
		mSelected = 1;

	mCursorTarget = mSelected;
	mCursorPos = float(mSelected);
}

//*********************************************************************************
void	CFEPMain::ActiveNotification(EFrontEndPage from)	// FUNCTION: 0x00464520
{
	mSelectFlash = 0;
	mIdleTime = 0;
}

// The per-file maths constants from maths.h (placed before the object's first function):
// FUNCTION: 0x004620c0 init:ZERO_FVECTOR
// FUNCTION: 0x004620e0 init:ID_FMATRIX
