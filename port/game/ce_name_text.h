// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_name_text.h: a file name's bytes as the characters the list shows. On Linux a name is UTF-8, so a character of
// several bytes is one character; a byte sequence that is not valid UTF-8 gives a minus sign for each of its bytes, as a character the
// font lacks does (PortCeExtrasInFont). On Windows the name is in the ANSI page and each byte is its own character. Characters above
// 65535 are shown as a minus sign. A host test runs this function (port/tools/ce_jukebox_tests.py).
#ifndef CE_NAME_TEXT_H
#define CE_NAME_TEXT_H

// Fills out (n characters, the last for the end mark) from the bytes of in; returns the characters written.
template <class W>
inline int	CeDecodeName(W *out, int n, const char *in, bool utf8)
{
	int at = 0;
	while (*in && (at < n - 1))
	{
		const unsigned char c = (unsigned char)*in;
		int extra = 0;
		unsigned code = c;
		if (utf8 && (c >= 0xC2) && (c <= 0xF4))
		{
			extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : 1;
			code = c & (0x3F >> extra);
			int i = 1;
			for (; i <= extra; i++)
			{
				const unsigned char next = (unsigned char)in[i];		// the end mark is not a continuation byte, so it stops the loop
				if ((next & 0xC0) != 0x80)
					break;
				code = (code << 6) | (next & 0x3F);
			}
			const bool whole = (i > extra);
			const bool shortest = (extra == 1) || ((extra == 2) && (code >= 0x800)) || ((extra == 3) && (code >= 0x10000));
			if (!whole || !shortest || (code > 0x10FFFF) || ((code >= 0xD800) && (code <= 0xDFFF)))
			{
				out[at++] = '-';
				in++;											// one byte at a time
				continue;
			}
		}
		else if (utf8 && (c >= 0x80))
		{
			out[at++] = '-';
			in++;
			continue;
		}
		out[at++] = (code > 0xFFFF) ? W('-') : W(code);
		in += 1 + extra;
	}
	out[at] = 0;
	return at;
}

// Where to break a sentence of one paragraph in two: the space that leaves the wider of the two lines as narrow as it can be, by the
// font's own measure (measure(first, count) gives the width of count characters from first). Returns the index of that space, or -1 when
// the text has none; the wider line's width is left in *widest. Balanced by what the font measures, not by a count of characters: a
// line of 48 narrow characters and one of 48 wide ones are not the same width, and a half wider than the hint area would be wrapped again.
template <class C, class Measure>
inline int	CeBreakAt(const C *text, Measure measure, float *widest)
{
	int length = 0;
	while (text[length])
		length++;
	int best = -1;
	float bestWidth = 0.f;
	for (int i = 1; i < length - 1; i++)
	{
		if (text[i] != ' ')
			continue;
		const float a = measure(text, i), b = measure(text + i + 1, length - i - 1);
		const float wider = (a > b) ? a : b;
		if ((best < 0) || (wider < bestWidth))
		{
			best = i;
			bestWidth = wider;
		}
	}
	if (widest)
		*widest = bestWidth;
	return best;
}

// The Goodie a rule says to unlock first. A rule that begins "Unlock Biography 3, then ..." names that Goodie by its kind and number, as the
// Goodies wall counts them; a player whose list shows its title ("Col. Chuck Kramer") cannot find "Biography 3". Returns the slot of that
// Goodie and, in *length, the length of the words "Biography 3" at the rule's seventh character; -1 when the rule starts otherwise.
struct SCeKindStart
{
	int			mFirst;
	const char	*mName;
};
static const SCeKindStart	kCeKindStarts[] = {{0, "Biography"}, {8, "Unit"}, {66, "Race Level"}, {74, "Extra"}, {78, "Concept Art"}, {201, "Cutscene"}};

inline int	CeUnlockedFirst(const char *rule, int *length)
{
	const char *lead = "Unlock ";
	int at = 0;
	while (lead[at])
	{
		if (rule[at] != lead[at])
			return -1;
		at++;
	}
	for (unsigned k = 0; k < sizeof(kCeKindStarts) / sizeof(kCeKindStarts[0]); k++)
	{
		int n = 0;
		while (kCeKindStarts[k].mName[n] && (rule[at + n] == kCeKindStarts[k].mName[n]))
			n++;
		if (kCeKindStarts[k].mName[n] || (rule[at + n] != ' '))
			continue;
		int digits = 0, number = 0;
		while ((rule[at + n + 1 + digits] >= '0') && (rule[at + n + 1 + digits] <= '9') && (digits < 4))
			number = number * 10 + (rule[at + n + 1 + digits++] - '0');
		if (!digits || (number < 1))
			return -1;
		*length = n + 1 + digits;
		return kCeKindStarts[k].mFirst + number - 1;
	}
	return -1;
}

#endif
