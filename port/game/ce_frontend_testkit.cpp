// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_frontend_testkit.cpp: the test kit's page adapter for the Onslaught CE page (ce_testkit.h; port/journeys/README.md).
// Compiled only by build.py --test-kit; it reads the page through ce_frontend.h's plain accessors and changes nothing.
#include "common.h"
#include "frontend.h"
#include "game.h"
#include "MenuOptionsList.h"
#include "ce_frontend.h"
#include "../platform/common/ce_testkit.h"
#include <stdio.h>

void	CeTestKitUtf8(const WCHAR *in, char *out, size_t size);			// ce_testkit_pages.cpp
int		CeTestKitMenuRows(CMenuOptionsList *list, CeTestRow *out, int max);

namespace
{
const char *const kCePages[] = {"hub", "bonus", "settings", "cheats", "help", "plans", "editor", "museum", "catalogue",
								"catalogue-items", "catalogue-find"};
const int kCePageNames = int(sizeof kCePages / sizeof kCePages[0]);

bool	CeActive()
{
	return GAME.GetGameState() == GAME_STATE_NOT_RUNNING && FRONTEND.GetCurrentPage() == FEP_LANGUAGE_TEST &&
		   PortCeFrontendMenuShown();
}
void	CeId(char *out, size_t size)
{
	const int page = PortCeFrontendPage();
	// a page this table does not name yet shows as "other" (the table follows ECePage in ce_frontend.cpp)
	snprintf(out, size, "ce:%s", page >= 0 && page < kCePageNames && page < PortCeFrontendPageCount() ? kCePages[page] : "other");
}
void	CeTitle(char *out, size_t size)
{
	const WCHAR *title = PortCeFrontendPageTitle(PortCeFrontendPage());
	if (title) CeTestKitUtf8(title, out, size);
	else if (size) out[0] = 0;
}
int		CeRows(CeTestRow *out, int max)
{
	CMenuOptionsList *rows = PortCeFrontendRows();
	return rows ? CeTestKitMenuRows(rows, out, max) : 0;
}
int		CeFocused()
{
	CMenuOptionsList *rows = PortCeFrontendRows();
	return rows ? rows->mSelected : -1;
}
void	CeBox(char *out, size_t size) { if (size) out[0] = 0; }
}

CE_TEST_PAGE("ce", 30, CeActive, CeId, CeTitle, CeRows, CeFocused, CeBox);
