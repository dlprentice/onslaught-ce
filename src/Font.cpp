// Font.cpp: the bitmap font's text helpers and character remapping (CDXBitmapFont). Not in Stuart's release; reconstructed
// from the retail code (00465710..00465e00), an object of its own between fmv.obj and FrontEnd.obj. The file name is
// ours: nothing in the retail executable names it.

#include	"Common.h"
#include	"Platform.h"

// The characters of a font texture, in cell order (005db5fc; name open): printable ASCII, then the accented letters,
// marks and box corners the languages need; a zero ends the list (two more cells follow it).
extern const WCHAR	FONT_CHARACTERS[158] = {
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b,
	0x002c, 0x002d, 0x002e, 0x002f, 0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
	0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f, 0x0040, 0x0041, 0x0042, 0x0043,
	0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005a, 0x005b,
	0x005c, 0x005d, 0x005e, 0x005f, 0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
	0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f, 0x0070, 0x0071, 0x0072, 0x0073,
	0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x007f,
	0x00e1, 0x00e0, 0x00e2, 0x00e4, 0x00e7, 0x00e9, 0x00e8, 0x00ea, 0x00eb, 0x00ed, 0x00ec, 0x00ee,
	0x00ef, 0x00f3, 0x00f2, 0x00f4, 0x00f6, 0x0153, 0x00f1, 0x00fa, 0x00f9, 0x00fb, 0x00fc, 0x00c1,
	0x00c0, 0x00c4, 0x00c2, 0x00c7, 0x00c9, 0x00c8, 0x00cd, 0x00cc, 0x00d1, 0x00d3, 0x00d2, 0x00d6,
	0x00dc, 0x00da, 0x00d9, 0x0152, 0x00a1, 0x00bf, 0x00a9, 0x00ae, 0x2122, 0x00ab, 0x00bb, 0x00ba,
	0x00aa, 0x001f, 0x00df, 0x00ca, 0x00cb, 0x00ce, 0x00cf, 0x00d4, 0x00db, 0x2510, 0x250c, 0x0000,
	0x250c, 0x2510,
};
// Cells no character maps to (005db738; name ours): "$@[\]^_{|}~" and 0x7f.
extern const WCHAR	FONT_UNMAPPED_CHARACTERS[14] = {
	0x0024, 0x0040, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f, 0x007b, 0x007c, 0x007d, 0x007e, 0x007f,
	0x0000, 0x0000,
};

// A character above 0xff and its cell (names ours)
struct SWideCharMap
{
	WCHAR	mChar;
	UBYTE	mCell;
};

static UBYTE		sUnknownChar;				// 00679af4: the cell of character 0x1f, drawn for characters the font lacks
static UBYTE		sCharMap[0x100];			// 006799f4: each character below 0x100's cell
static SWideCharMap	sWideChars[8];				// 006799d4

//*********************************************************************************
// A fading reveal: each character starts at reveal and each next one 0.01 behind it. A character fades in from
// transparent white over 0.25, turns to the colour over the next 0.25, and with fade_out set fades out between 0.5 and 15.
void	CDXBitmapFont::DrawTextDynamic(float x, float y, float z, float scalex, float scaley, DWORD colour, WCHAR *text, float reveal, int fade_out, int flags)	// FUNCTION: 0x00465710
{
	int		len = 0;
	WCHAR	*s;
	s = text;
	while (*s ++)
		len ++;

	int		alpha = colour >> 24;
	WCHAR	buf[1000];
	WCHAR	*d = buf;
	s = text;
	while (*s)
		*d ++ = *s ++;
	*d = 0;

	if (len > 1000)
		len = 1000;

	DWORD	any = 0;
	DWORD	shadows[1000];
	DWORD	colours[1000];
	for (int i = 0; i < len; i ++)
	{
		if (reveal < 0.f)
		{
			buf[i] = 0;
			break;
		}

		DWORD	col;
		if (reveal < 0.25f)
		{
			// a separate value: VC6 would fold the shifts of one expression into an and (004657cd)
			DWORD	a = (DWORD((reveal * 4.f) * 255.f) * alpha) >> 8;
			col = (a << 24) + 0xffffff;
		}
		else if (reveal < 0.5f)
		{
			float	f = (reveal - 0.25f) * 4.f;
			float	w = (1.f - f) * 255.f;
			col = alpha;
			col = (col << 8) + SINT(int((colour >> 16) & 0xff) * f + w);
			col = (col << 8) + SINT(int((colour >> 8) & 0xff) * f + w);
			col = (col << 8) + SINT(int(colour & 0xff) * f + w);
		}
		else if (fade_out)
		{
			if (reveal < 15.f)
				col = (SINT((1.f - (reveal - 0.5f) * (1.f / 14.5f)) * alpha) << 24) + (colour & 0xffffff);
			else
				col = 0;
		}
		else
			col = colour;

		colours[i] = col;
		shadows[i] = (alpha << 24) & col;
		any |= col;
		reveal -= 0.01f;
	}

	if (any)
	{
		DrawTextScaled(x + 1.f, y + 1.f, z, scalex, scaley, colour, buf, flags, shadows);
		DrawTextScaled(x, y, z, scalex, scaley, colour, buf, flags, colours);
	}
}

//*********************************************************************************
void	CDXBitmapFont::DrawTextWithShadow(float x, float y, DWORD colour, WCHAR *text, int flags, float z, float scalex, float scaley)	// FUNCTION: 0x004659a0
{
	DrawTextScaled(x + 1.f, y + 1.f, z, scalex, scaley, colour & 0xff000000, text, flags, NULL);
	DrawTextScaled(x, y, z, scalex, scaley, colour, text, flags, NULL);
}

//*********************************************************************************
int		CDXBitmapFont::WordWrap(SWordWrapFill *fill, WCHAR *text, float width)	// FUNCTION: 0x00465a20
{
	int		len = wcslen(text);
	int		pos = 0;
	int		col = 0;
	int		line = 0;

	for (int l = 0; l < 40; l ++)
		fill->c[l][0] = 0;

	WCHAR	punct[5];
	wcscpy(punct, ToWCHAR("!?;:"));

	SIZE	size;
	WCHAR	tmp[200];
	for (; pos < len; pos ++)
	{
		// the word starting here, with a space before punctuation kept in it
		WCHAR	*word = &text[pos];
		int		w = 0;
		while (1)
		{
			if (pos + w == len)
				break;
			if (word[w] == ' ')
			{
				if (pos + w + 1 < len)
				{
					WCHAR	c = text[pos + w + 1];
					if ((c != punct[0]) && (c != punct[1]) && (c != punct[2]) && (c != punct[3]))
						break;
				}
			}
			if (w > 98)
				break;
			w ++;
		}

		wcsncpy(tmp, fill->c[line], col);
		wcsncpy(&tmp[col], word, w);
		tmp[col + w] = 0;
		GetTextExtent(tmp, &size);
		if (size.cx >= width)
		{
			while ((col) && (fill->c[line][col - 1] == ' '))
				col --;
			fill->c[line][col] = 0;
			col = 0;
			line ++;
			if (line >= 40)
				line --;
		}

		WCHAR	ch = *word;
		if ((ch != ' ') || (col))
			fill->c[line][col ++] = ch;
	}

	fill->c[line][col] = 0;
	return line + 1;
}

//*********************************************************************************
void	CDXBitmapFont::InitCharMap()	// FUNCTION: 0x00465c10
{
	UBYTE	i;
	for (i = 0; FONT_CHARACTERS[i]; i ++)
		if (FONT_CHARACTERS[i] == 0x1f)
		{
			sUnknownChar = i;
			break;
		}

	memset(sCharMap, sUnknownChar, sizeof(sCharMap));
	memset(sWideChars, 0, sizeof(sWideChars));

	int		n = 0;
	for (i = 0; FONT_CHARACTERS[i]; i ++)
	{
		WCHAR	c = FONT_CHARACTERS[i];
		BOOL	unmapped = FALSE;
		for (const WCHAR *u = FONT_UNMAPPED_CHARACTERS; *u; u ++)
			if (*u == c)
			{
				unmapped = TRUE;
				break;
			}
		if (unmapped)
			continue;

		if (c >= 0x100)
		{
			sWideChars[n].mChar = c;
			sWideChars[n].mCell = i;
			n ++;
		}
		else
			sCharMap[c] = i;
	}
}

//*********************************************************************************
int		CDXBitmapFont::RemapChar(WCHAR c)	// FUNCTION: 0x00465cf0
{
	if (mCharSwapHack)
	{
		if (c == 0xbf)
			c = 0xa1;
		else if (c == 0xa1)
			c = 0xbf;
	}

	if (mUnknown16c)
	{
		if (c == '[')
			c = '^';
		else if (c == '^')
			c = '[';
	}

	if (c < 0x100)
		return sCharMap[c];

	for (SWideCharMap *wide = sWideChars; wide->mChar; wide ++)
		if (wide->mChar == c)
			return wide->mCell;

	if (c == 0x2019)
		return RemapChar('\'');
	if (c == 0x2013)
		return RemapChar('-');
	return sUnknownChar;
}

//*********************************************************************************
BOOL	CDXBitmapFont::CanDisplay(WCHAR c)	// FUNCTION: 0x00465dd0
{
	if (mSysFont)
		return TRUE;
	return RemapChar(c) != sUnknownChar;
}
