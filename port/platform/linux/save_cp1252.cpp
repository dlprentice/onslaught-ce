// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/save_cp1252.cpp (patch 0101): career names between Linux's UTF-8 file names and the game's
// one-byte characters. The original read file names through Windows' ANSI functions, which hand a Western European
// system's names over in code page 1252, and the game keeps each byte as one character (UMTexture.cpp ToWCHAR/
// FromWCHAR); IsCheatActive compares those bytes with its codes (latête is the byte 0xEA). Linux file names are UTF-8
// bytes, so a career named "latête" on Linux reached the game as "latÃªte" and its cheat never matched. These turn a
// UTF-8 name of code page 1252 characters into those characters (as the virtual keyboard types them and the Windows
// build keeps them), and a career name into its UTF-8 file name, never narrowing a character to a byte another one
// shares. Files that are not UTF-8, are plain ASCII, or hold characters code page 1252 lacks keep the game's own
// byte-for-byte name.
#include <stdint.h>
#include <string.h>

namespace
{
// Code page 1252's 0x80-0x9F as Windows maps them (the five it leaves undefined round-trip as the C1 controls).
const uint16_t kHigh[32] = {
	0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
	0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
};

// The code page 1252 byte for a code point, or -1 when it has none.
int ToByte(uint32_t c)
{
	if (c < 0x80 || (c >= 0xa0 && c <= 0xff))
		return (int)c;
	for (int i = 0; i < 32; ++i)
		if (kHigh[i] == c)
			return 0x80 + i;
	return -1;
}

uint32_t FromByte(unsigned b) { return (b >= 0x80 && b < 0xa0) ? kHigh[b - 0x80] : b; }

// One code point from strict UTF-8 (no overlong forms, no surrogates, at most U+10FFFF); 0 bytes when invalid.
unsigned Decode(const unsigned char *s, uint32_t *c)
{
	if (s[0] < 0x80) { *c = s[0]; return 1; }
	unsigned n = (s[0] & 0xe0) == 0xc0 ? 2 : (s[0] & 0xf0) == 0xe0 ? 3 : (s[0] & 0xf8) == 0xf0 ? 4 : 0;
	if (!n) return 0;
	uint32_t v = s[0] & (0x7f >> n);
	for (unsigned i = 1; i < n; ++i)
	{
		if ((s[i] & 0xc0) != 0x80) return 0;	// also stops at the terminator
		v = (v << 6) | (s[i] & 0x3f);
	}
	static const uint32_t least[5] = {0, 0, 0x80, 0x800, 0x10000};
	if (v < least[n] || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return 0;
	*c = v;
	return n;
}
}

// A file name (without its extension) as the career name the game shows: the characters of a UTF-8 name that has at least
// one non-ASCII character and only characters code page 1252 has, as Unicode code points in out (16-bit units,
// terminated), the form the virtual keyboard types (its trademark key is U+2122) and the Windows build keeps. Returns the
// number of characters, or 0 when the game should keep its byte-for-byte name (or out is too small).
extern "C" unsigned PortSaveNameFromFileCp1252(const char *file, void *out, unsigned capacity)
{
	if (!file || !out || !capacity) return 0;
	const unsigned char *s = (const unsigned char *)file;
	unsigned n = 0;
	bool wide = false;
	uint16_t unit;
	while (*s)
	{
		uint32_t c;
		unsigned used = Decode(s, &c);
		int b = used ? ToByte(c) : -1;
		if (b < 0 || n + 1 >= capacity) return 0;
		wide = wide || c >= 0x80;
		unit = (uint16_t)c;
		memcpy((char *)out + 2 * n++, &unit, 2);
		s += used;
	}
	if (!wide) return 0;
	unit = 0;
	memcpy((char *)out + 2 * n, &unit, 2);
	return n;
}

// The UTF-8 file name (without its extension) for a career name with at least one character above 0x7f, in out. A unit up
// to 0xff is a code page 1252 byte (the game's own one-byte names: 0x99 is the trademark sign); a unit above 0xff is
// the character itself (the keyboard's U+2122, U+0153), written as its own UTF-8, never narrowed to one byte (0.1.8
// narrowed U+2122 to '"', so "A(TM)" and "A\"" shared a file). Returns 1 when written, 0 when the game's byte-for-byte name
// stands (ASCII only, or no room).
extern "C" int PortSaveFileFromNameCp1252(const void *name, char *out, unsigned capacity)
{
	if (!name || !out || !capacity) return 0;
	unsigned n = 0;
	bool wide = false;
	for (unsigned i = 0;; ++i)
	{
		uint16_t unit;
		memcpy(&unit, (const char *)name + 2 * i, 2);
		if (!unit) break;
		uint32_t c = unit > 0xff ? unit : FromByte(unit);
		wide = wide || unit >= 0x80;
		unsigned char bytes[3];
		unsigned k;
		if (c < 0x80) { bytes[0] = (unsigned char)c; k = 1; }
		else if (c < 0x800) { bytes[0] = (unsigned char)(0xc0 | (c >> 6)); bytes[1] = (unsigned char)(0x80 | (c & 0x3f)); k = 2; }
		else { bytes[0] = (unsigned char)(0xe0 | (c >> 12)); bytes[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3f));
			   bytes[2] = (unsigned char)(0x80 | (c & 0x3f)); k = 3; }
		if (n + k >= capacity) return 0;
		memcpy(out + n, bytes, k);
		n += k;
	}
	if (!wide) return 0;
	out[n] = 0;
	return 1;
}

// The career name as the code page 1252 bytes Windows would have used for its file (the name an older Linux build
// wrote byte for byte, when the name is one-byte): units up to 0xff as themselves, U+2122 and the other characters
// of code page 1252's 0x80-0x9f block as their bytes. Returns 1 when written, 0 when a character has no code page 1252
// byte (such a name has no older file) or there is no room.
extern "C" int PortSaveLegacyFromNameCp1252(const void *name, char *out, unsigned capacity)
{
	if (!name || !out || !capacity) return 0;
	unsigned n = 0;
	for (unsigned i = 0;; ++i)
	{
		uint16_t unit;
		memcpy(&unit, (const char *)name + 2 * i, 2);
		if (!unit) break;
		int b = unit > 0xff ? ToByte(unit) : unit;
		if (b < 0 || n + 1 >= capacity) return 0;
		out[n++] = (char)b;
	}
	out[n] = 0;
	return 1;
}

// A one-byte file name (not UTF-8: an older Linux save, or one copied from Windows) as the career name: each byte as its
// code page 1252 character, as the Windows build lists it (0x99 is U+2122), so a name typed with the keyboard matches the
// file it is saved in. Returns the number of characters, or 0 when out is too small.
extern "C" unsigned PortSaveNameFromLegacyCp1252(const char *file, void *out, unsigned capacity)
{
	if (!file || !out || !capacity) return 0;
	unsigned n = 0;
	for (const unsigned char *s = (const unsigned char *)file; *s; ++s)
	{
		if (n + 1 >= capacity) return 0;
		uint16_t unit = (uint16_t)FromByte(*s);
		memcpy((char *)out + 2 * n++, &unit, 2);
	}
	uint16_t zero = 0;
	memcpy((char *)out + 2 * n, &zero, 2);
	return n;
}
