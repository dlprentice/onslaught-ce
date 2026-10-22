// FEPDevSelect.cpp: the storage-device page (reconstructed from the retail PC executable; Stuart's file is not in his
// release). RenderPreCommon folds with CFEPDirectory's (0051ae50).

#include	"Common.h"

#include	"FEPDevSelect.h"
#include	"Frontend.h"
#include	"MemoryCard.h"
#include	"Career.h"
#include	"FEMessBox.h"
#include	"renderinfo.h"
#include	"SpriteRenderer.h"
#include	"Texture.h"

// FUNCTION: 0x00458530 init:ZERO_FVECTOR
// FUNCTION: 0x00458550 init:ID_FMATRIX

//*********************************************************************************
// Keep the storage-selection operation together: its inlined lifetime reproduces Init's delayed register saves.
// The helper name is reconstructed; no standalone retail function is claimed.
static inline void SelectInitialStorageDevice(CFEPDevSelect *page)
{
	if (MEMORYCARD.IsHDDAvailable())
	{
		page->mCursor = 0;
		page->mState = 0;
		page->mCard = -1;
		return;
	}

	int		num;

	MEMORYCARD.GetNumCards(&num);
	int		found = -1;

	for (int i = 0; (i < num) && (found == -1); i++)
	{
		BOOL	present;

		MEMORYCARD.GetCardInfo(i, &present, NULL, NULL, NULL);
		if (present)
			found = i;
	}

	if (found == -1)
	{
		page->mCursor = -1;
		page->mState = -1;
		page->mCard = -2;
		return;
	}

	page->mState = 1;
	page->mCursor = found;
	page->mCard = found;
}

void	CFEPDevSelect::Shutdown()	// FUNCTION: 0x0040c640
{
}

BOOL	CFEPDevSelect::Init()	// FUNCTION: 0x00458620
{
	if (MEMORYCARD.Update())
		mCardsChanged = TRUE;

	if (PLAYABLE_DEMO)
		return TRUE;

	if (FRONTEND.GetAutoSave() == CFrontEnd::AUTO_SAVE_NOT)
	{
		SelectInitialStorageDevice(this);
	}

	return TRUE;
}

//*********************************************************************************
// Go on with the chosen device: to the name keyboard or the save list, or a message when there is no room.
bool	CFEPDevSelect::UseDevice()	// FUNCTION: 0x00458710
{
	if (mCard == -2)
		return true;

	FRONTEND.mMemoryCardNumber = mCard;

	if (FRONTEND.GetSaveMode())
	{
		BOOL	present, formatted;
		int		bytesfree, bytestotal, numsaves;

		MEMORYCARD.GetCardInfo(mCard, &present, &formatted, &bytesfree, &bytestotal);
		MEMORYCARD.GetNumSaves(mCard, &numsaves);

		if (!present)
		{
			WCHAR	name[200];
			WCHAR	text[200];

			MEMORYCARD.GetCardName(FRONTEND.GetMemoryCardNumber(), name);
			wcscpy(text, FET.GetText(FEX_MEMORY_CARD_REMOVED_XBOX, FRONTEND.GetMemoryCardNumber()));
			FEMESSBOX.Kill();
			FEMESSBOX.Create(320.f, 240.f, 400.f, FO(100), text, PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, FALSE);
			FEMESSBOX.SetOnDismiss(FEP_DEVSELECT, 0);
			return false;
		}

		if (formatted)
		{
			int		savesize;

			MEMORYCARD.GetSaveSize(CCareer::SizeOfSaveGame(), &savesize);

			if ((bytesfree < savesize) || (numsaves > 0xfff))
			{
				if (numsaves > 0)
					FRONTEND.mFEPSaveGame.AskIfYouWantToDelete(CAREER.InProgress(), numsaves > 0xfff, FALSE);
				else
				{
					FRONTEND.PlaySound(FES_SELECT);
					FEMESSBOX.Create(320.f, 240.f, 300.f, FO(100), FET.GetText(FEX_NOSPACE_NOSAVES_PS2, mCard + 1), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_OK, FALSE);
				}
				return true;
			}
		}

		FRONTEND.SetPage(FEP_VIRTUAL_KEYBOARD, 70);
		return true;
	}
	else
	{
		BOOL	present, formatted;
		int		bytesfree, bytestotal, numsaves;

		MEMORYCARD.GetCardInfo(mCard, &present, &formatted, &bytesfree, &bytestotal);
		MEMORYCARD.GetNumSaves(mCard, &numsaves);

		if (!present)
		{
			WCHAR	name[200];
			WCHAR	text[200];

			MEMORYCARD.GetCardName(FRONTEND.GetMemoryCardNumber(), name);
			wcscpy(text, FET.GetText(FEX_MEMORY_CARD_REMOVED_XBOX, FRONTEND.GetMemoryCardNumber()));
			FEMESSBOX.Kill();
			FEMESSBOX.Create(320.f, 240.f, 400.f, FO(100), text, PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, FALSE);
			FEMESSBOX.SetOnDismiss(FEP_DEVSELECT, 0);
			return false;
		}

		if (!formatted)
		{
			FRONTEND.PlaySound(FES_SELECT);
			FEMESSBOX.Create(320.f, 240.f, 300.f, FO(100), FET.GetText(FEX_UNFORMATTED_PS2, mCard + 1), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_OK, FALSE);
			return true;
		}

		FRONTEND.SetPage(FEP_DIRECTORY, 70);
		return true;
	}
}

//*********************************************************************************
void	CFEPDevSelect::Open()	// FUNCTION: 0x004589f0
{
	CheckDevice(FALSE);
	UseDevice();
}

//*********************************************************************************
void	CFEPDevSelect::ButtonPressed(SINT button, float val)	// FUNCTION: 0x00458a10
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_UP:
		if ((mState == 1) && (MEMORYCARD.IsHDDAvailable()))
		{
			FRONTEND.PlaySound(FES_MOVE);
			mState = 0;
			mCard = -1;
			return;
		}
		break;

	case BUTTON_FRONTEND_MENU_DOWN:
		if (mState == 0)
		{
			{
				BOOL	formatted;

				MEMORYCARD.GetCardInfo(mCursor, (BOOL *)&button, &formatted, NULL, NULL);
				if (button)
				{
					mState = 1;
					mCard = mCursor;
					FRONTEND.PlaySound(FES_MOVE);
					return;
				}
			}

			MEMORYCARD.GetNumCards(&button);

			int		found = -1;

			for (int i = 0; (i < button) && (found == -1); i++)
			{
				BOOL	p, f;

				MEMORYCARD.GetCardInfo(i, &p, &f, NULL, NULL);
				if (p)
					found = i;
			}

			if (found == -1)
			{
				mState = 0;
				mCard = -1;
				return;
			}

			mState = 1;
			mCursor = found;
			mCard = found;
			FRONTEND.PlaySound(FES_MOVE);
			return;
		}
		break;

	case BUTTON_FRONTEND_MENU_LEFT:
		if (mState == 1)
		{
			int		found = -1;

			for (int i = mCursor - 1; (i >= 0) && (found == -1); i--)
			{
				BOOL	f;

				MEMORYCARD.GetCardInfo(i, (BOOL *)&button, &f, NULL, NULL);
				if (button)
					found = i;
			}

			if (found != -1)
			{
				mCursor = found;
				mCard = found;
				FRONTEND.PlaySound(FES_MOVE);
				return;
			}
		}
		break;

	case BUTTON_FRONTEND_MENU_RIGHT:
		if (mState == 1)
		{
			int		found = -1;

			MEMORYCARD.GetNumCards(&button);

			for (int i = mCursor + 1; (i < button) && (found == -1); i++)
			{
				BOOL	p, f;

				MEMORYCARD.GetCardInfo(i, &p, &f, NULL, NULL);
				if (p)
					found = i;
			}

			if (found != -1)
			{
				mCursor = found;
				mCard = found;
				FRONTEND.PlaySound(FES_MOVE);
				return;
			}
		}
		break;

	case BUTTON_FRONTEND_MENU_SELECT:
		if (!UseDevice())
			FRONTEND.PlaySound(FES_SELECT);
		return;

	case BUTTON_FRONTEND_MENU_BACK:
		if (FRONTEND.GetSuccessPage() != FEP_DEBRIEFING)
		{
			FRONTEND.SetPage(FEP_MAIN, 70);
			FRONTEND.PlaySound(FES_BACK);
			return;
		}
		break;

	case BUTTON_FRONTEND_MENU_SKIP:
		if (FRONTEND.GetSaveMode())
		{
			FRONTEND.PlaySound(FES_SELECT);
			FEMESSBOX.Create(320.f, 240.f, 300.f, FO(100), FET.GetText(FEX_NOAUTOSAVE), PLATFORM.Font(FONT_SMALL), 0, 10, FALSE, FEMB_OPTION_YESNO, FEMB_Q_NO_AUTOSAVE);
		}
		break;
	};
}

//*********************************************************************************
void	CFEPDevSelect::CheckDevice(BOOL unused)	// FUNCTION: 0x00458ce0
{
	if (MEMORYCARD.Update())
		mCardsChanged = TRUE;

	BOOL	present, formatted;

	if (mCard >= 0)
		MEMORYCARD.GetCardInfo(mCard, &present, &formatted, NULL, NULL);
	else
		present = FALSE;

	if ((mCard == -1) && (MEMORYCARD.IsHDDAvailable()))
		return;

	if (present)
		return;

	// the device has gone: choose again
	if (MEMORYCARD.IsHDDAvailable())
	{
		mCursor = 0;
		mState = 0;
		mCard = -1;
	}
	else
	{
		int		num;
		int		found = -1;

		MEMORYCARD.GetNumCards(&num);

		for (int i = 0; (i < num) && (found == -1); i++)
		{
			BOOL	p;

			MEMORYCARD.GetCardInfo(i, &p, NULL, NULL, NULL);
			if (p)
				found = i;
		}

		if (found != -1)
		{
			mCursor = found;
			mState = 1;
			mCard = found;
		}
		else
		{
			mCursor = -1;
			mState = -1;
			mCard = -2;
		}
	}

	if (FEMESSBOX.BeingDisplayed())
	{
		UINT	q = FEMESSBOX.GetQuestion();

		if ((q >= FEMB_Q_DELETE_SAVES) && ((q <= FEMB_Q_DELETE_SAVE) || (q == FEMB_Q_FORMAT)))
			FEMESSBOX.Kill();
	}
}

//*********************************************************************************
void	CFEPDevSelect::Process(EFEPState state)	// FUNCTION: 0x00458e00
{
	if (PLAYABLE_DEMO)
		return;

	if (state == FEPS_INACTIVE)
		return;

	CheckDevice(FALSE);

	if (FEMESSBOX.BeingDisplayed())
	{
		switch (FEMESSBOX.GetQuestion())
		{
		case FEMB_Q_DELETE_SAVES:
			switch (FEMESSBOX.GetFinalChoice())
			{
			case FEMB_NO:
				if (FRONTEND.GetCurrentPage() != FEP_MAIN)
					FRONTEND.SetPage(FEP_MAIN, 50);
				break;

			case FEMB_YES:
				FRONTEND.SetPage(FEP_DIRECTORY, 0);
				break;
			};
			break;

		case 5:
			FEMESSBOX.GetFinalChoice();
			break;

		case FEMB_Q_NO_AUTOSAVE:
			switch (FEMESSBOX.GetFinalChoice())
			{
			case FEMB_YES:
				FRONTEND.SetPage(FRONTEND.GetSuccessPage(), FRONTEND.GetSuccessTransTime());
				FRONTEND.SetAutoSave(CFrontEnd::AUTO_SAVE_PRETEND);
				CAREER.SetInProgress();
				break;
			};
			break;
		};
	}

	if (state == FEPS_ACTIVE)
		ButtonPressed(BUTTON_FRONTEND_MENU_SELECT, 1.f);
}

//*********************************************************************************
void	CFEPDevSelect::RenderPreCommon(float transition, EFrontEndPage dest)
{
	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}

//*********************************************************************************
void	CFEPDevSelect::Render(float transition, EFrontEndPage dest)	// FUNCTION: 0x00458ee0
{
	SIZE	size;

	FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);

	if (transition > 0.f)
	{
		int		num;
		BOOL	present, formatted;
		int		bytesfree, bytestotal;
		WCHAR	freetext[256];
		WCHAR	totaltext[256];
		WCHAR	name[256];
		SINT	i;

		MEMORYCARD.GetNumCards(&num);

		SINT	alpha = MakeAlpha((dest == FEP_VIRTUAL_KEYBOARD) ? RangeTransition(transition, 0.5f, 1.f) : RangeTransition(transition, 0.75f, 1.f));

		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
		RENDERINFO.Apply();

		// the hard disk, then the cards two to a column
		if (MEMORYCARD.IsHDDAvailable())
		{
			MEMORYCARD.GetCardInfo(-1, NULL, NULL, &bytesfree, &bytestotal);
			RenderDevice(200.f, 120.f, float(bytestotal) - float(bytesfree), float(bytestotal), 0.89f, 1.f, alpha, mState == 0, TRUE, TRUE, TRUE);
		}

		for (i = 0; i < num; i++)
		{
			MEMORYCARD.GetCardInfo(i, &present, &formatted, &bytesfree, &bytestotal);
			if ((!present) || (!formatted))
			{
				bytesfree = 0;
				bytestotal = 0;
			}

			RenderDevice((i / 2) * 96.f + (i & 1) * 48.f + 152.f, 280.f, float(bytestotal) - float(bytesfree), float(bytestotal), 0.89f, 0.5f, alpha,
						 (mState == 1) && (mCursor == i), FALSE, formatted, present);
		}

		PLATFORM.Font(FONT_SMALL)->GetTextExtent(FET.GetText(FEX_DS_AVAILABLE_BLOCKS_XBOX), &size);
		PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(((410.f - size.cx) - 10.f) + 100.f, 340.f, BlendAlpha2(0xff7f7f00, alpha), FET.GetText(FEX_DS_AVAILABLE_BLOCKS_XBOX), 0, 0.79f, 1.f, 1.f);

		DWORD	col = BlendAlpha2(0xff7f7f7f, alpha);
		if (mState == 0)
		{
			if (MEMORYCARD.IsHDDAvailable())
			{
				MEMORYCARD.GetCardInfo(-1, &present, &formatted, &bytesfree, &bytestotal);
				bytesfree -= 2048;
				if (bytesfree < 0)
					bytesfree = 0;

				MEMORYCARD.MakeHumanReadableSize(bytesfree, freetext);
				MEMORYCARD.MakeHumanReadableSize(bytestotal, totaltext);

				PLATFORM.Font(FONT_NORMAL)->DrawTextWithShadow(92.f, 340.f, col, FET.GetText(FEX_DS_XBOXHARDDISK), 0, 0.79f, 1.f, 1.f);
				PLATFORM.Font(FONT_SMALL)->DrawTextWithShadow(510.f, 340.f, col, freetext, 0, 0.79f, 1.f, 1.f);
			}
		}
		else
		{
			MEMORYCARD.GetCardInfo(mCard, &present, &formatted, &bytesfree, &bytestotal);
			bytesfree -= 2048;
			if (bytesfree < 0)
				bytesfree = 0;

			if (present && formatted)
			{
				MEMORYCARD.GetCardName(mCard, name);
				MEMORYCARD.MakeHumanReadableSize(bytesfree, freetext);
				MEMORYCARD.MakeHumanReadableSize(bytestotal, totaltext);
				RENDERINFO.SetFogEnabled(false);
			}
		}

		// no device at all: the message, drawn once
		if ((mCard == -2) && (!FEMESSBOX.BeingDisplayed()) && (transition == 1.f))
		{
			FEMESSBOX.mUnknown0000 = FALSE;
			FEMESSBOX.Create(320.f, 340.f, 450.f, 0.1f, FET.GetText(FEX_NOMEMORYCARD_XBOX), PLATFORM.Font(FONT_SMALL), 0, 0, FALSE, FEMB_OPTION_NONE, 0);
			FEMESSBOX.Render();
			FEMESSBOX.mUnknown0000 = TRUE;
			FEMESSBOX.Kill();
		}

		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_WRAP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_WRAP);
		RENDERINFO.Apply();
	}

	if ((mCard != -2) || (FEMESSBOX.BeingDisplayed()))
		FRONTEND.GetCommonPage()->RenderHelpText(5, transition);
	if (FRONTEND.GetSuccessPage() == FEP_DEBRIEFING)
		FRONTEND.GetCommonPage()->RenderHelpText(3, transition);
	else
		FRONTEND.GetCommonPage()->RenderHelpText(FRONTEND.GetSaveMode() ? 2 : 1, transition);

	float	tr = (transition - 0.75f) * 4.f;
	Clamp(tr, 0.f, 1.f);
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(tr);

	EFETextHack	title, help;
	if (FRONTEND.GetSaveMode())
	{
		title = CAREER.InProgress() ? FEX_SAVEGAME2 : FEX_SAVENEWGAME2;
		help = FEX_SELECTSAVLOC;
	}
	else
	{
		title = FEX_LOADGAME2;
		help = FEX_SELECTLOADLOC;
	}
	FRONTEND.DrawTitleBar(FET.GetText(title), transition, dest);

	SINT	ta = MakeAlpha(tr);
	PLATFORM.Font(FONT_SMALL)->GetTextExtent(FET.GetText(help), &size);
	DWORD	hcol = 0xff3f3f5f;
	PLATFORM.Font(FONT_SMALL)->DrawTextDynamic(395.f - size.cx * 0.5f, 100.f, 0.01f, 1.f, 1.f, BlendAlpha2(hcol, ta), FET.GetText(help), 1000.f, 0, 0);
}

//*********************************************************************************
void	CFEPDevSelect::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x00459580
{
	CheckDevice(FALSE);
	mStartTime = PLATFORM.GetSysTimeFloat() + 2.f;
}

//*********************************************************************************
// The selected device's colour has its alpha scaled as ScaleAlpha does (masked, then shifted 16) and or-ed with the
// colour (004595b0+0xb4). VC6 turns the * 0x10000 into a shift plus a copy (C2 0x107290aa), and the next pass
// (0x1072930f) folds that copy into an add only: with | (and ^ or -) the chain stays a one-block temporary, which the
// chooser gives the register of the value it copies (col, eax) or else the next scratch register (ecx, `and cl,0`), and
// then moves into esi, while retail builds it in esi. An explicit shift (<< 16, (x >> 8) << 24, a bitfield, signed
// forms) is folded into the multiply and merged with xor/and/xor instead. No | spelling, constant form, cast, inline
// or by-reference helper or optimisation flag builds it in place. FEMessBox::Render 0044d6f0 or-s ScaleAlpha's parts too.
// STAND-IN: the colour's alpha is read through `(SINT)col < 0 ? (col >> 24) : (col >> 24)`, two
// identical arms. At allocation the chain then spans three blocks (the two arms and the join), so it is a candidate
// (priority 8 + 8 + 50 = 66) instead of a chooser temporary: it is coloured after the merged colour (87, esi), which
// leaves it an esi preference, and before col's first value (62, eax), so it takes esi. The sign test branches on the
// flags of the add that computes col (`add eax,ecx; mov esi,eax; jns` when the arms differ), so it adds no compare,
// and C2 merges the identical arms after allocation (0x10758522), which removes the branch. A zero test (`col ? `)
// lets the optimizer fold col >> 24 to 0 in that arm, so the arms differ; other conditions leave a dead compare
// (formatted: `test ebx,ebx`; v: `test ecx,ecx`); `selected`, also re-read through its address, changes nothing. Plain form
// (`| (col & 0x00ffffff)` without the arms): 335 differing bytes (the chain in ecx, two bytes longer); with `+`
// instead of `|`, one (add for or). Which source Stuart wrote is unknown.
// The green part is v / 2 placed at bit 8, written as (v & ~1) * 128: retail masks with ~1 (`and al,0xfe`) before
// the shift by 7. VC6 narrows a mask to the bits a shift keeps (0x1fffffe) when the mask comes from a shift pair
// ((v / 2) << 8, (v >> 1) << 8) or from a user mask before a shift ((v & ~1) << 7), but keeps it before a multiply;
// ((v >> 1) << 1) * 128 gives the same code, and (v / 2) * 256 keeps the shr and a shift by 8.
// FUNCTION: 0x004595b0
void	CFEPDevSelect::RenderDevice(float x, float y, float used, float total, float z, float scale, SINT alpha, BOOL selected,
									BOOL hdd, BOOL formatted, BOOL present)
{
	CTEXTURE	*tex = FED.GetTexture(hdd ? FET_XB_XBOX : (formatted ? FET_XB_MU : FET_XB_MU_CORRUPT));
	if (!tex)
		return;

	float	w = tex->mWidth * scale;
	float	h = tex->mHeight * scale;

	DWORD	col;
	if (selected)
	{
		DWORD	v = 0xbf - SINT(sinf((FRONTEND.GetCounter() * 3.14f) * (1.f / 30.f)) * -64.f);
		col = 0x6f000000 + ((v & ~1) * 128) + v;
		col = (((((SINT)col < 0 ? (col >> 24) : (col >> 24)) * alpha) & 0xffffff00) * 0x10000) | (col & 0x00ffffff);
	}
	else
		col = BlendAlpha2(0x6f000000, alpha);

	// red when the save will not fit
	SINT	savesize;
	MEMORYCARD.GetSaveSize(CCareer::SizeOfSaveGame(), &savesize);
	if (((total - used) < savesize) && FRONTEND.GetSaveMode() && formatted)
		col = (col & 0xff000000) + ((col & 0xff) << 16);

	float	bottom = y + h;
	FRONTEND.DrawPanel(x, y, x + w, bottom + 16.f, z, col);
	FRONTEND.DrawBox(x, y, x + w, bottom + 16.f, BlendAlpha2(0xffffffff, alpha), 2.f, z);

	if (present)
	{
		CSPRITERENDERER::DrawColouredSprite(x, y, 0.79f, tex, BlendAlpha2(0xffffffff, alpha), scale, scale, SA_TOP_LEFT, SPT_NONE, 1.f, 0.f);

		if (total != 0.f)
			FRONTEND.DrawBarGraph(x + w * 0.05f, bottom + 4.f, x + w * 0.95f, bottom + 12.f, used, total, z, BlendAlpha2(0xffffffff, alpha), BlendAlpha2(0xff004000, alpha), BlendAlpha2(0xffff0000, alpha));
	}
}

//*********************************************************************************
void	CFEPDevSelect::SetCurrentCard(int card)	// FUNCTION: 0x00459810
{
	mState = 1;
	mCard = card;
	mCursor = card;
}
