// ibuffer.h: CIBuffer, an index buffer the shell restores with the device (vtable 005dbec4, 0x24 bytes; ibuffer.obj,
// 00488210..00488620). Stuart's BattleEngine.cpp includes it; not in his release. The layout is CVBuffer's with the
// index format in place of the FVF and stride (constructor 00488210, CreateConfigured 00488330).

#ifndef IBUFFER_H
#define IBUFFER_H

#include "deviceobject.h"

struct IDirect3DIndexBuffer9;

class CIBuffer : public DeviceObject
{
public:
					CIBuffer();													// 00488210
	virtual			~CIBuffer();												// 00488290
	HRESULT			CreateInternal(UINT size, DWORD usage, SINT format, SINT pool);	// 00488330
	HRESULT Create(int count);					// 00488380: count 16-bit indices
	HRESULT Lock(WORD **ptr);					// 00488580
	HRESULT			Unlock();													// 004883f0
	virtual HRESULT	InitDeviceObjects();										// 00488460
	virtual HRESULT	RestoreDeviceObjects();									// 004884f0
	virtual HRESULT	InvalidateDeviceObjects();								// 00488520
	virtual HRESULT	DeleteDeviceObjects();									// 00488550
	HRESULT			LockDynamic(void **data);									// 004885e0: discards a dynamic buffer

	IDirect3DIndexBuffer9	*mIB;			// +0x08
	UINT			mSize;					// +0x0c: bytes
	DWORD			mUsage;					// +0x10
	SINT			mFormat;				// +0x14: a D3DFORMAT
	SINT			mPool;					// +0x18: a D3DPOOL
	UBYTE			*mData;					// +0x1c: the managed buffer's system-memory copy
	bool			mLocked;				// +0x20
};

#endif
