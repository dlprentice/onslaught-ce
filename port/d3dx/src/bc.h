// SPDX-License-Identifier: GPL-3.0-only
// DXT block encoding (bc.cpp) for the texture loader. An independent header written for the port: its declarations
// are the port's own, not DirectXTex's BC.h (whose OptimizeAlpha bc.cpp adapts, under the notice there).
#ifndef PORT_D3DX_BC_H
#define PORT_D3DX_BC_H

namespace bc {

struct Color
{
    float r, g, b, a;
};

enum Flags
{
    DITHER_RGB = 0x10000,
    DITHER_A = 0x20000,
    UNIFORM = 0x40000,
};

// Each takes the block's 16 pixels in row order and writes one block (8 bytes for DXT1, 16 for DXT3 and DXT5).
void EncodeDXT1(unsigned char *out, const Color *pixels, float threshold, unsigned flags);
void EncodeDXT3(unsigned char *out, const Color *pixels, unsigned flags);
void EncodeDXT5(unsigned char *out, const Color *pixels, unsigned flags);

} // namespace bc

#endif
