// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_catalogue_list.cpp: the Catalogue's lists (Models, Textures, Sound effects and each language's Speech) as a list box on the
// Extras host (ce_extras.cpp), as the Service Record's Missions and the Music Player show theirs: whole lines left-aligned, scrolled
// by the keys, the held keys and the wheel, the playing row marked. The rows, details and playing come from the Catalogue's own
// model (ce_catalogue_menu.cpp); the details box and Find are the Onslaught CE page's own (ce_frontend.cpp).
//
// Rows: the matches sorted by group, each group once as a heading (the cursor passes over it; the units first among the models,
// the others in alphabetical order), then "Find by name", then "Show all" while a search is on. A list without groups (speech) has no
// headings. A row shows its name whole when it fits (about kRowChars letters of the smaller face); a longer one is cut at the end, at
// a space, unless a row within a view of it would then read the same (judged on the cut text), when it keeps its start and its own end
// ("Muspell Ground... the East of the base."). A match's right-hand column is "Playing" (green) on the row that is playing.
//
// Textures and Models show a preview pane beside the list (list left, preview right): it loads once the cursor has rested a quarter of
// a second on a row (or at once on Select for textures), so moving down the list loads nothing and nothing flashes; the caption under
// it names the item and its group (and size for textures).
#ifdef CE_VERSION

#ifndef WCHAR
#include "common.h"		// the game's types; the host check (ce_catalogue_sounds_tests.py) gives WCHAR itself
#endif
#include "ce_extras.h"
#include "ce_catalogue_menu.h"
#include "ce_catalogue_sounds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void	PortCeCatalogueListDetails(const char *text);		// ce_frontend.cpp: the CE page's own details box
void	PortCeCatalogueListFind();						// ce_frontend.cpp: the CE page's Find, returning to this list
void	PortCeCatalogueListPicture(const char *text);	// ce_frontend.cpp: the same box, placed low under a texture's picture
bool	PortCeCataloguePreviewOpen(unsigned slot);		// ce_catalogue_preview.cpp: a texture's picture
void	PortCeCataloguePreviewClose();
bool	PortCeCataloguePreviewDrawIn(float left, float top, float right, float bottom, bool picture, bool checker);
void	PortCeCataloguePaneText(const char *first, const char *second, float left, float right, float y);
float	PortCeCatalogueNow();
bool	PortCeCatalogueModelLoad(int goodie);			// ce_catalogue_preview.cpp: patch 0251's model pane
void	PortCeCatalogueModelFree();
bool	PortCeCatalogueModelDraw(float left, float top, float right, float bottom, float yaw);

namespace
{
WCHAR	sTitle[48];

const unsigned	kRowChars = 42;		// letters of the smaller face that fit a widened row (the list box still cuts anything wider)
const unsigned	kPaneRowChars = 21;	// the same beside the picture pane (measured: 22 with the dots is the most the box keeps)
const unsigned	kModelRowChars = 21;	// fits the ~212px text width beside the model pane cleanly with no double ellipsis
const int		kNear = 10;			// rows either side that are compared: a view's worth
const float		kSettle = 0.25f;	// seconds the cursor rests on a row before its preview is read

unsigned	Matches() { return PortCeCatalogueMatches(); }
bool		Searching() { const char *q = PortCeCatalogueQuery(); return q && *q; }
unsigned	Slot(unsigned match) { PortCeCatalogueShowItem(match); return match % CE_CATALOGUE_SLOTS; }

// Each match's group and name, read once per rebuild (Build): the naming rules are too slow to run for every row on every frame.
enum { kCacheGroup = 48, kCacheName = 96 };
struct SNames { char mGroup[kCacheGroup]; char mName[kCacheName]; };
SNames		*sNames;
int			sNameCount, sNameSpace;
void	Build();

void	ReadParts(unsigned match, char *group, unsigned group_size, char *name, unsigned name_size)
{
	PortCeCatalogueItemParts(Slot(match), group, group_size, name, name_size);
}

void	Parts(unsigned match, char *group, unsigned group_size, char *name, unsigned name_size)
{
	Build();
	if (int(match) < sNameCount)
	{
		snprintf(group, group_size, "%s", sNames[match].mGroup);
		snprintf(name, name_size, "%s", sNames[match].mName);
		return;
	}
	ReadParts(match, group, group_size, name, name_size);
}

// ---- The rows: headings and matches in group order ---------------------------------------------------------------------------
// sRows[r] >= 0 is a match; -1 - g is group g's heading; kActions, kFind and kShowAll sit at the end.
enum { kFind = -100000, kShowAll = -100001, kActions = -100002, kGroupChars = 48 };
int			*sRows;
int			sRowCount, sRowSpace;
int			*sNumbers;			// per row: its number among same-named rows of its group, 0 when its name is its own
int			sNumberSpace;
char		(*sGroups)[kGroupChars];
int			sGroupCount, sGroupSpace;
// what the rows were made for: rebuilt when the collection, its matches or the search change
int			sBuiltCategory = -1;
unsigned	sBuiltMatches = 0;
char		sBuiltQuery[CE_CATALOGUE_QUERY_MAX + 1];
int			sSizeRow = -1;
unsigned	sSizeW = 0, sSizeH = 0;
bool		sSizeAlpha = false, sSizeOk = false;

bool	Grow(void **block, int *space, int need, size_t one)
{
	if (*space >= need) return true;
	int next = *space ? *space * 2 : 64;
	while (next < need) next *= 2;
	void *fresh = realloc(*block, size_t(next) * one);
	if (!fresh) return false;
	*block = fresh;
	*space = next;
	return true;
}

int		GroupBefore(const char *a, const char *b)
{
	// Units first among the models (the player's main interest); Models / Textures last; the rest in alphabetical order
	auto rank = [](const char *g) {
		if (!strcmp(g, "Units")) return 0;
		if (!strcmp(g, "Models") || !strcmp(g, "Textures")) return 2;
		return 1;
	};
	const int ra = rank(a), rb = rank(b);
	return ra != rb ? ra - rb : strcmp(a, b);
}

void	Build()
{
	const int category = PortCeCatalogueCategory();
	const unsigned matches = Matches();
	const char *query = PortCeCatalogueQuery();
	if (category == sBuiltCategory && matches == sBuiltMatches && !strcmp(query, sBuiltQuery))
		return;
	sBuiltCategory = category;
	sBuiltMatches = matches;
	snprintf(sBuiltQuery, sizeof sBuiltQuery, "%s", query);
	sGroupCount = 0;
	sNameCount = 0;
	sSizeRow = -1;
	sSizeOk = false;
	const bool cache = Grow((void **)&sNames, &sNameSpace, int(matches ? matches : 1), sizeof(SNames));
	int *group_of = (int *)malloc(size_t(matches ? matches : 1) * sizeof(int));
	char group[kGroupChars], name[kCacheName];
	for (unsigned m = 0; group_of && m < matches; m++)
	{
		ReadParts(m, group, sizeof group, name, sizeof name);
		if (cache)
		{
			snprintf(sNames[m].mGroup, kCacheGroup, "%s", group);
			snprintf(sNames[m].mName, kCacheName, "%s", name);
		}
		int g = 0;
		while (g < sGroupCount && strcmp(sGroups[g], group)) g++;
		if (g == sGroupCount)
		{
			if (!Grow((void **)&sGroups, &sGroupSpace, sGroupCount + 1, kGroupChars)) { group_of[m] = 0; continue; }
			snprintf(sGroups[sGroupCount++], kGroupChars, "%s", group);
		}
		group_of[m] = g;
	}
	// the groups' order, as a dynamically sized index list so no group is ever silently dropped
	int *order = (int *)malloc(size_t(sGroupCount ? sGroupCount : 1) * sizeof(int));
	const int groups = order ? sGroupCount : 0;
	for (int g = 0; g < groups; g++)
	{
		int at = g;
		while (at > 0 && GroupBefore(sGroups[order[at - 1]], sGroups[g]) > 0) { order[at] = order[at - 1]; at--; }
		order[at] = g;
	}
	const bool headings = !(groups == 1 && !sGroups[0][0]) && groups > 0;
	sRowCount = 0;
	Grow((void **)&sRows, &sRowSpace, int(matches) + groups + 3, sizeof(int));
	for (int k = 0; sRows && k < groups; k++)
	{
		const int g = order[k];
		if (headings) sRows[sRowCount++] = -1 - g;
		for (unsigned m = 0; group_of && m < matches; m++)
			if (group_of[m] == g) sRows[sRowCount++] = int(m);
	}
	free(order);
	if (!groups && matches)	// no groups read (nothing to group): the matches in their own order
		for (unsigned m = 0; m < matches; m++) sRows[sRowCount++] = int(m);
	if (cache && group_of) sNameCount = int(matches);
	free(group_of);
	// same-named rows of a group, numbered once here, not on every draw: names read once each, compared within the group
	if (sRows && Grow((void **)&sNumbers, &sNumberSpace, sRowCount + 3, sizeof(int)))
	{
		enum { kNameChars = 96 };
		char (*names)[kNameChars] = (char (*)[kNameChars])malloc(size_t(sRowCount ? sRowCount : 1) * kNameChars);
		char row_group[kGroupChars];
		for (int r = 0; r < sRowCount; r++)
		{
			sNumbers[r] = 0;
			if (names && sRows[r] >= 0) Parts(unsigned(sRows[r]), row_group, sizeof row_group, names[r], kNameChars);
		}
		// rows of one group are together, between its heading and the next: compare within that run only
		for (int start = 0; names && start < sRowCount; )
		{
			int end = start + 1;
			while (end < sRowCount && sRows[end] >= 0) end++;
			for (int r = start; r < end; r++)
			{
				if (sRows[r] < 0 || sNumbers[r]) continue;
				int count = 1;
				for (int o = r + 1; o < end; o++)
					if (sRows[o] >= 0 && !sNumbers[o] && !strcmp(names[o], names[r])) sNumbers[o] = ++count;
				if (count > 1) sNumbers[r] = 1;
			}
			start = end;
		}
		free(names);
	}
	if (sRows)
	{
		if (headings) sRows[sRowCount++] = kActions;
		sRows[sRowCount++] = kFind;
		if (Searching()) sRows[sRowCount++] = kShowAll;
	}
}

int		At(int row) { Build(); return (row >= 0 && row < sRowCount) ? sRows[row] : kFind; }
bool	IsItem(int row) { return At(row) >= 0; }
bool	Heading(int row) { const int at = At(row); return at == kActions || (at < 0 && at != kFind && at != kShowAll); }

// The place of a row's match among the matches as listed (headings not counted), from 1.
int		Place(int row)
{
	int place = 0;
	for (int r = 0; r <= row && r < sRowCount; r++)
		if (sRows[r] >= 0) place++;
	return place;
}

// A row's name; when other rows of its group have exactly the same name, its number among them ("Fire Ball 2").
void	ItemName(int row, char *out, unsigned size)
{
	char group[kGroupChars];
	Parts(unsigned(At(row)), group, sizeof group, out, size);
	if (row >= 0 && row < sRowCount && sRows[row] >= 0 && sNumbers && sNumbers[row])
	{
		const size_t length = strlen(out);
		snprintf(out + length, size > length ? size - length : 0, " %d", sNumbers[row]);
	}
}

// A name cut in its middle to `chars` letters: its start up to a space, "...", then its end from a space; both cut mid-word only
// when no space fits.
void	MiddleCut(const char *text, char *out, unsigned size, unsigned chars)
{
	const size_t length = strlen(text);
	if (length <= chars || chars < 9)
	{
		snprintf(out, size, "%s", text);
		return;
	}
	const size_t room = chars - 3;
	size_t head = room * 2 / 5, from = length - (room - head);
	const size_t first = head;
	while (head > 3 && text[head] != ' ') head--;
	if (text[head] != ' ') head = first;
	from = length - (room - head);			// the end gets the room the start left
	size_t at = from;
	while (at < length && text[at - 1] != ' ') at++;
	if (at < length && length - at >= 3) from = at;
	while (head > 0 && (text[head - 1] == ' ' || text[head - 1] == ',')) head--;
	snprintf(out, size, "%.*s...%s", int(head), text, text + from);
}

// What a row shows when it is cut at its end: the text up to the last space that leaves room for "...", without the
// stop or comma before it (so a sentence's own "." does not become "...."); a name that fits is shown whole.
void	EndCut(const char *text, char *out, unsigned size, unsigned chars)
{
	const size_t length = strlen(text);
	if (length <= chars)
	{
		snprintf(out, size, "%s", text);
		return;
	}
	size_t cut = chars - 3;
	while (cut > chars / 2 && text[cut] != ' ')
		cut--;
	if (text[cut] != ' ')
		cut = chars - 3;
	while (cut > 1 && strchr(" .,;:", text[cut - 1]))
		cut--;
	snprintf(out, size, "%.*s...", int(cut), text);
}

unsigned	RowChars();
extern void	(*sPane)(int, float, float, float, float);	// the page's pane, if any (set with the page's title)

bool	Playing(int row)
{
	return IsItem(row) && PortCeCatalogueSoundsPlaying() && (At(row) == PortCeCatalogueSelectedIndex());
}

int		Count()
{
	Build();
	return sRowCount;
}

bool	IsHeading(int index)
{
	return Heading(index);
}

void	Row(int index, SCeListRow *row)
{
	const int left = int(sizeof(row->mLeft) / sizeof(*row->mLeft)), right = int(sizeof(row->mRight) / sizeof(*row->mRight));
	const int at = At(index);
	if (at == kFind || at == kShowAll)
	{
		PortCeExtrasWiden(row->mLeft, left, at == kFind ? "Find by name" : "Show all");
		return;
	}
	if (at == kActions)
	{
		PortCeExtrasWiden(row->mLeft, left, "Actions");
		return;
	}
	if (at < 0)
	{
		PortCeExtrasWiden(row->mLeft, left, sGroups[-1 - at]);
		return;
	}
	const unsigned chars = RowChars();
	char text[160];
	ItemName(index, text, sizeof text);
	if (PortCeCatalogueCategory() == 0)
	{
		// Models: clean word-boundary EndCut by default; fall back to a compact MiddleCut only when a nearby row collides after cutting.
		char cut[160], other[160], otherCut[160];
		EndCut(text, cut, sizeof cut, chars);
		bool alike = false;
		for (int d = -kNear; d <= kNear && !alike; d++)
			if (d && IsItem(index + d))
			{
				ItemName(index + d, other, sizeof other);
				EndCut(other, otherCut, sizeof otherCut, chars);
				alike = strcmp(text, other) != 0 && !strcmp(cut, otherCut);
			}
		if (alike)
		{
			char shown[160];
			MiddleCut(text, shown, sizeof shown, chars > 2 ? chars - 2 : chars);
			PortCeExtrasWiden(row->mLeft, left, shown);
		}
		else
			PortCeExtrasWiden(row->mLeft, left, cut);
		return;
	}
	if (PortCeCatalogueCategory() == 1)
	{
		// Textures: rows are told apart by what differs. A run of names sharing a prefix up to a comma shows it on its
		// first row only ("Muspell Language, Graphics 1", then "Graphics 2"); a name still too long is cut in its middle, so its
		// end, where such names differ, stays. The caption and the foot keep the whole name.
		char shown[160];
		const char *comma = strrchr(text, ',');
		const char *tail = (comma && comma[1] == ' ') ? comma + 2 : NULL;
		if (tail && IsItem(index - 1))
		{
			char above[160];
			ItemName(index - 1, above, sizeof above);
			const size_t prefix = size_t(tail - text);
			if (!strncmp(above, text, prefix) && strlen(above) > prefix)
				memmove(text, tail, strlen(tail) + 1);
		}
		MiddleCut(text, shown, sizeof shown, chars);
		PortCeExtrasWiden(row->mLeft, left, shown);
		return;
	}
	// look-alikes are judged on what the rows would show once cut at their ends
	bool same = false, alike = false;
	char other[160], cut[160], otherCut[160];
	EndCut(text, cut, sizeof cut, chars);
	for (int d = -kNear; d <= kNear; d++)
		if (d && IsItem(index + d))
		{
			ItemName(index + d, other, sizeof other);
			EndCut(other, otherCut, sizeof otherCut, chars);
			same = same || !strcmp(text, other);
			alike = alike || (strcmp(text, other) && !strcmp(cut, otherCut));
		}
	const size_t length = strlen(text);
	if (alike && length > chars)
	{
		// its start, then its own end: the words that tell it from the rows that begin the same, each cut at a space
		const size_t keep = chars > 26 ? chars - 20 : 6;
		size_t head = chars > 30 ? 17 : 9, from = length > keep ? length - keep : 0;
		const size_t first = head;
		while (head > 4 && text[head] != ' ')
			head--;
		if (text[head] != ' ')
			head = first;
		size_t at_space = from;
		while (at_space < length && text[at_space] != ' ')
			at_space++;
		if (at_space < length)
			from = at_space;
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
	(void)same;		// same-named rows carry their number in the name (ItemName)
}

void	Hint(int index, WCHAR *help, WCHAR *extra, int n)
{
	char line[256];
	const int at = At(index);
	if (at < 0)
	{
		PortCeExtrasWiden(help, n, at == kFind ? "Type part of a name to find it." : at == kShowAll ? "Show every item again." : "");
		if (Searching())
			snprintf(line, sizeof line, "%u found for \"%s\".", Matches(), PortCeCatalogueQuery());
		else
			snprintf(line, sizeof line, "%u items.", Matches());
		PortCeExtrasWiden(extra, n, line);
		return;
	}
	// a page with a pane names the row in the pane's caption: the foot carries only the place and the keys; a list without one
	// gives the highlighted row's whole name with its group
	char group[kGroupChars], name[160];
	Parts(unsigned(at), group, sizeof group, name, sizeof name);
	if (sPane) line[0] = 0;
	else if (group[0]) snprintf(line, sizeof line, "%s: %s", group, name);
	else snprintf(line, sizeof line, "%s", name);
	PortCeExtrasWiden(help, n, line);
	char where[120];
	const unsigned slot = Slot(unsigned(at));
	snprintf(where, sizeof where, Searching() ? "Item %d of %u found. %s" : "Item %d of %u. %s", Place(index), Matches(),
			 Playing(index) ? "Playing: choose another, or go back to stop it." : PortCeCatalogueIsSound(slot) ? "Select to hear it." :
			 "Up and Down to look through them.");
	PortCeExtrasWiden(extra, n, where);
}

// ---- The picture pane (Textures) -----------------------------------------------------------------------------------------------
int		sPaneRow = -1;			// the row whose picture is open
int		sWaitRow = -1;			// the row the cursor rests on, and since when
float	sWaitSince;

void	PaneClose()
{
	PortCeCataloguePreviewClose();
	sPaneRow = sWaitRow = -1;
	sSizeRow = -1;
	sSizeOk = false;
}

bool	CachedTextureSize(int row, unsigned *w, unsigned *h, bool *alpha)
{
	if (!IsItem(row))
		return false;
	if (row != sSizeRow)
	{
		sSizeW = sSizeH = 0;
		sSizeAlpha = false;
		sSizeOk = PortCeCatalogueTextureSize(Slot(unsigned(At(row))), &sSizeW, &sSizeH, &sSizeAlpha);
		sSizeRow = row;
	}
	if (w) *w = sSizeW;
	if (h) *h = sSizeH;
	if (alpha) *alpha = sSizeAlpha;
	return sSizeOk;
}

// Reads the row's picture now (Select, or the cursor has rested on it).
void	PaneOpen(int row)
{
	PortCeCataloguePreviewClose();
	sPaneRow = row;
	if (IsItem(row))
	{
		const unsigned slot = Slot(unsigned(At(row)));
		PortCeCatalogueSelect(slot);
		PortCeCataloguePreviewOpen(slot);
	}
}

void	TexturePane(int row, float left, float top, float right, float bottom)
{
	if (row != sPaneRow)
	{
		if (row != sWaitRow) { sWaitRow = row; sWaitSince = PortCeCatalogueNow(); }
		else if (PortCeCatalogueNow() - sWaitSince >= kSettle) PaneOpen(row);
	}
	const float text = bottom - 42.f;
	unsigned pw = 0, ph = 0;
	bool transparent = false;
	const bool hasSize = CachedTextureSize(row, &pw, &ph, &transparent);
	// while the cursor moves the pane stays empty: no earlier picture beside a later row
	const bool shown = PortCeCataloguePreviewDrawIn(left, top, right, text - 4.f, row == sPaneRow, transparent);
	if (!IsItem(row) || row != sPaneRow)
		return;
	char group[kGroupChars], name[160], detail[160];
	Parts(unsigned(At(row)), group, sizeof group, name, sizeof name);
	// under the picture: its whole name, then its group and size on one grey line (both wrap to the pane)
	if (!shown)
		snprintf(detail, sizeof detail, "%s. This picture could not be read.", group);
	else if (hasSize)
		snprintf(detail, sizeof detail, transparent ? "%s, %u x %u, transparent" : "%s, %u x %u pixels", group, pw, ph);
	else
		snprintf(detail, sizeof detail, "%s", group);
	char cut[64], shown_name[160];
	ItemName(row, shown_name, sizeof shown_name);
	EndCut(shown_name, cut, sizeof cut, 60);
	PortCeCataloguePaneText(cut, detail, left, right, text);
}

// ---- The model pane (Models) ---------------------------------------------------------------------------------------------------
int		sModelRow = -1;			// the row whose model is loaded (a goodie's), and its goodie
int		sModelGoodie = -1;

void	ModelFree()
{
	if (sModelGoodie >= 0)
		PortCeCatalogueModelFree();
	sModelRow = sModelGoodie = -1;
}

void	ModelPane(int row, float left, float top, float right, float bottom)
{
	// moving off the loaded row lets its model go at once; resting a quarter second on a unit loads that one
	if (row != sModelRow && sModelRow >= 0)
		ModelFree();
	int goodie = -1;
	if (IsItem(row))
		goodie = PortCeCatalogueModelGoodie(Slot(unsigned(At(row))));
	if (row != sWaitRow) { sWaitRow = row; sWaitSince = PortCeCatalogueNow(); }
	else if (IsItem(row) && PortCeCatalogueNow() - sWaitSince >= kSettle)
	{
		PortCeCatalogueSelect(Slot(unsigned(At(row))));
		if (goodie >= 0 && row != sModelRow)
		{
			sModelRow = row;
			sModelGoodie = PortCeCatalogueModelLoad(goodie) ? goodie : -1;
			if (sModelGoodie < 0) PortCeCatalogueModelFree();
		}
	}
	const float text = bottom - 44.f;
	if (!IsItem(row))
		return;
	char group[kGroupChars], name[160], cut[64];
	Parts(unsigned(At(row)), group, sizeof group, name, sizeof name);
	char shown_name[160];
	ItemName(row, shown_name, sizeof shown_name);
	EndCut(shown_name, cut, sizeof cut, 60);	// whole: the caption wraps to the pane
	if (goodie < 0)
	{
		PortCeCataloguePreviewDrawIn(left, top, right, text - 4.f, false, false);
		// one quiet wrapped caption in the same grey tone, inset cleanly from the frame
		PortCeCataloguePaneText(NULL, "This model is loaded with its level.", left + 4.f, right - 4.f, (top + text) * 0.5f - 12.f);
		PortCeCataloguePaneText(cut, group, left, right, text);
		return;
	}
	if (row == sModelRow && sModelGoodie >= 0)
		PortCeCatalogueModelDraw(left, top, right, text - 4.f, PortCeCatalogueNow() * 0.5f);	// one turn in about 12 seconds
	else
		PortCeCataloguePreviewDrawIn(left, top, right, text - 4.f, false, false);
	PortCeCataloguePaneText(cut, group, left, right, text);
}

void	(*sPane)(int, float, float, float, float) = NULL;

unsigned	RowChars() { return sPane == ModelPane ? kModelRowChars : sPane ? kPaneRowChars : kRowChars; }

bool	Select(int index)
{
	const int at = At(index);
	if (at == kFind)
	{
		PortCeCatalogueSoundsStop();					// opening Find leaves the list, so its sound and previews end cleanly
		PaneClose();
		ModelFree();
		PortCeCatalogueListFind();
		return true;
	}
	if (at == kShowAll)
	{
		PaneClose();
		ModelFree();
		PortCeCatalogueFilter("");
		PortCeExtrasShow(XP_CATALOGUE_LIST, PortCeCatalogueListRowOf(PortCeCatalogueSelectedIndex()));
		return true;
	}
	if (at < 0)
		return false;
	const unsigned slot = Slot(unsigned(at));
	if (PortCeCatalogueIsTexture(slot))
	{
		PaneOpen(index);								// the picture is the pane beside the list: no box
		return true;
	}
	PortCeCatalogueSelect(slot);
	if (PortCeCatalogueCategory() == 0)
	{
		const int goodie = PortCeCatalogueModelGoodie(slot);
		if (goodie >= 0 && index != sModelRow)
		{
			ModelFree();
			sModelRow = index;
			sModelGoodie = PortCeCatalogueModelLoad(goodie) ? goodie : -1;
			if (sModelGoodie < 0) PortCeCatalogueModelFree();
		}
		return true;									// a model's name, group and place are under its pane: no box
	}
	char text[1536];
	PortCeCatalogueDetails(slot, text, sizeof text);
	if (PortCeCatalogueIsSound(slot))
	{
		if (!PortCeCataloguePlaySound(slot))
		{
			const char *problem = PortCeCatalogueSoundProblem();
			strncat(text, "\n", sizeof(text) - strlen(text) - 1);
			strncat(text, *problem ? problem : "This sound is no longer available.", sizeof(text) - strlen(text) - 1);
		}
		PortCeCatalogueListDetails(text);
	}
	else
		PortCeCatalogueListDetails(text);					// a model: its details
	return true;
}

const WCHAR	*Empty()
{
	return L"Nothing to list.";
}

// Leaving the list ends its sound and lets its preview go (Find, which keeps the host's page and does not come here, stops the sound
// in Select).
void	Close()
{
	PortCeCatalogueSoundsStop();
	PaneClose();
	ModelFree();
	sBuiltCategory = -1;
}

// eleven rows from the Service Record's own top, clear of the emblem at the top left
SCeListPage	sPage = {sTitle, Count, Row, Hint, Select, Empty, NULL, Close, NULL, NULL, 1.f, 0, true, 20.f, 11, 130.f, 0.f, IsHeading, NULL, NULL};
}

void	PortCeCatalogueListTitle(const char *label)
{
	PortCeExtrasWiden(sTitle, int(sizeof(sTitle) / sizeof(*sTitle)), label);
	// the page's shape for this collection: textures and models beside their pane, sounds the full width
	const int category = PortCeCatalogueCategory();
	sPane = (category == 1) ? TexturePane : (category == 0) ? ModelPane : NULL;
	sPage.mPane = sPane;
	sPage.mBoxRight = (category == 1) ? 344.f : (category == 0) ? 372.f : 0.f;
	PaneClose();
	ModelFree();
	sBuiltCategory = -1;
}

int		PortCeCatalogueListRowOf(int match)
{
	Build();
	int first_item = -1, find_row = 0;
	for (int r = 0; r < sRowCount; r++)
	{
		if (sRows[r] == kFind) find_row = r;
		if (sRows[r] >= 0)
		{
			if (first_item < 0) first_item = r;
			if (sRows[r] == match) return r;
		}
	}
	return first_item >= 0 ? first_item : find_row;
}

const SCeListPage	*PortCeCatalogueListPage()
{
	return &sPage;
}

#endif
