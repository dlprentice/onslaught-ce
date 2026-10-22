// FEPVirtualKeyboard.cpp: the name-entry keyboard page (reconstructed from the retail PC executable; Stuart's file is
// not in his release). RenderPreCommon folds with CFEPDirectory's (0051ae50).

#include	"Common.h"

#include	"FEPVirtualKeyboard.h"
#include	"Frontend.h"
#include	"MemoryCard.h"
#include	"ltshell.h"
#include	"renderinfo.h"
#include	<math.h>
#include	<string.h>

#define VK_MIN(a, b) (((a) < (b)) ? (a) : (b))

// The name box's edges; the name may not be wider than the space between them.
float	gVKLeft = 128.f;						// 0063fd1c
float	gVKRight = 530.f;						// 0063fd20
float	gVKTextWidth = gVKRight - gVKLeft - 4.f - 4.f - 10.f;	// 0089bcb8
// The per-file maths constants from maths.h come first:
// FUNCTION: 0x0051fd90 init:ZERO_FVECTOR
// FUNCTION: 0x0051fdb0 init:ID_FMATRIX
// FUNCTION: 0x0051fe80 init:gVKTextWidth
float	gVKTop = 408.f;							// 0063fd24
float	gVKBottom = 448.f;						// 0063fd28
float	gVKUnknown = 68.f;						// 0063fd2c (no reader found)
float	gVKTextY = 414.f;						// 0063fd30

// the cursor blinks: on for the first half of each second (CFEPVirtualKeyboard::DrawPanel 005213fb..00521410)
extern const float	VK_BLINK_PERIOD = 1.f;		// 005e4a2c
extern const float	VK_BLINK_ON = 0.5f;			// 005e4a30

//*********************************************************************************
// The PC's keyboard while the page is up: arrows, return, backspace and escape, and typed characters.
bool	VKKeyTrap(WORD key, KeyEventType event)	// FUNCTION: 0x0051feb0
{
	if (event == KEY_EVENT_DOWN)
	{
		if ((key == DIK_UP) || (key == DIK_LEFT))
			FRONTEND.mFEPVirtualKeyboard.HandleKey(VKT_LEFT);
		else if ((key == DIK_DOWN) || (key == DIK_RIGHT))
			FRONTEND.mFEPVirtualKeyboard.HandleKey(VKT_RIGHT);
		else if (key == DIK_ESCAPE)
			((CFrontEndPage *)&FRONTEND.mFEPVirtualKeyboard)->ButtonPressed(BUTTON_FRONTEND_MENU_BACK, 1.f);
		else if ((key == DIK_RETURN) || (key == DIK_NUMPADENTER))
		{
			FRONTEND.PlaySound(FES_SELECT);
			FRONTEND.mFEPVirtualKeyboard.HandleKey(VKT_DONE);
		}
		else if (key == DIK_BACK)
			FRONTEND.mFEPVirtualKeyboard.HandleKey(VKT_BACKSPACE);
	}
	else if (event == KEY_EVENT_CHAR)
	{
		if (PLATFORM.Font(FONT_NORMAL)->CanDisplay(key))
			FRONTEND.mFEPVirtualKeyboard.HandleKey(key);
	}

	return false;
}

//*********************************************************************************
BOOL	CFEPVirtualKeyboard::Init()	// FUNCTION: 0x0051ff90
{
	m_fColFraction = 0.5f;
	m_bCapsLock = FALSE;
	m_bShift = FALSE;
	m_iPos = 0;
	m_iMode = 0;
	m_iRow = 0;
	m_iCol = 0;
	m_iUnknown6f0 = 0;

	InitKeyboardLayout();

	return TRUE;
}

//*********************************************************************************
void	CFEPVirtualKeyboard::Shutdown()	// FUNCTION: 0x0051ffd0
{
	if (LT.GetKeytrap() == (pKeyTrapper)VKKeyTrap)
		LT.SetKeytrap(NULL);
}

//*********************************************************************************
inline void	CFEPVirtualKeyboard::MakeDefaultName()
{
	char	buf[20];

	wcscpy(m_strData, ToWCHAR("BEA"));
	sprintf(buf, " %d", 1);
	wcscat(m_strData, ToWCHAR(buf));

	for (int n = 2; n <= 0x1000; n++)
	{
		int		card = FRONTEND.GetMemoryCardNumber();
		int		num;

		if (MEMORYCARD.GetNumSaves(card, &num) != MCE_SUCCESS)
			break;

		BOOL	clash = FALSE;

		for (int i = 0; i < num; i++)
		{
			WCHAR	name[100];

			if ((MEMORYCARD.GetSaveName(card, i, name) == MCE_SUCCESS) && (!wcscmp(name, m_strData)))
			{
				clash = TRUE;
				break;
			}
		}

		if (!clash)
			break;

		wcscpy(m_strData, ToWCHAR("BEA"));
		sprintf(buf, " %d", n);
		wcscat(m_strData, ToWCHAR(buf));
	}

	m_iPos = VK_MIN(wcslen(m_strData), 31);
	m_Fresh = true;
}

void	CFEPVirtualKeyboard::SetDefaultName()	// FUNCTION: 0x0051fff0
{
	MakeDefaultName();
}

//*********************************************************************************
void	CFEPVirtualKeyboard::TransitionNotification(EFrontEndPage from)	// FUNCTION: 0x00520130
{
	if ((from == FEP_DEVSELECT) || (from == FEP_DIRECTORY) || (from == FEP_MAIN))
	{
		if (m_Fresh)
			MakeDefaultName();

		m_iMode = 0;
		m_iRow = 0;
		m_iCol = 0;
		m_fColFraction = 0.5f;

		m_iPos = VK_MIN(wcslen(m_strData), 31);
	}
}

//*********************************************************************************
void	CFEPVirtualKeyboard::Process(EFEPState state)	// FUNCTION: 0x005202d0
{
	if (state != FEPS_INACTIVE)
		FRONTEND.mFEPDirectory.RefreshSaveFileList(state);

	if (state == FEPS_ACTIVE)
	{
		if (LT.GetKeytrap() != (pKeyTrapper)VKKeyTrap)
			LT.SetKeytrap((pKeyTrapper)VKKeyTrap);

		if (!FEMESSBOX.BeingDisplayed())
		{
			BOOL	present, formatted;

			MEMORYCARD.Update();
			MEMORYCARD.GetCardInfo(FRONTEND.GetMemoryCardNumber(), &present, &formatted, NULL, NULL);
			if (!present)
				CFEPSaveGame::RemovedMUWhinge(FEX_REMOVEDSAVE);
		}
	}
	else
	{
		if (LT.GetKeytrap() == (pKeyTrapper)VKKeyTrap)
			LT.SetKeytrap(NULL);
	}
}

//*********************************************************************************
void	CFEPVirtualKeyboard::ButtonPressed(SINT button, float val)	// FUNCTION: 0x00520370
{
	switch (button)
	{
	case BUTTON_FRONTEND_MENU_SELECT:
		FRONTEND.PlaySound(FES_SELECT);
		HandleKey(m_Keys[m_iMode][m_iRow][m_iCol].mToken);
		break;

	case BUTTON_FRONTEND_MENU_BACK:
		FRONTEND.PlaySound(FES_BACK);
		FRONTEND.SetPage(FEP_MAIN, 70);
		break;

	case BUTTON_FRONTEND_MENU_UP:
		MoveToRow((m_iRow == 0) ? 4 : m_iRow - 1);
		break;

	case BUTTON_FRONTEND_MENU_DOWN:
		MoveToRow((m_iRow == 4) ? 0 : m_iRow + 1);
		break;

	case BUTTON_FRONTEND_MENU_LEFT:
		m_fColFraction = 0.5f;
		do
		{
			if (m_iCol == 0)
			{
				while (m_Keys[m_iMode][m_iRow][m_iCol + 1].mToken)
					m_iCol++;
			}
			else
				m_iCol--;
		} while (IsKeyDisabled());
		break;

	case BUTTON_FRONTEND_MENU_RIGHT:
		m_fColFraction = 0.5f;
		do
		{
			if (m_Keys[m_iMode][m_iRow][m_iCol + 1].mToken == 0)
				m_iCol = 0;
			else
				m_iCol++;
		} while (IsKeyDisabled());
		break;
	};
}

//*********************************************************************************
// STAND-IN (spelling): one key owns its token and width. This reconstructed inline setter preserves both stores
// and reproduces retail InitKeyboardLayout; it has no standalone retail function identity. Written as the two stores at
// each key, InitKeyboardLayout is 61 bytes off.
static inline void SetKeyboardKey(SVKKey &key, WORD token, DWORD width)
{
	key.mToken = token;
	key.mWidth = width;
}

void	CFEPVirtualKeyboard::InitKeyboardLayout()	// FUNCTION: 0x00520530
{
	m_iMode = 0;
	m_bCapsLock = 0;
	m_bShift = 0;
	m_iPos = 0;
	m_iRow = 1;
	m_iCol = 1;
	m_iUnknown6f0 = 1;
	SetKeyboardKey(m_Keys[0][0][0], 0x09, 166);
	SetKeyboardKey(m_Keys[0][0][1], '1', 34);
	SetKeyboardKey(m_Keys[0][0][2], '2', 34);
	SetKeyboardKey(m_Keys[0][0][3], '3', 34);
	SetKeyboardKey(m_Keys[0][0][4], '4', 34);
	SetKeyboardKey(m_Keys[0][0][5], '5', 34);
	SetKeyboardKey(m_Keys[0][0][6], '6', 34);
	SetKeyboardKey(m_Keys[0][0][7], '7', 34);
	SetKeyboardKey(m_Keys[0][0][8], '8', 34);
	SetKeyboardKey(m_Keys[0][0][9], '9', 34);
	SetKeyboardKey(m_Keys[0][0][10], '0', 34);
	SetKeyboardKey(m_Keys[0][1][0], 0x04, 166);
	SetKeyboardKey(m_Keys[0][1][1], 'A', 34);
	SetKeyboardKey(m_Keys[0][1][2], 'B', 34);
	SetKeyboardKey(m_Keys[0][1][3], 'C', 34);
	SetKeyboardKey(m_Keys[0][1][4], 'D', 34);
	SetKeyboardKey(m_Keys[0][1][5], 'E', 34);
	SetKeyboardKey(m_Keys[0][1][6], 'F', 34);
	SetKeyboardKey(m_Keys[0][1][7], 'G', 34);
	SetKeyboardKey(m_Keys[0][1][8], 'H', 34);
	SetKeyboardKey(m_Keys[0][1][9], 'I', 34);
	SetKeyboardKey(m_Keys[0][1][10], 'J', 34);
	SetKeyboardKey(m_Keys[0][2][0], 0x05, 166);
	SetKeyboardKey(m_Keys[0][2][1], 'K', 34);
	SetKeyboardKey(m_Keys[0][2][2], 'L', 34);
	SetKeyboardKey(m_Keys[0][2][3], 'M', 34);
	SetKeyboardKey(m_Keys[0][2][4], 'N', 34);
	SetKeyboardKey(m_Keys[0][2][5], 'O', 34);
	SetKeyboardKey(m_Keys[0][2][6], 'P', 34);
	SetKeyboardKey(m_Keys[0][2][7], 'Q', 34);
	SetKeyboardKey(m_Keys[0][2][8], 'R', 34);
	SetKeyboardKey(m_Keys[0][2][9], 'S', 34);
	SetKeyboardKey(m_Keys[0][2][10], 'T', 34);
	SetKeyboardKey(m_Keys[0][3][0], 0x07, 166);
	SetKeyboardKey(m_Keys[0][3][1], 'U', 34);
	SetKeyboardKey(m_Keys[0][3][2], 'V', 34);
	SetKeyboardKey(m_Keys[0][3][3], 'W', 34);
	SetKeyboardKey(m_Keys[0][3][4], 'X', 34);
	SetKeyboardKey(m_Keys[0][3][5], 'Y', 34);
	SetKeyboardKey(m_Keys[0][3][6], 'Z', 34);
	SetKeyboardKey(m_Keys[0][3][7], 0x03, 136);
	SetKeyboardKey(m_Keys[0][4][0], 0x08, 166);
	SetKeyboardKey(m_Keys[0][4][1], ' ', 204);
	SetKeyboardKey(m_Keys[0][4][2], 0x01, 68);
	SetKeyboardKey(m_Keys[0][4][3], 0x02, 68);
	SetKeyboardKey(m_Keys[1][0][0], 0x09, 166);
	SetKeyboardKey(m_Keys[1][0][1], '!', 34);
	SetKeyboardKey(m_Keys[1][0][2], '"', 34);
	SetKeyboardKey(m_Keys[1][0][3], '#', 34);
	SetKeyboardKey(m_Keys[1][0][4], '%', 34);
	SetKeyboardKey(m_Keys[1][0][5], '&', 34);
	SetKeyboardKey(m_Keys[1][0][6], 0x27, 34);
	SetKeyboardKey(m_Keys[1][0][7], '(', 34);
	SetKeyboardKey(m_Keys[1][0][8], ')', 34);
	SetKeyboardKey(m_Keys[1][0][9], '*', 34);
	SetKeyboardKey(m_Keys[1][0][10], '+', 34);
	SetKeyboardKey(m_Keys[1][1][0], 0x04, 166);
	SetKeyboardKey(m_Keys[1][1][1], ',', 34);
	SetKeyboardKey(m_Keys[1][1][2], '-', 34);
	SetKeyboardKey(m_Keys[1][1][3], '.', 34);
	SetKeyboardKey(m_Keys[1][1][4], '/', 34);
	SetKeyboardKey(m_Keys[1][1][5], ':', 34);
	SetKeyboardKey(m_Keys[1][1][6], ';', 34);
	SetKeyboardKey(m_Keys[1][1][7], '<', 34);
	SetKeyboardKey(m_Keys[1][1][8], '=', 34);
	SetKeyboardKey(m_Keys[1][1][9], '>', 34);
	SetKeyboardKey(m_Keys[1][1][10], '?', 34);
	SetKeyboardKey(m_Keys[1][2][0], 0x05, 166);
	SetKeyboardKey(m_Keys[1][2][1], 0xbf, 34);
	SetKeyboardKey(m_Keys[1][2][2], 0xa1, 34);
	SetKeyboardKey(m_Keys[1][2][3], 0xa9, 34);
	SetKeyboardKey(m_Keys[1][2][4], 0xae, 34);
	SetKeyboardKey(m_Keys[1][2][5], 0x2122, 34);
	SetKeyboardKey(m_Keys[1][2][6], 0xab, 34);
	SetKeyboardKey(m_Keys[1][2][7], 0xbb, 34);
	SetKeyboardKey(m_Keys[1][2][8], 0xba, 34);
	SetKeyboardKey(m_Keys[1][2][9], 0xaa, 34);
	SetKeyboardKey(m_Keys[1][3][0], 0x06, 166);
	SetKeyboardKey(m_Keys[1][3][1], 0x03, 136);
	SetKeyboardKey(m_Keys[1][4][0], 0x08, 166);
	SetKeyboardKey(m_Keys[1][4][1], ' ', 204);
	SetKeyboardKey(m_Keys[1][4][2], 0x01, 68);
	SetKeyboardKey(m_Keys[1][4][3], 0x02, 68);
	SetKeyboardKey(m_Keys[2][0][0], 0x09, 166);
	SetKeyboardKey(m_Keys[2][0][1], '1', 34);
	SetKeyboardKey(m_Keys[2][0][2], '2', 34);
	SetKeyboardKey(m_Keys[2][0][3], '3', 34);
	SetKeyboardKey(m_Keys[2][0][4], '4', 34);
	SetKeyboardKey(m_Keys[2][0][5], '5', 34);
	SetKeyboardKey(m_Keys[2][0][6], '6', 34);
	SetKeyboardKey(m_Keys[2][0][7], '7', 34);
	SetKeyboardKey(m_Keys[2][0][8], '8', 34);
	SetKeyboardKey(m_Keys[2][0][9], '9', 34);
	SetKeyboardKey(m_Keys[2][0][10], '0', 34);
	SetKeyboardKey(m_Keys[2][1][0], 0x04, 166);
	SetKeyboardKey(m_Keys[2][1][1], 0xe1, 34);
	SetKeyboardKey(m_Keys[2][1][2], 0xe0, 34);
	SetKeyboardKey(m_Keys[2][1][3], 0xe2, 34);
	SetKeyboardKey(m_Keys[2][1][4], 0xe4, 34);
	SetKeyboardKey(m_Keys[2][1][5], 0xe7, 34);
	SetKeyboardKey(m_Keys[2][1][7], 0xe9, 34);
	SetKeyboardKey(m_Keys[2][1][6], 0xe8, 34);
	SetKeyboardKey(m_Keys[2][1][8], 0xea, 34);
	SetKeyboardKey(m_Keys[2][1][9], 0xeb, 34);
	SetKeyboardKey(m_Keys[2][1][10], 0xed, 34);
	SetKeyboardKey(m_Keys[2][2][0], 0x05, 166);
	SetKeyboardKey(m_Keys[2][2][1], 0xec, 34);
	SetKeyboardKey(m_Keys[2][2][2], 0xee, 34);
	SetKeyboardKey(m_Keys[2][2][3], 0xef, 34);
	SetKeyboardKey(m_Keys[2][2][4], 0xf3, 34);
	SetKeyboardKey(m_Keys[2][2][5], 0xf2, 34);
	SetKeyboardKey(m_Keys[2][2][6], 0xf4, 34);
	SetKeyboardKey(m_Keys[2][2][7], 0xf6, 34);
	SetKeyboardKey(m_Keys[2][2][8], 0x153, 34);
	SetKeyboardKey(m_Keys[2][2][9], 0xf1, 34);
	SetKeyboardKey(m_Keys[2][2][10], 0xfa, 34);
	SetKeyboardKey(m_Keys[2][3][0], 0x06, 166);
	SetKeyboardKey(m_Keys[2][3][1], 0xf9, 34);
	SetKeyboardKey(m_Keys[2][3][2], 0xfb, 34);
	SetKeyboardKey(m_Keys[2][3][3], 0xfc, 34);
	SetKeyboardKey(m_Keys[2][3][4], 0xdf, 34);
	SetKeyboardKey(m_Keys[2][3][5], 0x03, 136);
	SetKeyboardKey(m_Keys[2][4][0], 0x07, 166);
	SetKeyboardKey(m_Keys[2][4][1], ' ', 204);
	SetKeyboardKey(m_Keys[2][4][2], 0x01, 68);
	SetKeyboardKey(m_Keys[2][4][3], 0x02, 68);
}

//*********************************************************************************
void	CFEPVirtualKeyboard::HandleKey(WCHAR key)	// FUNCTION: 0x00520cc0
{
	if (key >= 0x20)
	{
		if (wcslen(m_strData) >= 31)
			return;

		if (m_Fresh)
		{
			m_strData[0] = 0;
			m_iPos = 0;
			m_Fresh = false;
		}

		if (wcslen(m_strData) < 32)
		{
			SIZE	size;

			PLATFORM.Font(FONT_NORMAL)->GetTextExtent(m_strData, &size);
			if (float(size.cx) < gVKTextWidth)
			{
				int		pos = m_iPos;
				WCHAR	ch = key;

				// the font lacks some accented capitals: use the plain letter
				if (!PLATFORM.Font(FONT_NORMAL)->CanDisplay(key))
				{
					switch (key)
					{
					case 0xca: case 0xcb:	ch = 'E'; break;
					case 0xce: case 0xcf:	ch = 'I'; break;
					case 0xd4:				ch = 'O'; break;
					case 0xdb:				ch = 'U'; break;
					};
				}

				// insert it at the cursor
				WCHAR	*p = &m_strData[pos];
				WCHAR	c;

				do
				{
					c = *p;
					*p++ = ch;
					ch = c;
				} while (p[-1]);

				m_iPos++;
			}
		}

		m_bShift = FALSE;
		return;
	}

	switch (key)
	{
	case VKT_BACKSPACE:
		if (m_Fresh)
		{
			m_strData[0] = 0;
			m_iPos = 0;
			m_Fresh = false;
			break;
		}
		if (m_iPos > 0)
		{
			m_iPos--;
			WCHAR	*p = &m_strData[m_iPos];
			if (*p)
			{
				do
				{
					p[0] = p[1];
					p++;
				} while (*p);
			}
		}
		break;

	case VKT_SHIFT:
		m_bShift = !m_bShift;
		break;

	case VKT_CAPSLOCK:
		m_bCapsLock = !m_bCapsLock;
		break;

	case VKT_ALPHABET:
		m_iMode = 0;
		break;

	case VKT_SYMBOLS:
		m_iMode = 1;
		break;

	case VKT_ACCENTS:
		m_iMode = 2;
		break;

	case VKT_LEFT:
		if (m_Fresh)
			m_Fresh = false;
		if (m_iPos > 0)
			m_iPos--;
		break;

	case VKT_RIGHT:
		if (m_Fresh)
			m_Fresh = false;
		if ((m_iPos < int(wcslen(m_strData))) && (m_iPos < 31))
			m_iPos++;
		break;

	case VKT_DONE:
		wcscpy(FRONTEND.mFEPSaveGame.mSaveGameName, m_strData);
		FRONTEND.SetSaveMode(TRUE);
		FRONTEND.SetAutoSave(CFrontEnd::AUTO_SAVE_NOT);
		FRONTEND.SetPage(FEP_SAVEGAME, 20);
		break;
	};
}

//*********************************************************************************
void	CFEPVirtualKeyboard::MoveToRow(int row)	// FUNCTION: 0x00520f70
{
	for (;;)
	{
		int		oldrow = m_iRow;
		float	x = 0;
		int		c;

		for (c = 0; c < m_iCol; c++)
			x += float(m_Keys[m_iMode][m_iRow][c].mWidth);

		float	target = x + float(m_Keys[m_iMode][m_iRow][c].mWidth) * m_fColFraction;
		float	left = 0;

		m_iRow = row;
		m_iCol = 0;

		bool	found = false;
		while (m_Keys[m_iMode][m_iRow][m_iCol].mToken)
		{
			float	width = float(m_Keys[m_iMode][m_iRow][m_iCol].mWidth);
			float	right = left + width;

			if (target <= right)
			{
				m_fColFraction = (target - left) / width;
				found = true;
				break;
			}

			left = right;
			m_iCol++;
		}

		if (!found)
		{
			m_fColFraction = 0.5f;
			m_iCol--;
		}

		if (!IsKeyDisabled())
			return;

		int		dir = m_iRow - oldrow;

		// STAND-IN (spelling): the two self-copy arms are retail's; `(dir != -1) && (dir != 1)` is 91 bytes off
		if (dir == -1)
			dir = -1;
		else if (dir == 1)
			dir = 1;
		else
			dir = (dir < -1) ? 1 : -1;

		// STAND-IN (registers): the next row is worked out apart and copied to row at the end (00520fd7..005210ee: retail keeps
		// it in eax and moves it to ebx on each path); worked out in row itself the function is 47 bytes off
		int		newrow = m_iRow + dir;
		if (newrow < 0)
			newrow = 4;
		else if (newrow >= 5)
			newrow = 0;
		row = newrow;
	}
}

//*********************************************************************************
void	CFEPVirtualKeyboard::RenderPreCommon(float transition, EFrontEndPage dest)
{
	FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
}

//*********************************************************************************
// FUNCTION: 0x00521100
void	CFEPVirtualKeyboard::Render(float transition, EFrontEndPage dest)
{
	SINT	alpha = MakeAlpha(RangeTransition(transition, 0.5f, 1.f));

	WCHAR	*name = FRONTEND.mFEPDirectory.RenderSaveFileList(transition, (dest == FEP_VIRTUAL_KEYBOARD) ? FEP_DIRECTORY : dest);
	if (name)
	{
		wcsncpy(m_strData, name, 31);
		m_iPos = wcslen(m_strData);
		m_Fresh = true;
		FRONTEND.PlaySound(FES_SELECT);
	}

	FRONTEND.GetCommonPage()->RenderHelpText(5, transition);
	DrawPanel(gVKTextY, transition, alpha);
	FRONTEND.DrawTitleBar(FET.GetText(FRONTEND.GetSaveMode() ? FEX_CHOOSEGAMENAME2 : FEX_CHOOSEDELETE), transition, dest);
	transition = RangeTransition(transition, 0.75f, 1.f);
	FRONTEND.GetCommonPage()->RenderSmallBEALogo(transition);
}

//*********************************************************************************
// FUNCTION: 0x00521260
void	CFEPVirtualKeyboard::DrawPanel(float y, float transition, SINT alpha)
{
	RENDERINFO.SetFogEnabled(false);

	FRONTEND.DrawPanel(gVKLeft, gVKTop, gVKRight, gVKBottom + 3.f, 0.13f, BlendAlpha2(0xa0000000, alpha));
	FRONTEND.DrawBox(gVKLeft, gVKTop, gVKRight, gVKBottom + 3.f, BlendAlpha2(0xa0ffffff, alpha), 2.f, 0.13f);

	SIZE	size;
	PLATFORM.Font(FONT_NORMAL)->GetTextExtent(m_strData, &size);
	float	x = (gVKRight - gVKLeft) * 0.5f + gVKLeft - (size.cx >> 1);

	if (m_Fresh)
		FRONTEND.DrawPanel(x - 2.f, y, x + size.cx + 2.f, y + size.cy, 0.13f, BlendAlpha2(0xff0080a0, alpha));
	PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(x, y, 0.13f, 1.f, 1.f, BlendAlpha2(0xffffffff, alpha), m_strData, 1000.f, 0, 0);

	if ((!m_Fresh) && (fmod(PLATFORM.GetSysTimeFloat(), VK_BLINK_PERIOD) < VK_BLINK_ON))
	{
		WCHAR	cursor[2];
		WCHAR	left[32];
		SIZE	lsize;

		cursor[0] = 'I';
		cursor[1] = 0;
		SINT	i;
		for (i = 0; i < m_iPos; i++)
			left[i] = m_strData[i];
		left[i] = 0;
		PLATFORM.Font(FONT_NORMAL)->GetTextExtent(left, &lsize);
		PLATFORM.Font(FONT_NORMAL)->DrawTextDynamic(x + lsize.cx + 1.f, gVKTextY, 0.13f, 1.f, 1.f, BlendAlpha2(0xff0080a0, alpha), cursor, 1000.f, 0, 0);
	}
}

//*********************************************************************************
BOOL	CFEPVirtualKeyboard::IsKeyDisabled()	// FUNCTION: 0x005214d0
{
	SVKKey	*key = &m_Keys[m_iMode][m_iRow][m_iCol];

	if ((m_iMode == 1) && ((key->mToken == VKT_SHIFT) || (key->mToken == VKT_CAPSLOCK)))
		return TRUE;

	if ((key->mToken == VKT_DONE) && (m_strData[0] == 0))
		return TRUE;

	if (key->mToken == VKT_DONE)
	{
		for (WCHAR *p = m_strData; *p; p++)
		{
			if (*p != ' ')
				return FALSE;
		}
		return TRUE;
	}

	return FALSE;
}

// The maths pair after this object (00521560/00521580) is FEPWingmen.obj's: CFEPWingmen::Init copies the
// ID_FMATRIX that 00521580 initializes (0089bd40).
