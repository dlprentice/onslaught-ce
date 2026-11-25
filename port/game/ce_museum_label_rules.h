// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_label_rules.h: which label the visitor is shown, and how a capsule opens and closes. Plain arithmetic with no engine type, so a host test
// runs it (ce_museum_label_tests.py); ce_museum_label.cpp draws what it answers.
#ifndef CE_MUSEUM_LABEL_RULES_H
#define CE_MUSEUM_LABEL_RULES_H

// The label is the one for the exhibit the visitor is looking at: inside this distance of its centre (world units, on the ground plane) and within about
// twenty degrees of where the visitor faces (the cosine of the angle from the heading). A label already shown is kept a little beyond both, so one at the
// edge does not flicker as the walker rocks or turns. Of the exhibits that qualify the most central one wins, not the nearest: a machine at the side of
// the view never names itself over the one in the crosshair.
static const float kCeMuseumLabelRange = 14.0f, kCeMuseumLabelKeep = 17.0f;
static const float kCeMuseumLabelFront = 0.94f, kCeMuseumLabelFrontKeep = 0.88f;
// How much more central another exhibit must be, as a share of the squared cosine, to take the label from the one shown.
static const float kCeMuseumLabelHold = 0.03f;
// What each unit of distance costs a machine in the same comparison: about the difference three degrees off the line of sight makes, over ten units.
static const float kCeMuseumLabelFar = 0.0003f;

// The index of the exhibit to show, or -1. xy holds each exhibit's ground position (X, Y); (px, py) is the visitor's and (fx, fy) the direction it faces
// (any length; a zero direction shows the nearest in range). current is the exhibit shown now, or -1.
inline int CeMuseumNearestLabel(const float (*xy)[2], unsigned count, float px, float py, float fx, float fy, int current)
{
	const float facing = fx * fx + fy * fy;
	int best = -1;
	float bestScore = 0.0f;
	for (unsigned i = 0; i != count; ++i)
	{
		const float dx = xy[i][0] - px, dy = xy[i][1] - py, d2 = dx * dx + dy * dy;
		const bool held = static_cast<int>(i) == current;
		const float limit = held ? kCeMuseumLabelKeep : kCeMuseumLabelRange;
		if (!(d2 <= limit * limit))
			continue;										// too far, or not a number
		float score;
		if (facing > 0.0f && d2 > 0.0f)
		{
			const float along = dx * fx + dy * fy;			// the cosine's numerator
			const float need = held ? kCeMuseumLabelFrontKeep : kCeMuseumLabelFront;
			if (along <= 0.0f || along * along < need * need * d2 * facing)
				continue;
			// the squared cosine (one is dead ahead), less a little for distance so that of two machines on the same line of sight the nearer is named
			score = along * along / (d2 * facing) - kCeMuseumLabelFar * __builtin_sqrtf(d2) + (held ? kCeMuseumLabelHold : 0.0f);
		}
		else if (facing > 0.0f)
			score = 2.0f;									// standing on it
		else
			score = -d2;									// no heading: the nearest
		if (best < 0 || score > bestScore)
		{
			best = static_cast<int>(i);
			bestScore = score;
		}
	}
	return best;
}

// Which exhibit card to show (an index, or -1). In a hall of numbered displays (signs) a card is the display's own: only the display whose sign the visitor
// stands at and faces (sign, from the hall) names itself, so a machine seen down the hall from the entrance never puts its card up before the visitor reaches
// it. The in-view rule (nearest, CeMuseumNearestLabel) is for a hall without signs.
inline int CeMuseumCardLabel(bool signs, int sign, int nearest)
{
	if (sign >= 0)
		return sign;
	return signs ? -1 : nearest;
}

// The threshold measures from the near face of a large exhibit, not just its centre.
// Once the visitor is inspecting it, a small step back should not flicker the card.
inline bool CeMuseumInspectionNear(float dx, float dy, float radius, bool inspecting)
{
	const float reach = radius + (inspecting ? 7.5f : 6.0f);
	return radius >= 0.0f && dx * dx + dy * dy <= reach * reach;
}

// Whether a Battle Engine at height z (Z counts downward) is within the sea's reach: within `margin` of the water, or in it. The original ends the engine within 0.2
// of the water whatever its armour, so a hall puts the visitor back before that. A position that is not a number is not in reach (and is not moved).
inline bool CeMuseumInSeaReach(float z, float waterLevel, float margin)
{
	return z > waterLevel - margin;
}

// How far from where it started a visitor may go before the hall puts it back (world units, on the ground plane). The hall is a small site on an island: a
// walker is stopped by the dunes, but a Battle Engine in jet mode can fly out over the open sea with nothing in view, so the hall ends here.
static const float kCeMuseumHallRadius = 90.0f;

// Whether a visitor at (x, y), having started at (startX, startY), is past the hall's edge. A position that is not a number is not past it (and is not moved).
inline bool CeMuseumPastHallEdge(float x, float y, float startX, float startY, float radius)
{
	const float dx = x - startX, dy = y - startY;
	return dx * dx + dy * dy > radius * radius;
}

// Whether the Battle Engine's forward direction is level enough for a label to name what is ahead: within about thirty degrees of the horizon. Flying with the nose
// up or down over the hill and the sea, the machines on the ground are not what the visitor is looking at, so no name is held over that view.
inline bool CeMuseumLookingLevel(float fx, float fy, float fz)
{
	return fz * fz <= 0.25f * (fx * fx + fy * fy + fz * fz);
}

// Whether the line under a name only says again the name's last word (Forseti City Building, Building): the label then keeps the name alone. Letters are
// compared without case; a name or a line that is empty repeats nothing.
inline bool CeMuseumLineRepeatsName(const char *name, const char *line)
{
	if (!name || !line || !name[0] || !line[0])
		return false;
	unsigned length = 0, last = 0;
	for (unsigned i = 0; name[i]; ++i)
		if (name[i] == ' ')
			last = i + 1;
	for (; name[last + length]; ++length)
		;
	for (unsigned i = 0; i != length; ++i)
	{
		char a = name[last + i], b = line[i];
		if (!b)
			return false;
		if (a >= 'A' && a <= 'Z')
			a = static_cast<char>(a - 'A' + 'a');
		if (b >= 'A' && b <= 'Z')
			b = static_cast<char>(b - 'A' + 'a');
		if (a != b)
			return false;
	}
	return length != 0 && !line[length];
}

// How far a capsule is open, 0 to 1, for a text shown from `since` for `shown` milliseconds: it grows over `grow` ms and, when it is to end, shrinks over the same.
inline float CeMuseumCapsuleOpen(unsigned long long now, unsigned long long since, unsigned long long shown, unsigned long long grow, bool ending)
{
	if (now < since)
		return 0.0f;
	const unsigned long long age = now - since;
	float open = age >= grow ? 1.0f : static_cast<float>(age) / static_cast<float>(grow);
	if (ending && age >= shown)
		return 0.0f;
	if (ending && shown > grow && age + grow > shown)
		open = static_cast<float>(shown - age) / static_cast<float>(grow);
	return open < 0.0f ? 0.0f : open > 1.0f ? 1.0f : open;
}

// How far the hall's title capsule (a place's name at the top of the view) is open: it grows over `grow` ms from `since` and goes at once when its `shown`
// ms are up (a bar shrinking shut has already lost its words). It also goes at once when the visitor has walked out of the places a title names, into the
// screening room or its doorway (elsewhere): the projector's console panel takes the top of the view there, and a title is about the hall, not the cinema.
inline float CeMuseumTitleOpen(unsigned long long now, unsigned long long since, unsigned long long shown, unsigned long long grow, bool elsewhere)
{
	if (elsewhere || now < since || now - since >= shown)
		return 0.0f;
	return CeMuseumCapsuleOpen(now, since, shown, grow, false);
}

// Whether the visitor is away from what the title names: in the screening room or its doorway (cinema), or, for a place's name (wayfinding: the entrance
// or a numbered display), outside that place (inPlace false). The hall's own name and the put-back note are not places and only the cinema ends them early.
inline bool CeMuseumTitleElsewhere(bool cinema, bool wayfinding, bool inPlace)
{
	return cinema || (wayfinding && !inPlace);
}

// A definition's own name as a label reads it: Title Case words, the separators the small face lacks ( _ - . ) and every other character it lacks as spaces,
// a word written in capitals lowered after its first letter (a capital after a lower case letter, as in ForsetiTank, is kept), and a number that ends the
// name dropped. out holds size bytes; the result is always terminated and never longer than size - 1.
inline void CeMuseumPrettyName(char *out, unsigned size, const char *name)
{
	if (!out || !size)
		return;
	unsigned n = 0;
	bool start = true;
	for (; name && name[n] && n + 1 < size; ++n)
	{
		char c = name[n];
		const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'), digit = c >= '0' && c <= '9';
		if (!letter && !digit)
			c = ' ';
		if (start && c >= 'a' && c <= 'z')
			c = static_cast<char>(c - 'a' + 'A');
		else if (!start && c >= 'A' && c <= 'Z' && n && name[n - 1] >= 'A' && name[n - 1] <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
		start = (c == ' ');
		out[n] = c;
	}
	out[n] = 0;
	// A number that ends a name, after a space, is the level's own count of that kind of thing (Forseti City Building 1), never part of a name: it goes,
	// and so does a space left at the end.
	unsigned end = n;
	while (end > 0 && out[end - 1] >= '0' && out[end - 1] <= '9')
		--end;
	if (end < n && end >= 2 && out[end - 1] == ' ')
		out[n = end] = 0;
	while (n > 0 && out[n - 1] == ' ')
		out[--n] = 0;
}

// The line under an enlarged exhibit's name that says plainly it is not shown at its own size: "Shown three times life size." for a factor of three.
// Empty (and false) for a factor the words do not cover; out is always terminated when it has room.
inline bool CeMuseumScaleNote(char *out, unsigned size, unsigned factor)
{
	static const char *const kWords[] = {"two", "three", "four", "five", "six", "seven", "eight"};
	if (!out || !size)
		return false;
	out[0] = 0;
	if (factor < 2 || factor > 8)
		return false;
	const char *const parts[] = {"Shown ", kWords[factor - 2], " times life size."};
	unsigned n = 0;
	for (const char *part : parts)
		for (; *part; ++part)
		{
			if (n + 1 >= size)
			{
				out[0] = 0;
				return false;
			}
			out[n++] = *part;
		}
	out[n] = 0;
	return true;
}

// One rule for every Museum caption bar (a hall's title at the top, an exhibit's plaque at the bottom).
//
// The bar is the HUD's objective bar: a bracket cap is drawn centred on each end and reaches half the bar's height inward, so text needs that
// much room at each side before it touches a bracket. A fully open bar is the widest line plus kCeMuseumCaptionPad; while it opens, never
// narrower than the text plus the two caps. The HUD scale enlarges everything drawn in a caption's scope, so the width allowed is counted
// in the caption's own units: at most kCeMuseumCaptionViewShare of the view after scaling, which keeps a title clear of the facade sign
// behind it and a plaque inside the screen at every HUD size. Text wider than the room left is first wrapped onto a second line (the caller
// splits at CeMuseumCaptionSplit), then squeezed horizontally, never below kCeMuseumCaptionMinSqueeze.
static const float kCeMuseumCaptionPad = 70.0f;
static const float kCeMuseumCaptionViewShare = 0.60f;
static const float kCeMuseumCaptionMinSqueeze = 0.72f;

inline float CeMuseumCaptionCapInset(float barHeight)
{
	return barHeight > 0.0f ? barHeight * 0.5f : 0.0f;
}

struct CeMuseumCaptionFit
{
	float most;			// the widest the bar may ever be, in the caption's own units
	float fullWidth;	// the bar when fully open
	float barWidth;		// the bar at this opening
	float textRoom;		// the width its text may fill
	float scaleX;		// horizontal squeeze for the text (1: none)
	bool overflow;		// the widest line does not fit textRoom unsqueezed: wrap it if it can be wrapped
};

// widest: the widest text line, unscaled. minWidth: a width the bar keeps even for short text (0: none). open: 0.45 (first visible) to 1.
inline CeMuseumCaptionFit CeMuseumCaptionLayout(float widest, float barHeight, float viewWidth, float hudScale, float open, float minWidth)
{
	CeMuseumCaptionFit fit = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, false};
	if (!(widest >= 0.0f) || !(viewWidth > 0.0f))
		return fit;
	const float scale = hudScale > 0.01f ? hudScale : 1.0f;
	const float inset = CeMuseumCaptionCapInset(barHeight);
	fit.most = viewWidth * kCeMuseumCaptionViewShare / scale;
	float full = widest + kCeMuseumCaptionPad;
	if (full < minWidth)
		full = minWidth;
	if (full > fit.most)
		full = fit.most;
	float room = full - 2.0f * inset;
	if (room < 1.0f)
		room = 1.0f;
	fit.fullWidth = full;
	fit.textRoom = room;
	fit.overflow = widest > room;
	if (fit.overflow && widest > 0.0f)
	{
		fit.scaleX = room / widest;
		if (fit.scaleX < kCeMuseumCaptionMinSqueeze)
			fit.scaleX = kCeMuseumCaptionMinSqueeze;
	}
	const float t = open >= 1.0f ? 1.0f : (open <= 0.45f ? 0.0f : (open - 0.45f) / 0.55f);
	float closed = widest * fit.scaleX + 2.0f * inset;
	if (closed > full)
		closed = full;
	fit.barWidth = closed + (full - closed) * t;
	return fit;
}

// Where to break a line in two: the index of the space nearest its middle (the first line ends before it, the second starts after it),
// or -1 when the line has no space to break at or is too short to be worth breaking.
template <typename Char>
inline int CeMuseumCaptionSplit(const Char *text)
{
	if (!text)
		return -1;
	int length = 0;
	while (text[length])
		++length;
	if (length < 12)
		return -1;
	int best = -1, bestAway = length;
	for (int i = 1; i < length - 1; ++i)
	{
		if (text[i] != Char(' '))
			continue;
		const int away = i * 2 > length ? i * 2 - length : length - i * 2;
		if (away < bestAway)
		{
			bestAway = away;
			best = i;
		}
	}
	return best;
}

#endif
