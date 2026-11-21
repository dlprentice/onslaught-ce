// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_reading.h: the state and layout of a reading page (the Lore articles): lines of text in a panel with the list box's own
// scroll track beside it, moved a whole line at a time. Nothing here draws or reads the game: ce_lore.cpp draws from these numbers, and
// a host test (port/tools/ce_lore_tests.py) runs the same functions without the game.
//
// The panel is the Load Game list's (x 128 to 530, from y 130) and the track and bar are the list box's (x 510 to 530, the bar inset by
// 5), so that a reading page and the lists around it are one set of pages. The text is in the small face, which the guide sets for long
// text: fourteen lines in view.
#ifndef CE_READING_H
#define CE_READING_H

static const float	kCeReadLeft = 140.f;		// the text's left edge; the panel starts 12 to its left
static const float	kCeReadWidth = 364.f;		// the width a line is wrapped to
static const float	kCeReadTop = 130.f;
static const float	kCeReadTrack = 510.f;
static const float	kCeReadRight = 530.f;
static const int	kCeReadRows = 14;			// lines in view

struct SCeReading
{
	int		mCount;			// lines in the article
	int		mTop;			// the first line in view
};

// The panel's foot, for a line height of pitch.
inline float	CeReadBottom(float pitch)
{
	return kCeReadTop + pitch * kCeReadRows + 12.f;
}

inline void	CeReadClamp(SCeReading *page)
{
	const int last = (page->mCount > kCeReadRows) ? page->mCount - kCeReadRows : 0;
	if (page->mTop > last)
		page->mTop = last;
	if (page->mTop < 0)
		page->mTop = 0;
}

// Each entry points to text in the immutable article pack and to the first
// line of that heading after the current font has wrapped the article.
struct SCeReadSection
{
	int mLine;
	const char *mText;
	int mLength;
};

inline bool CeReadJump(SCeReading *page, const SCeReadSection *sections, int count, int index)
{
	if (!sections || index < 0 || index >= count || sections[index].mLine < 0 || sections[index].mLine >= page->mCount)
		return false;
	page->mTop = sections[index].mLine;
	CeReadClamp(page);
	return true;
}

inline int CeReadSectionAt(const SCeReading *page, const SCeReadSection *sections, int count, int chosen)
{
	if (!sections || count <= 0)
		return -1;
	// Two headings on the last screen can have the same legal scroll position.
	SCeReading previous = *page;
	if (CeReadJump(&previous, sections, count, chosen) && previous.mTop == page->mTop)
		return chosen;
	int current = 0;
	while (current + 1 < count && sections[current + 1].mLine <= page->mTop)
		++current;
	return current;
}

inline void	CeReadOpen(SCeReading *page, int count)
{
	page->mCount = (count < 0) ? 0 : count;
	page->mTop = 0;
	CeReadClamp(page);
}

// The view moves by delta lines (a key, the wheel); true when it moved.
inline bool	CeReadMove(SCeReading *page, int delta)
{
	const int was = page->mTop;
	page->mTop += delta;
	CeReadClamp(page);
	return page->mTop != was;
}

// A page up (-1) or down (+1): a whole view less one line, so that the last line read is the first line of the next view.
inline bool	CeReadPage(SCeReading *page, int direction)
{
	return CeReadMove(page, direction * (kCeReadRows - 1));
}

// The bar inside the track (the track less 5 at each end, as the list box's): the lines in view as a share of the lines, never shorter
// than a line, travelling the track less its own length from the first lines to the last.
inline float	CeReadTrackLength(float pitch)
{
	return CeReadBottom(pitch) - kCeReadTop - 10.f;
}

inline float	CeReadBarLength(const SCeReading *page, float pitch)
{
	const float track = CeReadTrackLength(pitch);
	if (page->mCount <= kCeReadRows)
		return track;
	const float length = track * float(kCeReadRows) / float(page->mCount);
	return (length < pitch) ? pitch : length;
}

inline void	CeReadBar(const SCeReading *page, float pitch, float *top, float *bottom)
{
	const float length = CeReadBarLength(page, pitch);
	const int last = page->mCount - kCeReadRows;
	*top = kCeReadTop + 5.f;
	if (last > 0)
		*top += (CeReadTrackLength(pitch) - length) * float(page->mTop) / float(last);
	*bottom = *top + length;
}

// The pointer held in the track at screen y: the view goes where the bar's middle would be under the pointer.
inline void	CeReadDrag(SCeReading *page, float pitch, float y)
{
	if (page->mCount <= kCeReadRows)
		return;
	const float length = CeReadBarLength(page, pitch);
	const float travel = CeReadTrackLength(pitch) - length;
	const float share = (y - length * 0.5f - kCeReadTop - 5.f) / travel;
	CeReadMove(page, int(share * float(page->mCount - kCeReadRows) + 0.5f) - page->mTop);
}

// How far into the article the view is, in whole percent of the lines (the last line in view over the count).
inline int	CeReadPercent(const SCeReading *page)
{
	if (page->mCount <= 0)
		return 100;
	const int foot = page->mTop + kCeReadRows;
	const int shown = (foot > page->mCount) ? page->mCount : foot;
	return (shown * 100) / page->mCount;
}

#endif
