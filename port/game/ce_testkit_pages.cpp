// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_testkit_pages.cpp: the test kit's page adapters for the original game's own screens (ce_testkit.h): its message
// box, its front end (the main menu and the Load Game list) and its pause menu in a level. Each reads the screen's own model
// through the original's headers; nothing here changes game state. The CE pages register theirs in their own kit files
// (ce_frontend_testkit.cpp, ce_extras_testkit.cpp). Compiled only by build.py --test-kit.
#include "common.h"
#include "frontend.h"
#include "FEPMain.h"
#include "FEPDirectory.h"
#include "FEMessBox.h"
#include "PauseMenu.h"
#include "MenuItem.h"
#include "MenuOptionsList.h"
#include "game.h"
#include "../platform/common/ce_testkit.h"
#include <stdio.h>
#include <string.h>

void	CeTestKitUtf8(const WCHAR *in, char *out, size_t size)
{
	size_t n = 0;
	for (; in && *in && n + 4 < size; ++in)
	{
		const unsigned c = unsigned(*in);
		if (c < 0x80) out[n++] = char(c);
		else if (c < 0x800) { out[n++] = char(0xc0 | (c >> 6)); out[n++] = char(0x80 | (c & 63)); }
		else { out[n++] = char(0xe0 | (c >> 12)); out[n++] = char(0x80 | ((c >> 6) & 63)); out[n++] = char(0x80 | (c & 63)); }
	}
	if (size) out[n < size ? n : size - 1] = 0;
}

int		CeTestKitMenuRows(CMenuOptionsList *list, CeTestRow *out, int max)
{
	int n = 0;
	for (int i = 0; list && i < list->mItems.Size() && n < max; ++i)
	{
		CMenuItem *item = list->mItems.At(i);
		WCHAR *text = item ? item->GetText() : NULL;
		CeTestKitUtf8(text, out[n].text, sizeof out[n].text);
		out[n].enabled = item && item->IsEnabled();
		++n;
	}
	return n;
}


namespace
{
// The front end's pages are a protected member; a pointer to it, taken through a derived class (as the editor reads the
// game's protected fields, EditorGameAccess), reads it without changing anything.
struct FrontEndAccess : CFrontEnd
{
	static CFrontEndPage *Page(int page) { return (FRONTEND.*(&FrontEndAccess::mPages))[page]; }
};

// ---- The message box (any screen): highest priority while it shows ----------------------------------------------------
bool	BoxActive() { return FEMESSBOX.BeingDisplayed() != FALSE; }
void	BoxId(char *out, size_t size) { snprintf(out, size, "box"); }
void	BoxTitle(char *out, size_t size) { if (size) out[0] = 0; }
void	BoxText(char *out, size_t size)
{
	size_t used = 0;
	if (size) out[0] = 0;
	for (int i = 0; i < FEMESSBOX.mNumLines && i < 40 && used + 2 < size; ++i)
	{
		char line[300];
		CeTestKitUtf8(FEMESSBOX.mLines.c[i], line, sizeof line);
		used += size_t(snprintf(out + used, size - used, "%s%s", used ? " " : "", line));
		if (used >= size) { out[size - 1] = 0; break; }
	}
}
int		BoxRows(CeTestRow *out, int max)
{
	int n = 0;
	if (FEMESSBOX.mOption == FEMB_OPTION_YESNO)
	{
		const char *names[2] = {"Yes", "No"};
		for (int i = 0; i < 2 && n < max; ++i) { snprintf(out[n].text, sizeof out[n].text, "%s", names[i]); out[n++].enabled = true; }
	}
	else if (FEMESSBOX.mOption != FEMB_OPTION_NONE && max > 0)
	{
		snprintf(out[0].text, sizeof out[0].text, "%s", FEMESSBOX.mOption == FEMB_OPTION_OK ? "OK" : "Continue");
		out[0].enabled = true;
		n = 1;
	}
	return n;
}
int		BoxFocused() { return FEMESSBOX.mOption == FEMB_OPTION_YESNO ? FEMESSBOX.mCursor : 0; }

// ---- The original front end ----------------------------------------------------------------------------------------------
const char *const kFrontEndPages[FEP_NUM_PAGES] = {
	"main", "development", "common", "be-config", "wingmen", "briefing", "debriefing", "level-select", "goodies", "dev-select",
	"load-game", "save-game", "intro", "virtual-keyboard", "directory", "multiplayer-start", "multiplayer", "options",
	"credits", "screen-pos", "e3-level-select", "onslaught-ce", "demo-main", "unknown"};

bool	FrontEndActive()
{
	if (GAME.GetGameState() != GAME_STATE_NOT_RUNNING) return false;
	return FRONTEND.GetCurrentPage() != FEP_LANGUAGE_TEST;	// the CE page (patch 0050) has its own adapter (ce_frontend.cpp)
}
void	FrontEndId(char *out, size_t size)
{
	const EFrontEndPage page = FRONTEND.GetCurrentPage();
	if (page >= 0 && page < FEP_NUM_PAGES) snprintf(out, size, "fe:%s", kFrontEndPages[page]);
	else snprintf(out, size, "fe:transition");
}
void	FrontEndTitle(char *out, size_t size) { if (size) out[0] = 0; }
int		FrontEndRows(CeTestRow *out, int max)
{
	const EFrontEndPage page = FRONTEND.GetCurrentPage();
	int n = 0;
	if (page == FEP_MAIN)
	{
		CFEPMain *main = (CFEPMain *)FrontEndAccess::Page(FEP_MAIN);
		for (int i = 0; main && i < main->GetNumActions() && n < max; ++i)
		{
			CeTestKitUtf8(main->GetActionText(i), out[n].text, sizeof out[n].text);
			out[n].enabled = main->IsActionAvailable(i) != FALSE;
			++n;
		}
	}
	else if (page == FEP_DIRECTORY)
	{
		CFEPDirectory *dir = (CFEPDirectory *)FrontEndAccess::Page(FEP_DIRECTORY);
		for (int i = 0; dir && i < dir->mNumSaves && n < max; ++i)
		{
			CeTestKitUtf8(dir->mSaves[i], out[n].text, sizeof out[n].text);
			out[n++].enabled = true;
		}
	}
	return n;
}
int		FrontEndFocused()
{
	const EFrontEndPage page = FRONTEND.GetCurrentPage();
	if (page == FEP_MAIN) { CFEPMain *main = (CFEPMain *)FrontEndAccess::Page(FEP_MAIN); return main ? main->mSelected : -1; }
	if (page == FEP_DIRECTORY) { CFEPDirectory *dir = (CFEPDirectory *)FrontEndAccess::Page(FEP_DIRECTORY); return dir ? dir->mCursor : -1; }
	return -1;
}
void	NoBox(char *out, size_t size) { if (size) out[0] = 0; }

// ---- The pause menu in a level -----------------------------------------------------------------------------------------
bool	PauseActive()
{
	CPauseMenu *menu = GAME.GetPauseMenu();
	return GAME.GetGameState() != GAME_STATE_NOT_RUNNING && menu && menu->mActive;
}
void	PauseId(char *out, size_t size) { CPauseMenu *menu = GAME.GetPauseMenu(); snprintf(out, size, "pause:%d", menu ? menu->mPage : -1); }
int		PauseRows(CeTestRow *out, int max)
{
	CPauseMenu *menu = GAME.GetPauseMenu();
	return menu && menu->mPage >= 0 && menu->mPage < menu->mLists.Size() ? CeTestKitMenuRows(menu->mLists.At(menu->mPage), out, max) : 0;
}
int		PauseFocused()
{
	CPauseMenu *menu = GAME.GetPauseMenu();
	return menu && menu->mPage >= 0 && menu->mPage < menu->mLists.Size() ? menu->mLists.At(menu->mPage)->mSelected : -1;
}


// ---- State every journey can read ----------------------------------------------------------------------------------------
bool	StateGameState(char *out, size_t size) { snprintf(out, size, "%d", int(GAME.GetGameState())); return true; }
bool	StateLevel(char *out, size_t size)
{
	if (GAME.GetGameState() == GAME_STATE_NOT_RUNNING) return false;
	snprintf(out, size, "%d", GAME.GetCurrentLevel());
	return true;
}
// yes in a build with the network (build.py --with-network), no in the default public build
// (build.py defines CE_NETWORK_OFF for a public build without the network; the network header is not included here, since a
// default-build file must not include it: network_default_text_tests.py)
bool	StateBuildNetwork(char *out, size_t size)
{
#ifdef CE_NETWORK_OFF
	snprintf(out, size, "no");
#else
	snprintf(out, size, "yes");
#endif
	return true;
}
bool	StateFrontEndPage(char *out, size_t size)
{
	if (GAME.GetGameState() != GAME_STATE_NOT_RUNNING) return false;
	FrontEndId(out, size);
	return true;
}
}

CE_TEST_PAGE("box", 100, BoxActive, BoxId, BoxTitle, BoxRows, BoxFocused, BoxText);
CE_TEST_PAGE("fe", 10, FrontEndActive, FrontEndId, FrontEndTitle, FrontEndRows, FrontEndFocused, NoBox);
CE_TEST_PAGE("pause", 20, PauseActive, PauseId, FrontEndTitle, PauseRows, PauseFocused, NoBox);
CE_TEST_STATE("game.state", StateGameState);
CE_TEST_STATE("build.network", StateBuildNetwork);
CE_TEST_STATE("game.level", StateLevel);
CE_TEST_STATE("frontend.page", StateFrontEndPage);
