// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_extras.cpp: the Extras pages of the Onslaught CE page: the Companion's player features, inside the game and in the
// game's own menu style. This file is the host: the Extras row's switch, the page stack, the game's option lists for the pages that
// are rows, and the list box for the pages that are long lists. What a page shows comes from the feature that owns it
// (ce_service_record.cpp for the Service Record).
//
// On by default wherever the CE menus are shown (PortCeMenus), unless ce.ini sets extras=0 (read once, here).
// Off (extras=0 or CE menus off), the hub has no Extras row and nothing in this file is reached. The pages exist
// only where the CE menus do (PortCeMenus): never in a replay, a recording, the test harness, -autoconfigtest or the demo.
//
// How the pages are made, so that they read as the game's own:
// - The Onslaught CE page (ce_frontend.cpp) keeps drawing the shell: the video background, the sliding borders, the title bar, the
//   help bar and the small logo. An Extras page gives it a title and draws only its own content, inside the CE page's own render
//   states and after its 0.4 s opening.
// - A page of rows is the game's options list (CMenuOptionsList on a CPauseMenu without the pause menu's lists, as the CE page's own
//   lists are): rows 20 apart, the highlighted row yellow, Back last, the highlighted row's hint under Back.
// - A long list is the Load Game page's list box, drawn with the same two front-end calls and the same numbers (ce_scroll_list.h):
//   a dark panel with a thin outline, rows in the larger face, the highlighted row white and pulsing and the others grey, a boxed
//   scroll track with its bar. Up and Down move a row, Left and Right a page; the mouse highlights, clicks, drags the bar and turns
//   the wheel. The highlighted row's hint is under the box.
// - Sounds are the front end's three: move, select and back.
// Preview is read-only; Restore as New asks before creating a separate career.
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "PauseMenu.h"
#include "MenuItem.h"
#include "MenuOptionsList.h"
#include "FEMessBox.h"
#include "ltshell.h"
#include "mouse.h"
#include "text.h"
#include "data/MissionScripts/text/text.stf"
#include "ce_extras.h"
#include "ce_menu_fit.h"
#include "ce_name_text.h"
#include "ce_scroll_list.h"
#include "../platform/common/portlog.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MEMTYPE_MENUITEM	((EMemoryType)0x80)		// the menu items' memory type (MenuItem.cpp, PauseMenu.cpp)

bool			PortCeMenus();							// ce_frontend.cpp
extern "C" int	CE_SettingGetBool(const char *key, int def);

//*********************************************************************************
void	PortCeExtrasWiden(WCHAR *out, int n, const char *in)
{
	int i = 0;
	for (; in[i] && (i < n - 1); i++)
		out[i] = (WCHAR)(unsigned char)in[i];
	out[i] = 0;
}

void	PortCeExtrasWidenName(WCHAR *out, int n, const char *in)
{
#ifdef _WIN32
	CeDecodeName(out, n, in, false);
#else
	CeDecodeName(out, n, in, true);
#endif
}

// A name in the characters the row face has: each one it lacks is shown as a minus sign. Only what is shown changes; a career is
// always read by its own name.
void	PortCeExtrasInFont(WCHAR *text)
{
	CDXBitmapFont *font = PLATFORM.Font(FONT_NORMAL);
	for (; *text; text++)
		if ((*text != ' ') && !font->CanDisplay(*text))
			*text = '-';
}

void	PortCeShowMessage(const char *text, float top_least = 104.f);

void	PortCeExtrasMessage(const char *text)
{
	PortCeShowMessage(text);
}

namespace
{
// Text colours are authored for the font's doubling colour stage, so each is half of what the screen shows.
const DWORD	kWhite = 0xffffffff;		// the empty-list line
const DWORD	kCream = 0xff7f6f5f;		// the hint: the colour of the original's own description text (253, 221, 189 on the screen)
const DWORD	kGrey = 0xff545454;			// the second hint line and notes: 0xa8 on the screen
const DWORD	kRowGrey = 0xff404040;		// a list box row that is not highlighted, as the Load Game list's
const DWORD	kRowGreySmall = 0xff505050;	// a small-face list box row that is not highlighted: raised for thin-stroke legibility
const DWORD	kRowDim = 0xff323232;		// a row that is not available yet
const DWORD	kBad = 0xff6e1e1e;			// the guide's failed state (220, 60, 60 on the screen), halved once for the font's doubling stage
const DWORD	kGood = 0xff175818;			// the message box's green, for a good state (always with its word)

// The pages: which are list boxes, where Back goes from each, and each one's title.
const int	kParent[XP_COUNT] = {-1, XP_HUB, XP_CAREERS, XP_RECORD, XP_RECORD, XP_HUB, XP_HUB, XP_HUB, XP_LORE, XP_ARTICLE, XP_RECORD, XP_RECORD, XP_HUB, -1, XP_HUB, XP_HUB, XP_RECORD};
WCHAR		kTitleExtras[] = L"Extras";
WCHAR		kTitleRecord[] = L"Service Record";

// The Extras list's rows and what the record page offers under its values.
struct SRow
{
	const WCHAR	*mLabel;
	int			mPage;					// the page the row opens
	const WCHAR	*mHelp;
	const WCHAR	*mExtra;
};

const SRow	kExtrasRows[] = {
	{L"Service Record", XP_CAREERS, L"Each career's missions, ranks and Goodies.", L"Nothing is changed by looking."},
	{L"Goodies Catalogue", XP_CATALOGUE, L"How each Goodie is earned.", L"Nothing is changed by looking."},
	{L"Music Player", XP_MUSIC, L"Listen to the game's music and your own.", NULL},
	{L"Lore", XP_LORE, L"Read about the world, the machines and the makers.", NULL},
	{L"Back Up All Careers", XP_BACKUP_ALL, L"One verified set of every career and your options.", L"Your careers are only read."},
	{L"Backup Preview", XP_BACKUP_PREVIEW, L"Verify a backup and see its saved career.", L"Verify a backup before choosing Restore as New."},
	{L"Import Career", XP_IMPORT_CAREER, L"Copy a career file into your saves as a new career.", L"Backs up your careers first. Existing careers stay untouched."},
};
const SRow	kRecordActions[] = {
	{L"Missions", XP_MISSIONS, L"Every mission, with your rank.", NULL},
	{L"Goodies", XP_GOODIES, L"Every Goodie, found or locked.", NULL},
	{L"Saved Settings", XP_STORED_SETTINGS, L"The sound levels and player settings in this career.", L"Looking changes nothing."},
	{L"Back Up Career", XP_BACKUP, L"Make a verified copy of this career.", L"Your original stays where it is."},
	{L"Edit Career", XP_EDIT_CAREER, L"Change unlocks, ranks, kills or settings and save as a new career.", L"Your original career is never changed."},
};
const int	kExtrasCount = int(sizeof(kExtrasRows) / sizeof(kExtrasRows[0]));
const int	kRecordActionCount = int(sizeof(kRecordActions) / sizeof(kRecordActions[0]));

// Where the two lists of rows are centred (the options list centres its items of 20 on y): the record's items sit
// higher so that its hint lines finish clearly above the help bar.
const float	kExtrasY = 295.f, kRecordY = 270.f;

int				sOffered = -1;
bool			sActive;
int				sPage = XP_HUB;
float			sPageTime;				// when the page showing opened: it takes no Select for 0.4 s
float			sMoveTime;				// the last move sound from the pointer
int				sPointerX = -1, sPointerY = -1;	// where the pointer was at the last draw: it highlights a row only by moving (a resting pointer must not undo the keys)
bool			sDragging = false;		// pointer captured in the list box scrollbar track while left button stays held
SCeScrollList	sLists[XP_COUNT];

const SCeListPage	*ListPage(int page)
{
	switch (page)
	{
	case XP_CAREERS:	return PortCeRecordCareersPage();
	case XP_MISSIONS:	return PortCeRecordMissionsPage();
	case XP_GOODIES:	return PortCeRecordGoodiesPage();
		case XP_STORED_SETTINGS: return PortCeRecordSettingsPage();
		case XP_BACKUP: return PortCeRecordBackupPage();
	case XP_BACKUP_PREVIEW: return PortCeBackupPreviewPage();
	case XP_BACKUP_ALL: return PortCeBackupAllPage();
	case XP_IMPORT_CAREER: return PortCeImportCareerPage();
	case XP_EDIT_CAREER: return PortCeCareerEditPage();
	case XP_MUSIC:		return PortCeJukeboxPage();
	case XP_CATALOGUE:	return PortCeCataloguePage();
	case XP_CATALOGUE_LIST:	return PortCeCatalogueListPage();
	case XP_LORE:		return PortCeLoreListPage();
		case XP_CONTENTS: return PortCeLoreContentsPage();
	}
	return NULL;
}

const SCeReadPage	*ReadPage(int page)
{
	return (page == XP_ARTICLE) ? PortCeLoreReadPage() : NULL;
}

// The width of the hint area: the space between the page's side borders.
const float	kLinesWidth = 420.f;

// What measures a run of characters in the small face, for CeBreakAt.
struct SMeasure
{
	CDXBitmapFont	*mFont;

	float	operator()(const WCHAR *first, int count) const
	{
		static WCHAR piece[400];
		if (count > 399)
			count = 399;
		memcpy(piece, first, sizeof(WCHAR) * count);
		piece[count] = 0;
		SIZE size;
		mFont->GetTextExtent(piece, &size);
		return float(size.cx);
	}
};

// Bounded replacement for retail CDXBitmapFont::WordWrap (SWordWrapFill is WCHAR c[40][100]).
// Retail WordWrap does not bound column index (< 99) or line index (< 40), which overflows on
// long narrow-glyph runs (e.g. crafted backup folder names or modded goodie titles).
int	SafeWordWrap(CDXBitmapFont *font, SWordWrapFill *fill, const WCHAR *text, float width)
{
	if (!font || !fill || !text || !text[0])
		return 0;
	int line = 0;
	int col = 0;
	fill->c[0][0] = 0;
	const WCHAR *p = text;
	while (*p && line < 40)
	{
		if (*p == L'\n')
		{
			fill->c[line][col] = 0;
			line++;
			col = 0;
			if (line < 40)
				fill->c[line][0] = 0;
			p++;
			continue;
		}
		if (*p == L' ')
		{
			if (col > 0 && col < 99)
			{
				fill->c[line][col] = L' ';
				fill->c[line][col + 1] = 0;
				SIZE s;
				font->GetTextExtent(fill->c[line], &s);
				if (float(s.cx) <= width)
					col++;
				else
				{
					fill->c[line][col] = 0;
					line++;
					col = 0;
					if (line < 40)
						fill->c[line][0] = 0;
				}
			}
			p++;
			continue;
		}
		// Collect one word (bounded to 99 chars so it always fits one row buffer).
		WCHAR word[100];
		int wlen = 0;
		while (*p && *p != L' ' && *p != L'\n' && wlen < 99)
			word[wlen++] = *p++;
		word[wlen] = 0;
		if (col > 0)
		{
			// Try appending to current line if it fits both buffer and pixel width.
			if (col + wlen < 100)
			{
				for (int i = 0; i <= wlen; i++)
					fill->c[line][col + i] = word[i];
				SIZE s;
				font->GetTextExtent(fill->c[line], &s);
				if (float(s.cx) <= width)
				{
					col += wlen;
					continue;
				}
			}
			// Trim trailing space and advance to next line.
			while (col > 0 && fill->c[line][col - 1] == L' ')
				col--;
			fill->c[line][col] = 0;
			line++;
			col = 0;
			if (line >= 40)
				break;
			fill->c[line][0] = 0;
		}
		// Copy word onto fresh line, splitting by character if a single word exceeds pixel width.
		for (int i = 0; i < wlen && line < 40; i++)
		{
			if (col < 99)
			{
				fill->c[line][col] = word[i];
				fill->c[line][col + 1] = 0;
				SIZE s;
				font->GetTextExtent(fill->c[line], &s);
				if (col > 0 && float(s.cx) > width)
				{
					fill->c[line][col] = 0;
					line++;
					col = 0;
					if (line >= 40)
						break;
					fill->c[line][0] = word[i];
					fill->c[line][1] = 0;
					col = 1;
				}
				else
					col++;
			}
			else
			{
				fill->c[line][col] = 0;
				line++;
				col = 0;
				if (line >= 40)
					break;
				fill->c[line][0] = word[i];
				fill->c[line][1] = 0;
				col = 1;
			}
		}
	}
	if (line < 40)
	{
		while (col > 0 && fill->c[line][col - 1] == L' ')
			col--;
		fill->c[line][col] = 0;
		if (col > 0 || line == 0)
			line++;
	}
	return line;
}

// The text in lines wrapped to the space between the page's side borders, each centred, from y down; returns where the next line
// would start. As the CE page's own hints are drawn (CCeBack, ce_frontend.cpp), except that a sentence the game's wrap would spread
// over two lines is broken instead at the space that makes the wider line narrowest, by the face's own measure: two even lines, not a
// full line and a word left over. A line wider than the area is logged once (a check for the evidence runs; none is ever drawn so).
float	Lines(const WCHAR *text, float y, DWORD colour)
{
	static SWordWrapFill fill;
	static WCHAR buffer[400];
	if (!text || !text[0])
		return y;
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	wcsncpy(buffer, text, 399);
	buffer[399] = 0;
	int count = SafeWordWrap(font, &fill, buffer, kLinesWidth);
	if (count == 2)
	{
		const SMeasure measure = {font};
		float widest = 0.f;
		const int at = CeBreakAt(buffer, measure, &widest);
		if ((at > 0) && (at <= 99) && (widest <= kLinesWidth))
		{
			wcsncpy(fill.c[0], buffer, at);
			fill.c[0][at] = 0;
			wcsncpy(fill.c[1], buffer + at + 1, 99);
			fill.c[1][99] = 0;
		}
	}
	const float line = float(font->GetHeight());
	for (int l = 0; l < count; l++)
	{
		SIZE size;
		font->GetTextExtent(fill.c[l], &size);
		if (float(size.cx) > kLinesWidth + 0.5f)
		{
			static unsigned seen[8];
			unsigned hash = 2166136261u;
			for (const WCHAR *p = fill.c[l]; *p; p++)
				hash = (hash ^ unsigned(*p)) * 16777619u;
			bool known = false;
			for (unsigned n : seen)
				known = known || (n == hash);
			if (!known)
			{
				static int next;
				seen[next++ & 7] = hash;
				fprintf(stderr, PORT_LOG_PREFIX "extras: a line of text is %d px, wider than the %d of the hint area\n", int(size.cx), int(kLinesWidth));
			}
		}
		font->DrawTextWithShadow(320.f - float(size.cx / 2), y + line * l, colour, fill.c[l], 0, 0, 1.f, 1.f);
	}
	return y + line * count;
}

// Same wrapped, centred hint text, bounded to at most maxLines lines (ellipsizing the final line if more wrap)
// so long dialogue or item hints never spill into the front end's bottom help bar (y = 455).
float	LinesMax(const WCHAR *text, float y, DWORD colour, int maxLines)
{
	static SWordWrapFill fill;
	static WCHAR buffer[400];
	if (!text || !text[0] || maxLines <= 0)
		return y;
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	int n = 0;
	for (; text[n] && n < 399; n++)
		buffer[n] = text[n];
	buffer[n] = 0;
	int count = SafeWordWrap(font, &fill, buffer, kLinesWidth);
	if (count == 2 && maxLines >= 2)
	{
		const SMeasure measure = {font};
		float widest = 0.f;
		const int at = CeBreakAt(buffer, measure, &widest);
		if ((at > 0) && (widest <= kLinesWidth))
		{
			int i = 0;
			for (; i < at && i < 99; i++) fill.c[0][i] = buffer[i];
			fill.c[0][i] = 0;
			int j = 0;
			for (; buffer[at + 1 + j] && j < 99; j++) fill.c[1][j] = buffer[at + 1 + j];
			fill.c[1][j] = 0;
		}
	}
	if (count > maxLines)
	{
		count = maxLines;
		WCHAR *last = fill.c[count - 1];
		int len = 0;
		while (len < 99 && last[len]) len++;
		while (len > 0)
		{
			while (len > 0 && (last[len - 1] == WCHAR(' ') || last[len - 1] == WCHAR('.') || last[len - 1] == WCHAR(',')))
				len--;
			int cut = len < 96 ? len : 96;
			last[cut] = WCHAR('.');
			last[cut + 1] = WCHAR('.');
			last[cut + 2] = WCHAR('.');
			last[cut + 3] = 0;
			SIZE sz;
			font->GetTextExtent(last, &sz);
			if (float(sz.cx) <= kLinesWidth)
				break;
			if (len > 0)
				len--;
		}
	}
	const float line = float(font->GetHeight());
	for (int l = 0; l < count; l++)
	{
		SIZE size;
		font->GetTextExtent(fill.c[l], &size);
		font->DrawTextWithShadow(320.f - float(size.cx / 2), y + line * l, colour, fill.c[l], 0, 0, 1.f, 1.f);
	}
	return y + line * count;
}

}

float	PortCeExtrasHint(const WCHAR *help, const WCHAR *extra, float y)
{
	return Lines(extra, Lines(help, y, kCream), kGrey);
}

namespace
{
//*********************************************************************************
// A row that opens a page.
class CExtrasItem : public CMenuItem
{
public:
	enum { ID = 0x4358 };				// its mId, which no item of the game's or of the CE page's uses

	CExtrasItem(const SRow *row) : CMenuItem((EPCText)0, ID), mRow(row) {}

	virtual WCHAR	*GetText() { return (WCHAR *)mRow->mLabel; }

	const SRow	*mRow;
};

// A label and its value, drawn by the game's own choice item as the options pages draw theirs. It has nothing to choose: it never
// opens a list of choices and sets nothing.
class CExtrasValue : public CMenuMenuItem
{
public:
	CExtrasValue(int row) : CMenuMenuItem((EPCText)0, 3, false), mRow(row) {}

	virtual WCHAR	*GetText() { return (WCHAR *)PortCeRecordLabel(mRow); }
	virtual int		GetNumChoices() { return 1; }
	virtual WCHAR	*GetChoiceText(int) { return (WCHAR *)PortCeRecordValue(mRow); }
	virtual int		GetValue() { return 0; }
	virtual void	SetValue(int) {}
	virtual void	ReceiveButtonAction(CController *, int, float) {}

	int		mRow;
};

// A page's Back item, the list's last: after drawing itself it draws the highlighted row's hint below the list, a line in white and
// one in grey, as the CE page's own Back item does.
class CExtrasBack : public CMenuItem
{
public:
	CExtrasBack(CMenuOptionsList *list, int page) : CMenuItem((DWORD)GI_BACK, 3), mList(list), mPage(page) {}

	virtual void	Render(float x, float y, BOOL selected)
	{
		CMenuItem::Render(x, y, selected);
		if (FEMESSBOX.BeingDisplayed())
			return;
		const WCHAR *help = NULL, *extra = NULL;
		const int row = mList->mSelected;
		if (mPage == XP_HUB)
		{
			if ((row >= 0) && (row < kExtrasCount))
			{
				help = kExtrasRows[row].mHelp;
				extra = kExtrasRows[row].mExtra;
			}
		}
		else if ((row >= 0) && (row < CE_RECORD_ROWS))
		{
			help = PortCeRecordHelp(row);
			extra = PortCeRecordExtra(row);
		}
		else if ((row >= CE_RECORD_ROWS) && (row < CE_RECORD_ROWS + kRecordActionCount))
		{
			help = kRecordActions[row - CE_RECORD_ROWS].mHelp;
			extra = kRecordActions[row - CE_RECORD_ROWS].mExtra;
		}
		const float line = float(PLATFORM.Font(FONT_SMALL)->GetHeight());
		Lines(extra, Lines(help, y + 20.f + line * 0.5f, kCream), kGrey);
	}

	CMenuOptionsList	*mList;
	int					mPage;
};

//*********************************************************************************
// The menu: the two pages of rows as the game's lists, and the input of every Extras page.
class CCeExtrasMenu : public CPauseMenu
{
public:
	enum { LIST_HUB = 0, LIST_RECORD = 1 };

	CCeExtrasMenu() : CPauseMenu(PORT_CE_NO_LISTS)
	{
		CMenuOptionsList *list = new (MEMTYPE_MENUITEM) CMenuOptionsList(kTitleExtras, 320.f, kExtrasY, FALSE, 0xb0000000);
		for (int i = 0; i < kExtrasCount; i++)
			list->AddItem(new (MEMTYPE_MENUITEM) CExtrasItem(&kExtrasRows[i]));
		list->AddItem(new (MEMTYPE_MENUITEM) CExtrasBack(list, XP_HUB));
		mLists.Append(list);
		list = new (MEMTYPE_MENUITEM) CMenuOptionsList(kTitleRecord, 320.f, kRecordY, FALSE, 0xb0000000);
		for (int i = 0; i < CE_RECORD_ROWS; i++)
			list->AddItem(new (MEMTYPE_MENUITEM) CExtrasValue(i));
		for (int i = 0; i < kRecordActionCount; i++)
			list->AddItem(new (MEMTYPE_MENUITEM) CExtrasItem(&kRecordActions[i]));
		list->AddItem(new (MEMTYPE_MENUITEM) CExtrasBack(list, XP_RECORD));
		mLists.Append(list);
	}

	CMenuOptionsList	*Rows(int page)
	{
		return mLists.At((page == XP_RECORD) ? LIST_RECORD : LIST_HUB);
	}

	// A page's own shape (rows in view, top) on its list.
	static void	Shape(const SCeListPage *page, SCeScrollList *list)
	{
		list->mRows = page->mRows;
		list->mY = page->mTopY;
	}

	// The cursor never rests on a group's heading: it goes on the way it was moving, or back when there is nothing that way.
	static void	SkipHeadings(const SCeListPage *page, SCeScrollList *list, int direction)
	{
		if (!page->mHeading || list->mCount <= 0)
			return;
		const int start = list->mCursor;
		for (int pass = 0; pass < 2 && page->mHeading(list->mCursor); pass++, direction = -direction)
		{
			int at = start;
			while (at >= 0 && at < list->mCount && page->mHeading(at))
				at += direction;
			if (at >= 0 && at < list->mCount)
				list->mCursor = at;
		}
		CeListClamp(list);
	}

	// Opens a page with its first row highlighted.
	void	Show(int page, int startRow = -1)
	{
		sPage = page;
		sPageTime = PLATFORM.GetSysTimeFloat();
		sPointerX = MOUSE_X;
		sPointerY = MOUSE_Y;
		sDragging = false;
		if (const SCeListPage *list = ListPage(page))
		{
			if (list->mOpen)
				list->mOpen();
			Shape(list, &sLists[page]);
			CeListReset(&sLists[page], list->mCount());
			sLists[page].mCursor = startRow >= 0 ? startRow : list->mStart;
			CeListClamp(&sLists[page]);
			SkipHeadings(list, &sLists[page], 1);
		}
		else if (const SCeReadPage *read = ReadPage(page))
			read->mOpen();
		else
		{
			mPage = (page == XP_RECORD) ? LIST_RECORD : LIST_HUB;
			Rows(page)->Reset();
			if (page == XP_RECORD)
				Rows(page)->mSelected = CE_RECORD_ROWS;		// the first row that does something: Missions
		}
	}

	// Back goes to the page above, which is as it was left; from the Extras list, back to the Onslaught CE page's own list.
	void	Back(bool sound = true)
	{
		if (sound)
			CFrontEnd::PlaySound(FES_BACK);
		sDragging = false;
		const SCeListPage *list = ListPage(sPage);
		if (list && list->mClose)
			list->mClose();
		if (const SCeReadPage *read = ReadPage(sPage))
			read->mClose();
		if (kParent[sPage] < 0)
		{
			sActive = false;
			return;
		}
		sPage = ((sPage == XP_GOODIES) && PortCeGoodiesFromCatalogue()) ? XP_CATALOGUE : kParent[sPage];
		sPageTime = PLATFORM.GetSysTimeFloat();
		sPointerX = MOUSE_X;
		sPointerY = MOUSE_Y;
		if (const SCeListPage *parent = ListPage(sPage))
		{
			Shape(parent, &sLists[sPage]);
			sLists[sPage].mCount = parent->mCount();
			CeListClamp(&sLists[sPage]);
			SkipHeadings(parent, &sLists[sPage], 1);
		}
		else if (!ReadPage(sPage))
			mPage = (sPage == XP_RECORD) ? LIST_RECORD : LIST_HUB;
	}

	bool	SelectAllowed()
	{
		return PLATFORM.GetSysTimeFloat() - sPageTime >= 0.4f;
	}

	// Select on a list box's highlighted row.
	void	SelectRow(const SCeListPage *page, SCeScrollList *list)
	{
		if (!SelectAllowed() || (list->mCursor < 0) || (list->mCursor >= list->mCount) || (page->mHeading && page->mHeading(list->mCursor)))
			return;
		if (page->mSelect(list->mCursor))
			CFrontEnd::PlaySound(FES_SELECT);
	}

	virtual void	ReceiveButtonAction(CController *from_controller, int button, float val)
	{
		if (!sActive)
			return;
		if (const SCeListPage *page = ListPage(sPage))
		{
			SCeScrollList *list = &sLists[sPage];
			Shape(page, list);
			list->mCount = page->mCount();
			bool moved = false;
			switch (button)
			{
			case BUTTON_FRONTEND_MENU_UP:		moved = CeListMove(list, -1);	SkipHeadings(page, list, -1);	break;
			case BUTTON_FRONTEND_MENU_DOWN:		moved = CeListMove(list, 1);	SkipHeadings(page, list, 1);	break;
			case BUTTON_FRONTEND_MENU_LEFT:
				if (page->mAdjust)
				{
					const int adj = page->mAdjust(list->mCursor, -1);
					if (adj != 0)
					{
						moved = adj > 0;
						break;
					}
				}
				moved = CeListPage(list, -1);	SkipHeadings(page, list, -1);	break;
			case BUTTON_FRONTEND_MENU_RIGHT:
				if (page->mAdjust)
				{
					const int adj = page->mAdjust(list->mCursor, 1);
					if (adj != 0)
					{
						moved = adj > 0;
						break;
					}
				}
				moved = CeListPage(list, 1);	SkipHeadings(page, list, 1);	break;
			case BUTTON_PAUSE_MENU_SELECT:
			case BUTTON_FRONTEND_MENU_SELECT:	SelectRow(page, list);			break;
			case BUTTON_FRONTEND_MENU_BACK:
				if (sPage == XP_BACKUP_PREVIEW || sPage == XP_IMPORT_CAREER || sPage == XP_EDIT_CAREER)
				{
					const int step = (sPage == XP_BACKUP_PREVIEW) ? PortCeBackupPreviewStepBack()
								   : (sPage == XP_IMPORT_CAREER)  ? PortCeImportCareerStepBack()
																  : PortCeCareerEditStepBack();
					if (step > 0)
						CFrontEnd::PlaySound(FES_BACK);
					else if (step == 0)
						Back();
				}
				else
					Back();
				break;
			}
			if (moved)
				CFrontEnd::PlaySound(FES_MOVE);
			return;
		}
		if (const SCeReadPage *read = ReadPage(sPage))
		{
			const bool isSelect = (button == BUTTON_PAUSE_MENU_SELECT || button == BUTTON_FRONTEND_MENU_SELECT);
			if (button == BUTTON_FRONTEND_MENU_BACK)
				Back();
			else if ((!isSelect || SelectAllowed()) && read->mButton(button))
				CFrontEnd::PlaySound(isSelect ? FES_SELECT : FES_MOVE);
			return;
		}
		CMenuOptionsList *list = Rows(sPage);
		switch (button)
		{
		default:
			list->ReceiveButtonAction(from_controller, button, val);
			break;
		case BUTTON_PAUSE_MENU_SELECT:
		case BUTTON_FRONTEND_MENU_SELECT:
		{
			if (!SelectAllowed())
				break;
			CMenuItem *item = list->mItems.At(list->mSelected);
			if (item->mText == GI_BACK)
				Back();
			else if (item->mId == CExtrasItem::ID)
			{
				CFrontEnd::PlaySound(FES_SELECT);
				Show(((CExtrasItem *)item)->mRow->mPage);
			}
			break;
		}
		case BUTTON_FRONTEND_MENU_BACK:
			Back();
			break;
		}
	}

	// A list box page, drawn as the Load Game page draws its list (CFEPDirectory::RenderSaveFileList), with a second column at the
	// right of each row and the highlighted row's hint under the box.
	void	DrawListBox(const SCeListPage *page, SCeScrollList *list)
	{
		Shape(page, list);
		const float bottom = CeListBottom(list), boxTop = CeListTopOf(list);
		const float edge = (page->mBoxRight > 0.f) ? page->mBoxRight : kCeListRight + page->mWiden;
		const float track = edge - (kCeListRight - kCeListTrack);	// the box's right part: its track inside the edge
		const DWORD box = 0x80ffffff;
		CDXBitmapFont *font = PLATFORM.Font(page->mSmallFace ? FONT_SMALL : FONT_NORMAL);
		// the smaller face sits in the middle of a row made for the larger
		const float lift = page->mSmallFace ? (float(PLATFORM.Font(FONT_NORMAL)->GetHeight()) - float(font->GetHeight())) * 0.5f : 0.f;
		FRONTEND.DrawPanel(kCeListLeft - 4.f, boxTop, edge, bottom, 0.55f, 0xa0000000);
		FRONTEND.DrawBox(kCeListLeft - 4.f, boxTop, edge, bottom, box, 2.f, 0.5f);

		list->mCount = page->mCount();
		CeListClamp(list);
		const bool wasDragging = sDragging;
		if (list->mCount > 0)
		{
			// the wheel moves the view; a press in the track captures dragging until release
			int scrollDir = 0;
			if (!FEMESSBOX.IsInteractive())
			{
				if (MOUSE_WHEEL)
				{
					const int notches = (MOUSE_WHEEL > 0) ? (MOUSE_WHEEL + 119) / 120 : -((-MOUSE_WHEEL + 119) / 120);
					MOUSE_WHEEL = 0;
					const int delta = -notches * 3;
					if (CeListScroll(list, delta))
						scrollDir = (delta < 0) ? -1 : 1;
				}
				const bool trackPress = MOUSE_BUTTONS[0].mDown && !LT.GetKeytrap() &&
						(float(MOUSE_X) >= track) && (float(MOUSE_X) < edge) &&
						(float(MOUSE_Y) >= boxTop) && (float(MOUSE_Y) < bottom);
				if (trackPress)
					sDragging = true;
				else if (!MOUSE_HELD[0] || LT.GetKeytrap())
					sDragging = false;
				if (sDragging)
				{
					const int beforeTop = list->mTop;
					CeListDrag(list, float(MOUSE_Y));
					if (list->mTop != beforeTop)
						scrollDir = (list->mTop < beforeTop) ? -1 : 1;
					if (!MOUSE_HELD[0])
						sDragging = false;
				}
			}
			else
				sDragging = false;
			SkipHeadings(page, list, (scrollDir < 0) ? -1 : 1);		// the wheel and the bar move the view, and the cursor with it
		}
		else
			sDragging = false;
		if (wasDragging && !sDragging)
			MOUSE_BUTTONS[0].mReleased = FALSE;
		const bool dragBusy = sDragging || wasDragging;

		const float now = PLATFORM.GetSysTimeFloat();
		const bool pointerMoved = (MOUSE_X != sPointerX) || (MOUSE_Y != sPointerY);
		sPointerX = MOUSE_X;
		sPointerY = MOUSE_Y;
		int clicked = -1;
		for (int i = list->mTop; (i < list->mCount) && (i < list->mTop + CeListRowsOf(list)); i++)
		{
			const float y = CeListRowY(list, i);
			const bool heading = page->mHeading && page->mHeading(i);
			// the pointer highlights the row it moves onto and chooses the row it clicks (a group's heading is neither)
			if (!dragBusy && !heading && pointerMoved && FRONTEND.MouseOverRect(kCeListLeft, y, track, y + kCeListPitch) && (list->mCursor != i))
			{
				list->mCursor = i;
				if (now - sMoveTime > 0.1f)
				{
					CFrontEnd::PlaySound(FES_MOVE);
					sMoveTime = now;
				}
			}
			if (!dragBusy && !heading && FRONTEND.MouseClickedInRect(kCeListLeft, y, track, y + kCeListPitch))
			{
				list->mCursor = i;
				clicked = i;
			}

			static SCeListRow row;
			memset(&row, 0, sizeof(row));
			page->mRow(i, &row);
			const bool focus = !heading && (i == list->mCursor);
			DWORD colour = heading ? kCream : row.mDim ? kRowDim : (page->mSmallFace ? kRowGreySmall : kRowGrey);
			if (focus)
			{
				// Narrow white pulse (0x77..0x7f before doubling -> luma 238..254) so focused rows stay strictly brighter than kCream (0x7f6f5f -> luma 228) at every phase.
				const int pulse = int(cosf((FRONTEND.GetCounter() * 3.14f) * (1.f / 30.f)) * -4.f);
				colour = 0xff7b7b7b - pulse * 0x010101;
			}
			// the right-hand word, ending a little before the scroll track
			float right = track - 8.f;
			if (row.mRight[0])
			{
				SIZE size;
				font->GetTextExtent(row.mRight, &size);
				right -= float(size.cx);
				DWORD tone = colour;
				if (row.mTone == CE_TONE_GOOD)
					tone = kGood;
				else if (row.mTone == CE_TONE_BAD)
					tone = kBad;
				else if (!focus && (row.mTone == CE_TONE_DIM))
					tone = kRowDim;
				font->DrawTextDynamic(right, y + lift, FO(100), 1.f, 1.f, tone, row.mRight, 1000.f, 0, 0);
				right -= 10.f;
			}
			// the row's own text: at the page's one scale, cut and marked when it is long (never each row to its own width)
			SIZE size;
			font->GetTextExtent(row.mLeft, &size);
			const float scale = (page->mScale > 0.f) ? page->mScale : 1.f;
			int length = int(wcslen(row.mLeft));
			if ((float(size.cx) * scale > right - kCeListLeft) && !wcsstr(row.mLeft, L"..."))
			{
				for (int c = length - 1; c > 1; c--)
				{
					if (row.mLeft[c] != WCHAR(' '))
						continue;
					int cut = c;
					while ((cut > 1) && (wcschr(L" .,;:", row.mLeft[cut - 1]) != NULL))
						cut--;
					WCHAR trial[sizeof(row.mLeft) / sizeof(*row.mLeft)];
					if ((cut <= 0) || (cut + 3 >= int(sizeof(row.mLeft) / sizeof(*row.mLeft))))
						continue;
					memcpy(trial, row.mLeft, size_t(cut) * sizeof(WCHAR));
					trial[cut] = '.';
					trial[cut + 1] = '.';
					trial[cut + 2] = '.';
					trial[cut + 3] = 0;
					SIZE ts;
					font->GetTextExtent(trial, &ts);
					if (float(ts.cx) * scale <= right - kCeListLeft)
					{
						wcscpy(row.mLeft, trial);
						size = ts;
						length = cut + 3;
						break;
					}
				}
			}
			while ((float(size.cx) * scale > right - kCeListLeft) && (length > 4))
			{
				length--;
				int cut = length - 3;
				while ((cut > 1) && ((row.mLeft[cut - 1] == WCHAR(' ')) || (row.mLeft[cut - 1] == WCHAR('.'))))
					cut--;
				row.mLeft[cut] = '.';
				row.mLeft[cut + 1] = '.';
				row.mLeft[cut + 2] = '.';
				row.mLeft[cut + 3] = 0;
				length = cut + 3;
				font->GetTextExtent(row.mLeft, &size);
			}
			font->DrawTextDynamic(kCeListLeft, y + lift, FO(100), scale, 1.f, colour, row.mLeft, 1000.f, 0, 0);
		}

		if (list->mCount <= 0)
		{
			// the Load Game page's own place for "no saves": the middle of the box
			static WCHAR empty[160];
			wcsncpy(empty, page->mEmpty(), 159);
			empty[159] = 0;
			Lines(empty, boxTop + kCeListPitch * 4.f, kWhite);
		}
		else
		{
			FRONTEND.DrawBox(track, boxTop, edge, bottom, box, 2.f, 0.5f);
			if (CeListHasBar(list))
			{
				float top, foot;
				CeListBar(list, &top, &foot);
				FRONTEND.DrawBox(track + 5.f, top, edge - 5.f, foot, 0xffffffff, 5.f, 0.5f);
			}

			static WCHAR help[200], extra[200];
			help[0] = extra[0] = 0;
			page->mHint(list->mCursor, help, extra, 200);
			const float hintY = (bottom >= 395.f) ? (bottom + 8.f) : (bottom + 12.f);
			const float lineH = float(PLATFORM.Font(FONT_SMALL)->GetHeight());
			const float limitY = 452.f;
			const int totalSlots = (lineH > 0.f && limitY > hintY) ? int((limitY - hintY) / lineH) : 1;
			int extraNeed = 0;
			if (extra[0])
			{
				static SWordWrapFill probe;
				static WCHAR probeBuf[200];
				int pLen = 0;
				for (; extra[pLen] && pLen < 199; pLen++) probeBuf[pLen] = extra[pLen];
				probeBuf[pLen] = 0;
				extraNeed = SafeWordWrap(PLATFORM.Font(FONT_SMALL), &probe, probeBuf, kLinesWidth);
				if (extraNeed < 1) extraNeed = 1;
			}
			const int extraReserve = (help[0] && extraNeed > 1 && totalSlots <= 2) ? 1 : extraNeed;
			const int helpMax = help[0] ? ((totalSlots - extraReserve >= 1) ? (totalSlots - extraReserve) : 1) : 0;
			float next = helpMax > 0 ? LinesMax(help, hintY, kCream, helpMax) : hintY;
			const int extraMax = (lineH > 0.f && limitY > next) ? int((limitY - next) / lineH) : 0;
			if (extra[0] && extraMax > 0)
				next = LinesMax(extra, next, kGrey, extraMax);
			if (page->mPane)
				page->mPane(list->mCursor, edge + 10.f, boxTop, 550.f, bottom);
			if (page->mDraw)
				page->mDraw(next + 4.f);
		}
		if (clicked >= 0)
			SelectRow(page, list);
	}

	void	Draw()
	{
		if (!FEMESSBOX.IsInteractive() && !LT.GetKeytrap() && MOUSE_BUTTONS[2].mDown)
		{
			MOUSE_BUTTONS[2].mDown = FALSE;
			ReceiveButtonAction(NULL, BUTTON_FRONTEND_MENU_BACK, 1.f);
			if (!sActive)
				return;
		}
		if (const SCeListPage *page = ListPage(sPage))
			DrawListBox(page, &sLists[sPage]);
		else if (const SCeReadPage *read = ReadPage(sPage))
			read->mDraw();
		else
			Rows(sPage)->Render(this);
	}
};

CCeExtrasMenu	*sMenu;

// Every page's own state is let go: the Extras pages are closing without their Back.
void	CloseAll()
{
	sDragging = false;
	for (int page = 0; page < XP_COUNT; page++)
	{
		const SCeListPage *list = ListPage(page);
		if (list && list->mClose)
			list->mClose();
		if (const SCeReadPage *read = ReadPage(page))
			read->mClose();
	}
	sActive = false;
	sPage = XP_HUB;
}
}

//*********************************************************************************
bool	PortCeExtrasOffered()
{
	if (sOffered < 0)
		sOffered = (PortCeMenus() && (CE_SettingGetBool("extras", 1) == 1)) ? 1 : 0;
	return sOffered == 1;
}

bool	PortCeExtrasActive()
{
	return sActive && sMenu;
}

void	PortCeExtrasOpen()
{
	if (!PortCeExtrasOffered())
		return;
	if (!sMenu)
		sMenu = new (MEMTYPE_MENUITEM) CCeExtrasMenu();
	CPauseMenu::mOptionsPage = true;
	sMenu->Activate(0);
	sActive = true;
	sMenu->Show(XP_HUB);
}

void	PortCeExtrasOpenPage(int page, int startRow)
{
	if (!PortCeMenus() || (page <= XP_HUB) || (page >= XP_COUNT))
		return;
	if (!sMenu)
		sMenu = new (MEMTYPE_MENUITEM) CCeExtrasMenu();
	CPauseMenu::mOptionsPage = true;
	sMenu->Activate(0);
	sActive = true;
	sMenu->Show(page, startRow);
}

void	PortCeExtrasHide()
{
	sDragging = false;
	sActive = false;
}

void	PortCeExtrasShow(int page, int startRow)
{
	if (PortCeExtrasActive() && (page > XP_HUB) && (page < XP_COUNT))
		sMenu->Show(page, startRow);
}

// The list already plays the select sound when a heading is accepted.
void PortCeRecordBackupBack()
{
	if (PortCeExtrasActive() && sPage == XP_BACKUP)
		sMenu->Back(false);
}

void PortCeBackupAllBack()
{
	if (PortCeExtrasActive() && sPage == XP_BACKUP_ALL)
		sMenu->Back(false);
}

void PortCeImportCareerBack()
{
	if (PortCeExtrasActive() && sPage == XP_IMPORT_CAREER)
		sMenu->Back(false);
}

void PortCeCareerEditBack()
{
	if (PortCeExtrasActive() && sPage == XP_EDIT_CAREER)
		sMenu->Back(false);
}

void PortCeBackupPreviewBack()
{
	if (PortCeExtrasActive() && sPage == XP_BACKUP_PREVIEW)
		sMenu->Back(false);
}

void	PortCeExtrasReturnToReader()
{
	if (PortCeExtrasActive() && sPage == XP_CONTENTS)
		sMenu->Back(false);
}

void	PortCeExtrasButton(int button, float val)
{
	if (PortCeExtrasActive())
		sMenu->ReceiveButtonAction(NULL, button, val);
}

void	PortCeExtrasProcess(bool pageActive)
{
	if (!PortCeExtrasActive())
		return;
	if (!pageActive)
	{
		CloseAll();
		return;
	}
	const SCeListPage *page = ListPage(sPage);
	if (page && page->mProcess)
		page->mProcess();
}

void	PortCeExtrasDraw()
{
	if (PortCeExtrasActive())
		sMenu->Draw();
}

WCHAR	*PortCeExtrasTitle()
{
	const SCeListPage *page = ListPage(sPage);
	if (page)
		return (WCHAR *)page->mTitle;
	if (const SCeReadPage *read = ReadPage(sPage))
		return (WCHAR *)read->mTitle();
	return (sPage == XP_RECORD) ? kTitleRecord : kTitleExtras;
}

void	PortCeExtrasShutdown()
{
	CloseAll();
	SAFE_DELETE(sMenu);
}

// ---- Plain reads of what the host shows (ce_extras.h), for the test kit's adapter (ce_extras_testkit.cpp, compiled only in
// --test-kit builds). They change nothing.
int		PortCeExtrasPageShown() { return sPage; }
const SCeListPage	*PortCeExtrasListShown() { return ListPage(sPage); }
int		PortCeExtrasListCursor() { return ListPage(sPage) ? sLists[sPage].mCursor : -1; }
CMenuOptionsList	*PortCeExtrasMenuRows() { return sMenu && (sPage == XP_HUB || sPage == XP_RECORD) ? sMenu->Rows(sPage) : NULL; }

#endif
