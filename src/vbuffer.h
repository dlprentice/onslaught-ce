// vbuffer.h: CVBuffer, a vertex buffer the shell restores with the device (vtable 005dfb8c; a DeviceObject). Not in
// Stuart's release: reconstructed from vbuffer.obj (004fff00..004fff50 and on). A managed buffer keeps a system-memory
// copy of its vertices (+0x24) that it copies into the device buffer when it is created and when a lock ends.

#ifndef VBUFFER_H
#define VBUFFER_H

#include "deviceobject.h"

class CVBuffer : public DeviceObject
{
public:
					CVBuffer();													// 004fff00
	virtual			~CVBuffer();												// 004fff80
	HRESULT			CreateInternal(UINT size, DWORD usage, DWORD fvf, D3DPOOL pool);	// 00500020
	BOOL			CreateDynamic(SINT count, SINT stride, DWORD fvf);			// 00500080
	BOOL			Create(SINT count, SINT stride, DWORD fvf);					// 005000c0
	virtual HRESULT	InitDeviceObjects();										// 00500120
	HRESULT			Lock(void **data);											// 005001b0
	HRESULT			Unlock();													// 005001e0
	virtual HRESULT	RestoreDeviceObjects();										// 00500250
	virtual HRESULT	InvalidateDeviceObjects();									// 00500280
	virtual HRESULT	DeleteDeviceObjects();										// 005002b0
	HRESULT			LockDynamic(void **data);									// 005002e0: discards a dynamic buffer
	void			SetStreamSource(UINT stream);								// 00500320: also sets RENDERINFO's FVF
	void			SetStreamSourceOnly(UINT stream);							// 00500360
	HRESULT			LockRange(UINT offset, UINT size, void **data, DWORD flags);	// 00500390

	IDirect3DVertexBuffer9 *GetVB() { return mVB; }

	IDirect3DVertexBuffer9	*mVB;			// +0x08
	DWORD			mUnknown0c;				// +0x0c
	UINT			mSize;					// +0x10: bytes
	DWORD			mUsage;					// +0x14
	DWORD			mFVF;					// +0x18
	UINT			mStride;				// +0x1c
	D3DPOOL			mPool;					// +0x20
	UBYTE			*mData;					// +0x24: the managed buffer's system-memory copy
	bool			mLocked;				// +0x28: the copy is locked
};

#endif
