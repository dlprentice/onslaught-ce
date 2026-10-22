// vbuffer.cpp: reconstructed from vbuffer.obj (004fff00..005003b9); Stuart's release has no copy.
// The allocation's __LINE__ is the retail one.
#include "common.h"
#include "ltshell.h"
#include "console.h"
#include "renderinfo.h"
#include "vbuffer.h"

//******************************************************************************************
CVBuffer::CVBuffer()	// FUNCTION: 0x004fff00
{
	mVB = NULL;
	LT.AddDeviceObject(this);
	mData = NULL;
	mLocked = false;
}

// FUNCTION: 0x004fff60 ??_GCVBuffer@@UAEPAXI@Z
//******************************************************************************************
CVBuffer::~CVBuffer()	// FUNCTION: 0x004fff80
{
	LT.RemoveDeviceObject(this);
	if (mPool == D3DPOOL_MANAGED)
		DeleteDeviceObjects();
	else
		InvalidateDeviceObjects();
	if (mData)
	{
		delete [] mData;
		mData = NULL;
	}
}

//******************************************************************************************
HRESULT CVBuffer::CreateInternal(UINT size, DWORD usage, DWORD fvf, D3DPOOL pool)	// FUNCTION: 0x00500020
{
	mSize = size;
	mStride = 0;
	mUsage = usage;
	mFVF = fvf;
	mPool = pool;
	HRESULT hr;
	if (pool == D3DPOOL_MANAGED)
		hr = InitDeviceObjects();
	else
		hr = RestoreDeviceObjects();
	CONSOLE.FatalError(SUCCEEDED(hr), 0xd2, 6);
	return hr;
}

//******************************************************************************************
BOOL CVBuffer::CreateDynamic(SINT count, SINT stride, DWORD fvf)	// FUNCTION: 0x00500080
{
	mStride = stride;
	mSize = count * stride;
	mUsage = D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC;
	mFVF = fvf;
	mPool = D3DPOOL_DEFAULT;
	return SUCCEEDED(RestoreDeviceObjects());
}

//******************************************************************************************
BOOL CVBuffer::Create(SINT count, SINT stride, DWORD fvf)	// FUNCTION: 0x005000c0
{
	mStride = stride;
	mSize = count * stride;
	mUsage = D3DUSAGE_WRITEONLY;
	mFVF = fvf;
	mPool = D3DPOOL_MANAGED;
#line 83
	mData = new(MEMTYPE_VBUFFER_DATA) UBYTE[mSize];
	return SUCCEEDED(InitDeviceObjects());
}

//******************************************************************************************
HRESULT CVBuffer::InitDeviceObjects()	// FUNCTION: 0x00500120
{
	if (mPool == D3DPOOL_MANAGED)
	{
		if (FAILED(LT.D3D_CreateVertexBuffer(mSize, mUsage, mFVF, D3DPOOL_MANAGED, &mVB)))
			return E_FAIL;
		if (mData)
		{
			void *data;
			HRESULT hr = mVB->Lock(0, 0, &data, D3DLOCK_NOSYSLOCK);
			if (FAILED(hr))
				return hr;
			memcpy(data, mData, mSize);
			return mVB->Unlock();
		}
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CVBuffer::Lock(void **data)	// FUNCTION: 0x005001b0
{
	if (mData)
	{
		*data = mData;
		mLocked = true;
		return S_OK;
	}
	return mVB->Lock(0, 0, data, D3DLOCK_NOSYSLOCK);
}

//******************************************************************************************
HRESULT CVBuffer::Unlock()	// FUNCTION: 0x005001e0
{
	if (mVB)
	{
		if (mLocked)
		{
			mLocked = false;
			void *data;
			HRESULT hr = mVB->Lock(0, 0, &data, D3DLOCK_NOSYSLOCK);
			if (FAILED(hr))
				return hr;
			memcpy(data, mData, mSize);
			return mVB->Unlock();
		}
		return mVB->Unlock();
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CVBuffer::RestoreDeviceObjects()	// FUNCTION: 0x00500250
{
	if (mPool == D3DPOOL_DEFAULT)
	{
		if (FAILED(LT.D3D_CreateVertexBuffer(mSize, mUsage, mFVF, D3DPOOL_DEFAULT, &mVB)))
			return E_FAIL;
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CVBuffer::InvalidateDeviceObjects()	// FUNCTION: 0x00500280
{
	if (mPool == D3DPOOL_DEFAULT && mVB)
	{
		LT.D3D_ReleaseVertexBuffer(mVB);
		mVB = NULL;
		mLocked = false;
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CVBuffer::DeleteDeviceObjects()	// FUNCTION: 0x005002b0
{
	if (mPool == D3DPOOL_MANAGED && mVB)
	{
		LT.D3D_ReleaseVertexBuffer(mVB);
		mVB = NULL;
		mLocked = false;
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CVBuffer::LockDynamic(void **data)	// FUNCTION: 0x005002e0
{
	if ((mUsage & D3DUSAGE_DYNAMIC) == D3DUSAGE_DYNAMIC)
		return mVB->Lock(0, 0, data, D3DLOCK_NOSYSLOCK | D3DLOCK_DISCARD);
	else
		return mVB->Lock(0, 0, data, D3DLOCK_NOSYSLOCK);
}

//******************************************************************************************
void CVBuffer::SetStreamSource(UINT stream)	// FUNCTION: 0x00500320
{
	RENDERINFO.SetFVF(mFVF);
	LT.GetDevice()->SetStreamSource(stream, mVB, 0, mStride);
}

//******************************************************************************************
void CVBuffer::SetStreamSourceOnly(UINT stream)	// FUNCTION: 0x00500360
{
	LT.GetDevice()->SetStreamSource(stream, mVB, 0, mStride);
}

//******************************************************************************************
HRESULT CVBuffer::LockRange(UINT offset, UINT size, void **data, DWORD flags)	// FUNCTION: 0x00500390
{
	if (mVB)
		return mVB->Lock(offset, size, data, flags);
	return E_FAIL;
}
