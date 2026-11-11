// SPDX-License-Identifier: GPL-3.0-only
// Stand-in for the game's Platform.h for ce_menu_fit_tests.py: a font that measures nine units a character.
#ifndef MENU_FIT_PLATFORM_H
#define MENU_FIT_PLATFORM_H
#include "common.h"
enum EFontType { FONT_NORMAL, FONT_SMALL };
struct CBITMAPFONT
{
    bool fails = false;
    int GetTextExtent(WCHAR *text, SIZE *size)
    {
        if (fails) return 0;
        long n = 0;
        while (text[n]) ++n;
        size->cx = n * 9;
        size->cy = 20;
        return 1;
    }
};
struct StandInPlatform
{
    CBITMAPFONT font;
    CBITMAPFONT *Font(EFontType) { return &font; }
};
extern StandInPlatform PLATFORM;
#endif
