// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_frontend.cpp: the CE layer's front-end additions (patch 0050): an "Onslaught CE" action on the main menu
// opening a page of the game's own options-style lists: Bonus Levels, CE Settings, Cheats (switches kept in ce.ini, patch
// 0051 at IsCheatActive, that work as the cheat codes do in a career's name), Help & Info (Controls & Tips, What's Coming,
// Updates & Rollback, About, Special Thanks) and Report a Problem. Every row has a hint under the list. The
// pause menu's Photo Mode item (patch 0138) is made here too. Built as a game file (port/build.py) so it uses the game's
// menu classes; public builds only.
//
// None of it exists while the CE menus are off: in replays, recordings and the test harness (PortCeLayer, ce.cpp), under
// -autoconfigtest and in the playable demo, the main menu has its seven rows, the language-test page keeps its slot and
// the options page and the pause menu are the game's own, so a tape navigates exactly the menus retail had.
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
#include "ce_photo.h"

#include <stdio.h>
#include <string.h>

#define MEMTYPE_MENUITEM	((EMemoryType)0x80)		// the menu items' memory type (MenuItem.cpp, PauseMenu.cpp)
#define PORT_CE_PHOTO_PCTEXT	0x4346				// the pause menu's Photo Mode item (patch 0138, PauseMenu.cpp)
#define PORT_CE_ACTION		9						// the main menu's CE action (patch 0050, FEPMain.cpp)
#define FEP_CE				FEP_LANGUAGE_TEST		// the CE page's slot

bool			PortCeLayer();						// port/platform/common/ce.cpp
extern "C" int	CE_OpenReportPage(void);
extern "C" int	CE_SettingGet(const char *key, int def);
extern "C" int	CE_TrySettingGet(const char *key, int def, int *out);
extern "C" int	CE_SettingSet(const char *key, int value);
extern "C" int	CE_CheatOn(int n);
extern "C" int	CE_CheatSet(int n, int on);
extern "C" int	CE_CheatReady(int n);
extern "C" int	CE_CheatRestartRequired(int n);
extern "C" void CE_MaladimRequireRestart(void);
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
	int saved = 0;
	if (!CE_TrySettingGet(PORT_CE_GOD_KEY, 0, &saved))
		fprintf(stderr, PORT_LOG_PREFIX "could not read god mode while saving; the setting was not updated\n");
	else if (saved != mask)
		CE_SettingSet(PORT_CE_GOD_KEY, mask);
	return true;
}

// CCareer::Load has read a file: god mode the Cheats page set comes back from ce.ini.
void	PortCeCareerLoaded(CCareer *career)
{
	if (!GodFromSwitchOnly())
		return;
	int mask = 0;
	if (!CE_TrySettingGet(PORT_CE_GOD_KEY, 0, &mask))
	{
		CE_MaladimRequireRestart();
		fprintf(stderr, PORT_LOG_PREFIX "could not read god mode while loading; restart after fixing settings\n");
		return;
	}
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
			if (CE_CheatRestartRequired(mCheat))
				ShowMessage("Your settings could not be read. Restart Onslaught CE to retry this cheat.", 415.f);
			else
				ShowMessage("Your settings could not be read or saved, so this was not changed.", 415.f);
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
			if (!value)
			{
				int saved = 0;
				if (!CE_TrySettingGet(PORT_CE_GOD_KEY, 0, &saved) ||
					(saved && !CE_SettingSet(PORT_CE_GOD_KEY, 0)))
					ShowMessage("The Maladim switch is off, but the God setting could not be cleared. Check your settings before enabling it again.", 415.f);
			}
			SyncGodItem();
		}
	}
	virtual int		GetValue()
	{
		// Keep a pending choice stable if another row or gameplay loads the settings while this list is open.
		if (mExpanded && mValue == -1) return -1;
		return CE_CheatReady(mCheat) ? (CE_CheatOn(mCheat) ? 1 : 0) : -1;
	}
	// On and Off, as the page's help line says what each cheat turns on
	virtual WCHAR	*GetChoiceText(int n)
	{
		static WCHAR off[] = L"Off", on[] = L"On", unavailable[] = L"Unavailable", restart[] = L"Restart to retry";
		return n < 0 ? (CE_CheatRestartRequired(mCheat) ? restart : unavailable) : n ? on : off;
	}

	int		mCheat;
};

// Record Play for Bug Reports: the launcher reads record_sessions from ce.ini as it starts the game. The list asks for the
// value every frame (CMenuMenuItem::RenderList), so it is read from ce.ini once and kept, as the cheats are.
class CCeRecordSwitch : public CCeSwitch
{
public:
	CCeRecordSwitch() : CCeSwitch(L"Record Play for Bug Reports: ") {}

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
			snprintf(text, sizeof(text), "Session recording is on from the next time you start Onslaught CE. Each start asks "
					 "whether to record; Stop recording there turns it off.\n\nA recording keeps your keyboard and mouse "
					 "input and copies some files from your CE folder to help reproduce a problem. Gamepads and flight sticks "
					 "are not recorded and are unavailable while recording; an older tape made with one can replay "
					 "differently. Saved in:\n%s\n\nWhile recording, the game runs at 30 frames a second with the original "
					 "menus; the Onslaught CE menu and cheats are hidden.", FolderText(folder, sizeof(folder), "tapes"));
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
// Under each page's list the page says what the highlighted row does: a line in white, and for some rows a second line in
// grey (the cheats' original codes). The texts are in the tables below, one hint a row; the list's last item, Back, draws them.
struct SCeHint
{
	const WCHAR	*mHelp;
	const WCHAR	*mExtra;
};

// The cheats the game checks (IsCheatActive's numbers; V3R5IOF, number 2, has no effect in the PC game and is left out),
// each with the line the page shows while it is highlighted: what it does (src/FEPLevelSelect.cpp IsWorldUnlocked,
// FEPGoodies.cpp get_goodie_state, PauseMenu.cpp's controls list and Player.cpp SetIsGod, game.cpp's
// BUTTON_TOGGLE_FREE_CAMERA and camera keys in PCController.cpp, CControllableCamera::ReceiveButtonAction in Camera.cpp)
// and the code a career's name held to turn it on.
struct SCeCheat
{
	const WCHAR	*mLabel;
	int			mCheat;
	SCeHint		mHint;
};

static const SCeCheat	kCheats[] = {
	{L"Unlock All Levels: ", 1, {L"Every level shows as open in the level select. Your career's own progress is not changed.",
	 L"Original code: TURKEY"}},
	{L"Unlock All Goodies: ", 0, {L"Every goodie on the Goodies page is open. Nothing is saved to your career.",
	 L"Original code: MALLOY"}},
	{L"Goodie Hints: ", 5, {L"The Goodies page shows the available unlock hints. Goodies cannot be opened, even if Unlock All "
							L"Goodies is on.", L"Original code: lat\u00eate"}},
	{L"God Mode Option: ", PORT_CE_GOD_CHEAT, {L"Adds God to Controller Options. God on: no damage, endless energy. Kept in "
											   L"ce.ini, not in your career.", L"Original code: Maladim"}},
	{L"Free Camera (F Key): ", 4, {L"F: free camera. W/S fly, A/D slide, arrows look. Esc freezes the battle; F returns. You "
								   L"can still be hit.", L"Original code: Aurore"}},
};
#define CE_NUM_CHEATS	int(sizeof(kCheats) / sizeof(kCheats[0]))
static WCHAR	kCheatsNote[] = L"Cheats apply to all careers and are off when recording. Wins count as usual.";

// The page's Back item, the list's last: after drawing itself (the list draws its items in order, so this comes after
// the others) it draws, below the list, the highlighted row's hint (hints[row], stride bytes apart, count of them: Back
// itself has none) and the page's standing note, if it has one, in the small font's normal text with its shadow as the
// options pages draw their rows (the list's disabled grey is for what cannot be chosen). Nothing is drawn while a message
// box is shown over the list.
class CCeBack : public CMenuItem
{
public:
	CCeBack(CMenuOptionsList *list, const SCeHint *hints, int stride, int count, const WCHAR *note)
		: CMenuItem((DWORD)GI_BACK, 3), mList(list), mHints(hints), mStride(stride), mCount(count), mNote(note) {}

	virtual void	Render(float x, float y, BOOL selected)
	{
		CMenuItem::Render(x, y, selected);
		if (FEMESSBOX.BeingDisplayed())
			return;
		CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
		float line = float(font->GetHeight());
		// what the highlighted row does, in white, then (for a cheat) its original code, as trivia, in the note's grey
		if ((mList->mSelected >= 0) && (mList->mSelected < mCount))
		{
			const SCeHint &hint = *(const SCeHint *)((const char *)mHints + mList->mSelected * mStride);
			float ty = y + 20.f + line * 0.5f;
			ty += line * Lines(font, hint.mHelp, ty, 0xffffffff);
			if (hint.mExtra)
				Lines(font, hint.mExtra, ty, kGrey);
		}
		// the note ends a little above the page's help bar (CFEPCommon::RenderHelpText draws it at y 455)
		if (mNote)
			Lines(font, mNote, 449.f, kGrey, true);
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
	const SCeHint		*mHints;
	int					mStride, mCount;
	const WCHAR			*mNote;
};

//*********************************************************************************
// The CE page's menu: a CPauseMenu without the pause menu's lists (patch 0050's constructor), so the list draws and
// takes input exactly as the options page's do.
static const char kSpecialThanks[] =
	"Special Thanks\n\n"
	"Lost Toys - original game.\n"
	"Stuart Gillam (Discord: desimbr)\n"
	"Help, insight, guidance.\n\n"
	"Early testers: Jeppi,\n"
	"BermudaMaster and others.\n\n"
	"Onslaught CE contributors,\n"
	"including work made with\ntheir own coding agents.\n"
	"BEA community and\npreservation projects.\n\n"
	"Original game credits: Credits.";

// What a row does when chosen. The first values are the old page's, kept; the plans are CEA_PLAN_FIRST + their index, the
// five Race Challenges CEA_RACE_FIRST + 0..4 (levels 901-905, ce_bonus.cpp), the pages CEA_PAGE_*.
enum ECeAction
{
	CEA_NONE = 0,
	CEA_ABOUT,
	CEA_UPDATES,
	CEA_REPORT,
	CEA_THANKS,
	CEA_BONUS,
	CEA_COOP,
	CEA_PLANS,
	CEA_PLAN_FIRST,
	CEA_RACE_FIRST = 100,
	CEA_PAGE_BONUS = 120,
	CEA_PAGE_SETTINGS,
	CEA_PAGE_CHEATS,
	CEA_PAGE_HELP,
	CEA_TIPS
};
#define CE_NUM_RACES	5

// The pages (lists of rows) and where Back goes from each: the hub, its four pages, and What's Coming under Help & Info.
enum ECePage
{
	CEP_HUB = 0,
	CEP_BONUS,
	CEP_SETTINGS,
	CEP_CHEATS,
	CEP_HELP,
	CEP_PLANS,
	CEP_COUNT
};
static const int	kCeParent[CEP_COUNT] = {-1, CEP_HUB, CEP_HUB, CEP_HUB, CEP_HUB, CEP_HELP};
static WCHAR		kCeTitles[CEP_COUNT][24] = {L"Onslaught CE", L"Bonus Levels", L"CE Settings", L"Cheats", L"Help & Info",
												L"What's Coming"};

// A row: its label, what it does, and its hint (the font has no underscore, so a setting's
// ce.ini name is left to the README).
struct SCeRow
{
	const WCHAR	*mLabel;
	int			mAction;
	SCeHint		mHint;
};

static const SCeRow	kHubRows[] = {
	{L"Bonus Levels", CEA_PAGE_BONUS, {L"Extra levels. None of them changes your career progress.", 0}},
	{L"CE Settings", CEA_PAGE_SETTINGS, {L"Options that Onslaught CE adds. They are kept in ce.ini.", 0}},
	{L"Cheats", CEA_PAGE_CHEATS, {L"The original game's cheats as switches. They apply to every career.", 0}},
	{L"Help & Info", CEA_PAGE_HELP, {L"Controls and tips, what is coming, updates, about and thanks.", 0}},
	{L"Report a Problem", CEA_REPORT, {L"Opens a problem report in your browser. Nothing is sent until you submit it.", 0}},
};

// Levels 201, 856 and 901-905 (ce_bonus.cpp), in the order of the list; a bonus level returns here, its row highlighted. The original
// ships the building-defence scenario twice (levels 856 and 858 are the same map), so the list has it once.
static const SCeRow	kBonusRows[] = {
	{L"Aquila Prototype", CEA_BONUS, {L"1 player. The original's demo level. Career progress is not changed.", 0}},
	{L"Building Defence", CEA_COOP, {L"2 players on this PC, in split screen. Career progress is not changed.", 0}},
	{L"Race Challenge 1", CEA_RACE_FIRST + 0, {L"1 player. Three laps against the clock. Career progress is not changed.", 0}},
	{L"Race Challenge 2", CEA_RACE_FIRST + 1, {L"1 player. Three laps against the clock. Career progress is not changed.", 0}},
	{L"Race Challenge 3", CEA_RACE_FIRST + 2, {L"1 player. Three laps against the clock. Career progress is not changed.", 0}},
	{L"Race Challenge 4", CEA_RACE_FIRST + 3, {L"1 player. Three laps against the clock. Career progress is not changed.", 0}},
	{L"Race Challenge 5", CEA_RACE_FIRST + 4, {L"1 player. Three laps against the clock. Career progress is not changed.", 0}},
};
#define CE_NUM_BONUS	int(sizeof(kBonusRows) / sizeof(kBonusRows[0]))

// CE Settings: the switch rows are items of their own (Record Play for Bug Reports); their hints.
static const SCeHint	kSettingsHints[] = {
	{L"Records your play for bug reports, from the next start of the game.", 0},
};
#define CE_NUM_SETTINGS	int(sizeof(kSettingsHints) / sizeof(kSettingsHints[0]))

static const SCeRow	kHelpRows[] = {
	{L"Controls & Tips", CEA_TIPS, {L"Dodge, zoom, photo mode, screenshots, the console, the free camera and the commander map.", 0}},
	{L"What's Coming", CEA_PLANS, {L"Plans and ideas. Nothing there starts a feature.", 0}},
	{L"Updates & Rollback", CEA_UPDATES, {L"How updates work, and how to go back to the version before.", 0}},
	{L"About Onslaught CE", CEA_ABOUT, {L"The version, the licence, and where the source and reports are.", 0}},
	{L"Special Thanks", CEA_THANKS, {L"The people and projects behind the game and this port.", 0}},
};

// The Controls & Tips note: eight lines at most in the game's font (about 47 characters a line in the message box), so the lines
// are broken here rather than left to the wrap. Only what exists: the dodge and the zoom as README's Dodging and Zooming say
// them, F11, F12 and the console (solo levels), the free camera with its cheat, and the commander map, which commander=1 in
// ce.ini turns on (experimental, off by default). The font has no backquote, so the console's key is named in words.
static const char kControlsTips[] =
	"Controls & Tips\n"
	"Dodge, in walker mode: tap A then D,\nor D then A.\n"
	"Zoom: mouse wheel, or the gamepad bumpers.\n"
	"Solo levels: F11 photo mode, F12 screenshot,\nthe grave key (left of 1) opens the console.\n"
	"F: free camera, with the Free Camera cheat on.\n"
	"M: commander map, with commander=1 in ce.ini.";

// These are descriptions, not controls. The separate Godot project and early concepts are labelled as such.
struct SCePlan { const WCHAR *mLabel; const char *mText; };
static const SCePlan kCePlans[] = {
	{L"Online play", "Internet Host and Join, versus, skirmish and cross-platform play are in development and not available yet. "
	 "Larger matches are a future idea. For two players on one PC, use Multiplayer on the main menu."},
	{L"Campaign co-op", "Playing the campaign together online is experimental and not available yet. Local co-op bonus "
	 "levels are on the Onslaught CE page."},
	{L"Battle Engine / mods", "Mods are experimental and work on Linux only. A mods= line in ce.ini turns on mods from the mods folder "
	 "beside it; Windows ignores the line and logs why. Each mod file is checked at start. A mod can replace game files such "
	 "as textures or supply a level's world file. There is no mod menu (see Mods in the project README). Online Battle "
	 "Engine customization is not available."},
	{L"Map and mission tools", "A level editor is being built outside the game, in the separate Onslaught Companion app. There are no "
	 "map or mission tools in the game itself."},
	{L"HUD and display", "On a wide screen the solo HUD's corner groups sit in the screen's corners, and a setting in ce.ini (named in "
	 "the README) keeps them in the 4:3 columns instead. Parts of the HUD can also scale at higher resolutions. Both are off in "
	 "recordings, replays and multiplayer. Sharper text is off by default. A setting in ce.ini (see the README) can sharpen bitmap "
	 "text drawn at least twice as large horizontally or vertically on a non-4:3 picture. It is normally off in recordings and "
	 "replays or when widescreen modes are off. Selectable HUD size and changing display mode without a restart are being "
	 "explored. Windowed and Fullscreen settings currently need a restart."},
	{L"Controls and tutorial", "More dodge and zoom guidance, control options and controller refinements are planned. "
	 "Solo gamepad defaults and English tutorial cues already exist."},
	{L"CE features in recordings", "Session recordings use original-style menus today. Controller input is not recorded and "
	 "many CE additions are off. More recording support is being explored."},
	{L"Full photo mode", "Photo mode already hides the HUD for pictures (F11), and F12 saves a screenshot in single-player levels. "
	 "A fuller photo mode with camera tools is an idea, not a feature yet."},
	{L"Godot and commander ideas", "The separate Godot rebuild is in development. Commander mode (experimental): an optional map "
	 "for ordering friendly squads in single-player missions. Turn it on in the ce.ini file (see the README). A second player "
	 "taking the commander seat online is an idea, not available."}
};
#define CE_NUM_PLANS int(sizeof(kCePlans) / sizeof(kCePlans[0]))

// The same topics as the list shows them: in Title Case like the original's pages (kCePlans' own labels are the topics'
// names, kept as they were), each with its hint. A test checks the two tables name the same topics.
static const WCHAR	*const kPlanLabels[CE_NUM_PLANS] = {
	L"Online Play", L"Campaign Co-op", L"Battle Engine / Mods", L"Map and Mission Tools", L"HUD and Display",
	L"Controls and Tutorial", L"CE Features in Recordings", L"Full Photo Mode", L"Godot and Commander Ideas"};
static const SCeHint	kPlanHints[CE_NUM_PLANS] = {
	{L"In development. Not available yet.", 0},
	{L"Experimental. Not available yet.", 0},
	{L"Experimental, and on Linux only. There is no mod menu.", 0},
	{L"Planned outside the game, in the Companion app.", 0},
	{L"Some of it works today. More is being explored.", 0},
	{L"Some of it works today. More is planned.", 0},
	{L"Recordings use the original's menus today.", 0},
	{L"An idea. F11 and F12 work today.", 0},
	{L"The Godot rebuild is in development. The commander map is experimental.", 0},
};

bool	PortCeBonusStart(int level);		// ce_bonus.cpp
int		PortCeBonusTakeReturn();			// ce_bonus.cpp: the bonus level the front end has just come back from, or 0

// The level a Bonus Levels row starts (ce_bonus.cpp), or 0 for any other action.
static int	BonusLevel(int action)
{
	if (action == CEA_BONUS)
		return 201;
	if (action == CEA_COOP)
		return 856;
	if ((action >= CEA_RACE_FIRST) && (action < CEA_RACE_FIRST + CE_NUM_RACES))
		return 901 + action - CEA_RACE_FIRST;
	return 0;
}

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
		// Each page is a list of its rows and, last, Back, which draws the highlighted row's hint (CCeBack); the lists are
		// in ECePage's order.
		AddPage(CEP_HUB, kHubRows, int(sizeof(kHubRows) / sizeof(kHubRows[0])));
		AddPage(CEP_BONUS, kBonusRows, CE_NUM_BONUS);
		CMenuOptionsList *list = NewList(CEP_SETTINGS);
		list->AddItem(new (MEMTYPE_MENUITEM) CCeRecordSwitch());
		list->AddItem(new (MEMTYPE_MENUITEM) CCeBack(list, kSettingsHints, int(sizeof(SCeHint)), CE_NUM_SETTINGS, NULL));
		mLists.Append(list);
		list = NewList(CEP_CHEATS);
		for (const SCeCheat &cheat : kCheats)
			list->AddItem(new (MEMTYPE_MENUITEM) CCeCheatSwitch(cheat.mLabel, cheat.mCheat));
		list->AddItem(new (MEMTYPE_MENUITEM) CCeBack(list, &kCheats[0].mHint, int(sizeof(SCeCheat)), CE_NUM_CHEATS, kCheatsNote));
		mLists.Append(list);
		AddPage(CEP_HELP, kHelpRows, int(sizeof(kHelpRows) / sizeof(kHelpRows[0])));
		list = NewList(CEP_PLANS);
		for (int i = 0; i < CE_NUM_PLANS; i++)
			list->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(kPlanLabels[i], CEA_PLAN_FIRST + i));
		list->AddItem(new (MEMTYPE_MENUITEM) CCeBack(list, kPlanHints, int(sizeof(SCeHint)), CE_NUM_PLANS, NULL));
		mLists.Append(list);
	}

	static CMenuOptionsList	*NewList(int page)
	{
		return new (MEMTYPE_MENUITEM) CMenuOptionsList(kCeTitles[page], 320.f, 300.f, FALSE, 0xb0000000);
	}

	// A page of plain rows: each its own row, then Back.
	void	AddPage(int page, const SCeRow *rows, int count)
	{
		CMenuOptionsList *list = NewList(page);
		for (int i = 0; i < count; i++)
			list->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(rows[i].mLabel, rows[i].mAction));
		list->AddItem(new (MEMTYPE_MENUITEM) CCeBack(list, &rows[0].mHint, int(sizeof(SCeRow)), count, NULL));
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
			Back();
			break;
		}
	}

	void	Leave()
	{
		DeActivate(FALSE);
		CFrontEnd::PlaySound(FES_BACK);
	}

	// Back goes to the page above (the hub leaves the Onslaught CE page, to the main menu).
	void	Back()
	{
		if (mPage == CEP_HUB)
			Leave();
		else
		{
			mPage = kCeParent[mPage];
			CFrontEnd::PlaySound(FES_BACK);
		}
	}

	// A page opens with its first row highlighted.
	void	OpenPage(int page)
	{
		mPage = page;
		mLists.At(mPage)->Reset();
	}

	// A bonus level has just ended and the Onslaught CE page is open again: its list, the level's row highlighted.
	void	ShowBonus(int level)
	{
		OpenPage(CEP_BONUS);
		for (int row = 0; row < CE_NUM_BONUS; row++)
			if (BonusLevel(kBonusRows[row].mAction) == level)
				mLists.At(mPage)->mSelected = row;
	}

	WCHAR	*PageTitle()
	{
		return kCeTitles[mPage];
	}

	void	Choose(CMenuItem *item)
	{
		if (item->mText == GI_BACK)
		{
			Back();
			return;
		}
		if ((item->mId != CCeMenuItem::ID) || !item->IsEnabled())
			return;
		char text[1536];
		const int action = ((CCeMenuItem *)item)->mAction;
		if ((action >= CEA_PLAN_FIRST) && (action < CEA_PLAN_FIRST + CE_NUM_PLANS))
		{
			ShowMessage(kCePlans[action - CEA_PLAN_FIRST].mText);
			CFrontEnd::PlaySound(FES_SELECT);
			return;
		}
		if (const int level = BonusLevel(action))
		{
			// The playable demo's level, the two co-op maps the Multiplayer grid leaves out (two players on one PC) or one
			// of the original's five Race Challenges (levels 901-905), played outside every career: nothing they do is
			// kept, and the front end comes back to this list with the level's row highlighted (ce_bonus.cpp, patch 0116).
			Leave();
			if (!PortCeBonusStart(level))
				ShowMessage(level >= 901 ? "The race could not be started." : "The bonus level could not be started.");
			return;
		}
		switch (action)
		{
		case CEA_PAGE_BONUS:
			OpenPage(CEP_BONUS);
			break;
		case CEA_PAGE_SETTINGS:
			OpenPage(CEP_SETTINGS);
			break;
		case CEA_PAGE_CHEATS:
			OpenPage(CEP_CHEATS);
			break;
		case CEA_PAGE_HELP:
			OpenPage(CEP_HELP);
			break;
		case CEA_PLANS:
			OpenPage(CEP_PLANS);
			break;
		case CEA_TIPS:
			ShowMessage(kControlsTips);
			break;
		case CEA_ABOUT:
		{
			const char *build = CE_BuildName();
			snprintf(text, sizeof(text), "Onslaught CE V%s%s%s\n\nBattle Engine Aquila (Lost Toys, 2003), rebuilt "
					 "from the game's source code to run natively on today's computers. It plays the game files of your "
					 "own copy of the game.\n\nAn unofficial fan project, not affiliated with or endorsed by the game's "
					 "developer, publisher or rights holders. Free software under the GNU General Public License, version 3, with no "
					 "warranty.\n\nConsole: press the grave key (left of 1) in a solo level, then type help.\n\nSource code, new versions and problem reports:\n"
					 CE_LINKS, CE_VERSION,
					 *build ? ", build " : "", build);
			ShowMessage(text);
			break;
		}
		case CEA_UPDATES:
		{
			char folder[1024];
			FolderText(folder, sizeof(folder), "");
			snprintf(text, sizeof(text), "%s\n\nAn update goes into the versions folder inside this Onslaught CE folder; "
					 "careers and settings are not changed. To go back to the version before it, run the launcher with "
					 "--rollback (see Updates in the README).\n\n%s ce.ini is in:\n%s\n\nNever start a 0.1.0-0.1.6 launcher.",
					 CE_SettingGet("update_check", 1) ? "Update checks are on: Onslaught CE looks for a signed update when it "
					 "starts and asks before installing it." : "Update checks are off (a line in ce.ini turns them off).",
					 // The game font has no underscore, so the setting's name is left to the README.
					 CE_SettingGet("update_check", 1) ? "To turn them off, add the line given under Updates in the README to "
					 "ce.ini." : "To turn them on, delete that line from ce.ini.", folder);
			ShowMessage(text);
			break;
		}
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
		FRONTEND.DrawTitleBar(mMenu ? mMenu->PageTitle() : title, transition, dest);
		FRONTEND.GetCommonPage()->RenderHelpText(1, transition);
		FRONTEND.GetCommonPage()->RenderSmallBEALogo(RangeTransition(transition, 0.75f, 1.f));
	}

	void	TransitionNotification(EFrontEndPage from)
	{
		if (!mMenu)
			mMenu = new (MEMTYPE_MENUITEM) CCeMenu();
		CPauseMenu::mOptionsPage = true;
		mMenu->Activate(0);
		// A bonus level has just ended: the page opens on the Bonus Levels list, that level's row highlighted (ce_bonus.cpp).
		if (int level = PortCeBonusTakeReturn())
			mMenu->ShowBonus(level);
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

//*********************************************************************************
// The pause menu's Photo Mode item (patch 0138, PauseMenu.cpp): photo mode on or off as F11 does it, so a gamepad can reach
// it. The item is in the menu only where photo mode can be used, a solo level (PortCePhotoAvailable: F11's own rule), and
// takes no row anywhere else, so a multiplayer, network or demo pause menu is the game's own. Its text says what choosing
// it does.
class CCePhotoItem : public CMenuItem
{
public:
	CCePhotoItem() : CMenuItem((EPCText)PORT_CE_PHOTO_PCTEXT, 3) {}

	virtual WCHAR	*GetText()
	{
		static WCHAR off[] = L"Photo Mode", on[] = L"End Photo Mode";
		return PortCePhotoOn() ? on : off;
	}

	virtual BOOL	IsEnabled() { return PortCePhotoAvailable() ? TRUE : FALSE; }
	virtual int		GetHeight() { return PortCePhotoAvailable() ? 20 : 0; }

	virtual void	Render(float x, float y, BOOL selected)
	{
		if (PortCePhotoAvailable())
			CMenuItem::Render(x, y, selected);
	}
};

CMenuItem	*PortCePhotoItem()
{
	return new (MEMTYPE_MENUITEM) CCePhotoItem();
}

#endif
