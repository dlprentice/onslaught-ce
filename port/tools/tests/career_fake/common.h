// SPDX-License-Identifier: GPL-3.0-only
// A stand-in for the game's common.h, only so that port/game/ce_career_view.cpp compiles in a host test (port/tools/ce_career_cross_tests.py).
#ifndef CE_FAKE_COMMON_H
#define CE_FAKE_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
typedef unsigned short WCHAR;
typedef int BOOL;
// the game's wide strings are two bytes a character, which the C library's are not
inline WCHAR *ce_fake_wcsncpy(WCHAR *out, const WCHAR *in, size_t n)
{
	size_t i = 0;
	for (; (i < n) && in[i]; i++)
		out[i] = in[i];
	for (; i < n; i++)
		out[i] = 0;
	return out;
}
#define wcsncpy ce_fake_wcsncpy
#endif
