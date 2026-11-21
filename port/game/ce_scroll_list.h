// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_scroll_list.h: the state and layout of a scrolling list box, as the original's Load Game page draws one (a dark
// panel with a thin outline, whole rows, a boxed scroll track with its bar on the right edge). Nothing here draws or reads the game:
// the Extras pages (ce_extras.cpp) draw from these numbers with the front end's own panel, box and font calls, and a host test
// (port/tools/ce_extras_tests.py) runs the same functions without the game.
//
// The numbers are the Load Game page's own, measured on a frame of it: the panel from x 128 to 530, rows from y 130 at a pitch of 24,
// the track from x 510, the bar inset by 5. The Extras pages show ten rows (the Load Game page shows eleven) so that the highlighted
// row's hint has room between the box and the help bar.
#ifndef CE_SCROLL_LIST_H
#define CE_SCROLL_LIST_H

static const float	kCeListLeft = 132.f;		// a row's text starts here; the panel starts 4 to its left
static const float	kCeListTop = 130.f;
static const float	kCeListPitch = 24.f;
static const float	kCeListTrack = 510.f;		// the scroll track's left edge; a row's own area ends here
static const float	kCeListRight = 530.f;
static const int	kCeListRows = 10;			// rows in view

struct SCeScrollList
{
	int		mCount;			// rows in the list
	int		mCursor;		// the highlighted row
	int		mTop;			// the first row in view
};

inline float	CeListBottom()
{
	return kCeListTop + kCeListPitch * kCeListRows + 7.f;
}

// Brings the cursor into the list and the view onto the cursor, a whole row at a time.
inline void	CeListClamp(SCeScrollList *list)
{
	if (list->mCount < 0)
		list->mCount = 0;
	if (list->mCursor >= list->mCount)
		list->mCursor = list->mCount - 1;
	if (list->mCursor < 0)
		list->mCursor = 0;
	const int last = (list->mCount > kCeListRows) ? list->mCount - kCeListRows : 0;
	if (list->mTop > last)
		list->mTop = last;
	if (list->mTop < 0)
		list->mTop = 0;
	if (list->mTop > list->mCursor)
		list->mTop = list->mCursor;
	if (list->mTop + kCeListRows <= list->mCursor)
		list->mTop = list->mCursor - kCeListRows + 1;
}

inline void	CeListReset(SCeScrollList *list, int count)
{
	list->mCount = count;
	list->mCursor = 0;
	list->mTop = 0;
	CeListClamp(list);
}

// Moves the cursor by delta rows and stops at the ends, as the Load Game list does; true when it moved.
inline bool	CeListMove(SCeScrollList *list, int delta)
{
	const int was = list->mCursor;
	list->mCursor += delta;
	CeListClamp(list);
	return list->mCursor != was;
}

// A page up (-1) or down (+1): the cursor goes to the view's edge, and from the edge a whole view further.
inline bool	CeListPage(SCeScrollList *list, int direction)
{
	const int was = list->mCursor;
	if (direction < 0)
		list->mCursor = (list->mCursor > list->mTop) ? list->mTop : list->mCursor - kCeListRows;
	else
	{
		const int foot = list->mTop + kCeListRows - 1;
		list->mCursor = (list->mCursor < foot) ? foot : list->mCursor + kCeListRows;
	}
	CeListClamp(list);
	return list->mCursor != was;
}

// The view moves by delta rows (the mouse wheel) and the cursor follows only as far as it must to stay in view; true when it moved.
inline bool	CeListScroll(SCeScrollList *list, int delta)
{
	const int was = list->mTop;
	int top = list->mTop + delta;
	const int last = (list->mCount > kCeListRows) ? list->mCount - kCeListRows : 0;
	if (top > last)
		top = last;
	if (top < 0)
		top = 0;
	if (list->mCursor < top)
		list->mCursor = top;
	if (list->mCursor > top + kCeListRows - 1)
		list->mCursor = top + kCeListRows - 1;
	list->mTop = top;
	CeListClamp(list);
	return list->mTop != was;
}

// The row at screen y, or -1 when y is outside the rows in view or below the list's last row.
inline int	CeListRowAt(const SCeScrollList *list, float y)
{
	if (y < kCeListTop)
		return -1;
	const int row = int((y - kCeListTop) / kCeListPitch);
	if ((row < 0) || (row >= kCeListRows) || (list->mTop + row >= list->mCount))
		return -1;
	return list->mTop + row;
}

// The top of a row in view.
inline float	CeListRowY(const SCeScrollList *list, int index)
{
	return kCeListTop + kCeListPitch * float(index - list->mTop);
}

// The scroll bar inside its track, as the Load Game page places it: the track less 5 at each end, the bar covering the rows in view as
// a share of the rows. A list that fits has a bar the whole length. A long list's bar is never shorter than one row, so that it stays
// a bar and not a small square; it then travels the track less its own length, from the top end with the first rows in view to the
// bottom end with the last.
inline float	CeListTrackLength()
{
	return CeListBottom() - kCeListTop - 10.f;
}

inline float	CeListBarLength(const SCeScrollList *list)
{
	const float track = CeListTrackLength();
	if (list->mCount <= kCeListRows)
		return track;
	const float length = track * float(kCeListRows) / float(list->mCount);
	return (length < kCeListPitch) ? kCeListPitch : length;
}

inline void	CeListBar(const SCeScrollList *list, float *top, float *bottom)
{
	const float length = CeListBarLength(list);
	const int last = list->mCount - kCeListRows;
	*top = kCeListTop + 5.f;
	if (last > 0)
		*top += (CeListTrackLength() - length) * float(list->mTop) / float(last);
	*bottom = *top + length;
}

// The pointer held in the track at screen y: the view goes where the bar's middle would be under the pointer.
inline void	CeListDrag(SCeScrollList *list, float y)
{
	if (list->mCount <= kCeListRows)
		return;
	const float length = CeListBarLength(list);
	const float travel = CeListTrackLength() - length;
	const float share = (y - length * 0.5f - kCeListTop - 5.f) / travel;
	const int top = int(share * float(list->mCount - kCeListRows) + 0.5f);
	CeListScroll(list, top - list->mTop);
}

#endif
