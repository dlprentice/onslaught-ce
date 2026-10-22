// DXPalletizer.h: CDXPalletizer reduces a 32-bit texture to a palette of at most 256 colours with a colour octree
// over red, green, blue and alpha (sixteen children a node), and can swizzle the result for the PS2's GS
// (reconstructed from DXPalletizer.obj, 0054e500..0054f5bf; Stuart's release has no copy). CDXEngine's landscape
// texture cache is its one user (0054797e..005479a6), with the palletizer on its stack.

#ifndef DXPALLETIZER_H
#define DXPALLETIZER_H

// A colour as the palette stores it: red first (the octree's inserts pass a pixel's bytes 2, 1, 0 and 3).
class CPalColour
{
public:
	CPalColour() {}								// an empty constructor: Palletize's new[] keeps the count store

	UBYTE		r, g, b, a;
};

// An octree node (0x54 bytes, MT_PALETTIZER).
class CPalNode
{
public:
	CPalNode()
	{
		mCount = 0;
		mLeaf = TRUE;
		mIndex = -1;
		mAssigned = FALSE;
		for (SINT r = 0; r < 2; r++)
			for (SINT g = 0; g < 2; g++)
				for (SINT b = 0; b < 2; b++)
					for (SINT a = 0; a < 2; a++)
						mChildren[r][g][b][a] = NULL;		// a loop: VC6 turns it into the rep stos (0054e5b9)
	}
	~CPalNode()
	{
		for (SINT r = 0; r < 2; r++)
			for (SINT g = 0; g < 2; g++)
				for (SINT b = 0; b < 2; b++)
					for (SINT a = 0; a < 2; a++)
					{
						delete mChildren[r][g][b][a];
						mChildren[r][g][b][a] = NULL;
					}
	}

	CPalNode	*&Child(UBYTE r, UBYTE g, UBYTE b, UBYTE a) { return mChildren[r][g][b][a]; }

	CPalNode	*Insert(UBYTE r, UBYTE g, UBYTE b, UBYTE a, char depth);	// 0054e500
	void		BuildPalette(CPalColour *palette);						// 0054e670
	DWORD		AssignIndices(DWORD *next, DWORD threshold);			// 0054e6e0
	void		Collapse();												// 0054e790

	CPalColour	mColour;				// +0x00
	SINT		mIndex;					// +0x04: the palette entry, -1 for none
	DWORD		mCount;					// +0x08: the pixels this node stands for
	BOOL		mLeaf;					// +0x0c
	BOOL		mAssigned;				// +0x10
	CPalNode	*mChildren[2][2][2][2];	// +0x14: by the next bit of red, green, blue and alpha
};

class CDXPalletizer
{
public:
	CDXPalletizer() { mRoot = NULL; mNumColours = 0; }	// inline (CDXLandscape::BuildTextureCache 0054787d)
	void		Palletize(UBYTE *src, SINT width, SINT height, DWORD colours, UBYTE **pixels, CPalColour **palette,
						  BOOL alpha, BOOL allocate, BOOL swizzle, BOOL full_alpha, BOOL darken, BOOL swizzle_palette);	// 0054e9d0
	UBYTE		FindNearestColour(UBYTE r, UBYTE g, UBYTE b, UBYTE a);	// 0054ef70

	static int	SwizzleBlock(int width, int height, UBYTE *src, UBYTE *dst);		// 0054f090
	static int	SwizzleTexture(int width, int height, UBYTE *src, UBYTE *dst);	// 0054f380

	CPalColour	mPalette[256];			// +0x000
	DWORD		mNumColours;			// +0x400
	CPalNode	*mRoot;					// +0x404
};

#endif
