// ibuffer.cpp: reconstructed from ibuffer.obj (00488210..0048861f); Stuart's release has no copy.
// The allocation's __LINE__ is retail's. The managed copy and direct device lock are separate paths.
#include "common.h"
#include "ltshell.h"
#include "console.h"
#include "ibuffer.h"

//******************************************************************************************
CIBuffer::CIBuffer()	// FUNCTION: 0x00488210
{
	mIB = NULL;
	LT.AddDeviceObject(this);
	mData = NULL;
	mLocked = false;
}

// FUNCTION: 0x00488270 ??_GCIBuffer@@UAEPAXI@Z
//******************************************************************************************
CIBuffer::~CIBuffer()	// FUNCTION: 0x00488290
{
	LT.RemoveDeviceObject(this);
	if (mPool == D3DPOOL_MANAGED)
		DeleteDeviceObjects();
	else
		InvalidateDeviceObjects();
	SAFE_DELETE_ARRAY(mData);
}

//******************************************************************************************
HRESULT CIBuffer::CreateInternal(UINT size, DWORD usage, SINT format, SINT pool)	// FUNCTION: 0x00488330
{
	mSize = size;
	mUsage = usage;
	mFormat = format;
	mPool = pool;
	HRESULT hr;
	if (pool == D3DPOOL_MANAGED)
		hr = InitDeviceObjects();
	else
		hr = RestoreDeviceObjects();
	CONSOLE.FatalError(SUCCEEDED(hr), 0xd2, 7);
	return hr;
}

//******************************************************************************************
HRESULT CIBuffer::Create(int count)	// FUNCTION: 0x00488380
{
#line 54
	mData = new(MEMTYPE_IBUFFER) UBYTE[count * sizeof(WORD)];
	return CreateInternal(count * sizeof(WORD), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED);
}

//******************************************************************************************
HRESULT CIBuffer::Unlock()	// FUNCTION: 0x004883f0
{
	if (mIB)
	{
		if (mLocked)
		{
			mLocked = false;
			void *data;
			HRESULT hr = mIB->Lock(0, 0, &data, D3DLOCK_NOSYSLOCK);
			if (FAILED(hr))
				return hr;
			memcpy(data, mData, mSize);
			return mIB->Unlock();
		}
		return mIB->Unlock();
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CIBuffer::InitDeviceObjects()	// FUNCTION: 0x00488460
{
	if (mPool == D3DPOOL_MANAGED)
	{
		if (FAILED(LT.D3D_CreateIndexBuffer(mSize, mUsage, (D3DFORMAT)mFormat, D3DPOOL_MANAGED, &mIB)))
			return E_FAIL;
		if (mData)
		{
			void *data;
			HRESULT hr = mIB->Lock(0, 0, &data, D3DLOCK_NOSYSLOCK);
			if (FAILED(hr))
				return hr;
			memcpy(data, mData, mSize);
			return mIB->Unlock();
		}
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CIBuffer::RestoreDeviceObjects()	// FUNCTION: 0x004884f0
{
	if (mPool == D3DPOOL_DEFAULT)
	{
		if (FAILED(LT.D3D_CreateIndexBuffer(mSize, mUsage, (D3DFORMAT)mFormat, D3DPOOL_DEFAULT, &mIB)))
			return E_FAIL;
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CIBuffer::InvalidateDeviceObjects()	// FUNCTION: 0x00488520
{
	if (mPool == D3DPOOL_DEFAULT)
		SAFE_RELEASE(mIB);
	return S_OK;
}

//******************************************************************************************
HRESULT CIBuffer::DeleteDeviceObjects()	// FUNCTION: 0x00488550
{
	if (mPool == D3DPOOL_MANAGED)
		SAFE_RELEASE(mIB);
	return S_OK;
}

//******************************************************************************************
HRESULT CIBuffer::Lock(WORD **ptr)	// FUNCTION: 0x00488580
{
	if (mData)
	{
		*ptr = (WORD *)mData;
		mLocked = true;
		return S_OK;
	}
	return LockDynamic((void **)ptr);
}

//******************************************************************************************
HRESULT CIBuffer::LockDynamic(void **data)	// FUNCTION: 0x004885e0
{
	if ((mUsage & D3DUSAGE_DYNAMIC) == D3DUSAGE_DYNAMIC)
		return mIB->Lock(0, 0, data, D3DLOCK_NOSYSLOCK | D3DLOCK_DISCARD);
	else
		return mIB->Lock(0, 0, data, D3DLOCK_NOSYSLOCK);
}
