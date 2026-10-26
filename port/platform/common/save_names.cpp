// SPDX-License-Identifier: GPL-3.0-only
// Strict savecard-only UTF-16/UTF-8 conversion. No locale, replacement characters, normalization or file operations.
// Patch 0066 admits this on Windows. Linux retains its existing raw-byte save namespace until migration is defined.
#include "save_names.h"
#include <stdint.h>
#include <string.h>

namespace
{
const unsigned InvalidName = 123, TooLong = 206, InvalidUnicode = 1113, MaxUnits = 31;

uint16_t Unit(const void *text, unsigned at)
{
	uint16_t value;
	memcpy(&value, (const char *)text + at * 2, 2);
	return value;
}

void PutUnit(void *text, unsigned at, uint16_t value) { memcpy((char *)text + at * 2, &value, 2); }

unsigned NameError(const uint16_t *name, unsigned length)
{
	if (!length) return InvalidName;
	for (unsigned i = 0; i < length; i++)
	{
		uint16_t c = name[i];
		if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
			return InvalidName;
	}
	// A DOS device name remains a device when an extension is appended. Do not send a save write to it.
	char head[MaxUnits + 1] = {};
	unsigned n = 0;
	while (n < length && n < sizeof(head) - 1 && name[n] != '.')
	{
		uint16_t c = name[n];
		head[n++] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c < 128 ? (char)c : '!';
	}
	while (n && head[n - 1] == ' ') head[--n] = 0;
	if (!strcmp(head, "CON") || !strcmp(head, "PRN") || !strcmp(head, "AUX") || !strcmp(head, "NUL") ||
		!strcmp(head, "CONIN$") || !strcmp(head, "CONOUT$")) return InvalidName;
	if (n == 4 && (!strncmp(head, "COM", 3) || !strncmp(head, "LPT", 3)) &&
		((name[3] >= '1' && name[3] <= '9') || name[3] == 0xb9 || name[3] == 0xb2 || name[3] == 0xb3))
		return InvalidName;
	return 0;
}

unsigned ReadName(const void *input, uint16_t (&name)[MaxUnits + 1], unsigned &length)
{
	if (!input) return InvalidName;
	for (length = 0; length <= MaxUnits; length++)
	{
		uint16_t c = Unit(input, length);
		if (!c) { name[length] = 0; return NameError(name, length); }
		if (length == MaxUnits) return TooLong;
		name[length] = c;
		if (c >= 0xd800 && c <= 0xdbff)
		{
			uint16_t next = Unit(input, length + 1);
			if (next < 0xdc00 || next > 0xdfff) return InvalidUnicode;
			if (length + 1 >= MaxUnits) return TooLong;
			name[++length] = next;
		}
		else if (c >= 0xdc00 && c <= 0xdfff) return InvalidUnicode;
	}
	return TooLong;
}
}

extern "C" unsigned PortSavePathUtf8(const void *input, char *path, unsigned capacity)
{
	if (path && capacity) path[0] = 0;
	if (!path || !capacity) return TooLong;
	uint16_t name[MaxUnits + 1];
	unsigned length;
	if (unsigned error = ReadName(input, name, length)) return error;
	char encoded[MaxUnits * 3 + 1];
	unsigned bytes = 0;
	for (unsigned i = 0; i < length; i++)
	{
		uint32_t c = name[i];
		if (c >= 0xd800 && c <= 0xdbff) { c = 0x10000 + ((c - 0xd800) << 10) + (name[i + 1] - 0xdc00); i++; }
		if (c < 0x80) encoded[bytes++] = (char)c;
		else if (c < 0x800) { encoded[bytes++] = (char)(0xc0 | (c >> 6)); encoded[bytes++] = (char)(0x80 | (c & 63)); }
		else if (c < 0x10000)
		{
			encoded[bytes++] = (char)(0xe0 | (c >> 12)); encoded[bytes++] = (char)(0x80 | ((c >> 6) & 63)); encoded[bytes++] = (char)(0x80 | (c & 63));
		}
		else
		{
			encoded[bytes++] = (char)(0xf0 | (c >> 18)); encoded[bytes++] = (char)(0x80 | ((c >> 12) & 63));
			encoded[bytes++] = (char)(0x80 | ((c >> 6) & 63)); encoded[bytes++] = (char)(0x80 | (c & 63));
		}
	}
	const char prefix[] = "savegames\\", suffix[] = ".bes";
	if (capacity < sizeof(prefix) - 1 + bytes + sizeof(suffix)) return TooLong;
	memcpy(path, prefix, sizeof(prefix) - 1);
	memcpy(path + sizeof(prefix) - 1, encoded, bytes);
	memcpy(path + sizeof(prefix) - 1 + bytes, suffix, sizeof(suffix));
	return 0;
}

extern "C" unsigned PortSaveNameUtf16(const char *filename, void *output, unsigned capacity)
{
	if (output && capacity) PutUnit(output, 0, 0);
	if (!filename || (!output && capacity)) return InvalidName;
	// Native enumeration's filename buffer is 260 bytes; the old caller copied it into 256. Stop before either can alias.
	unsigned bytes = 0;
	while (bytes < 256 && filename[bytes]) bytes++;
	if (bytes == 256) return TooLong;
	if (bytes <= 4 || filename[bytes - 4] != '.' || (filename[bytes - 3] | 32) != 'b' ||
		(filename[bytes - 2] | 32) != 'e' || (filename[bytes - 1] | 32) != 's') return InvalidName;
	bytes -= 4;
	uint16_t name[MaxUnits + 1];
	unsigned units = 0;
	for (unsigned i = 0; i < bytes;)
	{
		uint32_t c = (unsigned char)filename[i++], minimum = 0;
		unsigned extra = 0;
		if (c >= 0xc2 && c <= 0xdf) { c &= 31; extra = 1; minimum = 0x80; }
		else if (c >= 0xe0 && c <= 0xef) { c &= 15; extra = 2; minimum = 0x800; }
		else if (c >= 0xf0 && c <= 0xf4) { c &= 7; extra = 3; minimum = 0x10000; }
		else if (c >= 0x80) return InvalidUnicode;
		if (extra > bytes - i) return InvalidUnicode;
		while (extra--)
		{
			unsigned char next = (unsigned char)filename[i++];
			if ((next & 0xc0) != 0x80) return InvalidUnicode;
			c = (c << 6) | (next & 63);
		}
		if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return InvalidUnicode;
		unsigned needed = c >= 0x10000 ? 2 : 1;
		if (units + needed > MaxUnits) return TooLong;
		if (needed == 2) { c -= 0x10000; name[units++] = (uint16_t)(0xd800 | (c >> 10)); name[units++] = (uint16_t)(0xdc00 | (c & 1023)); }
		else name[units++] = (uint16_t)c;
	}
	name[units] = 0;
	if (unsigned error = NameError(name, units)) return error;
	if (output)
	{
		if (capacity <= units) return TooLong;
		memcpy(output, name, (units + 1) * 2);
	}
	return 0;
}
