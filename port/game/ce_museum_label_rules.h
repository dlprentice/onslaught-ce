// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_museum_label_rules.h: which label the visitor is shown, and how a capsule opens and closes. Plain arithmetic with no engine type, so a host test
// runs it (ce_museum_label_tests.py); ce_museum_label.cpp draws what it answers.
#ifndef CE_MUSEUM_LABEL_RULES_H
#define CE_MUSEUM_LABEL_RULES_H

// The label is the one for the exhibit the visitor is looking at: inside this distance of its centre (world units, on the ground plane) and within about
// twenty degrees of where the visitor faces (the cosine of the angle from the heading). A label already shown is kept a little beyond both, so one at the
// edge does not flicker as the walker rocks or turns. Of the exhibits that qualify the most central one wins, not the nearest: a machine at the side of
// the view never names itself over the one in the crosshair.
static const float kCeMuseumLabelRange = 30.0f, kCeMuseumLabelKeep = 36.0f;
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

#endif
