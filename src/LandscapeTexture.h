// LandscapeTexture.h: CLandscapeTexture (vtable 005dc1f0; LandscapeTexture.obj 0048e3d0..0048f1bf), one mip level
// of the landscape's texture. All levels draw through one shared system-memory texture (sTexture) the size of the
// top level, copied into each level's own texture; tiles are redrawn from the height field's lit texels and the
// damage marks, and queued so a frame redraws a bounded number.

#ifndef LANDSCAPETEXTURE_H
#define LANDSCAPETEXTURE_H

#include "UMTexture.h"

class CLandscapeTexture;

// A queued tile (0x14 bytes; the queue is 0x6fa7d8..0x6fabc0, fifty of them).
struct SLandscapeTile
{
	CLandscapeTexture	*mTexture;			// +0x00
	UWORD			mTile;					// +0x04: y << 6 | x in 8-metre tiles
	UWORD			mPad06;
	BOOL			mUrgent;				// +0x08
	SINT			mX;						// +0x0c: texels at this level
	SINT			mY;						// +0x10
};

class CLandscapeTexture : public CUMTexture
{
public:
					CLandscapeTexture();									// 0048e430
					~CLandscapeTexture();									// 0048e450
	HRESULT			Init(SINT level, SINT n);								// 0048e4d0
	virtual HRESULT	RestoreDeviceObjects();									// 0048e610
	virtual HRESULT	InitDeviceObjects();									// 0048e670
	virtual HRESULT	DeleteDeviceObjects();									// 0048e790
	static void		ResetUpdateQueue();										// 0048e7b0
	static void		FlushUpdateQueue();										// 0048e7c0
	void			QueueTileUpdate(UWORD tile, BOOL urgent);				// 0048e880
	void			CopyTileToTexture(RECT *rect);							// 0048e950
	void			UpdateTile(UWORD tile);									// 0048ea80
	void			BlendAlpha(UWORD *dst, SINT pitch, UBYTE *alpha, SINT x, SINT y, SINT shift, SINT limit);	// 0048ee00
	void			UpdateTileRange(SINT x0, SINT y0, SINT x1, SINT y1);	// 0048ef00
	void			Refresh();												// 0048f180
	// marks a tile's cache entry stale (CDXLandscape::UpdateArea 00547510, AddDamage 005477a8). The index is a statement
	// of its own: retail reads mMaskY through the texture's address before it forms the texture pointer (00547510,
	// 005477a8), which the one-expression form doesn't give.
	void			InvalidateTile(UWORD tile)
	{
		SINT	index = ((tile & mMaskY) >> mLevel) + (tile & mMaskX);
		mTileCache[index] |= 0x8000;
	}
#ifdef DXLANDSCAPE_CPP
	// (These members exist only where DXLANDSCAPE_CPP is defined, so the class definition already differs between
	// translation units: a formal portability/ODR limit that predates the request's members. They add no data member,
	// virtual function or layout change.)
	// the tile's cache entry at this level, and a request for the tile there: queued unless the entry holds it; urgent
	// when only stale (CalcLODs 00547153). CalcLODs passes its tile index to both, and retail reads that index from
	// memory once for the entry (00547157) and again for the request (0054718b); one RequestTile(UWORD tile) reads it
	// once. The split is inferred from those reads; the original members are unknown. RequestTile takes the tile before
	// the entry: the pinned VC6 evaluates this call's arguments right to left (observed for this call; C++ leaves the
	// order unspecified), so CalcLODs forms the entry before it reads t again, as retail does.
	// The flag is bound to a const reference: retail keeps it in a memory byte (stored at 005471a4, reloaded masked at
	// 005471ab), which a reference to the comparison's temporary gives; a plain bool stays in a register. The original
	// spelling is unknown.
	// The flag is the whole urgent predicate, the tile changed and only its stale bit differs: `(tile != old) &&
	// ((old & 0x7fff) == tile)` (a suggested form). VC6 drops the first operand, known true under the
	// guard, but as an operand of && the mask is done in 16 bits, retail's `and ax,0x7fff` (00547197), the only 16-bit
	// and with a constant in the game's own code. The bare comparison, a cast, a masked UWORD local, `old &= 0x7fff`,
	// signed types, a bitfield or a UWORD-typed mask mask in 32 bits (`and eax,0x7fff`); `& ~0x8000` masks the high
	// byte (`and ah,0x7f`); testing the stale bit itself (`(old & 0x8000) && ...`) keeps a `test ah,0x80`. The particle
	// descriptors' `p->mFoR && (p->mFoR->mFlags | 1)` is narrowed the same way (`or ax,0x1`, 004c1c04). The original
	// spelling is unknown (synthetic probes and CalcLODs).
	// STAND-IN (two self-copy arms, CalcLODs 00547153..00547188): the row and the index are named and each has an arm
	// that assigns it its own value again. Neither arm can change anything: for defined shift counts the masked UWORD
	// row and index are never negative (a negative or out-of-range level stays the undefined input it already was).
	// Both arms emit no logic: they vanish after register allocation with their tests. The row's arm makes the row a
	// value crossing a block, coloured eax before the texture pointer and the masked tile, which then take edx and edi
	// as retail's (00547169..00547181); the index's arm keeps the masked x alive past the sum, so the sum goes to the
	// row's eax: `add eax,ecx` (00547183).
	// Plain form `return &mTileCache[((tile & mMaskY) >> mLevel) + (tile & mMaskX)];`: pointer eax, masked tile edx.
	// The original spelling is unknown; replace the arms when a natural form gives those registers.
	UWORD			*TileEntry(UWORD tile)
	{
		SINT	y = (tile & mMaskY) >> mLevel;
		if (y < 0)
			y = (tile & mMaskY) >> mLevel;
		SINT	index = y + (tile & mMaskX);
		if (index < 0)
			index = y + (tile & mMaskX);
		return &mTileCache[index];
	}
	// STAND-IN (self-copy arm on old, CalcLODs 0054718f): the stale entry re-read when its stale bit is set gives the same
	// value; the arm vanishes after register allocation. It raises old's priority above the request's tile, so old
	// takes eax and the second read of t ecx, as retail's (0054718b..00547192). Plain form (no arm): old ecx, tile eax.
	// The original spelling is unknown; replace the arm when a natural form orders old first.
	void			RequestTile(UWORD tile, UWORD *entry)
	{
		UWORD	old = *entry;
		if ((SWORD)old < 0)
			old = *entry;
		if (tile != old)
		{
			*entry = tile;
			const bool	&stale = ((tile != old) && ((old & 0x7fff) == tile));
			QueueTileUpdate(tile, stale);
		}
	}
#endif

	BOOL			mDirty;					// +0x2c
	SINT			mUnknown30;				// +0x30: Init's second argument; indexes 0x4c-byte records off ENGINE
	SINT			mLevel;					// +0x34
	SINT			mTiles;					// +0x38: 512 >> level
	SINT			mTileMask;				// +0x3c
	UWORD			*mTileCache;			// +0x40: one word per tile, 0xffff when stale (levels past 0)
	SINT			mTileCacheSize;			// +0x44
	UWORD			mMaskX;					// +0x48
	UWORD			mMaskY;					// +0x4a
	SINT			mUnknown4C;				// +0x4c: the object is 0x50 bytes (CDXLandscape::Init allocates 0x50, 00544b76)

	static IDirect3DTexture9	*sTexture;	// 006fabf4
	static SINT		sRefCount;				// 006fabf8
	static SINT		sShift;					// 006fabf0: how many times the shared texture was halved
	static UINT		sSize;					// 0062d864: 512 unless LANDSCAPE_LOWRES or the device's limits halve it
	static SLandscapeTile	sQueue[50];		// 006fa7d8
	static UWORD	sColours[512 * 512];	// 0067a7d8: the full-resolution landscape colours (R5G6B5) the tiles are built in
	static SLandscapeTile	*sQueueEnd;		// 0062d868
};

#endif
