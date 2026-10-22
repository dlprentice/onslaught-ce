#include	"Common.h"	// FUNCTION: 0x00541de0 init:ZERO_FVECTOR
						// FUNCTION: 0x00541e00 init:ID_FMATRIX

#ifdef _DIRECTX

#include	"Game.h"
#include	"Camera.h"
#include	"Engine.h"
#include	"Profile.h"
#include	"Map.h"
#include	"EventManager.h"
#include	"SoundManager.h"
#include	"debuglog.h"
#include    "console.h"
#include	"debris.h"
#include    "scheduledevent.h"
#include	"DebugText.h"
#include	"music.h"
#include	"spriterenderer.h"
#include	"renderinfo.h"
#include	"Player.h"
#include    "World.h"
#include	"CLIParams.h"
#include    "text.h"



//*******************************************************************************
CDXGame		GAME;						// 008a9a98
// FUNCTION: 0x00541ed0 init:GAME
// FUNCTION: 0x00541ef0 exit:GAME
// FUNCTION: 0x00541f10 ??0CDXGame@@QAE@XZ
// FUNCTION: 0x00541f00 ??1CDXGame@@UAE@XZ
// FUNCTION: 0x00541f30 ??_GCDXGame@@UAEPAXI@Z

// Stuart's DumpTimeRecords and AddTimeStatus are gone from the retail class: declaring them in CDXGame changes
// CGame::DrawDebugStuff's register choice (00470825), and the retail build has neither.

//*******************************************************************************

/*

	if (mFixedFrameRate)
	{
		float time = (float)timeGetTime() * 0.001f;
		if (time - mLastUpdateTime > MAX_UPDATE_TIME) 
		{
			mLastUpdateTime = time ;
			GAME.Update() ;
		}
	}
	else
	{
		GAME.Update() ;
	}

*/


//*******************************************************************************
// Open question: whether this is DXGame.obj's last function or opens an include-light object of its own before
// DXHud (it follows ??_GCDXGame at 00541f30 with no initializer or .CRT$XCU entry between, refers to no global, and
// Stuart's DXGame.cpp has no such code).
#include	"maptex.h"

// A height-field vertex's light record (0x18 bytes; CHeightField's array at +0x20). Names ours.
struct SLitVertex
{
	DWORD	mUnknown00;				// +0x00
	DWORD	mLayers;				// +0x04: bit n: light map n reaches the vertex
	DWORD	mColour;				// +0x08: its tint, 0x00rrggbb
	SWORD	mIntensity[5];			// +0x0c: each light map's strength at the vertex
	SWORD	mUnknown16;				// +0x16
};

// One corner's tint, or a step of it, in 8.8 per channel. Names ours. Four words: retail's frame keeps each of these
// records in 16 bytes whose fourth word nothing reads (0x38, 0x48, 0x58, 0xb4, 0xc8 against the 0xc8-byte frame).
struct SCacheRGB
{
	SINT	r, g, b, a;
};

// A packed 0x00rrggbb colour's channels, each in 8.8. The colour is signed: the four corner colours then go through
// one stack slot (0x10) and red is read back as a byte, `mov [esp+0x10],ecx; mov ah,[esp+0x12]` (00542314, as retail);
// taken as a DWORD (or UINT, or a DWORD reference) they stay in registers and red is shifted out (157/441 -> 146/404).
static inline void UnpackRGB(SCacheRGB &l, SINT c)
{
	l.r = ((c >> 16) & 0xff) << 8;
	l.g = c & 0xff00;
	l.b = (c & 0xff) << 8;
}

// The same channels less base's, channel by channel: retail subtracts each channel as it unpacks it (00542330:
// l01.r's sub precedes the copy of p01's colour that becomes l01.g; 00542393: l11.r's sub reads l10.r from its home
// before l11.g is unpacked).
static inline void UnpackDiff(SCacheRGB &l, SINT c, const SCacheRGB &base)
{
	l.r = (((c >> 16) & 0xff) << 8) - base.r;
	l.g = (c & 0xff00) - base.g;
	l.b = ((c & 0xff) << 8) - base.b;
}

//*******************************************************************************
// Matched. Retail's structure throughout (the 16-byte tint records stepped in place, the light
// gradients as CHeightField::Unknown47eff0 writes its alpha ones, step << 2 kept whole in the texel offset, a row
// counter of its own). The cell's column indices come first: then the second row reloads mGridMaskY and mGridShiftX as
// retail does.
// step4 is one value: the block's factor and the copy's length, and VC6's CSE gives the DWORD pointer steps
// (b += step, out += step) the same temporary, so it recomputes `lea [step*4]` from step at each use, as retail does
// in the copy loop (005420d5, 005420ff) and at out's step (00542591). Written inline, the copy's step * 4 was a hoisted
// temporary of its own (spilled) and out's step folded into a lea. shift2 is shift * 2 named once: its value is one
// more counted web in the light gradients' block (the ddc shift; the tints' `shift * 2` share it through CSE), which
// puts dc (1264) above v (1240) in the allocator's queue. dc's colouring then splits p00 before v pops, so v takes ebx
// and p00 ecx (in the cell's first block, the light-map loop and the corner setup) as retail; without it p00 had only
// ebx left when v popped and v took ecx.
// plane is the texel pointer itself and the copy's s a copy of it, both set beside the copy's pointers: s then pops
// before plane and the copy loop takes retail's registers (s in edi, b in memory and esi, moved for the copy at
// 005420dc), and plane keeps eax through the light-map loop with p00 - p01 in esi and p10 - p01 in ebp
// (0054212b..00542168); set after the copy loop (from a pointer that lived across it) plane lost those registers.
// There is no separate src: with `src = ...; plane = src; s = src;` every register is the same, but src's web goes at
// once and its node is then the last entry of the allocator's free list when it first rebuilds the prologue's values,
// and rowskip took it. The later rebuilds left block, w, rowskip in ascending node index, so the row's end reloaded
// block first and block's spill slot fell below rowskip's. Without src, rowskip takes a fresh index and the last
// rebuild leaves rowskip, w, block: retail's reload order (005425ba..005425c8) and slots (rowskip 0x8c, block 0x94).
// The corner setup unpacks l01 and l11 as differences, channel by channel (UnpackDiff), takes the remaining
// differences red, blue, green and the shifts record by record, red first. The light gradients' row step adds ddc
// before skip: the slot packer then counts ddc's second reference first, so ddc takes 0x90 and skip 0xa8 as retail
// (the scheduler still issues skip's load first, 005422a7).
// The PC texture cache's texels (CDXLandscape::BuildTextureCache 00547975): w x h cells from (x, y), each a
// (1 << shift)-texel square of the first mixer texture's tile, lit by the five mixer planes its corner vertices
// enable, tinted by their colours (both bilinear across the cell) and written opaque into dst at cell (dx, dy) of a
// size-texel-wide image.
void CHeightField::GetCacheTexels(SINT shift, CMapTex *maptex, DWORD *dst, SINT x, SINT y, SINT dx, SINT dy, SINT w, SINT h, SINT size)	// FUNCTION: 0x00541f50
{
	SINT	step = 1 << shift;
	SINT	shift2 = shift * 2;
	SINT	skip = maptex->mSize - step;
	SINT	step4 = step * 4;
	SINT	block = maptex->mSize * step4;
	SINT	pitch = maptex->mSize * 4;
	DWORD	*out = dst + (dy * size + dx) * step;
	SINT	rowskip = (size - w) * step;

	// Retail tests these variable-size loop counts for zero, then decrements them.
	SINT	cy = y;
	for (SINT j = h; j != 0; j--, cy++)
	{
		SINT	cx = x;
		for (SINT i = w; i != 0; i--, cx++)
		{
			SINT	c0 = cx & mGridMaskX;
			SINT	c1 = (cx + 1) & mGridMaskX;
			SINT	r0 = (cy & mGridMaskY) << mGridShiftX;
			SINT	r1 = ((cy + 1) & mGridMaskY) << mGridShiftX;
			SLitVertex	*v = (SLitVertex *)mUnknown20;
			SLitVertex	*p00 = &v[r0 + c0];
			SLitVertex	*p01 = &v[r0 + c1];
			SLitVertex	*p10 = &v[r1 + c0];
			SLitVertex	*p11 = &v[r1 + c1];

			// the tile's square, copied (plane then steps through the light maps stored after the tile texture)
			UBYTE	*plane = maptex->mData + (cy & 15) * block + (cx & 15) * (step << 2);
			UBYTE	*s = plane;
			DWORD	*b = mUnknown28;
			SINT	k;
			for (k = step; k != 0; k--)
			{
				memcpy(b, s, step4);
				s += pitch;
				b += step;
			}

			// each light map the corners enable, blended over it
			SINT	bit = 1;
			for (k = 0; k < 5; k++)
			{
				plane += maptex->mTexSize;
				if (p00->mLayers & bit)
				{
					// retail steps one pointer along p01's intensities, shifts p10's once for both of its
					// differences and subtracts dc from (i11 - i10)
					SINT	i00 = p00->mIntensity[k] << 16;
					SINT	dc = (p01->mIntensity[k] << 16) - i00;
					SINT	i10 = p10->mIntensity[k] << 16;
					SINT	ddc = ((p11->mIntensity[k] << 16) - i10) - dc;
					SINT	dr = i10 - i00;
					dc >>= shift;
					dr >>= shift;
					ddc >>= shift2;
					DWORD	*p = (DWORD *)plane;
					UBYTE	*t = (UBYTE *)mUnknown28;
					for (SINT r = step; r != 0; r--)
					{
						SINT	lv = i00;
						i00 += dr;
						for (SINT c = step; c != 0; c--)
						{
							SINT	lit = (lv & 0xff000000) + *p;
							SINT	d = lit - *(SINT *)t;
							if (d > 0)
							{
								d >>= 25;
								if (d >= 16)
									*(SINT *)t = lit;
								else
								{
									SINT	e = 15 - d;
									t[0] = ((t[0] * e) >> 4) + ((((UBYTE *)p)[0] * d) >> 4);
									t[1] = ((((UBYTE *)p)[1] * d) >> 4) + ((t[1] * e) >> 4);
									t[2] = ((((UBYTE *)p)[2] * d) >> 4) + ((t[2] * e) >> 4);
									t[3] = lit >> 24;
								}
							}
							t += 4;
							p++;
							lv += dc;
						}
						dc += ddc;
						p += skip;
					}
				}
				bit <<= 1;
			}

			// the corners' tints, bilinear in 8.8, over the square: l01 becomes the column step, l10 the row step,
			// l11 the column step's change per row and l00 the row's start (retail subtracts l01 from l11 - l10).
			// The differences take red, blue, green (retail's loads of l10.r and l00.r, then l00.b and l10.b, then
			// l11.r and l01.r, 005423ae..005423d2); col is set field by field (a whole-record copy would also copy
			// the unset fourth word, which retail never touches).
			SCacheRGB	l00, l01, l10, l11;
			UnpackRGB(l00, p00->mColour);
			UnpackDiff(l01, p01->mColour, l00);
			UnpackRGB(l10, p10->mColour);
			UnpackDiff(l11, p11->mColour, l10);
			l10.r -= l00.r;
			l10.b -= l00.b;
			l10.g -= l00.g;
			l11.r -= l01.r;
			l11.b -= l01.b;
			l11.g -= l01.g;
			l01.r >>= shift;
			l01.g >>= shift;
			l01.b >>= shift;
			l10.r >>= shift;
			l10.g >>= shift;
			l10.b >>= shift;
			l11.r >>= shift2;
			l11.g >>= shift2;
			l11.b >>= shift2;
			l00.r += 0x400;
			l00.g += 0x400;
			l00.b += 0x400;
			UBYTE	*t = (UBYTE *)mUnknown28;
			DWORD	*o = out;
			for (SINT r = step; r != 0; r--)
			{
				l00.r += l10.r;
				l00.g += l10.g;
				l00.b += l10.b;
				l01.r += l11.r;
				l01.g += l11.g;
				l01.b += l11.b;
				SCacheRGB	col;
				col.r = l00.r;
				col.g = l00.g;
				col.b = l00.b;
				for (SINT c = step; c != 0; c--)
				{
					col.r += l01.r;
					col.g += l01.g;
					col.b += l01.b;
					*o = (t[2] * col.r) & 0xff0000;
					*o |= ((t[1] * col.g) >> 8) & 0xff00;
					*o |= ((t[0] * col.b) >> 16) & 0xff | 0x80000000;
					o++;
					t += 4;
				}
				o += size - step;
			}
			out += step;
		}
		out += rowskip;
	}
}

#endif
