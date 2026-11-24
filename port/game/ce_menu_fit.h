// SPDX-License-Identifier: GPL-3.0-only
// Long values in the options-style lists (a graphics adapter's marketing name, a folder) fit the 640 x 480 page.
// The game squeezes a label that is too wide for the left half of its row; the value half had no such rule, because the
// values of 2003 ("NVIDIA GeForce4 Ti 4200") were short. patch 0212 asks PortCeValueFit for each value it draws.
#ifndef CE_MENU_FIT_H
#define CE_MENU_FIT_H

// The virtual column where a value must end: the pages' right-hand ornament begins a little before the screen's edge.
static const float kCeValueRight = 600.f;
// The virtual column where a label must begin near the top/bottom of front-end options pages so long labels clear the curved
// left metal arc; mid-screen rows relax toward 58 units via PortCeLabelLeftAt(page, y).
static const float kCeLabelLeft = 104.f;
float PortCeLabelLeft(int page);
float PortCeLabelLeftAt(int page, float y);
// Horizontal gap between a row's colon and its value in CE menus (original uses 2 units).
static const float kCeValueGap = 6.f;
// How far a value is squeezed before its end is cut off and marked: the narrowest text that still reads.
static const float kCeValueMinScale = 0.75f;
// Where a value that cannot fit even squeezed is cut (characters kept, then "...").
static const int kCeValueMaxChars = 60;

// The horizontal scale that makes text of `width` units, drawn from x, end at `right`: exactly 1 when it already fits (the
// game's own draw call is then unchanged), else the fitting scale, never below `minimum` (the caller cuts what still overflows).
inline float PortCeFitScale(float x, float width, float right, float minimum)
{
    const float room = right - x;
    if (!(width > 0.f) || width <= room) return 1.f;
    if (!(room > 0.f)) return minimum;
    const float scale = room / width;
    return scale < minimum ? minimum : scale;
}

// A drop-down list stays inside its page (patch 0212, CMenuMenuItem::RenderList). The original centres the list on its row and, when it
// runs off the screen, moves it back inside 0..480 or squeezes it; a display with many sizes then drew the Screen mode list under the
// options page's title bar. With the ordinary CE menus a list the original would place outside these limits is moved inside them, and one
// with more entries than fit shows a window of them that keeps the highlighted entry in view. A list the original places inside them is
// drawn by exactly the original's calls.
static const float kCeListFrontTop = 104.f;		// the front end's options pages: under the title bar
static const float kCeListFrontBottom = 452.f;		// and above the help line
static const float kCeListPauseTop = 8.f;		// a level's Pause Menu: nearly the whole screen
static const float kCeListPauseBottom = 472.f;
// The room an entry takes when a window is sized: at least the original's 16-unit row (the small font's rows measure 16; a taller font
// gets its own height).
static const float kCeListEntryRoom = 16.f;
// While the pointer rests on the first or last entry of a window with more entries beyond it, the window moves one entry this often
// (seconds): the mouse has no other way to reach them (the original binds no wheel in the menus).
static const float kCeListScrollEvery = 0.15f;
// A window the list has not been drawn in for this long (seconds) was closed: the next one opens centred on the highlighted entry again.
static const float kCeListForget = 0.5f;

struct SCeListPlace
{
    int first;		// the first entry drawn
    int shown;		// how many are drawn
    float top;		// where the first one is drawn
};

// Where the original's RenderList puts the top of a list of `count` entries `rowh` apart for the row at y (its own arithmetic).
inline int PortCeListOriginalTop(float y, long rowh, int count)
{
    return int(y - ((count - 1) * rowh) / 2);
}

// The window of an expanded list. False, with nothing written, when the original's placement already lies inside [topLimit, bottomLimit]:
// that list keeps the original's layout. Otherwise as many entries as fit (never squeezed); the first of them kept from `first` while
// the selection is in view, else moved the least that brings it in, or centred on the selection when `first` is negative (a list just
// opened); and the top centred on the row as the original centres it, then moved inside the limits. Each entry is given at least
// kCeListEntryRoom when the window is sized and moved, so its panel stays inside the limits too.
inline bool PortCeListPlace(float y, long rowh, int count, int selection, int first, float topLimit, float bottomLimit, SCeListPlace *out)
{
    if (count <= 0 || rowh <= 0) return false;
    const int top = PortCeListOriginalTop(y, rowh, count);
    if ((float)top >= topLimit && (float)top + (float)(count * rowh) <= bottomLimit) return false;
    const float room = (float)rowh > kCeListEntryRoom ? (float)rowh : kCeListEntryRoom;
    // In whole units and integer division: the game's files are compiled to convert floats to integers by rounding (/QIfist), and
    // 348 / 16 = 21.75 must give 21 entries, not 22.
    int capacity = int(bottomLimit - topLimit) / int(room);
    if (capacity < 1) capacity = 1;
    const int shown = count < capacity ? count : capacity;
    if (first < 0) first = selection - shown / 2;
    else if (selection < first) first = selection;
    else if (selection >= first + shown) first = selection - shown + 1;
    if (first > count - shown) first = count - shown;
    if (first < 0) first = 0;
    float placed = (float)PortCeListOriginalTop(y, rowh, shown);
    if (placed + room * (float)shown > bottomLimit) placed = bottomLimit - room * (float)shown;
    if (placed < topLimit) placed = topLimit;
    out->first = first;
    out->shown = shown;
    out->top = placed;
    return true;
}

// How far the pointer resting on entry `hovered` moves the window: -1 on its first entry with more above, 1 on its last with more below.
inline int PortCeListEdgeStep(int first, int shown, int count, int hovered)
{
    if (hovered == first && first > 0) return -1;
    if (hovered >= 0 && hovered == first + shown - 1 && first + shown < count) return 1;
    return 0;
}

// Video Options with Onslaught CE's rows has twenty-two rows (twenty-one without the widescreen switch), up to eight more
// than the original's fourteen: its first row would meet the emblem in the front end and Back would meet the spoken subtitle
// in a level at 20 units a row. That one list (patch 0215, CMenuOptionsList::Render) uses 14 units a row plus a 4-unit
// breathing gap before Apply/Back. In the front end it is centred 7 units lower (on 307, not 300), so its first row stays
// cleanly below the top emblem (y 136..143 > 127) and Back ends above the help line (450 <= 452 for 22 rows, 443 <= 452 for
// 21 rows); in a level's Pause Menu it shifts 10 units higher so Back clears the message bar (403 <= 406 for 22 rows,
// 396 <= 406 for 21 rows).
static const int kCeCompactPitch = 14;
static const int kCeCompactActionGap = 4;
static const float kCeCompactFrontShift = 7.f;
static const float kCeCompactPauseShift = -10.f;

#endif
