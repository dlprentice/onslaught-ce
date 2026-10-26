// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_frontend.cpp: the CE layer's front-end additions (patch 0050): an "Onslaught CE" action on the main menu
// opening a page of the game's own options-style list (About, Special Thanks, Check for updates, Record sessions, Report a problem,
// Back), and a Cheats page in Options whose switches (ce.ini, patch 0051 at IsCheatActive) work as the cheat codes do in
// a career's name. Built as a game file (port/build.py) so it uses the game's menu classes; public builds only.
//
// None of it exists while the CE menus are off: in replays, recordings and the test harness (PortCeLayer, ce.cpp), under
// -autoconfigtest and in the playable demo, the main menu has its seven rows, the language-test page keeps its slot and
// the options page its four lists, so a tape navigates exactly the menus retail had.
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "FEPOptions.h"
#include "PauseMenu.h"
#include "MenuItem.h"
#include "MenuOptionsList.h"
#include "FEMessBox.h"
#include "Career.h"
#include "CLIParams.h"
#include "ltshell.h"
#include "renderinfo.h"
#include "text.h"
#include "TransitionHelpers.h"
#include "data/MissionScripts/text/text.stf"
#include "../platform/common/portlog.h"

#include <stdio.h>
#include <string.h>

#define MEMTYPE_MENUITEM	((EMemoryType)0x80)		// the menu items' memory type (MenuItem.cpp, PauseMenu.cpp)
#define PORT_CE_CHEATS_PCTEXT	0x4345				// the options list's Cheats item (patch 0050, PauseMenu.cpp)
#define PORT_CE_ACTION		9						// the main menu's CE action (patch 0050, FEPMain.cpp)
#define FEP_CE				FEP_LANGUAGE_TEST		// the CE page's slot

bool			PortCeLayer();						// port/platform/common/ce.cpp
extern "C" int	CE_OpenReportPage(void);
extern "C" int	CE_SettingGet(const char *key, int def);
extern "C" int	CE_SettingSet(const char *key, int value);
extern "C" int	CE_CheatOn(int n);
extern "C" int	CE_CheatSet(int n, int on);
extern "C" int	CE_UserDirForDialog(char *out, int n, int (*shown)(int));
extern "C" const char *CE_BuildName(void);

#define CE_LINKS	"github.com/dlprentice/onslaught-ce"

//*********************************************************************************
bool	PortCeMenus()
{
	static int on = -1;
	if (on < 0)
		on = (PortCeLayer() && !CLIPARAMS.mAutoConfigTest && !PLAYABLE_DEMO) ? 1 : 0;
	return on == 1;
}

EFrontEndPage	PortCeAsOptions(EFrontEndPage page)
{
	return ((page == FEP_CE) && PortCeMenus()) ? FEP_OPTIONS : page;
}

// The main menu's rows: Onslaught CE goes before Quit (row 6), Quit moves to row 7; other rows are their actions.
int		PortCeMainAction(int row)
{
	if (!PortCeMenus())
		return row;
	if (row == 6)
		return PORT_CE_ACTION;
	if (row == 7)
		return 6;
	return row;
}

int		PortCeMainRows(int rows)
{
	return PortCeMenus() ? rows + 1 : rows;
}

WCHAR	*PortCeMainText()
{
	static WCHAR text[] = L"Onslaught CE";
	return text;
}

//*********************************************************************************
// Text: Latin-1 to the game's 2-byte characters (the fonts have Latin-1's letters).
static void	Widen(WCHAR *out, int n, const char *in)
{
	int i = 0;
	for (; in[i] && i < n - 1; i++)
		out[i] = (WCHAR)(unsigned char)in[i];
	out[i] = 0;
}

// Whether the message box's font has character c (src/Font.cpp: no "~", "\\", "_", "$", "@", "[", "]", "^", "{", "|", "}").
static int	Shown(int c)
{
	return PLATFORM.Font(FONT_SMALL)->CanDisplay((WCHAR)c) ? 1 : 0;
}

// The CE folder in the font's characters (CE_UserDirForDialog, ce.cpp), with sub ("/tapes") after it, or "the sub
// folder in" before a folder given in words; then how to open it: on Linux, a note naming the first hidden folder on the
// way (a name that starts with a dot), on Windows, Explorer's address bar (AppData is hidden there). A path's spaces are
// kNoBreak, so WrapLines never breaks "Onslaught CE" at its space. Returns out.
#define kNoBreak	'\xa0'

static char	*FolderText(char *out, int n, const char *sub)
{
	char dir[1024];
	int path = CE_UserDirForDialog(dir, sizeof(dir), Shown);
	if (path)
		for (char *c = dir; *c; c++)
			if (*c == ' ')
				*c = kNoBreak;
	if (!*sub)
		snprintf(out, n, "%s", dir);
	else if (path)
		snprintf(out, n, "%s/%s", dir, sub);
	else
		snprintf(out, n, "the %s folder in %s", sub, dir);
	size_t l = strlen(out);
#ifndef _WIN32
	const char *dot = path ? strstr(dir, "/.") : NULL;
	if (dot)
	{
		int len = (int)strcspn(dot + 1, "/");
		snprintf(out + l, n - l, "\n(%.*s is a hidden folder: in your file manager, press Ctrl+H to show it.)", len, dot + 1);
	}
#else
	if (path)
		snprintf(out + l, n - l, "\n(To open it, paste this into File Explorer's address bar: AppData is a hidden folder.)");
#endif
	return out;
}

// The width of a line as it will be shown, kNoBreak as a space.
static void	Extent(CDXBitmapFont *font, const WCHAR *line, SIZE *size)
{
	WCHAR shown[100];
	int i = 0;
	for (; line[i] && i < 99; i++)
		shown[i] = (line[i] == (WCHAR)(unsigned char)kNoBreak) ? ' ' : line[i];
	shown[i] = 0;
	font->GetTextExtent(shown, size);
}

// Lays text out in the message box's lines (FEMessBox's 40 of 100 characters) as CDXBitmapFont::WordWrap would, but with
// '\n' starting a new line and a word wider than the box (a long folder path) broken after a '/' or '\', or wherever it
// must, instead of WordWrap's one character a line; kNoBreak (FolderText) joins a word and is shown as a space. Returns
// the number of lines.
static int	WrapLines(SWordWrapFill *fill, const WCHAR *text, CDXBitmapFont *font, float width)
{
	const int	kMaxLines = 40, kMaxChars = 99;
	WCHAR	cur[100], tryline[100];
	int		lines = 0, len = 0;
	SIZE	size;
	cur[0] = 0;
	const WCHAR *p = text;
	while (*p && (lines < kMaxLines))
	{
		if (*p == '\n')
		{
			wcscpy(fill->c[lines++], cur);
			cur[0] = 0;
			len = 0;
			p++;
			continue;
		}
		if (*p == ' ')
		{
			p++;
			continue;
		}
		int	w = 0;
		while (p[w] && (p[w] != ' ') && (p[w] != '\n'))
			w++;
		// the word on this line, after a space
		int	sep = len ? 1 : 0;
		if (len + sep + w <= kMaxChars)
		{
			wcscpy(tryline, cur);
			if (sep)
				tryline[len] = ' ';
			for (int i = 0; i < w; i++)
				tryline[len + sep + i] = p[i];
			tryline[len + sep + w] = 0;
			Extent(font, tryline, &size);
			if (size.cx < width)
			{
				wcscpy(cur, tryline);
				len += sep + w;
				p += w;
				continue;
			}
		}
		if (len)
		{
			// the word starts the next line
			wcscpy(fill->c[lines++], cur);
			cur[0] = 0;
			len = 0;
			continue;
		}
		// the word alone is too wide: the longest part that fits, ending after a separator where one does
		int	fit = 0, cut = 0;
		for (int n = 1; (n <= w) && (n <= kMaxChars); n++)
		{
			wcsncpy(tryline, p, n);
			tryline[n] = 0;
			Extent(font, tryline, &size);
			if (size.cx >= width)
				break;
			fit = n;
			if ((p[n - 1] == '/') || (p[n - 1] == '\\'))
				cut = n;
		}
		int	n = cut ? cut : (fit ? fit : 1);
		wcsncpy(fill->c[lines], p, n);
		fill->c[lines++][n] = 0;
		p += n;
	}
	if (len && (lines < kMaxLines))
		wcscpy(fill->c[lines++], cur);
	for (int l = 0; l < lines; l++)
		for (WCHAR *c = fill->c[l]; *c; c++)
			if (*c == (WCHAR)(unsigned char)kNoBreak)
				*c = ' ';
	return lines ? lines : 1;
}

// The game's message box with a Continue button, as the save page uses for its errors; '\n' starts a line. y is its
// centre: the Cheats page's list stays drawn behind the box, so its message goes below the list.
static void	ShowMessage(const char *text, float y = 240.f)
{
	static WCHAR msg[2048];
	static WCHAR blank[] = L" ";
	Widen(msg, 2048, text);
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	// every CE message is written in the font's characters (FolderText); one that is not would show a box for it
	for (WCHAR *c = msg; *c; c++)
		if ((*c != '\n') && (*c != (WCHAR)(unsigned char)kNoBreak) && !font->CanDisplay(*c))
		{
			fprintf(stderr, PORT_LOG_PREFIX "message character 0x%02x is not in the game's font\n", (unsigned)*c);
			break;
		}
	FEMESSBOX.Kill();
	FEMESSBOX.Create(320.f, y, 440.f, FO(100), blank, font, 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, 0);
	// Create's own layout (CFEMessBox::Create): the lines, then a line of the normal font for the button
	for (int l = 0; l < 40; l++)
		FEMESSBOX.mLines.c[l][0] = 0;
	FEMESSBOX.mNumLines = WrapLines(&FEMESSBOX.mLines, msg, font, 440.f - 16.f);
	FEMESSBOX.mHeight = float(FEMESSBOX.mLineHeight) * FEMESSBOX.mNumLines + 16.f + float(PLATFORM.Font(FONT_NORMAL)->GetHeight());
	// centred on the screen, or lower for a tall message so it starts below the page's title bar (it ends above y 104)
	float top = y - FEMESSBOX.mHeight * 0.5f;
	if (top < 104.f)
		FEMESSBOX.mY = 104.f + FEMESSBOX.mHeight * 0.5f;
	FEMESSBOX.SetOnDismiss(FEP_NONE, 0);
}

//*********************************************************************************
// An item with its own text that does one thing when chosen.
class CCeMenuItem : public CMenuItem
{
public:
	enum { ID = 0x4345 };				// its mId, which no item of the game's uses

	CCeMenuItem(const WCHAR *text, int action) : CMenuItem((EPCText)0, ID), mAction(action) { mCeText = text; }

	virtual WCHAR	*GetText() { return (WCHAR *)mCeText; }

	const WCHAR	*mCeText;
	int			mAction;
};

// The options list's Cheats item: chosen, CPauseMenu::ButtonPressed opens page 4 by its PC text (patch 0050).
class CCeCheatsItem : public CMenuItem
{
public:
	CCeCheatsItem() : CMenuItem((EPCText)PORT_CE_CHEATS_PCTEXT, 3) {}

	virtual WCHAR	*GetText() { static WCHAR text[] = L"Cheats"; return text; }
};

// A Yes/No switch with its own label (the game's Yes and No, as its other switches).
class CCeSwitch : public CBOOLMenuItem
{
public:
	CCeSwitch(const WCHAR *label) : CBOOLMenuItem((EPCText)0, 3, false) { mCeLabel = label; }

	virtual WCHAR	*GetText() { return (WCHAR *)mCeLabel; }

	const WCHAR	*mCeLabel;
};

//*********************************************************************************
// The God item. Retail builds it only when a pause or options menu is made, so the options menu (made once, kept until
// the front end shuts down) would show a Maladim switched on here only after a restart, and switching it off would
// leave the career's god flag set with no item to clear it. The options menu registers itself (patch 0050), and the
// Maladim switch adds or removes the item on its controls list (page 1, after the configuration name) at once.
#define PORT_CE_CONFIG_NAME	((EPCText)0xef)		// CConfigName's text, the item before God (PauseMenu.cpp)
#define PORT_CE_GOD_CHEAT	3					// Maladim

static CPauseMenu	*sOptionsMenu = NULL;

// God mode the Cheats page allowed (Maladim on there, but not in the career's name and not every cheat through HELP ME!!)
// stays out of the career and options files (patch 0052, Career.cpp): CCareer::Save writes it as off and ce.ini's
// god_mode keeps it (bit 0 player 1, bit 1 player 2) for the next CE session. A recording or a replay (the CE layer off)
// then starts from files that hold only what the career's name allowed, as retail would, and never turns it on.
extern bool	gPortCheatNameOnly;					// patch 0051, FEPSaveGame.cpp
#define PORT_CE_GOD_KEY	"god_mode"

static bool	GodFromSwitchOnly()
{
	if (!PortCeLayer() || !CE_CheatOn(PORT_CE_GOD_CHEAT))
		return false;
	gPortCheatNameOnly = true;
	bool byName = FRONTEND.mFEPSaveGame.IsCheatActive(PORT_CE_GOD_CHEAT) != FALSE;
	gPortCheatNameOnly = false;
	return !byName;
}

static int	GodMask(CCareer *career)
{
	return (career->GetIsGod(0) ? 1 : 0) | (career->GetIsGod(1) ? 2 : 0);
}

// CCareer::Save is about to copy the career: true when its god flags must be written as off.
bool	PortCeCareerSaving(CCareer *career)
{
	if (!GodFromSwitchOnly())
		return false;
	int mask = GodMask(career);
	if (CE_SettingGet(PORT_CE_GOD_KEY, 0) != mask)
		CE_SettingSet(PORT_CE_GOD_KEY, mask);
	return true;
}

// CCareer::Load has read a file: god mode the Cheats page set comes back from ce.ini.
void	PortCeCareerLoaded(CCareer *career)
{
	if (!GodFromSwitchOnly())
		return;
	int mask = CE_SettingGet(PORT_CE_GOD_KEY, 0);
	if (mask & 1)
		career->SetIsGod(0, TRUE);
	if (mask & 2)
		career->SetIsGod(1, TRUE);
}

void	PortCeOptionsMenu(CPauseMenu *menu, bool alive)
{
	if (alive)
		sOptionsMenu = menu;
	else if (sOptionsMenu == menu)
		sOptionsMenu = NULL;
}

static void	SyncGodItem()
{
	if (!sOptionsMenu || (sOptionsMenu->mLists.Size() < 2))
		return;
	CMenuOptionsList *list = sOptionsMenu->mLists.At(1);
	bool want = FRONTEND.mFEPSaveGame.IsCheatActive(PORT_CE_GOD_CHEAT) != FALSE;
	CMenuItem *god = NULL;
	int n = list->mItems.Size();
	for (int i = 0; i < n; i++)
	{
		CMenuItem *item = list->mItems.At(i);
		if ((item->mText == GI_GOD_ON) || (item->mText == GI_GOD_OFF))
			god = item;
	}
	if (want && !god)
	{
		const int kMax = 64;
		CMenuItem *items[kMax];
		if (n > kMax)
			return;
		for (int i = 0; i < n; i++)
			items[i] = list->mItems.At(i);
		list->mItems.RemoveAll();
		for (int i = 0; i < n; i++)
		{
			list->mItems.Append(items[i]);
			if ((items[i]->mText == 0) && (items[i]->mPCText == PORT_CE_CONFIG_NAME))
				list->mItems.Append(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)((CAREER.GetIsGod(0) == 1) ? GI_GOD_ON : GI_GOD_OFF), 3));
		}
	}
	else if (!want && god)
	{
		list->mItems.Remove(god);
		delete god;
	}
	list->Reset();
}

// A cheat's switch: IsCheatActive's number, kept in ce.ini.
class CCeCheatSwitch : public CCeSwitch
{
public:
	CCeCheatSwitch(const WCHAR *label, int cheat) : CCeSwitch(label), mCheat(cheat) {}

	virtual void	SetValue(int value)
	{
		if (!CE_CheatSet(mCheat, value))
		{
			ShowMessage("Your settings could not be saved, so this was not changed.", 415.f);
			return;
		}
		if (mCheat == PORT_CE_GOD_CHEAT)
		{
			// Maladim off (and not in the career's name): god mode ends with it, for both players
			if (!FRONTEND.mFEPSaveGame.IsCheatActive(PORT_CE_GOD_CHEAT))
			{
				CAREER.SetIsGod(0, FALSE);
				CAREER.SetIsGod(1, FALSE);
			}
			if (!value && CE_SettingGet(PORT_CE_GOD_KEY, 0))
				CE_SettingSet(PORT_CE_GOD_KEY, 0);
			SyncGodItem();
		}
	}
	virtual int		GetValue() { return CE_CheatOn(mCheat) ? 1 : 0; }
	// On and Off, as the page's help line says what each cheat turns on
	virtual WCHAR	*GetChoiceText(int n)
	{
		static WCHAR off[] = L"Off", on[] = L"On";
		return n ? on : off;
	}

	int		mCheat;
};

// Record sessions: the launcher reads record_sessions from ce.ini as it starts the game. The list asks for the value
// every frame (CMenuMenuItem::RenderList), so it is read from ce.ini once and kept, as the cheats are.
class CCeRecordSwitch : public CCeSwitch
{
public:
	CCeRecordSwitch() : CCeSwitch(L"Record sessions: ") {}

	static int	sValue;

	virtual void	SetValue(int value)
	{
		value = value ? 1 : 0;
		if (!CE_SettingSet("record_sessions", value))
		{
			ShowMessage("Your settings could not be saved, so this was not changed.");
			return;
		}
		sValue = value;
		char folder[1024], text[1536];
		if (value)
			snprintf(text, sizeof(text), "Session recording is now on. It starts the next time you launch Onslaught CE: quit "
					 "the game, then start it again. Each start then asks whether to record; choose Stop recording there to "
					 "turn it off.\n\nA recording keeps your keyboard and mouse input and a copy of your careers and "
					 "settings, so a problem you report can be replayed exactly. Recordings are saved in:\n%s\n\nWhile "
					 "recording, the game runs at 30 frames a second with the original menus, so the Onslaught CE menu and "
					 "cheats are hidden.", FolderText(folder, sizeof(folder), "tapes"));
		else
			snprintf(text, sizeof(text), "Session recording is now off, from the next time you launch Onslaught CE.");
		ShowMessage(text);
	}
	virtual int		GetValue()
	{
		if (sValue < 0)
			sValue = CE_SettingGet("record_sessions", 0) ? 1 : 0;
		return sValue;
	}
};

int	CCeRecordSwitch::sValue = -1;

//*********************************************************************************
// The cheats the game checks (IsCheatActive's numbers; V3R5IOF, number 2, has no effect in the PC game and is left out),
// each with the line the page shows while it is highlighted: what it does (src/FEPLevelSelect.cpp IsWorldUnlocked,
// FEPGoodies.cpp get_goodie_state, PauseMenu.cpp's controls list and Player.cpp SetIsGod, game.cpp's
// BUTTON_TOGGLE_FREE_CAMERA on F in PCController.cpp) and the code a career's name held to turn it on.
struct SCeCheat
{
	const WCHAR	*mLabel;
	int			mCheat;
	const WCHAR	*mHelp;
	const WCHAR	*mCode;
};

static const SCeCheat	kCheats[] = {
	{L"Unlock all levels: ", 1, L"Opens every level in the level select.", L"Original code: TURKEY"},
	{L"Unlock all goodies: ", 0, L"Opens every goodie on the Goodies page.", L"Original code: MALLOY"},
	{L"Goodie hints: ", 5, L"The Goodies page shows how to unlock each goodie, but none can be opened while this is on.",
	 L"Original code: lat\u00eate"},
	{L"God mode option: ", PORT_CE_GOD_CHEAT, L"Adds a God switch to Controller Options; turn it on there for no damage "
											   L"and endless energy.", L"Original code: Maladim"},
	{L"Free camera (F key): ", 4, L"In a level, F lets you fly the camera around; F again returns it to your Battle Engine.",
	 L"Original code: Aurore"},
};
#define CE_NUM_CHEATS	int(sizeof(kCheats) / sizeof(kCheats[0]))

// The page's Back item, the list's last: after drawing itself (the list draws its items in order, so this comes after
// the others) it draws, below the list, the highlighted cheat's help line and the page's standing note, in the small
// font's normal text with its shadow as the options pages draw their rows (the list's disabled grey is for what cannot
// be chosen). Nothing is drawn while a message box is shown over the list.
class CCeCheatsBack : public CMenuItem
{
public:
	CCeCheatsBack(CMenuOptionsList *list) : CMenuItem((DWORD)GI_BACK, 3), mList(list) {}

	virtual void	Render(float x, float y, BOOL selected)
	{
		CMenuItem::Render(x, y, selected);
		if (FEMESSBOX.BeingDisplayed())
			return;
		static WCHAR note[] = L"Cheats apply to every career and are off while a session is being recorded.";
		CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
		float line = float(font->GetHeight());
		// what the highlighted cheat does, in white, then its original code, as trivia, in the note's grey
		if ((mList->mSelected >= 0) && (mList->mSelected < CE_NUM_CHEATS))
		{
			const SCeCheat &cheat = kCheats[mList->mSelected];
			float ty = y + 20.f + line * 0.5f;
			ty += line * Lines(font, cheat.mHelp, ty, 0xffffffff);
			Lines(font, cheat.mCode, ty, kGrey);
		}
		// the note ends a little above the page's help bar (CFEPCommon::RenderHelpText draws it at y 455)
		Lines(font, note, 449.f, kGrey, true);
	}

	static const DWORD	kGrey = 0xffa8a8a8;

	// The text in lines wrapped to the space between the page's side borders, each centred on the page, from y down (or
	// ending at y, when up); returns the number of lines.
	static int	Lines(CDXBitmapFont *font, const WCHAR *text, float y, DWORD colour, bool up = false)
	{
		static SWordWrapFill fill;
		static WCHAR buf[400];
		wcsncpy(buf, text, 399);
		buf[399] = 0;
		int n = font->WordWrap(&fill, buf, 420.f);
		float line = float(font->GetHeight());
		if (up)
			y -= line * n;
		for (int l = 0; l < n; l++)
		{
			SIZE size;
			font->GetTextExtent(fill.c[l], &size);
			font->DrawTextWithShadow(320.f - float(size.cx / 2), y + line * l, colour, fill.c[l], 0, 0, 1.f, 1.f);
		}
		return n;
	}

	CMenuOptionsList	*mList;
};

CMenuItem	*PortCeCheatsItem()
{
	return new (MEMTYPE_MENUITEM) CCeCheatsItem();
}

CMenuOptionsList	*PortCeCheatsList(float y)
{
	static WCHAR title[] = L"Cheats";
	CMenuOptionsList *list = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, y, FALSE, 0xb0000000);
	for (const SCeCheat &cheat : kCheats)
		list->AddItem(new (MEMTYPE_MENUITEM) CCeCheatSwitch(cheat.mLabel, cheat.mCheat));
	list->AddItem(new (MEMTYPE_MENUITEM) CCeCheatsBack(list));
	return list;
}

//*********************************************************************************
// The CE page's menu: a CPauseMenu without the pause menu's lists (patch 0050's constructor), so the list draws and
// takes input exactly as the options page's do.
static const char kSpecialThanks[] =
	"Special Thanks\n\n"
	"Lost Toys - original game.\n"
	"Stuart Gillam (Discord: desimbr)\n"
	"Help, insight, guidance.\n\n"
	"Early Discord testers:\n"
	"Jeppi, BermudaMaster.\n\n"
	"Onslaught CE contributors.\n"
	"BEA community and\npreservation projects.\n"
	"Everyone sharing fixes\nand guiding coding agents.\n\n"
	"Original game credits: Credits.";

enum ECeAction
{
	CEA_NONE = 0,
	CEA_ABOUT,
	CEA_UPDATES,
	CEA_REPORT,
	CEA_THANKS
};

CPauseMenu::CPauseMenu(EPortCeNoLists) : mActive(FALSE), mPage(0), mCloseTime(-1.f)
{
	mOptionsPage = true;
	mResult = 0;
	mCircle1 = NULL;
	mCircle2 = NULL;
	mChosen = 0;
	mConfirm = NULL;
	mQuitConfirm = NULL;
	mNoFade = FALSE;
	mFirstRenderTime = 0;
}

class CCeMenu : public CPauseMenu
{
public:
	CCeMenu() : CPauseMenu(PORT_CE_NO_LISTS)
	{
		static WCHAR title[] = L"Onslaught CE";
		CMenuOptionsList *list = new (MEMTYPE_MENUITEM) CMenuOptionsList(title, 320.f, 300.f, FALSE, 0xb0000000);
		list->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(L"About Onslaught CE", CEA_ABOUT));
		list->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(L"Special Thanks", CEA_THANKS));
		list->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(L"Check for updates", CEA_UPDATES));
		list->AddItem(new (MEMTYPE_MENUITEM) CCeRecordSwitch());
		list->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(L"Report a problem", CEA_REPORT));
		list->AddItem(new (MEMTYPE_MENUITEM) CMenuItem((DWORD)GI_BACK, 3));
		mLists.Append(list);
	}

	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val)
	{
		if (PLATFORM.GetSysTimeFloat() - mOpenTime < 0.4f)
			return;
		CMenuOptionsList *list = mLists.At(mPage);
		if (list->PassToSelected(from_controller, button, val))
			return;
		switch (button)
		{
		default:
			list->ReceiveButtonAction(from_controller, button, val);
			break;
		case BUTTON_PAUSE_MENU_SELECT:
			Choose(list->mItems.At(list->mSelected));
			break;
		case BUTTON_FRONTEND_MENU_BACK:
			Leave();
			break;
		}
	}

	void	Leave()
	{
		DeActivate(FALSE);
		CFrontEnd::PlaySound(FES_BACK);
	}

	void	Choose(CMenuItem *item)
	{
		if (item->mText == GI_BACK)
		{
			Leave();
			return;
		}
		if ((item->mId != CCeMenuItem::ID) || !item->IsEnabled())
			return;
		char text[1536];
		switch (((CCeMenuItem *)item)->mAction)
		{
		case CEA_ABOUT:
		{
			const char *build = CE_BuildName();
			snprintf(text, sizeof(text), "Onslaught CE v%s (preview)%s%s\n\nBattle Engine Aquila (Lost Toys, 2003), rebuilt "
					 "from the game's source code to run natively on today's computers. It plays the game files of your "
					 "own copy of the game.\n\nAn unofficial fan project, not affiliated with or endorsed by the game's "
					 "developer, publisher or rights holders. Free software under the GNU General Public License, version 3, with no "
					 "warranty.\n\nSource code, new versions and problem reports:\n" CE_LINKS, CE_VERSION,
					 *build ? ", build " : "", build);
			ShowMessage(text);
			break;
		}
		case CEA_UPDATES:
			if (CE_SettingGet("update_check", 1))
			{
				char folder[1024];
				snprintf(text, sizeof(text), "Onslaught CE looks for a new version each time you start it, and asks you "
						 "before updating. To check now, quit the game and start it again.\n\nA version you chose Skip "
						 "this version for is not offered again. To be asked about it again, quit the game, open the file "
						 "ce.ini in a text editor, delete the line that starts with \"skip\", and start Onslaught CE again. "
						 "ce.ini is in:\n%s\n\nNew versions are also listed at:\n" CE_LINKS "/releases",
						 FolderText(folder, sizeof(folder), ""));
			}
			else
			{
				char folder[1024];
				snprintf(text, sizeof(text), "Update checks are turned off. To turn them back on, quit the game, open the "
						 "file ce.ini in a text editor, delete the line that starts with \"update\", and start Onslaught "
						 "CE again. ce.ini is in:\n%s\n\nNew versions are also listed at:\n" CE_LINKS "/releases",
						 FolderText(folder, sizeof(folder), ""));
			}
			ShowMessage(text);
			break;
		case CEA_THANKS:
			ShowMessage(kSpecialThanks);
			break;
		case CEA_REPORT:
		{
			char folder[1024];
			FolderText(folder, sizeof(folder), "");
			if (CE_OpenReportPage())
				snprintf(text, sizeof(text), "A problem report page has opened in your web browser (it may be behind the "
						 "game). It already names your version and system; nothing is sent until you submit it.\n\nPlease "
						 "describe what happened, and attach the file onslaught-ce.log from this folder:\n%s", folder);
			else
				snprintf(text, sizeof(text), "Your web browser could not be opened. To report a problem, go to:\n" CE_LINKS
						 "/issues\n\nPlease describe what happened, and attach the file onslaught-ce.log from this "
						 "folder:\n%s", folder);
			ShowMessage(text);
			break;
		}
		}
		CFrontEnd::PlaySound(FES_SELECT);
	}

	// CPauseMenu::Render for an options-style page: the render states, the 0.4 s before the list appears, the list.
	WCHAR	*Draw()
	{
		if (!mActive)
		{
			mFirstRenderTime = 0;
			if ((mCloseTime < 0.f) || (PLATFORM.GetSysTimeFloat() - mCloseTime > 0.4f))
				return NULL;
		}
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		RENDERINFO.SetFogEnabled(false);
		RENDERINFO.SetUnknown2ed(false);
		RENDERINFO.STS(0, TSS_ADDRESSU, TADDRESS_CLAMP);
		RENDERINFO.STS(0, TSS_ADDRESSV, TADDRESS_CLAMP);
		LT.SRS(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
		LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
		LT.SRS(D3DRS_ALPHAREF, 8);
		LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
		LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
		LT.SRS(D3DRS_ZENABLE, FALSE);
		RENDERINFO.Apply();
		if (mFirstRenderTime == 0.f)
			mFirstRenderTime = PLATFORM.GetSysTimeFloat();
		float fade = mActive ? PLATFORM.GetSysTimeFloat() - mOpenTime : (mCloseTime + 0.4f) - PLATFORM.GetSysTimeFloat();
		WCHAR *title = NULL;
		if (fade > 0.4f)
			title = mLists.At(mPage)->Render(this);
		LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
		LT.SRS(D3DRS_ZENABLE, TRUE);
		return title;
	}
};

//*********************************************************************************
// The page, made as the options page is: the video background, the sliding borders, the menu, the title bar, the help
// line and the small logo.
class CFEPCe : public CFrontEndPage
{
public:
	CFEPCe() : mMenu(NULL) {}

	void	Shutdown() { SAFE_DELETE(mMenu); }

	void	ButtonPressed(SINT button, float val)
	{
		if (mMenu)
			mMenu->ReceiveButtonAction(NULL, button, val);
	}

	void	Process(EFEPState state)
	{
		if ((state == FEPS_ACTIVE) && mMenu && (!mMenu->mActive))
			FRONTEND.SetPage(FEP_MAIN, 70);
	}

	void	RenderPreCommon(float transition, EFrontEndPage dest)
	{
		FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
	}

	void	Render(float transition, EFrontEndPage dest)
	{
		static WCHAR title[] = L"Onslaught CE";
		FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);
		// the message box is translucent: the list is not drawn behind it
		if (mMenu && !FEMESSBOX.BeingDisplayed())
			mMenu->Draw();
		FRONTEND.DrawTitleBar(title, transition, dest);
		FRONTEND.GetCommonPage()->RenderHelpText(1, transition);
		FRONTEND.GetCommonPage()->RenderSmallBEALogo(RangeTransition(transition, 0.75f, 1.f));
	}

	void	TransitionNotification(EFrontEndPage from)
	{
		if (!mMenu)
			mMenu = new (MEMTYPE_MENUITEM) CCeMenu();
		CPauseMenu::mOptionsPage = true;
		mMenu->Activate(0);
	}

	CCeMenu	*mMenu;
};

static CFEPCe	sCePage;

CFrontEndPage	*PortCePage()
{
	return &sCePage;
}

void	PortCeOpenPage()
{
	FRONTEND.SetPage(FEP_CE, 70);
}

#endif
