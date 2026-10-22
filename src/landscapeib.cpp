// landscapeib.cpp: reconstructed from landscapeib.obj (0048df20..0048e3c7); Stuart's release has no copy. The
// allocation's __LINE__ (44) is placed with #line.
#include "common.h"
#define LANDSCAPEIB_CPP
#include "ltshell.h"
#include "landscapeib.h"
#include "landscape.h"
#include "NvTriStrip.h"

struct STriangle
{
	UWORD	a, b, c;
};

//******************************************************************************************
// FUNCTION: 0x0048df20
void	CLandscapeIB::CreateIndexBuffer()
{
	// the patch manager's precomputed strips, when the level brought them: copy this set's range
	if (PATCHMANAGER.mLoaded)
	{
		SINT	i = mLevel * 16 + mEdges;
		SINT	n = PATCHMANAGER.mOffsets[i + 1] - PATCHMANAGER.mOffsets[i];
		LT.D3D_CreateIndexBuffer(n * 2, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &mIB);
		mCount = n - 2;
		UWORD	*p;
		if (mIB->Lock(0, 0, (void **)&p, D3DLOCK_NOSYSLOCK) >= 0)
		{
			memcpy(p, PATCHMANAGER.mData + PATCHMANAGER.mOffsets[mLevel * 16 + mEdges], n * 2);
			Unlock();
		}
		return;
	}

#line 44
	STriangle	*tris = new(MEMTYPE_UNKNOWN) STriangle[mSize * mSize * 2];
	SINT	stride = mSize + 1;
	SINT	row = mSize * 2;
	STriangle	*t = tris;
	SINT	x, n;
	for (n = 0; n < mSize; n++)
	{
		for (x = 0; x < mSize; x++)
		{
			SINT	i = n * stride + x;
			t[0].a = i;
			t[0].b = i + stride;
			t[0].c = i + stride + 1;
			t[1].a = i;
			t[1].b = i + stride + 1;
			t[1].c = i + 1;
			t += 2;
		}
	}

	if (mEdges & 1)
	{
		STriangle	*p = tris + 1;
		for (n = mSize / 2; n; n--)
		{
			p[0].c++;
			p[1].a++;
			p[2].c = 0;
			p[2].b = 0;
			p[2].a = 0;
			p += 4;
		}
	}
	if (mEdges & 2)
	{
		STriangle	*corner = tris + row - 2;
		STriangle	*p = corner;
		for (n = mSize / 2; n; n--)
		{
			p[0].c -= stride;
			p[row + 1].c -= stride;
			p[1].c = 0;
			p[1].b = 0;
			p[1].a = 0;
			p += row * 2;
		}
		if (mEdges & 1)
		{
			corner[0].c = 0;
			corner[0].b = 0;
			corner[0].a = 0;
		}
	}
	if (mEdges & 8)
	{
		STriangle	*p = tris;
		for (n = mSize / 2; n; n--)
		{
			p[0].b += stride;
			p[row + 1].a += stride;
			p[row].c = 0;
			p[row].b = 0;
			p[row].a = 0;
			p += row * 2;
		}
	}
	if (mEdges & 4)
	{
		STriangle	*p = tris + (mSize - 1) * row;
		for (n = mSize / 2; n; n--)
		{
			p[1].b--;
			p[2].b--;
			p[0].c = 0;
			p[0].b = 0;
			p[0].a = 0;
			p += 4;
		}
		p = tris + (mSize - 1) * row;
		if (mEdges & 8)
		{
			p[1].c = 0;
			p[1].b = 0;
			p[1].a = 0;
		}
	}

	// drop the triangles the stitching emptied
	SINT	count = 0;
	t = tris;
	STriangle	*dst = tris;
	for (n = mSize * mSize * 2; n; n--)
	{
		if (t->c | t->b | t->a)
		{
			*dst = *t;
			dst++;
			count++;
		}
		t++;
	}

	SetCacheSize(24);
	SetStitchStrips(true);
	SetMinStripSize(0);
	SetListsOnly(false);
	PrimitiveGroup	*groups;
	UWORD	*p;
	unsigned short	num_groups;
	GenerateStrips((unsigned short *)tris, count * 3, &groups, &num_groups);

	mCount = groups->numIndices;
	LT.D3D_CreateIndexBuffer(mCount * 2, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &mIB);
	if (mIB)
	{
		p = NULL;
		mIB->Lock(0, 0, (void **)&p, D3DLOCK_NOSYSLOCK);
		if (p)
		{
			memcpy(p, groups->indices, mCount * 2);
			Unlock();
		}
		delete [] groups;
		mCount -= 2;
		delete [] tris;
	}
}

//******************************************************************************************
// FUNCTION: 0x0048e310 ??1PrimitiveGroup@@QAE@XZ

// FUNCTION: 0x0048e330
CLandscapeIB::CLandscapeIB()
{
	mSize = 0;
}

// The destructor is the compiler's: a jump to ~CIBuffer with no vtable store.
// FUNCTION: 0x0048e350 ??1CLandscapeIB@@UAE@XZ

// FUNCTION: 0x0048e360
void	CLandscapeIB::Init(SINT level, SINT edges)
{
	mLevel = level;
	mEdges = edges;
	mSize = 2 << level;
	SINT	count = mSize * mSize * 2;
	SINT	half = mSize / 2;
	if (edges & 1)
		count -= half;
	if (edges & 2)
		count -= half;
	if (edges & 4)
		count -= half;
	if (edges & 8)
		count -= half;
	mCount = count;
	mPool = D3DPOOL_MANAGED;
	InitDeviceObjects();
}

// FUNCTION: 0x0048e3c0
HRESULT	CLandscapeIB::InitDeviceObjects()
{
	CreateIndexBuffer();
	return S_OK;
}

