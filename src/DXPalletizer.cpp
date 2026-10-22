// DXPalletizer.cpp: reconstructed from DXPalletizer.obj (0054e500..0054f5bf); Stuart's release has no copy. The
// allocations' __LINE__ values are the retail ones; the #line directives place them, because the retail file's layout
// is unknown.
#include "common.h"
#include "DXPalletizer.h"
#include "map.h"

// The GS swizzle tables, in their retail order (DXPalletizer.obj's data, 00651360..00651d5f). SwizzleBlock reads the
// last three it names; the others are unreferenced.
static int	sUnusedSwizzle[256] =
{
	0, 68, 8, 76, 16, 84, 24, 92, 1, 69, 9, 77, 17, 85, 25, 93, 2, 70, 10, 78, 18, 86, 26, 94, 3, 71, 11, 79, 19, 87, 27, 95,
	4, 64, 12, 72, 20, 80, 28, 88, 5, 65, 13, 73, 21, 81, 29, 89, 6, 66, 14, 74, 22, 82, 30, 90, 7, 67, 15, 75, 23, 83, 31, 91,
	32, 100, 40, 108, 48, 116, 56, 124, 33, 101, 41, 109, 49, 117, 57, 125, 34, 102, 42, 110, 50, 118, 58, 126, 35, 103, 43, 111, 51, 119, 59, 127,
	36, 96, 44, 104, 52, 112, 60, 120, 37, 97, 45, 105, 53, 113, 61, 121, 38, 98, 46, 106, 54, 114, 62, 122, 39, 99, 47, 107, 55, 115, 63, 123,
	4, 64, 12, 72, 20, 80, 28, 88, 5, 65, 13, 73, 21, 81, 29, 89, 6, 66, 14, 74, 22, 82, 30, 90, 7, 67, 15, 75, 23, 83, 31, 91,
	0, 68, 8, 76, 16, 84, 24, 92, 1, 69, 9, 77, 17, 85, 25, 93, 2, 70, 10, 78, 18, 86, 26, 94, 3, 71, 11, 79, 19, 87, 27, 95,
	36, 96, 44, 104, 52, 112, 60, 120, 37, 97, 45, 105, 53, 113, 61, 121, 38, 98, 46, 106, 54, 114, 62, 122, 39, 99, 47, 107, 55, 115, 63, 123,
	32, 100, 40, 108, 48, 116, 56, 124, 33, 101, 41, 109, 49, 117, 57, 125, 34, 102, 42, 110, 50, 118, 58, 126, 35, 103, 43, 111, 51, 119, 59, 127,
};

static int	sSmallSwizzle[128] =		// 00651760: a texture smaller than a block, even then odd strips
{
	0, 36, 8, 44, 1, 37, 9, 45, 2, 38, 10, 46, 3, 39, 11, 47, 4, 32, 12, 40, 5, 33, 13, 41, 6, 34, 14, 42, 7, 35, 15, 43,
	16, 52, 24, 60, 17, 53, 25, 61, 18, 54, 26, 62, 19, 55, 27, 63, 20, 48, 28, 56, 21, 49, 29, 57, 22, 50, 30, 58, 23, 51, 31, 59,
	4, 32, 12, 40, 5, 33, 13, 41, 6, 34, 14, 42, 7, 35, 15, 43, 0, 36, 8, 44, 1, 37, 9, 45, 2, 38, 10, 46, 3, 39, 11, 47,
	20, 48, 28, 56, 21, 49, 29, 57, 22, 50, 30, 58, 23, 51, 31, 59, 16, 52, 24, 60, 17, 53, 25, 61, 18, 54, 26, 62, 19, 55, 27, 63,
};

static int	sColumnSwizzle[128] =		// 00651960: a 16x16 block's columns, even then odd
{
	0, 36, 8, 44, 1, 37, 9, 45, 2, 38, 10, 46, 3, 39, 11, 47, 4, 32, 12, 40, 5, 33, 13, 41, 6, 34, 14, 42, 7, 35, 15, 43,
	16, 52, 24, 60, 17, 53, 25, 61, 18, 54, 26, 62, 19, 55, 27, 63, 20, 48, 28, 56, 21, 49, 29, 57, 22, 50, 30, 58, 23, 51, 31, 59,
	4, 32, 12, 40, 5, 33, 13, 41, 6, 34, 14, 42, 7, 35, 15, 43, 0, 36, 8, 44, 1, 37, 9, 45, 2, 38, 10, 46, 3, 39, 11, 47,
	20, 48, 28, 56, 21, 49, 29, 57, 22, 50, 30, 58, 23, 51, 31, 59, 16, 52, 24, 60, 17, 53, 25, 61, 18, 54, 26, 62, 19, 55, 27, 63,
};

static int	sUnusedBlock4[32] =
{
	0, 2, 8, 10, 1, 3, 9, 11, 4, 6, 12, 14, 5, 7, 13, 15, 16, 18, 24, 26, 17, 19, 25, 27, 20, 22, 28, 30, 21, 23, 29, 31,
};

static int	sUnusedBlock32[32] =
{
	0, 1, 4, 5, 16, 17, 20, 21, 2, 3, 6, 7, 18, 19, 22, 23, 8, 9, 12, 13, 24, 25, 28, 29, 10, 11, 14, 15, 26, 27, 30, 31,
};

static int	sBlockOrder[32] =			// 00651c60: the block each 16x16 source block becomes
{
	0, 1, 4, 5, 16, 17, 20, 21, 2, 3, 6, 7, 18, 19, 22, 23, 8, 9, 12, 13, 24, 25, 28, 29, 10, 11, 14, 15, 26, 27, 30, 31,
};

static int	sBlock8[32] =				// 00651ce0: the PSMT8 block layout
{
	0, 1, 4, 5, 16, 17, 20, 21,
	2, 3, 6, 7, 18, 19, 22, 23,
	8, 9, 12, 13, 24, 25, 28, 29,
	10, 11, 14, 15, 26, 27, 30, 31,
};

//******************************************************************************************
// Walks down from this node by the colour's bits, from bit depth to bit 0, making the nodes it needs; a new node
// takes the colour at the centre of its cube, or the colour itself at the bottom.
#line 6
CPalNode *CPalNode::Insert(UBYTE r, UBYTE g, UBYTE b, UBYTE a, char depth)	// FUNCTION: 0x0054e500
{
	CPalNode	*node = this;
	while (depth >= 0)
	{
		UBYTE	rs = r >> depth;
		UBYTE	rb = rs & 1;
		UBYTE	gs = g >> depth;
		UBYTE	gb = gs & 1;
		UBYTE	bs = b >> depth;
		UBYTE	bb = bs & 1;
		UBYTE	as = a >> depth;
		UBYTE	ab = as & 1;
		if (!node->mChildren[rb][gb][bb][ab])
		{
			node->mLeaf = FALSE;
			node->mChildren[rb][gb][bb][ab] = new(MT_PALETTIZER) CPalNode;
			if (depth > 0)
			{
				node->mChildren[rb][gb][bb][ab]->mColour.r = ((rs << 1) + 1) << (depth - 1);
				node->mChildren[rb][gb][bb][ab]->mColour.g = ((gs << 1) + 1) << (depth - 1);
				node->mChildren[rb][gb][bb][ab]->mColour.b = ((bs << 1) + 1) << (depth - 1);
				node->mChildren[rb][gb][bb][ab]->mColour.a = ((as << 1) + 1) << (depth - 1);
			}
			else
			{
				node->mChildren[rb][gb][bb][ab]->mColour.r = r;
				node->mChildren[rb][gb][bb][ab]->mColour.g = g;
				node->mChildren[rb][gb][bb][ab]->mColour.b = b;
				node->mChildren[rb][gb][bb][ab]->mColour.a = a;
			}
		}
		node = node->mChildren[rb][gb][bb][ab];
		depth--;
	}
	node->mCount++;
	return node;
}

//******************************************************************************************
void CPalNode::BuildPalette(CPalColour *palette)	// FUNCTION: 0x0054e670
{
	if (mIndex >= 0)
		palette[mIndex] = mColour;

	for (SINT r = 0; r < 2; r++)
		for (SINT g = 0; g < 2; g++)
			for (SINT b = 0; b < 2; b++)
				for (SINT a = 0; a < 2; a++)
					if (mChildren[r][g][b][a])
						mChildren[r][g][b][a]->BuildPalette(palette);
}

//******************************************************************************************
// Gives a palette entry to every leaf standing for at least threshold pixels; returns the pixels given entries.
DWORD CPalNode::AssignIndices(DWORD *next, DWORD threshold)	// FUNCTION: 0x0054e6e0
{
	if (mLeaf)
	{
		if (mCount >= threshold)
		{
			mIndex = *next;
			mAssigned = TRUE;
			(*next)++;
			DWORD	count = mCount;
			mCount = 0;
			return count;
		}
		return 0;
	}

	DWORD	total = 0;
	for (SINT r = 0; r < 2; r++)
		for (SINT g = 0; g < 2; g++)
			for (SINT b = 0; b < 2; b++)
				for (SINT a = 0; a < 2; a++)
					if (mChildren[r][g][b][a])
						total += mChildren[r][g][b][a]->AssignIndices(next, threshold);
	return total;
}

//******************************************************************************************
// Folds the deepest level of unassigned leaves into their parents.
void CPalNode::Collapse()	// FUNCTION: 0x0054e790
{
	if (mAssigned || mLeaf)
		return;

	BOOL	leaves = TRUE;
	SINT	r, g, b, a;
	for (r = 0; r < 2; r++)
		for (g = 0; g < 2; g++)
			for (b = 0; b < 2; b++)
				for (a = 0; a < 2; a++)
					if ((mChildren[r][g][b][a]) && (!mChildren[r][g][b][a]->mLeaf))
						leaves = FALSE;

	if (leaves)
	{
		mLeaf = TRUE;
		for (r = 0; r < 2; r++)
			for (g = 0; g < 2; g++)
				for (b = 0; b < 2; b++)
					for (a = 0; a < 2; a++)
					{
						if (mChildren[r][g][b][a])
						{
							if (!mChildren[r][g][b][a]->mAssigned)
							{
								mCount += mChildren[r][g][b][a]->mCount;
								delete mChildren[r][g][b][a];
								mChildren[r][g][b][a] = NULL;
							}
							else
							{
								mCount += mChildren[r][g][b][a]->mCount;
								mAssigned = TRUE;
							}

						}
					}
	}
	else
	{
		for (r = 0; r < 2; r++)
			for (g = 0; g < 2; g++)
				for (b = 0; b < 2; b++)
					for (a = 0; a < 2; a++)
						if (mChildren[r][g][b][a])
							mChildren[r][g][b][a]->Collapse();
	}
}

// FUNCTION: 0x0054e950 ??_GCPalNode@@QAEPAXI@Z

//******************************************************************************************
// Reduces the width x height pixels at src (bytes blue, green, red, alpha) to a palette of colours entries: an
// octree of every colour, whose leaves get entries in order of how many pixels they stand for, the least used folded
// into their parents until everything has an entry. The indices go to *pixels and the palette to *palette (both
// allocated when allocate is set), optionally swizzled for the GS; darken appends a second half to the palette, each
// entry scaled by the fog colour over the ambient.
// Matched with labelled STAND-INs (a register-allocation row).
// - Retail keeps colours in ebp from `shr ebp,1` (0054eabd) to the end, count and bs in memory (0054ead6, 0054ed72),
//   rs in edi and gs in ebx (0054ed3d, 0054ed55), the darken loop's down-counter colours * 2 - colours in memory
//   (0054ed9c), and in the indices loop indices in ebx and i in esi (0054ec35, 0054ec3b).
// - Without the arms the builder gives colours' main web 101, the darken scales rs, gs and bs 122 each and i 114. The
//   scales pop first (rs edi, gs ebp, bs ebx) and bs takes ebx, colours' last free register, so colours is split and
//   reloaded all over, and i takes ebx (retail: esi). The 117 web of the earlier diagnosis is the down-counter (the log
//   names it colours). With the natural forms below, forcing colours' symbol to 122 or more
//   (the priority tool; ties go to colours' higher tie key, 398) gave retail's code exactly.
// STAND-IN: two assignments of a value the variable already holds, each behind a test: the red channel's quotient,
// named red, again under `if (rs > 0)` in the darken loop, and fog again under three tests of colours.
// - Mechanism (as in CDXBitmapFont::InitFromTexture): CSE turns each arm into a self-copy that reaches the register
//   allocator as its own blocks, tests included, and is deleted after allocation with its branch and compares, so no
//   code is emitted and nothing changes at run time. The red arm's test splits the darken loop body, so the loop
//   references of gs and bs sit in smaller blocks (122 -> 66) and colours pops before them; the fog arm's three test
//   blocks reference colours (101 -> 115), so it also pops before i (114) and takes ebp, which leaves ebx the last
//   register of i's neighbours: i takes esi and indices ebx, as in retail.
// - Each arm is needed: without the red arm 40/140 (shape/instructions), without the fog arm 1/15 (i and indices
//   swapped). No fog condition with one or two tests of colours matched (36 tried; two tests give colours 111); 16 of
//   the 56 three-test conditions tried match, the others swap i and indices or two spilled darken temporaries' frame
//   slots. No single arm matched (about 1,800 variants measured).
// - Natural forms, not stand-ins: pal and out are assigned before *palette and *pixels (retail stores each in its own
//   arm), left = width * height (retail's `test esi,esi` after the copy), and the two loop forms commented below. The
//   indices loop stores in each arm of an if/else, as retail lays it out (full_alpha tested once per pixel, the
//   unshifted alpha as the fall-through, `shr cl,1` in the else arm); the ?: spelling left 60 masked blocks.
// FUNCTION: 0x0054e9d0
#line 235
void CDXPalletizer::Palletize(UBYTE *src, SINT width, SINT height, DWORD colours, UBYTE **pixels, CPalColour **palette,
							  BOOL alpha, BOOL allocate, BOOL swizzle, BOOL full_alpha, BOOL darken, BOOL swizzle_palette)
{
	mRoot = new(MT_PALETTIZER) CPalNode;
	CPalColour	grey;
	grey.r = 0x80;
	grey.g = 0x80;
	grey.b = 0x80;
	grey.a = 0x80;
	mRoot->mColour = grey;

	CPalColour	*pal;
	if (allocate)
	{
#line 244
		pal = new(MEMTYPE_SCRATCHPAD) CPalColour[colours];
		*palette = pal;
	}
	else
		pal = *palette;

	mNumColours = 0;
	if (darken)
		colours >>= 1;

	SINT		count = width * height;
	CPalNode	*node = mRoot;
	UBYTE		*p = src;
	SINT		i;
	for (i = 0; i < count; i++, p += 4)
	{
		if (*(DWORD *)&node->mColour == *(DWORD *)p)
			node->mCount++;
		else if (alpha)
		{
			if (full_alpha)
				node = mRoot->Insert(p[2], p[1], p[0], p[3], 7);
			else
				node = mRoot->Insert(p[2], p[1], p[0], p[3] >> 1, 7);
		}
		else
			node = mRoot->Insert(p[2], p[1], p[0], 0x80, 7);
	}

	DWORD	left = width * height;
	while (left)
	{
		DWORD	got = mRoot->AssignIndices(&mNumColours, (left - mNumColours + colours - 1) / (colours - mNumColours));
		left -= got;
		if (!left)
			break;
		// Written with the test inside: retail keeps it at the top and jumps back to it (a plain while is rotated).
		while (1)
		{
			if (got <= 0)
				break;
			got = mRoot->AssignIndices(&mNumColours, (left - mNumColours + colours - 1) / (colours - mNumColours));
			left -= got;
			if (!left)
				break;
		}
		if (left)
			mRoot->Collapse();
	}

	mRoot->BuildPalette(mPalette);

	UBYTE	*out;
	if (allocate)
	{
#line 310
		out = new(MEMTYPE_SCRATCHPAD) UBYTE[count];
		*pixels = out;
	}
	else
		out = *pixels;

	DWORD	used[256];
	memset(used, 0, sizeof(used));

#line 327
	UBYTE	*indices = new(MEMTYPE_SCRATCHPAD) UBYTE[count];
	p = src;
	for (i = 0; i < count; i++, p += 4)
	{
		if (full_alpha)
			indices[i] = FindNearestColour(p[2], p[1], p[0], p[3]);
		else
			indices[i] = FindNearestColour(p[2], p[1], p[0], p[3] >> 1);
		used[indices[i]]++;
	}

	if (swizzle)
	{
		SwizzleTexture(width, height, indices, out);
		delete [] indices;
	}
	else
		memcpy(out, indices, count);

	if (darken)
	{
		DWORD	fog = MAP.GetSun();
		if (colours > 1 || colours < 256 || colours == 256)	// STAND-IN (see above)
			fog = MAP.GetSun();
		DWORD	ambient = MAP.GetAmbient();
		SINT	rs = (SINT)((((ambient >> 16) & 0xff) << 8) / (((fog >> 16) & 0xfe) + 1) + 0xff) / 2;
		SINT	gs = (SINT)((ambient & 0xff00) / (((fog >> 8) & 0xfe) + 1) + 0xff) / 2;
		SINT	bs = (SINT)(((ambient & 0xff) << 8) / ((fog & 0xfe) + 1) + 0xff) / 2;
		for (DWORD c = colours; c < colours * 2; c++)
		{
			CPalColour	col = mPalette[c - colours];
			SINT	red = (col.r * rs) / 255;
			if (rs > 0)						// STAND-IN (see above)
				red = (col.r * rs) / 255;
			mPalette[c].r = red;
			mPalette[c].g = (col.g * gs) / 255;
			mPalette[c].b = (col.b * bs) / 255;
		}
		colours *= 2;
	}

	if ((colours == 256) && ((swizzle) || (swizzle_palette)))
	{
		CPalColour	*from = mPalette;
		// j counts blocks of four rows: C2 makes j * 4 the induction variable and leaves the j and k loops alone
		// (j in memory, tested against 0x20), as retail does; counting j by 4 itself optimised all four loops.
		for (SINT j = 0; j < 8; j++)
			for (SINT k = 0; k < 2; k++)
				for (SINT l = 0; l < 2; l++)
					for (SINT m = 0; m < 8; m++)
						pal[(j * 4 + k) * 8 + l * 16 + m] = *from++;
	}
	else
	{
		for (i = 0; i < (SINT)colours; i++)
			pal[i] = mPalette[i];
	}

	delete mRoot;
	mRoot = NULL;
}

//******************************************************************************************
UBYTE CDXPalletizer::FindNearestColour(UBYTE r, UBYTE g, UBYTE b, UBYTE a)	// FUNCTION: 0x0054ef70
{
	UBYTE	best_index = 0;
	DWORD	best = abs(a - mPalette[0].a) + abs(b - mPalette[0].b) + abs(g - mPalette[0].g) + abs(r - mPalette[0].r);
	for (DWORD i = 1; i < mNumColours; i++)
	{
		DWORD	d = abs(r - mPalette[i].r) + abs(a - mPalette[i].a) + abs(g - mPalette[i].g) + abs(b - mPalette[i].b);
		if (d < best)
		{
			best = d;
			best_index = (UBYTE)i;
		}
	}
	return best_index;
}

//******************************************************************************************
// Swizzles one 128x64 block of 8-bit texels for the GS, 16x16 at a time (a texture smaller than a block one
// four-row strip at a time). The tables are walked with running indices (k++): VC6 steps a row pointer at the top of
// each row for them, as retail does, where indexing [row][i * 4 + j] runs one merged pointer through the table.
// The block loop matches retail once the allocator sees the same webs in the same order (it began at 462 bytes):
// - The column loop's table entry is a named v, as in the small path's loop. A named web is coloured before the
//   counters, so v takes ecx and n edx, c ebx as retail has them; unnamed, n took ecx, c edx and the entry ebx.
// - The copy loops step their own pointers. C2 makes the destination's induction temporary before the source's
//   whatever the order of the statements, so retail's to = eax, from = ecx and its step order (to first) need the
//   user pointers; the copy-out's o is set before its d, as retail's early lea edx,[out] shows.
// - order is declared before row: that decides which of the two takes the lower frame slot (retail's order does).
// - b is read straight after the copy-in loop. Read after the column loops, the copy-out head's reloads took eax and
//   ecx the wrong way round (orderp in eax, block_y loaded after b); the position of that one load moves them.
int CDXPalletizer::SwizzleBlock(int width, int height, UBYTE *src, UBYTE *dst)	// FUNCTION: 0x0054f090
{
	int		*block_x = new int[32];
	int		*block_y = new int[32];
	int		x, y;
	int		k = 0;
	for (y = 0; y < 4; y++)
		for (x = 0; x < 8; x++)
		{
			int		n = sBlock8[k++];
			block_x[n] = x;
			block_y[n] = y;
		}

	int		blocks_x = width / 16;
	int		blocks_y = height / 16;
	UBYTE	out[256];
	UBYTE	in[256];
	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	if ((blocks_x) && (blocks_y))
	{
		for (y = 0; y < blocks_y; y++)
		{
			int		*order = sBlockOrder + y * blocks_x;
			UBYTE	*row = src + y * 16 * 128;
			for (x = 0; x < blocks_x; x++)
			{
				UBYTE	*s = row + x * 16;
				int		i;
				UBYTE	*from = s;
				UBYTE	*to = in;
				for (i = 0; i < 16; i++)
				{
					memcpy(to, from, 16);
					to += 16;
					from += 128;
				}

				int		b = order[x];
				int		n = 0;
				UBYTE	*column = in;
				for (DWORD c = 0; c < 4; c++)
				{
					int		k = (c & 1) * 64;
					for (i = 0; i < 16; i++)
						for (int j = 0; j < 4; j++)
						{
							int		v = sColumnSwizzle[k++];
							out[n++] = column[v];
						}
					column += 64;
				}

				UBYTE	*o = out;
				UBYTE	*d = dst + ((block_y[b] * 64) + block_x[b]) * 32;
				for (i = 0; i < 8; i++)
				{
					memcpy(d, o, 32);
					o += 32;
					d += 256;
				}
			}
		}
	}
	else
	{
		int		n = 0;
		UBYTE	*s = src;
		for (DWORD strip = 0; strip < (DWORD)((height + 3) / 4); strip++)
		{
			int		k = (strip & 1) * 64;
			for (DWORD i = 0; i < (DWORD)width; i++)
				for (int j = 0; j < 4; j++)
				{
					DWORD	v = sSmallSwizzle[k++];
					dst[n++] = s[v - (v >> 4) * (16 - width)];
				}
			s += 64;
		}
	}

	delete block_x;
	delete block_y;
	return 0;
}

//******************************************************************************************
// Swizzles a whole texture (each side a power of two up to 1024) one 128x64 block at a time.
// y also counts the height check. On that shared counter VC6 does not strength-reduce the y loop, so retail computes
// w * blocks_x and the block bases inside it; with a counter of its own they are hoisted.
// The row copies step their own pointers, the strided one first: retail sets them up before each loop's test.
int CDXPalletizer::SwizzleTexture(int width, int height, UBYTE *src, UBYTE *dst)	// FUNCTION: 0x0054f380
{
	int		x, y, i;
	for (x = 0; x < 11; x++)
		if (width == (1024 >> x))
			break;
	if (x == 11)
		return -1;
	for (y = 0; y < 11; y++)
		if (height == (1024 >> y))
			break;
	if (y == 11)
		return -1;

	UBYTE	block[128 * 64];
	UBYTE	swizzled[128 * 64];
	memset(block, 0, sizeof(block));
	memset(swizzled, 0, sizeof(swizzled));

	int		blocks_x = (width - 1) / 128 + 1;
	int		blocks_y = (height - 1) / 64 + 1;
	int		w, w2, h, h2;
	if (blocks_x == 1)
	{
		w = width;
		w2 = width * 2;
	}
	else
	{
		w = 128;
		w2 = 256;
	}
	if (blocks_y == 1)
	{
		h = height;
		h2 = height / 2;
	}
	else
	{
		h = 64;
		h2 = 32;
	}

	for (y = 0; y < blocks_y; y++)
	{
		for (x = 0; x < blocks_x; x++)
		{
			UBYTE	*s = src + (w * blocks_x) * y * 64 + x * w;
			UBYTE	*to = block;
			for (i = 0; i < h; i++)
			{
				memcpy(to, s, w);
				s += w * blocks_x;
				to += 128;
			}

			SwizzleBlock(128, 64, block, swizzled);

			UBYTE	*d = dst + (h2 * blocks_x * y + x) * w2;
			UBYTE	*from = swizzled;
			for (i = 0; i < h2; i++)
			{
				memcpy(d, from, w2);
				d += w2 * blocks_x;
				from += 256;
			}
		}
	}
	return 0;
}
