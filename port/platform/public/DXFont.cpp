// SPDX-License-Identifier: GPL-3.0-only
// port/platform/public/DXFont.cpp: the PC bitmap font (CDXBitmapFont, Platform.h), written for the public build in
// place of src/DXFont.cpp. A font is either a texture of square character cells (a bitmap font, whose characters'
// widths are measured from the texture's alpha) or a Windows font drawn into a new texture (a system font). Text is
// drawn as one screen-space quad per character through the shared quad buffer. The arithmetic is the game's, in the
// same order, so text lands on the same pixels and measures the same widths; the front end's 640 x 480 coordinates
// scale by the window through multiplications by 1/640 and 1/480, as the game's compiled code does. Where the player asked
// for it (ce.ini sharp_ui=1, off by default), the picture takes the non-4:3 wide path and bitmap text is drawn at least
// twice its texture's size horizontally or vertically (for example, menus at 1920 x 1080), it can use a sharper copy;
// elsewhere the game's texture is the one drawn.
#include	"Common.h"
#include	"Platform.h"
#include	"Texture.h"
#include	"VBufTexture.h"
#include	"FastVB.h"
#include	"frontend.h"
#include	"tweak.h"
#include	"chunker.h"
#include	"mathsconst.h"
#include	"../platform/common/portlog.h"

extern CTweakSLONG	USE_MODULATE_2X;
extern const WCHAR	FONT_CHARACTERS[];		// a bitmap font's characters, in cell order

// Added to every character's texture coordinates (left and top) and taken from the right and bottom.
float	FONT_U_OFFSET = 0.0f;
float	FONT_V_OFFSET = 0.0f;

namespace
{
// A transformed, coloured, textured vertex, as the quad buffer is set up for.
struct GlyphVertex
{
	D3DXVECTOR4	pos;
	DWORD		colour;
	FLOAT		u, v;
};
const DWORD	GLYPH_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

void	PutVertex(GlyphVertex *v, FLOAT x, FLOAT y, FLOAT z, DWORD colour, FLOAT u, FLOAT tv)
{
	v->pos = D3DXVECTOR4(x, y, z, 1.0f);
	v->colour = colour;
	v->u = u;
	v->v = tv;
}

// Text colours are authored for a doubling colour stage; with the plain stage each channel is doubled, up to 255.
DWORD	StageColour(DWORD c)
{
	if (USE_MODULATE_2X.mValue)
		return c;
	DWORD	r = (c >> 15) & 0x1fe;
	DWORD	g = (c >> 7) & 0x1fe;
	DWORD	b = (c & 0xff) << 1;
	r = r > 0xff ? 0xff : r;
	g = g > 0xff ? 0xff : g;
	b = b > 0xff ? 0xff : b;
	return (c & 0xff000000) + (r << 16) + (g << 8) + b;
}

// Walks left from column `x` (no further than `left`) to the first column with a pixel whose alpha is above 0x10 in
// rows top .. top + rows - 2, and returns the column one left of it (left - 1 when there is none).
int		LastInkedColumn(const D3DLOCKED_RECT &lr, int left, int x, int top, int rows)
{
	bool	inked = false;
	for (; x >= left && !inked; x--)
	{
		for (int y = top; y < top + rows - 1; y++)
		{
			const DWORD	*row = (const DWORD *)((const UBYTE *)lr.pBits + y * lr.Pitch);
			if ((UBYTE)(row[x] >> 24) > 0x10)
				inked = true;
		}
	}
	return x;
}

// ---- Text at large sizes --------------------------------------------------------------------------------------------
// The bitmap fonts are anti-aliased renderings 16 and 32 pixels high. Drawn 4.5 times larger (the 640 x 480 screen on 4K)
// with the sampler the game sets (linear, one mip level: measured with BEA_PORT_D3DLOG, which records sampler states) each
// stroke becomes a soft smear. A copy of the texture four times as large, each character cell rebuilt with a cubic
// interpolation of its alpha and the edge then steepened around half coverage, keeps the glyph's outline and gives it
// a crisp edge at any size from twice up; its colour is interpolated weighted by the alpha, so a font with a shaded face
// (the title font) keeps it and no edge darkens. The characters' cells, widths and the quads drawn are the original
// texture's: only the pixels sampled differ. It is the player's choice (ce.ini sharp_ui=1, PortSharpUi): by default the
// text keeps the original's soft look, as the rest of the picture does.
const int		CRISP_SCALE = 4;
const float		CRISP_FROM = 0.30f;			// the alpha below which the steepened edge is clear, and above CRISP_TO fully inked
const float		CRISP_TO = 0.70f;
const float		CRISP_MIN_SCALE = 2.0f;		// text drawn at less than this keeps the game's texture

// The copy requires PortWideActive, a bitmap font, the larger drawing scale at CRISP_MIN_SCALE or more and sharp_ui=1.
// Ordinary replays and recordings disable wide modes; BEA_PORT_WIDE_TEST or BEA_PORT_CE_LAYER=1 can bypass that gate.
// A 4:3 back buffer remains inactive. PortSharpUi is asked last, so only an eligible draw reads the setting.
bool	CrispWanted(bool sysFont, float scale, int wide)
{
	return !sysFont && wide && scale >= CRISP_MIN_SCALE && PortSharpUi();
}

// Catmull-Rom weights of the four source texels around output texel `o` of an upsampling by CRISP_SCALE; `first` is the
// leftmost of the four.
void	CrispWeights(int o, int *first, float w[4])
{
	float	x = (o + 0.5f) / CRISP_SCALE - 0.5f;
	int		i0 = (int)floorf(x);
	float	t = x - i0;
	*first = i0 - 1;
	w[0] = ((-0.5f * t + 1.0f) * t - 0.5f) * t;
	w[1] = (1.5f * t - 2.5f) * t * t + 1.0f;
	w[2] = ((-1.5f * t + 2.0f) * t + 0.5f) * t;
	w[3] = (0.5f * t - 0.5f) * t * t;
}

float	Steepen(float a)
{
	float	t = (a - CRISP_FROM) / (CRISP_TO - CRISP_FROM);
	t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
	return t * t * (3.f - 2.f * t);
}

// The pixels of an A8R8G8B8 texture of w x h, four times as large into dst (pitches in bytes), cell by cell (cells of
// `cell` pixels, the last ones in a row or column cut short): a neighbouring character never leaks into one, the cubic's
// reads clamp at the cell's edges. Four planes per cell: the alpha and the colour weighted by it, widened first, then
// heightened. A font is mostly blank, and where no texel under the cubic has alpha every sum is zero: those rows, texels
// and clear texels' colours are not worked out (the result is the same as working them out, which the tests compare).
void	CrispUpsample(const void *src, int srcPitch, int w, int h, int cell, void *dst, int dstPitch)
{
	const int	cols = cell * CRISP_SCALE;
	float		*wide = new float[4 * cell * cols];			// per row of the cell: alpha, alpha * red, * green, * blue (widened)
	int			*firstTab = new int[cols];					// the cubic's leftmost texel and its four weights for each output texel of a cell
	float		*weightTab = new float[4 * cols];
	for (int o = 0; o < cols; o++)
		CrispWeights(o, &firstTab[o], &weightTab[4 * o]);
	bool		*rowInk = new bool[cell];					// whether a row of the cell has any texel with alpha
	for (int cy = 0; cy < h; cy += cell)
		for (int cx = 0; cx < w; cx += cell)
		{
			const int	cw = cx + cell > w ? w - cx : cell;
			const int	ch = cy + cell > h ? h - cy : cell;
			for (int y = 0; y < ch; y++)
			{
				const DWORD	*row = (const DWORD *)((const UBYTE *)src + (cy + y) * srcPitch) + cx;
				bool		ink = false;
				for (int i = 0; i < cw; i++)
					ink = ink || (row[i] >> 24) != 0;
				rowInk[y] = ink;
				if (!ink)
				{
					// no alpha anywhere in the row: every weighted sum is zero
					for (int c = 0; c < 4; c++)
						memset(&wide[(c * cell + y) * cols], 0, cw * CRISP_SCALE * sizeof(float));
					continue;
				}
				for (int o = 0; o < cw * CRISP_SCALE; o++)
				{
					const int		first = firstTab[o];
					const float		*wt = &weightTab[4 * o];
					float	sum[4] = {0.f, 0.f, 0.f, 0.f};
					DWORD	taps[4];
					for (int k = 0; k < 4; k++)
					{
						int	i = first + k;
						i = i < 0 ? 0 : i >= cw ? cw - 1 : i;
						taps[k] = row[i];
					}
					if (((taps[0] | taps[1] | taps[2] | taps[3]) >> 24) != 0)		// otherwise every sum is zero
						for (int k = 0; k < 4; k++)
						{
							DWORD	p = taps[k];
							float	a = (float)(p >> 24) * (1.f / 255.f);
							sum[0] += wt[k] * a;
							sum[1] += wt[k] * a * (float)((p >> 16) & 0xff);
							sum[2] += wt[k] * a * (float)((p >> 8) & 0xff);
							sum[3] += wt[k] * a * (float)(p & 0xff);
						}
					for (int c = 0; c < 4; c++)
						wide[(c * cell + y) * cols + o] = sum[c];
				}
			}
			for (int o = 0; o < ch * CRISP_SCALE; o++)
			{
				const int		first = firstTab[o];
				const float		*wt = &weightTab[4 * o];
				DWORD	*dest = (DWORD *)((UBYTE *)dst + (cy * CRISP_SCALE + o) * dstPitch) + cx * CRISP_SCALE;
				int		row[4];								// the cubic's four source rows, clamped to the cell
				bool	kernelInk = false;
				for (int k = 0; k < 4; k++)
				{
					int	i = first + k;
					row[k] = i < 0 ? 0 : i >= ch ? ch - 1 : i;
					kernelInk = kernelInk || rowInk[row[k]];
				}
				const int	sy = o / CRISP_SCALE;
				for (int x = 0; x < cw * CRISP_SCALE; x++)
				{
					float	sum[4] = {0.f, 0.f, 0.f, 0.f};
					if (kernelInk)
						for (int k = 0; k < 4; k++)
							sum[0] += wt[k] * wide[(0 * cell + row[k]) * cols + x];
					DWORD	rgb;
					if (sum[0] > 0.02f)
					{
						for (int k = 0; k < 4; k++)
							for (int c = 1; c < 4; c++)
								sum[c] += wt[k] * wide[(c * cell + row[k]) * cols + x];
						// the colour the weighted texels share; the cubic's overshoot is clamped away
						float	inv = 1.f / sum[0];
						int		rgbc[3];
						for (int c = 0; c < 3; c++)
						{
							int	v = (int)floorf(sum[1 + c] * inv + 0.5f);
							rgbc[c] = v < 0 ? 0 : v > 255 ? 255 : v;
						}
						rgb = ((DWORD)rgbc[0] << 16) | ((DWORD)rgbc[1] << 8) | (DWORD)rgbc[2];
					}
					else
					{
						// no ink here to speak of, which the steepened alpha hides: the colour of the most inked source texel
						// within one of it, so that sampling across an edge never mixes in a texel the font left black
						int		sx = x / CRISP_SCALE;
						DWORD	best = 0;
						int		bestAlpha = -1;
						const bool	windowInk = rowInk[sy > 0 ? sy - 1 : 0] || rowInk[sy] || rowInk[sy + 1 < ch ? sy + 1 : ch - 1];
						for (int dy = -1; dy <= 1; dy++)
							for (int dx = -1; dx <= 1; dx++)
							{
								int		px = sx + dx, py = sy + dy;
								px = px < 0 ? 0 : px >= cw ? cw - 1 : px;
								py = py < 0 ? 0 : py >= ch ? ch - 1 : py;
								DWORD	p = ((const DWORD *)((const UBYTE *)src + (cy + py) * srcPitch))[cx + px];
								if ((int)(p >> 24) > bestAlpha)
								{
									bestAlpha = (int)(p >> 24);
									best = p;
								}
								if (!windowInk)
									goto found;				// every texel of the window is clear: the first one
							}
					found:
						rgb = best & 0x00ffffff;
					}
					// floorf, then the integral value: this file is compiled with /QIfist, where a float converts to an
					// integer by rounding, so the usual (int)(v + 0.5f) would turn full coverage into 256
					int	ink;
					if (sum[0] <= CRISP_FROM)
						ink = 0;							// Steepen is 0 up to CRISP_FROM and 1 from CRISP_TO on
					else if (sum[0] >= CRISP_TO)
						ink = 255;
					else
					{
						ink = (int)floorf(Steepen(sum[0]) * 255.f + 0.5f);
						ink = ink > 255 ? 255 : ink;
					}
					dest[x] = ((DWORD)ink << 24) | rgb;
				}
			}
		}
	delete [] rowInk;
	delete [] weightTab;
	delete [] firstTab;
	delete [] wide;
}

struct CrispFont
{
	const CDXBitmapFont	*font;
	IDirect3DDevice9	*device;		// the device the copy was made for: after the device is made again it is made again
	IDirect3DTexture9	*texture;		// NULL: the copy could not be made, the game's texture is used
};
CrispFont		gCrispFonts[16];
int				gNumCrisp;

// The texture four times as large, or NULL when the game's is not a lockable A8R8G8B8 texture of the font's size. Made once
// per font, the first time text of a size that wants it is drawn; the session log says so (and why, when it is not made).
IDirect3DTexture9	*BuildCrispTexture(CDXBitmapFont *font)
{
	IDirect3DTexture9	*src = font->mTexture ? font->mTexture->GetTexture() : NULL;
	if (!src)
		return NULL;
	D3DSURFACE_DESC	desc;
	const char		*why = NULL;
	if (FAILED(src->GetLevelDesc(0, &desc)))
		why = "its description is not available";
	else if (desc.Format != D3DFMT_A8R8G8B8)
		why = "it is not an A8R8G8B8 texture";
	else if (desc.Width != font->mTexWidth || desc.Height != font->mTexHeight)
		why = "its size is not the font's";
	else if (font->mHeight < 4 || font->mHeight > 128)
		why = "the character cells are an unusual size";
	if (why)
	{
		PortSessionLog(PORT_SESSION_VIDEO, false, "font copy not made: %s; the game's texture is drawn", why);
		return NULL;
	}
	const DWORD		began = GetTickCount();
	D3DLOCKED_RECT	in;
	if (FAILED(src->LockRect(0, &in, NULL, D3DLOCK_READONLY)))
	{
		PortSessionLog(PORT_SESSION_VIDEO, true, "font copy not made: the font texture could not be locked");
		return NULL;
	}
	IDirect3DTexture9	*dst = NULL;
	if (FAILED(LT.GetDevice()->CreateTexture(desc.Width * CRISP_SCALE, desc.Height * CRISP_SCALE, 1, 0, D3DFMT_A8R8G8B8,
											 D3DPOOL_MANAGED, &dst, NULL)))
	{
		src->UnlockRect(0);
		PortSessionLog(PORT_SESSION_VIDEO, true, "font copy not made: the larger texture could not be created");
		return NULL;
	}
	D3DLOCKED_RECT	out;
	if (FAILED(dst->LockRect(0, &out, NULL, 0)))
	{
		src->UnlockRect(0);
		dst->Release();
		PortSessionLog(PORT_SESSION_VIDEO, true, "font copy not made: the larger texture could not be locked");
		return NULL;
	}
	CrispUpsample(in.pBits, in.Pitch, (int)desc.Width, (int)desc.Height, (int)font->mHeight, out.pBits, out.Pitch);
	dst->UnlockRect(0);
	src->UnlockRect(0);
	PortSessionLog(PORT_SESSION_VIDEO, false, "font copy made: %ux%u texture, cells of %d, in %lu ms", (unsigned)desc.Width,
				   (unsigned)desc.Height, (int)font->mHeight, (unsigned long)(GetTickCount() - began));
	return dst;
}

// The copy of this font's texture to draw text of this size from, or NULL for the game's texture.
IDirect3DTexture9	*CrispTexture(CDXBitmapFont *font, float scale)
{
	if (!CrispWanted(font->mSysFont, scale, PortWideActive()))
		return NULL;
	IDirect3DDevice9	*device = LT.GetDevice();
	for (int i = 0; i < gNumCrisp; i++)
		if (gCrispFonts[i].font == font)
		{
			if (gCrispFonts[i].device != device)
			{
				// a copy made for a device that is gone (a Reset keeps a managed texture; a new device does not)
				if (gCrispFonts[i].texture)
					gCrispFonts[i].texture->Release();
				gCrispFonts[i].device = device;
				gCrispFonts[i].texture = BuildCrispTexture(font);
			}
			return gCrispFonts[i].texture;
		}
	if (gNumCrisp == (int)(sizeof(gCrispFonts) / sizeof(gCrispFonts[0])))
		return NULL;
	gCrispFonts[gNumCrisp].font = font;
	gCrispFonts[gNumCrisp].device = device;
	gCrispFonts[gNumCrisp].texture = BuildCrispTexture(font);
	return gCrispFonts[gNumCrisp++].texture;
}

void	ReleaseCrispTexture(const CDXBitmapFont *font)
{
	for (int i = 0; i < gNumCrisp; i++)
		if (gCrispFonts[i].font == font)
		{
			if (gCrispFonts[i].texture)
				gCrispFonts[i].texture->Release();
			gCrispFonts[i] = gCrispFonts[--gNumCrisp];
			return;
		}
}
}

CDXBitmapFont::CDXBitmapFont()
: mCharSwapHack(FALSE), mUnknown16c(0)
{
	mTexture = NULL;
	mVBufTexture = NULL;
	mVBufTexture2 = NULL;
	InitCharMap();
}

CDXBitmapFont::~CDXBitmapFont()
{
	ReleaseCrispTexture(this);
	if (mTexture)
	{
		mTexture->Release();
		mTexture = NULL;
	}
	if (mVBufTexture)
	{
		mVBufTexture->Release();
		mVBufTexture = NULL;
	}
	if (mVBufTexture2)
	{
		mVBufTexture2->Release();
		mVBufTexture2 = NULL;
	}
}

void	CDXBitmapFont::InitialiseAsSysFont(TCHAR *name, int size, int flags)
{
	strcpy(mFontName, FromTCHAR(name));
	mHeight = size;
	mFlags = flags;
	mTexture = NULL;
	mSysFont = TRUE;
}

void	CDXBitmapFont::InitialiseAsBitmapFont(char *name, int size)
{
	strcpy(mTextureName, name);
	mSysFont = FALSE;
	mHeight = size;
	mTexture = NULL;
}

// A bitmap font's texture holds its characters in square cells of the font's height, left to right and then down,
// in FONT_CHARACTERS' order. Each character's width is measured from the cell's alpha; the space is half a cell.
void	CDXBitmapFont::InitFromTexture()
{
	int	compression = LT.m_bTextureCompressionDisabled;
	LT.m_bTextureCompressionDisabled = 1;
	mTexture = CDXTexture::GetTextureByName(mTextureName, TEXFMT_A8R8G8B8, TEX_NORMAL, 1, 1, 1);
	LT.m_bTextureCompressionDisabled = compression;
	if (!mTexture || !mTexture->GetTexture())
		return;

	mTexWidth = mTexture->mWidth;
	mTexHeight = mTexture->GetHeight();

	D3DSURFACE_DESC	desc;
	mTexture->GetTexture()->GetLevelDesc(0, &desc);
	D3DLOCKED_RECT	lr;
	if (FAILED(mTexture->GetTexture()->LockRect(0, &lr, NULL, 0)))
		return;

	float	u = 0.f;
	float	v = 0.f;
	int		ch = 0;
	do
	{
		int		cellLeft = int(float(mTexWidth) * u);
		int		right = cellLeft + mHeight - 1;
		int		top = int(float(mTexHeight) * v);
		int		left = cellLeft - 1 < 0 ? 0 : cellLeft - 1;
		right = LastInkedColumn(lr, left, right - 1, top, mHeight);

		int		width = ch == 0 ? int(mHeight >> 1) : right - left + 2;
		mTexCoords[ch][0] = u;
		mTexCoords[ch][1] = v;
		mTexCoords[ch][2] = float(width) / mTexWidth + u;
		mTexCoords[ch][3] = float(mHeight) / mTexHeight + v;

		u += float(mHeight) / float(mTexWidth);
		if (u >= 1.f)
		{
			u = 0.f;
			v += float(mHeight) / float(mTexHeight);
		}
		ch++;
	} while (FONT_CHARACTERS[ch] && v < 1.f);

	mTexture->GetTexture()->UnlockRect(0);
}

// A system font: the printable ASCII characters drawn with GDI, white on black, into a 32-bit bitmap in rows (a
// pixel's gap after each for antialiasing), then copied into a new A8R8G8B8 texture as white with the drawn
// intensity as alpha. The texture is 256, 512 or 1024 square by the font's height, smaller (and the text scaled up
// when drawn) when the device cannot take that size.
void	CDXBitmapFont::InitFromSysFont()
{
	mTextScale = 1.f;
	mTexWidth = mTexHeight = mHeight > 40 ? 1024 : mHeight > 20 ? 512 : 256;

	D3DCAPS9	caps;
	LT.GetDevice()->GetDeviceCaps(&caps);
	if (mTexWidth > caps.MaxTextureWidth)
	{
		mTextScale = (FLOAT)caps.MaxTextureWidth / (FLOAT)mTexWidth;
		mTexWidth = mTexHeight = caps.MaxTextureWidth;
	}

	mTexture = new(MEMTYPE_TEXTURE) CDXTexture;
	mTexture->InitEmpty(mTexWidth, mTexHeight, TEXFMT_A8R8G8B8, 1, 0, 1);

	BITMAPINFO	info;
	ZeroMemory(&info.bmiHeader, sizeof(BITMAPINFOHEADER));
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = (int)mTexWidth;
	info.bmiHeader.biHeight = -(int)mTexHeight;		// top-down rows
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biCompression = BI_RGB;
	info.bmiHeader.biBitCount = 32;

	DWORD	*pixels;
	HDC		dc = CreateCompatibleDC(NULL);
	HBITMAP	bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (VOID **)&pixels, NULL, 0);
	SetMapMode(dc, MM_TEXT);

	INT		height = -MulDiv(mHeight, (INT)(GetDeviceCaps(dc, LOGPIXELSY) * mTextScale), 72);
	HFONT	font = CreateFont(height, 0, 0, 0, (mFlags & 1) ? FW_BOLD : FW_NORMAL, (mFlags & 2) ? TRUE : FALSE, FALSE, FALSE,
							  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH,
							  ToTCHAR(mFontName));
	SelectObject(dc, bitmap);
	SelectObject(dc, font);
	SetTextColor(dc, RGB(255, 255, 255));
	SetBkColor(dc, 0x00000000);
	SetTextAlign(dc, TA_TOP);

	DWORD	x = 0;
	DWORD	y = 0;
	TCHAR	one[2];
	strcpy(one, ToTCHAR("x"));
	for (char c = 32; c < 127; c++)
	{
		one[0] = c;
		SIZE	extent;
		GetTextExtentPoint32(dc, one, 1, &extent);
		if ((DWORD)(x + extent.cx + 1) > mTexWidth)
		{
			x = 0;
			y += extent.cy + 1;
		}
		ExtTextOut(dc, x, y, ETO_OPAQUE, NULL, one, 1, NULL);

		int		cell = RemapChar(c);
		mTexCoords[cell][0] = ((FLOAT)x) / mTexWidth;
		mTexCoords[RemapChar(c)][1] = ((FLOAT)y) / mTexHeight;
		mTexCoords[RemapChar(c)][2] = ((FLOAT)(x + extent.cx)) / mTexWidth;
		mTexCoords[RemapChar(c)][3] = ((FLOAT)(y + extent.cy)) / mTexHeight;
		x += extent.cx + 1;
	}

	D3DLOCKED_RECT	lr;
	if (SUCCEEDED(mTexture->GetTexture()->LockRect(0, &lr, 0, D3DLOCK_NOSYSLOCK)))
	{
		DWORD	*out = (DWORD *)lr.pBits;
		for (DWORD n = 0; n < mTexWidth * mTexHeight; n++)
		{
			BYTE	ink = (BYTE)(pixels[n] & 0xff);
			out[n] = ink > 0 ? ((DWORD)ink << 24) | 0x00ffffff : 0x00000000;
		}
		mTexture->GetTexture()->UnlockRect(0);
		DeleteObject(bitmap);
		DeleteDC(dc);
		DeleteObject(font);
		strcpy(mTexture->mName, "SystemFont");
	}
}

// The texture and the quad buffer, made on first use.
static void	PrepareFont(CDXBitmapFont *font)
{
	if (font->mSysFont)
		font->InitFromSysFont();
	else
		font->InitFromTexture();
	font->mVBufTexture = CVBufTexture::GetOrCreate(font->mTexture, 0);
	font->mVBufTexture->SetupV(GLYPH_FVF, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, sizeof(GlyphVertex), D3DPT_TRIANGLELIST, D3DPOOL_DEFAULT);
	font->mVBufTexture->SetupI(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 2, D3DPOOL_DEFAULT);
}

// Draws text from (x, y), scaled; '\n' starts a new line under the first, '\r' is skipped. flags 8 keeps the depth
// test on; flags 0x10 keeps y's fraction (y is otherwise floored; x always is). `colours`, when given, holds one
// colour per drawn character. More than 2500 characters draw nothing.
HRESULT	CDXBitmapFont::DrawTextScaled(float x, float y, float z, float scalex, float scaley, DWORD colour, WCHAR *text, int flags, DWORD *colours)
{
	int		glyphs = 0;
	for (WCHAR *p = text; *p; p++)
		if (*p != '\r' && *p != '\n')
			glyphs++;
	if (!glyphs)
		return S_OK;

	FASTVB.Create();
	if (FRONTEND.IsRunning() && PortWideActive())
	{
		// Widescreen (patch 0042's DXFont part): the virtual screen in its centred 4:3 box, unstretched.
		x = x * PortUiScale() + PortUiLeft();
		y = y * PortUiScale() + PortUiTop();
		scalex *= PortUiScale();
		scaley *= PortUiScale();
	}
	else if (FRONTEND.IsRunning())
	{
		x = PLATFORM.GetWindowWidth() * x * 0.0015625f;
		y = PLATFORM.GetWindowHeight() * y * 0.0020833334f;
		scalex = PLATFORM.GetWindowWidth() * scalex * 0.0015625f;
		scaley = PLATFORM.GetWindowHeight() * scaley * 0.0020833334f;
	}
	if (!mTexture)
		PrepareFont(this);

	bool	depthTest = (flags & 8) != 0;
	if (!depthTest)
		LT.SRS(D3DRS_ZENABLE, FALSE);
	if (!(flags & 0x10))
		y = floorf(y);
	float	penX = floorf(x);
	float	penY = y;
	float	lineStart = penX;
	float	lineHeight = (mTexCoords[0][3] - mTexCoords[0][1]) * mTexHeight;
	if (glyphs > 2500)
		return S_OK;

	GlyphVertex	*v;
	if (FASTVB.LockAligned((void **)&v, glyphs * 4) != 0xffff)
	{
		for (WCHAR *p = text; *p; p++)
		{
			WCHAR	c = *p;
			if (c == '\n')
			{
				penX = lineStart;
				penY += lineHeight * scaley;
				continue;
			}
			if (c == '\r')
				continue;

			FLOAT	u1 = mTexCoords[RemapChar(c)][0] + FONT_U_OFFSET;
			FLOAT	v1 = mTexCoords[RemapChar(c)][1] + FONT_V_OFFSET;
			FLOAT	u2 = mTexCoords[RemapChar(c)][2] - FONT_U_OFFSET;
			FLOAT	v2 = mTexCoords[RemapChar(c)][3] - FONT_V_OFFSET;
			FLOAT	width = (u2 - u1) * mTexWidth * scalex;
			FLOAT	height = (v2 - v1) * mTexHeight * scaley;
			DWORD	col = StageColour(colours ? *colours++ : colour);

			// Pixel centres: each corner half a pixel up and left.
			FLOAT	top = penY - 0.5f;
			FLOAT	bottom = penY + height - 0.5f;
			FLOAT	left = penX - 0.5f;
			FLOAT	right = penX + width - 0.5f;
			PutVertex(v + 0, left, top, z, col, u1, v1);
			PutVertex(v + 1, right, top, z, col, u2, v1);
			PutVertex(v + 2, right, bottom, z, col, u2, v2);
			PutVertex(v + 3, left, bottom, z, col, u1, v2);
			v += 4;

			penX += width;
			if (!mSysFont)
				penX += scalex;		// a bitmap font's one-pixel gap
		}
		IDirect3DTexture9	*crisp = CrispTexture(this, scalex > scaley ? scalex : scaley);
		LT.D3D_SetTexture(0, crisp ? crisp : mTexture->GetTexture());
		FASTVB.Render();
	}

	if (!depthTest)
		LT.SRS(D3DRS_ZENABLE, TRUE);
	return S_OK;
}

HRESULT	CDXBitmapFont::DrawText(float x, float y, DWORD colour, WCHAR *text, int a, int b, float z)
{
	return DrawTextScaled(x, y, z, 1.f, 1.f, colour, text, a, (DWORD *)b);
}

// The text's width (its widest line) and height in pixels at scale 1, converted with the current rounding. A newline
// starts a new line and is also measured, as the game measures it; '\r' is skipped.
BOOL	CDXBitmapFont::GetTextExtent(WCHAR *text, SIZE *size)
{
	if (!text || !size)
		return FALSE;
	if (!mTexture)
		PrepareFont(this);

	FLOAT	line = 0.0f;
	FLOAT	lineHeight = (mTexCoords[0][3] - mTexCoords[0][1]) * mTexHeight;
	FLOAT	widest = 0.0f;
	FLOAT	total = lineHeight;
	for (WCHAR *p = text; *p; )
	{
		WCHAR	c = *p++;
		if (c == '\n')
		{
			line = 0.0f;
			total += lineHeight;
		}
		else if (c == '\r')
			continue;

		FLOAT	u1 = mTexCoords[RemapChar(c)][0];
		FLOAT	u2 = mTexCoords[RemapChar(c)][2];
		line += (u2 - u1) * mTexWidth;
		if (!mSysFont)
			line += 1.f;
		if (line > widest)
			widest = line;
	}
	size->cx = (int)widest;
	size->cy = (int)total;
	return TRUE;
}

// A font from the level's resources: the texture, then the font's record and its texture coordinates.
void	CDXBitmapFont::Deserialize(CChunkReader *c)
{
	c->GetNext();
	c->GetNext();
	mTexture = CDXTexture::Deserialize(FALSE, c);
	mTexture->AddRef();
	LT.MoveDeviceObjectToFirst(mTexture);
	c->GetNext();
	c->Read(mFontName, 0x50, 1);
	c->Read(&mHeight, 4, 1);
	c->Read(&mFlags, 4, 1);
	c->Read(mTextureName, 0x100, 1);
	c->Read(&mTexWidth, 4, 1);
	c->Read(&mTexHeight, 4, 1);
	c->GetNext();
	c->Read(&mTextScale, 4, 1);
	c->Read(mTexCoords, 0x1000, 1);
	mSysFont = FALSE;
	mVBufTexture = CVBufTexture::GetTextureByTex(mTexture);
	mVBufTexture->SetupV(GLYPH_FVF, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, sizeof(GlyphVertex), D3DPT_TRIANGLELIST, D3DPOOL_DEFAULT);
	mVBufTexture->SetupI(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 2, D3DPOOL_DEFAULT);
}

BOOL	CDXBitmapFont::IsValid()
{
	return mTexture && mTexture->GetTexture() ? TRUE : FALSE;
}

// The debug font's cells hold only plain ASCII: accented lower-case letters draw as their plain capitals.
int		CDXBitmapDebugFont::RemapChar(WCHAR c)
{
	if (c < 0x80)
		return c - 32;
	if (c >= 0xe0 && c <= 0xe5)
		return 'A';
	if (c == 0xe7)
		return 'C';
	if (c >= 0xe8 && c <= 0xeb)
		return 'E';
	if (c >= 0xec && c <= 0xef)
		return 'I';
	if (c == 0xf1)
		return 'N';
	if (c >= 0xf2 && c <= 0xf6)
		return 'O';
	if (c >= 0xf9 && c <= 0xfc)
		return 'U';
	return 0;
}
