// damage.cpp: reconstructed from damage.obj (00440b70..00441083); Stuart's release has no copy. The landscape's
// damage marks: a texture of mip levels built from damage0.tga (inverted, box-filtered, rescaled) and up to 10000
// marks kept on one list per landscape cell, recycled round-robin after the level's locked ones. The allocations'
// __LINE__s are the retail ones; the #lines place them.

#include "common.h"
#define DAMAGE_FULL
#include "landscape.h"
#include "tgaloader.h"
#include "chunker.h"

// Each mip level's offset in the texture data (the smallest first; 00628298), and so the total for n levels at n + 1.
SINT	MIP_OFFSETS[] = { 0, 1, 5, 21, 85, 341, 1365, 5461 };

//******************************************************************************************
CDamage::CDamage()	// FUNCTION: 0x00440b70
{
	mTexture = NULL;
	mLoaded = FALSE;
}

//******************************************************************************************
void CDamage::Init()	// FUNCTION: 0x00440b90
{
	if (!mLoaded)
	{
#line 22
		mTexture = new(MEMTYPE_LANDSCAPE) CDamageTexture;
		mTexture->Load("data\\textures\\mixers\\damage0.tga");
	}
	Reset();
}

//******************************************************************************************
void CDamage::Shutdown()	// FUNCTION: 0x00440c00
{
	if (mTexture)
		mTexture->Free();
	SAFE_DELETE(mTexture);
}

//******************************************************************************************
void CDamage::Reset()	// FUNCTION: 0x00440c40
{
	memset(mMarks, 0, sizeof(mMarks));
	memset(mCells, 0, sizeof(mCells));
	mCount = mLockedCount = 1;
}

//******************************************************************************************
void CDamageTexture::Load(char *name)	// FUNCTION: 0x00440c70
{
	SINT		status;
	CTGALoader	tga(name, &status);
	tga.Load();

	mLevels = 0;
	switch (tga.mWidth)
	{
	case 4:		mLevels = 2;	break;
	case 8:		mLevels = 3;	break;
	case 16:	mLevels = 4;	break;
	case 32:	mLevels = 5;	break;
	case 64:	mLevels = 6;	break;
	}

	mSize = MIP_OFFSETS[mLevels + 1];
#line 78
	mData = new(MEMTYPE_LANDSCAPE) UBYTE[mSize];

	// the top level, inverted
	UBYTE	*src = tga.mData;
	UBYTE	*dst = mData + MIP_OFFSETS[mLevels];
	SINT	w = tga.mWidth;
	SINT	n;
	for (n = w * w; n; n--)
	{
		*dst++ = 255 - *src;
		src += 3;
	}

	// each lower level, box-filtered from the one above
	for (SINT l = mLevels - 1; l >= 1; l--)
	{
		SINT	sw = w;
		w >>= 1;
		src = mData + MIP_OFFSETS[l + 1];
		dst = mData + MIP_OFFSETS[l];
		for (SINT y = w; y; y--)
		{
			for (SINT x = w; x; x--)
			{
				SINT sum = src[1] + src[sw + 1];
				sum += src[0];
				sum += src[sw];
				*dst++ = (sum << 6) >> 8;
				src += 2;
			}
			src += sw;
		}
	}

	// rescaled to 32 down to 20
	dst = mData;
	for (n = MIP_OFFSETS[mLevels + 1]; n; n--)
	{
		*dst = 0x20 - (*dst * 3) / 64;
		dst++;
	}
}

//******************************************************************************************
void CDamageTexture::Deserialize(CChunkReader *c)
{
	c->Read(&mLevels, 4, 1);
	mSize = MIP_OFFSETS[mLevels + 1];
#line 117
	mData = new(MEMTYPE_LANDSCAPE) UBYTE[mSize];
	c->Read(mData, mSize, 1);
}

//******************************************************************************************
void CDamage::Add(SWORD cell, UBYTE x, UBYTE y, UBYTE size)	// FUNCTION: 0x00440eb0
{
	CDamageMark	*m = &mMarks[mCount];

	// off whichever list it was on
	UWORD	prev = m->mPrev;
	UWORD	next = m->mNext;
	if (prev & 0x8000)
		mCells[prev & ~0x8000] = next;
	else
		mMarks[prev].mNext = next;
	mMarks[next].mPrev = prev;

	// and onto the front of the cell's
	UWORD	*head = &mCells[cell];
	UWORD	first = *head;
	m->mNext = first;
	if (first)
		mMarks[first].mPrev = (UWORD)mCount;
	*head = (UWORD)mCount;
	m->mX = x;
	m->mY = y;
	m->mPrev = cell | 0x8000;
	m->mSize = size;

	if (++mCount == 10000)
		mCount = mLockedCount;
}

//******************************************************************************************
void CDamage::Remove(SWORD cell, UBYTE x, UBYTE y)	// FUNCTION: 0x00440f80
{
	UWORD	i = mCells[cell];
	while (i)
	{
		CDamageMark	*m = &mMarks[i];
		if (m->mX == x && m->mY == y)
		{
			UWORD	prev = m->mPrev;
			UWORD	next = m->mNext;
			if (prev & 0x8000)
				mCells[prev & ~0x8000] = next;
			else
				mMarks[prev].mNext = next;
			mMarks[next].mPrev = prev;
		}
		i = m->mNext;
	}
}

//******************************************************************************************
void CDamage::DeserializeAll(CChunkReader *c)	// FUNCTION: 0x00441000
{
#line 198
	mTexture = new(MEMTYPE_LANDSCAPE) CDamageTexture;
	mTexture->Deserialize(c);
	mLoaded = TRUE;
}

//******************************************************************************************
// The empty destructor (CDXLandscape's exit 005446f9). It compiles to a lone ret, the body DebugTrace already has, so
// the linker folds it into that one (/OPT:ICF) and the annotation names the survivor.

// FUNCTION: 0x0040c640
CDamage::~CDamage()
{
}
