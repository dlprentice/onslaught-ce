// vbuftexture.cpp: reconstructed from vbuftexture.obj (005003c0..0050162b); Stuart's release has no copy.
// The allocations' __LINE__ values are the retail ones.
#include "common.h"
#include "ltshell.h"
#include "console.h"
#include "renderinfo.h"
#include "tweak.h"
#include "vbuffer.h"
#include "ibuffer.h"
#include "VBufTexture.h"
#include "Texture.h"

class CDebugLog
{
public:
	void	AddMessage(char *fmt, ...);
};
extern CDebugLog SETUP_HISTORY;


int		VBUFTEXTURE_UNUSED = 1900000;						// 00633d28 (unreferenced)
bool	VBUFTEXTURE_NO_DOWNSIZE = true;						// 00633d2c: DownSizeSome does nothing while set
bool			CVBufTexture::mDoubleBuffer;	// placement evidence: retail's .bss order (created before CLAIM_WRITEONLY)
CVBufTexture	*CVBufTexture::mFirst;
CTweakSLONG	CLAIM_WRITEONLY("CLAIM_WRITEONLY", 0);			// 00854de0
// FUNCTION: 0x005003c0 init:CLAIM_WRITEONLY
// FUNCTION: 0x005003e0 exit:CLAIM_WRITEONLY

bool	VBUFTEXTURE_VALIDATE;								// 00854dd9
bool	VBUFTEXTURE_VALIDATE_FAILED;						// 00854dd8


static inline SINT RoundSize(SINT size)
{
	if (size == 0)
		return 0;
	SINT s = 1024;
	while (s < size)
		s += s;
	return s;
}

//******************************************************************************************
CVBufTexture::CVBufTexture(CDXTexture *texture)	// FUNCTION: 0x005003f0
{
	mFVF = 0;
	mVUsage = 0;
	mVB[0] = NULL;
	mVB[1] = NULL;
	mCurrentVB = 0;
	mIB = NULL;
	mStride = 0;
	mVSize[0] = 0;
	mVSize[1] = 0;
	mLastVUsed = 0;
	mVUsed = 0;
	mVLocked = false;
	mVData = NULL;
	mPrimType = 0;
	mIFormat = 0;
	mIUsage = 0;
	mISize = 0;
	mIUsed = 0;
	mILocked = false;
	mIData = NULL;
	mNext = mFirst;
	mFirst = this;
	mRefCount = 0;
	mTexture = texture;
	if (texture)
		texture->AddRef();
	mRefCount++;
}

//******************************************************************************************
CVBufTexture::~CVBufTexture()	// FUNCTION: 0x00500460
{
	if (mTexture && mTexture->mVBufTexture == this)
		mTexture->mVBufTexture = NULL;
	if (mTexture)
	{
		mTexture->Release();
		mTexture = NULL;
	}
	if (mVB[0])
	{
		UnlockVB();
		SAFE_DELETE(mVB[0]);
	}
	if (mVB[1])
	{
		UnlockVB();
		SAFE_DELETE(mVB[1]);
	}
	if (mIB)
	{
		UnlockIB();
		SAFE_DELETE(mIB);
	}
	if (mFirst == this)
		mFirst = mNext;
	else
	{
		CVBufTexture *v = mFirst;
		while (v->mNext != this)
			v = v->mNext;
		v->mNext = mNext;
	}
}

//******************************************************************************************
void CVBufTexture::SetVBFormat(DWORD fvf, DWORD usage, SINT stride, SINT primtype, SINT pool)	// FUNCTION: 0x00500540
{
	mFVF = fvf;
	if (!CLAIM_WRITEONLY.mValue)
	{
		usage &= ~(D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC);
		pool = D3DPOOL_MANAGED;
	}
	mVUsage = usage;
	mStride = stride;
	mPrimType = primtype;
	mVPool = pool;
}

//******************************************************************************************
void CVBufTexture::SetIBFormat(SINT format, DWORD usage, SINT size, SINT pool)	// FUNCTION: 0x00500590
{
	mIFormat = format;
	if (!CLAIM_WRITEONLY.mValue)
	{
		usage &= ~(D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC);
		pool = D3DPOOL_MANAGED;
	}
	mIUsage = usage;
	mIPool = pool;
}

//******************************************************************************************
void CVBufTexture::SetPersist()	// FUNCTION: 0x005005d0
{
	mPersist = true;
}

//******************************************************************************************
inline void CVBufTexture::EnsureLockV()
{
	if (!mVLocked)
	{
		HRESULT hr = mVB[mCurrentVB]->LockDynamic((void **)&mVData);
		if (FAILED(hr))
		{
			SETUP_HISTORY.AddMessage("EnsureLockV failed for %s", DXGetErrorString9(hr));
			CONSOLE.FatalError(false, 0xd2, 0x3e9);
		}
		mVLocked = true;
	}
}

//******************************************************************************************
inline void CVBufTexture::EnsureLockI()
{
	if (!mILocked)
	{
		CONSOLE.FatalError(SUCCEEDED(mIB->LockDynamic((void **)&mIData)), 0xd2, 0x3e9);
		mILocked = true;
	}
}

//******************************************************************************************
// The rounded capacity reuses the size parameter; the shared final store gives retail's three return paths.
void CVBufTexture::ResizeVB(SINT size)	// FUNCTION: 0x005005e0
{
	size = RoundSize(size);
	if (size)
	{
#line 182
		CVBuffer *vb = new(MEMTYPE_VBUFFER) CVBuffer;
		CONSOLE.FatalError(SUCCEEDED(vb->CreateInternal(size, mVUsage, mFVF, (D3DPOOL)mVPool)), 0xd2, 3);
		mVLocked = false;
		if (mVSize[mCurrentVB] > 0)
		{
			if (mVUsed)
			{
				void *data;
				if (SUCCEEDED(vb->LockDynamic(&data)))
				{
					EnsureLockV();
					memcpy(data, mVData, mVUsed);
					UnlockVB();
					mVData = (UBYTE *)data;
					mVLocked = true;
				}
				else
					CONSOLE.FatalError(false, 0xd2, 4);
			}
			if (mVB[mCurrentVB])
				delete mVB[mCurrentVB];
		}
		mVB[mCurrentVB] = vb;
	}
	else
	{
		if (mVB[mCurrentVB])
		{
			delete mVB[mCurrentVB];
			mVB[mCurrentVB] = NULL;
		}
	}
	mVSize[mCurrentVB] = size;
}

//******************************************************************************************
void CVBufTexture::ResizeIB(SINT size)	// FUNCTION: 0x005007f0
{
	size = RoundSize(size);
	if (size)
	{
#line 251
		CIBuffer *ib = new(MEMTYPE_IBUFFER) CIBuffer;
		CONSOLE.FatalError(SUCCEEDED(ib->CreateInternal(size, mIUsage, mIFormat, mIPool)), 0xd2, 2);
		mILocked = false;
		if (mISize > 0)
		{
			if (mIUsed)
			{
				void *data;
				if (SUCCEEDED(ib->LockDynamic(&data)))
				{
					EnsureLockI();
					memcpy(data, mIData, mIUsed);
					UnlockIB();
					mILocked = true;
					mIData = (UBYTE *)data;
				}
				else
					CONSOLE.FatalError(false, 0xd2, 5);
			}
			delete mIB;
		}
		mIB = ib;
	}
	else
	{
		if (mIB)
		{
			UnlockIB();
			delete mIB;
			mIB = NULL;
		}
	}
	mISize = size;
}

//******************************************************************************************
void CVBufTexture::UnlockVB()	// FUNCTION: 0x005009c0
{
	if (mVLocked)
	{
		mVB[mCurrentVB]->Unlock();
		mVData = NULL;
		mVLocked = false;
	}
}

//******************************************************************************************
void CVBufTexture::UnlockIB()	// FUNCTION: 0x005009f0
{
	if (mILocked)
	{
		mIB->Unlock();
		mIData = NULL;
		mILocked = false;
	}
}

//******************************************************************************************
int CVBufTexture::AddVertices(void *verts, int count)	// FUNCTION: 0x00500a10
{
	int size = count * mStride;
	if (mVUsed + size > mVSize[mCurrentVB])
		ResizeVB(mVUsed + size);
	EnsureLockV();
	memcpy(mVData + mVUsed, verts, size);
	int first = mVUsed / mStride;
	mVUsed += size;
	return first;
}

//******************************************************************************************
void CVBufTexture::AddIndices(void *indices, int count)	// FUNCTION: 0x00500ac0
{
	int size = count * 2;
	if (mIUsed + size > mISize)
		ResizeIB(mIUsed + size);
	EnsureLockI();
	memcpy(mIData + mIUsed, indices, size);
	mIUsed += size;
}

//******************************************************************************************
UWORD *CVBufTexture::GetIndexPtr(int count)	// FUNCTION: 0x00500b40
{
	if (mIUsed + count * 2 > mISize)
		ResizeIB(mIUsed + count * 2);
	EnsureLockI();
	UWORD *p = (UWORD *)(mIData + mIUsed);
	mIUsed += count * 2;
	return p;
}

//******************************************************************************************
int CVBufTexture::GetVertexPtr(void **verts, int count)	// FUNCTION: 0x00500bb0
{
	int size = count * mStride;
	if (mVUsed + size > mVSize[mCurrentVB])
		ResizeVB(mVUsed + size);
	EnsureLockV();
	*verts = mVData + mVUsed;
	int first = mVUsed / mStride;
	mVUsed += size;
	return first;
}

// retail calls the two primitive counts out of line from Draw, DWATS, DrawIndexed and DrawNonIndexed
// (00500ebf, 00500fff, 0050113f, 005011f7), where /Ob2 would inline them; only this pragma reproduces the calls.
#pragma auto_inline(off)
//******************************************************************************************
int CVBufTexture::GetNumIndexedPrimitives()	// FUNCTION: 0x00500c50
{
	int n = mIUsed >> 1;
	if (mPrimType == D3DPT_POINTLIST)
		return n;
	else if (mPrimType == D3DPT_LINELIST)
		return n / 2;
	else if (mPrimType == D3DPT_LINESTRIP)
		return n - 1;
	else if (mPrimType == D3DPT_TRIANGLELIST)
		return n / 3;
	else if (mPrimType == D3DPT_TRIANGLESTRIP)
		return n - 2;
	else if (mPrimType == D3DPT_TRIANGLEFAN)
		return n - 2;
	return 0;
}

//******************************************************************************************
int CVBufTexture::GetNumPrimitives()	// FUNCTION: 0x00500cb0
{
	int n = mVUsed / mStride;
	if (mPrimType == D3DPT_POINTLIST)
		return n;
	else if (mPrimType == D3DPT_LINELIST)
		return n / 2;
	else if (mPrimType == D3DPT_LINESTRIP)
		return n - 1;
	else if (mPrimType == D3DPT_TRIANGLELIST)
		return n / 3;
	else if (mPrimType == D3DPT_TRIANGLESTRIP)
		return n - 1;
	else if (mPrimType == D3DPT_TRIANGLEFAN)
		return n - 2;
	return 0;
}

#pragma auto_inline(on)
//******************************************************************************************
void CVBufTexture::DrawList(SVBufTextureDrawList *list)	// FUNCTION: 0x00500d10
{
	for (int pass = 0; pass < 6; pass++)
	{
		for (int i = 0; i < list->mCount; i++)
		{
			if (list->mEntries[i].mItem->mPass == pass && list->mEntries[i].mVBuf)
				list->mEntries[i].mVBuf->Draw(true);
		}
	}
}

//******************************************************************************************
void CVBufTexture::ResetAll()	// FUNCTION: 0x00500d60
{
	char buf[256];
	for (CVBufTexture *v = mFirst; v; v = v->mNext)
	{
		CIBuffer *ib = v->mIB;
		if (ib && !v->mPersist)
		{
			if (v->mILocked)
			{
				ib->Unlock();
				v->mIData = NULL;
				v->mILocked = false;
			}
			if (v->mIUsed)
			{
				if (v->mTexture)
					sprintf(buf, "WARNING : CVBufTexture %s has %d orphan indices\n", v->mTexture->GetName(), v->mIUsed);
				else
					sprintf(buf, "WARNING : CVBufTexture [no texture] has %d orphen indices\n", v->mIUsed);
				TRACE(buf);
			}
			v->mIUsed = 0;
		}
		CVBuffer *vb = v->mVB[v->mCurrentVB];
		if (vb && !v->mPersist)
		{
			if (v->mVLocked)
			{
				vb->Unlock();
				v->mVData = NULL;
				v->mVLocked = false;
			}
			if (v->mVUsed)
			{
				if (v->mTexture)
					sprintf(buf, "WARNING : CVBufTexture %s has %d orphan vertices\n", v->mTexture->GetName(), v->mVUsed);
				else
					sprintf(buf, "WARNING : CVBufTexture [no texture] has %d orphen vertices\n", v->mVUsed);
				TRACE(buf);
			}
			if (v->mVUsed)
				v->mLastVUsed = v->mVUsed;
			v->mVUsed = 0;
		}
	}
}

//******************************************************************************************
void CVBufTexture::Draw(bool reset)	// FUNCTION: 0x00500e70
{
	if (mVUsed && mIUsed)
	{
		UnlockVB();
		UnlockIB();
		int numverts = mVUsed / mStride;
		int numprims = GetNumIndexedPrimitives();
		if (mTexture)
		{
			mTexture->Select();
			mTexture->Activate();
		}
		if (mFVF)
			RENDERINFO.SetFVF(mFVF);
		RENDERINFO.Apply();
		LT.GetDevice()->SetStreamSource(0, mVB[mCurrentVB]->mVB, 0, mStride);
		LT.GetDevice()->SetIndices(mIB->mIB);
		LT.D3D_DrawIndexedPrimitive((D3DPRIMITIVETYPE)mPrimType, 0, numverts, 0, numprims);
		if (mTexture)
			mTexture->Deactivate(false);
		if (reset)
			Reset();
	}
}

//******************************************************************************************
void CVBufTexture::Reset()	// FUNCTION: 0x00500f80
{
	if (mVUsed)
		mLastVUsed = mVUsed;
	mVUsed = 0;
	mIUsed = 0;
	if (mDoubleBuffer)
		mCurrentVB ^= 1;
}

//******************************************************************************************
void CVBufTexture::DWATS(bool reset, int numverts, int numprims)	// FUNCTION: 0x00500fa0
{
	if (mVUsed && mIUsed)
	{
		UnlockVB();
		UnlockIB();
		if (!numverts)
			numverts = mVUsed / mStride;
		if (!numprims)
			numprims = GetNumIndexedPrimitives();
		if (mFVF)
			RENDERINFO.SetFVF(mFVF);
		RENDERINFO.Apply(false, VBUFTEXTURE_VALIDATE);
		LT.GetDevice()->SetStreamSource(0, mVB[mCurrentVB]->mVB, 0, mStride);
		LT.GetDevice()->SetIndices(mIB->mIB);
		HRESULT hr;
		if (VBUFTEXTURE_VALIDATE && FAILED(hr = LT.D3D_ValidateDevice()))
		{
			SETUP_HISTORY.AddMessage("CVBT::DWATS ValidateDevice failed for %s\n", DXGetErrorString9(hr));
			VBUFTEXTURE_VALIDATE_FAILED = true;
		}
		else
			LT.D3D_DrawIndexedPrimitive((D3DPRIMITIVETYPE)mPrimType, 0, numverts, 0, numprims);
		if (reset)
			Reset();
	}
}

//******************************************************************************************
void CVBufTexture::DrawIndexed(bool reset, int numverts, int numprims)	// FUNCTION: 0x005010e0
{
	if (mVUsed && mIUsed)
	{
		UnlockVB();
		UnlockIB();
		if (!numverts)
			numverts = mVUsed / mStride;
		if (!numprims)
			numprims = GetNumIndexedPrimitives();
		LT.GetDevice()->SetStreamSource(0, mVB[mCurrentVB]->mVB, 0, mStride);
		LT.GetDevice()->SetIndices(mIB->mIB);
		LT.D3D_DrawIndexedPrimitive((D3DPRIMITIVETYPE)mPrimType, 0, numverts, 0, numprims);
		if (reset)
			Reset();
	}
}

//******************************************************************************************
void CVBufTexture::DrawNonIndexed(bool reset, int numprims)	// FUNCTION: 0x005011c0
{
	if (mVUsed)
	{
		UnlockVB();
		if (!numprims)
			numprims = GetNumPrimitives();
		if (mFVF)
			RENDERINFO.SetFVF(mFVF);
		RENDERINFO.Apply();
		LT.GetDevice()->SetStreamSource(0, mVB[mCurrentVB]->mVB, 0, mStride);
		LT.GetDevice()->DrawPrimitive((D3DPRIMITIVETYPE)mPrimType, 0, numprims);
		if (reset)
		{
			if (mVUsed)
				mLastVUsed = mVUsed;
			mVUsed = 0;
			if (mDoubleBuffer)
				mCurrentVB ^= 1;
		}
	}
}

//******************************************************************************************
inline CVBufTexture *CVBufTexture::Find(CDXTexture *texture)
{
	for (CVBufTexture *v = mFirst; v; v = v->mNext)
		if (v->mTexture == texture)
			return v;
	return NULL;
}

//******************************************************************************************
// The found and the new object share one tail, `if (!unique) store; return v;`. Retail's found path stores without
// testing `unique` (it cannot be set there) and the new object's copy tests it, each with its own epilogue, the new
// object's null stub after its return (005012f7) and the found path last. Two separate stores and returns compile to
// an inline stub and one shared epilogue instead.
CVBufTexture *CVBufTexture::GetOrCreate(CDXTexture *texture, SINT unique)	// FUNCTION: 0x00501280
{
	CVBufTexture *v = NULL;
	if (!unique)
	{
		if (texture->mVBufTexture)
		{
			texture->mVBufTexture->mRefCount++;
			return texture->mVBufTexture;
		}
		if (texture)
			v = Find(texture);
	}
	if (!v)
	{
#line 750
		v = new(MEMTYPE_VBUFTEXTURE) CVBufTexture(texture);
	}
	else
		v->mRefCount++;
	if (!unique)
		texture->mVBufTexture = v;
	return v;
}


//******************************************************************************************
void CVBufTexture::Release()	// FUNCTION: 0x00501310
{
	mRefCount--;
}

//******************************************************************************************
CVBufTexture *CVBufTexture::GetByName(char *name, int a)	// FUNCTION: 0x00501320
{
	CDXTexture *texture = CDXTexture::FindTexture(name, 0, 0, a, 1, 1);
	texture->Release();
	return GetOrCreate(texture, 0);
}

//******************************************************************************************
void CVBufTexture::FreeLevelResources()	// FUNCTION: 0x00501360
{
	char buf[256];
	CVBufTexture *v = mFirst;
	while (v)
	{
		CVBufTexture *next = v->mNext;
		if (!v->mRefCount)
			delete v;
		v = next;
	}
	v = mFirst;
	if (v)
	{
		TRACE("\n--------------------------------------\n");
		TRACE("VBufTexture end-of-level resource leaks\n");
		TRACE("--------------------------------------\n\n");
		do
		{
			if (v->mTexture)
				sprintf(buf, "VBufTexture for texture '%s' leaked : refcount=%d\n", v->mTexture->GetName(), v->mRefCount);
			else
				sprintf(buf, "VBufTexture for NULL texture leaked : refcount=%d\n", v->mRefCount);
			TRACE(buf);
			v = v->mNext;
		} while (v);
		TRACE("\n--------------------------------------\n\n");
	}
	else
		TRACE("No end-of-level VBufTexture resource leaks\n");
}

//******************************************************************************************
void CVBufTexture::FreeAllResources()	// FUNCTION: 0x00501450
{
	char buf[256];
	CVBufTexture *v = mFirst;
	while (v)
	{
		CVBufTexture *next = v->mNext;
		if (!v->mRefCount)
			delete v;
		v = next;
	}
	v = mFirst;
	if (v)
	{
		TRACE("\n--------------------------\n");
		TRACE("VBufTexture resource leaks\n");
		TRACE("--------------------------\n\n");
		do
		{
			if (v->mTexture)
				sprintf(buf, "VBufTexture for texture '%s' leaked : refcount=%d\n", v->mTexture->GetName(), v->mRefCount);
			else
				sprintf(buf, "VBufTexture for NULL texture leaked : refcount=%d\n", v->mRefCount);
			TRACE(buf);
			v = v->mNext;
		} while (v);
		TRACE("\n--------------------------\n\n");
	}
	else
		TRACE("No VBufTexture resource leaks\n");
}

//******************************************************************************************
void CVBufTexture::DownSizeSome()	// FUNCTION: 0x00501540
{
	if (!VBUFTEXTURE_NO_DOWNSIZE)
	{
		int best = 0;
		CVBufTexture *bestv = NULL;
		for (CVBufTexture *v = mFirst; v; v = v->mNext)
		{
			if (!v->mPersist)
			{
				SINT want = RoundSize(max(v->mLastVUsed, v->mVUsed));
				SINT have = v->mVSize[v->mCurrentVB];
				if (have > want)
				{
					int halvings = 0;
					while (have > want)
					{
						have >>= 1;
						halvings++;
					}
					if (halvings > best)
					{
						best = halvings;
						bestv = v;
					}
				}
			}
		}
		if (bestv)
			bestv->ResizeVB(bestv->mLastVUsed);
	}
}

//******************************************************************************************
void CVBufTexture::ClearOut()	// FUNCTION: 0x005015c0
{
	for (CVBufTexture *v = mFirst; v; v = v->mNext)
	{
		SINT vsize = RoundSize(v->mVUsed);
		SINT isize = RoundSize(v->mIUsed);
		if (vsize < v->mVSize[v->mCurrentVB])
			v->ResizeVB(vsize);
		if (isize < v->mISize)
			v->ResizeIB(isize);
	}
}

// retail: a zero vector initialized after the object's functions (00501630 writes 00854df0, between CLAIM_WRITEONLY and
// the buffer list in the bss); nothing reads it. Its name is open.
static FVector	VBUFTEXTURE_ZERO(0.0f, 0.0f, 0.0f);
// FUNCTION: 0x00501630 init:VBUFTEXTURE_ZERO
