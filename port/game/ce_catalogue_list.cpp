// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_catalogue_list.cpp: the Catalogue's sound lists (Sound effects and each language's Speech) as a list box on the
// Extras host (ce_extras.cpp), as the Service Record's Missions and the Music Player show theirs: whole lines left-aligned, scrolled
// by the keys, the held keys and the wheel, the playing row marked. The rows, details and playing come from the Catalogue's own
// model (ce_catalogue_menu.cpp); the details box and Find are the Onslaught CE page's own (ce_frontend.cpp).
//
// Rows: every match, then "Find by name", then "Show all" while a search is on. A row shows its whole title when it fits (about
// kRowChars letters of the smaller face); a longer one is cut at the end, at a space, unless a row within a view of it would then read the same
// (judged on the cut text),
// when it keeps its start and its own end ("Muspell Ground F... the East of the base."). A match's right-hand column is "Playing"
// (green) on the row that is playing, else, only where a nearby row has exactly the same title, its file's own name in dark grey.
// The highlighted row's whole title is always in the hint under the box.
#ifdef CE_VERSION

#ifndef WCHAR
#include "common.h"		// the game's types; the host check (ce_catalogue_sounds_tests.py) gives WCHAR itself
#endif
#include "ce_extras.h"
#include "ce_catalogue_menu.h"
#include "ce_catalogue_sounds.h"

#include <stdio.h>
#include <string.h>

void	PortCeCatalogueListDetails(const char *text);		// ce_frontend.cpp: the CE page's own details box
void	PortCeCatalogueListFind();						// ce_frontend.cpp: the CE page's Find, returning to this list

namespace
{
WCHAR	sTitle[48];

unsigned	Matches() { return PortCeCatalogueMatches(); }
bool		Searching() { const char *q = PortCeCatalogueQuery(); return q && *q; }
bool		IsItem(int index) { return (index >= 0) && (unsigned(index) < Matches()); }
unsigned	Slot(int index) { PortCeCatalogueShowItem(unsigned(index)); return unsigned(index) % CE_CATALOGUE_SLOTS; }

void	ItemTitle(int index, char *out, unsigned size)
{
	const unsigned slot = Slot(index);
	PortCeCatalogueTitle(slot, out, size);
}

const unsigned	kRowChars = 42;		// letters of the smaller face that fit a widened row (the list box still cuts anything wider)
const int		kNear = 10;			// rows either side that are compared: a view's worth

// What a row shows when it is cut at its end: the text up to the last space that leaves room for "...", without the
// stop or comma before it (so a sentence's own "." does not become "...."); a title that fits is shown whole.
void	EndCut(const char *text, char *out, unsigned size)
{
	const size_t length = strlen(text);
	if (length <= kRowChars)
	{
		snprintf(out, size, "%s", text);
		return;
	}
	size_t cut = kRowChars - 3;
	while (cut > kRowChars / 2 && text[cut] != ' ')
		cut--;
	if (text[cut] != ' ')
		cut = kRowChars - 3;
	while (cut > 1 && strchr(" .,;:", text[cut - 1]))
		cut--;
	snprintf(out, size, "%.*s...", int(cut), text);
}

bool	Playing(int index)
{
	return PortCeCatalogueSoundsPlaying() && (index == PortCeCatalogueSelectedIndex());
}

int		Count()
{
	return int(Matches()) + 1 + (Searching() ? 1 : 0);
}

void	Row(int index, SCeListRow *row)
{
	const int left = int(sizeof(row->mLeft) / sizeof(*row->mLeft)), right = int(sizeof(row->mRight) / sizeof(*row->mRight));
	if (!IsItem(index))
	{
		PortCeExtrasWiden(row->mLeft, left, unsigned(index) == Matches() ? "Find by name" : "Show all");
		return;
	}
	char text[160];
	ItemTitle(index, text, sizeof text);
	// look-alikes are judged on what the rows would show once cut at their ends
	bool same = false, alike = false;
	char other[160], cut[160], otherCut[160];
	EndCut(text, cut, sizeof cut);
	for (int d = -kNear; d <= kNear; d++)
		if (d && IsItem(index + d))
		{
			ItemTitle(index + d, other, sizeof other);
			EndCut(other, otherCut, sizeof otherCut);
			same = same || !strcmp(text, other);
			alike = alike || (strcmp(text, other) && !strcmp(cut, otherCut));
		}
	const size_t length = strlen(text);
	if (alike && length > kRowChars)
	{
		// its start, then its own end: the words that tell it from the rows that begin the same, each cut at a space
		size_t head = 17, from = length - (kRowChars - 20);
		while (head > 8 && text[head] != ' ')
			head--;
		if (text[head] != ' ')
			head = 17;
		size_t at = from;
		while (at < length && text[at] != ' ')
			at++;
		if (at < length)
			from = at;
		char shown[160];
		snprintf(shown, sizeof shown, "%.*s...%s", int(head), text, text + from);
		PortCeExtrasWiden(row->mLeft, left, shown);
	}
	else
		PortCeExtrasWiden(row->mLeft, left, cut);
	if (Playing(index))
	{
		PortCeExtrasWiden(row->mRight, right, "Playing");
		row->mTone = CE_TONE_GOOD;
		return;
	}
	// rows with exactly the same title are told apart by their files
	if (same)
	{
		char tag[40];
		PortCeCatalogueRowTag(Slot(index), tag, sizeof tag);
		PortCeExtrasWiden(row->mRight, right, tag);
		row->mTone = CE_TONE_DIM;
	}
}

void	Hint(int index, WCHAR *help, WCHAR *extra, int n)
{
	char line[160];
	if (!IsItem(index))
	{
		PortCeExtrasWiden(help, n, unsigned(index) == Matches() ? "Type part of a name to find it." : "Show every item again.");
		if (Searching())
			snprintf(line, sizeof line, "%u found for \"%s\".", Matches(), PortCeCatalogueQuery());
		else
			snprintf(line, sizeof line, "%u items.", Matches());
		PortCeExtrasWiden(extra, n, line);
		return;
	}
	// the highlighted row's whole line, however long, then where it is and what Select does
	ItemTitle(index, line, sizeof line);
	PortCeExtrasWiden(help, n, line);
	char where[96];
	snprintf(where, sizeof where, Searching() ? "Item %d of %u found. %s" : "Item %d of %u. %s", index + 1, Matches(),
			 Playing(index) ? "Playing: choose another, or go back to stop it." : "Select to hear it.");
	PortCeExtrasWiden(extra, n, where);
}

bool	Select(int index)
{
	if (!IsItem(index))
	{
		if (unsigned(index) == Matches())
		{
			PortCeCatalogueSoundsStop();					// opening Find leaves the list, so its sound ends as on every other way out
			PortCeCatalogueListFind();
		}
		else
		{
			PortCeCatalogueFilter("");
			const int at = PortCeCatalogueSelectedIndex();
			PortCeExtrasShow(XP_SOUNDS, at >= 0 ? at : 0);
		}
		return true;
	}
	const unsigned slot = Slot(index);
	PortCeCatalogueSelect(slot);
	char text[1536];
	PortCeCatalogueDetails(slot, text, sizeof text);
	if (!PortCeCataloguePlaySound(slot))
	{
		const char *problem = PortCeCatalogueSoundProblem();
		strncat(text, "\n", sizeof(text) - strlen(text) - 1);
		strncat(text, *problem ? problem : "This sound is no longer available.", sizeof(text) - strlen(text) - 1);
	}
	PortCeCatalogueListDetails(text);
	return true;
}

const WCHAR	*Empty()
{
	return L"Nothing to list.";
}

// Leaving the list ends its sound (Find, which keeps the host's page and does not come here, stops it in Select).
void	Close()
{
	PortCeCatalogueSoundsStop();
}
}

void	PortCeCatalogueListTitle(const char *label)
{
	PortCeExtrasWiden(sTitle, int(sizeof(sTitle) / sizeof(*sTitle)), label);
}

const SCeListPage	*PortCeCatalogueSoundsListPage()
{
	// the smaller face, so that most spoken lines fit their row; the highlighted line is whole in the hint
	static const SCeListPage page = {sTitle, Count, Row, Hint, Select, Empty, NULL, Close, NULL, NULL, 1.f, 0, true, 20.f};
	return &page;
}

#endif
