// Texture.h: textures (only what matched files need).

#ifndef TEXTURE_H
#define TEXTURE_H

// The texture formats (CDXTexture::FormatToString 00557a00 names them in this order; ToD3DFormat 0055a0f0).
enum ETextureFormat
{
	TEXFMT_UNKNOWN = 0,
	TEXFMT_A1R5G5B5,
	TEXFMT_A4R4G4B4,
	TEXFMT_X8R8G8B8,
	TEXFMT_A8R8G8B8,
	TEXFMT_R5G6B5,
	TEXFMT_DXT1,
	TEXFMT_DXT2,
	TEXFMT_DXT4,
	TEXFMT_V8U8,
	TEXFMT_Q8W8V8U8
};
enum ETextureType { TEX_NORMAL };

#include "DX.h"

class CChunkReader;
class CVBufTexture;
class CDXTexture;

// The platform-independent texture record (texture.cpp), 0xb0 bytes at +0x08 of a PC
// texture. Its constructor (004f2710) links the texture on a list, names it "JCLTEX #n" and clears the name; its
// destructor (004f2790) unlinks it. CFEPGoodies::Deserialise adds a reference inline through it (0045c8f1: +0x08,
// then +0x9c) and reads the height at +0xb0 (0045c909).
class CTexture
{
public:
				CTexture();								// 004f2710
				~CTexture();							// 004f2790
	void		AddRef() { mRefCount++; }
	void		Release();									// 004f27e0
	SWORD		GetHeight() { return mHeight; }
	SINT		GetRealWidth() { return mWidth << mLossShift; }
	SINT		GetRealHeight() { return mHeight << mLossShift; }
	char		*GetName() { return mName; }		// the name starts the record (CVBufTexture::ResetAll 00500da2)

	char		mName[0x80];								// +0x00 (the constructor clears 0x20 dwords)
	SINT		mMode;										// +0x80: the blend mode (0..5; CDXTexture::Activate 00558af9)
	float		mOffsetU;									// +0x84: the texture transform (0 and 1 by default;
	float		mOffsetV;									// +0x88  Activate sets a transform unless it is identity)
	float		mScaleU;									// +0x8c
	float		mScaleV;									// +0x90
	SINT		mUnknown94;									// +0x94 (-1)
	CDXTexture	*mNext;										// +0x98: the list of every texture (head 0083d9b0)
	static CDXTexture	*mFirst;								// 0083d9b0: the list's head
	SINT		mRefCount;									// +0x9c
	SINT		mFormat;									// +0xa0: the requested format (the PC Init's second argument)
	SINT		mWidth;										// +0xa4 (+0xac of the texture: CFEPDevSelect::RenderDevice
															// 004595e6 loads it as an int)
	SWORD		mHeight;									// +0xa8
	SWORD		mLossShift;									// +0xaa: the resolution shift the last load applied (DXTexture.obj; CConsole::RenderLoadingScreen 0042c9c6 reads it)
	BOOL		mLit;										// +0xac: the name contains "_lit" (00556f06)
};

// A PC texture (RTTI CDXTexture, vtable 005e59a0, 0x158 bytes: CDXTexture::Deserialize allocates it, 00559bff). The
// core is declared first and the device object second; VC6 places the polymorphic base at +0x00 and constructs the
// core first (00556ce1), then destroys the device object first (00556e55). CFEPGoodies::FreeUpGoodyResources raises
// the byte flag at 0083d998 around ForceDelete (0045cd83..0045cd98; the flag's name is ours).
class CDXTexture : public CTexture, public DeviceObject
{
public:
							CDXTexture();								// 00556cc0
	virtual					~CDXTexture();							// 00556d90
	virtual HRESULT			InitDeviceObjects();					// 1: 00557a90
	virtual HRESULT			RestoreDeviceObjects();					// 2: 00557060
	virtual HRESULT			InvalidateDeviceObjects();				// 3: 005572c0
	virtual HRESULT			DeleteDeviceObjects();					// 4: 00558600
	virtual BOOL			Init(char *name, ETextureFormat format, ETextureType type, SINT a, SINT b);	// 5: 00556e90
	virtual BOOL			InitEmpty(DWORD width, DWORD height, ETextureFormat format, SINT a, UBYTE b, SINT c);	// 6: 00556fc0
	virtual HRESULT			Unknown7()				{ return S_OK; }	// 7: the shared return-0 (00405930)
	virtual void			Select();								// 8: 005593a0: sets the current frame on stage 0

	IDirect3DTexture9		*GetTexture();							// 00558690: the current frame
	HRESULT					LoadFrame(SINT frame);					// 00557300: a frame from its .aya file
	void					CreateMipmaps(CChunkReader *c, SINT frame, SINT levels);	// 00559410: a frame from a resource
	void					Dump(char *name);						// 005586e0: the first frame as a TGA
	bool					Activate();								// 005588f0: sets the stage states for the mode
	void					Deactivate(bool keepalpha);				// 00558fb0
	static bool				SetupAlphaStage(SINT alpha);			// 00558ef0
	static char				*FormatToString(ETextureFormat format);	// 00557a00
	static D3DFORMAT		ToD3DFormat(ETextureFormat format);		// 0055a0f0

	static CDXTexture			*Deserialize(bool fromresources, CChunkReader *c);	// 00559be0
	static CDXTexture			*FindTexture(char *name, int a, int b, int c, int d, int e);	// 004f27f0 (CFrontEndData::Init, CMesh::InitAll)
	// Stuart's name and defaults (CEngine::InitResources 00449d67: -1, 1, 1 after the format and type)
	static CDXTexture			*GetTextureByName(char *name, ETextureFormat fmt = TEXFMT_UNKNOWN, ETextureType type = TEX_NORMAL, int c = -1, int d = 1, int e = 1) { return FindTexture(name, fmt, type, c, d, e); }
	static void				FreeLevelResources();					// 004f2b40
#if defined(TEXTURE_CPP) || defined(SYSTEM_CPP)	// only these, so the other objects keep their declaration counts
	static void				ClearOut();								// 004f2a30 (CSystem::Shutdown 004f01c9)
#endif
	void					ForceDeleteVBuffer();					// 00556f80
	void					ForceDelete();							// 00556f50
	// The current-frame operation is GetTexture() above; the former GetAnimatedFrame declaration
	// was an undefined alias for the same 00558690 implementation, not a second animation method.

	static UBYTE			mForceDeleting;							// 0083d998
	static ETextureFormat	FromD3DFormat(D3DFORMAT format);		// 0055a170
	static void				DumpInterestingTextures();				// 00558870
	static void				LoadLevelResources(SINT level);			// 004f29c0

	IDirect3DTexture9		*mTextures[16];							// +0xb8: the animation frames
	IDirect3DTexture9		*mUnknownF8[16];						// +0xf8 (cleared by the constructor)
	SINT					mNumFrames;								// +0x138
	SINT					mMipFilter;								// +0x13c: nonzero filters the mip levels (D3DX box, 00557e59)
	CVBufTexture			*mVBufTexture;							// +0x140: its shared buffers (CVBufTexture::GetOrCreate
																	// 0050128e, ~CVBufTexture 0050046c)
	ETextureFormat			mTexFormat;								// +0x144
	SINT					mLevels;								// +0x148: mip levels to create
	UBYTE					mDisplayFormat;							// +0x14c: an empty texture in the display's format
	SINT					mPool;									// +0x150: D3DPOOL
	UBYTE					mCompressible;							// +0x154: loaded while LT.m_bTextureCompressionDisabled was clear (may be DXTn)
	union
	{
		UBYTE				mFlags155;								// +0x155 (the constructor clears the byte, 00556d51)
		struct
		{
			UBYTE			mUnknown155 : 1;						// bit 0: CFrontEndData::Init sets it on two textures (00468c30,
																	// 00468e01); LoadFrame loads it 32-bit when the options allow 1
			UBYTE			mUnknown155b : 1;						// bit 1: keeps its resolution unless mip-mapped (005577d5)
			UBYTE			mUnknown155c : 1;						// bit 2: loses resolution with 0 or 1 levels (005577bb)
		};
	};
	SWORD					mResourceLevels;						// +0x156: the mip levels in its resource (-1)
};
#define CTEXTURE CDXTexture

#endif
