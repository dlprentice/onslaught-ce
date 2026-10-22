// LandscapeTexture.cpp: reconstructed from LandscapeTexture.obj (0048e3d0..0048f1bf); Stuart's release has no copy.
// The tile cache allocation's __LINE__ (91) is placed with #line.
#include "common.h"
#define LANDSCAPETEXTURE_CPP
#define DAMAGE_FULL
#include "maths.h"
#include "ltshell.h"
#include "console.h"
#include "tweak.h"
#include "LandscapeTexture.h"
#include "landscape.h"
#include "DXLandscape.h"
#include "engine.h"
#include "map.h"
#include "maptex.h"

// FUNCTION: 0x0048e3d0 init:LANDSCAPE_LOWRES
// FUNCTION: 0x0048e3f0 exit:LANDSCAPE_LOWRES
CTweakSLONG	LANDSCAPE_LOWRES("LANDSCAPE_LOWRES", 0);					// 006fabd0
// FUNCTION: 0x0048e400 init:LANDSCAPE_TEXTURES_MANAGED
// FUNCTION: 0x0048e420 exit:LANDSCAPE_TEXTURES_MANAGED
CTweakSLONG	LANDSCAPE_TEXTURES_MANAGED("LANDSCAPE_TEXTURES_MANAGED", 0);	// 006fabc0

IDirect3DTexture9	*CLandscapeTexture::sTexture;
SINT		CLandscapeTexture::sRefCount;
SINT		CLandscapeTexture::sShift;
UINT		CLandscapeTexture::sSize = 512;
SLandscapeTile	CLandscapeTexture::sQueue[50];
UWORD		CLandscapeTexture::sColours[512 * 512];
SLandscapeTile	*CLandscapeTexture::sQueueEnd = CLandscapeTexture::sQueue;

//******************************************************************************************
// FUNCTION: 0x0048e430
CLandscapeTexture::CLandscapeTexture()
{
	mDirty = FALSE;
	mTileCache = NULL;
}

// FUNCTION: 0x0048e450
CLandscapeTexture::~CLandscapeTexture()
{
	if (mTileCache)
		delete [] mTileCache;
	if (--sRefCount == 0)
	{
		sTexture->Release();
		sTexture = NULL;
	}
}

//******************************************************************************************
// FUNCTION: 0x0048e4d0
HRESULT	CLandscapeTexture::Init(SINT level, SINT n)
{
	mLevel = level;
	switch (level)
	{
	case 0:
		mMaskX = 0x3f;
		mMaskY = 0xfc0;
		break;
	case 1:
		mMaskX = 0x1f;
		mMaskY = 0x7c0;
		break;
	case 2:
		mMaskX = 0xf;
		mMaskY = 0x3c0;
		break;
	case 3:
		mMaskX = 0x7;
		mMaskY = 0x1c0;
		break;
	case 4:
		mMaskX = 0x3;
		mMaskY = 0xc0;
		break;
	}
	mTiles = 512 / (1 << level);
	mTileMask = mTiles - 1;
	mUnknown30 = n;
	InitDeviceObjects();
	sRefCount++;
	if (level)
	{
		mTileCacheSize = 512 / (8 << level);
		mTileCacheSize *= mTileCacheSize;
#line 91
		mTileCache = new(MEMTYPE_LANDSCAPE) UWORD[mTileCacheSize];
		memset(mTileCache, 0xff, mTileCacheSize * 2);
	}
	HRESULT	hr = CUMTexture::Init(sSize, 1, 1);
	sTexture->AddDirtyRect(NULL);
	if (!LANDSCAPE_TEXTURES_MANAGED.mValue)
		LT.GetDevice()->UpdateTexture(sTexture, mTexture);
	return hr;
}

//******************************************************************************************
// FUNCTION: 0x0048e610
HRESULT	CLandscapeTexture::RestoreDeviceObjects()
{
	HRESULT	hr = CUMTexture::RestoreDeviceObjects();
	if (FAILED(hr))
		return hr;
	if (mDirty)
		Refresh();
	return hr;
}

//******************************************************************************************
// FUNCTION: 0x0048e670
HRESULT	CLandscapeTexture::InitDeviceObjects()
{
	if (!sTexture)
	{
		// the shared system-memory copy: 512 square, halved for LANDSCAPE_LOWRES and the device's limits
		sSize = 512;
		sShift = 0;
		if (LANDSCAPE_LOWRES.mValue)
		{
			sSize = 256;
			sShift = 1;
		}
		while (sSize > LT.m_d3dCaps.MaxTextureWidth && sSize > LT.m_d3dCaps.MaxTextureHeight)
		{
			sSize >>= 1;
			sShift++;
		}
		CONSOLE.FatalError(sSize >= 128, 0xc5, -1);
		LT.D3D_CreateTexture(sSize, sSize, 1, 0, D3DFMT_R5G6B5, D3DPOOL_SYSTEMMEM, &sTexture);

		D3DLOCKED_RECT	lr;
		if (SUCCEEDED(sTexture->LockRect(0, &lr, NULL, 0)))
		{
			UWORD	*line = (UWORD *)lr.pBits;
			for (UINT y = sSize; y; y--)
			{
				UWORD	*p = line;
				for (UINT x = sSize; x; x--)
					*p++ = 0x18e3;
				line = (UWORD *)((UBYTE *)line + lr.Pitch);
			}
			sTexture->UnlockRect(0);
		}
	}
	return CUMTexture::Init(sSize, 1, 1);
}

//******************************************************************************************
// FUNCTION: 0x0048e790
HRESULT	CLandscapeTexture::DeleteDeviceObjects()
{
	if (sTexture)
	{
		sTexture->Release();
		sTexture = NULL;
	}
	return S_OK;
}

// FUNCTION: 0x0048e7b0
void	CLandscapeTexture::ResetUpdateQueue()
{
	sQueueEnd = sQueue;
}

//******************************************************************************************
// Redraws the queued tiles, keeping the deferrable ones back while others were drawn; a second pass draws one
// anyway when nothing else was. The log of what was drawn is built and not used.
// FUNCTION: 0x0048e7c0
void	CLandscapeTexture::FlushUpdateQueue()
{
	char	log[52];
	SINT	updated = 0;
	SINT	n = 0;
	SLandscapeTile	*dst;
	for (SINT pass = 0; pass < 2; pass++)
	{
		dst = sQueue;
		for (SLandscapeTile *src = sQueue; src < sQueueEnd; src++)
		{
			if (src->mUrgent && ((pass == 0) || (updated >= 1)))
			{
				*dst = *src;
				dst++;
			}
			else
			{
				src->mTexture->UpdateTile(src->mTile);
				updated++;
				log[n++] = src->mUrgent ? 'x' : 'X';
			}
		}
		if ((pass == 0) && (updated >= 1))
			break;
	}
	sQueueEnd = dst;
	log[n] = 0;
}

//******************************************************************************************
// A tile already queued moves to the end, deferrable only if both requests were.
// FUNCTION: 0x0048e880
void	CLandscapeTexture::QueueTileUpdate(UWORD tile, BOOL urgent)
{
	SINT	x = (((tile & 0x3f) << 3) & mTileMask) << mLevel;
	SINT	y = (((tile >> 3) & mTileMask) & ~7) << mLevel;
	BOOL	found = FALSE;
	for (SLandscapeTile *t = sQueue; t < sQueueEnd; t++)
	{
		if ((t->mX == x) && (t->mY == y) && (t->mTexture == this))
		{
			if (!t->mUrgent)
				urgent = FALSE;
			*t = sQueueEnd[-1];
			sQueueEnd--;
			found = TRUE;
			break;
		}
	}
	if ((!found) && (sQueueEnd >= sQueue + 50))
		FlushUpdateQueue();
	sQueueEnd->mTile = tile;
	sQueueEnd->mTexture = this;
	sQueueEnd->mX = x;
	sQueueEnd->mY = y;
	sQueueEnd->mUrgent = urgent;
	sQueueEnd++;
}

//******************************************************************************************
// FUNCTION: 0x0048e950
void	CLandscapeTexture::CopyTileToTexture(RECT *rect)
{
	if (sShift)
	{
		RECT	r;
		r.left = rect->left >> sShift;
		r.right = rect->right >> sShift;
		r.top = rect->top >> sShift;
		r.bottom = rect->bottom >> sShift;

		IDirect3DTexture9	*tex = LANDSCAPE_TEXTURES_MANAGED.mValue ? mTexture : sTexture;
		D3DLOCKED_RECT		lr;
		if (SUCCEEDED(tex->LockRect(0, &lr, &r, 0)))
		{
			UWORD	*dst = (UWORD *)lr.pBits;
			UWORD	*src = &sColours[(rect->top << 9) + rect->left];
			for (SINT y = 0; y < r.bottom - r.top; y++)
				for (SINT x = 0; x < r.right - r.left; x++)
					dst[(lr.Pitch >> 1) * y + x] = src[((y << sShift) << 9) + (x << sShift)];
		}
	}
	if (LANDSCAPE_TEXTURES_MANAGED.mValue)
		mTexture->UnlockRect(0);
	else
		sTexture->UnlockRect(0);
}

//******************************************************************************************
// Prepare the high-resolution target before testing the result; the colour-map path needs no lock.
// Each resolution path owns its upload guard.
// FUNCTION: 0x0048ea80
void	CLandscapeTexture::UpdateTile(UWORD tile)
{
	SINT	y;
	RECT			r;
	D3DLOCKED_RECT	lr;
	SINT	x;
	SINT	shift = mLevel - sShift;
	x = (tile & 0x3f) << 3;
	y = (tile >> 6) << 3;
	mDirty = TRUE;

	if (shift < 0)
	{
		// finer than the shared copy: build the tile in the colour map, then copy it down
		r.left = (x & mTileMask) << mLevel;
		r.top = (y & mTileMask) << mLevel;
		SINT	size = 8 << mLevel;
		r.right = r.left + size;
		r.bottom = r.top + size;
		HRESULT	hr = S_OK;
		if (!sShift)
		{
			if (LANDSCAPE_TEXTURES_MANAGED.mValue)
				hr = mTexture->LockRect(0, &lr, &r, 0);
			else
				hr = sTexture->LockRect(0, &lr, &r, 0);
		}
		else
		{
			lr.Pitch = 0x400;
			lr.pBits = &sColours[(r.top << 9) + r.left];
		}
		if (FAILED(hr))
			return;
		MAP.Unknown47eff0(mLevel, &ENGINE.GetMapTex()[mUnknown30], lr.pBits, lr.Pitch / 2, tile, 5, 0, 0, 8, 8);
		{
			UWORD	*dst = (UWORD *)lr.pBits;
			UWORD	first = CDXLandscape::mDamage.mCells[tile];
			CDamageMark	*m = first ? &CDXLandscape::mDamage.mMarks[first] : NULL;
			if (m)
			{
				SINT	pitch = lr.Pitch / 2;
				SINT	down = 4 - mLevel;
				SINT	size = 1 << (mLevel + 3);
				do
				{
					SINT	n = m->mSize - down;
					if (n > 0)
						BlendAlpha(dst, pitch, CDXLandscape::mDamage.mTexture->GetLevel(n),
								   (char)m->mX >> down, (char)m->mY >> down, n, size);
					m = m->mNext ? &CDXLandscape::mDamage.mMarks[m->mNext] : NULL;
				} while (m);
			}
		}
		CopyTileToTexture(&r);
		if (!LANDSCAPE_TEXTURES_MANAGED.mValue)
			LT.GetDevice()->UpdateTexture(sTexture, mTexture);
	}
	else
	{
		r.left = (x & mTileMask) << shift;
		SINT	size = 8 << shift;
		r.top = (y & mTileMask) << shift;
		r.right = r.left + size;
		r.bottom = r.top + size;
		IDirect3DTexture9	*tex = LANDSCAPE_TEXTURES_MANAGED.mValue ? mTexture : sTexture;
		if (SUCCEEDED(tex->LockRect(0, &lr, &r, 0)))
		{
			UWORD	first;
			CDamageMark	*m;
			MAP.Unknown47eff0(shift, &ENGINE.GetMapTex()[mUnknown30], lr.pBits, lr.Pitch / 2, tile, 5, 0, 0, 8, 8);
			first = CDXLandscape::mDamage.mCells[tile];
			UWORD	*dst;
			dst = (UWORD *)lr.pBits;
			m = first ? &CDXLandscape::mDamage.mMarks[first] : NULL;
			if (m)
			{
				SINT	pitch = lr.Pitch / 2;
				SINT	down = 4 - mLevel;
				SINT	size = 1 << (mLevel + 3);
				do
				{
					SINT	n = m->mSize - down;
					if (n > 0)
						BlendAlpha(dst, pitch, CDXLandscape::mDamage.mTexture->GetLevel(n),
								   (char)m->mX >> down, (char)m->mY >> down, n, size);
					m = m->mNext ? &CDXLandscape::mDamage.mMarks[m->mNext] : NULL;
				} while (m);
			}
			if (LANDSCAPE_TEXTURES_MANAGED.mValue)
				mTexture->UnlockRect(0);
			else
				sTexture->UnlockRect(0);
		}
		if (!LANDSCAPE_TEXTURES_MANAGED.mValue)
			LT.GetDevice()->UpdateTexture(sTexture, mTexture);
	}
}

//******************************************************************************************
// Blends a damage mark's alpha into a tile: texels under alpha below 32 darken by alpha / 32 (565, two channels at
// once), clipped to the tile.
// FUNCTION: 0x0048ee00
void	CLandscapeTexture::BlendAlpha(UWORD *dst, SINT pitch, UBYTE *alpha, SINT x, SINT y, SINT shift, SINT limit)
{
	SINT	size = 1 << shift;
	SINT	w = size;
	SINT	h = size;
	if (x < 0)
	{
		w = size + x;
		alpha -= x;
	}
	else
	{
		if (x + size > limit)
			w = limit - x;
		dst += x;
	}
	if (y < 0)
	{
		h = size + y;
		alpha -= size * y;
	}
	else
	{
		if (y + size > limit)
			h = limit - y;
		dst += pitch * y;
	}
	for (; h; h--)
	{
		UBYTE	*a = alpha;
		UWORD	*d = dst;
		for (SINT i = w; i; i--)
		{
			SINT	v = *a;
			if (v < 32)
			{
				DWORD	c = *d;
				c = (c + (c << 16)) & 0x7e0f81f;
				c = ((c * v) >> 5) & 0x7e0f81f;
				*d = UWORD((c >> 16) + c);
			}
			a++;
			d++;
		}
		dst += pitch;
		alpha += size;
	}
}

// The colour-map arm's pitch store (see UpdateTileRange).
static inline void	SetPitch(D3DLOCKED_RECT &lr, INT pitch) { lr.Pitch = pitch; }

//******************************************************************************************
// The lock takes UpdateTile's form, which puts its sTexture and colour-map arms after the epilogue as in retail
// (0048f155, 0048f15f); the loop reads lr.Pitch, which the calls may change, so the pitch is multiplied and halved in
// every column (0048efdc, 0048eff8), and read again for half after the blit (0048f04f). The tile is a SINT passed whole
// to the blit, cut to UWORD only for the damage cell (0048f024). The destination is retail's common subexpression:
// computed once per column, stored at its definition (0048efff) and reloaded for BlendAlpha (0048f0a9). Over lr.Pitch
// itself VC6 computes it twice (the blit call kills lr's fields, a synthetic probe), and a named destination is
// the hottest web (priority 848) and takes esi. Over a copy of the pitch taken in the column the two uses are one
// expression, and its temporary, a compiler temporary queued last, gets no register: m takes esi, this edi, y0 ebp, x0
// stays in its argument slot and ebx is pushed only around the loops, as in retail. The column's byte offset is a named
// local, the hottest web (priority 768): it takes eax while the pitch is still live for the half, so the pitch takes
// edx (0048ef7e, the arm's 0x400 at 0048f164), is copied for the multiply and for the division (0048efd6, 0048efe9) and
// reloaded after the offset counter is stored (0048f0e9). The columns count i from 0, stepped before x: VC6 makes
// i * 16 an induction variable set after the inner loop's guard (0048efc3; a px counter is set before it). py += 8
// precedes y++ (0048f107). The rect is written left, right, top, bottom: the parameters' first references then list y0
// before y1, so the column offset takes y0's home and py y1's (0048efc3, 0048ef8c). The colour-map arm sets pBits
// before the pitch, which gives retail's prologue (x1 loaded with x0 before the pushes, 0048ef07, and the rect in
// eax/ecx/edx).
// STAND-IN (operand rank, no code): the arm stores the pitch through SetPitch, a file-local inline, so lr.Pitch's field
// symbol is first named there, after py, and outranks it: retail multiplies the pitch copy by py in memory (imul
// eax,[py], 0048efdc). With `lr.Pitch = 0x400` written in the arm the field symbol is numbered before py and py is
// loaded and multiplied by the pitch instead (4/4 shape/instructions). The helper emits the same store. Original
// spelling unknown.
// FUNCTION: 0x0048ef00
void	CLandscapeTexture::UpdateTileRange(SINT x0, SINT y0, SINT x1, SINT y1)
{
	RECT			r;
	D3DLOCKED_RECT	lr;
	r.left = x0 * 8;
	r.right = x1 * 8 + 8;
	r.top = y0 * 8;
	r.bottom = y1 * 8 + 8;
	HRESULT	hr = S_OK;
	if (!sShift)
	{
		if (LANDSCAPE_TEXTURES_MANAGED.mValue)
			hr = mTexture->LockRect(0, &lr, &r, 0);
		else
			hr = sTexture->LockRect(0, &lr, &r, 0);
	}
	else
	{
		lr.pBits = &sColours[(r.top << 9) + r.left];
		SetPitch(lr, 0x400);
	}
	if (FAILED(hr))
		return;

	UBYTE	*bits = (UBYTE *)lr.pBits;
	SINT	py = 0;
	for (SINT y = y0; y <= y1; py += 8, y++)
	{
		for (SINT i = 0, x = x0; x <= x1; i++, x++)
		{
			SINT	tile = (y << 6) + x;
			SINT	pitch = lr.Pitch;
			SINT	ofs = pitch * py + i * 16;
			MAP.Unknown47eff0(mLevel, &ENGINE.GetMapTex()[mUnknown30], bits + ofs, pitch / 2, tile, 5, 0, 0, 8, 8);
			UWORD	first = CDXLandscape::mDamage.mCells[(UWORD)tile];
			CDamageMark	*m = first ? &CDXLandscape::mDamage.mMarks[first] : NULL;
			if (m)
			{
				SINT	half = lr.Pitch / 2;
				SINT	down = 4 - mLevel;
				SINT	size = 1 << (mLevel + 3);
				do
				{
					SINT	n = m->mSize - down;
					if (n > 0)
						BlendAlpha((UWORD *)(bits + ofs), half, CDXLandscape::mDamage.mTexture->GetLevel(n),
								   (char)m->mX >> down, (char)m->mY >> down, n, size);
					m = m->mNext ? &CDXLandscape::mDamage.mMarks[m->mNext] : NULL;
				} while (m);
			}
		}
	}
	CopyTileToTexture(&r);
	if (!LANDSCAPE_TEXTURES_MANAGED.mValue)
		LT.GetDevice()->UpdateTexture(sTexture, mTexture);
}

//******************************************************************************************
// FUNCTION: 0x0048f180
void	CLandscapeTexture::Refresh()
{
	mDirty = TRUE;
	if (mTileCache)
		memset(mTileCache, 0xff, mTileCacheSize * 2);
	else
		UpdateTileRange(0, 0, 63, 63);
}

#include "mathsconst.h"	// the maths constant's include, where retail's initializer falls (placement evidence)	// FUNCTION: 0x0048f1c0 init:ZERO_FVECTOR
// STAND-IN (link layout): retail's 56 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_LandscapeTexture[56] = { 0 };
