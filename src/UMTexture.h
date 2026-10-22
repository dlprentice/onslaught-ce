// UMTexture.h: a texture the program fills itself (vtable 005df908: a DeviceObject; 0x2c bytes; only what matched
// files need so far). The shell recreates it with the device. Its constructor (004f79d0) clears the texture and
// registers with the shell; its destructor (004f7a40) releases it.

#ifndef UMTEXTURE_H
#define UMTEXTURE_H

#include "deviceobject.h"

class CUMTexture : public DeviceObject
{
public:
	CUMTexture();							// 004f79d0: joins the shell's device objects
	virtual			~CUMTexture();			// 004f7a40 (CLandscapeTexture's destructor calls it, 0048e4b9)
	virtual HRESULT	RestoreDeviceObjects();		// 2 (004f7b60)
	virtual HRESULT	InvalidateDeviceObjects();	// 3 (004f7bd0)
	// the size, the kind (0 a 16-bit render target, 1 a 16-bit texture, 3 a render target in the display format,
	// 4 a 32-bit texture, 5 a managed 16-bit texture) and the mip levels; makes the texture (004f7ab0). Its result is
	// CLandscapeTexture::Init's (0048e5bc).
	HRESULT			Init(int size, int mode, int levels);

	IDirect3DTexture9	*mTexture;			// +0x08
	DWORD			mFormat;				// +0x0c: a D3DFORMAT, 0 for the display's format (DWORD: some includers lack d3d9.h)
	DWORD			mUsage;					// +0x10
	int				mSize;					// +0x14
	int				mLevels;				// +0x18
	DWORD			mPool;					// +0x1c: a D3DPOOL
	DWORD			mUnknown20;				// +0x20
	DWORD			mCreatedFormat;			// +0x24: the D3DFORMAT the texture was made in
	DWORD			mUnknown28;				// +0x28
};

#endif
