// SPDX-License-Identifier: GPL-3.0-only AND MIT
// DXT1 to DXT5 block encoding for the texture loader. Adapted from DirectXTex (https://github.com/microsoft/DirectXTex,
// commit 1acf4ebe6330e06b7eb52e8db63d4f824ad77427): DirectXTex/BC.cpp (SHA-256
// 7554b64224f39e68ff99f9b070394dfddee811593a1b3d7c8004c5c2f94a47b0) and the OptimizeAlpha template in DirectXTex/BC.h
// (SHA-256 498e86defa9e46f207a54aff2fbda814467c2d6d608e9d678ffff5e1cd142a49), which descend from D3DX's encoder:
//
//   Copyright (c) Microsoft Corporation.
//   MIT License. Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
//   associated documentation files (the "Software"), to deal in the Software without restriction, including without
//   limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
//   Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//   The above copyright notice and this permission notice shall be included in all copies or substantial portions of
//   the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
//   NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
//   EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
//   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
//   OTHER DEALINGS IN THE SOFTWARE.
//
// Changes for the port, each measured against retail d3dx9 (port/d3dx/README.md): the alpha optimiser takes its
// difference the way round D3DX did (DirectXTex reversed it); the port always dithers colour and alpha and weights by
// luminance, as retail's results require. Retail's encoder runs on the x87 (its output follows the precision
// control), so this file builds in the x87 class like the game: at the game's 24-bit precision it matches retail in
// 3,805 of 4,000 random blocks; the rest differ by one endpoint step where retail's own arithmetic order lands an
// unquantised endpoint a few ulps the other side of a half.
#include "bc.h"
#include <string.h>

namespace bc {

namespace {

const Color kLuminance = {0.2125f / 0.7154f, 1.0f, 0.0721f / 0.7154f, 1.0f};
const Color kLuminanceInv = {0.7154f / 0.2125f, 1.0f, 0.7154f / 0.0721f, 1.0f};
const float kFltMin = 1.175494351e-38f;

void Decode565(Color *c, unsigned short w)
{
    c->r = (float)((w >> 11) & 31) * (1.0f / 31.0f);
    c->g = (float)((w >> 5) & 63) * (1.0f / 63.0f);
    c->b = (float)(w & 31) * (1.0f / 31.0f);
    c->a = 1.0f;
}

unsigned short Encode565(const Color *c)
{
    float r = c->r < 0.0f ? 0.0f : c->r > 1.0f ? 1.0f : c->r;
    float g = c->g < 0.0f ? 0.0f : c->g > 1.0f ? 1.0f : c->g;
    float b = c->b < 0.0f ? 0.0f : c->b > 1.0f ? 1.0f : c->b;
    return (unsigned short)(((int)(r * 31.0f + 0.5f) << 11) | ((int)(g * 63.0f + 0.5f) << 5) | (int)(b * 31.0f + 0.5f));
}

void Lerp(Color *out, const Color *a, const Color *b, float s)
{
    out->r = a->r + s * (b->r - a->r);
    out->g = a->g + s * (b->g - a->g);
    out->b = a->b + s * (b->b - a->b);
    out->a = a->a + s * (b->a - a->a);
}

// Floyd-Steinberg weights within the 4x4 block.
template <typename T> void Diffuse(T *err, size_t i, const T &diff)
{
    if (3 != (i & 3))
        err[i + 1] += diff * (7.0f / 16.0f);
    if (i < 12)
    {
        if (i & 3)
            err[i + 3] += diff * (3.0f / 16.0f);
        err[i + 4] += diff * (5.0f / 16.0f);
        if (3 != (i & 3))
            err[i + 5] += diff * (1.0f / 16.0f);
    }
}

struct Rgb
{
    float r, g, b;
    Rgb &operator+=(const Rgb &o)
    {
        r += o.r;
        g += o.g;
        b += o.b;
        return *this;
    }
    Rgb operator*(float s) const { return {r * s, g * s, b * s}; }
};

void OptimizeRGB(Color *pX, Color *pY, const Color *pts, unsigned steps, unsigned flags)
{
    const float eps = (0.25f / 64.0f) * (0.25f / 64.0f);
    static const float pC3[] = {2.0f / 2.0f, 1.0f / 2.0f, 0.0f / 2.0f};
    static const float pD3[] = {0.0f / 2.0f, 1.0f / 2.0f, 2.0f / 2.0f};
    static const float pC4[] = {3.0f / 3.0f, 2.0f / 3.0f, 1.0f / 3.0f, 0.0f / 3.0f};
    static const float pD4[] = {0.0f / 3.0f, 1.0f / 3.0f, 2.0f / 3.0f, 3.0f / 3.0f};
    const float *pC = steps == 3 ? pC3 : pC4, *pD = steps == 3 ? pD3 : pD4;

    Color X = (flags & UNIFORM) ? Color{1.0f, 1.0f, 1.0f, 1.0f} : kLuminance;
    Color Y = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 16; i++)
    {
        if (pts[i].r < X.r) X.r = pts[i].r;
        if (pts[i].g < X.g) X.g = pts[i].g;
        if (pts[i].b < X.b) X.b = pts[i].b;
        if (pts[i].r > Y.r) Y.r = pts[i].r;
        if (pts[i].g > Y.g) Y.g = pts[i].g;
        if (pts[i].b > Y.b) Y.b = pts[i].b;
    }
    Color AB = {Y.r - X.r, Y.g - X.g, Y.b - X.b, 0.0f};
    float fAB = AB.r * AB.r + AB.g * AB.g + AB.b * AB.b;
    if (fAB < kFltMin)
    {
        *pX = {X.r, X.g, X.b, 1.0f};
        *pY = {Y.r, Y.g, Y.b, 1.0f};
        return;
    }
    float fABInv = 1.0f / fAB;
    Color Dir = {AB.r * fABInv, AB.g * fABInv, AB.b * fABInv, 0.0f};
    Color Mid = {(X.r + Y.r) * 0.5f, (X.g + Y.g) * 0.5f, (X.b + Y.b) * 0.5f, 0.0f};
    float fDir[4] = {0, 0, 0, 0};
    for (int i = 0; i < 16; i++)
    {
        float pr = (pts[i].r - Mid.r) * Dir.r, pg = (pts[i].g - Mid.g) * Dir.g, pb = (pts[i].b - Mid.b) * Dir.b;
        float f;
        f = pr + pg + pb;
        fDir[0] += f * f;
        f = pr + pg - pb;
        fDir[1] += f * f;
        f = pr - pg + pb;
        fDir[2] += f * f;
        f = pr - pg - pb;
        fDir[3] += f * f;
    }
    float fDirMax = fDir[0];
    int iDirMax = 0;
    for (int d = 1; d < 4; d++)
        if (fDir[d] > fDirMax)
        {
            fDirMax = fDir[d];
            iDirMax = d;
        }
    if (iDirMax & 2)
    {
        float f = X.g;
        X.g = Y.g;
        Y.g = f;
    }
    if (iDirMax & 1)
    {
        float f = X.b;
        X.b = Y.b;
        Y.b = f;
    }
    if (fAB < 1.0f / 4096.0f)
    {
        *pX = {X.r, X.g, X.b, 1.0f};
        *pY = {Y.r, Y.g, Y.b, 1.0f};
        return;
    }
    float fSteps = (float)(steps - 1);
    for (int it = 0; it < 8; it++)
    {
        Color S[4];
        for (unsigned s = 0; s < steps; s++)
            S[s] = {X.r * pC[s] + Y.r * pD[s], X.g * pC[s] + Y.g * pD[s], X.b * pC[s] + Y.b * pD[s], 1.0f};
        Dir = {Y.r - X.r, Y.g - X.g, Y.b - X.b, 0.0f};
        float fLen = Dir.r * Dir.r + Dir.g * Dir.g + Dir.b * Dir.b;
        if (fLen < 1.0f / 4096.0f)
            break;
        float fScale = fSteps / fLen;
        Dir.r *= fScale;
        Dir.g *= fScale;
        Dir.b *= fScale;
        float d2X = 0, d2Y = 0;
        Color dX = {0, 0, 0, 0}, dY = {0, 0, 0, 0};
        for (int i = 0; i < 16; i++)
        {
            float fDot = (pts[i].r - X.r) * Dir.r + (pts[i].g - X.g) * Dir.g + (pts[i].b - X.b) * Dir.b;
            unsigned s = fDot <= 0.0f ? 0 : fDot >= fSteps ? steps - 1 : (unsigned)(fDot + 0.5f);
            Color diff = {S[s].r - pts[i].r, S[s].g - pts[i].g, S[s].b - pts[i].b, 0.0f};
            float fC = pC[s] * (1.0f / 8.0f), fD = pD[s] * (1.0f / 8.0f);
            d2X += fC * pC[s];
            dX.r += fC * diff.r;
            dX.g += fC * diff.g;
            dX.b += fC * diff.b;
            d2Y += fD * pD[s];
            dY.r += fD * diff.r;
            dY.g += fD * diff.g;
            dY.b += fD * diff.b;
        }
        if (d2X > 0.0f)
        {
            float f = -1.0f / d2X;
            X.r += dX.r * f;
            X.g += dX.g * f;
            X.b += dX.b * f;
        }
        if (d2Y > 0.0f)
        {
            float f = -1.0f / d2Y;
            Y.r += dY.r * f;
            Y.g += dY.g * f;
            Y.b += dY.b * f;
        }
        if (dX.r * dX.r < eps && dX.g * dX.g < eps && dX.b * dX.b < eps && dY.r * dY.r < eps && dY.g * dY.g < eps &&
            dY.b * dY.b < eps)
            break;
    }
    *pX = {X.r, X.g, X.b, 1.0f};
    *pY = {Y.r, Y.g, Y.b, 1.0f};
}

void EncodeRGB(unsigned char *out, const Color *pColor, bool colorKey, float threshold, unsigned flags)
{
    unsigned steps;
    if (colorKey)
    {
        int keyed = 0;
        for (int i = 0; i < 16; i++)
            if (pColor[i].a < threshold)
                keyed++;
        if (keyed == 16)
        {
            const unsigned char solid[8] = {0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
            memcpy(out, solid, 8);
            return;
        }
        steps = keyed > 0 ? 3 : 4;
    }
    else
        steps = 4;

    Color C[16];
    Rgb err[16];
    memset(err, 0, sizeof(err));
    for (size_t i = 0; i < 16; i++)
    {
        Rgb clr = {pColor[i].r, pColor[i].g, pColor[i].b};
        if (flags & DITHER_RGB)
            clr += err[i];
        C[i].r = (float)(int)(clr.r * 31.0f + 0.5f) * (1.0f / 31.0f);
        C[i].g = (float)(int)(clr.g * 63.0f + 0.5f) * (1.0f / 63.0f);
        C[i].b = (float)(int)(clr.b * 31.0f + 0.5f) * (1.0f / 31.0f);
        C[i].a = 1.0f;
        if (flags & DITHER_RGB)
            Diffuse(err, i, Rgb{C[i].a * (clr.r - C[i].r), C[i].a * (clr.g - C[i].g), C[i].a * (clr.b - C[i].b)});
        if (!(flags & UNIFORM))
        {
            C[i].r *= kLuminance.r;
            C[i].g *= kLuminance.g;
            C[i].b *= kLuminance.b;
        }
    }

    Color A, B, Cc, D;
    OptimizeRGB(&A, &B, C, steps, flags);
    if (flags & UNIFORM)
    {
        Cc = A;
        D = B;
    }
    else
    {
        Cc = {A.r * kLuminanceInv.r, A.g * kLuminanceInv.g, A.b * kLuminanceInv.b, A.a};
        D = {B.r * kLuminanceInv.r, B.g * kLuminanceInv.g, B.b * kLuminanceInv.b, B.a};
    }
    unsigned short wA = Encode565(&Cc), wB = Encode565(&D);
    if (steps == 4 && wA == wB)
    {
        out[0] = (unsigned char)wA;
        out[1] = (unsigned char)(wA >> 8);
        out[2] = (unsigned char)wB;
        out[3] = (unsigned char)(wB >> 8);
        memset(out + 4, 0, 4);
        return;
    }
    Decode565(&Cc, wA);
    Decode565(&D, wB);
    if (flags & UNIFORM)
    {
        A = Cc;
        B = D;
    }
    else
    {
        A = {Cc.r * kLuminance.r, Cc.g * kLuminance.g, Cc.b * kLuminance.b, Cc.a};
        B = {D.r * kLuminance.r, D.g * kLuminance.g, D.b * kLuminance.b, D.a};
    }
    Color Step[4];
    unsigned short w0, w1;
    if ((steps == 3) == (wA <= wB))
    {
        w0 = wA;
        w1 = wB;
        Step[0] = A;
        Step[1] = B;
    }
    else
    {
        w0 = wB;
        w1 = wA;
        Step[0] = B;
        Step[1] = A;
    }
    static const unsigned pSteps3[] = {0, 2, 1};
    static const unsigned pSteps4[] = {0, 2, 3, 1};
    const unsigned *pSteps;
    if (steps == 3)
    {
        pSteps = pSteps3;
        Lerp(&Step[2], &Step[0], &Step[1], 0.5f);
    }
    else
    {
        pSteps = pSteps4;
        Lerp(&Step[2], &Step[0], &Step[1], 1.0f / 3.0f);
        Lerp(&Step[3], &Step[0], &Step[1], 2.0f / 3.0f);
    }
    Color Dir = {Step[1].r - Step[0].r, Step[1].g - Step[0].g, Step[1].b - Step[0].b, 0.0f};
    float fSteps = (float)(steps - 1);
    float fScale = wA != wB ? fSteps / (Dir.r * Dir.r + Dir.g * Dir.g + Dir.b * Dir.b) : 0.0f;
    Dir.r *= fScale;
    Dir.g *= fScale;
    Dir.b *= fScale;

    unsigned dw = 0;
    memset(err, 0, sizeof(err));
    for (size_t i = 0; i < 16; i++)
    {
        if (steps == 3 && pColor[i].a < threshold)
            dw = (3u << 30) | (dw >> 2);
        else
        {
            Rgb clr;
            if (flags & UNIFORM)
                clr = {pColor[i].r, pColor[i].g, pColor[i].b};
            else
                clr = {pColor[i].r * kLuminance.r, pColor[i].g * kLuminance.g, pColor[i].b * kLuminance.b};
            if (flags & DITHER_RGB)
                clr += err[i];
            float fDot = (clr.r - Step[0].r) * Dir.r + (clr.g - Step[0].g) * Dir.g + (clr.b - Step[0].b) * Dir.b;
            unsigned s = fDot <= 0.0f ? 0 : fDot >= fSteps ? 1 : pSteps[(unsigned)(fDot + 0.5f)];
            dw = (s << 30) | (dw >> 2);
            if (flags & DITHER_RGB)
                Diffuse(err, i, Rgb{C[i].a * (clr.r - Step[s].r), C[i].a * (clr.g - Step[s].g), C[i].a * (clr.b - Step[s].b)});
        }
    }
    out[0] = (unsigned char)w0;
    out[1] = (unsigned char)(w0 >> 8);
    out[2] = (unsigned char)w1;
    out[3] = (unsigned char)(w1 >> 8);
    out[4] = (unsigned char)dw;
    out[5] = (unsigned char)(dw >> 8);
    out[6] = (unsigned char)(dw >> 16);
    out[7] = (unsigned char)(dw >> 24);
}

// D3DX's alpha optimiser, with the difference taken the way round D3DX took it (DirectXTex reversed it).
void OptimizeAlpha(float *pX, float *pY, const float *pts, unsigned steps)
{
    static const float pC6[] = {5.0f / 5.0f, 4.0f / 5.0f, 3.0f / 5.0f, 2.0f / 5.0f, 1.0f / 5.0f, 0.0f / 5.0f};
    static const float pD6[] = {0.0f / 5.0f, 1.0f / 5.0f, 2.0f / 5.0f, 3.0f / 5.0f, 4.0f / 5.0f, 5.0f / 5.0f};
    static const float pC8[] = {7.0f / 7.0f, 6.0f / 7.0f, 5.0f / 7.0f, 4.0f / 7.0f,
                                3.0f / 7.0f, 2.0f / 7.0f, 1.0f / 7.0f, 0.0f / 7.0f};
    static const float pD8[] = {0.0f / 7.0f, 1.0f / 7.0f, 2.0f / 7.0f, 3.0f / 7.0f,
                                4.0f / 7.0f, 5.0f / 7.0f, 6.0f / 7.0f, 7.0f / 7.0f};
    const float *pC = steps == 6 ? pC6 : pC8, *pD = steps == 6 ? pD6 : pD8;
    float fX = 1.0f, fY = 0.0f;
    if (steps == 8)
    {
        for (int i = 0; i < 16; i++)
        {
            if (pts[i] < fX) fX = pts[i];
            if (pts[i] > fY) fY = pts[i];
        }
    }
    else
    {
        for (int i = 0; i < 16; i++)
        {
            if (pts[i] < fX && pts[i] > 0.0f) fX = pts[i];
            if (pts[i] > fY && pts[i] < 1.0f) fY = pts[i];
        }
        if (fX == fY)
            fY = 1.0f;
    }
    float fSteps = (float)(steps - 1);
    for (int it = 0; it < 8; it++)
    {
        if (fY - fX < 1.0f / 256.0f)
            break;
        float fScale = fSteps / (fY - fX);
        float S[8];
        for (unsigned s = 0; s < steps; s++)
            S[s] = pC[s] * fX + pD[s] * fY;
        if (steps == 6)
        {
            S[6] = 0.0f;
            S[7] = 1.0f;
        }
        float dX = 0, dY = 0, d2X = 0, d2Y = 0;
        for (int i = 0; i < 16; i++)
        {
            float fDot = (pts[i] - fX) * fScale;
            unsigned s;
            if (fDot <= 0.0f)
                s = (steps == 6 && pts[i] <= fX * 0.5f) ? 6u : 0u;
            else if (fDot >= fSteps)
                s = (steps == 6 && pts[i] >= (fY + 1.0f) * 0.5f) ? 7u : steps - 1;
            else
                s = (unsigned)(fDot + 0.5f);
            if (s < steps)
            {
                float fDiff = pts[i] - S[s];
                dX += pC[s] * fDiff;
                d2X += pC[s] * pC[s];
                dY += pD[s] * fDiff;
                d2Y += pD[s] * pD[s];
            }
        }
        if (d2X > 0.0f)
            fX -= dX / d2X;
        if (d2Y > 0.0f)
            fY -= dY / d2Y;
        if (fX > fY)
        {
            float f = fX;
            fX = fY;
            fY = f;
        }
        if (dX * dX < 1.0f / 64.0f && dY * dY < 1.0f / 64.0f)
            break;
    }
    *pX = fX < 0.0f ? 0.0f : fX > 1.0f ? 1.0f : fX;
    *pY = fY < 0.0f ? 0.0f : fY > 1.0f ? 1.0f : fY;
}

} // namespace

void EncodeDXT1(unsigned char *out, const Color *pixels, float threshold, unsigned flags)
{
    Color c[16];
    if (flags & DITHER_A)
    {
        float err[16] = {0};
        for (size_t i = 0; i < 16; i++)
        {
            float a = pixels[i].a + err[i];
            c[i] = pixels[i];
            c[i].a = (float)(int)(pixels[i].a + err[i] + 0.5f);
            Diffuse(err, i, a - c[i].a);
        }
    }
    else
        memcpy(c, pixels, sizeof(c));
    EncodeRGB(out, c, true, threshold, flags);
}

void EncodeDXT3(unsigned char *out, const Color *pixels, unsigned flags)
{
    unsigned bm[2] = {0, 0};
    float err[16] = {0};
    for (size_t i = 0; i < 16; i++)
    {
        float a = pixels[i].a;
        if (flags & DITHER_A)
            a += err[i];
        unsigned u = (unsigned)(a * 15.0f + 0.5f);
        bm[i >> 3] >>= 4;
        bm[i >> 3] |= u << 28;
        if (flags & DITHER_A)
            Diffuse(err, i, a - (float)u * (1.0f / 15.0f));
    }
    for (int k = 0; k < 8; k++)
        out[k] = (unsigned char)(bm[k >> 2] >> (8 * (k & 3)));
    EncodeRGB(out + 8, pixels, false, 0.0f, flags);
}

void EncodeDXT5(unsigned char *out, const Color *pixels, unsigned flags)
{
    float fAlpha[16], err[16] = {0};
    float fMin = pixels[0].a, fMax = pixels[0].a;
    for (size_t i = 0; i < 16; i++)
    {
        float a = pixels[i].a;
        if (flags & DITHER_A)
            a += err[i];
        fAlpha[i] = (float)(int)(a * 255.0f + 0.5f) * (1.0f / 255.0f);
        if (fAlpha[i] < fMin)
            fMin = fAlpha[i];
        else if (fAlpha[i] > fMax)
            fMax = fAlpha[i];
        if (flags & DITHER_A)
            Diffuse(err, i, a - fAlpha[i]);
    }
    EncodeRGB(out + 8, pixels, false, 0.0f, flags);
    if (fMin == 1.0f)
    {
        out[0] = out[1] = 0xff;
        memset(out + 2, 0, 6);
        return;
    }
    unsigned steps = (fMin == 0.0f || fMax == 1.0f) ? 6u : 8u;
    float fA, fB;
    OptimizeAlpha(&fA, &fB, fAlpha, steps);
    unsigned char bA = (unsigned char)(int)(fA * 255.0f + 0.5f), bB = (unsigned char)(int)(fB * 255.0f + 0.5f);
    fA = (float)bA * (1.0f / 255.0f);
    fB = (float)bB * (1.0f / 255.0f);
    if (steps == 8 && bA == bB)
    {
        out[0] = bA;
        out[1] = bB;
        memset(out + 2, 0, 6);
        return;
    }
    static const unsigned pSteps6[] = {0, 2, 3, 4, 5, 1};
    static const unsigned pSteps8[] = {0, 2, 3, 4, 5, 6, 7, 1};
    const unsigned *pSteps;
    float fStep[8] = {0};
    if (steps == 6)
    {
        out[0] = bA;
        out[1] = bB;
        fStep[0] = fA;
        fStep[1] = fB;
        for (int i = 1; i < 5; i++)
            fStep[i + 1] = (fStep[0] * (float)(5 - i) + fStep[1] * (float)i) * (1.0f / 5.0f);
        fStep[6] = 0.0f;
        fStep[7] = 1.0f;
        pSteps = pSteps6;
    }
    else
    {
        out[0] = bB;
        out[1] = bA;
        fStep[0] = fB;
        fStep[1] = fA;
        for (int i = 1; i < 7; i++)
            fStep[i + 1] = (fStep[0] * (float)(7 - i) + fStep[1] * (float)i) * (1.0f / 7.0f);
        pSteps = pSteps8;
    }
    float fSteps = (float)(steps - 1);
    float fScale = fStep[0] != fStep[1] ? fSteps / (fStep[1] - fStep[0]) : 0.0f;
    memset(err, 0, sizeof(err));
    for (int set = 0; set < 2; set++)
    {
        unsigned dw = 0;
        for (size_t i = set * 8; i < (size_t)set * 8 + 8; i++)
        {
            float a = pixels[i].a;
            if (flags & DITHER_A)
                a += err[i];
            float fDot = (a - fStep[0]) * fScale;
            unsigned s;
            if (fDot <= 0.0f)
                s = (steps == 6 && a <= fStep[0] * 0.5f) ? 6u : 0u;
            else if (fDot >= fSteps)
                s = (steps == 6 && a >= (fStep[1] + 1.0f) * 0.5f) ? 7u : 1u;
            else
                s = pSteps[(unsigned)(fDot + 0.5f)];
            dw = (s << 21) | (dw >> 3);
            if (flags & DITHER_A)
                Diffuse(err, i, a - fStep[s]);
        }
        out[2 + set * 3] = (unsigned char)dw;
        out[3 + set * 3] = (unsigned char)(dw >> 8);
        out[4 + set * 3] = (unsigned char)(dw >> 16);
    }
}

} // namespace bc
