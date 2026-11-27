// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_lore.cpp: the Lore pages (Onslaught CE > Extras > Lore): the articles of the lore library as a list, and one article read
// in a panel in the small face, a whole line at a time. The text is the generated pack (ce_lore_pack.h, made by
// port/tools/gen_lore_pack.py from the Toolkit's lore library); the panel, the track and the bar are the list box's (ce_reading.h).
// Nothing is written: not a file, not a setting.
#ifdef CE_VERSION

#include "common.h"
#include "frontend.h"
#include "FEMessBox.h"
#include "ltshell.h"
#include "mouse.h"
#include "ce_extras.h"
#include "ce_reading.h"
#include "ce_lore_pack.h"
#include "../platform/common/portlog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
// Text colours are authored for the font's doubling colour stage, so each is half of what the screen shows.
const DWORD	kText = 0xffd0d0d0;			// a paragraph or a bullet: a little under the list rows' white, so that a page of it rests the eye
const DWORD	kHead = 0xff7f6f5f;			// a heading: the colour of the original's own description text
const DWORD	kQuote = 0xff766c5c;			// a quotation: warm parchment tone set in with a vertical accent bar

enum { kMaxLines = 900, kLineChars = 100, kBulletIndent = 14 };

struct SLine
{
	WCHAR			mText[kLineChars];
	unsigned char	mKind;				// H, P, B or Q
	bool			mMark;				// the first line of a bullet, which carries its dash
};

SLine		*sLines;
int			sLineCount;
SCeReadSection	sSections[kMaxLines]; // each heading occupies at least one rendered line
int			sSectionCount;
int			sChosen = -1;
SCeReading	sRead;
int			sArticle;
WCHAR		sTitle[64];
bool		sDragging = false;

void	Widen(WCHAR *out, int n, const char *in)
{
	PortCeExtrasWiden(out, n, in);
}

// One block of the pack (a kind and its text) wrapped to the panel and appended as lines.
void	AddBlock(char kind, const char *text, int length)
{
	static SWordWrapFill fill;
	static WCHAR buffer[1600];
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	if (!font)
		return;
	if (length > 1590)
		length = 1590;
	char narrow[1600];
	memcpy(narrow, text, size_t(length));
	narrow[length] = 0;
	Widen(buffer, 1600, narrow);
	for (WCHAR *c = buffer; *c; c++)
		if ((*c != ' ') && !font->CanDisplay(*c))
			*c = '-';
	const bool indented = (kind == 'B') || (kind == 'Q');
	// a heading has a blank line above it, and every block one below
	if ((kind == 'H') && (sLineCount > 0) && (sLineCount < kMaxLines))
	{
		memset(&sLines[sLineCount], 0, sizeof(SLine));
		sLines[sLineCount++].mKind = 'P';
	}
	const int count = PortCeExtrasSafeWordWrap(font, &fill, buffer, kCeReadWidth - (indented ? float(kBulletIndent) : 0.f));
	for (int l = 0; (l < count) && (sLineCount < kMaxLines); l++)
	{
		if (kind == 'H' && l == 0 && sSectionCount < kMaxLines)
		{
			SCeReadSection &section = sSections[sSectionCount++];
			section.mLine = sLineCount;
			section.mText = text;
			section.mLength = length;
		}
		SLine *line = &sLines[sLineCount++];
		memset(line, 0, sizeof(SLine));
		wcsncpy(line->mText, fill.c[l], kLineChars - 1);
		line->mKind = (unsigned char)kind;
		line->mMark = (kind == 'B') && (l == 0);
	}
	if (sLineCount < kMaxLines)
	{
		memset(&sLines[sLineCount], 0, sizeof(SLine));
		sLines[sLineCount++].mKind = 'P';
	}
}

void	Open()
{
	free(sLines);
	sLines = (SLine *)calloc(kMaxLines, sizeof(SLine));
	sLineCount = 0;
	sSectionCount = 0;
	sChosen = -1;
	sDragging = false;
	if (sLines && (sArticle >= 0) && (sArticle < kCeLoreCount))
	{
		const SCeLoreArticle &article = kCeLore[sArticle];
		Widen(sTitle, int(sizeof(sTitle) / sizeof(WCHAR)), article.mTitle);
		const char *at = article.mText;
		while (*at)
		{
			int length = 0;
			while (at[length] && (at[length] != '\n'))
				length++;
			if (length > 1)
				AddBlock(at[0], at + 1, length - 1);
			at += length;
			if (*at == '\n')
				at++;
		}
		while ((sLineCount > 0) && !sLines[sLineCount - 1].mText[0])		// no blank lines at the foot
			sLineCount--;
	}
	CeReadOpen(&sRead, sLineCount);
	fprintf(stderr, PORT_LOG_PREFIX "lore: opened article %d, %d lines\n", sArticle, sLineCount);
}

void	Close()
{
	free(sLines);
	sLines = NULL;
	sLineCount = 0;
	sSectionCount = 0;
	sChosen = -1;
	sDragging = false;
	CeReadOpen(&sRead, 0);
}

const WCHAR	*Title()
{
	return sTitle;
}

bool	Button(int button)
{
	switch (button)
	{
		case BUTTON_PAUSE_MENU_SELECT:
		case BUTTON_FRONTEND_MENU_SELECT:
		{
			const int row = CeReadSectionAt(&sRead, sSections, sSectionCount, sChosen);
			if (row < 0)
				return false;
			PortCeExtrasShow(XP_CONTENTS, row);
			return true;
		}
	case BUTTON_FRONTEND_MENU_UP:		return CeReadMove(&sRead, -1);
	case BUTTON_FRONTEND_MENU_DOWN:		return CeReadMove(&sRead, 1);
	case BUTTON_FRONTEND_MENU_LEFT:		return CeReadPage(&sRead, -1);
	case BUTTON_FRONTEND_MENU_RIGHT:	return CeReadPage(&sRead, 1);
	}
	return false;
}

void	Draw()
{
	if (!sLines || !sLineCount)
	{
		PortCeExtrasHint(L"This article could not be opened.", L"Go back to Lore.", kCeReadTop);
		return;
	}
	CDXBitmapFont *font = PLATFORM.Font(FONT_SMALL);
	const float pitch = float(font->GetHeight());
	const float bottom = CeReadBottom(pitch);
	const DWORD box = 0x80ffffff;
	FRONTEND.DrawPanel(kCeReadLeft - 12.f, kCeReadTop, kCeReadRight, bottom, 0.55f, 0xa0000000);
	FRONTEND.DrawBox(kCeReadLeft - 12.f, kCeReadTop, kCeReadRight, bottom, box, 2.f, 0.5f);
	// the wheel moves the view by three lines; a press in the track captures dragging until release
	const bool wasDragging = sDragging;
	if (!FEMESSBOX.IsInteractive())
	{
		if (MOUSE_WHEEL)
		{
			const int notches = (MOUSE_WHEEL > 0) ? (MOUSE_WHEEL + 119) / 120 : -((-MOUSE_WHEEL + 119) / 120);
			MOUSE_WHEEL = 0;
			CeReadMove(&sRead, -notches * 3);
		}
		const bool trackPress = MOUSE_BUTTONS[0].mDown && !LT.GetKeytrap() &&
				(float(MOUSE_X) >= kCeReadTrack) && (float(MOUSE_X) < kCeReadRight) &&
				(float(MOUSE_Y) >= kCeReadTop) && (float(MOUSE_Y) < bottom);
		if (trackPress)
			sDragging = true;
		else if (!MOUSE_HELD[0] || LT.GetKeytrap())
			sDragging = false;
		if (sDragging)
		{
			CeReadDrag(&sRead, pitch, float(MOUSE_Y));
			if (!MOUSE_HELD[0])
				sDragging = false;
		}
	}
	else
		sDragging = false;
	if (wasDragging && !sDragging)
		MOUSE_BUTTONS[0].mReleased = FALSE;
	const bool dragBusy = sDragging || wasDragging;
	for (int i = 0; i < kCeReadRows; i++)
	{
		const int index = sRead.mTop + i;
		if (index >= sLineCount)
			break;
		const SLine *line = &sLines[index];
		if (!line->mText[0])
			continue;
		const float y = kCeReadTop + 6.f + pitch * float(i);
		const bool indented = (line->mKind == 'B') || (line->mKind == 'Q');
		const DWORD colour = (line->mKind == 'H') ? kHead : (line->mKind == 'Q') ? kQuote : kText;
		if (line->mKind == 'Q')
		{
			const bool prevQuote = (i > 0) && (sLines[index - 1].mKind == 'Q') && (sLines[index - 1].mText[0] != 0);
			const bool nextQuote = (i + 1 < kCeReadRows) && (index + 1 < sLineCount) &&
					(sLines[index + 1].mKind == 'Q') && (sLines[index + 1].mText[0] != 0);
			FRONTEND.DrawPanel(kCeReadLeft + 3.f, y, kCeReadTrack - 6.f, y + pitch, 0.55f, 0x22c8a878);
			FRONTEND.DrawPanel(kCeReadLeft + 3.f, prevQuote ? y : (y + 1.f), kCeReadLeft + 5.5f,
					nextQuote ? (y + pitch) : (y + pitch - 1.f), 0.55f, 0xc8d8b480);
		}
		if (line->mMark)
		{
			static WCHAR dash[] = L"-";
			font->DrawTextWithShadow(kCeReadLeft, y, colour, dash, 0, 0, 1.f, 1.f);
		}
		font->DrawTextWithShadow(kCeReadLeft + (indented ? float(kBulletIndent) : 0.f), y, colour, (WCHAR *)line->mText, 0, 0, 1.f, 1.f);
	}
	FRONTEND.DrawBox(kCeReadTrack, kCeReadTop, kCeReadRight, bottom, box, 2.f, 0.5f);
	if (CeReadHasBar(&sRead))
	{
		float top, foot;
		CeReadBar(&sRead, pitch, &top, &foot);
		FRONTEND.DrawBox(kCeReadTrack + 5.f, top, kCeReadRight - 5.f, foot, 0xffffffff, 5.f, 0.5f);
	}

	static WCHAR help[120], extra[120];
	char text[96];
	if (sRead.mCount > kCeReadRows)
		Widen(help, 120, "Up and Down read. Left and Right turn the page.");
	else
		Widen(help, 120, "That is the whole article.");
	const float next = PortCeExtrasHint(help, NULL, bottom + 12.f);
	if (sSectionCount)
		snprintf(text, sizeof(text), "Select for Contents.  %d percent read.", CeReadPercent(&sRead));
	else
		snprintf(text, sizeof(text), "%d percent read.", CeReadPercent(&sRead));
	Widen(extra, 120, text);
	SIZE size;
	font->GetTextExtent(extra, &size);
	const float left = 320.f - float(size.cx) / 2.f;
	const float right = left + float(size.cx);
	const bool over = !dragBusy && sSectionCount && FRONTEND.MouseOverRect(left, next, right, next + pitch);
	PortCeExtrasHint(over ? extra : NULL, over ? NULL : extra, next);
	if (!dragBusy && sSectionCount && FRONTEND.MouseClickedInRect(left, next, right, next + pitch))
		PortCeExtrasButton(BUTTON_PAUSE_MENU_SELECT, 1.f);
}

const SCeReadPage	kReadPage = {Title, Open, Close, Draw, Button};

//*********************************************************************************
int		Count()
{
	return kCeLoreCount;
}

void	Row(int index, SCeListRow *out)
{
	if ((index >= 0) && (index < kCeLoreCount))
		Widen(out->mLeft, int(sizeof(out->mLeft) / sizeof(WCHAR)), kCeLore[index].mTitle);
}

void	Hint(int index, WCHAR *help, WCHAR *extra, int n)
{
	if ((index >= 0) && (index < kCeLoreCount))
		Widen(help, n, kCeLore[index].mBlurb);
	Widen(extra, n, "Select to read.");
}

bool	Select(int index)
{
	if ((index < 0) || (index >= kCeLoreCount))
		return false;
	sArticle = index;
	PortCeExtrasShow(XP_ARTICLE);
	return true;
}

const WCHAR	*Empty()
{
	static WCHAR none[] = L"No articles are available.";
	return none;
}

int ContentsCount() { return sSectionCount; }

void ContentsRow(int index, SCeListRow *out)
{
	if (index < 0 || index >= sSectionCount)
		return;
	const SCeReadSection &section = sSections[index];
	char label[sizeof(out->mLeft) / sizeof(WCHAR)];
	const int cap = int(sizeof(label)) - 1;
	const int length = section.mLength < cap ? section.mLength : cap;
	memcpy(label, section.mText, size_t(length));
	label[length] = 0;
	Widen(out->mLeft, int(sizeof(label)), label);
}

void ContentsHint(int, WCHAR *help, WCHAR *extra, int n)
{
	Widen(help, n, "Select a heading to read from there.");
	Widen(extra, n, kCeLore[sArticle].mTitle);
}

bool ContentsSelect(int index)
{
	if (!CeReadJump(&sRead, sSections, sSectionCount, index))
		return false;
	sChosen = index;
	PortCeExtrasReturnToReader();
	return true;
}

const WCHAR *NoHeadings() { return L"No headings are available."; }

const SCeListPage kContentsPage = {L"Contents", ContentsCount, ContentsRow, ContentsHint, ContentsSelect, NoHeadings,
	NULL, NULL, NULL, NULL, 1.0f, 0};

const SCeListPage	kListPage = {L"Lore", Count, Row, Hint, Select, Empty, NULL, NULL, NULL, NULL, 1.0f, 0};
}

const SCeListPage	*PortCeLoreListPage() { return &kListPage; }
const SCeListPage *PortCeLoreContentsPage() { return &kContentsPage; }
const SCeReadPage	*PortCeLoreReadPage() { return &kReadPage; }

#endif
