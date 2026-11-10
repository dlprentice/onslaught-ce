// SPDX-License-Identifier: GPL-3.0-only
// The value half of an options-style row and its dropdown list (patch 0212): fits what a row draws after its label on the 640 x 480 page.
#include "common.h"
#include "Platform.h"
#include "ce_menu_fit.h"

bool PortCeQualityOptionsAvailable();	// ce_quality_options.cpp: the ordinary CE menus (never a faithful reference run)

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
