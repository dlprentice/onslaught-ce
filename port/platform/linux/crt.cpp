// SPDX-License-Identifier: GPL-3.0-only
// port/platform/linux/crt.cpp: the MSVC runtime functions glibc lacks or defines differently, for the game's Linux
// build: wide strings with a 2-byte wchar_t (the game builds with -fshort-wchar, glibc's are 4-byte), strlwr/strupr,
// _spawnl, _controlfp over the x87 control word. Platform code is built without -fshort-wchar, so wide strings are
// handled here as 16-bit units.
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../common/portlog.h"

typedef uint16_t wc16;

extern "C" {

size_t port_wcslen(const wc16 *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	return n;
}

wc16 *port_wcscpy(wc16 *d, const wc16 *s)
{
	wc16 *r = d;
	while ((*d++ = *s++))
	{
	}
	return r;
}

wc16 *port_wcsncpy(wc16 *d, const wc16 *s, size_t n)
{
	size_t i = 0;
	for (; i < n && s[i]; i++)
		d[i] = s[i];
	for (; i < n; i++)
		d[i] = 0;
	return d;
}

wc16 *port_wcscat(wc16 *d, const wc16 *s)
{
	port_wcscpy(d + port_wcslen(d), s);
	return d;
}

int port_wcscmp(const wc16 *a, const wc16 *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (int)*a - (int)*b;
}

static wc16 Lower16(wc16 c)
{
	// MSVC's _wcsicmp folds through the C locale: ASCII and Latin-1 letters.
	if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
		return c + 0x20;
	return c;
}

int port_wcsicmp(const wc16 *a, const wc16 *b)
{
	while (*a && Lower16(*a) == Lower16(*b))
		a++, b++;
	return (int)Lower16(*a) - (int)Lower16(*b);
}

wc16 *port_wcsstr(const wc16 *h, const wc16 *n)
{
	size_t nl = port_wcslen(n);
	if (!nl)
		return (wc16 *)h;
	for (; *h; h++)
		if (memcmp(h, n, nl * sizeof(wc16)) == 0)
			return (wc16 *)h;
	return NULL;
}

// MSVC's legacy swprintf (no count): the format is wide; the game's two uses take %d and %ld. A %s argument is a wide
// string in MSVC's swprintf, %S a narrow one.
int port_swprintf(wc16 *d, const wc16 *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	size_t out = 0;
	for (const wc16 *f = fmt; *f; f++)
	{
		if (*f != '%')
		{
			d[out++] = *f;
			continue;
		}
		char spec[32];
		size_t sl = 0;
		spec[sl++] = '%';
		f++;
		while (*f && strchr("-+ #0123456789.lhL", (char)*f) && sl < sizeof(spec) - 2)
			spec[sl++] = (char)*f++;
		wc16 conv = *f;
		if (!conv)
			break;
		char buf[512];
		if (conv == '%')
		{
			d[out++] = '%';
			continue;
		}
		if (conv == 's')
		{
			const wc16 *s = va_arg(ap, const wc16 *);
			while (s && *s)
				d[out++] = *s++;
			continue;
		}
		if (conv == 'S')
		{
			const char *s = va_arg(ap, const char *);
			while (s && *s)
				d[out++] = (unsigned char)*s++;
			continue;
		}
		spec[sl++] = (char)conv;
		spec[sl] = 0;
		if (strchr("eEfgG", (char)conv))
			snprintf(buf, sizeof(buf), spec, va_arg(ap, double));
		else if (conv == 'c')
		{
			buf[0] = (char)va_arg(ap, int);
			buf[1] = 0;
		}
		else
			snprintf(buf, sizeof(buf), spec, va_arg(ap, long));
		for (char *p = buf; *p; p++)
			d[out++] = (unsigned char)*p;
	}
	d[out] = 0;
	va_end(ap);
	return (int)out;
}

char *strlwr(char *s)
{
	for (char *p = s; *p; p++)
		*p = (char)tolower((unsigned char)*p);
	return s;
}

char *strupr(char *s)
{
	for (char *p = s; *p; p++)
		*p = (char)toupper((unsigned char)*p);
	return s;
}

// console.cpp launches the retail "message" helper to show a fatal error; the port prints the text instead.
intptr_t _spawnl(int, const char *cmd, const char *arg0, ...)
{
	va_list ap;
	va_start(ap, arg0);
	fprintf(stderr, PORT_LOG_PREFIX "%s", cmd);
	if (arg0)
		fprintf(stderr, " %s", arg0);
	for (const char *a = va_arg(ap, const char *); a; a = va_arg(ap, const char *))
		fprintf(stderr, " %s", a);
	fprintf(stderr, "\n");
	va_end(ap);
	return 0;
}

// MSVC's _controlfp on the x87 control word (exception masks, rounding, precision). Only the x87 unit is
// touched; the game's maths is x87 (port/README.md, compile contract). MSVC's bit values differ from the hardware's.
unsigned int _controlfp(unsigned int newval, unsigned int mask)
{
	unsigned short cw;
	__asm__ volatile("fnstcw %0" : "=m"(cw));
	// hardware -> MSVC
	unsigned int cur = 0;
	if (cw & 0x01) cur |= 0x10;			// invalid
	if (cw & 0x02) cur |= 0x80000;		// denormal
	if (cw & 0x04) cur |= 0x08;			// zero divide
	if (cw & 0x08) cur |= 0x04;			// overflow
	if (cw & 0x10) cur |= 0x02;			// underflow
	if (cw & 0x20) cur |= 0x01;			// inexact
	switch ((cw >> 10) & 3)
	{
	case 1: cur |= 0x100; break;		// down
	case 2: cur |= 0x200; break;		// up
	case 3: cur |= 0x300; break;		// chop
	}
	switch ((cw >> 8) & 3)
	{
	case 0: cur |= 0x20000; break;		// 24-bit
	case 2: cur |= 0x10000; break;		// 53-bit
	}
	unsigned int v = (cur & ~mask) | (newval & mask);
	// MSVC -> hardware
	unsigned short ncw = cw & ~0x0F3F;
	if (v & 0x10) ncw |= 0x01;
	if (v & 0x80000) ncw |= 0x02;
	if (v & 0x08) ncw |= 0x04;
	if (v & 0x04) ncw |= 0x08;
	if (v & 0x02) ncw |= 0x10;
	if (v & 0x01) ncw |= 0x20;
	ncw |= (unsigned short)(((v >> 8) & 3) << 10);
	unsigned int pc = (v >> 16) & 3;
	ncw |= (unsigned short)((pc == 2 ? 0 : pc == 1 ? 2 : 3) << 8);
	__asm__ volatile("fldcw %0" : : "m"(ncw));
	return v;
}

}
