// SPDX-License-Identifier: GPL-3.0-only
// The value half of an options-style row and its dropdown list (patch 0212): fits what a row draws after its label on the 640 x 480 page.
#include "common.h"
#include "Platform.h"
#include "ce_menu_fit.h"

bool PortCeQualityOptionsAvailable();	// ce_quality_options.cpp: the ordinary CE menus (never a faithful reference run)
bool PortCeQualityRow(const void *item);	// ce_quality_options.cpp: one of the Video Options rows Onslaught CE adds
#ifdef CE_VERSION
#include "FrontEndPage.h"
EFrontEndPage PortCeAsOptions(EFrontEndPage page);	// ce_frontend.cpp: Onslaught CE's own pages are options pages too
#endif

namespace
{
// `text` cut so that it and an ellipsis fit in `room` at the horizontal scale `scale` (a buffer that stays valid until the next cut), with the
// extent of what is returned in `size`. Even the ellipsis alone when nothing else fits.
WCHAR *Cut(CBITMAPFONT *font, WCHAR *text, float room, float scale, SIZE *size)
{
    static WCHAR cut[kCeValueMaxChars + 4];
    int length = 0;
    while (text[length] && length < kCeValueMaxChars) ++length;
    for (; length > 0; --length)
    {
        for (int i = 0; i < length; ++i) cut[i] = text[i];
        cut[length] = cut[length + 1] = cut[length + 2] = (WCHAR)'.';
        cut[length + 3] = 0;
        if (font->GetTextExtent(cut, size) && size->cx > 0 && (float)size->cx * scale <= room + 0.01f) break;
    }
    if (!length)
    {
        cut[0] = cut[1] = cut[2] = (WCHAR)'.';
        cut[3] = 0;
        font->GetTextExtent(cut, size);
    }
    return cut;
}
}

// The text to draw for the value of a list row that starts at x, and the horizontal scale to draw it at. A value that fits is
// returned unchanged at scale 1, so every layout that already fit (the game's own pages, a short adapter name) is drawn by
// exactly the call it was. A longer one is squeezed (as the game squeezes a long label); one too long even squeezed is cut with
// an ellipsis in a buffer that stays valid until the next call.
WCHAR *PortCeValueFit(WCHAR *text, float x, float *scale)
{
    *scale = 1.f;
    if (!text || !PortCeQualityOptionsAvailable()) return text;
    CBITMAPFONT *font = PLATFORM.Font(FONT_SMALL);
    SIZE size;
    if (!font || !font->GetTextExtent(text, &size) || size.cx <= 0) return text;
    const float room = kCeValueRight - x;
    float fit = PortCeFitScale(x, (float)size.cx, kCeValueRight, kCeValueMinScale);
    if ((float)size.cx * fit <= room + 0.01f)
    {
        *scale = fit;
        return text;
    }
    WCHAR *cut = Cut(font, text, room, kCeValueMinScale, &size);
    *scale = size.cx > 0 ? PortCeFitScale(x, (float)size.cx, kCeValueRight, kCeValueMinScale) : kCeValueMinScale;
    return cut;
}

// The text a dropdown list draws for one of its entries, which starts at x: unchanged when it fits (so a list that fit is drawn and
// measured exactly as before), else cut with an ellipsis to end at the page's column. The list measures its widest entry with this
// text too, so its box follows.
WCHAR *PortCeListFit(WCHAR *text, float x)
{
    if (!text || !PortCeQualityOptionsAvailable()) return text;
    CBITMAPFONT *font = PLATFORM.Font(FONT_SMALL);
    SIZE size;
    if (!font || !font->GetTextExtent(text, &size) || size.cx <= 0) return text;
    const float room = kCeValueRight - x;
    if ((float)size.cx <= room) return text;
    return Cut(font, text, room, 1.f, &size);
}

namespace
{
// The one expanded list's window: lists open one at a time.
struct SListWindow
{
    const void *owner;	// the item whose list it is
    int first;			// its first entry drawn
    int hovered;		// the entry the pointer rested on when it was last drawn, else -1
    float seen;			// when it was last drawn
    float scrolled;		// when the pointer last moved it
};
SListWindow gWindow = { 0, -1, -1, -1.f, -1.f };

// The front end's options pages (Options and Onslaught CE's own) show their title bar above a list; a level's Pause Menu does not.
bool OptionsPage(int page)
{
#ifdef CE_VERSION
    return PortCeAsOptions((EFrontEndPage)page) == FEP_OPTIONS;
#else
    (void)page;
    return false;
#endif
}

bool Within(float now, float since, float limit) { return since >= 0.f && now >= since && now - since < limit; }
}

// RenderList (patch 0212) for an expanded list of `count` entries `rowh` apart at the row y, on the front end's page `page`: 0 when the
// list keeps the original's layout (it fits inside its page's limits, or this is not the ordinary CE menus); else 1, with where its first
// entry is drawn, which entry that is and how many are drawn.
int PortCeListWindow(const void *owner, float y, long rowh, int count, int selection, int page, float *top, int *first, int *shown)
{
    if (!owner || !PortCeQualityOptionsAvailable()) return 0;
    const bool front = OptionsPage(page);
    const float now = PLATFORM.GetSysTimeFloat();
    const bool open = gWindow.owner == owner && Within(now, gWindow.seen, kCeListForget);
    const int hovered = open ? gWindow.hovered : -1;
    gWindow.hovered = -1;
    SCeListPlace place;
    if (!PortCeListPlace(y, rowh, count, selection, open ? gWindow.first : -1, front ? kCeListFrontTop : kCeListPauseTop,
                         front ? kCeListFrontBottom : kCeListPauseBottom, &place))
    {
        gWindow.owner = 0;
        return 0;
    }
    const int step = PortCeListEdgeStep(place.first, place.shown, count, hovered);
    if (step && !Within(now, gWindow.scrolled, kCeListScrollEvery))
    {
        place.first += step;
        gWindow.scrolled = now;
    }
    gWindow.owner = owner;
    gWindow.first = place.first;
    gWindow.seen = now;
    *top = place.top;
    *first = place.first;
    *shown = place.shown;
    return 1;
}

// RenderList: the pointer rests on entry `entry` of the window PortCeListWindow laid out this frame.
void PortCeListHover(int entry)
{
    gWindow.hovered = entry;
}

// Left margin for a row's label in RenderList (patch 0212): original 5 units outside the front end's options pages,
// kCeLabelLeft at the top/bottom of options pages, relaxing along the curved left metal arc toward 58 units around y 290.
float PortCeLabelLeft(int page)
{
    return (PortCeQualityOptionsAvailable() && OptionsPage(page)) ? kCeLabelLeft : 5.f;
}

float PortCeLabelLeftAt(int page, float y)
{
    if (!PortCeQualityOptionsAvailable() || !OptionsPage(page)) return 5.f;
    const float dy = y - 290.f;
    float t = (dy * dy) / (150.f * 150.f);
    if (t > 1.f) t = 1.f;
    return 58.f + (kCeLabelLeft - 58.f) * t;
}

// Horizontal gap after a row's colon in RenderList (patch 0212): original 2 units in faithful runs, kCeValueGap in CE menus.
float PortCeValueGap()
{
    return PortCeQualityOptionsAvailable() ? kCeValueGap : 2.f;
}

// CMenuOptionsList::Render (patch 0215): the row pitch of a list holding `item`, 0 for the list's own item heights.
int PortCeCompactPitch(const void *item)
{
    return (PortCeQualityRow(item) && PortCeQualityOptionsAvailable()) ? kCeCompactPitch : 0;
}

// Extra vertical separation before Apply/Back at the bottom of the compact Video Options list (patch 0215).
int PortCeCompactActionGap(int pitch, int index, int count)
{
    return (pitch && count >= 2 && index == count - 2) ? kCeCompactActionGap : 0;
}

// How far that list's centre moves: down in the front end (so the first row stays below the emblem) and slightly up in a level's Pause Menu
// (so Back clears the message bar).
float PortCeCompactShift(int pitch, bool options_page)
{
    if (!pitch) return 0.f;
    return options_page ? kCeCompactFrontShift : kCeCompactPauseShift;
}
