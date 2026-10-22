// SPDX-License-Identifier: GPL-3.0-only
// port/platform/public/DXFont.cpp: the PC bitmap font (CDXBitmapFont, Platform.h), written for the public build in
// place of src/DXFont.cpp. A font is either a texture of square character cells (a bitmap font, whose characters'
// widths are measured from the texture's alpha) or a Windows font drawn into a new texture (a system font). Text is
// drawn as one screen-space quad per character through the shared quad buffer. The arithmetic is the game's, in the
// same order, so text lands on the same pixels and measures the same widths; the front end's 640 x 480 coordinates
// scale by the window through multiplications by 1/640 and 1/480, as the game's compiled code does.
#include	"Common.h"
#include	"Platform.h"
#include	"Texture.h"
#include	"VBufTexture.h"
#include	"FastVB.h"
#include	"frontend.h"
#include	"tweak.h"
#include	"chunker.h"
#include	"mathsconst.h"

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
	if (FRONTEND.IsRunning())
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
		LT.D3D_SetTexture(0, mTexture->GetTexture());
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
