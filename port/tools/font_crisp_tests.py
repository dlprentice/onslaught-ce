#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The sharper copy of a bitmap font's texture (port/platform/public/DXFont.cpp: CrispUpsample, CrispWanted) and the switch
that turns it on (ce.ini sharp_ui=1, off by default: port/platform/common/widescreen.cpp PortSharpUi).

When the player asks for it, on a wide screen at two or more times a font's texture size, text is drawn from a copy of
the font texture four times as large: each character cell rebuilt with a cubic interpolation of its alpha, the edge steepened
around half coverage, the colour interpolated weighted by the alpha. The production text of the weights, the steepening, the
upsampling and the gate is cut out of DXFont.cpp and run on authored textures:
- a solid cell stays solid (alpha 255, its colour), an empty one stays empty; a stroke keeps a solid core and clear sides;
- a character never leaks into the cell beside it, the last cells of a texture whose size is not a multiple of the cell are cut
  short without a read or a write outside the buffers, and the pitches (source and destination) are the ones given;
- a coloured stroke keeps its colour right up to its edge, and the texels beside it carry it too (no dark fringe when the
  sampler blends across an edge);
- the gate: bitmap fonts only, wide screens only, text of twice the size or more, and only with sharp_ui=1, asked last;
- nothing is lost by skipping the blank parts of a font (rows, taps and texels with no alpha, which are most of one): the bytes
  equal those of an upsampling that works out every texel, on font-like textures, single faint texels at the corners and edges of a
  cell, rows and columns of ink, and a texture with no alpha at all.
DXFont.cpp is compiled with /QIfist (a float becomes an integer by rounding: tools/qifist.py, port/build.py), where the usual
(int)(v + 0.5f) turns full coverage into 256 and the pixel into clear: the first version of this code drew hollow letters. The
checks therefore run twice, plain (with the address and undefined-behaviour sanitizers) and through the same /QIfist rewrite
that builds the game, with the pinned LLVM. Causal mutants of each of these must be caught, the rounding one only by the second.

The switch runs end to end: the production PortSharpUi and CrispWanted with ce.cpp's ce.ini reader (CE_SettingGetBool and the
settings-folder code it uses, cut out of ce.cpp and port/updater/ce_report.h) on real files in a private folder. No ce.ini, no
sharp_ui line, sharp_ui=0, sharp_ui=1 and values that are not exactly 0 or 1 each give the right answer (only 1 turns it on);
nothing the other terms refuse reads the file, a back buffer that does not take the wide paths never does, it is read once and
the session log says the answer once. Causal mutants of the switch and of the gate's use of it must be caught.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import deps  # noqa: E402
from ce_palette_tests import no_core, SANITIZE  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "port/platform/public/DXFont.cpp"
QIFIST = ROOT / "port/tools/qifist.py"
WIDESCREEN = ROOT / "port/platform/common/widescreen.cpp"
CE = ROOT / "port/platform/common/ce.cpp"
REPORT = ROOT / "port/updater/ce_report.h"
PORTLOG = ROOT / "port/platform/common/portlog.h"
GATE = "return !sysFont && wide && scale >= CRISP_MIN_SCALE && PortSharpUi();"

HARNESS = r"""
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
typedef unsigned int DWORD;
typedef unsigned char UBYTE;
// PortSharpUi (widescreen.cpp): ce.ini's sharp_ui as the gate gets it; the reads are counted (the end-to-end test below runs
// the real one on real files).
static int sharpAnswer = 1, sharpReads = 0;
int PortSharpUi() { ++sharpReads; return sharpAnswer; }
@BLOCK@

static void check(bool ok, const char *what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(2); } }
static DWORD px(int a, int r, int g, int b) { return ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b; }

// A texture of w x h pixels in a buffer with the given pitch (bytes), guard bytes after it, filled by f(x, y).
struct Tex
{
    int w, h, pitch;
    std::vector<UBYTE> bytes;
    Tex(int w_, int h_, int pitch_) : w(w_), h(h_), pitch(pitch_), bytes((size_t)pitch_ * h_ + 64, 0xA5) {}
    DWORD &at(int x, int y) { return *(DWORD *)&bytes[(size_t)y * pitch + (size_t)x * 4]; }
    void clear(DWORD v) { for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) at(x, y) = v; }
    bool guard() const { for (size_t i = (size_t)pitch * h; i < bytes.size(); i++) if (bytes[i] != 0xA5) return false; return true; }
};
static Tex up(Tex &src, int cell, int dstPitchExtra = 0)
{
    Tex dst(src.w * CRISP_SCALE, src.h * CRISP_SCALE, src.w * CRISP_SCALE * 4 + dstPitchExtra);
    // pad bytes between rows are 0xA5 too: they must still be after the call
    CrispUpsample(src.bytes.data(), src.pitch, src.w, src.h, cell, dst.bytes.data(), dst.pitch);
    return dst;
}
// The same upsampling with nothing skipped: every row, tap and texel worked out. The production function must give these bytes.
static void CrispUpsampleReference(const void *src, int srcPitch, int w, int h, int cell, void *dst, int dstPitch)
{
    const int cols = cell * CRISP_SCALE;
    float *wide = new float[4 * cell * cols];
    for (int cy = 0; cy < h; cy += cell)
        for (int cx = 0; cx < w; cx += cell)
        {
            const int cw = cx + cell > w ? w - cx : cell;
            const int ch = cy + cell > h ? h - cy : cell;
            for (int y = 0; y < ch; y++)
            {
                const DWORD *row = (const DWORD *)((const UBYTE *)src + (cy + y) * srcPitch) + cx;
                for (int o = 0; o < cw * CRISP_SCALE; o++)
                {
                    int first; float wt[4];
                    CrispWeights(o, &first, wt);
                    float sum[4] = {0.f, 0.f, 0.f, 0.f};
                    for (int k = 0; k < 4; k++)
                    {
                        int i = first + k;
                        i = i < 0 ? 0 : i >= cw ? cw - 1 : i;
                        DWORD p = row[i];
                        float a = (float)(p >> 24) * (1.f / 255.f);
                        sum[0] += wt[k] * a;
                        sum[1] += wt[k] * a * (float)((p >> 16) & 0xff);
                        sum[2] += wt[k] * a * (float)((p >> 8) & 0xff);
                        sum[3] += wt[k] * a * (float)(p & 0xff);
                    }
                    for (int c = 0; c < 4; c++) wide[(c * cell + y) * cols + o] = sum[c];
                }
            }
            for (int o = 0; o < ch * CRISP_SCALE; o++)
            {
                int first; float wt[4];
                CrispWeights(o, &first, wt);
                DWORD *dest = (DWORD *)((UBYTE *)dst + (cy * CRISP_SCALE + o) * dstPitch) + cx * CRISP_SCALE;
                for (int x = 0; x < cw * CRISP_SCALE; x++)
                {
                    float sum[4] = {0.f, 0.f, 0.f, 0.f};
                    for (int k = 0; k < 4; k++)
                    {
                        int i = first + k;
                        i = i < 0 ? 0 : i >= ch ? ch - 1 : i;
                        for (int c = 0; c < 4; c++) sum[c] += wt[k] * wide[(c * cell + i) * cols + x];
                    }
                    DWORD rgb;
                    if (sum[0] > 0.02f)
                    {
                        float inv = 1.f / sum[0];
                        int rgbc[3];
                        for (int c = 0; c < 3; c++)
                        {
                            int v = (int)floorf(sum[1 + c] * inv + 0.5f);
                            rgbc[c] = v < 0 ? 0 : v > 255 ? 255 : v;
                        }
                        rgb = ((DWORD)rgbc[0] << 16) | ((DWORD)rgbc[1] << 8) | (DWORD)rgbc[2];
                    }
                    else
                    {
                        int sx = x / CRISP_SCALE, sy = o / CRISP_SCALE;
                        DWORD best = 0;
                        int bestAlpha = -1;
                        for (int dy = -1; dy <= 1; dy++)
                            for (int dx = -1; dx <= 1; dx++)
                            {
                                int px = sx + dx, py = sy + dy;
                                px = px < 0 ? 0 : px >= cw ? cw - 1 : px;
                                py = py < 0 ? 0 : py >= ch ? ch - 1 : py;
                                DWORD p = ((const DWORD *)((const UBYTE *)src + (cy + py) * srcPitch))[cx + px];
                                if ((int)(p >> 24) > bestAlpha) { bestAlpha = (int)(p >> 24); best = p; }
                            }
                        rgb = best & 0x00ffffff;
                    }
                    int ink = (int)floorf(Steepen(sum[0]) * 255.f + 0.5f);
                    ink = ink > 255 ? 255 : ink;
                    dest[x] = ((DWORD)ink << 24) | rgb;
                }
            }
        }
    delete [] wide;
}
static bool sameAsReference(Tex &src, int cell, int dstPitchExtra)
{
    Tex a(src.w * CRISP_SCALE, src.h * CRISP_SCALE, src.w * CRISP_SCALE * 4 + dstPitchExtra), b(a.w, a.h, a.pitch);
    CrispUpsample(src.bytes.data(), src.pitch, src.w, src.h, cell, a.bytes.data(), a.pitch);
    CrispUpsampleReference(src.bytes.data(), src.pitch, src.w, src.h, cell, b.bytes.data(), b.pitch);
    return a.bytes == b.bytes;
}
static unsigned seed = 12345u;
static unsigned rnd() { seed = seed * 1664525u + 1013904223u; return seed >> 8; }

static bool padsIntact(Tex &t, int widthPixels)
{
    for (int y = 0; y < t.h; y++)
        for (int x = widthPixels * 4; x < t.pitch; x++)
            if (t.bytes[(size_t)y * t.pitch + x] != 0xA5) return false;
    return t.guard();
}

int main()
{
    // The weights are a partition of unity, and reproduce a constant.
    for (int o = 0; o < 4 * CRISP_SCALE; o++)
    {
        int first; float w[4];
        CrispWeights(o, &first, w);
        check(fabsf(w[0] + w[1] + w[2] + w[3] - 1.f) < 1e-5f, "the cubic weights sum to one");
    }

    // A solid cell stays solid, whatever the rounding does: alpha 255 and its colour exactly.
    {
        Tex s(16, 16, 64); s.clear(px(255, 16, 32, 48));
        Tex d = up(s, 16);
        bool all = true;
        for (int y = 0; y < d.h; y++) for (int x = 0; x < d.w; x++) all = all && d.at(x, y) == px(255, 16, 32, 48);
        check(all, "a solid cell stays solid, alpha 255 and its colour");
        check(padsIntact(d, d.w), "nothing is written past the output");
    }
    // An empty texture stays clear.
    {
        Tex s(32, 32, 128); s.clear(0);
        Tex d = up(s, 16);
        bool all = true;
        for (int y = 0; y < d.h; y++) for (int x = 0; x < d.w; x++) all = all && (d.at(x, y) >> 24) == 0;
        check(all, "an empty texture stays clear");
    }
    // A stroke (a column 242 and its neighbour 76 of 255): a solid core, clear sides, a monotonic edge.
    {
        Tex s(16, 16, 64); s.clear(px(0, 255, 255, 255));
        for (int y = 2; y < 14; y++) { s.at(5, y) = px(242, 255, 255, 255); s.at(4, y) = px(76, 255, 255, 255); }
        Tex d = up(s, 16);
        int y = 32, run = 0, best = 0, start = -1;
        for (int x = 0; x < d.w; x++)
        {
            if ((d.at(x, y) >> 24) == 255) { if (!run) start = x; run++; best = run > best ? run : best; } else run = 0;
        }
        check(best >= 3, "a stroke keeps a solid core");
        for (int x = 0; x < 8; x++) check((d.at(x, y) >> 24) == 0, "a stroke leaves its left side clear");
        for (int x = 40; x < d.w; x++) check((d.at(x, y) >> 24) == 0, "and its right side");
        int a0 = 0;
        for (int x = 8; x <= start; x++) { int a = d.at(x, y) >> 24; check(a >= a0, "the rising edge is monotonic"); a0 = a; }
    }
    // A character never leaks into its neighbour: a solid cell beside a clear one.
    {
        Tex s(32, 16, 128); s.clear(0);
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) s.at(x, y) = px(255, 255, 255, 255);
        Tex d = up(s, 16);
        bool left = true, right = true;
        for (int y = 0; y < d.h; y++)
        {
            for (int x = 0; x < 64; x++) left = left && (d.at(x, y) >> 24) == 255;
            for (int x = 64; x < 128; x++) right = right && (d.at(x, y) >> 24) == 0;
        }
        check(left, "the cell keeps its ink at its edge");
        check(right, "a character never leaks into the next cell");
    }
    // Colour: a coloured block with clear grey texels around it keeps its colour to the edge, and bleeds it into the clear texels beside.
    {
        Tex s(16, 16, 64); s.clear(px(0, 90, 90, 90));
        for (int y = 5; y < 11; y++) for (int x = 5; x < 11; x++) s.at(x, y) = px(255, 200, 40, 10);
        Tex d = up(s, 16);
        for (int y = 0; y < d.h; y++)
            for (int x = 0; x < d.w; x++)
            {
                DWORD p = d.at(x, y);
                if ((p >> 24) > 0)
                {
                    int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
                    check(abs(r - 200) <= 1 && abs(g - 40) <= 1 && abs(b - 10) <= 1, "ink keeps its colour right up to the edge");
                }
            }
        DWORD beside = d.at(5 * CRISP_SCALE - 3, 8 * CRISP_SCALE);       // clear, three upsampled texels left of the block
        check((beside >> 24) == 0 && (beside & 0xffffff) == px(0, 200, 40, 10), "a clear texel beside ink carries the ink's colour");
        DWORD far = d.at(1, 1);
        check((far >> 24) == 0, "far from any ink the texture is clear");
    }
    // Pitches other than the width, and cells cut short at the right and bottom (40 x 24 with cells of 16).
    {
        Tex s(40, 24, 40 * 4 + 24); s.clear(px(255, 9, 8, 7));
        Tex d = up(s, 16, 40);
        bool all = true;
        for (int y = 0; y < d.h; y++) for (int x = 0; x < d.w; x++) all = all && d.at(x, y) == px(255, 9, 8, 7);
        check(all, "cells cut short are rebuilt whole");
        check(padsIntact(d, d.w), "the output pitch is the one given, and nothing is written past it");
        // the source's pad bytes were not touched either
        for (int y = 0; y < s.h; y++) for (int x = 40 * 4; x < s.pitch; x++) check(s.bytes[(size_t)y * s.pitch + x] == 0xA5, "the source is only read");
    }
    // Skipping what is blank changes nothing: the bytes equal those of the upsampling that works out every texel. Textures of
    // strokes with soft edges and noise in the clear texels, a font with blank cells and rows, one faint texel in each corner
    // of a cell and beside its edges, no alpha at all (the colour of clear texels alone), cut-short cells and odd pitches.
    {
        const int sizes[][3] = {{64, 48, 16}, {40, 24, 16}, {37, 53, 8}, {96, 64, 32}};
        for (const auto &z : sizes)
        {
            Tex s(z[0], z[1], z[0] * 4 + 12);
            for (int y = 0; y < s.h; y++)
                for (int x = 0; x < s.w; x++)
                {
                    int cxn = x / z[2], cyn = y / z[2], lx = x % z[2], ly = y % z[2];
                    bool used = ((cxn * 7 + cyn * 13) % 5) != 0;
                    bool stroke = used && lx > 1 && lx < z[2] - 1 && ly > 1 && ly < z[2] - 1 && ((lx / 2 + ly / 3 + cxn) % 3 == 0);
                    int a = stroke ? (rnd() % 3 == 0 ? 255 : 40 + (int)(rnd() % 216)) : (rnd() % 50 == 0 ? 1 + (int)(rnd() % 12) : 0);
                    DWORD rgb = stroke ? ((rnd() % 4 == 0) ? (rnd() & 0xffffff) : 0xffffff) : ((rnd() % 2) ? 0 : (rnd() & 0xffffff));
                    s.at(x, y) = ((DWORD)a << 24) | rgb;
                }
            check(sameAsReference(s, z[2], 0), "a font-like texture upsamples to the reference's bytes");
            check(sameAsReference(s, z[2], 20), "and with another destination pitch");
        }
        // one faint texel in a blank texture: at the corners and edges of a cell, and in the rows and columns that only the cubic's
        // outer taps and the clear-texel colour search reach
        for (int cell : {8, 16})
        {
            const int w = cell * 3 + 3, h = cell * 2 + 5;
            const int spots[][2] = {{0, 0}, {cell - 1, 0}, {0, cell - 1}, {cell - 1, cell - 1}, {cell, cell}, {cell + 1, cell - 1},
                                    {w - 1, h - 1}, {w - 2, 0}, {3, h - 1}, {cell / 2, cell - 1}, {cell - 1, cell / 2}};
            for (const auto &p : spots)
                for (int alpha : {1, 3, 9, 255})
                {
                    Tex s(w, h, w * 4 + 8);
                    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) s.at(x, y) = px(0, 90 + (x * 5 + y) % 7, 30, 20 + (x + y * 3) % 9);
                    s.at(p[0], p[1]) = px(alpha, 250, 10 + alpha % 100, 5);
                    check(sameAsReference(s, cell, 0), "a single faint or full texel upsamples to the reference's bytes");
                }
        }
        // ink in one row only, and in one column only, of a cell, with the neighbouring rows and columns blank
        for (int line = 0; line < 3; line++)
        {
            Tex s(32, 32, 32 * 4);
            for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) s.at(x, y) = px(0, 50, 60, 70);
            for (int i = 0; i < 16; i++)
            {
                s.at(i, line == 0 ? 0 : line == 1 ? 15 : 8) = px(200, 255, 128, 0);       // a top, a bottom and a middle row
                s.at(line == 0 ? 0 : line == 1 ? 15 : 8, 16 + i) = px(90, 0, 128, 255);   // a left, a right and a middle column
            }
            check(sameAsReference(s, 16, 0), "a row or column of ink upsamples to the reference's bytes");
        }
        Tex none(32, 32, 160);
        for (int y = 0; y < none.h; y++) for (int x = 0; x < none.w; x++) none.at(x, y) = px(0, (x * 9) & 255, (y * 7) & 255, (x + y) & 255);
        check(sameAsReference(none, 16, 0), "a texture with no alpha upsamples to the reference's bytes");
    }
    // The gate, with sharp_ui=1.
    check(CrispWanted(false, 2.0f, 1), "a bitmap font at twice its size on a wide screen is drawn from the copy");
    check(CrispWanted(false, 2.25f, 1), "at 1080p (2.25 times)");
    check(CrispWanted(false, 4.5f, 1), "and at 4.5 times");
    sharpReads = 0;
    check(!CrispWanted(false, 1.99f, 1), "less than twice keeps the game's texture");
    check(!CrispWanted(false, 1.0f, 1), "at its own size too");
    check(!CrispWanted(false, 4.5f, 0), "a 4:3 screen, a replay or a recording (not wide) never does");
    check(!CrispWanted(true, 4.5f, 1), "a system font never does");
    check(sharpReads == 0, "sharp_ui is asked last: nothing the other terms refuse asks for it");
    // Without sharp_ui=1 (the default), the game's texture at every size.
    sharpAnswer = 0;
    check(!CrispWanted(false, 2.0f, 1) && !CrispWanted(false, 2.25f, 1) && !CrispWanted(false, 4.5f, 1),
          "without sharp_ui=1 the game's texture is drawn at every size");
    puts("crisp passed");
    return 0;
}
"""

# The switch end to end: the production PortSharpUi and CrispWanted with ce.cpp's reader, on real files in the folder
# BEA_USER_DIR names. Usage: sharp on|off USERDIR/ce.ini CASE|-  (the answer the case must give; the ce.ini the reader reads;
# a file holding the case's ce.ini, or - for no ce.ini at all).
SHARP_HARNESS = r"""
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
@PORTLOG@
@READER@
static int logLines;
static char logged[640];
void PortSessionLog(PortSessionCategory, bool, const char *format, ...)
{
    va_list a;
    va_start(a, format);
    vsnprintf(logged, sizeof logged, format, a);
    va_end(a);
    ++logLines;
}
static bool gActive = true;		// widescreen.cpp's: this back buffer takes the wide paths
@SHARP@
namespace
{
@GATE@
}

static void check(bool ok, const char *what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(2); } }
static void put(const char *path, const char *text, size_t n)
{
    FILE *f = fopen(path, "wb");
    check(f && fwrite(text, 1, n, f) == n && fclose(f) == 0, "the test writes ce.ini");
}

int main(int argc, char **argv)
{
    check(argc == 4, "usage: on|off USERDIR/ce.ini CASE|-");
    const bool want = !strcmp(argv[1], "on");
    const char *ini = argv[2], *given = argv[3];
    // The opposite answer: what ce.ini says whenever the case's file is not the one that may be read.
    const char *other = want ? "sharp_ui=0\n" : "sharp_ui=1\n";

    // Nothing the other terms refuse reads the file, and a back buffer that does not take the wide paths never does.
    put(ini, other, strlen(other));
    gActive = false;
    check(!PortSharpUi() && !CrispWanted(false, 4.5f, 1), "no wide back buffer: off, and ce.ini is not read");
    gActive = true;
    check(!CrispWanted(false, 4.5f, 0), "not a wide picture: off");
    check(!CrispWanted(true, 4.5f, 1), "a system font: off");
    check(!CrispWanted(false, 1.99f, 1) && !CrispWanted(false, 1.0f, 1), "text drawn at less than twice its size: off");
    check(logLines == 0, "nothing the other terms refuse reads sharp_ui");

    // The case's ce.ini (none at all for -): the first draw that could use the copy reads it.
    if (!strcmp(given, "-"))
        check(unlink(ini) == 0, "no ce.ini");
    else
    {
        char text[4096];
        FILE *f = fopen(given, "rb");
        size_t n = f ? fread(text, 1, sizeof text, f) : 0;
        check(f && fclose(f) == 0 && n < sizeof text, "the case's text");
        put(ini, text, n);
    }
    check(CrispWanted(false, 2.25f, 1) == want, want ? "sharp_ui=1 turns the sharper text on" : "anything but sharp_ui=1 leaves it off");
    check(CrispWanted(false, 4.5f, 1) == want, "at every size from twice up");
    check(!CrispWanted(false, 1.99f, 1) && !CrispWanted(true, 4.5f, 1) && !CrispWanted(false, 4.5f, 0), "the other terms still refuse");
    check(logLines == 1 && strstr(logged, want ? "sharp_ui=1 (ce.ini)" : "sharp_ui=0 (ce.ini)"), "the session log says the answer, once");

    // Read once: a change of the file takes effect at the next start. A back buffer without the wide paths later is off.
    put(ini, other, strlen(other));
    check(CrispWanted(false, 2.25f, 1) == want && PortSharpUi() == (want ? 1 : 0) && logLines == 1, "read once");
    gActive = false;
    check(!PortSharpUi() && !CrispWanted(false, 2.25f, 1), "a back buffer that does not take the wide paths: off");
    printf("sharp_ui %s passed\n", want ? "on" : "off");
    return 0;
}
"""

# What ce.ini holds (None: no ce.ini at all) and the answer: on only for sharp_ui=1, as CE_SettingGetBool reads a switch.
SHARP_CASES = [
    (None, "off"), ("", "off"), ("rumble=1\nhud_anchor=0\n", "off"), ("sharp_ui=0\n", "off"),
    ("sharp_ui=1\n", "on"), ("rumble=0\r\nsharp_ui=1\r\n", "on"), ("sharp_ui= 1 \n", "on"), ("hud_anchor=1\nsharp_ui=1", "on"),
    ("sharp_ui =1\n", "on"), ("sharp_ui = 1\n", "on"), ("  sharp_ui\t=\t1\r\n", "on"),
    # values that are not exactly 0 or 1, a second line, and other keys: the default
    ("sharp_ui=yes\n", "off"), ("sharp_ui=2\n", "off"), ("sharp_ui=1x\n", "off"), ("sharp_ui=on\n", "off"), ("sharp_ui=\n", "off"),
    ("sharp_ui=-1\n", "off"), ("sharp_ui=1\nsharp_ui=1\n", "off"), ("sharp_ui = 1\nsharp_ui=0\n", "off"), ("Sharp_UI=1\n", "off"),
    ("; sharp_ui = 1\n", "off"), ("sharp_ui x=1\n", "off"), ("sharp_ui_x=1\n", "off"),
]
# Causal mutants of the switch (widescreen.cpp) and of the gate's use of it (DXFont.cpp), run on every case.
SHARP_MUTANTS = [
    ("default-on", "wide", 'CE_SettingGetBool("sharp_ui", 0)', 'CE_SettingGetBool("sharp_ui", 1)'),
    ("no-wide-fence", "wide", "\tif (!gActive)\n\t\treturn 0;\n", ""),
    ("read-every-time", "wide", "\tif (ini < 0)\n\t{\n\t\tini = CE_SettingGetBool", "\t{\n\t\tini = CE_SettingGetBool"),
    ("lenient-reader", "wide", 'ini = CE_SettingGetBool("sharp_ui", 0) ? 1 : 0;', 'ini = CE_SettingGet("sharp_ui", 0) ? 1 : 0;'),
    ("another-key", "wide", 'CE_SettingGetBool("sharp_ui", 0)', 'CE_SettingGetBool("sharp", 0)'),
    ("gate-ignores-sharp-ui", "font", GATE, "return !sysFont && wide && scale >= CRISP_MIN_SCALE;"),
    ("gate-asks-sharp-ui-first", "font", GATE, "return PortSharpUi() && !sysFont && wide && scale >= CRISP_MIN_SCALE;"),
]


def block(source):
    """From the constants through CrispUpsample, ahead of the font table."""
    start = source.index("const int\t\tCRISP_SCALE = 4;")
    end = source.index("struct CrispFont")
    return source[start:end]


def definition(source, signature):
    """One complete definition: from SIGNATURE (which must occur once) to its closing brace, counting braces outside strings,
    characters and comments."""
    if source.count(signature) != 1:
        raise AssertionError(f"expected one {signature!r}")
    start = source.index(signature)
    i, depth = source.index("{", start), 0
    while True:
        c = source[i]
        if source.startswith("//", i):
            i = source.index("\n", i)
        elif source.startswith("/*", i):
            i = source.index("*/", i) + 1
        elif c in "\"'":
            i += 1
            while source[i] != c:
                i += 2 if source[i] == "\\" else 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if not depth:
                return source[start:i + 1] + "\n"
        i += 1


def sharp_program(wide=None, font=None):
    """The end-to-end program: the production PortSharpUi (widescreen.cpp) and CrispWanted (DXFont.cpp, with its threshold)
    with ce.cpp's ce.ini reader and the settings-folder code it uses (ce_report.h); WIDE and FONT replace those sources."""
    wide = wide if wide is not None else WIDESCREEN.read_text()
    font = font if font is not None else SOURCE.read_text()
    report, ce = REPORT.read_text(), CE.read_text()
    start = report.index("typedef struct CeSettingsDir")
    end = report.index("} CeSettingsDir;", start) + len("} CeSettingsDir;")
    reader = "".join([definition(report, "static inline size_t ce_trim_sep_len("),
                      definition(report, "static inline void ce_user_dir("), report[start:end] + "\n",
                      definition(report, "static inline void ce_settings_dir_close("),
                      definition(report, "static inline int ce_settings_dir_open_ex("),
                      definition(report, "static inline int ce_ini_match_key("),
                      definition(ce, "static FILE *ce_ini_open_regular("), definition(ce, "static char *ce_ini_read("),
                      definition(ce, "static int ce_ini_value("), definition(ce, 'extern "C" int CE_SettingGet('),
                      definition(ce, 'extern "C" int CE_SettingGetBool(')])
    scale = re.search(r"^const float\t\tCRISP_MIN_SCALE = [^\n]*\n", font, re.M)
    if not scale:
        raise AssertionError("CRISP_MIN_SCALE")
    return (SHARP_HARNESS.replace("@PORTLOG@", PORTLOG.read_text()).replace("@READER@", reader)
            .replace("@SHARP@", definition(wide, "int PortSharpUi()"))
            .replace("@GATE@", scale.group(0) + definition(font, "bool\tCrispWanted(")))


class CrispTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "local-data/port/font-crisp-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="run-", dir=parent)
        self.out = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def llvm(self):
        clang = deps.DATA / "llvm/bin/clang++"
        self.assertTrue(clang.exists(), f"{clang}: the pinned LLVM is required (port/tools/deps.py toolchain)")
        return str(clang)

    def build(self, name, source, qifist):
        program = self.out / f"{name}.cpp"
        program.write_text(HARNESS.replace("@BLOCK@", block(source)))
        exe = self.out / name
        if not qifist:
            built = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++14", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter",
                                    "-Werror", *SANITIZE, str(program), "-o", str(exe)], capture_output=True, text=True,
                                   timeout=180, preexec_fn=no_core)
            self.assertEqual(built.returncode, 0, built.stderr)
            return exe
        clang = self.llvm()
        ll = self.out / f"{name}.ll"
        steps = [[clang, "-std=gnu++14", "-O2", "-fno-exceptions", "-S", "-emit-llvm", "-Xclang", "-disable-llvm-passes", "-o", str(ll), str(program)],
                 [sys.executable, str(QIFIST), str(ll)],
                 [clang, "-O2", "-o", str(exe), str(ll), "-lm"]]
        for step in steps:
            built = subprocess.run(step, capture_output=True, text=True, timeout=180, preexec_fn=no_core)
            self.assertEqual(built.returncode, 0, " ".join(step) + "\n" + built.stdout + built.stderr)
        return exe

    def run_exe(self, exe):
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:abort_on_error=0", UBSAN_OPTIONS="halt_on_error=1")
        return subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=60, preexec_fn=no_core)

    def test_crisp_plain_and_qifist(self):
        for qifist in (False, True):
            with self.subTest(qifist=qifist):
                ran = self.run_exe(self.build("plain" if not qifist else "qifist", SOURCE.read_text(), qifist))
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                self.assertIn("crisp passed", ran.stdout)

    def test_qifist_rewrite_is_what_builds_the_game(self):
        # The pipeline above must be the game's: qifist.py rewrites float-to-integer conversions, and port/build.py runs it on
        # every game file but cframetimer.cpp, which DXFont.cpp is not (it replaces src/DXFont.cpp in the public build).
        build = (ROOT / "port/build.py").read_text()
        self.assertIn("qifist.py", build)
        self.assertIn("-Xclang -disable-llvm-passes", build)
        self.assertNotIn("DXFont.cpp", (ROOT / "port/tools/qifist.py").read_text())

    def test_mutants_are_caught(self):
        source = SOURCE.read_text()
        mutants = [
            ("rounding-plus-half", "ink = (int)floorf(Steepen(sum[0]) * 255.f + 0.5f);\n\t\t\t\t\t\tink = ink > 255 ? 255 : ink;",
             "ink = (int)(Steepen(sum[0]) * 255.f + 0.5f);", "qifist"),
            ("no-clamp-at-the-cell-edge", "\t\t\t\t\t\ti = i < 0 ? 0 : i >= cw ? cw - 1 : i;\n\t\t\t\t\t\ttaps[k] = row[i];",
             "\t\t\t\t\t\ttaps[k] = row[i];", "any"),
            ("row-ink-misses-the-last-column", "for (int i = 0; i < cw; i++)\n\t\t\t\t\tink = ink || (row[i] >> 24) != 0;",
             "for (int i = 0; i < cw - 1; i++)\n\t\t\t\t\tink = ink || (row[i] >> 24) != 0;", "any"),
            # (the outer two taps' weights are never positive, so ink in them alone cannot lift a sum over the no-ink threshold:
            # leaving one of those out is no change at all, and the inner taps are the ones to leave out)
            ("kernel-ink-misses-an-inner-row", "kernelInk = kernelInk || rowInk[row[k]];", "kernelInk = kernelInk || (k != 1 && rowInk[row[k]]);", "any"),
            ("window-ink-misses-the-row-below", "rowInk[sy > 0 ? sy - 1 : 0] || rowInk[sy] || rowInk[sy + 1 < ch ? sy + 1 : ch - 1];",
             "rowInk[sy > 0 ? sy - 1 : 0] || rowInk[sy];", "any"),
            ("tap-skip-misses-the-fourth", "if (((taps[0] | taps[1] | taps[2] | taps[3]) >> 24) != 0)", "if (((taps[0] | taps[1] | taps[2]) >> 24) != 0)", "any"),
            ("clear-threshold-too-high", "if (sum[0] <= CRISP_FROM)\n\t\t\t\t\t\tink = 0;", "if (sum[0] <= 0.5f)\n\t\t\t\t\t\tink = 0;", "any"),
            ("full-threshold-too-low", "else if (sum[0] >= CRISP_TO)\n\t\t\t\t\t\tink = 255;", "else if (sum[0] >= 0.5f)\n\t\t\t\t\t\tink = 255;", "any"),
            ("weights-wrong-sign", "w[0] = ((-0.5f * t + 1.0f) * t - 0.5f) * t;", "w[0] = ((-0.5f * t + 1.0f) * t + 0.5f) * t;", "any"),
            ("thresholds-swapped", "const float\t\tCRISP_FROM = 0.30f;", "const float\t\tCRISP_FROM = 0.80f;", "any"),
            ("colour-not-weighted", "sum[1] += wt[k] * a * (float)((p >> 16) & 0xff);", "sum[1] += wt[k] * (float)((p >> 16) & 0xff);", "any"),
            ("dilation-off", "rgb = best & 0x00ffffff;", "rgb = best & 0x00000000;", "any"),
            ("destination-pitch-ignored", "DWORD\t*dest = (DWORD *)((UBYTE *)dst + (cy * CRISP_SCALE + o) * dstPitch) + cx * CRISP_SCALE;",
             "DWORD\t*dest = (DWORD *)((UBYTE *)dst + (cy * CRISP_SCALE + o) * (w * CRISP_SCALE * 4)) + cx * CRISP_SCALE;", "any"),
            ("source-pitch-ignored", "const DWORD\t*row = (const DWORD *)((const UBYTE *)src + (cy + y) * srcPitch) + cx;",
             "const DWORD\t*row = (const DWORD *)((const UBYTE *)src + (cy + y) * (w * 4)) + cx;", "any"),
            ("cells-not-cut-short", "const int\tcw = cx + cell > w ? w - cx : cell;", "const int\tcw = cell;", "any"),
            ("gate-ignores-wide-screens", GATE, "return !sysFont && scale >= CRISP_MIN_SCALE && PortSharpUi();", "any"),
            ("gate-scale", "const float\t\tCRISP_MIN_SCALE = 2.0f;", "const float\t\tCRISP_MIN_SCALE = 1.0f;", "any"),
            ("gate-system-fonts", GATE, "return wide && scale >= CRISP_MIN_SCALE && PortSharpUi();", "any"),
            ("gate-ignores-sharp-ui", GATE, "return !sysFont && wide && scale >= CRISP_MIN_SCALE;", "any"),
            ("gate-asks-sharp-ui-first", GATE, "return PortSharpUi() && !sysFont && wide && scale >= CRISP_MIN_SCALE;", "any"),
        ]
        for name, old, new, where in mutants:
            with self.subTest(mutant=name):
                self.assertEqual(source.count(old), 1, name)
                broken = source.replace(old, new)
                results = {}
                for qifist in (False, True):
                    exe = self.build(f"{name}-{'q' if qifist else 'p'}", broken, qifist)       # a mutant that does not build is a bad mutant
                    ran = self.run_exe(exe)
                    results[qifist] = "caught" if ran.returncode != 0 else "survived"
                if where == "qifist":
                    self.assertEqual(results[True], "caught", f"mutant {name} survived the /QIfist build: {results}")
                    self.assertEqual(results[False], "survived", f"the rounding mutant is exact in a plain build: {results}")
                else:
                    self.assertIn("caught", results.values(), f"mutant {name} survived: {results}")

    def test_clear_row_fast_path_keeps_transparent_colours(self):
        # Clear texels may carry RGB for filtering.  Tied zero alphas must
        # choose the same first sample as the full reference search.
        source = SOURCE.read_text()
        old = "const DWORD clear = clearRow[sx > 0 ? sx - 1 : 0] & 0x00ffffff;"
        mutants = (
            ("wrong-clear-sample", "const DWORD clear = clearRow[sx] & 0x00ffffff;"),
            ("discard-clear-colour", "const DWORD clear = clearRow[sx > 0 ? sx - 1 : 0] & 0x00000000;"),
        )
        self.assertEqual(source.count(old), 1)
        for name, replacement in mutants:
            with self.subTest(mutant=name):
                for qifist in (False, True):
                    exe = self.build(f"{name}-{'q' if qifist else 'p'}", source.replace(old, replacement), qifist)
                    ran = self.run_exe(exe)
                    self.assertNotEqual(ran.returncode, 0, f"{name} survived {'qifist' if qifist else 'plain'}")
                    self.assertIn("FAIL:", ran.stderr, f"{name} failed without a pixel assertion")

    def build_sharp(self, name, program_text):
        program = self.out / f"{name}.cpp"
        program.write_text(program_text)
        exe = self.out / name
        # -w: the reader's text is ce.cpp's own (its warnings are that file's business); the sanitizers still watch it run
        built = subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-g", "-w", *SANITIZE, str(program), "-o", str(exe)],
                               capture_output=True, text=True, timeout=180, preexec_fn=no_core)
        self.assertEqual(built.returncode, 0, built.stderr)      # a mutant that does not build is a bad mutant
        return exe

    def sharp_case(self, exe, text, answer):
        case = self.out / f"case-{len(list(self.out.glob('case-*')))}"
        (case / "user").mkdir(parents=True)
        if text is not None:
            (case / "ini.txt").write_bytes(text.encode())
        env = {k: v for k, v in os.environ.items() if not k.startswith(("BEA_", "XDG_"))}
        env.update(BEA_USER_DIR=str(case / "user"), ASAN_OPTIONS="detect_leaks=0:abort_on_error=0", UBSAN_OPTIONS="halt_on_error=1")
        return subprocess.run([str(exe), answer, str(case / "user/ce.ini"), str(case / "ini.txt") if text is not None else "-"],
                              env=env, capture_output=True, text=True, timeout=60, preexec_fn=no_core)

    def test_sharp_ui_switch_end_to_end(self):
        exe = self.build_sharp("sharp", sharp_program())
        for text, answer in SHARP_CASES:
            with self.subTest(ini=text):
                ran = self.sharp_case(exe, text, answer)
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                self.assertIn(f"sharp_ui {answer} passed", ran.stdout)

    def test_sharp_ui_mutants_are_caught(self):
        for name, where, old, new in SHARP_MUTANTS:
            with self.subTest(mutant=name):
                source = (WIDESCREEN if where == "wide" else SOURCE).read_text()
                self.assertEqual(source.count(old), 1, name)
                broken = source.replace(old, new)
                exe = self.build_sharp(f"mutant-{name}", sharp_program(**{where: broken}))
                failed = [text for text, answer in SHARP_CASES if self.sharp_case(exe, text, answer).returncode != 0]
                self.assertTrue(failed, f"mutant {name} survived every case")

    def test_linux_user_gdi_font_and_dib_lifecycle(self):
        user_src = (ROOT / "port/platform/linux/user.cpp").read_text()
        gdi_start = user_src.index("// ---- GDI: memory DCs and DIB sections")
        gdi_end = user_src.index("// ---- version information:")
        gdi_slice = user_src[gdi_start:gdi_end]
        program = self.out / "linux_user_gdi.cpp"
        program.write_text(
            "#include <limits.h>\n"
            "#include <stdint.h>\n"
            "#include <stdio.h>\n"
            "#include <stdlib.h>\n"
            "#include <string.h>\n"
            '#include "windows.h"\n'
            + gdi_slice
            + r"""
static void check(bool ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(2); }
}
int main()
{
    HDC dc = CreateCompatibleDC(NULL);
    check(dc != NULL, "CreateCompatibleDC returns non-NULL");
    HFONT font = CreateFontA(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, VARIABLE_PITCH, "Sans");
    check(font != NULL, "CreateFontA returns non-NULL");
    HGDIOBJ prevFont = SelectObject(dc, font);
    check(prevFont != NULL && prevFont != (HGDIOBJ)(intptr_t)-1, "SelectObject accepts font handle");

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 16;
    bmi.bmiHeader.biHeight = -16;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    void *bits = (void *)0x1;
    HBITMAP bmp = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    check(bmp != NULL && bits != NULL, "CreateDIBSection allocates valid 32-bit section");
    memset(bits, 0x7f, 16 * 16 * 4);

    // Regression for Linux DeleteObject(font) casting sentinel 0x464f to Dib* and segfaulting.
    check(DeleteObject(font) == TRUE, "DeleteObject recognizes font sentinel without dereferencing");
    check(DeleteObject(NULL) == FALSE, "DeleteObject rejects NULL");
    check(DeleteObject((HGDIOBJ)(intptr_t)-1) == FALSE, "DeleteObject rejects HGDI_ERROR");
    check(DeleteObject(GetDC(NULL)) == FALSE, "DeleteObject rejects screen DC sentinel");
    check(DeleteObject(dc) == FALSE, "DeleteObject rejects memory DC sentinel");
    check(DeleteObject(bmp) == TRUE, "DeleteObject frees DIB section");
    check(DeleteDC(dc) == TRUE, "DeleteDC succeeds");

    // Bounds and overflow hardening on CreateDIBSection.
    void *badBits = (void *)0x1;
    check(CreateDIBSection(dc, NULL, DIB_RGB_COLORS, &badBits, NULL, 0) == NULL && badBits == NULL, "NULL bmi rejected");
    bmi.bmiHeader.biWidth = 0;
    badBits = (void *)0x1;
    check(CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &badBits, NULL, 0) == NULL && badBits == NULL, "zero width rejected");
    bmi.bmiHeader.biWidth = 16;
    bmi.bmiHeader.biHeight = INT_MIN;
    badBits = (void *)0x1;
    check(CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &badBits, NULL, 0) == NULL && badBits == NULL, "INT_MIN height rejected without UB");
    bmi.bmiHeader.biWidth = 65536;
    bmi.bmiHeader.biHeight = -65536;
    badBits = (void *)0x1;
    check(CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &badBits, NULL, 0) == NULL && badBits == NULL, "oversized DIB rejected");
    bmi.bmiHeader.biWidth = 16;
    bmi.bmiHeader.biHeight = -16;
    bmi.bmiHeader.biBitCount = 7;
    badBits = (void *)0x1;
    check(CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &badBits, NULL, 0) == NULL && badBits == NULL, "invalid bpp rejected");
    puts("linux user gdi passed");
    return 0;
}
"""
        )
        exe = self.out / "linux_user_gdi"
        built = subprocess.run(
            [os.environ.get("CXX", "g++"), "-std=c++14", "-g", "-Wall", "-Wextra", "-Werror", *SANITIZE,
             f"-I{ROOT / 'port/include'}", f"-I{ROOT / 'port/include/linux'}",
             f"-I{deps.DATA / 'dxvk-native/usr/include/dxvk'}", str(program), "-o", str(exe)],
            capture_output=True, text=True, timeout=180, preexec_fn=no_core
        )
        self.assertEqual(built.returncode, 0, built.stderr)
        ran = self.run_exe(exe)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("linux user gdi passed", ran.stdout)

    def test_dxfont_lifecycle_zstate_and_generation_cache(self):
        src = SOURCE.read_text()
        ns_start = src.index("namespace\n{")
        ctor_start = src.index("CDXBitmapFont::CDXBitmapFont()")
        ns_block = src[ns_start:ctor_start]
        dtor_fn = definition(src, "CDXBitmapFont::~CDXBitmapFont()")
        sys_fn = definition(src, "void\tCDXBitmapFont::InitFromSysFont()")
        prep_fn = definition(src, "static void\tPrepareFont(CDXBitmapFont *font)")
        draw_fn = definition(src, "HRESULT\tCDXBitmapFont::DrawTextScaled(")

        program = self.out / "dxfont_lifecycle.cpp"
        program.write_text(
            r"""#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

typedef unsigned char UBYTE, BYTE;
typedef unsigned int DWORD, UINT;
typedef int INT, BOOL;
typedef float FLOAT;
typedef long HRESULT;
typedef uint16_t WCHAR;
typedef char TCHAR;
typedef const char *LPCTSTR;
typedef void VOID;
#define TRUE 1
#define FALSE 0
#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define ZeroMemory(p, n) memset((p), 0, (n))
#define RGB(r, g, b) ((DWORD)(((BYTE)(r)) | ((DWORD)((BYTE)(g)) << 8) | ((DWORD)((BYTE)(b)) << 16)))

enum {
    BI_RGB = 0, DIB_RGB_COLORS = 0, MM_TEXT = 1, LOGPIXELSY = 90,
    FW_NORMAL = 400, FW_BOLD = 700, DEFAULT_CHARSET = 1,
    OUT_DEFAULT_PRECIS = 0, CLIP_DEFAULT_PRECIS = 0, ANTIALIASED_QUALITY = 4, VARIABLE_PITCH = 2,
    TA_TOP = 0, ETO_OPAQUE = 2,
    D3DFVF_XYZRHW = 0x004, D3DFVF_DIFFUSE = 0x040, D3DFVF_TEX1 = 0x100,
    D3DFMT_A8R8G8B8 = 21, D3DFMT_INDEX16 = 101,
    D3DPOOL_DEFAULT = 0, D3DPOOL_MANAGED = 1,
    D3DUSAGE_WRITEONLY = 8, D3DUSAGE_DYNAMIC = 512,
    D3DPT_TRIANGLELIST = 4, D3DLOCK_READONLY = 16, D3DLOCK_NOSYSLOCK = 2048,
    D3DRS_ZENABLE = 7, INVALID_STATE = 0x7fffffff,
    MEMTYPE_TEXTURE = 1, TEXFMT_A8R8G8B8 = 21
};
typedef DWORD COLORREF, D3DFORMAT, D3DPOOL;
struct SIZE { INT cx, cy; };
struct RECT { INT left, top, right, bottom; };
struct BITMAPINFOHEADER {
    DWORD biSize; INT biWidth; INT biHeight; uint16_t biPlanes; uint16_t biBitCount;
    DWORD biCompression; DWORD biSizeImage; INT biXPelsPerMeter; INT biYPelsPerMeter; DWORD biClrUsed; DWORD biClrImportant;
};
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; DWORD bmiColors[1]; };
typedef void *HGDIOBJ, *HDC, *HBITMAP, *HFONT, *HANDLE;
struct D3DXVECTOR4 {
    FLOAT x, y, z, w;
    D3DXVECTOR4() : x(0), y(0), z(0), w(0) {}
    D3DXVECTOR4(FLOAT a, FLOAT b, FLOAT c, FLOAT d) : x(a), y(b), z(c), w(d) {}
};
struct D3DSURFACE_DESC { D3DFORMAT Format; UINT Width, Height; };
struct D3DLOCKED_RECT { INT Pitch; void *pBits; };
struct D3DCAPS9 { DWORD MaxTextureWidth, MaxTextureHeight; };

enum PortSessionCategory { PORT_SESSION_VIDEO = 0 };
static int gLogErrors = 0;
void PortSessionLog(PortSessionCategory, bool isError, const char *, ...) { if (isError) ++gLogErrors; }
DWORD GetTickCount() { return 1000; }
INT MulDiv(INT a, INT b, INT c) { return (INT)(((int64_t)a * b) / c); }
const char *ToTCHAR(const char *s) { return s; }

static uint64_t gDeviceGen = 1;
extern "C" uint64_t PortCeD3dDeviceGeneration(void) __attribute__((weak));
extern "C" uint64_t PortCeD3dDeviceGeneration(void) { return gDeviceGen; }
struct TweakStub { int mValue; } USE_MODULATE_2X = {1};
float FONT_U_OFFSET = 0.0f, FONT_V_OFFSET = 0.0f;
static int gWideActive = 1, gSharpUi = 1;
int PortWideActive() { return gWideActive; }
int PortSharpUi() { return gSharpUi; }
float PortUiScale() { return 1.0f; }
float PortUiLeft() { return 0.0f; }
float PortUiTop() { return 0.0f; }

static int gLiveTextures = 0;
static bool gFailCreateTex = false, gFailDstLock = false, gFailSysLock = false;
static INT gSysPitchOverride = 0;

struct IDirect3DTexture9 {
    UINT w, h;
    INT pitch;
    D3DFORMAT fmt;
    int refs = 1;
    bool isSysTex = false;
    std::vector<BYTE> storage;
    IDirect3DTexture9(UINT ww, UINT hh, D3DFORMAT ff, INT pp = 0)
        : w(ww), h(hh), pitch(pp ? pp : (INT)(ww * 4)), fmt(ff), storage((size_t)(pitch > 0 ? pitch : 4) * hh, 0)
    { ++gLiveTextures; }
    ~IDirect3DTexture9() { --gLiveTextures; }
    void Release() { if (--refs == 0) delete this; }
    HRESULT GetLevelDesc(UINT, D3DSURFACE_DESC *d) { d->Format = fmt; d->Width = w; d->Height = h; return S_OK; }
    HRESULT LockRect(UINT, D3DLOCKED_RECT *lr, const RECT *, DWORD)
    {
        if ((isSysTex && gFailSysLock) || (!isSysTex && w >= 64 && gFailDstLock)) { lr->pBits = NULL; lr->Pitch = 0; return E_FAIL; }
        lr->Pitch = (isSysTex && gSysPitchOverride) ? gSysPitchOverride : pitch;
        if (lr->Pitch > 0 && (size_t)lr->Pitch * h > storage.size()) storage.resize((size_t)lr->Pitch * h, 0xee);
        lr->pBits = storage.data();
        return S_OK;
    }
    HRESULT UnlockRect(UINT) { return S_OK; }
};

struct IDirect3DDevice9 {
    DWORD maxW = 1024, maxH = 1024;
    DWORD devZ = TRUE;
    HRESULT GetDeviceCaps(D3DCAPS9 *c) { c->MaxTextureWidth = maxW; c->MaxTextureHeight = maxH; return S_OK; }
    HRESULT CreateTexture(UINT w, UINT h, UINT, DWORD, D3DFORMAT fmt, D3DPOOL, IDirect3DTexture9 **out, HANDLE *)
    {
        if (gFailCreateTex) { *out = NULL; return E_FAIL; }
        *out = new IDirect3DTexture9(w, h, fmt);
        return S_OK;
    }
    HRESULT GetRenderState(DWORD, DWORD *v) { *v = devZ; return S_OK; }
};

struct LtStub {
    IDirect3DDevice9 *dev = NULL;
    DWORD mRenderStates[16];
    int srsCalls = 0;
    IDirect3DTexture9 *boundTex = NULL;
    LtStub() { for (int i = 0; i < 16; ++i) mRenderStates[i] = TRUE; }
    IDirect3DDevice9 *GetDevice() { return dev; }
    void SRS(DWORD state, DWORD val) { mRenderStates[state] = val; ++srsCalls; }
    void D3D_SetTexture(DWORD, IDirect3DTexture9 *t) { boundTex = t; }
} LT;

struct FastVbStub {
    std::vector<BYTE> buf;
    int renders = 0;
    FastVbStub() : buf(2600 * 4 * 32, 0) {}
    void Create() {}
    UINT LockAligned(void **out, int verts) { *out = buf.data(); (void)verts; return 0; }
    void Render() { ++renders; }
} FASTVB;

struct FrontendStub { bool IsRunning() { return false; } } FRONTEND;
struct PlatformStub { float GetWindowWidth() { return 640.f; } float GetWindowHeight() { return 480.f; } } PLATFORM;

struct CDXTexture {
    IDirect3DTexture9 *tex = NULL;
    char mName[64] = {};
    int refs = 1;
    static void *operator new(size_t sz, int) { return ::operator new(sz); }
    static void operator delete(void *p) { ::operator delete(p); }
    static void operator delete(void *p, int) { ::operator delete(p); }
    bool InitEmpty(DWORD w, DWORD h, int fmt, int, int, int)
    {
        if (gFailCreateTex) return false;
        tex = new IDirect3DTexture9(w, h, (D3DFORMAT)fmt);
        tex->isSysTex = true;
        return true;
    }
    IDirect3DTexture9 *GetTexture() { return tex; }
    void Release() { if (--refs == 0) { if (tex) tex->Release(); delete this; } }
};

struct CVBufTexture {
    int refs = 1;
    static CVBufTexture *GetOrCreate(CDXTexture *, int) { return new CVBufTexture; }
    void SetupV(DWORD, DWORD, int, DWORD, D3DPOOL) {}
    void SetupI(D3DFORMAT, DWORD, int, D3DPOOL) {}
    void Release() { if (--refs == 0) delete this; }
};

enum GdiKind { GDI_STOCK_BMP = 1, GDI_STOCK_FONT = 2, GDI_BMP = 3, GDI_FONT = 4, GDI_DC = 5 };
struct GdiObj {
    GdiKind kind;
    bool selected = false;
    std::vector<DWORD> px;
    explicit GdiObj(GdiKind k) : kind(k) {}
};
static GdiObj gStockBmpObj(GDI_STOCK_BMP), gStockFontObj(GDI_STOCK_FONT);
struct GdiDc : GdiObj {
    GdiObj *curBmp = &gStockBmpObj;
    GdiObj *curFont = &gStockFontObj;
    GdiDc() : GdiObj(GDI_DC) {}
};

static bool gLinuxSelectSemantics = false;
static bool gFailDc = false, gFailDib = false, gFailFont = false, gFailSelectBmp = false, gFailSelectFont = false, gFailExtent = false;
static int gLiveDcs = 0, gLiveBitmaps = 0, gLiveFonts = 0, gSelectOrderViolations = 0;

HDC CreateCompatibleDC(HDC)
{
    if (gFailDc) return NULL;
    ++gLiveDcs;
    return (HDC)(new GdiDc());
}
BOOL DeleteDC(HDC h)
{
    if (!h) return FALSE;
    GdiDc *dc = (GdiDc *)h;
    if (!gLinuxSelectSemantics && (dc->curBmp != &gStockBmpObj || dc->curFont != &gStockFontObj))
        ++gSelectOrderViolations;
    --gLiveDcs;
    delete dc;
    return TRUE;
}
HBITMAP CreateDIBSection(HDC, const BITMAPINFO *bmi, UINT, VOID **bits, HANDLE, DWORD)
{
    if (gFailDib) { *bits = NULL; return NULL; }
    int w = bmi->bmiHeader.biWidth;
    int h = bmi->bmiHeader.biHeight < 0 ? -bmi->bmiHeader.biHeight : bmi->bmiHeader.biHeight;
    GdiObj *b = new GdiObj(GDI_BMP);
    b->px.assign((size_t)w * (size_t)h, 0x000000a0u);
    *bits = b->px.data();
    ++gLiveBitmaps;
    return (HBITMAP)b;
}
INT SetMapMode(HDC, INT) { return MM_TEXT; }
INT GetDeviceCaps(HDC, INT) { return 96; }
HFONT CreateFont(INT, INT, INT, INT, INT, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCTSTR)
{
    if (gFailFont) return NULL;
    ++gLiveFonts;
    return (HFONT)(new GdiObj(GDI_FONT));
}
HGDIOBJ SelectObject(HDC hdc, HGDIOBJ hobj)
{
    GdiDc *dc = (GdiDc *)hdc;
    GdiObj *obj = (GdiObj *)hobj;
    if (!dc || !obj) return NULL;
    if (obj->kind == GDI_BMP || obj->kind == GDI_STOCK_BMP)
    {
        if (gFailSelectBmp && obj->kind == GDI_BMP) return (HGDIOBJ)(intptr_t)-1;
        GdiObj *old = dc->curBmp;
        old->selected = false;
        dc->curBmp = obj;
        obj->selected = (obj->kind == GDI_BMP);
        return gLinuxSelectSemantics ? hobj : (HGDIOBJ)old;
    }
    if (obj->kind == GDI_FONT || obj->kind == GDI_STOCK_FONT)
    {
        if (gFailSelectFont && obj->kind == GDI_FONT) return (HGDIOBJ)(intptr_t)-1;
        GdiObj *old = dc->curFont;
        old->selected = false;
        dc->curFont = obj;
        obj->selected = (obj->kind == GDI_FONT);
        return gLinuxSelectSemantics ? hobj : (HGDIOBJ)old;
    }
    return NULL;
}
BOOL DeleteObject(HGDIOBJ hobj)
{
    if (!hobj || hobj == (HGDIOBJ)(intptr_t)-1) return FALSE;
    GdiObj *obj = (GdiObj *)hobj;
    if (!gLinuxSelectSemantics && obj->selected) ++gSelectOrderViolations;
    if (obj->kind == GDI_BMP) { --gLiveBitmaps; delete obj; return TRUE; }
    if (obj->kind == GDI_FONT) { --gLiveFonts; delete obj; return TRUE; }
    return FALSE;
}
COLORREF SetTextColor(HDC, COLORREF c) { return c; }
COLORREF SetBkColor(HDC, COLORREF c) { return c; }
UINT SetTextAlign(HDC, UINT a) { return a; }
BOOL GetTextExtentPoint32(HDC, LPCTSTR, INT, SIZE *sz)
{
    if (gFailExtent) return FALSE;
    sz->cx = 8; sz->cy = 16;
    return TRUE;
}
BOOL ExtTextOut(HDC, INT, INT, UINT, const RECT *, LPCTSTR, UINT, const INT *) { return TRUE; }

struct CDXBitmapFont {
    CDXTexture *mTexture = NULL;
    CVBufTexture *mVBufTexture = NULL;
    CVBufTexture *mVBufTexture2 = NULL;
    BOOL mSysFont = FALSE;
    DWORD mHeight = 16, mFlags = 0, mTexWidth = 16, mTexHeight = 16;
    FLOAT mTextScale = 1.f;
    char mFontName[80] = "Arial";
    char mTextureName[256] = "font";
    FLOAT mTexCoords[256][4] = {};
    CDXBitmapFont() { mTexCoords[0][3] = 1.f; }
    ~CDXBitmapFont();
    int RemapChar(WCHAR c) { return (c >= 32 && c < 127) ? (int)(c - 32) : 0; }
    void InitFromTexture() {}
    void InitFromSysFont();
    HRESULT DrawTextScaled(float x, float y, float z, float scalex, float scaley, DWORD colour, WCHAR *text, int flags, DWORD *colours);
};
"""
            + ns_block + dtor_fn + sys_fn + prep_fn + draw_fn
            + r"""
static void check(bool ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(2); }
}
static void resetFaults()
{
    gFailCreateTex = gFailDstLock = gFailSysLock = false;
    gFailDc = gFailDib = gFailFont = gFailSelectBmp = gFailSelectFont = gFailExtent = false;
    gSysPitchOverride = 0;
}
int main()
{
    IDirect3DDevice9 dev;
    LT.dev = &dev;

    // 1. InitFromSysFont: Windows stock-handle semantics + padded destination pitch + deselection before DeleteObject.
    for (bool linuxSel : {false, true})
    {
        resetFaults();
        gLinuxSelectSemantics = linuxSel;
        gSysPitchOverride = 256 * 4 + 32;
        CDXBitmapFont f;
        f.mSysFont = TRUE;
        f.mHeight = 16;
        f.InitFromSysFont();
        check(f.mTexture != NULL && f.mTexture->GetTexture() != NULL, "InitFromSysFont succeeds");
        check(gLiveDcs == 0 && gLiveBitmaps == 0 && gLiveFonts == 0, "all GDI handles cleaned up on success");
        check(gSelectOrderViolations == 0, "GDI objects deselected before DeleteObject/DeleteDC");
        const BYTE *firstRow = f.mTexture->GetTexture()->storage.data();
        DWORD firstPx = 0;
        memcpy(&firstPx, firstRow, 4);
        check(firstPx == 0xa0ffffffu, "alpha intensity copied cleanly with padded Pitch");
    }

    // 2. InitFromSysFont failure paths: zero leaked GDI handles or partial textures.
    gLinuxSelectSemantics = false;
    for (int fault = 0; fault < 7; ++fault)
    {
        resetFaults();
        if (fault == 0) gFailDc = true;
        if (fault == 1) gFailDib = true;
        if (fault == 2) gFailFont = true;
        if (fault == 3) gFailSelectBmp = true;
        if (fault == 4) gFailSelectFont = true;
        if (fault == 5) gFailExtent = true;
        if (fault == 6) gFailSysLock = true;
        {
            CDXBitmapFont f;
            f.mSysFont = TRUE;
            f.mHeight = 16;
            f.InitFromSysFont();
            check(f.mTexture == NULL, "failed InitFromSysFont releases and nulls mTexture");
        }
        check(gLiveDcs == 0 && gLiveBitmaps == 0 && gLiveFonts == 0 && gSelectOrderViolations == 0 && gLiveTextures == 0,
              "every InitFromSysFont failure cleans up all GDI and D3D objects");
    }
    resetFaults();

    // 3. DrawTextScaled: null/empty/>2500 glyphs never mutate D3DRS_ZENABLE; prior FALSE and TRUE Z states are restored.
    {
        CDXBitmapFont f;
        f.mSysFont = FALSE;
        f.mHeight = 16;
        f.mTexWidth = 16;
        f.mTexHeight = 16;
        f.mTexture = new(MEMTYPE_TEXTURE) CDXTexture;
        f.mTexture->InitEmpty(16, 16, TEXFMT_A8R8G8B8, 1, 0, 1);

        LT.mRenderStates[D3DRS_ZENABLE] = TRUE;
        LT.srsCalls = 0;
        check(f.DrawTextScaled(0, 0, 0, 1, 1, 0xffffffffu, NULL, 0, NULL) == S_OK && LT.srsCalls == 0, "NULL text untouched Z");

        std::vector<WCHAR> huge(2502, (WCHAR)'A');
        huge.back() = 0;
        check(f.DrawTextScaled(0, 0, 0, 1, 1, 0xffffffffu, huge.data(), 0, NULL) == S_OK, ">2500 glyphs returns S_OK");
        check(LT.mRenderStates[D3DRS_ZENABLE] == TRUE && LT.srsCalls == 0, ">2500 glyphs does not leave ZENABLE disabled");

        WCHAR hi[] = {'H', 'i', 0};
        LT.mRenderStates[D3DRS_ZENABLE] = FALSE;
        f.DrawTextScaled(0, 0, 0, 1.f, 1.f, 0xffffffffu, hi, 0, NULL);
        check(LT.mRenderStates[D3DRS_ZENABLE] == FALSE, "caller disabled ZENABLE is restored on exit");

        LT.mRenderStates[D3DRS_ZENABLE] = TRUE;
        f.DrawTextScaled(0, 0, 0, 1.f, 1.f, 0xffffffffu, hi, 0, NULL);
        check(LT.mRenderStates[D3DRS_ZENABLE] == TRUE, "caller enabled ZENABLE is restored on exit");

        // CrispTexture cache: generation change at same IDirect3DDevice9* rebuilds; transient failure retries and logs once.
        gDeviceGen = 10;
        IDirect3DTexture9 *t1 = CrispTexture(&f, 2.5f);
        check(t1 != NULL, "crisp texture built for generation 10");
        check(CrispTexture(&f, 2.5f) == t1, "cached within same generation");

        // Advance generation at identical device pointer; fail first two attempts, then succeed on third.
        gDeviceGen = 11;
        gFailCreateTex = true;
        gLogErrors = 0;
        check(CrispTexture(&f, 2.5f) == NULL, "gen 11 failed build returns NULL instead of stale gen 10 texture");
        check(CrispTexture(&f, 2.5f) == NULL, "second failed call returns NULL");
        check(gLogErrors == 1, "failure logged at most once per generation");
        gFailCreateTex = false;
        IDirect3DTexture9 *t2 = CrispTexture(&f, 2.5f);
        check(t2 != NULL, "CrispTexture retries and succeeds once CreateTexture recovers");
    }
    check(gLiveTextures == 0, "destructor released font and crisp textures");
    puts("dxfont lifecycle passed");
    return 0;
}
"""
        )
        exe = self.out / "dxfont_lifecycle"
        built = subprocess.run(
            [os.environ.get("CXX", "g++"), "-std=c++14", "-g", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-address",
             "-Werror", *SANITIZE, str(program), "-o", str(exe)],
            capture_output=True, text=True, timeout=180, preexec_fn=no_core
        )
        self.assertEqual(built.returncode, 0, built.stderr)
        ran = self.run_exe(exe)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("dxfont lifecycle passed", ran.stdout)


if __name__ == "__main__":
    unittest.main()
