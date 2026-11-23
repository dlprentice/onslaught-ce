// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_frontend.cpp: the CE layer's front-end additions (patch 0050): an "Onslaught CE" action on the main menu
// opening a page of the game's own options-style lists: Bonus Levels, CE Settings, Cheats (switches kept in ce.ini, patch
// 0051 at IsCheatActive, that work as the cheat codes do in a career's name), Help & Info (Controls & Tips, What's Coming,
// Updates & Rollback, About, Special Thanks), Report a Problem and the Museum (a page of its own: a hall of the game's
// machines, ce_museum_visit.h, and the Goodies gallery as a visitor, ce_museum.h, patch 0199). Every row has a hint under the list. The
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
#include "ce_museum.h"
#include "ce_photo.h"
#include "ce_editor.h"
#include "ce_dodge_assist.h"
#include "ce_dodge_binding.h"
#include "ce_tutorial_pad.h"
#include "ce_extras.h"
#include "ce_modspage.h"
#include "ce_catalogue_menu.h"
#include "ce_catalogue_preview.h"
#include "ce_catalogue_sounds.h"
extern "C" int CE_SettingGetBool(const char *, int);
#include "hud_scale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MEMTYPE_MENUITEM	((EMemoryType)0x80)		// the menu items' memory type (MenuItem.cpp, PauseMenu.cpp)
#define PORT_CE_PHOTO_PCTEXT	0x4346				// the pause menu's Photo Mode item (patch 0138, PauseMenu.cpp)
#define PORT_CE_ACTION		9						// the main menu's CE action (patch 0050, FEPMain.cpp)
#define FEP_CE				FEP_LANGUAGE_TEST		// the CE page's slot

bool			PortCeLayer();						// port/platform/common/ce.cpp
bool			PortCeExperimental(const char *mode);	// ce.cpp: one of the unfinished features (the ce.ini lines under [Experimental]), read once; the pages below offer each only on yes
void			PortCeExperimentalBlock();			// ce.cpp: adds the [Experimental] lines to ce.ini when it has none
bool			PortModsPlatformOff();				// port/platform/common/mods.cpp: true on platforms without the mod loader (Windows)
extern "C" int	CE_OpenReportPage(void);
extern "C" int	CE_SettingIs(const char *key, const char *value);
extern "C" int	CE_SettingLauncherIs(const char *key, const char *value);
extern "C" int	CE_SettingGetText(const char *key, char *out, int n);
extern "C" int	CE_SettingGetBool(const char *key, int def);
extern "C" int	CE_SettingSetText(const char *key, const char *value);
extern "C" int	CE_TrySettingGet(const char *key, int def, int *out);
extern "C" int	CE_SettingSet(const char *key, int value);
extern "C" int	CE_CheatOn(int n);
extern "C" int	CE_CheatSet(int n, int on);
extern "C" int	CE_CheatReady(int n);
extern "C" int	CE_CheatRestartRequired(int n);
extern "C" void CE_MaladimRequireRestart(void);
extern "C" int	CE_UserDirForDialog(char *out, int n, int (*shown)(int));
extern "C" const char *CE_BuildName(void);
extern "C" bool	PortCeMuseumOpen(void);				// the Goodies Gallery row (below, near PortCeOpenPage)
// ce_museum_visit.h: Begin prepares the native visit; this menu dispatches its level only after success.
extern "C" bool PortCeMuseumVisitBegin(void);
extern "C" int PortCeMuseumVisitLevel(void);
extern "C" const char *PortCeMuseumVisitMessage(void);
extern "C" bool PortCeMuseumVisitTakeNotice(const char **sentence);

#define CE_LINKS	"github.com/dlprentice/onslaught-ce"

#ifndef CE_NETWORK_OFF
// The Network game pages (port/game/ce_netpages.cpp), drawn inside this page.
bool	PortCeNetOffered();
bool	PortCeNetActive();
void	PortCeNetOpen();
void	PortCeNetButton(SINT button, float val);
void	PortCeNetProcess();
void	PortCeNetDraw();
void	PortCeNetKeepStartFrame();			// ce_network_wait.cpp
void	PortCeNetShutdown();
bool	PortCeNetOpenOnEntry();
WCHAR	*PortCeNetTitle();					// the network page's own title

#endif
//*********************************************************************************
bool	PortCeMenus()
{
	static int on = -1;
	if (on < 0)
	{
		on = (PortCeLayer() && !CLIPARAMS.mAutoConfigTest && !PLAYABLE_DEMO) ? 1 : 0;
		if (on == 1)
		{
			PortCeExperimentalBlock();		// the [Experimental] lines are added to ce.ini here, once, as the first main menu is built
			static const char *const modes[] = {"map_editor", "museum", "commander", "play_unit"};
			for (const char *mode : modes)
				(void)PortCeExperimental(mode);		// and each is read here and kept
		}
	}
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
	if (!PortCeMenus())
		return rows;
	PortCeModsPageMainMenu();		// the main menu asks this as it is drawn: the Mods page's once-a-start notice (ce_modspage.cpp)
	return rows + 1;
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
// way (a name that starts with a dot; "." and ".." name no folder of their own, and ce_user_dir keeps them as they were
// typed), on Windows, Explorer's address bar: the path is to be typed there, as text on a game screen cannot be copied, and the
// note names AppData as hidden only for a path that goes through it or ends in it (a folder BEA_USER_DIR chose need not; the
// separators are forward slashes, ce_forward_slashes). A path's spaces are
// kNoBreak, so WrapLines never
// breaks "Onslaught CE" at its space. A path longer than kFolderShown characters (a folder BEA_USER_DIR chose, say) is
// shown as "..." and its last ones, and on Windows the note then says that only its end is shown and where all of it can be
// read (the launcher's --report names the folder), as what is shown is not the path to
// type; a hidden folder's name longer than kHiddenShown is shown as its first ones and "...". So every message naming
// the folder still ends on the screen (ShowMessage). dir is as large as CE_UserDirForDialog's own buffer, so the last
// characters shown are the folder's own, however long it is. Returns out.
#define kNoBreak	'\xa0'
static const int	kFolderShown = 160;
#ifndef _WIN32
static const int	kHiddenShown = 24;
#endif

static char	*FolderText(char *out, int n, const char *sub)
{
	char dir[4096], note[160] = "";
	int path = CE_UserDirForDialog(dir, sizeof(dir), Shown);
	if (path)
	{
		for (char *c = dir; *c; c++)
			if (*c == ' ')
				*c = kNoBreak;
		const int length = (int)strlen(dir);
#ifndef _WIN32
		for (const char *dot = strstr(dir, "/."); dot; dot = strstr(dot + 1, "/."))
		{
			const int len = (int)strcspn(dot + 1, "/");
			if ((len == 1) || ((len == 2) && (dot[2] == '.')))
				continue;
			const bool cut = len > kHiddenShown;
			snprintf(note, sizeof(note), "\n(%.*s%s is a hidden folder: in your file manager, press Ctrl+H to show it.)",
					 cut ? kHiddenShown - 3 : len, dot + 1, cut ? "..." : "");
			break;
		}
#else
		bool appdata = !strncmp(dir, "%APPDATA%", 9);
		for (const char *c = dir; *c && !appdata; c++)
			appdata = (c[0] == '/') && ((c[1] | 0x20) == 'a') && ((c[2] | 0x20) == 'p') && ((c[3] | 0x20) == 'p') && ((c[4] | 0x20) == 'd') &&
					  ((c[5] | 0x20) == 'a') && ((c[6] | 0x20) == 't') && ((c[7] | 0x20) == 'a') && ((c[8] == '/') || !c[8]);
		snprintf(note, sizeof(note), "%s", (length > kFolderShown) ?
				 "\n(Only its end is shown. Starting Onslaught CE with --report shows the whole path.)" :
				 appdata ? "\n(To open it, type this into File Explorer's address bar: AppData is a hidden folder.)" :
				 "\n(To open it, type this into File Explorer's address bar.)");
#endif
		if (length > kFolderShown)
		{
			memmove(dir + 3, dir + length - (kFolderShown - 3), size_t(kFolderShown - 3) + 1);
			memcpy(dir, "...", 3);
		}
	}
	if (!*sub)
		snprintf(out, n, "%s%s", dir, note);
	else if (path)
		snprintf(out, n, "%s/%s%s", dir, sub, note);
	else
		snprintf(out, n, "the %s folder in %s", sub, dir);
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

// Set by ShowMessage while the box it made is up: the front end then lets Esc and gamepad B close it as Select does
// (PortCeMessageButton, patch 0050). CFEPCe::Process clears it once the box is gone.
static bool	sCeBox = false;

// The game's message box with a Continue button, as the save page uses for its errors; '\n' starts a line. y is its centre. The
// page's list is not drawn while the box is up (the box is translucent), so the box floats mid-page. A message of three lines or
// fewer is the original's 400 pixels wide, a longer one 440 (the original's boxes are 300, 350 and 400 wide).
static void	ShowMessage(const char *text, float y = 240.f, float top_least = 122.f)
{
	static WCHAR msg[2048];
	static WCHAR blank[] = L" ";
	static SWordWrapFill probe;
	Widen(msg, 2048, text);
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	// every CE message is written in the font's characters (FolderText); one that is not would show a box for it
	for (WCHAR *c = msg; *c; c++)
		if ((*c != '\n') && (*c != (WCHAR)(unsigned char)kNoBreak) && !font->CanDisplay(*c))
		{
			fprintf(stderr, PORT_LOG_PREFIX "message character 0x%02x is not in the game's font\n", (unsigned)*c);
			break;
		}
	const float width = (WrapLines(&probe, msg, font, 400.f - 16.f) <= 3) ? 400.f : 440.f;
	FEMESSBOX.Kill();
	FEMESSBOX.Create(320.f, y, width, FO(100), blank, font, 0, 10, FALSE, FEMB_OPTION_A_CONTINUE, 0);
	// Create's own layout (CFEMessBox::Create): the lines, then a line of the normal font for the button
	for (int l = 0; l < 40; l++)
		FEMESSBOX.mLines.c[l][0] = 0;
	FEMESSBOX.mNumLines = WrapLines(&FEMESSBOX.mLines, msg, font, width - 16.f);
	FEMESSBOX.mHeight = float(FEMESSBOX.mLineHeight) * FEMESSBOX.mNumLines + 16.f + float(PLATFORM.Font(FONT_NORMAL)->GetHeight());
	// centred on y, or lower for a tall message so it starts below the page's title bar and its emblem (at y 122); a message that
	// would then pass the foot of the screen (a long folder) is raised to end at y 476, over the title bar (the box is drawn
	// after the page), so its button stays in view
	const float top = y - FEMESSBOX.mHeight * 0.5f;
	float placed = (top < top_least) ? top_least : top;
	if (placed + FEMESSBOX.mHeight > 476.f)
		placed = 476.f - FEMESSBOX.mHeight;
	if (placed < 0.f)
		placed = 0.f;
	if (placed != top)
		FEMESSBOX.mY = placed + FEMESSBOX.mHeight * 0.5f;
	FEMESSBOX.SetOnDismiss(FEP_NONE, 0);
	sCeBox = true;
}

// The front end's redirect hands a button to the message box while it is up (CFrontEnd::ButtonPressed, patch 0050 calls this): on the
// Onslaught CE page, for a box ShowMessage made, Esc and gamepad B (BUTTON_FRONTEND_MENU_BACK) are the box's Select, so they close it as
// OK does. A box of the original's (a Yes/No question, a one-line notice) and any other page keep the original's input.
SINT	PortCeMessageButton(SINT button, EFrontEndPage page)
{
	if ((button == BUTTON_FRONTEND_MENU_BACK) && (page == FEP_CE) && sCeBox && PortCeMenus())
		return BUTTON_FRONTEND_MENU_SELECT;
	return button;
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

// An installed Map Editor map row: asks the game's level text table for its localized mission name with level code
// (2.01 keeps its own prototype title, since retail aliases 201 to 200's text).
class CCeEditorMapItem : public CCeMenuItem
{
public:
	CCeEditorMapItem(const WCHAR *fallback, int action, int level) : CCeMenuItem(fallback, action), mLevel(level) { mShown[0] = 0; }

	virtual WCHAR	*GetText()
	{
		if (mLevel == 201)
			return CCeMenuItem::GetText();
		const WCHAR *name = CFrontEndText::GetLevelName(mLevel);
		if (!name || !*name)
			return CCeMenuItem::GetText();
		int len = 0;
		while (name[len] && len < 63)
		{
			mShown[len] = name[len];
			++len;
		}
		mShown[len] = 0;
		return mShown;
	}

	int		mLevel;
	WCHAR	mShown[64];
};

// A Yes/No switch with its own label (the game's Yes and No, as its other switches).
// Empty catalogue slots occupy no space and cannot take the keyboard or mouse focus.
class CCeCatalogueRow : public CCeMenuItem
{
public:
	CCeCatalogueRow(int action) : CCeMenuItem(mLabel, action), mShown(false) { mLabel[0] = 0; }
	void Bind(const char *text) { Widen(mLabel, int(sizeof(mLabel)/sizeof(*mLabel)), text); mShown = *text != 0; }
	void BindWide(const WCHAR *text) { wcsncpy(mLabel, text, sizeof(mLabel)/sizeof(*mLabel) - 1); mLabel[sizeof(mLabel)/sizeof(*mLabel) - 1] = 0; mShown = *text != 0; }
	virtual BOOL IsEnabled() { return mShown; }
	virtual int GetHeight() { return mShown ? 20 : 0; }
	virtual void Render(float x, float y, BOOL selected) { if (mShown) CMenuItem::Render(x,y,selected); }
	WCHAR mLabel[100];
	bool mShown;
	// The items page: the twelve item rows, the page turn, then Find. A row's text is nearly as wide as the Service
	// Record's list box gives a row (ce_scroll_list.h: 370), centred, and kept clear of the emblem at the top left.
	enum { kFindRow = CE_CATALOGUE_SLOTS + 1, kRowWidth = 340 };
	static int TextWidth(const WCHAR *text)
	{
		WCHAR copy[100];
		wcsncpy(copy, text, 99); copy[99] = 0;
		SIZE size;
		PLATFORM.Font(FONT_SMALL)->GetTextExtent(copy, &size);
		return int(size.cx);
	}
	// Cut to the width and mark the cut as the list box does, then end with tail (" (tag)") when there is one.
	static void Fit(WCHAR *out, int n, const char *text, const char *tail)
	{
		WCHAR end[48];
		Widen(end, int(sizeof(end) / sizeof(*end)), tail ? tail : "");
		const int room = kRowWidth - (*end ? TextWidth(end) : 0);
		Widen(out, n, text);
		int length = int(wcslen(out));
		while (TextWidth(out) > room && length > 4)
		{
			length--;
			out[length - 3] = '.'; out[length - 2] = '.'; out[length - 1] = '.'; out[length] = 0;
		}
		if (*end && int(wcslen(out) + wcslen(end)) < n)
			wcscat(out, end);
	}
};

// The same native character trap and small-face cursor as the join-code and
// career-name fields. Only this page owns text; gameplay bindings never see it.
class CCeCatalogueField : public CCeCatalogueRow
{
public:
	CCeCatalogueField(int action) : CCeCatalogueRow(action) { Set(""); }
	void Set(const char *value) { snprintf(mValue, sizeof mValue, "%s", value); Refresh(); }
	void Refresh() { char text[44]; snprintf(text, sizeof text, "Name: %s", mValue); Bind(text); }
	void Backspace() { const size_t n = strlen(mValue); if (n) mValue[n-1] = 0; Refresh(); }
	void Add(WORD character)
	{
		if (character == '_') character = ' ';
		const size_t n = strlen(mValue);
		if (n >= CE_CATALOGUE_QUERY_MAX || character < 32 || character > 126 ||
			!PLATFORM.Font(FONT_SMALL)->CanDisplay(character)) return;
		mValue[n] = char(character); mValue[n+1] = 0; Refresh();
	}
	void Render(float x, float y, BOOL selected)
	{
		CCeCatalogueRow::Render(x, y, selected);
		if (selected && fmod(PLATFORM.GetSysTimeFloat(), 1.f) < .5f) {
			CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL); SIZE extent;
			font->GetTextExtent(mLabel, &extent);
			static WCHAR cursor[] = L"I";
			font->DrawTextWithShadow(x + float(extent.cx) * .5f + 1.f, y, 0xff00c0ff, cursor, 0, 0, 1.f, 1.f);
		}
	}
	char mValue[CE_CATALOGUE_QUERY_MAX + 1];
};
template <class A, class B> struct CeSameType { static constexpr bool value = false; };
template <class A> struct CeSameType<A, A> { static constexpr bool value = true; };
void CatalogueKeyTrap(WORD key, KeyEventType event);
pKeyTrapper CatalogueTrap()
{
	static_assert(CeSameType<decltype(&CatalogueKeyTrap), pKeyTrapper>::value, "CatalogueKeyTrap must match pKeyTrapper");
	return &CatalogueKeyTrap;
}
void CatalogueTextEnd() { if (LT.GetKeytrap() == CatalogueTrap()) LT.SetKeytrap(NULL); }

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
				ShowMessage("Your settings could not be read. Restart Onslaught CE to retry this cheat.");
			else
				ShowMessage("Your settings could not be read or saved, so this was not changed.");
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
					ShowMessage("The Maladim switch is off, but the God setting could not be cleared. Check your settings before enabling it again.");
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

//*********************************************************************************
// Under each page's list the page says what the highlighted row does: a line in cream, as the original's Multiplayer page words
// its description, and for some rows a second line in grey (a cheat's original code, or when a setting is used). The texts are in the
// tables below, one hint a row; the list's last item, Back, draws them.
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
	{L"Unlock All Levels:", 1, {L"Every level is open on the Select Level screen. Your own progress is kept.",
	 L"Original code: TURKEY"}},
	{L"Unlock All Goodies:", 0, {L"Every goodie on the Goodies screen is open. Nothing is saved to your career.",
	 L"Original code: MALLOY"}},
	{L"Goodie Hints:", 5, {L"The Goodies screen shows its unlock hints. While on, goodies cannot be opened.",
						   L"Original code: lat\u00eate"}},
	{L"God Mode Option:", PORT_CE_GOD_CHEAT, {L"Adds a God row to Controller Options. God ON: no damage and endless energy.",
											  L"Original code: Maladim"}},
	{L"Free Camera:", 4, {L"F: free camera, F returns. W/S fly, A/D slide, arrows look. You can still be hit.",
						  L"Original code: Aurore"}},
};
#define CE_NUM_CHEATS	int(sizeof(kCheats) / sizeof(kCheats[0]))
static WCHAR	kCheatsNote[] = L"Cheats apply to every career and are off while recording. Wins still count.";

// Where a hint's second line ends when it is anchored (CCeBack::EXTRA_FOOT): the page's note ends at 449, this a little above it.
static const float	kCeFootEnd = 441.f;

// The page's Back item, the list's last: after drawing itself (the list draws its items in order, so this comes after
// the others) it draws, below the list, the highlighted row's hint (hints[row], stride bytes apart, count of them: Back
// itself has none) and the page's standing note, if it has one, in the small font's normal text with its shadow as the
// options pages draw their rows (the list's disabled grey is for what cannot be chosen). Nothing is drawn while a message
// box is shown over the list. The hint is cream and the rest grey, so that white in the row font keeps meaning "you can choose
// this": a white hint under the list read as two more rows.
class CCeBack : public CMenuItem
{
public:
	// Where a hint's second line (grey) goes: straight under the hint's own lines (EXTRA_FOLLOW), two lines under the hint's first
	// line, the same place on every row of a page whose hints are two lines each (EXTRA_FIXED: the Cheats page's original codes),
	// or ending at the same place above the page's help bar however many lines the hint has (EXTRA_FOOT: CE Settings' "applies" line).
	enum { EXTRA_FOLLOW = 0, EXTRA_FIXED = 1, EXTRA_FOOT = 2 };

	CCeBack(CMenuOptionsList *list, const SCeHint *hints, int stride, int count, const WCHAR *note, int extra = EXTRA_FOLLOW)
		: CMenuItem((DWORD)GI_BACK, 3), mList(list), mHints(hints), mStride(stride), mCount(count), mNote(note), mExtra(extra) {}

	virtual void	Render(float x, float y, BOOL selected)
	{
		CMenuItem::Render(x, y, selected);
		if (FEMESSBOX.BeingDisplayed())
			return;
		CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
		float line = float(font->GetHeight());
		// what the highlighted row does, in cream, then (for a cheat or a setting) its second line, in the note's grey
		if ((mList->mSelected >= 0) && (mList->mSelected < mCount))
		{
			const SCeHint &hint = *(const SCeHint *)((const char *)mHints + mList->mSelected * mStride);
			const float first = y + 20.f + line * 0.5f;
			float ty = first + line * Lines(font, hint.mHelp, first, kCream);
			if (hint.mExtra)
			{
				if ((mExtra == EXTRA_FIXED) && (ty < first + line * 2.f))
					ty = first + line * 2.f;
				if (mExtra == EXTRA_FOOT)
					Lines(font, hint.mExtra, kCeFootEnd, kGrey, true, ty);
				else
					Lines(font, hint.mExtra, ty, kGrey);
			}
		}
		// the note ends a little above the page's help bar (CFEPCommon::RenderHelpText draws it at y 455)
		if (mNote)
			Lines(font, mNote, 449.f, kNoteGrey, true, 0.f, 500.f);
	}

	// 0xa8 on the screen: text colours are authored for a doubling colour stage (StageColour in port/platform/public/DXFont.cpp), so
	// the colour given is half of it. A channel of 0x80 or more doubles past 255, and 0xa8 here would draw the same white as the rows.
	static const DWORD	kGrey = 0xff545454;
	// 0xc4 on the screen: slightly brighter silver-grey for standing footer notes so caveats read cleanly while staying dimmer than white rows.
	static const DWORD	kNoteGrey = 0xff626262;
	// the original's description text (FEPMultiplayer.cpp), 0xfe, 0xde, 0xbe on the screen: cream
	static const DWORD	kCream = 0xff7f6f5f;
	// The text in lines wrapped to the space between the page's side borders, each centred on the page, from y down (or
	// ending at y, when up, and not above floor); returns the number of lines.
	static int	Lines(CDXBitmapFont *font, const WCHAR *text, float y, DWORD colour, bool up = false, float floor = 0.f, float width = 420.f)
	{
		static SWordWrapFill fill;
		static WCHAR buf[400];
		static WCHAR wrapped[40][100];
		int total = 0;
		const WCHAR *p = text;
		while (total < 40)
		{
			const WCHAR *nl = wcschr(p, L'\n');
			size_t len = nl ? size_t(nl - p) : wcslen(p);
			if (len > 399)
				len = 399;
			memcpy(buf, p, len * sizeof(WCHAR));
			buf[len] = 0;
			int n = font->WordWrap(&fill, buf, width);
			for (int l = 0; (l < n) && (total < 40); l++)
			{
				wcsncpy(wrapped[total], fill.c[l], 99);
				wrapped[total][99] = 0;
				total++;
			}
			if (!nl)
				break;
			p = nl + 1;
		}
		float line = float(font->GetHeight());
		if (up)
		{
			y -= line * total;
			if (y < floor)
				y = floor;
		}
		for (int l = 0; l < total; l++)
		{
			SIZE size;
			font->GetTextExtent(wrapped[l], &size);
			font->DrawTextWithShadow(320.f - float(size.cx / 2), y + line * l, colour, wrapped[l], 0, 0, 1.f, 1.f);
		}
		return total;
	}

	CMenuOptionsList	*mList;
	const SCeHint		*mHints;
	int					mStride, mCount;
	const WCHAR			*mNote;
	int					mExtra;
};

//*********************************************************************************
// The CE page's menu: a CPauseMenu without the pause menu's lists (patch 0050's constructor), so the list draws and
// takes input exactly as the options page's do.
static const char kSpecialThanks[] =
	"Lost Toys - original game.\n"
	"Stuart Gillam\n"
	"Help, insight, guidance.\n\n"
	"Early testers: Jeppi,\n"
	"BermudaMaster and others.\n\n"
	"Onslaught CE contributors,\n"
	"including work made with\ntheir own coding agents.\n"
	"BEA community and\npreservation projects.\n\n"
	"The original game's credits\nare in Options > Credits.";

// What a row does when chosen. The first values are the old page's, kept; the plans are CEA_PLAN_FIRST + their index, the
// five Race Challenges CEA_RACE_FIRST + 0..4 (levels 901-905, ce_bonus.cpp), the Goodies Gallery CEA_MUSEUM (ce_museum.h), the pages
// CEA_PAGE_*.
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
	CEA_MUSEUM = 110,
	CEA_MUSEUM_VISIT,
	CEA_EXTRAS,
	CEA_MODS,
	CEA_CARRIER_CLASH,
	CEA_CATALOGUE = 300, CEA_CAT_MODELS, CEA_CAT_TURN = CEA_CAT_MODELS + CE_CATALOGUE_GROUPS, CEA_CAT_ENTRY,
	CEA_CAT_FIND = CEA_CAT_ENTRY + CE_CATALOGUE_SLOTS, CEA_CAT_CLEAR, CEA_CAT_FIELD, CEA_CAT_APPLY, CEA_CAT_ERASE,
	CEA_PAGE_BONUS = 120,
	CEA_PAGE_SETTINGS,
	CEA_PAGE_CHEATS,
	CEA_PAGE_HELP,
	CEA_PAGE_EDITOR,
	CEA_PAGE_MUSEUM = 126,
	CEA_PAGE_LABS = 127,
	CEA_EDITOR_FIRST = 200,
#ifdef CE_NETWORK_OFF
	CEA_TIPS = 125
#else
	CEA_TIPS = 125,
	CEA_NETWORK = 90
#endif
};
#define CE_NUM_RACES	5

// The pages (lists of rows) and where Back goes from each: the hub, its pages, What's Coming under Help & Info, Catalogue and Experimental Features.
enum ECePage
{
	CEP_HUB = 0,
	CEP_BONUS,
	CEP_SETTINGS,
	CEP_CHEATS,
	CEP_HELP,
	CEP_PLANS,
	CEP_EDITOR,
	CEP_MUSEUM,
	CEP_CATALOGUE,
	CEP_CATALOGUE_ITEMS,
	CEP_CATALOGUE_FIND,
	CEP_LABS,
	CEP_COUNT
};
static const int	kCeParent[CEP_COUNT] = {-1, CEP_HUB, CEP_HUB, CEP_HUB, CEP_HUB, CEP_HELP, CEP_HUB, CEP_HUB, CEP_HUB, CEP_CATALOGUE, CEP_CATALOGUE_ITEMS, CEP_HUB};
// The title bar's text while one of Help & Info's boxes is up, in the order of CEBT_*.
enum { CEBT_TIPS = 1, CEBT_UPDATES, CEBT_ABOUT, CEBT_THANKS };
static WCHAR		kBoxTitles[4][24] = {L"Controls & Tips", L"Updates", L"About Onslaught CE", L"Special Thanks"};
static WCHAR		kCeTitles[CEP_COUNT][24] = {L"Onslaught CE", L"Bonus Levels", L"CE Settings", L"Cheats", L"Help & Info",
												L"What's Coming", L"Map Editor", L"Museum", L"Catalogue", L"Catalogue", L"Find by name",
												L"Experimental Features"};

// A row: its label, what it does, and its hint (the font has no underscore, so a setting's
// ce.ini name is left to the README).
struct SCeRow
{
	const WCHAR	*mLabel;
	int			mAction;
	SCeHint		mHint;
};

static const SCeRow	kHubRows[] = {
#ifndef CE_NETWORK_OFF
	{L"Network Game", CEA_NETWORK, {L"Play a mission together with a friend on another PC.", 0}},
#endif
	{L"Bonus Levels", CEA_PAGE_BONUS, {L"Extra levels, outside your career.", 0}},
	{L"Extras", CEA_EXTRAS, {L"Service record, goodies guide, music player, lore and career backups.", 0}},
	{L"Mods", CEA_MODS, {L"Turn the mods in your mods folder on or off, and put them in order.", 0}},
	{L"Catalogue", CEA_CATALOGUE, {L"Browse the models, textures and sounds installed with the game.", 0}},
	{L"CE Settings", CEA_PAGE_SETTINGS, {L"The options Onslaught CE adds to the game.", 0}},
	{L"Cheats", CEA_PAGE_CHEATS, {L"The original game's cheats as switches. They apply to every career.", 0}},
	{L"Experimental Features", CEA_PAGE_LABS, {L"Turn unfinished CE features on or off. Changes apply on next start.", 0}},
	{L"Help & Info", CEA_PAGE_HELP, {L"Controls and tips, what is coming, updates, about and thanks.", 0}},
	{L"Report a Problem", CEA_REPORT, {L"Opens a problem report in your browser. Nothing is sent until you submit it.", 0}},
	{L"Map Editor", CEA_PAGE_EDITOR, {L"Move a level's objects and play your changes.", 0}},
	{L"Museum", CEA_PAGE_MUSEUM, {L"See machines, art and films.", L"Your career is not changed."}},
};

// The Museum page (the hub's one museum row): a hall to walk in and the Goodies wall for a visitor. Each is a way into a visit that leaves the career as it
// was; the page is reached only through its hub row, which is offered only with the museum switched on.
static const SCeRow	kMuseumRows[] = {
	{L"Units", CEA_MUSEUM_VISIT, {L"Walk among the game's machines.", L"Your career is not changed."}},
	{L"Art and Movies", CEA_MUSEUM, {L"Pictures, models and movies from the game.", L"Your career is not changed."}},
};
static const int kMuseumRowCount = int(sizeof(kMuseumRows) / sizeof(kMuseumRows[0]));

// The first native authoring slice offers a bounded set of installed single-player maps.
// A missing map or unsupported placement set is refused during load and returns here without career writes.
static const int kEditorLevels[] = {221, 100, 110, 201};
static const SCeRow kEditorRows[] = {
    {L"Map 2.21", CEA_EDITOR_FIRST + 0, {L"Move objects and play your changes.", L"Your careers are not changed."}},
    {L"Map 1.00", CEA_EDITOR_FIRST + 1, {L"Move objects and play your changes.", L"Your careers are not changed."}},
    {L"Map 1.10", CEA_EDITOR_FIRST + 2, {L"Move objects and play your changes.", L"Your careers are not changed."}},
    {L"2.01 - Aquila Prototype", CEA_EDITOR_FIRST + 3, {L"Move objects and play your changes.", L"Your careers are not changed."}},
};
static const int kEditorMapCount = int(sizeof(kEditorLevels) / sizeof(kEditorLevels[0]));

// The rows and pages that are unfinished features: offered, and acted on, only with the feature's ce.ini line set to 1 under [Experimental]
// (PortCeExperimental): map_editor for the Map Editor and its map pages, museum for the Museum and its two ways in.
static const char	*ExperimentalMode(int action)
{
	if ((action == CEA_PAGE_EDITOR) || ((action >= CEA_EDITOR_FIRST) && (action < CEA_EDITOR_FIRST + kEditorMapCount)))
		return "map_editor";
	if ((action == CEA_MUSEUM) || (action == CEA_MUSEUM_VISIT) || (action == CEA_PAGE_MUSEUM))
		return "museum";
	return NULL;
}

// Optional pages own their menu entry and lifetime; ordinary careers keep their normal flow.
static bool	ExperimentalOffered(int action)
{
	if (action == CEA_EXTRAS)
		return PortCeExtrasOffered();
	if (action >= CEA_CATALOGUE && action <= CEA_CAT_ERASE)
	{
		static const bool catalogue = PortCeMenus() && (CE_SettingGetBool("catalogue", 1) == 1);
		return catalogue;
	}
	if (action == CEA_MODS)
		return PortCeModsPageOffered();
	const char *mode = ExperimentalMode(action);
	return !mode || PortCeExperimental(mode);
}

// Levels 201, 856, Carrier Clash (archive 742) and 901-905 (ce_bonus.cpp), in the order of the list; a bonus level returns here, its row
// highlighted. The original ships the building-defence scenario twice (levels 856 and 858 are the same map), so the list has it once.
static const SCeRow	kBonusRows[] = {
	{L"Aquila Prototype", CEA_BONUS, {L"1 player. Interception, from the original's demo.", 0}},
	{L"Building Defence", CEA_COOP, {L"2 players on this PC, in split screen.", 0}},
	{L"Carrier Clash", CEA_CARRIER_CLASH, {L"1 player. Venturer vs. Fenrir over open seas.", 0}},
	{L"Race Challenge 1", CEA_RACE_FIRST + 0, {L"1 player. Three laps against the clock.", 0}},
	{L"Race Challenge 2", CEA_RACE_FIRST + 1, {L"1 player. Three laps against the clock.", 0}},
	{L"Race Challenge 3", CEA_RACE_FIRST + 2, {L"1 player. Three laps against the clock.", 0}},
	{L"Race Challenge 4", CEA_RACE_FIRST + 3, {L"1 player. Three laps against the clock.", 0}},
	{L"Race Challenge 5", CEA_RACE_FIRST + 4, {L"1 player. Three laps against the clock.", 0}},
};
static WCHAR	kBonusNote[] = L"Bonus levels do not change your career.";
#define CE_NUM_BONUS	int(sizeof(kBonusRows) / sizeof(kBonusRows[0]))

// CE Settings: a row for each ce.ini line the page sets, in the page's order, one table for the labels, the choices, the hints and
// the defaults. A row is a choice among the texts its line can hold (SCeChoice: what the page shows, and the value written, which
// is exactly what the game's reader of that line tests for), or, with no choices, a line the page only shows (Your Music). The
// game reads each line where it uses it (the consumers: hud_scale.cpp, ce_debrief.cpp, ce_rumble.cpp, ce_commander.cpp,
// ce_palette.cpp, ce_music.cpp, widescreen.cpp and the launcher), and a line it cannot use has the default, which the row then shows. The
// game's readers take a line whole (case and surrounding blanks ignored, a line twice unusable); update_check and record_sessions are the
// launcher's, which reads the first line and compares its text exactly, so those rows are looked up that way (mLauncher). A test
// (ce_settings_tests.py) reads the table and the consumers' own reads and checks that the keys, values and defaults agree, and runs the
// lookup against the actual readers, the launcher's own included. The
// hint's grey line says when a change is used, which is when the consumer reads the line: not before the next level for the HUD
// (hud_scale.cpp asks again then); for the console, each time it opens (PortCePaletteOpen), while the developer information text keeps
// the colour it read first (PortCeDevStatsGreen); else at the next start, as each reads its line once.
struct SCeChoice
{
	const WCHAR	*mLabel;
	const char	*mValue;
};

// What the page does after a good write, besides showing the choice.
enum ECeAfter
{
	CEAF_NONE = 0,
	CEAF_HUD,								// the HUD asks for its two lines again, so the next level uses the change
	CEAF_RECORD								// the dialog that says what session recording is and where its tapes go
};

struct SCeSetting
{
	const WCHAR		*mLabel;				// with its colon, and no space after it: the list draws the choice from its own gap
											// (CMenuMenuItem::RenderList right-aligns the label at x 320 and starts the choice at 322)
	const char		*mKey;					// the line's name in ce.ini
	const SCeChoice	*mChoices;				// NULL for a line the page only shows
	int				mCount;
	int				mDefault;				// the choice that applies, and shows, when ce.ini has no line the game can use
	SCeHint			mHint;					// what it does in white, when a change is used in grey
	int				mAfter;
	int				mLauncher;				// 1: the launcher reads the line, in its own way (CE_SettingLauncherIs), not the game
};

#define CE_CHOICES(array)	array, int(sizeof(array) / sizeof(array[0]))

static const SCeChoice	kNoYes[] = {{L"No", "0"}, {L"Yes", "1"}};
static const SCeChoice	kHudSizes[] = {{L"Original", "0"}, {L"Larger", "1"}};
static const SCeChoice	kHudLayouts[] = {{L"Corners", "1"}, {L"4:3 Columns", "0"}};
static const SCeChoice	kMusicOrders[] = {{L"In Order", "linear"}, {L"Shuffle", "random"}, {L"One Track", "single"}};
static const SCeChoice	kConsoleTexts[] = {{L"Yellow", "yellow"}, {L"Green", "green"}};

static const WCHAR		kNextLevel[] = L"Applies from the next level.";
static const WCHAR		kNextStart[] = L"Applies the next time you start the game.";
static const WCHAR		kNextConsole[] = L"Console: when it next opens. Developer text: from the next start.";
static const WCHAR		kNextRead[] = L"Set in ce.ini. Read when the game starts.";

static const SCeSetting	kSettings[] = {
	{L"HUD Size:", "hud_scale", CE_CHOICES(kHudSizes), 1,
	 {L"Larger scales the HUD up on big screens. Original keeps it at its fixed size.", kNextLevel}, CEAF_HUD, 0},
	{L"HUD Layout:", "hud_anchor", CE_CHOICES(kHudLayouts), 0,
	 {L"Wide screens: radar and readouts in the screen's corners, or nearer the middle.", kNextLevel}, CEAF_HUD, 0},
	{L"Sharper Text:", "sharp_ui", CE_CHOICES(kNoYes), 0,
	 {L"Experimental. Sharper large text on wide screens.", kNextStart}, CEAF_NONE, 0},
	{L"End-of-Level Stats:", "debrief_stats", CE_CHOICES(kNoYes), 1,
	 {L"Shows your mission statistics on the victory and defeat screens.", kNextStart}, CEAF_NONE, 0},
	{L"Controller Rumble:", "rumble", CE_CHOICES(kNoYes), 1,
	 {L"Rumbles a gamepad when your Battle Engine is hit or shaken. Not flight sticks.", kNextStart}, CEAF_NONE, 0},
	{L"Dodge Button:", "dodge_button", CE_CHOICES(kNoYes), 1,
	 {L"Ground: hold Dodge and move. Pick a key in Controller Options.", kNextLevel}, CEAF_NONE, 0},
	{L"Commander Map:", "commander", CE_CHOICES(kNoYes), 0,
	 {L"Live squad view for solo play. Pick a key in Controller Options.", kNextStart}, CEAF_NONE, 0},
	{L"Your Music:", "music_dir", NULL, 0, 0,
	 {L"Your own music's folder. Select to see it.", kNextRead}, CEAF_NONE, 0},
	{L"Music Order:", "music_playtype", CE_CHOICES(kMusicOrders), 0,
	 {L"How your own music plays: in order, shuffled, or one track and then quiet.", kNextStart}, CEAF_NONE, 0},
	{L"Developer Information:", "dev_stats", CE_CHOICES(kNoYes), 0,
	 {L"Shows the original developers' text at the top left of every level.", kNextStart}, CEAF_NONE, 0},
	{L"Console Text:", "console_theme", CE_CHOICES(kConsoleTexts), 0,
	 {L"Colour of the console and developer text.", kNextConsole}, CEAF_NONE, 0},
	{L"Check for Updates at Start:", "update_check", CE_CHOICES(kNoYes), 1,
	 {L"Looks for a new version when the game starts, and asks before installing it.", kNextStart}, CEAF_NONE, 1},
	{L"Session Recording:", "record_sessions", CE_CHOICES(kNoYes), 0,
	 {L"Records your play for a problem report. Each start then asks whether to record.", kNextStart}, CEAF_RECORD, 1},
};
#define CE_NUM_SETTINGS	int(sizeof(kSettings) / sizeof(kSettings[0]))

// Experimental Features (Labs): On/Off switches that write 1 or 0 directly to ce.ini via CE_SettingSet,
// keeping the [Experimental] block in place without requiring hand edits. Changes take effect on next restart.
struct SCeLab
{
	const WCHAR	*mLabel;
	const char	*mKey;
	SCeHint		mHint;
	bool		mNeedsMods;
};

static const SCeLab	kLabs[] = {
	{L"Commander Mode:", "commander",
	 {L"Live squad view and orders in solo levels.", 0}, false},
	{L"Play Any Unit:", "play_unit",
	 {L"Pilot any friendly unit in solo levels.", 0}, false},
	{L"Grand Museum:", "museum",
	 {L"Adds the Museum for units, art and films.", 0}, false},
	{L"Map Editor:", "map_editor",
	 {L"Adds the Map Editor to edit and play maps.", 0}, false},
	{L"Mods Menu:", "mods_menu",
	 {L"Adds the Mods page to manage your mods.", 0}, true},
};
#define CE_NUM_LABS	int(sizeof(kLabs) / sizeof(kLabs[0]))
static WCHAR		kLabsNote[] = L"Unfinished features - changes apply on next restart.";

// Where the lists are centred: MenuOptionsList.cpp's Render centres its items of 20 pixels on y, so with n of them (Back among them)
// the first is at y - 10n - 15 and Back, the last, at y + 10n - 35, and Back draws the hint from 28 pixels below its top. The title
// bar ends near 104 and the help bar starts at 455 (the hints end by 449). The pages of up to ten items are centred on 300, as before;
// the hub, which can hold up to thirteen items with every optional and experimental row on, on 275; CE Settings, with fourteen, on 260,
// which puts its first row at 105 and leaves room for a hint of three lines under Back (the white lines and the grey one: three end at 441).
static const float		kCeListY = 300.f, kCeSettingsY = 260.f, kCeHubY = 275.f;

static const SCeRow	kHelpRows[] = {
	{L"Controls & Tips", CEA_TIPS, {L"Dodge, zoom, Photo Mode, screenshots, the console, the free camera and the commander view.", 0}},
	{L"What's Coming", CEA_PLANS, {L"Plans and ideas for later versions.", 0}},
	{L"Updates", CEA_UPDATES, {L"How updates work, and how to roll back.", 0}},
	{L"About Onslaught CE", CEA_ABOUT, {L"Version, licence and project links.", 0}},
	{L"Special Thanks", CEA_THANKS, {L"The people behind the game and this port.", 0}},
};

// The Controls & Tips note (no heading: the title bar names it while the box is up): eight lines at most in the game's font (about 47 characters a line in the message box), so the lines
// are broken here rather than left to the wrap. Only what exists: the dodge and the zoom as README's Dodging and Zooming say
// them, F11, F12 and the console (single-player levels), the free camera with its cheat, and the commander view (experimental: it exists only with
// commander=1 under [Experimental] in ce.ini, so the line is shown only when Commander is already on). The font has no backquote, so the console's key is named in words.
// The dodge's two lines name the Dodge Button only where it works now, by the feature's own conditions (ce_dodge_assist.cpp): the
// key while the CE Settings row is On and no control is bound to Left Ctrl, the gamepad button while the row is On and a connected
// gamepad (not a flight stick) has B free. The hint names each available control, using the current bindings; with neither, the note
// gives the double tap alone, which always works.

static const char kTipsRest[] =
	"Zoom: mouse wheel, or the gamepad bumpers.\n"
	"Single-player levels: F11 Photo Mode, F12\nscreenshot, the grave key (left of 1): console.\n"
	"F: free camera, with the Free Camera cheat on.";
// The commander view's line, added to the note only with commander=1 under [Experimental] in ce.ini, that is, only when Commander is on:
// it names the keys and gives no step to take, and says that the view M opens does not pause the battle (0.1.13's M opened the paused
// map, which is now V inside the view). (The Commander Map row of CE Settings is listed under the same condition.)
static const char kControlsTipsCommander[] = "\n%s: commander view, the battle runs on.";
static const char kControlsTipsCommanderMap[] = "\n%s: commander view, the battle runs on. V: map.";

// Whether a connected gamepad can use the Dodge Button now: a gamepad, not a flight stick, with button B bound to no control
// (PortCeDodgeAssistPadFree). An empty slot is no gamepad (PortCeTutorialPadInfo answers 0 for it).
static bool	DodgePadUsable()
{
	for (int pad = 0; (pad < LT.mJoypads) && (pad < MAX_JOYPADS); pad++)
	{
		CeTutorialPadInfo info = {};
		if (LT.mUnknown33474[pad] && PortCeTutorialPadInfo(LT.pJoystick[pad], &info) && (info.kind == CE_TUTORIAL_GAMEPAD) &&
			PortCeDodgeAssistPadFree(pad))
			return true;
	}
	return false;
}

// The note as it reads now. The Dodge Button's row is asked of ce.ini itself, with the feature's own reader and default: the value
// the feature keeps (PortCeDodgeAssistOn) was read in a level and is not asked again in the front end, so it would still say On
// after the row on CE Settings, one page away, was set to Off.
static void	ControlsTips(char *out, int n, bool commander)
{
	const bool on = CE_SettingGetBool("dodge_button", 1) != 0;
    const bool key=on && PortCeDodgeAssistKeyFree(), pad=on && DodgePadUsable();
    char name[32]{},left[32]{},right[32]{},dodge[256]{};
    PortCeDodgeStrafeNames(left,sizeof(left),right,sizeof(right));
    if(key) PortCeDodgeBindingName(PortCeDodgeBindingKey(),name,sizeof(name));
    if(name[0] || pad) {
        snprintf(dodge,sizeof(dodge),"Dodge (Walker): hold %s%s%s.\nPress a movement direction.\n",
            name,name[0] && pad?" or ":"",pad?"gamepad B":"");
    }
    char gesture[160];
    snprintf(gesture,sizeof(gesture),"%s %.18s then %.18s, or %.18s then %.18s.\n",
        name[0] || pad?"Or tap":"Dodge (Walker): tap",left,right,right,left);
    if(n<=0)return;
    out[0]=0;
    char commanderLine[128] = {};
	if (commander && !PortCeActionConflict(CeControlAction::Commander, PortCeActionKey(CeControlAction::Commander)))
	{
		const bool map = CE_SettingGetBool("commander_tactical_map", 0) == 1;
		snprintf(commanderLine, sizeof(commanderLine), map ? kControlsTipsCommanderMap : kControlsTipsCommander, PortCeActionName(CeControlAction::Commander));
	}
	const char *sections[]={dodge,gesture,kTipsRest,commanderLine};
    size_t used=0;
    for(const char *part:sections) {
        size_t len=strlen(part);
        if(len>static_cast<size_t>(n-1)-used) len=static_cast<size_t>(n-1)-used;
        memcpy(out+used,part,len);used+=len;out[used]=0;
    }
}

// These are descriptions, not controls. Each plan is one of three grades: it works today, it is in development, or it is an idea; the
// separate Godot project and early concepts are labelled as such.
struct SCePlan { const WCHAR *mLabel; const char *mText; };
static const SCePlan kCePlans[] = {
#ifdef CE_NETWORK_OFF
	{L"Online Play", "Playing over the Internet is in development and not available yet: hosting and joining games, versus and skirmish, "
	 "between Windows and Linux. Larger matches are an idea. For two players on one PC, use Multiplayer on the Main Menu."},
	{L"Campaign Co-Op", "Playing the campaign together online is planned and not available yet. For two players on one PC, Building "
	 "Defence is in Onslaught CE > Bonus Levels, and the original's Co-Op Mode is in Multiplayer on the Main Menu."},
#else
	{L"Online Play", "Host and Join (Onslaught CE > Network Game) plays the building defence or the tutorial with one other player. It is "
	 "experimental and has not been tested across the Internet. Versus, skirmish, matchmaking and larger matches are not available. For "
	 "two players on one PC, use Multiplayer on the Main Menu."},
	{L"Campaign Co-Op", "Playing the campaign together online is not available yet. Network game offers the building defence and the "
	 "tutorial; local co-op bonus levels are on the Onslaught CE page."},
#endif
	{L"Mods", "The Mods page (Onslaught CE > Mods) lists the mods in your mods folder. Turn each on or off and put them in "
	 "order there; a change applies the next time you start the game. A mod can replace game files such as textures or change a level, "
	 "and each mod file is checked at start. Mods are off in recordings, replays and network games, and on Windows for "
	 "now. Online Battle Engine customization is not available."},
	{L"Map and Mission Tools", "The experimental native Map Editor opens installed scenes inside CE. Select a supported map, "
     "move named placements, undo changes and save or open a CE project. Terrain shaping, new object creation and mission-script editing "
     "are still being developed. Saved projects refer to your installed game resources; no original map data is overwritten."},
	{L"HUD and Display", "On wide screens the HUD's size and placement, and Sharper Text, are in CE Settings today. Window mode is in "
	 "Video Options. More HUD and display options are planned."},
	{L"Controls and Tutorial", "More help with dodging and zooming, more control options and better gamepad support are planned. "
	 "Gamepads already work in single-player levels, and the tutorial's dodge and zoom tips name your own controls (English only)."},
	{L"CE Features in Recordings", "While a session is recorded the menus are the original's, gamepads are not recorded and most "
	 "Onslaught CE additions are off. Recording more of them is an idea."},
	{L"Full Photo Mode", "In single-player levels, F11 hides the HUD and F12 takes a screenshot. "
	 "A future Photo Mode could add camera\xa0tools."},
	{L"Other Projects and Ideas", "The separate Godot rebuild is in development. Commander mode (experimental): an optional live view "
	 "and paused map for ordering friendly squads in single-player missions. A second player "
	 "taking the commander seat online is an idea, not available."}
};
#define CE_NUM_PLANS int(sizeof(kCePlans) / sizeof(kCePlans[0]))

static_assert(CEA_PLAN_FIRST > CEA_PLANS, "plan actions must follow base actions");
#ifndef CE_NETWORK_OFF
static_assert(CEA_PLAN_FIRST + CE_NUM_PLANS <= CEA_NETWORK, "plan actions must not collide with CEA_NETWORK");
static_assert(CEA_NETWORK < CEA_RACE_FIRST, "CEA_NETWORK must precede race actions");
#endif
static_assert(CEA_PLAN_FIRST + CE_NUM_PLANS <= CEA_RACE_FIRST, "plan actions must not collide with race actions");
static_assert(CEA_RACE_FIRST + CE_NUM_RACES <= CEA_MUSEUM, "race actions must not collide with museum actions");
static_assert(CEA_MUSEUM < CEA_MUSEUM_VISIT && CEA_MUSEUM_VISIT < CEA_EXTRAS && CEA_EXTRAS < CEA_MODS &&
              CEA_MODS < CEA_CARRIER_CLASH && CEA_CARRIER_CLASH < CEA_PAGE_BONUS,
              "feature actions must not collide with page actions");
static_assert(CEA_PAGE_BONUS < CEA_PAGE_SETTINGS && CEA_PAGE_SETTINGS < CEA_PAGE_CHEATS && CEA_PAGE_CHEATS < CEA_PAGE_HELP &&
              CEA_PAGE_HELP < CEA_PAGE_EDITOR && CEA_PAGE_EDITOR < CEA_TIPS && CEA_TIPS < CEA_PAGE_MUSEUM && CEA_PAGE_MUSEUM < CEA_PAGE_LABS,
              "page and tips actions must not collide");
static_assert(CEA_PAGE_LABS < CEA_EDITOR_FIRST, "page actions must not collide with editor map actions");
static_assert(CEA_EDITOR_FIRST + kEditorMapCount <= CEA_CATALOGUE, "editor map actions must not collide with catalogue actions");
static_assert(CEA_CATALOGUE < CEA_CAT_MODELS && CEA_CAT_MODELS + CE_CATALOGUE_GROUPS <= CEA_CAT_TURN &&
              CEA_CAT_TURN < CEA_CAT_ENTRY && CEA_CAT_ENTRY + CE_CATALOGUE_SLOTS <= CEA_CAT_FIND &&
              CEA_CAT_FIND < CEA_CAT_CLEAR && CEA_CAT_CLEAR < CEA_CAT_FIELD &&
              CEA_CAT_FIELD < CEA_CAT_APPLY && CEA_CAT_APPLY < CEA_CAT_ERASE,
              "catalogue actions must not collide");

// The same topics as the list shows them: in Title Case like the original's pages (kCePlans' own labels are the topics'
// names, kept as they were), each with its hint. A test checks the two tables name the same topics.
static const WCHAR	*const kPlanLabels[CE_NUM_PLANS] = {
	L"Online Play", L"Campaign Co-Op", L"Mods", L"Map and Mission Tools", L"HUD and Display",
	L"Controls and Tutorial", L"CE Features in Recordings", L"Full Photo Mode", L"Other Projects and Ideas"};
static const SCeHint	kPlanHints[CE_NUM_PLANS] = {
#ifdef CE_NETWORK_OFF
	{L"In development. Not available yet.", 0},
	{L"Planned. Not available yet.", 0},
#else
	{L"Experimental Host and Join for two players.", 0},
	{L"The tutorial is experimental. Campaign co-op is planned.", 0},
#endif
	{L"Mods are on the Mods page. Battle Engine customization is an idea.", 0},
	{L"Experimental Map Editor inside CE; mission tools remain planned.", 0},
	{L"Some of it works today. More is planned.", 0},
	{L"Some of it works today. More is planned.", 0},
	{L"Recordings use the original's menus today.", 0},
	{L"An idea. F11 and F12 work today.", 0},
	{L"The Godot rebuild is in development. Commander mode is experimental.", 0},
};

// Without a feature's ce.ini line the Help pages do not describe it as a thing this version has: without commander=1 Controls & Tips
// drops the commander view's line (kControlsTipsCommander) and says so in its hint, and the two plans that name the Map Editor (map_editor)
// and Commander (commander) say that they are in development. Indexes are kCePlans'.
static const WCHAR	kControlsHintPlain[] = L"Dodge, zoom, Photo Mode, screenshots, the console and the free camera.";
enum { CEPL_MODS = 2, CEPL_TOOLS = 3, CEPL_GODOT = 8 };
// The mods entry where the Mods page is not offered (its ce.ini line is off, or the build cannot load a mod): no page is described.
static const char	kPlanModsPlain[] = "Mods are experimental and work on Linux only in this version. Turn on the Mods page in ce.ini, as described in "
									   "the README under Mods. Windows support is planned.";
static const WCHAR	kPlanModsPlainHint[] = L"Available on Linux (see the README). Windows support is planned.";
struct SCePlanPlain { int mIndex; const char *mMode; const char *mText; const WCHAR *mHint; };
static const SCePlanPlain kPlanPlain[] = {
	{CEPL_TOOLS, "map_editor", "A map and mission editor is in development. This version does not offer it.",
	 L"In development. Not available yet."},
	{CEPL_GODOT, "commander", "A separate rebuild of the game in the Godot engine is in development. A map for ordering friendly squads, "
	 "and a second player commanding online, are ideas. This version offers neither.", L"A separate project and ideas. Not included."},
};

// The text a Help & Info plan shows: kCePlans' own, or, with its feature's line off, the plain one.
static const char	*PlanText(int index)
{
	if ((index == CEPL_MODS) && !PortCeModsPageOffered())
		return kPlanModsPlain;
	for (const SCePlanPlain &plain : kPlanPlain)
		if ((plain.mIndex == index) && !PortCeExperimental(plain.mMode))
			return plain.mText;
	return kCePlans[index].mText;
}

bool	PortCeBonusStart(int level);		// ce_bonus.cpp
bool	PortCeBonusStartCarrierClash();		// ce_bonus.cpp: standalone Carrier Clash bonus session (archive 742)
int		PortCeBonusTakeReturn();			// ce_bonus.cpp: the bonus level the front end has just come back from, or 0
int		PortCeBonusTakeModReturn();			// ce_bonus.cpp: the level a mod adds that the front end has just come back from, or 0
extern "C" bool PortCeCarrierClashBegin(void);
extern "C" void PortCeCarrierClashCancel(void);
extern "C" const char *PortCeCarrierClashMessage(void);
extern "C" bool PortCeCarrierClashTakeNotice(const char **sentence);

// The level a Bonus Levels row starts (ce_bonus.cpp), or 0 for any other action.
static int	BonusLevel(int action)
{
	if (action == CEA_BONUS)
		return 201;
	if (action == CEA_COOP)
		return 856;
	if (action == CEA_CARRIER_CLASH)
		return 742;
	if ((action >= CEA_RACE_FIRST) && (action < CEA_RACE_FIRST + CE_NUM_RACES))
		return 901 + action - CEA_RACE_FIRST;
	return 0;
}

//*********************************************************************************
// Session Recording, after a good write: what the choice means. The launcher reads record_sessions from ce.ini as it
// starts the game, which is when it takes effect.
static void	RecordNote(bool on)
{
	char folder[1024], text[2048];
	if (on)
		snprintf(text, sizeof(text), "Session recording starts the next time you start Onslaught CE. Each start then asks whether "
				 "to record.\n\nWhile recording, gamepads and flight sticks do not work, the game runs at 30 frames a second, and "
				 "the Onslaught CE menu and cheats are hidden.\n\nRecordings are saved in:\n%s",
				 FolderText(folder, sizeof(folder), "tapes"));
	else
		snprintf(text, sizeof(text), "Session recording is off from the next time you start Onslaught CE.");
	ShowMessage(text);
}

// A folder path in the characters the font has: a Windows path's backslashes as forward slashes, as CE_UserDirForDialog shows the CE
// folder (ce.cpp), and each other character the font lacks (an underscore, or a byte of a UTF-8 name above 0x7f) as a minus sign.
static void	InFontCharacters(char *text)
{
	for (; *text; text++)
		if (*text == '\\')
			*text = '/';
		else if (((unsigned char)*text > 0x7f) || !Shown((unsigned char)*text))
			*text = '-';
}

// The name Your Music shows for the folder path: its last part (a path ending in a separator loses it first), in the font's
// characters, 24 at most, or its first 21 and "..." when it is longer (the list then fits what is still too wide to its row,
// PortCeValueFit). out holds 25 bytes at least.
static void	MusicFolderName(const char *path, char *out, int n)
{
	int end = int(strlen(path));
	while ((end > 1) && ((path[end - 1] == '/') || (path[end - 1] == '\\')))
		end--;
	int start = end;
	while ((start > 0) && (path[start - 1] != '/') && (path[start - 1] != '\\'))
		start--;
	if (start == end)
		start = 0;						// nothing but separators: all of it
	char name[28];
	int length = end - start;
	if (length > 24)
	{
		int shown = 21;
		while (shown && path[start + shown - 1] == ' ')
			--shown;
		memcpy(name, path + start, shown);
		strcpy(name + shown, "...");
	}
	else
	{
		memcpy(name, path + start, length);
		name[length] = 0;
	}
	InFontCharacters(name);
	snprintf(out, n, "%s", name);
}

// What choosing Your Music says: what ce.ini sets, as the page reads nothing else. The music player (ce_music.cpp) can still
// refuse the folder (not an absolute folder it can list, or one with no track it can play: Ogg Vorbis, stereo, 44100 Hz), and
// then the game's music plays and the log says what it found. So the dialog names the folder ce.ini sets (60 characters of it
// at most, the last ones when it is longer), which tracks can play and where to read how to change it, or says that it is off
// and how to name a folder in ce.ini. A leading ~/, which the music
// player takes as the home folder on Linux when HOME is set, is named in words: the font has no tilde. Eight lines at most in
// the game's font; no-break spaces keep 44100 Hz on one line, and two words or more on the last line of
// each paragraph of words.
static void	ShowMusicFolder(const char *key)
{
	char dir[256], path[300], shown[96], text[1024];
	if (CE_SettingGetText(key, dir, int(sizeof(dir))))
	{
		snprintf(path, sizeof(path), "%s", dir);
#ifndef _WIN32
		const char *home = getenv("HOME");
		if ((dir[0] == '~') && (dir[1] == '/') && home && *home)
			snprintf(path, sizeof(path), "your home folder%s", dir + 1);
#endif
		const int length = int(strlen(path));
		if (length > 60)
		{
			memcpy(shown, "...", 3);
			memcpy(shown + 3, path + length - 57, 58);		// the last 57 characters and the end
		}
		else
			memcpy(shown, path, size_t(length) + 1);
		InFontCharacters(shown);
		snprintf(text, sizeof(text), "ce.ini sets your own music folder to:\n%s\n\nTracks must be Ogg Vorbis, stereo, 44100\xa0"
				 "Hz. They play in place of the level music. The README says how to change the folder, under Your own\xa0" "music.", shown);
	}
	else
		snprintf(text, sizeof(text), "Your own music is off, so the game's music\xa0" "plays.\n\nTo use your own, name a folder of Ogg Vorbis "
				 "tracks in ce.ini. The README says how, under Your own\xa0" "music.");
	ShowMessage(text);
}

// A CE Settings row as an item of its list: a choice among the texts its ce.ini line can hold, drawn and driven by the game's own
// choice item (CMenuMenuItem: the label, the choice, and the list of choices that drops down when it is chosen). The list asks
// GetValue every frame and calls SetValue when a choice is made, so the choice is looked up in ce.ini once, when the page opens
// (Refresh), and kept: it is the first choice whose value the line holds as the game's own readers take it (CE_SettingIs: the whole
// text, once), else the default, which is what the game does with a line it cannot use. A choice is shown only after it is written
// (CE_SettingSetText: ce.ini is replaced whole or left as it was). Nothing is written where the CE layer is off (a recorded session).
class CCeSettingItem : public CMenuMenuItem
{
public:
	CCeSettingItem(const SCeSetting &setting) : CMenuMenuItem((EPCText)0, 3, false), mSetting(&setting), mIndex(-1) {}

	virtual WCHAR	*GetText() { return (WCHAR *)mSetting->mLabel; }
	virtual int		GetNumChoices() { return mSetting->mCount; }
	virtual WCHAR	*GetChoiceText(int n)
	{
		return (WCHAR *)mSetting->mChoices[((n >= 0) && (n < mSetting->mCount)) ? n : mSetting->mDefault].mLabel;
	}
	virtual int		GetValue()
	{
		if (mIndex < 0)
			mIndex = Lookup();
		return mIndex;
	}
	virtual void	Refresh()
	{
		mIndex = -1;
		CMenuMenuItem::Refresh();
	}
	virtual void	SetValue(int choice)
	{
		if (!PortCeLayer() || (choice < 0) || (choice >= mSetting->mCount))
			return;
		if (!CE_SettingSetText(mSetting->mKey, mSetting->mChoices[choice].mValue))
		{
			ShowMessage("Your settings could not be saved, so this was not changed.");
			return;
		}
		mIndex = choice;
		if (mSetting->mAfter == CEAF_HUD)
			CeHudSettingsReload();
		else if (mSetting->mAfter == CEAF_RECORD)
			RecordNote(!strcmp(mSetting->mChoices[choice].mValue, "1"));
	}

	int		Lookup()
	{
		for (int i = 0; i < mSetting->mCount; i++)
			if (mSetting->mLauncher ? CE_SettingLauncherIs(mSetting->mKey, mSetting->mChoices[i].mValue)
										: CE_SettingIs(mSetting->mKey, mSetting->mChoices[i].mValue))
				return i;
		return mSetting->mDefault;
	}

	const SCeSetting	*mSetting;
	int					mIndex;			// the choice shown, or -1 until it is looked up
};

// Your Music: read only, because the folder is a path and the page cannot take one. It shows the folder's own name (the last part
// of music_dir, 24 characters at most) or Not set, drawn as the other rows are; choosing it says which folder it is and where to set it.
// It has nothing to choose, so it never opens a list of choices, and writes nothing.
class CCeMusicItem : public CMenuMenuItem
{
public:
	CCeMusicItem(const SCeSetting &setting) : CMenuMenuItem((EPCText)0, 3, false), mSetting(&setting), mRead(false) { mShown[0] = 0; }

	virtual WCHAR	*GetText() { return (WCHAR *)mSetting->mLabel; }
	virtual int		GetNumChoices() { return 1; }
	virtual WCHAR	*GetChoiceText(int)
	{
		if (!mRead)
		{
			mRead = true;
			char dir[256], name[28] = "Not set";
			if (CE_SettingGetText(mSetting->mKey, dir, int(sizeof(dir))))
				MusicFolderName(dir, name, int(sizeof(name)));
			Widen(mShown, int(sizeof(mShown) / sizeof(mShown[0])), name);
		}
		return mShown;
	}
	virtual int		GetValue() { return 0; }
	virtual void	SetValue(int) {}
	virtual void	Refresh()
	{
		mRead = false;
		CMenuMenuItem::Refresh();
	}
	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val)
	{
		if (button == BUTTON_FRONTEND_MENU_SELECT)
		{
			ShowMusicFolder(mSetting->mKey);
			CFrontEnd::PlaySound(FES_SELECT);
		}
	}

	const SCeSetting	*mSetting;
	WCHAR				mShown[28];
	bool				mRead;
};

// An Experimental Features (Labs) row: an On/Off switch backed directly by ce.ini via CE_SettingSet(key, 0/1).
// Reads its current disk value once when the page opens (Refresh), writes in-place preserving [Experimental] layout,
// and never writes while PortCeLayer() is false (replays/recordings/probes).
class CCeLabSwitch : public CMenuMenuItem
{
public:
	CCeLabSwitch(const SCeLab &lab) : CMenuMenuItem((EPCText)0, 3, false), mLab(&lab), mIndex(-1) {}

	virtual WCHAR	*GetText() { return (WCHAR *)mLab->mLabel; }
	virtual int		GetNumChoices() { return 2; }
	virtual WCHAR	*GetChoiceText(int n)
	{
		static WCHAR off[] = L"Off", on[] = L"On";
		return n ? on : off;
	}
	virtual int		GetValue()
	{
		if (mIndex < 0)
			mIndex = Lookup();
		return mIndex;
	}
	virtual void	Refresh()
	{
		mIndex = -1;
		CMenuMenuItem::Refresh();
	}
	virtual void	SetValue(int value)
	{
		if (!PortCeLayer() || (value != 0 && value != 1))
			return;
		if (!CE_SettingSet(mLab->mKey, value))
		{
			ShowMessage("Your settings could not be saved, so this was not changed.");
			return;
		}
		mIndex = value;
	}

	int		Lookup()
	{
		if (CE_SettingGetBool(mLab->mKey, 0) == 1)
			return 1;
		if (!strcmp(mLab->mKey, "play_unit") && CE_SettingIs(mLab->mKey, "tank"))
			return 1;
		return 0;
	}
	virtual void	Render(float x, float y, BOOL selected)
	{
		if (mExpanded)
		{
			CMenuMenuItem::Render(x, y, selected);
			return;
		}
		int value = GetValue();
		if (value != mValue)
		{
			mValue = value;
			mSelection = value;
		}
		CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
		if (!font)
			return;
		const bool on = (mSelection != 0);
		DWORD labelCol = !IsEnabled() ? 0xff505050u : (selected ? 0xffffcc00u : 0xffffffffu);
		// 2x colour stage (StageColour): crisp teal-green for On vs soft grey for Off; distinct when selected too.
		DWORD valCol = !IsEnabled() ? 0xff505050u
			: (selected ? (on ? 0xff487f38u : 0xff7f6e28u) : (on ? 0xff347f58u : 0xff545454u));
		WCHAR *label = GetText();
		SIZE size;
		font->GetTextExtent(label, &size);
		float scale = 1.f;
		float tx = x - float(size.cx);
		if (tx < 5.f)
		{
			scale = (x - 5.f) / float(size.cx);
			tx = 5.f;
		}
		font->DrawTextWithShadow(tx, y, labelCol, label, 0, 0, scale, 1.f);
		font->DrawTextWithShadow(x + 8.f, y, valCol, GetChoiceText(mSelection), 0, 0, 1.f, 1.f);
	}

	const SCeLab	*mLab;
	int				mIndex;
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
	CCeMenu() : CPauseMenu(PORT_CE_NO_LISTS), mPageOpened(false), mBoxTitle(0)
	{
		// Each page is a list of its rows and, last, Back, which draws the highlighted row's hint (CCeBack); the lists are
		// in ECePage's order.
		// Each unfinished feature is offered only with its own ce.ini line set to 1 under [Experimental] (read once, ce.cpp): without
		// map_editor the hub has no Map Editor row, without museum no Museum row, without commander CE
		// Settings has no Commander Map row, and the Help pages say that feature is not here.
		const bool commander = PortCeExperimental("commander");
		const int hubCount = int(sizeof(kHubRows) / sizeof(kHubRows[0]));
		mHubCount = 0;
		for (int i = 0; i < hubCount; i++)
		{
#ifndef CE_NETWORK_OFF
			// The first row is only available when this process can offer a network game (not recordings, replays or tests).
			if ((kHubRows[i].mAction == CEA_NETWORK) && !PortCeNetOffered())
				continue;
#endif
			if (ExperimentalOffered(kHubRows[i].mAction))
				mHubRows[mHubCount++] = kHubRows[i];
		}
		AddPage(CEP_HUB, mHubRows, mHubCount, NULL, kCeHubY);
		AddPage(CEP_BONUS, kBonusRows, CE_NUM_BONUS, kBonusNote);
		// CE Settings is its table, a row each (kSettings; the Commander Map row only with commander on), centred higher than
		// the other pages for its fourteen items.
		mSettingCount = 0;
		for (const SCeSetting &setting : kSettings)
			if (commander || strcmp(setting.mKey, "commander"))
				mSettings[mSettingCount++] = setting;
		CMenuOptionsList *list = NewList(CEP_SETTINGS, kCeSettingsY);
		for (int i = 0; i < mSettingCount; i++)
		{
			if (mSettings[i].mChoices)
				list->AddItem(new (MEMTYPE_MENUITEM) CCeSettingItem(mSettings[i]));
			else
				list->AddItem(new (MEMTYPE_MENUITEM) CCeMusicItem(mSettings[i]));
		}
		list->AddItem(new (MEMTYPE_MENUITEM) CCeBack(list, &mSettings[0].mHint, int(sizeof(SCeSetting)), mSettingCount, NULL, CCeBack::EXTRA_FOOT));
		mLists.Append(list);
		list = NewList(CEP_CHEATS);
		for (const SCeCheat &cheat : kCheats)
			list->AddItem(new (MEMTYPE_MENUITEM) CCeCheatSwitch(cheat.mLabel, cheat.mCheat));
		list->AddItem(new (MEMTYPE_MENUITEM) CCeBack(list, &kCheats[0].mHint, int(sizeof(SCeCheat)), CE_NUM_CHEATS, kCheatsNote, CCeBack::EXTRA_FIXED));
		mLists.Append(list);
		const int helpCount = int(sizeof(kHelpRows) / sizeof(kHelpRows[0]));
		for (int i = 0; i < helpCount; i++)
			mHelpRows[i] = kHelpRows[i];
		if (!commander)
			mHelpRows[0].mHint.mHelp = kControlsHintPlain;
		AddPage(CEP_HELP, mHelpRows, helpCount);
		for (int i = 0; i < CE_NUM_PLANS; i++)
			mPlanHints[i] = kPlanHints[i];
		for (const SCePlanPlain &plain : kPlanPlain)
			if (!PortCeExperimental(plain.mMode))
				mPlanHints[plain.mIndex].mHelp = plain.mHint;
		if (!PortCeModsPageOffered())
			mPlanHints[CEPL_MODS].mHelp = kPlanModsPlainHint;
		list = NewList(CEP_PLANS);
		for (int i = 0; i < CE_NUM_PLANS; i++)
			list->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(kPlanLabels[i], CEA_PLAN_FIRST + i));
		list->AddItem(new (MEMTYPE_MENUITEM) CCeBack(list, mPlanHints, int(sizeof(SCeHint)), CE_NUM_PLANS, NULL));
		mLists.Append(list);
		list = NewList(CEP_EDITOR);		// reached only from the Map Editor row, which the hub offers only on yes
		for (int i = 0; i < kEditorMapCount; i++)
			list->AddItem(new (MEMTYPE_MENUITEM) CCeEditorMapItem(kEditorRows[i].mLabel, kEditorRows[i].mAction, kEditorLevels[i]));
		list->AddItem(new (MEMTYPE_MENUITEM) CCeBack(list, &kEditorRows[0].mHint, int(sizeof(SCeRow)), kEditorMapCount, NULL));
		mLists.Append(list);
		AddPage(CEP_MUSEUM, kMuseumRows, kMuseumRowCount);		// reached only from the Museum row, which the hub offers only with the museum on
		memset(mCategoryHints, 0, sizeof(mCategoryHints));
		{
			// The Catalogue lists stay lightweight and inert until ReadCatalogue() runs when the row is chosen (guarded by ExperimentalOffered).
			CMenuOptionsList *groups = NewList(CEP_CATALOGUE);
			for (int i = 0; i < CE_CATALOGUE_GROUPS; ++i)
				groups->AddItem(mCategory[i] = new (MEMTYPE_MENUITEM) CCeCatalogueRow(CEA_CAT_MODELS + i));
			groups->AddItem(new (MEMTYPE_MENUITEM) CCeBack(groups, mCategoryHints, sizeof(SCeHint), CE_CATALOGUE_GROUPS, NULL));
			mLists.Append(groups);
			CMenuOptionsList *items = NewList(CEP_CATALOGUE_ITEMS, kCeSettingsY + 15.f);	// sixteen rows: the first below the emblem
			// What the player came for first: the items, then the page turn and Find, Back last.
			for (int i = 0; i < CE_CATALOGUE_SLOTS; ++i)
				items->AddItem(mCatalogRows[i] = new (MEMTYPE_MENUITEM) CCeCatalogueRow(CEA_CAT_ENTRY + i));
			items->AddItem(mCatalogMore = new (MEMTYPE_MENUITEM) CCeCatalogueRow(CEA_CAT_TURN));
			items->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(L"Find by name", CEA_CAT_FIND));
			items->AddItem(mCatalogClear = new (MEMTYPE_MENUITEM) CCeCatalogueRow(CEA_CAT_CLEAR));
			items->AddItem(new (MEMTYPE_MENUITEM) CCeBack(items, NULL, 0, 0, NULL));
			mLists.Append(items);
			CMenuOptionsList *find = NewList(CEP_CATALOGUE_FIND);
			find->AddItem(mCatalogField = new (MEMTYPE_MENUITEM) CCeCatalogueField(CEA_CAT_FIELD));
			find->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(L"Find", CEA_CAT_APPLY));
			find->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(L"Clear text", CEA_CAT_ERASE));
			find->AddItem(new (MEMTYPE_MENUITEM) CCeBack(find, NULL, 0, 0,
				L"Type a name. Enter finds, Esc cancels.\nSpaces also match underscores."));
			mLists.Append(find);
		}
		mLabCount = 0;
		const bool modsAvailable = !PortModsPlatformOff();
		for (const SCeLab &lab : kLabs)
			if (!lab.mNeedsMods || modsAvailable)
				mLabs[mLabCount++] = lab;
		CMenuOptionsList *labs = NewList(CEP_LABS);
		for (int i = 0; i < mLabCount; i++)
			labs->AddItem(new (MEMTYPE_MENUITEM) CCeLabSwitch(mLabs[i]));
		labs->AddItem(new (MEMTYPE_MENUITEM) CCeBack(labs, &mLabs[0].mHint, int(sizeof(SCeLab)), mLabCount, kLabsNote));
		mLists.Append(labs);
	}

	// The pages' rows as this process offers them (the constructor fills them in).
	SCeRow		mHubRows[sizeof(kHubRows) / sizeof(kHubRows[0])];
	int			mHubCount;
	SCeSetting	mSettings[CE_NUM_SETTINGS];
	int			mSettingCount;
	SCeLab		mLabs[CE_NUM_LABS];
	int			mLabCount;
	SCeRow		mHelpRows[sizeof(kHelpRows) / sizeof(kHelpRows[0])];
	SCeHint		mPlanHints[CE_NUM_PLANS];

	CCeCatalogueRow *mCategory[CE_CATALOGUE_GROUPS], *mCatalogRows[CE_CATALOGUE_SLOTS], *mCatalogMore, *mCatalogClear;
	CCeCatalogueField *mCatalogField;
	SCeHint mCategoryHints[CE_CATALOGUE_GROUPS]; int mCatalogueGroupChosen;	// the category row a CEA_CAT_MODELS action came from
	void ReadCatalogue()
	{
		PortCeCatalogueOpen();
		for (int i = 0; i < CE_CATALOGUE_GROUPS; ++i)
		{
			char label[40], text[44];
			const unsigned count = PortCeCatalogueCount(i);
			PortCeCatalogueGroupLabel(i, label, sizeof label);
			if (*label) snprintf(text, sizeof text, "%s (%u)", label, count);
			else text[0] = 0;
			mCategory[i]->Bind(text);
			mCategoryHints[i].mHelp = i == 2 ? (count ? L"The game's sound effects for its language. Choose one to hear it." : L"No sound effects are loaded in this session.")
				: i > 2 ? (PortCeCatalogueGroupWorded(i) ? L"The radio messages' spoken lines in this language. Choose one to hear it."
					: L"Spoken lines in this language, named by their files: the game has no text for them.")
				: (count ? L"Choose an item to see its details." : L"No matching files were found in your game folder.");
			mCategoryHints[i].mExtra = (i >= 2 && PortCeExtrasOffered()) ? L"Music is in Extras > Music Player." : NULL;
		}
	}
	void BindCatalogueItems()
	{
		char titles[CE_CATALOGUE_SLOTS][100];
		WCHAR shown[CE_CATALOGUE_SLOTS][100];
		for (int i = 0; i < CE_CATALOGUE_SLOTS; ++i)
		{
			PortCeCatalogueTitle(i, titles[i], sizeof titles[i]);
			CCeCatalogueRow::Fit(shown[i], 100, titles[i], NULL);
		}
		for (int i = 0; i < CE_CATALOGUE_SLOTS; ++i)
		{
			// Rows that would read the same end with their file's own name, so each can be told apart.
			bool same = false;
			for (int j = 0; j < CE_CATALOGUE_SLOTS && !same && *titles[i]; ++j)
				same = j != i && !wcscmp(shown[i], shown[j]);
			char tag[40], tail[44] = {};
			if (same)
			{
				PortCeCatalogueRowTag(i, tag, sizeof tag);
				if (*tag) snprintf(tail, sizeof tail, " (%s)", tag);
			}
			WCHAR label[100];
			if (*tail) CCeCatalogueRow::Fit(label, 100, titles[i], tail);
			else wcscpy(label, shown[i]);
			mCatalogRows[i]->BindWide(label);
		}
		char more[44] = {};
		const int page = PortCeCataloguePageNumber(), pages = PortCeCataloguePageCount();
		if (pages > 1)
			snprintf(more, sizeof more, "%s (%d of %d)", page == pages ? "First page" : "Next page", page, pages);
		mCatalogMore->Bind(more);
		mCatalogClear->Bind(*PortCeCatalogueQuery() ? "Clear filter" : "");
	}
	void CatalogueSelect()
	{
		if (mPage != CEP_CATALOGUE_ITEMS) return;
		const int row = mLists.At(mPage)->mSelected;
		if (row >= 0 && row < CE_CATALOGUE_SLOTS) PortCeCatalogueSelect(unsigned(row));
	}
	// The list box for the collection being browsed, on its selected item (or its first).
	void OpenCatalogueList()
	{
		char title[40]; PortCeCatalogueGroupLabel(PortCeCatalogueCategory(), title, sizeof title);
		PortCeCatalogueListTitle(title);
		PortCeExtrasOpenPage(XP_CATALOGUE_LIST, PortCeCatalogueListRowOf(PortCeCatalogueSelectedIndex()));
	}
	// Find, chosen in a catalogue list: this page's Find page, and back to the list when it is done.
	void CatalogueFindFromList()
	{
		PortCeExtrasHide();
		mPage = CEP_CATALOGUE;
		mCatalogField->Set(PortCeCatalogueQuery());
		OpenPage(CEP_CATALOGUE_FIND); LT.SetKeytrap(CatalogueTrap()); mPageOpened = true;
	}
	// Find is done: back to the list it came from, on the selected item.
	void CatalogueResult()
	{
		CatalogueTextEnd();
		mPage = CEP_CATALOGUE;
		OpenCatalogueList();
	}
	void CatalogueKeys(WORD key, KeyEventType event)
	{
		if (!mActive || mPage != CEP_CATALOGUE_FIND || FEMESSBOX.BeingDisplayed()) return;
		CMenuOptionsList *list = mLists.At(mPage);
		if (event == KEY_EVENT_DOWN) {
			if (key == DIK_ESCAPE) Back();
			else if (key == DIK_UP) list->ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_UP, 1.f);
			else if (key == DIK_DOWN || key == DIK_TAB) list->ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_DOWN, 1.f);
			else if (key == DIK_RETURN || key == DIK_NUMPADENTER) Choose(list->mItems.At(list->mSelected));
			else if (key == DIK_BACK && list->mSelected == 0) mCatalogField->Backspace();
		} else if (event == KEY_EVENT_CHAR && list->mSelected == 0) mCatalogField->Add(key);
	}
	void CatalogueStatus()
	{
		if (mPage != CEP_CATALOGUE_ITEMS) return;
		char text[128]; const unsigned n = PortCeCatalogueMatches();
		if (*PortCeCatalogueQuery()) snprintf(text, sizeof text, "%u match%s for %s", n, n==1?"":"es", PortCeCatalogueQuery());
		else snprintf(text, sizeof text, "%u items. Find searches this whole collection.", n);
		WCHAR wide[128]; Widen(wide, 128, text); CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL); SIZE size;
		font->GetTextExtent(wide, &size); font->DrawTextWithShadow(320.f-float(size.cx)*.5f, 436.f, 0xffffffff, wide, 0, 0, 1.f, 1.f);
	}

	static CMenuOptionsList	*NewList(int page, float y = kCeListY)
	{
		return new (MEMTYPE_MENUITEM) CMenuOptionsList(kCeTitles[page], 320.f, y, FALSE, 0xb0000000);
	}

	// A page of plain rows: each its own row, then Back, and the page's standing note under the hint if it has one.
	void	AddPage(int page, const SCeRow *rows, int count, const WCHAR *note = NULL, float y = kCeListY)
	{
		CMenuOptionsList *list = NewList(page, y);
		for (int i = 0; i < count; i++)
			list->AddItem(new (MEMTYPE_MENUITEM) CCeMenuItem(rows[i].mLabel, rows[i].mAction));
		list->AddItem(new (MEMTYPE_MENUITEM) CCeBack(list, &rows[0].mHint, int(sizeof(SCeRow)), count, note));
		mLists.Append(list);
	}

	// One Select reaches the page as two actions in one update, the front end's and the pause menu's, in the order of the input's
	// bindings. The pause menu's chooses the row (Choose), which can open another page; the front end's, coming after it, would then
	// be a choice on that page's first row (CE Settings' HUD Size list opening). So a page the pause menu's select opened takes no
	// front end select until the next update (CFEPCe::Process), whatever the order.
	bool	mPageOpened;

	// While one of Help & Info's own boxes is up (the one text it shows, with no heading of its own) the title bar names it: 0 for none,
	// else the index into kBoxTitles, kept until the box is gone.
	int		mBoxTitle;

	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val)
	{
		if (PLATFORM.GetSysTimeFloat() - mOpenTime < 0.4f)
			return;
		if ((button == BUTTON_FRONTEND_MENU_SELECT) && mPageOpened)
			return;
		CatalogueSelect();
		if (PortCeExtrasActive())
		{
			PortCeExtrasButton(button, val);		// an Extras page is showing (ce_extras.cpp): the input is its own
			return;
		}
		CMenuOptionsList *list = mLists.At(mPage);
		if (list->PassToSelected(from_controller, button, val))
			return;
		switch (button)
		{
		default:
			list->ReceiveButtonAction(from_controller, button, val);
			break;
		case BUTTON_PAUSE_MENU_SELECT:
		{
			const int page = mPage;
			Choose(list->mItems.At(list->mSelected));
			if (mPage != page)
				mPageOpened = true;
			break;
		}
		case BUTTON_FRONTEND_MENU_BACK:
			Back();
			break;
		}
	}

	void	Leave()
	{
		CatalogueTextEnd();
		DeActivate(FALSE);
		CFrontEnd::PlaySound(FES_BACK);
	}

	// Back goes to the page above (the hub leaves the Onslaught CE page, to the main menu).
	void	Back()
	{
		CatalogueSelect();
		if (mPage == CEP_CATALOGUE_FIND) { CatalogueResult(); CFrontEnd::PlaySound(FES_BACK); return; }
		if (mPage == CEP_HUB)
			Leave();
		else
		{
			if (mPage == CEP_CATALOGUE)
				PortCeCatalogueClose();
			mPage = kCeParent[mPage];
			CFrontEnd::PlaySound(FES_BACK);
		}
	}

	// A page opens with its first row highlighted.
	void	OpenPage(int page)
	{
		if (page != CEP_CATALOGUE_FIND) CatalogueTextEnd();
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

	// A museum hall or the Goodies Gallery has just been left and the Onslaught CE page is open again: the Museum page, the way the player chose highlighted
	// (CEA_MUSEUM_VISIT or CEA_MUSEUM). Back from it goes to the hub, on the Museum row.
	void	ShowMuseum(int action = CEA_MUSEUM_VISIT)
	{
		OpenPage(CEP_HUB);
		for (int row = 0; row < mHubCount; row++)
			if (mHubRows[row].mAction == CEA_PAGE_MUSEUM)
				mLists.At(CEP_HUB)->mSelected = row;
		OpenPage(CEP_MUSEUM);
		for (int row = 0; row < kMuseumRowCount; row++)
			if (kMuseumRows[row].mAction == action)
				mLists.At(CEP_MUSEUM)->mSelected = row;
	}

	WCHAR	*PageTitle()
	{
		if (mBoxTitle && FEMESSBOX.BeingDisplayed())
			return kBoxTitles[mBoxTitle - 1];
		mBoxTitle = 0;
		return PortCeExtrasActive() ? PortCeExtrasTitle() : kCeTitles[mPage];
	}

	void	Choose(CMenuItem *item)
	{
		mBoxTitle = 0;
		if (item->mText == GI_BACK)
		{
			Back();
			return;
		}
		if ((item->mId != CCeMenuItem::ID) || !item->IsEnabled())
			return;
		char text[1536];
		int action = ((CCeMenuItem *)item)->mAction;
		if (!ExperimentalOffered(action))
			return;							// not offered, so not reached; a second line behind the rows that are left out
		if (action >= CEA_CAT_ENTRY && action < CEA_CAT_ENTRY + CE_CATALOGUE_SLOTS)
		{
			const unsigned slot = action - CEA_CAT_ENTRY;
			PortCeCatalogueSelect(slot);
			PortCeCatalogueDetails(slot, text, sizeof text);
			if (*text) {
				// (a sound is never a row here: the sound lists are list boxes, ce_catalogue_list.cpp)
				if (PortCeCatalogueIsTexture(slot) && PortCeCataloguePreviewOpen(slot))
					ShowMessage(text, 329.f);
				else {
					if (PortCeCatalogueIsTexture(slot))
						strncat(text, "\nPicture unavailable.", sizeof text - strlen(text) - 1);
					ShowMessage(text);
				}
				CFrontEnd::PlaySound(FES_SELECT);
			}
			return;
		}
		if ((action >= CEA_PLAN_FIRST) && (action < CEA_PLAN_FIRST + CE_NUM_PLANS))
		{
			ShowMessage(PlanText(action - CEA_PLAN_FIRST));
			CFrontEnd::PlaySound(FES_SELECT);
			return;
		}
        if (action >= CEA_EDITOR_FIRST && action < CEA_EDITOR_FIRST + kEditorMapCount)
        {
            // Begin validates and takes the career snapshot before it requests the selected level exactly once.
            if (PortCeEditorBegin(kEditorLevels[action - CEA_EDITOR_FIRST]))
                Leave();
            else
                ShowMessage(PortCeEditorMessage());
            return;
        }
		if (action == CEA_CARRIER_CLASH)
		{
			if (!mActive)
				return;
			if (PortCeCarrierClashBegin())
			{
				if (PortCeBonusStartCarrierClash())
					Leave();
				else
				{
					PortCeCarrierClashCancel();
					ShowMessage("Carrier Clash could not be started.");
				}
			}
			else if (const char *ccWhy = PortCeCarrierClashMessage())
			{
				if (*ccWhy)
					ShowMessage(ccWhy);
			}
			return;
		}
		if (const int level = BonusLevel(action))
		{
			// The playable demo's level, the two co-op maps the Multiplayer grid leaves out (two players on one PC) or one
			// of the original's five Race Challenges (levels 901-905), played outside every career: nothing they do is
			// kept, and the front end comes back to this list with the level's row highlighted (ce_bonus.cpp, patch 0116).
			if (PortCeBonusStart(level))
				Leave();
			else
				ShowMessage(level >= 901 ? "The race could not be started." : "The bonus level could not be started.");
			return;
		}
		// Every category row is one action, told apart by the row (its number is not a case label of its own).
		mCatalogueGroupChosen = 0;
		if (action > CEA_CAT_MODELS && action < CEA_CAT_MODELS + CE_CATALOGUE_GROUPS)
		{
			mCatalogueGroupChosen = action - CEA_CAT_MODELS;
			action = CEA_CAT_MODELS;
		}
		switch (action)
		{
		case CEA_PAGE_EDITOR:
			OpenPage(CEP_EDITOR);
			break;
		case CEA_PAGE_MUSEUM:
			OpenPage(CEP_MUSEUM);
			break;
		case CEA_PAGE_LABS:
			OpenPage(CEP_LABS);
			break;
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
		case CEA_EXTRAS:
			// The Extras pages take this page's input, drawing and title until their own Back; the select that opened them is not theirs.
			PortCeExtrasOpen();
			mPageOpened = true;
			break;
		case CEA_TIPS:
		{
			char tips[512];
			ControlsTips(tips, sizeof(tips), PortCeExperimental("commander"));
			ShowMessage(tips);
			mBoxTitle = CEBT_TIPS;
			break;
		}
		case CEA_MUSEUM:
			// The Goodies gallery as a visitor (ce_museum.h). A second Select while the first is still opening it finds the visit under
			// way and says nothing.
			if (!PortCeMuseumOpen() && !PortCeMuseumActive())
				ShowMessage("The gallery could not be opened.");
			break;
		case CEA_CATALOGUE:
			ReadCatalogue();
			OpenPage(CEP_CATALOGUE);
			break;
		case CEA_CAT_MODELS:
		{
			const int category = mCatalogueGroupChosen;
			if (!PortCeCatalogueCount(category))
				ShowMessage(category == 2 ? "No sound effects are loaded in this session." : category > 2 ? "No spoken lines were found in this language's folder." : (category ? "No textures were found in your game folder." : "No models were found in your game folder."));
			else
			{
				// Every group is a list box (ce_catalogue_list.cpp): whole names, scrolled, Select opening the row's preview or
				// details as before. Its Back returns here, to the groups.
				PortCeCatalogueBrowse(category);
				OpenCatalogueList();
				mPageOpened = true;
			}
			break;
		}
		case CEA_CAT_TURN:
			PortCeCatalogueTurn();
			BindCatalogueItems();
			mLists.At(CEP_CATALOGUE_ITEMS)->mSelected = 0;
			break;
		case CEA_CAT_FIND:
			CatalogueSelect(); mCatalogField->Set(PortCeCatalogueQuery());
			OpenPage(CEP_CATALOGUE_FIND); LT.SetKeytrap(CatalogueTrap()); mPageOpened = true;
			break;
		case CEA_CAT_FIELD:
		case CEA_CAT_APPLY:
			PortCeCatalogueFilter(mCatalogField->mValue); CatalogueResult(); mPageOpened = true;
			break;
		case CEA_CAT_ERASE:
			mCatalogField->Set(""); mLists.At(CEP_CATALOGUE_FIND)->mSelected = 0;
			break;
		case CEA_CAT_CLEAR:
			PortCeCatalogueFilter(""); CatalogueResult();
			break;
		case CEA_MODS:
			PortCeModsPageOpen();
			break;
		case CEA_MUSEUM_VISIT:
			// Native input can deliver its second Select after this page has already dispatched the visit.
			if (!mActive)
				break;
			if (PortCeMuseumVisitBegin())
			{
				FRONTEND.SetQuit(PortCeMuseumVisitLevel());
				Leave();
			}
			else if (const char *why = PortCeMuseumVisitMessage())
			{
				// A refusal a player can act on has a sentence; one they cannot (the visit is already opening) has none and shows no box.
				if (*why)
					ShowMessage(why);
			}
			break;
#ifndef CE_NETWORK_OFF
		case CEA_NETWORK:
			PortCeNetOpen();
			break;
#endif
		case CEA_ABOUT:
		{
			const char *build = CE_BuildName();
			snprintf(text, sizeof(text), "Onslaught CE V%s%s%s\n\nBattle Engine Aquila (Lost Toys, 2003), rebuilt "
					 "from the game's source code to run natively on today's computers. It plays the game files of your "
					 "own copy of the game.\n\nAn unofficial fan project, not affiliated with or endorsed by the game's "
					 "developer, publisher or rights holders. Free software under the GNU General Public License, version 3, with no "
					 "warranty.\n\nSource code, new versions and problem reports:\n"
					 CE_LINKS, CE_VERSION,
					 *build ? ", build " : "", build);
			ShowMessage(text);
			mBoxTitle = CEBT_ABOUT;
			break;
		}
		case CEA_UPDATES:
		{
			// ce.ini turns the launcher's checks off when update_check is exactly 0, read as the launcher reads it. The page reads only
			// ce.ini (an environment variable, a launcher switch and a development build turn checking off too), so "on" is the
			// setting's part. The game font has no underscore, so the row of CE Settings that sets the line is named instead. The
			// folder is not named here (CE Settings has the row; Report a Problem names the folder).
			const bool on = !CE_SettingLauncherIs("update_check", "0");
			snprintf(text, sizeof(text), "%s\n\nUpdates keep your careers, settings and the previous version. "
					 "To go back, follow the steps under Updates in the\xa0README.\n\nIf you still have Onslaught CE 0.1.6 or older, do not "
					 "start its launcher. See the README.",
					 on ? "Onslaught CE looks for a new version when it starts, and asks before installing it. Check for Updates at "
					 "Start in CE Settings turns this off." :
					 "Onslaught CE does not look for new versions when it starts. Check for Updates at Start in CE Settings turns "
					 "this on.");
			ShowMessage(text);
			mBoxTitle = CEBT_UPDATES;
			break;
		}
		case CEA_THANKS:
			ShowMessage(kSpecialThanks);
			mBoxTitle = CEBT_THANKS;
			break;
		case CEA_REPORT:
		{
			char folder[1024];
			FolderText(folder, sizeof(folder), "");
			if (CE_OpenReportPage())
				snprintf(text, sizeof(text), "A problem report page has opened in your web browser (it may be behind the "
						 "game). It already names your version and system. Nothing is sent until you submit it.\n\nPlease "
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
		if ((fade > 0.4f) && PortCeExtrasActive())
			PortCeExtrasDraw();
		else if (fade > 0.4f)
		{
			title = mLists.At(mPage)->Render(this);
			CatalogueSelect(); CatalogueStatus();
		}
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

#ifdef CE_NETWORK_OFF
	void	Shutdown() { CatalogueTextEnd(); PortCeCataloguePreviewClose(); PortCeCatalogueClose(); PortCeExtrasShutdown(); PortCeModsPageShutdown(); SAFE_DELETE(mMenu); }
#else
	void	Shutdown() { CatalogueTextEnd(); PortCeCataloguePreviewClose(); PortCeCatalogueClose(); PortCeExtrasShutdown(); PortCeModsPageShutdown(); PortCeNetShutdown(); SAFE_DELETE(mMenu); }
#endif

	void	ButtonPressed(SINT button, float val)
	{
		if (PortCeModsPageButton(button, val))
			return;							// the Mods page (ce_modspage.cpp) is open and took it
#ifdef CE_NETWORK_OFF
		if (mMenu)
#else
		if (PortCeNetActive())
			PortCeNetButton(button, val);
		else if (mMenu)
#endif
			mMenu->ReceiveButtonAction(NULL, button, val);
	}

	void	Process(EFEPState state)
	{
		// a new update: a page the last one's select opened takes the front end's select again (CCeMenu::mPageOpened)
		if (mMenu)
			mMenu->mPageOpened = false;
		PortCeExtrasProcess(state == FEPS_ACTIVE);
		PortCeModsPageProcess();
		if (sCeBox && !FEMESSBOX.BeingDisplayed())
		{
			PortCeCataloguePictureClose();	// a sound plays on after its box: another choice or leaving the list stops it
			sCeBox = false;
		}				// the box ShowMessage made is gone: Esc and B are the page's again
#ifndef CE_NETWORK_OFF
		if (state == FEPS_ACTIVE)
			PortCeNetProcess();
#endif
		if ((state == FEPS_ACTIVE) && mMenu && (!mMenu->mActive))
			FRONTEND.SetPage(FEP_MAIN, 70);
	}

	void	RenderPreCommon(float transition, EFrontEndPage dest)
	{
		FRONTEND.DrawStandardVideoBackground(transition, 0x3fffffff, dest);
	}

	void	Render(float transition, EFrontEndPage dest)
	{
		if (PortCeModsPageRender(transition, dest))
			return;							// the Mods page is open: it drew itself in this page's frame
#ifdef CE_NETWORK_OFF
		static WCHAR title[] = L"Onslaught CE";
#else
		static WCHAR title[] = L"Onslaught CE";
#endif
		FRONTEND.DrawSlidingTextBordersAndMask(transition, dest);
		// the message box is translucent: the list is not drawn behind it
#ifdef CE_NETWORK_OFF
		if (mMenu && !FEMESSBOX.BeingDisplayed())
#else
		bool net = PortCeNetActive();
		if (net)
			PortCeNetDraw();
		else if (mMenu && !FEMESSBOX.BeingDisplayed())
#endif
			mMenu->Draw();
		if (sCeBox && FEMESSBOX.BeingDisplayed())
			PortCeCataloguePreviewDraw(FEMESSBOX.mY - FEMESSBOX.mHeight * 0.5f);
#ifdef CE_NETWORK_OFF
		FRONTEND.DrawTitleBar(mMenu ? mMenu->PageTitle() : title, transition, dest);
#else
		FRONTEND.DrawTitleBar(net ? PortCeNetTitle() : (mMenu ? mMenu->PageTitle() : title), transition, dest);
#endif
		FRONTEND.GetCommonPage()->RenderHelpText(1, transition);
		FRONTEND.GetCommonPage()->RenderSmallBEALogo(RangeTransition(transition, 0.75f, 1.f));
#ifndef CE_NETWORK_OFF
		if (net)
			PortCeNetKeepStartFrame();	// the first frame of a network start, before the pointer: the waiting screen's background
#endif
	}

	void	TransitionNotification(EFrontEndPage from)
	{
		if (!mMenu)
			mMenu = new (MEMTYPE_MENUITEM) CCeMenu();
		CPauseMenu::mOptionsPage = true;
		mMenu->Activate(0);
		// A bonus level has just ended: the page opens on the Bonus Levels list, that level's row highlighted (ce_bonus.cpp).
		// A level a mod adds, started from the Mods page, has just ended: that page opens again with the mod's row highlighted instead.
		const int modLevel = PortCeBonusTakeModReturn();
		if (modLevel && PortCeModsPageReturn(modLevel))
		{
		}
		else if (int level = PortCeBonusTakeReturn())
			mMenu->ShowBonus(level);
		{
			const char *ccNotice = NULL;
			if (PortCeCarrierClashTakeNotice(&ccNotice))
			{
				mMenu->ShowBonus(742);
				if (ccNotice && *ccNotice)
					ShowMessage(ccNotice);
			}
		}
		// A museum visit has just ended (the player left it, or its load was refused): the hub on the museum's row, and the reason if it was
		// refused (ce_museum_visit.h).
		{
			const char *sentence = NULL;
			if (PortCeMuseumVisitTakeNotice(&sentence))
			{
				mMenu->ShowMuseum();
				if (sentence)
					ShowMessage(sentence);
			}
			else if (from == FEP_GOODIES)
				mMenu->ShowMuseum(CEA_MUSEUM);		// back from the gallery (its Back is answered with this page): its own row
		}
        if (int level = PortCeEditorMenuReturn())
        {
            mMenu->OpenPage(CEP_EDITOR);
            for (int row = 0; row < kEditorMapCount; ++row)
                if (kEditorLevels[row] == level)
                    mMenu->mLists.At(CEP_EDITOR)->mSelected = row;
            if (PortCeEditorMessage()[0])	// a refusal's sentence; a player's own leave has none
                ShowMessage(PortCeEditorMessage());
        }
#ifndef CE_NETWORK_OFF
		// back from a network game (ce_netpages.cpp PortCeNetIntro): its pages, with the outcome
		if (PortCeNetOpenOnEntry())
			PortCeNetOpen();
#endif
	}

	CCeMenu	*mMenu;
};

static CFEPCe	sCePage;

// The Catalogue's sound lists (ce_catalogue_list.cpp) are list boxes on the Extras host; they show their details in this page's own
// box (Esc closes it, as on every Catalogue page) and use this page's Find.
void	PortCeCatalogueListDetails(const char *text)
{
	ShowMessage(text);
}
// A texture's details with its picture: the box placed low, as the items page placed it, so the picture has room above.
void	PortCeCatalogueListPicture(const char *text)
{
	ShowMessage(text, 329.f);
}
void	PortCeCatalogueListFind()
{
	if (sCePage.mMenu)
		sCePage.mMenu->CatalogueFindFromList();
}

void CatalogueKeyTrap(WORD key, KeyEventType event)
{
	if (sCePage.mMenu && LT.GetKeytrap() == CatalogueTrap()) sCePage.mMenu->CatalogueKeys(key, event);
}

CFrontEndPage	*PortCePage()
{
	return &sCePage;
}

// The Mods page starts a level: this page closes as it does for a bonus level (ce_modspage.cpp).
void	PortCeLeaveCePage()
{
	if (sCePage.mMenu)
		sCePage.mMenu->Leave();
}

// The game's message box with a Continue button, for a page drawn inside this one (ce_modspage.cpp).
void	PortCeShowMessage(const char *text, float top_least)
{
	ShowMessage(text, 240.f, top_least);
}

// The user folder, or a folder in it, as this page's own boxes name it (the Mods page's Open Mods Folder).
char	*PortCeFolderText(char *out, int n, const char *sub)
{
	return FolderText(out, n, sub);
}

void	PortCeOpenPage()
{
	FRONTEND.SetPage(FEP_CE, 70);
}

// The Goodies Gallery row: a visit (ce_museum.h) begins only where the CE menus are on, and the page it opens is the Goodies gallery,
// reached from the CE page. The visit starts before the page is requested so that the front end's page hook (patch 0199) sees it.
extern "C" bool PortCeMuseumOpen(void)
{
	if (!PortCeMuseumBegin(PortCeMenus()))
		return false;
	FRONTEND.SetPage(FEP_GOODIES, 70);
	return PortCeMuseumActive();
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

// The pause menu's way out of a museum visit (patch 0207, PauseMenu.cpp): the game's own Quit item, so the choice, the "Are you sure?" and the way back to the
// front end are the game's, worded for the place. The visit has no Retry and no mission rows.
class CCeLeaveMuseumItem : public CMenuItem
{
public:
	explicit CCeLeaveMuseumItem(DWORD quit) : CMenuItem(quit, 2) {}

	virtual WCHAR	*GetText()
	{
		static WCHAR leave[] = L"Leave Museum";
		return leave;
	}
};

CMenuItem	*PortCeMuseumLeaveItem(DWORD quit)
{
	return new (MEMTYPE_MENUITEM) CCeLeaveMuseumItem(quit);
}

// ---- The test kit's adapter for the Onslaught CE page (ce_testkit.h): its pages as the player sees them -------------------
// Plain reads of the Onslaught CE page (ce_frontend.h), for the test kit (ce_frontend_testkit.cpp). They change nothing.
bool	PortCeFrontendMenuShown() { return sCePage.mMenu && sCePage.mMenu->mActive; }
int		PortCeFrontendPage() { return sCePage.mMenu ? sCePage.mMenu->mPage : -1; }
int		PortCeFrontendPageCount() { return CEP_COUNT; }
const WCHAR	*PortCeFrontendPageTitle(int page) { return page >= 0 && page < CEP_COUNT ? kCeTitles[page] : NULL; }
CMenuOptionsList	*PortCeFrontendRows()
{
	CCeMenu *menu = sCePage.mMenu;
	return menu && menu->mPage >= 0 && menu->mPage < menu->mLists.Size() ? menu->mLists.At(menu->mPage) : NULL;
}

#endif
