// FastVB.cpp: a dynamic vertex buffer for screen-space quads and strips (text, sprites, the compass, the loading
// screen). Not in Stuart's release: reconstructed from retail (0051a1f0..0051a6ff).

#include "common.h"

#if TARGET == PC

#include	"DX.h"
#include	"ltshell.h"
#include	"vbuffer.h"
#include	"ibuffer.h"
#include	"FastVB.h"
// mIB and the constructor (which names it) come after FASTVB: retail lays mIB out before FASTVB (00897a90, 00897a98),
// so mIB's symbol was created later; the constructor still expands into FASTVB's initializer, built at the end of the
// file (placement evidence; Stuart's release has no FastVB.cpp).
// FUNCTION: 0x0051a1f0 init:FASTVB
// FUNCTION: 0x0051a230 exit:FASTVB
CFastVB		FASTVB;
CIBuffer	*CFastVB::mIB;	inline CFastVB::CFastVB() { mIB = NULL; mVB = NULL; mPos = 0; mStart = 0xffff; mCount = 0; mCapacity = 5000; }
// FUNCTION: 0x0051a270
HRESULT CFastVB::Create()
{
	if (mVB)
		return 0;

#line 41
	mVB = new (MEMTYPE_VBUFFER) CVBuffer;
	HRESULT hr = mVB->CreateDynamic(mCapacity, FASTVB_STRIDE, FASTVB_FVF);
	if (FAILED(hr))
	{
		SAFE_DELETE(mVB);
		return hr;
	}
	LT.MoveDeviceObjectToFirst(mVB);
	return hr;
}

// FUNCTION: 0x0051a340
void CFastVB::Destroy()
{
	SAFE_DELETE(mVB);
	SAFE_DELETE(mIB);
}

// FUNCTION: 0x0051a380
WORD CFastVB::LockAligned(void **ptr, int count)
{
	if (!mVB)
		return 0xffff;

	DWORD flags;
	mPos = ((mPos + 3) / 4) * 4;
	if (mPos && mPos + count <= mCapacity)
		flags = D3DLOCK_NOOVERWRITE | D3DLOCK_NOSYSLOCK;
	else
	{
		mPos = 0;
		flags = D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK;
	}

	WORD start = mPos;
	HRESULT hr = mVB->LockRange(start * FASTVB_STRIDE, count * FASTVB_STRIDE, ptr, flags);
	mPos += count;
	if (FAILED(hr))
		return 0xffff;

	mCount = count;
	mStart = start;
	return start;
}

// FUNCTION: 0x0051a430
WORD CFastVB::Lock(void **ptr, int count)
{
	if (!mVB)
		return 0xffff;

	if (mStart == 0xffff)
		return LockAligned(ptr, count);

	DWORD flags;
	if (mPos && mPos + count <= mCapacity)
	{
		flags = D3DLOCK_NOOVERWRITE | D3DLOCK_NOSYSLOCK;
		mVB->Unlock();
	}
	else
	{
		Render();
		mPos = 0;
		mStart = 0;
		mCount = 0;
		flags = D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK;
	}

	WORD start = mPos;
	HRESULT hr = mVB->LockRange(start * FASTVB_STRIDE, count * FASTVB_STRIDE, ptr, flags);
	mPos += count;
	if (FAILED(hr))
	{
		Render();
		return 0xffff;
	}

	mCount += count;
	return start;
}

// FUNCTION: 0x0051a510
void CFastVB::Render()
{
	if (mStart == 0xffff)
		return;

	mVB->Unlock();
	LT.m_pd3dDevice->SetStreamSource(0, mVB->GetVB(), 0, FASTVB_STRIDE);

	if (!mIB)
	{
#line 195
		mIB = new (MEMTYPE_IBUFFER) CIBuffer;
		mIB->Create(7500);

		WORD *indices;
		if (SUCCEEDED(mIB->Lock(&indices)))
		{
			WORD *p = indices;
			for (int i = 0; i < mCapacity; i += 4, p += 6)
			{
				p[0] = i;
				p[1] = i + 1;
				p[2] = i + 2;
				p[3] = i + 2;
				p[4] = i + 3;
				p[5] = i;
			}
			mIB->Unlock();
		}
		LT.MoveDeviceObjectToFirst(mIB);
	}

	if (mIB)
	{
		LT.GetDevice()->SetIndices(mIB->mIB);
		LT.ForceFVF(FASTVB_FVF);
		LT.D3D_DrawIndexedPrimitive(D3DPT_TRIANGLELIST, mStart, mCount, (mStart / 4) * 6, mCount / 2);
	}

	mStart = 0xffff;
	mCount = 0;
}

// FUNCTION: 0x0051a6a0
void CFastVB::RenderStrip()
{
	mVB->Unlock();
	LT.m_pd3dDevice->SetStreamSource(0, mVB->mVB, 0, FASTVB_STRIDE);
	LT.ForceFVF(FASTVB_FVF);
	LT.GetDevice()->DrawPrimitive(D3DPT_TRIANGLESTRIP, mStart, mCount - 2);
	mStart = 0xffff;
	mCount = 0;
}

#endif
