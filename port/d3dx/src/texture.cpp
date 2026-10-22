// SPDX-License-Identifier: GPL-3.0-only
// D3DXCreateTextureFromFileInMemoryEx and D3DXCreateTextureFromFileExA for the files the game loads: DDS (DXT1 to
// DXT5 and uncompressed RGB, as the resources hold) and TGA (data/textures). The size, level count and format
// decisions, the pixel conversions and the filters are checked byte for byte against retail d3dx9 on a fake device
// (port/d3dx/tests: tex_corpus.py, texcase.h, fakedev.c, tex_compare.py); see port/d3dx/README.md for what is
// measured and what is not.
#include <d3dx9.h>
#include <stdio.h>
#include "bc.h"
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace {

// ---- formats

enum Kind { K_NONE, K_RGB, K_LUM, K_DXT };

struct Format
{
    D3DFORMAT format;
    Kind kind;
    UINT bits;                         // per pixel; per block for DXT (64 or 128)
    DWORD a, r, g, b;                  // channel masks (r = luminance for K_LUM)
};

#define FCC(a, b, c, d) MAKEFOURCC(a, b, c, d)

const Format kFormats[] = {
    {D3DFMT_R8G8B8, K_RGB, 24, 0, 0xff0000, 0xff00, 0xff},
    {D3DFMT_A8R8G8B8, K_RGB, 32, 0xff000000, 0xff0000, 0xff00, 0xff},
    {D3DFMT_X8R8G8B8, K_RGB, 32, 0, 0xff0000, 0xff00, 0xff},
    {D3DFMT_R5G6B5, K_RGB, 16, 0, 0xf800, 0x7e0, 0x1f},
    {D3DFMT_X1R5G5B5, K_RGB, 16, 0, 0x7c00, 0x3e0, 0x1f},
    {D3DFMT_A1R5G5B5, K_RGB, 16, 0x8000, 0x7c00, 0x3e0, 0x1f},
    {D3DFMT_A4R4G4B4, K_RGB, 16, 0xf000, 0xf00, 0xf0, 0xf},
    {D3DFMT_R3G3B2, K_RGB, 8, 0, 0xe0, 0x1c, 0x3},
    {D3DFMT_A8, K_RGB, 8, 0xff, 0, 0, 0},
    {D3DFMT_A8R3G3B2, K_RGB, 16, 0xff00, 0xe0, 0x1c, 0x3},
    {D3DFMT_X4R4G4B4, K_RGB, 16, 0, 0xf00, 0xf0, 0xf},
    {D3DFMT_A2B10G10R10, K_RGB, 32, 0xc0000000, 0x3ff, 0xffc00, 0x3ff00000},
    {D3DFMT_A8B8G8R8, K_RGB, 32, 0xff000000, 0xff, 0xff00, 0xff0000},
    {D3DFMT_X8B8G8R8, K_RGB, 32, 0, 0xff, 0xff00, 0xff0000},
    {D3DFMT_G16R16, K_RGB, 32, 0, 0xffff, 0xffff0000, 0},
    {D3DFMT_A2R10G10B10, K_RGB, 32, 0xc0000000, 0x3ff00000, 0xffc00, 0x3ff},
    {D3DFMT_L8, K_LUM, 8, 0, 0xff, 0, 0},
    {D3DFMT_A8L8, K_LUM, 16, 0xff00, 0xff, 0, 0},
    {D3DFMT_A4L4, K_LUM, 8, 0xf0, 0xf, 0, 0},
    {D3DFMT_L16, K_LUM, 16, 0, 0xffff, 0, 0},
    {D3DFMT_DXT1, K_DXT, 64, 0, 0, 0, 0},
    {D3DFMT_DXT2, K_DXT, 128, 0, 0, 0, 0},
    {D3DFMT_DXT3, K_DXT, 128, 0, 0, 0, 0},
    {D3DFMT_DXT4, K_DXT, 128, 0, 0, 0, 0},
    {D3DFMT_DXT5, K_DXT, 128, 0, 0, 0, 0},
};

const Format *Find(D3DFORMAT f)
{
    for (const Format &x : kFormats)
        if (x.format == f)
            return &x;
    return NULL;
}

UINT RowBytes(const Format *f, UINT w) { return f->kind == K_DXT ? (w + 3) / 4 * f->bits / 8 : w * f->bits / 8; }
UINT Rows(const Format *f, UINT h) { return f->kind == K_DXT ? (h + 3) / 4 : h; }

// Retail's choice of texture format: for each requested format, the formats it accepts in order of preference; the
// device's best-ranked supported one wins, and none fails with D3DERR_NOTAVAILABLE. Measured from
// D3DXCheckTextureRequirements on the fake device: every requested format against every single and pair of supported
// formats (41,871 probes, no ordering violation), then 20,000 random supported sets predicted exactly (tex_reqs.py).
// A requested D3DFMT_UNKNOWN ranks as A8R8G8B8 does.
struct Preference
{
    DWORD requested;
    DWORD order[24];
};

const Preference kPreferences[] = {
    {0, {21, 32, 36, 31, 35, 25, 26, 29, 22, 33, 23, 24, 30, 27, 34, 28, 0}},
    {20, {20, 22, 33, 21, 32, 31, 35, 36, 23, 24, 25, 30, 26, 27, 29, 34, 0}},
    {21, {21, 32, 36, 31, 35, 25, 26, 29, 22, 33, 23, 24, 30, 27, 34, 28, 0}},
    {22, {22, 33, 21, 32, 31, 35, 36, 23, 24, 25, 30, 26, 27, 29, 34, 0}},
    {23, {23, 22, 33, 21, 32, 31, 35, 36, 24, 25, 30, 26, 27, 29, 34, 0}},
    {24, {24, 23, 22, 33, 25, 21, 32, 31, 35, 36, 30, 26, 27, 29, 34, 0}},
    {25, {25, 21, 31, 32, 35, 36, 26, 29, 24, 23, 22, 33, 30, 27, 34, 28, 0}},
    {26, {26, 21, 32, 36, 31, 35, 25, 29, 30, 24, 23, 22, 33, 27, 34, 28, 0}},
    {27, {27, 30, 24, 23, 22, 33, 29, 26, 25, 21, 32, 31, 35, 36, 34, 0}},
    {28, {28, 29, 21, 32, 36, 26, 31, 35, 25, 0}},
    {29, {29, 21, 32, 36, 26, 31, 35, 25, 27, 30, 24, 23, 22, 33, 34, 28, 0}},
    {30, {30, 24, 23, 22, 33, 26, 25, 21, 32, 31, 35, 36, 27, 29, 34, 0}},
    {31, {31, 35, 36, 21, 32, 25, 26, 29, 22, 33, 23, 24, 30, 27, 34, 28, 0}},
    {32, {32, 21, 36, 31, 35, 25, 26, 29, 22, 33, 23, 24, 30, 27, 34, 28, 0}},
    {33, {33, 22, 21, 32, 31, 35, 36, 23, 24, 25, 30, 26, 27, 29, 34, 0}},
    {34, {34, 36, 31, 35, 22, 33, 21, 32, 23, 24, 25, 30, 26, 27, 29, 0}},
    {35, {35, 31, 36, 21, 32, 25, 26, 29, 22, 33, 23, 24, 30, 27, 34, 28, 0}},
    {36, {36, 21, 31, 32, 35, 25, 26, 29, 22, 33, 23, 24, 30, 27, 34, 28, 0}},
    {40, {40, 41, 21, 32, 36, 31, 35, 25, 26, 29, 22, 33, 23, 24, 30, 27, 34, 28, 0}},
    {41, {41, 40, 21, 32, 36, 31, 35, 25, 26, 29, 22, 33, 23, 24, 30, 27, 34, 28, 0}},
    {50, {50, 22, 33, 81, 51, 21, 32, 31, 35, 36, 23, 24, 25, 30, 52, 26, 27, 29, 34, 0}},
    {51, {51, 21, 32, 36, 31, 35, 52, 25, 26, 29, 50, 22, 33, 81, 23, 24, 30, 27, 34, 28, 0}},
    {52, {52, 26, 51, 21, 32, 36, 31, 35, 25, 29, 30, 24, 23, 50, 22, 33, 81, 27, 34, 28, 0}},
    {60, {60, 64, 62, 63, 67, 110, 61, 0}},
    {61, {61, 62, 67, 60, 64, 63, 110, 0}},
    {62, {62, 67, 61, 60, 64, 63, 110, 0}},
    {63, {63, 110, 67, 60, 64, 62, 61, 0}},
    {64, {64, 110, 67, 60, 62, 63, 61, 0}},
    {67, {67, 110, 62, 63, 61, 64, 60, 0}},
    {75, {0}},
    {77, {0}},
    {80, {0}},
    {81, {81, 36, 31, 35, 50, 22, 33, 51, 21, 32, 23, 24, 25, 30, 52, 26, 27, 29, 34, 0}},
    {82, {0}},
    {110, {110, 63, 67, 64, 60, 62, 61, 0}},
    {111, {34, 36, 31, 35, 22, 33, 21, 32, 23, 24, 25, 30, 26, 27, 29, 0}},
    {112, {34, 36, 31, 35, 22, 33, 21, 32, 23, 24, 25, 30, 26, 27, 29, 0}},
    {113, {36, 21, 31, 32, 35, 25, 26, 29, 22, 33, 23, 24, 30, 27, 34, 28, 0}},
    {114, {34, 36, 31, 35, 22, 33, 21, 32, 23, 24, 25, 30, 26, 27, 29, 0}},
    {115, {34, 36, 31, 35, 22, 33, 21, 32, 23, 24, 25, 30, 26, 27, 29, 0}},
    {116, {36, 21, 31, 32, 35, 25, 26, 29, 22, 33, 23, 24, 30, 27, 34, 28, 0}},
    {117, {117, 62, 67, 61, 60, 64, 63, 110, 0}},
    {FCC('D', 'X', 'T', '1'), {FCC('D', 'X', 'T', '1'), 21, 31, 32, 35, 36, 25, 26, 29, 23, 22, 33, 24, 30, 27, 34, 28, 0}},
    {FCC('D', 'X', 'T', '2'), {FCC('D', 'X', 'T', '2'), 21, 32, 36, 31, 35, 25, 26, 29, 23, 22, 33, 24, 30, 27, 34, 28, 0}},
    {FCC('Y', 'U', 'Y', '2'), {FCC('Y', 'U', 'Y', '2'), 22, 33, 21, 32, 31, 35, 36, 23, 24, 25, 30, 26, 27, 29, 34, 0}},
    {FCC('D', 'X', 'T', '3'), {FCC('D', 'X', 'T', '3'), 21, 32, 36, 31, 35, 25, 26, 29, 23, 22, 33, 24, 30, 27, 34, 28, 0}},
    {FCC('D', 'X', 'T', '4'), {FCC('D', 'X', 'T', '4'), 21, 32, 36, 31, 35, 25, 26, 29, 23, 22, 33, 24, 30, 27, 34, 28, 0}},
    {FCC('D', 'X', 'T', '5'), {FCC('D', 'X', 'T', '5'), 21, 32, 36, 31, 35, 25, 26, 29, 23, 22, 33, 24, 30, 27, 34, 28, 0}},
    {FCC('G', 'R', 'G', 'B'), {FCC('G', 'R', 'G', 'B'), 22, 33, 21, 32, 31, 35, 36, 23, 24, 25, 30, 26, 27, 29, 34, 0}},
    {FCC('R', 'G', 'B', 'G'), {FCC('R', 'G', 'B', 'G'), 22, 33, 21, 32, 31, 35, 36, 23, 24, 25, 30, 26, 27, 29, 34, 0}},
    {FCC('U', 'Y', 'V', 'Y'), {FCC('U', 'Y', 'V', 'Y'), 22, 33, 21, 32, 31, 35, 36, 23, 24, 25, 30, 26, 27, 29, 34, 0}},
};

// ---- a decoded image: levels of one format

struct Level
{
    UINT width, height;
    std::vector<unsigned char> data; // rows without padding
};

struct Image
{
    D3DXIMAGE_INFO info;
    const Format *format;
    std::vector<Level> levels;
};

DWORD Read32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (DWORD)p[3] << 24; }

bool ParseDds(const unsigned char *p, UINT size, Image &img)
{
    if (size < 128 || Read32(p) != MAKEFOURCC('D', 'D', 'S', ' ') || Read32(p + 4) != 124)
        return false;
    UINT height = Read32(p + 12), width = Read32(p + 16), mips = Read32(p + 28);
    DWORD pfFlags = Read32(p + 80), fourcc = Read32(p + 84), bits = Read32(p + 88);
    DWORD rm = Read32(p + 92), gm = Read32(p + 96), bm = Read32(p + 100), am = Read32(p + 104);
    DWORD caps2 = Read32(p + 112);
    if (caps2 & 0x200 || caps2 & 0x200000)
        return false; // cube and volume maps: the game has none
    const Format *f = NULL;
    if (pfFlags & 4)
        f = Find((D3DFORMAT)fourcc);
    else
        for (const Format &x : kFormats)
            if (x.kind != K_DXT && x.bits == bits && x.r == rm && x.g == gm && x.b == bm &&
                x.a == ((pfFlags & 1) ? am : 0) && ((pfFlags & 0x20000) != 0) == (x.kind == K_LUM))
                f = &x;
    if (!f || width == 0 || height == 0)
        return false;
    if (mips == 0)
        mips = 1;
    img.format = f;
    UINT pos = 128, w = width, h = height;
    for (UINT i = 0; i < mips; i++)
    {
        UINT n = RowBytes(f, w) * Rows(f, h);
        if (pos + n > size)
            return false;
        Level l;
        l.width = w;
        l.height = h;
        l.data.assign(p + pos, p + pos + n);
        img.levels.push_back(l);
        pos += n;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    img.info.Width = width;
    img.info.Height = height;
    img.info.Depth = 1;
    img.info.MipLevels = mips;
    img.info.Format = f->format;
    img.info.ResourceType = D3DRTYPE_TEXTURE;
    img.info.ImageFileFormat = D3DXIFF_DDS;
    return true;
}

bool ParseTga(const unsigned char *p, UINT size, Image &img)
{
    if (size < 18)
        return false;
    UINT idLength = p[0], mapType = p[1], type = p[2];
    UINT width = p[12] | p[13] << 8, height = p[14] | p[15] << 8, bits = p[16], desc = p[17];
    if (mapType != 0 || !(type == 2 || type == 3 || type == 10 || type == 11) || width == 0 || height == 0)
        return false;
    const Format *f;
    if (type == 3 || type == 11)
        f = bits == 8 ? Find(D3DFMT_L8) : NULL;
    else if (bits == 15 || bits == 16)
        f = Find((desc & 15) ? D3DFMT_A1R5G5B5 : D3DFMT_X1R5G5B5);
    else if (bits == 24)
        f = Find(D3DFMT_R8G8B8);
    else if (bits == 32)
        f = Find((desc & 15) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8);
    else
        f = NULL;
    if (!f)
        return false;
    UINT bpp = (bits + 7) / 8, pos = 18 + idLength, n = width * height;
    std::vector<unsigned char> pixels(n * bpp);
    if (type == 2 || type == 3)
    {
        if (pos + n * bpp > size)
            return false;
        memcpy(pixels.data(), p + pos, n * bpp);
    }
    else
    {
        UINT i = 0;
        while (i < n)
        {
            if (pos >= size)
                return false;
            UINT head = p[pos++], count = (head & 0x7f) + 1;
            if (i + count > n)
                return false;
            if (head & 0x80)
            {
                if (pos + bpp > size)
                    return false;
                for (UINT k = 0; k < count; k++)
                    memcpy(&pixels[(i + k) * bpp], p + pos, bpp);
                pos += bpp;
            }
            else
            {
                if (pos + count * bpp > size)
                    return false;
                memcpy(&pixels[i * bpp], p + pos, count * bpp);
                pos += count * bpp;
            }
            i += count;
        }
    }
    Level l;
    l.width = width;
    l.height = height;
    l.data.resize(n * (f->bits / 8));
    for (UINT y = 0; y < height; y++)
    {
        UINT sy = (desc & 0x20) ? y : height - 1 - y;
        for (UINT x = 0; x < width; x++)
        {
            UINT sx = (desc & 0x10) ? width - 1 - x : x;
            memcpy(&l.data[(y * width + x) * (f->bits / 8)], &pixels[(sy * width + sx) * bpp], f->bits / 8);
        }
    }
    img.format = f;
    img.levels.push_back(l);
    img.info.Width = width;
    img.info.Height = height;
    img.info.Depth = 1;
    img.info.MipLevels = 1;
    img.info.Format = f->format;
    img.info.ResourceType = D3DRTYPE_TEXTURE;
    img.info.ImageFileFormat = D3DXIFF_TGA;
    return true;
}

// ---- pixels as floats

struct Color
{
    float r, g, b, a;
};

float Channel(DWORD v, DWORD mask)
{
    if (!mask)
        return 0.0f;
    DWORD shift = 0;
    while (!(mask >> shift & 1))
        shift++;
    DWORD max = mask >> shift;
    return (float)((v & mask) >> shift) * (1.0f / (float)max);
}

DWORD Pack(float v, DWORD mask)
{
    if (!mask)
        return 0;
    DWORD shift = 0;
    while (!(mask >> shift & 1))
        shift++;
    DWORD max = mask >> shift;
    if (v <= 0.0f)
        return 0;
    if (v >= 1.0f)
        return mask;
    return ((DWORD)(v * (float)max + 0.5f) << shift) & mask;
}

void DecodeDxtColors(const unsigned char *b, bool dxt1, Color out[4])
{
    unsigned c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
    for (int k = 0; k < 2; k++)
    {
        unsigned c = k ? c1 : c0;
        // a reciprocal multiply, as retail: c / 31 rounds differently in a few 3-colour midpoints
        out[k].r = (float)(c >> 11) * (1.0f / 31.0f);
        out[k].g = (float)(c >> 5 & 63) * (1.0f / 63.0f);
        out[k].b = (float)(c & 31) * (1.0f / 31.0f);
        out[k].a = 1.0f;
    }
    // retail decodes every DXT format's colour block with DXT1's rule: c0 <= c1 is the 3-colour mode
    (void)dxt1;
    if (c0 > c1)
    {
        out[2] = {(2 * out[0].r + out[1].r) / 3, (2 * out[0].g + out[1].g) / 3, (2 * out[0].b + out[1].b) / 3, 1.0f};
        out[3] = {(out[0].r + 2 * out[1].r) / 3, (out[0].g + 2 * out[1].g) / 3, (out[0].b + 2 * out[1].b) / 3, 1.0f};
    }
    else
    {
        out[2] = {(out[0].r + out[1].r) / 2, (out[0].g + out[1].g) / 2, (out[0].b + out[1].b) / 2, 1.0f};
        out[3] = {0.0f, 0.0f, 0.0f, 0.0f};
    }
}

// Decodes a level into floats, one Color per pixel.
void Decode(const Format *f, const Level &l, std::vector<Color> &out)
{
    out.assign(l.width * l.height, Color());
    if (f->kind == K_DXT)
    {
        UINT bw = (l.width + 3) / 4, bh = (l.height + 3) / 4, bs = f->bits / 8;
        for (UINT by = 0; by < bh; by++)
            for (UINT bx = 0; bx < bw; bx++)
            {
                const unsigned char *b = &l.data[(by * bw + bx) * bs];
                const unsigned char *cb = f->format == D3DFMT_DXT1 ? b : b + 8;
                Color pal[4];
                DecodeDxtColors(cb, f->format == D3DFMT_DXT1, pal);
                DWORD idx = Read32(cb + 4);
                for (UINT py = 0; py < 4; py++)
                    for (UINT px = 0; px < 4; px++)
                    {
                        UINT x = bx * 4 + px, y = by * 4 + py;
                        if (x >= l.width || y >= l.height)
                            continue;
                        Color c = pal[idx >> (2 * (py * 4 + px)) & 3];
                        if (f->format == D3DFMT_DXT2 || f->format == D3DFMT_DXT3)
                            c.a = (float)(b[(py * 4 + px) / 2] >> (4 * (px & 1)) & 15) * (1.0f / 15.0f);
                        else if (f->format == D3DFMT_DXT4 || f->format == D3DFMT_DXT5)
                        {
                            float a0 = b[0] * (1.0f / 255.0f), a1 = b[1] * (1.0f / 255.0f), ap[8];
                            ap[0] = a0;
                            ap[1] = a1;
                            for (int k = 2; k < 8; k++)
                                ap[k] = b[0] > b[1] ? ((8 - k) * a0 + (k - 1) * a1) / 7.0f
                                        : k < 6     ? ((6 - k) * a0 + (k - 1) * a1) / 5.0f
                                        : k == 6    ? 0.0f
                                                    : 1.0f;
                            unsigned long long bits = 0;
                            for (int k = 0; k < 6; k++)
                                bits |= (unsigned long long)b[2 + k] << (8 * k);
                            c.a = ap[bits >> (3 * (py * 4 + px)) & 7];
                        }
                        // DXT2 and DXT4 store colour premultiplied by alpha
                        if (f->format == D3DFMT_DXT2 || f->format == D3DFMT_DXT4)
                        {
                            if (c.a > 0.0f)
                            {
                                float r = 1.0f / c.a;
                                c.r *= r;
                                c.g *= r;
                                c.b *= r;
                            }
                            else
                                c.r = c.g = c.b = 0.0f;
                        }
                        out[y * l.width + x] = c;
                    }
            }
        return;
    }
    UINT bpp = f->bits / 8;
    for (UINT i = 0; i < l.width * l.height; i++)
    {
        DWORD v = 0;
        for (UINT k = 0; k < bpp; k++)
            v |= (DWORD)l.data[i * bpp + k] << (8 * k);
        Color c;
        if (f->kind == K_LUM)
            c.r = c.g = c.b = Channel(v, f->r);
        else
        {
            c.r = Channel(v, f->r);
            c.g = Channel(v, f->g);
            c.b = Channel(v, f->b);
        }
        c.a = f->a ? Channel(v, f->a) : 1.0f;
        out[i] = c;
    }
}

bool Encode(const Format *f, UINT w, UINT h, const std::vector<Color> &in, unsigned char *dst, UINT pitch)
{
    if (f->kind == K_DXT)
    {
        // retail's encoder diffuses error in colour and alpha and weights colour by luminance (bc.cpp)
        const unsigned flags = bc::DITHER_RGB | bc::DITHER_A;
        UINT bw = (w + 3) / 4, bh = (h + 3) / 4, bs = f->bits / 8;
        for (UINT by = 0; by < bh; by++)
            for (UINT bx = 0; bx < bw; bx++)
            {
                bc::Color px[16];
                for (UINT k = 0; k < 16; k++)
                {
                    // a level smaller than a block repeats its pixels across the block
                    const Color &c = in[((by * 4 + k / 4) % h) * w + (bx * 4 + k % 4) % w];
                    px[k] = {c.r, c.g, c.b, c.a};
                    if (f->format == D3DFMT_DXT2 || f->format == D3DFMT_DXT4)
                    {
                        px[k].r *= c.a;
                        px[k].g *= c.a;
                        px[k].b *= c.a;
                    }
                }
                unsigned char *blk = dst + by * pitch + bx * bs;
                if (f->format == D3DFMT_DXT1)
                    bc::EncodeDXT1(blk, px, 0.5f, flags);
                else if (f->format == D3DFMT_DXT2 || f->format == D3DFMT_DXT3)
                    bc::EncodeDXT3(blk, px, flags);
                else
                    bc::EncodeDXT5(blk, px, flags);
            }
        return true;
    }
    UINT bpp = f->bits / 8;
    for (UINT y = 0; y < h; y++)
        for (UINT x = 0; x < w; x++)
        {
            const Color &c = in[y * w + x];
            DWORD v;
            if (f->kind == K_LUM)
                v = Pack(c.r * 0.2125f + c.g * 0.7154f + c.b * 0.0721f, f->r) | Pack(c.a, f->a);
            else
                v = Pack(c.r, f->r) | Pack(c.g, f->g) | Pack(c.b, f->b) | Pack(c.a, f->a);
            for (UINT k = 0; k < bpp; k++)
                dst[y * pitch + x * bpp + k] = (unsigned char)(v >> (8 * k));
        }
    return true;
}

// Point and box resampling of a float image.
void Resample(const std::vector<Color> &src, UINT sw, UINT sh, std::vector<Color> &dst, UINT dw, UINT dh, DWORD filter)
{
    dst.assign(dw * dh, Color());
    DWORD type = filter & 0xffff;
    if (type == D3DX_FILTER_BOX && dw * 2 == sw && dh * 2 == sh)
    {
        for (UINT y = 0; y < dh; y++)
            for (UINT x = 0; x < dw; x++)
            {
                const Color &a = src[(2 * y) * sw + 2 * x], &b = src[(2 * y) * sw + 2 * x + 1];
                const Color &c = src[(2 * y + 1) * sw + 2 * x], &d = src[(2 * y + 1) * sw + 2 * x + 1];
                dst[y * dw + x] = {(a.r + b.r + c.r + d.r) * 0.25f, (a.g + b.g + c.g + d.g) * 0.25f,
                                   (a.b + b.b + c.b + d.b) * 0.25f, (a.a + b.a + c.a + d.a) * 0.25f};
            }
        return;
    }
    for (UINT y = 0; y < dh; y++)
        for (UINT x = 0; x < dw; x++)
            dst[y * dw + x] = src[(y * sh / dh) * sw + x * sw / dw];
}

// ---- requirements

bool Supported(IDirect3D9 *d3d, const D3DDEVICE_CREATION_PARAMETERS &cp, D3DFORMAT adapter, DWORD usage, D3DFORMAT f)
{
    return SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, adapter, usage, D3DRTYPE_TEXTURE, f));
}

HRESULT Requirements(IDirect3DDevice9 *device, UINT &w, UINT &h, UINT &levels, DWORD usage, D3DFORMAT &format)
{
    D3DCAPS9 caps;
    D3DDEVICE_CREATION_PARAMETERS cp;
    D3DDISPLAYMODE mode;
    IDirect3D9 *d3d = NULL;
    if (FAILED(device->GetDeviceCaps(&caps)) || FAILED(device->GetCreationParameters(&cp)) ||
        FAILED(device->GetDisplayMode(0, &mode)) || FAILED(device->GetDirect3D(&d3d)))
        return D3DERR_INVALIDCALL;
    D3DFORMAT chosen = D3DFMT_UNKNOWN;
    for (const Preference &p : kPreferences)
        if (p.requested == (DWORD)format)
        {
            for (const DWORD *f = p.order; *f && chosen == D3DFMT_UNKNOWN; f++)
                if (Supported(d3d, cp, mode.Format, usage, (D3DFORMAT)*f))
                    chosen = (D3DFORMAT)*f;
            break;
        }
    d3d->Release();
    if (chosen == D3DFMT_UNKNOWN)
        return D3DERR_NOTAVAILABLE;
    format = chosen;
    if (caps.TextureCaps & D3DPTEXTURECAPS_POW2)
    {
        UINT p = 1;
        while (p < w)
            p <<= 1;
        w = p;
        for (p = 1; p < h; p <<= 1)
            ;
        h = p;
    }
    if (w > caps.MaxTextureWidth)
        w = caps.MaxTextureWidth;
    if (h > caps.MaxTextureHeight)
        h = caps.MaxTextureHeight;
    UINT max = 1;
    for (UINT x = w, y = h; x > 1 || y > 1; x = x > 1 ? x / 2 : 1, y = y > 1 ? y / 2 : 1)
        max++;
    if (levels == 0 || levels == D3DX_DEFAULT || levels > max)
        levels = max;
    return D3D_OK;
}

// Writes one level: raw bytes, or pixels encoded into the level's format (returned in written, rows packed, so the
// next mip filters from the level as stored, as retail's does).
HRESULT LoadLevel(IDirect3DTexture9 *tex, UINT level, const Format *df, UINT dw, UINT dh, const std::vector<Color> *pixels,
                  const Level *raw, Level &written)
{
    UINT rb = RowBytes(df, dw), rows = Rows(df, dh);
    written.width = dw;
    written.height = dh;
    if (raw)
        written.data = raw->data;
    else
    {
        written.data.assign(rb * rows, 0);
        if (!Encode(df, dw, dh, *pixels, written.data.data(), rb))
            return E_NOTIMPL;
    }
    D3DLOCKED_RECT lr;
    HRESULT hr = tex->LockRect(level, &lr, NULL, 0);
    if (FAILED(hr))
        return hr;
    for (UINT r = 0; r < rows; r++)
        memcpy((unsigned char *)lr.pBits + r * lr.Pitch, &written.data[r * rb], rb);
    tex->UnlockRect(level);
    return D3D_OK;
}

HRESULT Create(IDirect3DDevice9 *device, const Image &img, UINT width, UINT height, UINT mips, DWORD usage,
               D3DFORMAT format, D3DPOOL pool, DWORD filter, DWORD mipFilter, D3DXIMAGE_INFO *pSrcInfo,
               IDirect3DTexture9 **ppTexture)
{
    if (pSrcInfo)
        *pSrcInfo = img.info;
    UINT w = (width == 0 || width == D3DX_DEFAULT) ? img.info.Width : width;
    UINT h = (height == 0 || height == D3DX_DEFAULT) ? img.info.Height : height;
    D3DFORMAT f = format == D3DFMT_UNKNOWN ? img.info.Format : format;
    HRESULT hr = Requirements(device, w, h, mips, usage, f);
    if (FAILED(hr))
        return hr;
    const Format *df = Find(f);
    if (!df)
        return D3DERR_NOTAVAILABLE;
    IDirect3DTexture9 *tex = NULL, *staging = NULL;
    hr = device->CreateTexture(w, h, mips, usage, f, pool, &tex, NULL);
    if (FAILED(hr))
        return hr;
    IDirect3DTexture9 *target = tex;
    if (pool == D3DPOOL_DEFAULT)
    {
        hr = device->CreateTexture(w, h, mips, 0, f, D3DPOOL_SYSTEMMEM, &staging, NULL);
        if (FAILED(hr))
        {
            tex->Release();
            return hr;
        }
        target = staging;
    }
    if (filter == D3DX_DEFAULT)
        filter = D3DX_FILTER_TRIANGLE | D3DX_FILTER_DITHER;
    if (mipFilter == D3DX_DEFAULT)
        mipFilter = D3DX_FILTER_BOX;
    std::vector<Color> src, next;
    Level prev;
    UINT cw = w, ch = h;
    for (UINT i = 0; i < mips && SUCCEEDED(hr); i++)
    {
        Level written;
        // Retail copies a stored level, but decodes and re-encodes a DXT level narrower or shorter than a block.
        if (i < img.levels.size() && img.levels[i].width == cw && img.levels[i].height == ch && img.format == df &&
            (df->kind != K_DXT || (cw >= 4 && ch >= 4)))
            hr = LoadLevel(target, i, df, cw, ch, NULL, &img.levels[i], written);
        else
        {
            if (i < img.levels.size() && img.levels[i].width == cw && img.levels[i].height == ch)
            {
                Decode(img.format, img.levels[i], next);
            }
            else if (i < img.levels.size() && (i == 0 || img.levels[i].width == cw))
            {
                Decode(img.format, img.levels[i], src);
                Resample(src, img.levels[i].width, img.levels[i].height, next, cw, ch, i == 0 ? filter : mipFilter);
            }
            else
            {
                Decode(df, prev, src);
                Resample(src, prev.width, prev.height, next, cw, ch, mipFilter);
            }
            hr = LoadLevel(target, i, df, cw, ch, &next, NULL, written);
        }
        prev.width = written.width;
        prev.height = written.height;
        prev.data.swap(written.data);
        cw = cw > 1 ? cw / 2 : 1;
        ch = ch > 1 ? ch / 2 : 1;
    }
    if (SUCCEEDED(hr) && staging)
        hr = device->UpdateTexture(staging, tex);
    if (staging)
        staging->Release();
    if (FAILED(hr))
    {
        tex->Release();
        return hr;
    }
    *ppTexture = tex;
    return D3D_OK;
}

} // namespace

extern "C" HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(LPDIRECT3DDEVICE9 pDevice, LPCVOID pSrcData,
    UINT SrcDataSize, UINT Width, UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    DWORD Filter, DWORD MipFilter, D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette,
    LPDIRECT3DTEXTURE9 *ppTexture)
{
    (void)ColorKey;
    (void)pPalette;
    if (!pDevice || !pSrcData || !ppTexture)
        return D3DERR_INVALIDCALL;
    *ppTexture = NULL;
    Image img;
    const unsigned char *p = (const unsigned char *)pSrcData;
    if (!ParseDds(p, SrcDataSize, img) && !ParseTga(p, SrcDataSize, img))
        return D3DXERR_INVALIDDATA;
    return Create(pDevice, img, Width, Height, MipLevels, Usage, Format, Pool, Filter, MipFilter, pSrcInfo, ppTexture);
}

extern "C" HRESULT WINAPI D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE9 pDevice, LPCSTR pSrcFile, UINT Width,
    UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter,
    D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE9 *ppTexture)
{
    if (!pSrcFile)
        return D3DERR_INVALIDCALL;
    FILE *f = fopen(pSrcFile, "rb");
    if (!f)
        return D3DXERR_INVALIDDATA;
    std::vector<unsigned char> data;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.insert(data.end(), buf, buf + n);
    fclose(f);
    return D3DXCreateTextureFromFileInMemoryEx(pDevice, data.data(), (UINT)data.size(), Width, Height, MipLevels,
                                               Usage, Format, Pool, Filter, MipFilter, ColorKey, pSrcInfo, pPalette,
                                               ppTexture);
}

extern "C" HRESULT WINAPI D3DXSaveSurfaceToFileA(LPCSTR pDestFile, D3DXIMAGE_FILEFORMAT DestFormat,
    LPDIRECT3DSURFACE9 pSrcSurface, CONST PALETTEENTRY *pSrcPalette, CONST RECT *pSrcRect)
{
    // Only the debug screen dump (DebugLog.cpp) calls it; the port does not write image files.
    (void)pDestFile;
    (void)DestFormat;
    (void)pSrcSurface;
    (void)pSrcPalette;
    (void)pSrcRect;
    return D3DERR_NOTAVAILABLE;
}

extern "C" HRESULT WINAPI D3DXLoadSurfaceFromFileA(LPDIRECT3DSURFACE9 pDestSurface, CONST PALETTEENTRY *pDestPalette,
    CONST RECT *pDestRect, LPCSTR pSrcFile, CONST RECT *pSrcRect, DWORD Filter, D3DCOLOR ColorKey,
    D3DXIMAGE_INFO *pSrcInfo)
{
    // As D3DXSaveSurfaceToFileA: the debug screen dump only.
    (void)pDestSurface;
    (void)pDestPalette;
    (void)pDestRect;
    (void)pSrcFile;
    (void)pSrcRect;
    (void)Filter;
    (void)ColorKey;
    (void)pSrcInfo;
    return D3DERR_NOTAVAILABLE;
}
