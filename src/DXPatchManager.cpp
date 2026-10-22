// DXPatchManager.cpp: reconstructed from DXPatchManager.obj (005502d0..005507e7); Stuart's release has no copy. The
// landscape's patches: three lists of CLandscapeVB grids (2, 4 and 8 cells) and 48 index buffers, one for each level
// and edge mask, plus a word table read with the level. The allocations' __LINE__s are the retail ones; the #lines
// place them.

#include "common.h"
#define DXPATCHMANAGER_CPP
#include "DX.h"
#include "landscape.h"
#include "LandscapeVB.h"
#include "landscapeib.h"
#include "chunker.h"

CDXPatchManager	PATCHMANAGER;						// 009c64d8
// FUNCTION: 0x005502d0 init:PATCHMANAGER
// FUNCTION: 0x00550300 exit:PATCHMANAGER

// Out of line (CDXPatchManager::Init expands it; the linker drops the copy): the vector ctor and ??_E follow it, as retail's
void	CDXPatchList::Init(SINT count, SINT cells)
{
	mCount = count;
#line 17
	CLandscapeVB	*vbs = new (MEMTYPE_LANDSCAPE) CLandscapeVB[count];
	mPatches = (UBYTE *)vbs;
	for (SINT c0 = 0; c0 < mCount; c0++)
		((CLandscapeVB *)mPatches)[c0].Create(cells);
}

//******************************************************************************************
CDXPatchList::~CDXPatchList()	// FUNCTION: 0x005503b0
{
	if (mPatches)
	{
		delete [] (CLandscapeVB *)mPatches;
		mPatches = NULL;
	}
}

//******************************************************************************************
void	CDXPatchList::Reset()	// FUNCTION: 0x005503d0
{
	CLandscapeVB	*vb = (CLandscapeVB *)mPatches;
	for (SINT c0 = 0; c0 < mCount; vb++, c0++)
		vb->mSlot = 0xffff;
}

//******************************************************************************************
// The slot is reconstructed as an int; that is inferred, not the original signature, which is unknown. Allocate's
// code is byte-identical with an int or a UWORD slot (it stores only the low word, 00550424), and CDXLandscape::CalcLODs
// widens its UWORD once before the search (`and edi,0xffff`, 00546f96), which an int parameter gives there.
CLandscapeVB	*CDXPatchList::Allocate(SINT slot)	// FUNCTION: 0x00550400
{
	CLandscapeVB	*vb = (CLandscapeVB *)mPatches;
	for (SINT c0 = mCount; c0; vb++, c0--)
	{
		if (vb->mSlot == 0xffff)
		{
			vb->mSlot = slot;
			return vb;
		}
	}
	return NULL;
}

//******************************************************************************************
// Near miss: the second list's inlined loop (00550556) keeps the patch pointer in eax where ours uses ecx; the first
// and third match.
void	CDXPatchManager::Init(int a, int b, int c)	// FUNCTION: 0x00550430
{
#line 84
	mLists = new (MEMTYPE_LANDSCAPE) CDXPatchList[3];
	mLists[0].Init(a, 2);
	mLists[1].Init(b, 4);
	mLists[2].Init(c, 8);

#line 91
	mIBs = new (MEMTYPE_LANDSCAPE) CLandscapeIB[48];
	for (SINT level = 0; level < 3; level++)
		for (SINT edges = 0; edges < 16; edges++)
			mIBs[level * 16 + edges].Init(level, edges);
}

//******************************************************************************************
void	CDXPatchManager::Shutdown()	// FUNCTION: 0x005506e0
{
	if (mLists)
	{
		delete [] mLists;
		mLists = NULL;
	}
	if (mIBs)
	{
		delete [] mIBs;
		mIBs = NULL;
	}
}

//******************************************************************************************
void	CDXPatchManager::FreeData()	// FUNCTION: 0x00550730
{
	if (mData)
	{
		delete [] mData;
		mData = NULL;
	}
}

//******************************************************************************************
void	CDXPatchManager::DeserializeAll(CChunkReader *c)	// FUNCTION: 0x00550750
{
	for (SINT level = 0; level < 3; level++)
		for (SINT edges = 0; edges < 16; edges++)
		{
			SINT	v;
			c->Read(&v, 4, 1);
			mOffsets[level * 16 + edges] = v;
		}

	SINT	count;
	c->Read(&count, 4, 1);
	mOffsets[3 * 16] = count;
#line 148
	mData = new (MEMTYPE_LANDSCAPE) UWORD[count];
	c->Read(mData, 2, count);
	mLoaded = TRUE;
}
// STAND-IN (link layout): retail's 16 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_DXPatchManager[16] = { 0 };

// FUNCTION: 0x00550320 ??_ECLandscapeVB@@UAEPAXI@Z
// FUNCTION: 0x00550380 ??0CLandscapeVB@@QAE@XZ
// FUNCTION: 0x005503a0 ??1CLandscapeVB@@UAE@XZ
// FUNCTION: 0x00550680 ??_ECLandscapeIB@@UAEPAXI@Z
