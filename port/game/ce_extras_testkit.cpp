// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_extras_testkit.cpp: the test kit's page adapter for the Extras host and the state keys of its two backup
// pages (ce_testkit.h; port/journeys/README.md). Compiled only by build.py --test-kit; it reads the host through ce_extras.h's
// plain accessors and changes nothing.
#include "common.h"
#include "ce_extras.h"
#include "MenuOptionsList.h"
#include "../platform/common/ce_testkit.h"
#include <stdio.h>

void	CeTestKitUtf8(const WCHAR *in, char *out, size_t size);			// ce_testkit_pages.cpp
int		CeTestKitMenuRows(CMenuOptionsList *list, CeTestRow *out, int max);

namespace
{
const char *const kXpPages[] = {"hub", "careers", "record", "missions", "goodies", "music", "catalogue", "lore", "article",
								"contents", "stored-settings", "backup", "backup-preview", "sounds", "backup-all"};
static_assert(sizeof kXpPages / sizeof kXpPages[0] == XP_COUNT, "one page name per ECeExtrasPage");

bool	XpActive() { return PortCeExtrasActive(); }
void	XpId(char *out, size_t size)
{
	const int page = PortCeExtrasPageShown();
	snprintf(out, size, "xp:%s", page >= 0 && page < XP_COUNT ? kXpPages[page] : "other");
}
void	XpTitle(char *out, size_t size)
{
	if (size) out[0] = 0;
	if (const SCeListPage *page = PortCeExtrasListShown()) CeTestKitUtf8(page->mTitle, out, size);
}
int		XpRows(CeTestRow *out, int max)
{
	if (const SCeListPage *page = PortCeExtrasListShown())
	{
		const int count = page->mCount ? page->mCount() : 0;
		int n = 0;
		for (int i = 0; i < count && n < max; ++i)
		{
			SCeListRow row{};
			page->mRow(i, &row);
			char left[200], right[64];
			CeTestKitUtf8(row.mLeft, left, sizeof left);
			CeTestKitUtf8(row.mRight, right, sizeof right);
			// the row's own words; a right-hand state word follows two spaces
			snprintf(out[n].text, sizeof out[n].text, right[0] ? "%s  %s" : "%s", left, right);
			out[n++].enabled = !row.mDim;
		}
		return n;
	}
	CMenuOptionsList *rows = PortCeExtrasMenuRows();
	return rows ? CeTestKitMenuRows(rows, out, max) : 0;
}
int		XpFocused()
{
	if (PortCeExtrasListShown()) return PortCeExtrasListCursor();
	CMenuOptionsList *rows = PortCeExtrasMenuRows();
	return rows ? rows->mSelected : -1;
}
void	NoBox(char *out, size_t size) { if (size) out[0] = 0; }

bool	StateBackup(char *out, size_t size)
{
	static const char *const names[] = {"idle", "picking", "working", "other", "ok", "error"};
	const int state = PortCeBackupViewState();
	if (state < 0 || state > 5) return false;
	snprintf(out, size, "%s", names[state]);
	return true;
}
bool	StateRestorePhase(char *out, size_t size)
{
	static const char *const names[] = {"idle", "proposing", "confirm", "working", "done"};
	const int phase = PortCeRestorePhase();
	if (!PortCeRestoreShown() || phase < 0 || phase > 4) return false;
	snprintf(out, size, "%s", names[phase]);
	return true;
}
bool	StateRestoreOutcome(char *out, size_t size)
{
	static const char *const names[] = {"none", "complete", "uncertain", "refused", "changed", "occupied", "busy", "cancelled",
										"unsupported", "name", "authority", "failed"};
	const int outcome = PortCeRestoreOutcome();
	if (!PortCeRestoreShown() || outcome < 0 || outcome > 11) return false;
	snprintf(out, size, "%s", names[outcome]);
	return true;
}
bool	StateRestoreTarget(char *out, size_t size)
{
	const char *target = PortCeRestoreTarget();
	if (!PortCeRestoreShown() || !target || !target[0]) return false;
	snprintf(out, size, "%s", target);
	return true;
}
bool	StatePreviewVerified(char *out, size_t size)
{
	if (!PortCeRestoreShown()) return false;
	snprintf(out, size, "%s", PortCePreviewVerified() ? "yes" : "no");
	return true;
}
}

// Right and Left page a list box by its ten rows in view.
CE_TEST_PAGE("xp", 40, XpActive, XpId, XpTitle, XpRows, XpFocused, NoBox, 10, 79, 0x14d, 80, 0x14b);
CE_TEST_STATE("backup.state", StateBackup);
CE_TEST_STATE("restore.phase", StateRestorePhase);
CE_TEST_STATE("restore.outcome", StateRestoreOutcome);
CE_TEST_STATE("restore.target", StateRestoreTarget);
CE_TEST_STATE("preview.verified", StatePreviewVerified);
