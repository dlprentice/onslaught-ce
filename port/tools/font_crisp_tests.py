#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The sharper copy of a bitmap font's texture (port/platform/public/DXFont.cpp: CrispUpsample, CrispWanted).

On a wide screen at two or more times a font's size (4K), text is drawn from a copy of the font texture four times as large:
each character cell rebuilt with a cubic interpolation of its alpha, the edge steepened around half coverage, the colour
interpolated weighted by the alpha. The production text of the weights, the steepening, the upsampling and the gate is cut
out of DXFont.cpp and run on authored textures:
- a solid cell stays solid (alpha 255, its colour), an empty one stays empty; a stroke keeps a solid core and clear sides;
- a character never leaks into the cell beside it, the last cells of a texture whose size is not a multiple of the cell are cut
  short without a read or a write outside the buffers, and the pitches (source and destination) are the ones given;
- a coloured stroke keeps its colour right up to its edge, and the texels beside it carry it too (no dark fringe when the
  sampler blends across an edge);
- the gate: bitmap fonts only, wide screens only, text of twice the size or more;
- nothing is lost by skipping the blank parts of a font (rows, taps and texels with no alpha, which are most of one): the bytes
  equal those of an upsampling that works out every texel, on font-like textures, single faint texels at the corners and edges of a
  cell, rows and columns of ink, and a texture with no alpha at all.
DXFont.cpp is compiled with /QIfist (a float becomes an integer by rounding: tools/qifist.py, port/build.py), where the usual
(int)(v + 0.5f) turns full coverage into 256 and the pixel into clear: the first version of this code drew hollow letters. The
checks therefore run twice, plain (with the address and undefined-behaviour sanitizers) and through the same /QIfist rewrite
that builds the game, with the pinned LLVM. Causal mutants of each of these must be caught, the rounding one only by the second.
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

HARNESS = r"""
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
typedef unsigned int DWORD;
typedef unsigned char UBYTE;
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
    // The gate.
    check(CrispWanted(false, 2.0f, 1), "a bitmap font at twice its size on a wide screen is drawn from the copy");
    check(CrispWanted(false, 4.5f, 1), "and at 4.5 times");
    check(!CrispWanted(false, 1.99f, 1), "less than twice keeps the game's texture");
    check(!CrispWanted(false, 1.0f, 1), "at its own size too");
    check(!CrispWanted(false, 4.5f, 0), "a 4:3 screen, a replay or a recording (not wide) never does");
    check(!CrispWanted(true, 4.5f, 1), "a system font never does");
    puts("crisp passed");
    return 0;
}
"""


def block(source):
    """From the constants through CrispUpsample, ahead of the font table."""
    start = source.index("const int\t\tCRISP_SCALE = 4;")
    end = source.index("struct CrispFont")
    return source[start:end]


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
            ("gate-ignores-wide-screens", "return !sysFont && wide && scale >= CRISP_MIN_SCALE;", "return !sysFont && scale >= CRISP_MIN_SCALE;", "any"),
            ("gate-scale", "const float\t\tCRISP_MIN_SCALE = 2.0f;", "const float\t\tCRISP_MIN_SCALE = 1.0f;", "any"),
            ("gate-system-fonts", "return !sysFont && wide && scale >= CRISP_MIN_SCALE;", "return wide && scale >= CRISP_MIN_SCALE;", "any"),
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


if __name__ == "__main__":
    unittest.main()
