// maptex.cpp: reconstructed from maptex.obj (00491160..00491764); Stuart's release has no copy. The landscape's mixer
// textures: six 32-bit textures per mip level (grass, rock, sand, snow, road and concrete), the top level loaded
// from TGA files and each lower one averaged down from the one above. The allocations' __LINE__s are set by #line.
#include "common.h"
#define MAPTEX_CPP
#include "maths.h"
#include "maptex.h"
#include "tgaloader.h"
#include "chunker.h"

static char	*MIXER_NAMES[MT_NUMTEX] = { "grass", "rock", "sand", "snow", "road", "concrete" };	// 0062da98

//******************************************************************************************
// FUNCTION: 0x00491160
CMapTex::CMapTex()
{
	mUnknown04 = 0;
	mData = NULL;
	mUnknown08 = NULL;
	mSet = -1;
}

// FUNCTION: 0x00491180
CMapTex::~CMapTex()
{
	mSet = -1;
	SAFE_DELETE_ARRAY(mData);
	SAFE_DELETE_ARRAY(mUnknown08);
}

//******************************************************************************************
// Texture n from a TGA file; the alpha becomes a signed blend weight ((a >> 2) - 32) and its range is kept.
// FUNCTION: 0x004911c0
BOOL	CMapTex::LoadTexture(char *name, int size, int n)
{
	SINT		status;
	CTGALoader	tga(name, &status);
	if (!tga.Load())
		return FALSE;

	SINT	count = size * size;
	UBYTE	*dest = mData + mTexSize * n;
	UBYTE	*src = tga.mData;
	SINT	i;
	for (i = count; i; i--)
	{
		dest[0] = src[0];
		dest[1] = src[1];
		dest[2] = src[2];
		dest[3] = 0;
		dest += 4;
		src += 3;
	}

	if (tga.mAlpha)
	{
		char	lo = 127;
		char	hi = -128;
		UBYTE	*a = tga.mAlpha;
		char	*d = (char *)(mData + mTexSize * n + 3);
		for (i = count; i; i--)
		{
			char	v = char((*a >> 2) - 32);
			*d = v;
			if (v > hi)
				hi = v;
			if (v < lo)
				lo = v;
			d += 4;
			a++;
		}
		mMinAlpha[n] = lo;
		mMaxAlpha[n] = hi;
	}
	else
	{
		char	lo = 127;
		char	hi = -128;
		char	*d = (char *)(mData + mTexSize * n + 3);
		for (i = count; i; i--)
		{
			*d = 0;
			if (hi < 0)
				hi = 0;
			if (lo > 0)
				lo = 0;
			d += 4;
		}
		mMinAlpha[n] = lo;
		mMaxAlpha[n] = hi;
	}
	return TRUE;
}

//******************************************************************************************
// FUNCTION: 0x00491340
void	CMapTex::Downsample(UBYTE *dest, UBYTE *src)
{
	SINT	stride = mSize * 8;
	for (SINT y = mSize; y; y--)
	{
		for (SINT x = mSize; x; x--)
		{
			dest[0] = UBYTE((src[0] + src[4] + src[stride] + src[stride + 4]) >> 2);
			dest[1] = UBYTE((src[1] + src[5] + src[stride + 1] + src[stride + 5]) >> 2);
			dest[2] = UBYTE((src[2] + src[6] + src[stride + 2] + src[stride + 6]) >> 2);
			dest[3] = UBYTE((char(src[3]) + char(src[7]) + char(src[stride + 3]) + char(src[stride + 7])) / 4);
			src += 8;
			dest += 4;
		}
		src += stride;
	}
}

//******************************************************************************************
// FUNCTION: 0x004914b0
BOOL	CMapTex::LoadAll(int set, int num, int size)
{
	if (mSet != set)
	{
		char	name[52];
		char	buf[256];
		sprintf(buf, "Warning : Loading mixer texture set %d manually!\n", set);
		TRACE(buf);
		SAFE_DELETE_ARRAY(mData);
		SAFE_DELETE_ARRAY(mUnknown08);
		mSet = set;
		mTexSize = size * size * 4;
		mNum = num;
		mUnknown04 = 0;
		mSize = size;
#line 151
		mData = new(MEMTYPE_MAPTEX) UBYTE[mTexSize * num];
		for (SINT i = 0; i < mNum; i++)
		{
			sprintf(name, "data\\textures\\mixers\\%s%.2d.tga", MIXER_NAMES[i], set);
			if (!LoadTexture(name, size, i))
				return FALSE;
		}
	}
	return TRUE;
}

//******************************************************************************************
// The next mip level down from another set.
// FUNCTION: 0x004915d0
void	CMapTex::LoadAllFromMapTex(CMapTex *from)
{
	if (mSet != from->mSet)
	{
		SAFE_DELETE_ARRAY(mData);
		SAFE_DELETE_ARRAY(mUnknown08);
		mSet = from->mSet;
		mUnknown04 = 0;
		mSize = from->mSize >> 1;
		mTexSize = mSize * mSize * 4;
		mNum = from->mNum;
#line 175
		mData = new(MEMTYPE_MAPTEX) UBYTE[mTexSize * mNum];
		for (SINT i = 0; i < mNum; i++)
		{
			mMinAlpha[i] = from->mMinAlpha[i];
			mMaxAlpha[i] = from->mMaxAlpha[i];
			Downsample(mData + mTexSize * i, from->mData + from->mTexSize * i);
		}
	}
}

//******************************************************************************************
// FUNCTION: 0x004916c0
void	CMapTex::Deserialize(CChunkReader *c, int n)
{
	c->GetNext();
	c->GetNext();
	c->Read(this, sizeof(CMapTex), 1);
	if (mData)
	{
		c->GetNext();
#line 380
		mData = new(MEMTYPE_MAPTEX) UBYTE[mNum * mTexSize];
		c->Read(mData, mNum * mTexSize, 1);
	}
	if (mUnknown08)
	{
		c->GetNext();
#line 410
		mUnknown08 = new(MEMTYPE_MAPTEX) DWORD[mNum * 1024];
		c->Read(mUnknown08, mNum * 1024, 1);
	}
}

#include "mathsconst.h"	// the maths constant's include, where retail's initializer falls (placement evidence)	// FUNCTION: 0x00491770 init:ZERO_FVECTOR
