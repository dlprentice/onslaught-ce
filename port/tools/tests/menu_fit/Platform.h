// SPDX-License-Identifier: GPL-3.0-only
// Stand-in for the game's Platform.h for ce_menu_fit_tests.py: a font that measures nine units a character, and a clock.
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
    float now = 10.f;							// the clock the list window reads (seconds), set by the driver
    CBITMAPFONT *Font(EFontType) { return &font; }
    float GetSysTimeFloat() { return now; }
};
extern StandInPlatform PLATFORM;
#endif
