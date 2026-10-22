// DXTexture.cpp: the PC texture (CDXTexture). Not in Stuart's release; reconstructed from the
// retail code. A texture is a device object holding up to sixteen animation frames: an empty texture made to a size
// (render targets, fonts), a named special ("*15..", "*SC.."), a file under data\Textures (".._n.tga" frames for an
// animated one) or a pre-converted .aya image from the resources; resource files carry its mip levels directly.

#include	"Common.h"
#include	"Texture.h"
#include	"VBufTexture.h"
#include	"membuffer.h"
#include	"chunker.h"
#include	"console.h"
#include	"CLIParams.h"
#include	"engine.h"
#include	"meshrenderer.h"
#include	"renderinfo.h"
#include	"tgaloader.h"
#include	"tweak.h"
#include	"Platform.h"


static FVector	TEXTURE_ZERO(0.0f, 0.0f, 0.0f);	// 009cc048
// FUNCTION: 0x00556b80 init:TEXTURE_ZERO

CTweakSLONG	DXTN_IS_16_BIT("DXTN_IS_16_BIT", 0);											// 009cc128
CTweakSLONG	RENDERSTATE_TWEAK_CARD_CANT_DO_TFACTOR("RENDERSTATE_TWEAK_CARD_CANT_DO_TFACTOR", -1);	// 009cc118
CTweakSLONG	TEXTURE_RES_LOSS_SHIFT("TEXTURE_RES_LOSS_SHIFT", -1);							// 009cc108
CTweakSLONG	USER_TEXTURE_RES_LOSS_SHIFT("USER_TEXTURE_RES_LOSS_SHIFT", -1);				// 009cc0f8
CTweakSLONG	TEXTURE_ALLOW_32_BIT("TEXTURE_ALLOW_32_BIT", -1);								// 009cc0e8
CTweakSLONG	USER_TEXTURE_ALLOW_32_BIT("USER_TEXTURE_ALLOW_32_BIT", -1);					// 009cc0d8
// FUNCTION: 0x00556ba0 init:DXTN_IS_16_BIT
// FUNCTION: 0x00556bc0 exit:DXTN_IS_16_BIT
// FUNCTION: 0x00556bd0 init:RENDERSTATE_TWEAK_CARD_CANT_DO_TFACTOR
// FUNCTION: 0x00556bf0 exit:RENDERSTATE_TWEAK_CARD_CANT_DO_TFACTOR
// FUNCTION: 0x00556c00 init:TEXTURE_RES_LOSS_SHIFT
// FUNCTION: 0x00556c20 exit:TEXTURE_RES_LOSS_SHIFT
// FUNCTION: 0x00556c30 init:USER_TEXTURE_RES_LOSS_SHIFT
// FUNCTION: 0x00556c50 exit:USER_TEXTURE_RES_LOSS_SHIFT
// FUNCTION: 0x00556c60 init:TEXTURE_ALLOW_32_BIT
// FUNCTION: 0x00556c80 exit:TEXTURE_ALLOW_32_BIT
// FUNCTION: 0x00556c90 init:USER_TEXTURE_ALLOW_32_BIT
// FUNCTION: 0x00556cb0 exit:USER_TEXTURE_ALLOW_32_BIT

// The user's option wins over the level's; 32-bit textures: 2 all, 1 the flagged ones. Both getters keep the value in a
// local and return the default early. As three returns, VC6's diamond-to-select pass (C2 0x1072d139) turns the
// level arm and the 0 into neg/sbb/and, where retail branches at all three of LoadFrame's value uses (00557801,
// 00557835, 00557871). With the local, the level arm is two assignments when that pass runs, so the branch stays.
inline SINT	GetAllow32Bit()
{
	SINT	allow;
	if (USER_TEXTURE_ALLOW_32_BIT.IsSet())
		allow = USER_TEXTURE_ALLOW_32_BIT.mValue;
	else if (TEXTURE_ALLOW_32_BIT.IsSet())
		allow = TEXTURE_ALLOW_32_BIT.mValue;
	else
		return 2;
	return allow;
}

inline SINT	GetResLossShift()
{
	SINT	shift;
	if (USER_TEXTURE_RES_LOSS_SHIFT.IsSet())
		shift = USER_TEXTURE_RES_LOSS_SHIFT.mValue;
	else if (TEXTURE_RES_LOSS_SHIFT.IsSet())
		shift = TEXTURE_RES_LOSS_SHIFT.mValue;
	else
		return 0;
	return shift;
}

// the 16-bit format a 32-bit one falls back to
inline D3DFORMAT	To16Bit(D3DFORMAT f)
{
	D3DFORMAT result;
	switch (f)
	{
	case D3DFMT_A8R8G8B8:
	case D3DFMT_DXT2:		result = D3DFMT_A4R4G4B4; break;
	case D3DFMT_X8R8G8B8:
	case D3DFMT_DXT1:		result = D3DFMT_R5G6B5; break;
	default:			result = f; break;
	}
	return result;
}

//******************************************************************************************
CDXTexture::CDXTexture()	// FUNCTION: 0x00556cc0
{
	mMode = 0;
	for (int i = 0; i < 16; i++)
	{
		mTextures[i] = NULL;
		mUnknownF8[i] = NULL;
	}
	mOffsetU = 0;
	mOffsetV = 0;
	mScaleU = 1.f;
	mScaleV = 1.f;
	mNumFrames = 1;
	mPool = 1;
	mVBufTexture = NULL;
	LT.AddDeviceObject(this);
	mFlags155 = 0;
	mResourceLevels = -1;
}

// FUNCTION: 0x00556d70 ??_GCDXTexture@@UAEPAXI@Z

//******************************************************************************************
CDXTexture::~CDXTexture()	// FUNCTION: 0x00556d90
{
	LT.RemoveDeviceObject(this);
	if (LT.mD3DActive)
		DeleteDeviceObjects();
	MEM_MANAGER.UnTagMem(this);
}

//******************************************************************************************
BOOL	CDXTexture::Init(char *name, ETextureFormat format, ETextureType type, SINT levels, SINT mipfilter)	// FUNCTION: 0x00556e90
{
	mLevels = levels;
	mTexFormat = format;
	mMipFilter = mipfilter;
	mFormat = format;
	mPool = 1;
	strcpy(mName, name);
	mCompressible = (LT.m_bTextureCompressionDisabled == 0);
	if (strstr(mName, "_lit"))
		mLit = TRUE;
	else
		mLit = FALSE;
	AddRef();
	return InitDeviceObjects() >= 0;
}

//******************************************************************************************
void	CDXTexture::ForceDelete()	// FUNCTION: 0x00556f50
{
	for (int i = 0; i < 4; i++)
		LT.D3D_SetTexture(i, NULL);
	delete this;
}

//******************************************************************************************
void	CDXTexture::ForceDeleteVBuffer()	// FUNCTION: 0x00556f80
{
	if (mVBufTexture)
	{
		mVBufTexture->Release();
		delete mVBufTexture;
		mVBufTexture = NULL;
	}
}

//******************************************************************************************
BOOL	CDXTexture::InitEmpty(DWORD width, DWORD height, ETextureFormat format, SINT levels, UBYTE displayformat, SINT pool)	// FUNCTION: 0x00556fc0
{
	strcpy(mName, "");
	mWidth = width;
	mHeight = (SWORD)height;
	mTexFormat = format;
	mLevels = levels;
	mMipFilter = 1;
	mDisplayFormat = displayformat;
	mPool = pool;
	AddRef();
	return InitDeviceObjects() >= 0;
}

//******************************************************************************************
HRESULT	CDXTexture::RestoreDeviceObjects()	// FUNCTION: 0x00557060
{
	if (!LT.mD3DActive)
		return S_OK;

	HRESULT	hr = S_OK;
	if (mDisplayFormat)
	{
		mTexFormat = LT.GetDisplayFormat();
		if (!mTextures[0])
			hr = LT.D3D_CreateTexture(mWidth, mHeight, mLevels, 0, ToD3DFormat(mTexFormat), (D3DPOOL)mPool, &mTextures[0]);
		MEM_MANAGER.TagMem(this, LT.GetBPP(ToD3DFormat(mTexFormat)) * mHeight * mWidth / 8, "Textures/Custom (%dx%dx%d)",
						   mWidth, mHeight, LT.GetBPP(ToD3DFormat(mTexFormat)));
	}
	return hr;
}

//******************************************************************************************
HRESULT	CDXTexture::InvalidateDeviceObjects()	// FUNCTION: 0x005572c0
{
	if (mDisplayFormat)
	{
		SAFE_RELEASE(mTextures[0]);
		MEM_MANAGER.UnTagMem(this);
	}
	return S_OK;
}

//******************************************************************************************
// A frame's resource path (reconstructed; name ours). The MAX_PATH name buffer gives the frame
// retail's unwind stub shows (EBP-0x244, 005d7da0), and the inline's nested budget keeps FormatToString out of line as
// retail's LoadFrame calls it (00557664).
static inline void BuildTextureFramePath(CDXTexture *texture, char *path, SINT frame, bool usedxtn)
{
	bool compressible = texture->mCompressible != 0;
	ETextureFormat texFormat = texture->mTexFormat;
	char	name[MAX_PATH];
	strcpy(name, texture->mName);
	for (char *p = name; *p; p++)
		if (*p == '\\')
			*p = '%';

	const char	*format;
	int		len;
	if (usedxtn)
	{
		format = "data\\resources\\dxtntextures\\%s(%d)%s.aya";
		len = 0x1c;
	}
	else
	{
		format = compressible ? "data\\resources\\textures\\%s(%d)%s.aya" : "data\\resources\\textures\\mustbe_%s(%d)%s.aya";
		len = 0x18;
	}
	sprintf(path, format, name, frame, CDXTexture::FormatToString(texFormat));
	if (strlen(path) > (UINT)(len + 0x41))
	{
		path[len + 0x3d] = '.';
		path[len + 0x3e] = 'a';
		path[len + 0x3f] = 'y';
		path[len + 0x40] = 'a';
		path[len + 0x41] = 0;
	}
}

// CD3DApplication::GetAdapterInfo inlined (its out-of-line copy is Career.obj's 00420cd0), as in PauseMenu.cpp: -1 is
// the current adapter.
inline D3DAdapterInfo	*AdapterInfo(int n)
{
	if (n == -1)
		return &LT.m_Adapters[LT.m_dwAdapter];
	return &LT.m_Adapters[n];
}

//******************************************************************************************
// One frame from the converted resources: data\resources\textures\NAME(FRAME)FORMAT.aya, or the DXTn copy. The device
// index comes through GetAdapterInfo(-1), as in CCareer::WriteOptions 00420b10: retail forms the adapter's address
// before that load (00557344, 0055734b).
HRESULT	CDXTexture::LoadFrame(SINT frame)	// FUNCTION: 0x00557300
{
	mTextures[frame] = NULL;

	D3DAdapterInfo	*adapter = &LT.m_Adapters[LT.m_dwAdapter];
	D3DDeviceInfo	*dev = &adapter->devices[AdapterInfo(-1)->dwCurrentDevice];
	D3DModeInfo	*mode = &dev->modes[dev->dwCurrentMode];
	bool	dxtn = ((mode->mUnknown10 & 3) == 3);
	bool	allow32 = (GetAllow32Bit() == 2) || ((GetAllow32Bit() == 1) && mUnknown155);

	D3DFORMAT	fmt = D3DFMT_UNKNOWN;
	if (DXTN_IS_16_BIT.mValue)
	{
		if (allow32)
			fmt = ToD3DFormat(mTexFormat);
	}
	else if (!dxtn && !allow32)
		fmt = To16Bit(ToD3DFormat(mTexFormat));

	bool	usedxtn = CLIPARAMS.mDXTNTextures && mCompressible;
	if (strcmp(mName, "mouse.tga") == 0 && allow32)
		fmt = D3DFMT_A8R8G8B8;
	if (!usedxtn && !allow32)
		fmt = To16Bit(ToD3DFormat(mTexFormat));

	HRESULT	hr;
	while (TRUE)
	{
		char	path[256];
		BuildTextureFramePath(this, path, frame, usedxtn);

		CDXMemBuffer	mb;
		hr = -1;
		if (mb.InitFromFile(path, MEMTYPE_MEMBUFFER, TRUE, 0))
		{
#line 418
			UBYTE	*data = (UBYTE *)new(MEMTYPE_SCRATCHPAD) UBYTE[0x500000];
			SINT	size = mb.Read(data, 0x500000);
			if (size == 0x500000)
				CONSOLE.FatalError(FALSE, 0xd1, -1);
			else
			{
				SINT	w = -1, h = -1;
				if (mLossShift)
				{
					mWidth <<= mLossShift;
					mHeight <<= mLossShift;
					mLossShift = 0;
				}
				if (GetResLossShift() &&
					((((mLevels == 0) || (mLevels == 1)) && mUnknown155c) || ((mLevels != 0) && (mLevels != 1) && !mUnknown155b)))
				{
					mLossShift = GetResLossShift();
					mWidth >>= GetResLossShift();
					mHeight >>= GetResLossShift();
					w = mWidth;
					h = mHeight;
				}
				hr = D3DXCreateTextureFromFileInMemoryEx(LT.GetDevice(), data, size, w, h, mResourceLevels, 0, fmt,
														  (D3DPOOL)mPool, D3DX_FILTER_POINT, D3DX_DEFAULT, 0, NULL, NULL,
														  &mTextures[frame]);
			}
			delete [] data;
			mb.Close();
			if (SUCCEEDED(hr))
				break;
		}
		if (!usedxtn && mCompressible)
			usedxtn = TRUE;
		else if (FAILED(hr) && fmt)
			fmt = D3DFMT_UNKNOWN;
		else
			break;
	}
	return hr;
}

//******************************************************************************************
// A format's name for the resource paths. LoadFrame (00557300) calls it out of line (00557664): BuildTextureFramePath's
// nested inline budget denies the site (see there).
char	*CDXTexture::FormatToString(ETextureFormat format)	// FUNCTION: 0x00557a00
{
	switch (format)
	{
	case TEXFMT_UNKNOWN:	return "UNKNOWN";
	case TEXFMT_A1R5G5B5:	return "A1R5G5B5";
	case TEXFMT_A4R4G4B4:	return "A4R4G4B4";
	case TEXFMT_X8R8G8B8:	return "X8R8G8B8";
	case TEXFMT_A8R8G8B8:	return "A8R8G8B8";
	case TEXFMT_R5G6B5:		return "R5G6B5";
	case TEXFMT_DXT1:		return "DXT1";
	case TEXFMT_DXT2:		return "DXT2";
	case TEXFMT_DXT4:		return "DXT4";
	case TEXFMT_V8U8:		return "V8U8";
	case TEXFMT_Q8W8V8U8:	return "Q8W8V8U8";
	}
	return "nk";
}

//******************************************************************************************
HRESULT	CDXTexture::InitDeviceObjects()	// FUNCTION: 0x00557a90
{
	if (!LT.mD3DActive)
		return S_OK;

	HRESULT	hr = -1;
	if (strlen(mName) == 0)
	{
		// an empty texture
		if (mDisplayFormat)
			mTexFormat = LT.GetDisplayFormat();
		while (TRUE)
		{
			hr = LT.D3D_CreateTextureNoCheck(mWidth, mHeight, mLevels, 0, ToD3DFormat(mTexFormat), (D3DPOOL)mPool, &mTextures[0]);
			if (SUCCEEDED(hr))
				break;
			if (mTexFormat != TEXFMT_A8R8G8B8)
				break;
			mTexFormat = TEXFMT_A4R4G4B4;
		}
	}
	else if (mName[0] == '*')
	{
		// "*15 L SIZE" and "*SC L SIZE": a 16-bit or screen-format texture of L levels
		mDisplayFormat = FALSE;
		if (strncmp(&mName[1], "15", 2) == 0)
			mTexFormat = TEXFMT_A1R5G5B5;
		else if (strnicmp(&mName[1], "SC", 2) == 0)
		{
			mTexFormat = LT.GetDisplayFormat();
			mDisplayFormat = TRUE;
		}
		else
			mTexFormat = TEXFMT_X8R8G8B8;
		mLevels = mName[4] - '0';
		mWidth = atoi(&mName[6]);
		mHeight = mWidth;
		hr = LT.D3D_CreateTexture(mWidth, mHeight, mLevels, 0, ToD3DFormat(mTexFormat), (D3DPOOL)mPool, &mTextures[0]);
	}
	else
	{
		char	path[128] = "data\\Textures\\";
		strcat(path, mName);
		strlwr(path);

		bool	animated = FALSE;
		for (char *p = mName; *p; p++)
			if (p[0] == '\\' && p[1] == '_')
				animated = TRUE;

		BOOL	loaded = FALSE;
		if (!CLIPARAMS.mBuildResources)
		{
			loaded = TRUE;
			for (int i = 0; i < mNumFrames; i++)
			{
				hr = LoadFrame(i);
				if (FAILED(hr))
				{
					loaded = FALSE;
					break;
				}
			}
		}

		if (!loaded)
		{
			mDisplayFormat = FALSE;
			if (animated)
			{
				char	status[256];
				char	*ext = path;
				while (*ext != '.')
					ext++;
				*ext++ = 0;

				sprintf(status, "Loading animated texture %s", mName);
				CONSOLE.Status(status);
				mNumFrames = 0;
				while (TRUE)
				{
					char	fname[64];
					char	frame[32];
					sprintf(fname, "%s%d.%s", path, mNumFrames, ext);
					sprintf(frame, "Frame %d", mNumFrames);
					CONSOLE.StatusUpdateLine(status, frame);
					FILE	*f = fopen(fname, "r");
					if (!f)
						break;
					fclose(f);
					DWORD	mipfilter = (mMipFilter ? D3DX_FILTER_BOX | D3DX_FILTER_MIRROR :
												   D3DX_FILTER_POINT | D3DX_FILTER_MIRROR);
					hr = D3DXCreateTextureFromFileEx(LT.GetDevice(), ToTCHAR(fname), D3DX_DEFAULT, D3DX_DEFAULT, mLevels, 0,
													 ToD3DFormat(mTexFormat), (D3DPOOL)mPool, D3DX_FILTER_BOX | D3DX_FILTER_MIRROR,
													 mipfilter,
													 0, NULL, NULL, &mTextures[mNumFrames]);
					if (hr == 0x80070002)
					{
						TRACE("Warning : Texture not found...\n");
						TRACE(path);
						TRACE("\n");
					}
					else if (FAILED(hr))
					{
						TRACE("Error loading animated texture :\n");
						OutputDebugString(DXGetErrorString9(hr));
						TRACE("\n");
					}
					mNumFrames++;
				}
				CONSOLE.StatusDone(status, TRUE);
			}
			else
			{
				char	status[256];
				sprintf(status, "Loading texture %s", mName);
				CONSOLE.Status(status);
				DWORD	mipfilter = (mMipFilter ? D3DX_FILTER_BOX | D3DX_FILTER_MIRROR :
											   D3DX_FILTER_POINT | D3DX_FILTER_MIRROR);
				hr = D3DXCreateTextureFromFileEx(LT.GetDevice(), ToTCHAR(path), D3DX_DEFAULT, D3DX_DEFAULT, mLevels, 0,
												 ToD3DFormat(mTexFormat), (D3DPOOL)mPool, D3DX_FILTER_BOX | D3DX_FILTER_MIRROR,
												 mipfilter,
												 0, NULL, NULL, &mTextures[0]);
				if (hr == 0x80070002)
				{
					CONSOLE.StatusDone(status, FALSE);
					TRACE("Warning : Texture not found...\n");
					TRACE(path);
					TRACE("\n");
				}
				else if (FAILED(hr))
				{
					CONSOLE.StatusDone(status, FALSE);
					TRACE("WARNING : Error loading texture :\n");
					OutputDebugString(DXGetErrorString9(hr));
					TRACE("\n");
					TRACE(path);
					TRACE("\n");
				}
				else
					CONSOLE.StatusDone(status, TRUE);
			}
		}
	}

	if (hr == S_OK)
	{
		D3DSURFACE_DESC	desc;
		mTextures[0]->GetLevelDesc(0, &desc);
		mWidth = desc.Width;
		mHeight = desc.Height;
		if (CLIPARAMS.mBuildResources || !mTexFormat)
			mTexFormat = FromD3DFormat(desc.Format);

		if (strlen(mName))
			MEM_MANAGER.TagMem(this, LT.GetBPP(ToD3DFormat(mTexFormat)) * mHeight * mWidth * mNumFrames / 8,
							   "Textures/%s/Loaded (%dx%dx%dx%d)", mName, mWidth, mHeight, LT.GetBPP(ToD3DFormat(mTexFormat)), mNumFrames);
		else
			MEM_MANAGER.TagMem(this, LT.GetBPP(ToD3DFormat(mTexFormat)) * mHeight * mWidth * mNumFrames / 8,
							   "Textures/No name/ (%dx%dx%dx%d)", mWidth, mHeight, LT.GetBPP(ToD3DFormat(mTexFormat)), mNumFrames);
	}
	else
	{
		mWidth = -1;
		mHeight = -1;
	}

	return hr;
}

static FVector	TEXTURE_ZERO2(0.0f, 0.0f, 0.0f);	// 009cc138
// FUNCTION: 0x005585e0 init:TEXTURE_ZERO2

//******************************************************************************************
HRESULT	CDXTexture::DeleteDeviceObjects()	// FUNCTION: 0x00558600
{
	for (int i = 0; i < mNumFrames; i++)
	{
		if (mTextures[i])
		{
			MEM_MANAGER.UnTagMem(this);
			int	refs = mTextures[i]->Release();
			if (refs > 0)
			{
				char	buffer[256];
				sprintf(buffer, "Texture %s refcount=%d\n", mName, refs);
				TRACE(buffer);
			}
			mTextures[i] = NULL;
		}
	}
	return S_OK;
}

//******************************************************************************************
IDirect3DTexture9	*CDXTexture::GetTexture()	// FUNCTION: 0x00558690
{
	if (mNumFrames == 1)
		return mTextures[0];
	return mTextures[int(ENGINE.GetRenderTime() / 0.05f) % mNumFrames];
}

// Pixel layouts, all read through DWORD bitfields: retail loads a whole dword even for a 16-bit pixel
// (00558786) and a component's low byte on its own (0055878c).
struct SPixel565	{ DWORD b : 5; DWORD g : 6; DWORD r : 5; };
struct SPixel1555	{ DWORD b : 5; DWORD g : 5; DWORD r : 5; DWORD a : 1; };
struct SPixel8888	{ DWORD b : 8; DWORD g : 8; DWORD r : 8; DWORD a : 8; };

//******************************************************************************************
// 16-bit rows are indexed as WORDs (edi*2) and each pixel read through its DWORD bitfield struct.
void	CDXTexture::Dump(char *name)	// FUNCTION: 0x005586e0
{
	D3DSURFACE_DESC	desc;
	mTextures[0]->GetLevelDesc(0, &desc);
	D3DLOCKED_RECT	lr;
	if (FAILED(mTextures[0]->LockRect(0, &lr, NULL, 0)))
		return;

	int		height = desc.Height;
	int		width = desc.Width;
#line 929
	UBYTE	*data = (UBYTE *)new(MEMTYPE_TEXTURE) UBYTE[width * height * 4];
	UBYTE	*p = data;
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			UBYTE	c0, c1, c2;
			if (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8)
			{
				SPixel8888	*c = &((SPixel8888 *)((UBYTE *)lr.pBits + lr.Pitch * y))[x];
				c0 = c->r;
				c1 = c->g;
				c2 = c->b;
			}
			else if (desc.Format == D3DFMT_R5G6B5)
			{
				SPixel565	*c = (SPixel565 *)&((WORD *)((UBYTE *)lr.pBits + lr.Pitch * y))[x];
				c0 = c->b << 3;
				c1 = c->g << 2;
				c2 = c->r << 3;
			}
			else if (desc.Format == D3DFMT_A1R5G5B5)
			{
				SPixel1555	*c = (SPixel1555 *)&((WORD *)((UBYTE *)lr.pBits + lr.Pitch * y))[x];
				c0 = c->b << 3;
				c1 = c->g << 3;
				c2 = c->r << 3;
			}
			else
			{
				TRACE("WARNING : Attempt to dump texture with unknown format!\n");
				c0 = 0;
				c1 = 0;
				c2 = 0;
			}
			*p++ = c0;
			*p++ = c1;
			*p++ = c2;
			*p++ = 0;
		}
	}
	mTextures[0]->UnlockRect(0);
	CTGALoader::Save24BitWithPitch(name, (ULONG *)data, width, height, desc.Width * 4);
	delete [] data;
}

//******************************************************************************************
// The console's dumptextures: every unnamed or special texture to TextureDump\N.tga.
void	CDXTexture::DumpInterestingTextures()	// FUNCTION: 0x00558870
{
	char	name[256];
	int		n = 0;
	CDXTexture	*t = CDXTexture::mFirst;
	CreateDirectory(ToTCHAR("TextureDump"), NULL);
	for (; t; t = t->CTexture::mNext)
	{
		if (strlen(t->mName) == 0 || t->mName[0] == '*')
		{
			sprintf(name, "TextureDump\\%d.tga", n);
			t->Dump(name);
			n++;
		}
	}
}

//******************************************************************************************
// STAND-IN (labelled 2026-10-15; origin not recorded): the projection's near depth read through a file-local
// inline with a named float. Written plainly (`ENGINE.GetNearZ() * ENGINE.GetFarZ()`, or a named local in Activate):
// Activate 14/14/27 by score, and GetTexture (00558690) and Select (005593a0) lose their matches. Original: unknown.
static inline float NearDepth(CEngine& engine)
{
	float nearZ = engine.GetNearZ();
	return nearZ;
}

// Stage 0's states for the texture's mode: false when the card cannot blend it.
bool	CDXTexture::Activate()	// FUNCTION: 0x005588f0
{
	if (CMeshRenderer::mRenderMode == 5)
		return true;

	CLight	light;
	D3DXMATRIX	world;
	D3DXMATRIX	inv;

	if (mScaleU != 1.f || mScaleV != 1.f || mOffsetU != 0.f || mOffsetV != 0.f)
	{
		mOffsetU = mOffsetU - (mScaleU - 1.f) * 0.5f;
		mOffsetV = (mScaleV - 1.f) * 0.5f + mOffsetV;
		D3DMATRIX m = {mScaleU, 0, 0, 0,
			0, mScaleV, 0, 0,
			mOffsetU, mOffsetV, 1.f, 0,
			0, 0, 0, 1.f};
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &m);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
	}

	SINT	alpha = MESH_ALPHA;
	switch (mMode)
	{
	case 0:
		SetupAlphaStage(alpha);
		if (alpha != 255)
		{
			LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
			return true;
		}
		break;

	case 4:
		LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
		LT.SetAlphaRef(1);
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		SetupAlphaStage(alpha);
		return true;

	case 1:
		{
			// dot-product bump mapping against the first light, in object space
			LT.STS(0, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3);
			LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			LT.STS(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
			LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
			light = RENDERINFO.mLights[0];
			RENDERINFO.GetWorld(&world);
			D3DXMatrixTranspose(&inv, &world);
			D3DXVECTOR3	dir = *(D3DXVECTOR3 *)&light.mDir;
			D3DXVECTOR4	v;
			D3DXVec3Transform(&v, &dir, &inv);
			LT.SRS(D3DRS_TEXTUREFACTOR, (0xff << 24) + (int(v.x * 127.f + 128.f) << 16) + (int(v.y * 127.f + 128.f) << 8) + int(v.z * 127.f + 128.f));
		}
		return true;

	case 2:
		{
			// an environment map from the camera-space reflection vector
			D3DXMATRIX	m(0.5f, 0, 0, 0,
						  0, -0.5f, 0, 0,
						  0, 0, 1.f, 0,
						  0.5f, 0.5f, 0, 1.f);
			LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &m);
			LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
			LT.STS(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR);
			LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
			LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
			if (!SetupAlphaStage(alpha))
				return false;
		}
		break;

	case 3:
		{
			// projected from the camera position
			D3DMATRIX	m;
			RENDERINFO.GetProjection(&m);
			m._11 *= 1.0f / 3.28f;
			m._12 *= 1.0f / 3.28f;
			m._13 *= 1.0f / 3.28f;
			m._14 *= 1.0f / 3.28f;
			m._21 *= -1.0f / 2.25f;
			m._22 *= -1.0f / 2.25f;
			m._23 *= -1.0f / 2.25f;
			m._24 *= -1.0f / 2.25f;
			float	nf = -(NearDepth(ENGINE) * ENGINE.GetFarZ());
			float	d = ENGINE.GetFarZ() - ENGINE.GetNearZ();
			m._31 = -(nf * 3.12f / d);
			m._32 = -(nf * 4.68f / d);
			LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &m);
			LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT4 | D3DTTFF_PROJECTED);
			LT.STS(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEPOSITION);
			RENDERINFO.SetLightingEnabled(false);
			LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
			SetupAlphaStage(alpha);
		}
		return true;

	case 5:
		return false;
	}
	return true;
}

//******************************************************************************************
// Stage 1 fades the texture by the mesh's alpha through the texture factor.
bool	CDXTexture::SetupAlphaStage(SINT alpha)	// FUNCTION: 0x00558ef0
{
	if (alpha == 255)
		return true;
	if (RENDERSTATE_TWEAK_CARD_CANT_DO_TFACTOR.IsSet() && !RENDERSTATE_TWEAK_CARD_CANT_DO_TFACTOR.mValue)
		return false;
	LT.STS(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(1, D3DTSS_COLORARG1, D3DTA_CURRENT);
	LT.STS(1, D3DTSS_COLORARG2, D3DTA_TFACTOR);
	LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_CURRENT);
	LT.STS(1, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);
	LT.SRS(D3DRS_TEXTUREFACTOR, (alpha << 24) + 0xffffff);
	LT.STS(1, D3DTSS_TEXCOORDINDEX, 0);
	return true;
}

//******************************************************************************************
// Undoes Activate's states.
void	CDXTexture::Deactivate(bool keepalpha)	// FUNCTION: 0x00558fb0
{
	if (CMeshRenderer::mRenderMode == 5)
		return;

	SINT	alpha = MESH_ALPHA;
	if (mScaleU != 1.f || mScaleV != 1.f || mOffsetU != 0.f || mOffsetV != 0.f)
	{
		D3DXMATRIX m;
		D3DXMatrixIdentity(&m);
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &m);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	}

	switch (mMode)
	{
	case 0:
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
		LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		if (alpha != 255)
		{
			LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
			if (!keepalpha)
				LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
		}
		return;

	case 4:
		LT.SetAlphaRef(8);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
		LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		if (alpha != 255)
		{
			LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
			if (!keepalpha)
				LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
		}
		return;

	case 1:
		LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		LT.SetModulate(0);
		LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
		return;

	case 2:
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
		LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
		break;

	case 3:
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
		RENDERINFO.SetLightingEnabled(true);
		LT.SetModulate(0);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
		LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.STS(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
		if (alpha != 255)
		{
			LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
			if (!keepalpha)
				LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
		}
		return;

	default:
		return;
	}

	if (alpha != 255)
	{
		LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
		if (!keepalpha)
			LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
	}
}

//******************************************************************************************
void	CDXTexture::Select()	// FUNCTION: 0x005593a0
{
	if (CMeshRenderer::mRenderMode != 5)
		LT.D3D_SetTexture(0, GetTexture());
}

//******************************************************************************************
// Matched. The allocation follows the copy loops' shape: the box filter and the per-pixel copy step
// s and d themselves, from row starts saved before each row (s0, d0), and advance from those at the row's end. VC6
// then keeps s and d in eax/edx through both loops and their latches (rebuilt from the saved copies: 00559a0a,
// 00559a9a), the inline memcpy's count copy takes ebp (00559a6f), and w, srcw and shift are split around the inner
// loops (w in memory at frame's dead home, srcw ebp outside them, both reloaded after each: 00559a06, 00559a92,
// 00559a96). Separate sp/dp pointer copies gave w or shift ebp whole in the first round instead (for loop 351/641,
// the old guarded do-while 174/600; register-forcing logs). The box filter's x loop is a plain for loop:
// retail counts it down from a copy of w (005599ac..00559a04). The skip loop decrements levels before halving mWidth
// and then mHeight (mWidth's temporary in ecx, 0055961c), the per-pixel copy advances s before d (the step's reload
// in edi, 00559a81), and width is read before height (their spill stores in retail's order, 00559437).
// The 32-bit sources are DWORD arrays, whose size keeps retail's shr/shl pair (the hand-written `(n >> 2) << 2`
// folds to an and).
// A frame from a resource file: its mip levels, halved while they exceed the card's limit, in the file's format or
// converted to a 16-bit one.
void	CDXTexture::CreateMipmaps(CChunkReader *c, SINT frame, SINT levels)	// FUNCTION: 0x00559410
{
	DWORD	width = mWidth;
	DWORD	height = mHeight;
	ETextureFormat	fileformat = mTexFormat;
	SINT	createlevels = levels;
	while (width > LT.m_d3dCaps.MaxTextureWidth || height > LT.m_d3dCaps.MaxTextureHeight)
	{
		width >>= 1;
		height >>= 1;
		if (createlevels > 1)
			createlevels--;
		if (mLevels > 1)
			mLevels--;
		mLossShift++;
	}
	TRACE("************ lose res texture: %s\n", mName);

	if (FAILED(LT.D3D_CreateTextureNoCheck(width, height, createlevels, 0, ToD3DFormat(fileformat), (D3DPOOL)mPool, &mTextures[frame])))
	{
		switch (mTexFormat)
		{
		case TEXFMT_A8R8G8B8:	fileformat = TEXFMT_A4R4G4B4;	break;
		case TEXFMT_X8R8G8B8:	fileformat = TEXFMT_R5G6B5;		break;
		default:
			CONSOLE.FatalError(FALSE, 0xc7, -1);
			mTextures[frame] = NULL;
			return;
		}
		if (FAILED(LT.D3D_CreateTextureNoCheck(width, height, createlevels, 0, ToD3DFormat(fileformat), (D3DPOOL)mPool, &mTextures[frame])))
		{
			CONSOLE.FatalError(FALSE, 0xc8, -1);
			mTextures[frame] = NULL;
			return;
		}
	}

	MEMORYSTATUS	ms;
	GlobalMemoryStatus(&ms);

	// skip the levels too big for the card
	while (levels > 1 && ((DWORD)mWidth > width || (DWORD)mHeight > height))
	{
		c->GetNext();
		c->Skip();
		levels--;
		mWidth >>= 1;
		mHeight >>= 1;
	}

	DWORD	w = width;
	DWORD	h = height;
	SINT	srcw = mWidth;
	SINT	srch = mHeight;
	for (int level = 0; level < levels; level++)
	{
		c->GetNext();
		D3DLOCKED_RECT	lr;
		if (FAILED(mTextures[frame]->LockRect(level, &lr, NULL, 0)))
			return;

		// the source is 1 << shift times the destination's size
		int	shift = 0;
		while ((w << shift) < srcw)
			shift++;

		if (mTexFormat == TEXFMT_A8R8G8B8 && fileformat == TEXFMT_A4R4G4B4)
		{
#line 2938
			DWORD	*src = new(MEMTYPE_TEMP) DWORD[c->GetSize() >> 2];
			c->Read(src, c->GetSize(), 1);
			for (DWORD y = 0; y < h; y++)
				for (DWORD x = 0; x < w; x++)
				{
					DWORD	s = src[(x << shift) + (y << shift) * srcw];
					((WORD *)lr.pBits)[(lr.Pitch * y >> 1) + x] = (WORD)(((s >> 16) & 0xf000) + ((s >> 12) & 0xf00) + ((s >> 8) & 0xf0) + ((s >> 4) & 0xf));
				}
			delete [] src;
		}
		else if (mTexFormat == TEXFMT_X8R8G8B8 && fileformat == TEXFMT_R5G6B5)
		{
#line 2962
			DWORD	*src = new(MEMTYPE_TEMP) DWORD[c->GetSize() >> 2];
			c->Read(src, c->GetSize(), 1);
			for (DWORD y = 0; y < h; y++)
				for (DWORD x = 0; x < w; x++)
				{
					DWORD	s = src[(x << shift) + (y << shift) * srcw];
					((WORD *)lr.pBits)[(y * lr.Pitch >> 1) + x] = (WORD)(((s >> 8) & 0xf800) + ((s >> 5) & 0x7e0) + ((s >> 3) & 0x1f));
				}
			delete [] src;
		}
		else if (mTexFormat == fileformat)
		{
			int	bpp = 2;
			switch (mTexFormat)
			{
			case TEXFMT_X8R8G8B8:
			case TEXFMT_A8R8G8B8:
				bpp = 4;
				break;
			case TEXFMT_A1R5G5B5:
			case TEXFMT_A4R4G4B4:
			case TEXFMT_R5G6B5:
				bpp = 2;
				break;
			}
#line 3007
			UBYTE	*src = new(MEMTYPE_TEMP) UBYTE[c->GetSize()];
			c->Read(src, c->GetSize(), 1);
			UBYTE	*s = src;
			UBYTE	*d = (UBYTE *)lr.pBits;
			if (w == srcw)
			{
				for (DWORD y = 0; y < h; y++)
				{
					memcpy(d, s, bpp * srcw);
					s += bpp * srcw;
					d += lr.Pitch;
				}
			}
			else if (w == (srcw >> 1) && h == (DWORD)(srch >> 1) && (mTexFormat == TEXFMT_X8R8G8B8 || mTexFormat == TEXFMT_A8R8G8B8))
			{
				// a box filter of the level above
				for (DWORD y = 0; y < h; y++)
				{
					UBYTE	*s0 = s;
					UBYTE	*d0 = d;
					for (DWORD x = 0; x < w; x++)
					{
						DWORD	*sp = (DWORD *)s;
						DWORD	*below = (DWORD *)(s + ((srcw * 4) << shift));
						*(DWORD *)d = ((below[1] >> 2) & 0x3f3f3f3f) + ((sp[1] >> 2) & 0x3f3f3f3f) + ((below[0] >> 2) & 0x3f3f3f3f) + ((sp[0] >> 2) & 0x3f3f3f3f);
						s += bpp << shift;
						d += bpp;
					}
					s = s0 + ((bpp * srcw) << shift);
					d = d0 + lr.Pitch;
				}
			}
			else
			{
				for (DWORD y = 0; y < h; y++)
				{
					UBYTE	*s0 = s;
					UBYTE	*d0 = d;
					for (DWORD x = 0; x < w; x++)
					{
						memcpy(d, s, bpp);
						s += bpp << shift;
						d += bpp;
					}
					s = s0 + ((bpp * srcw) << shift);
					d = d0 + lr.Pitch;
				}
			}
			delete [] src;
		}
		else
		{
			CONSOLE.FatalError(FALSE, 0xc9, -1);
			c->Skip();
		}

		mTextures[frame]->UnlockRect(level);
		w >>= 1;
		h >>= 1;
		srcw >>= 1;
		srch >>= 1;
	}

	// Keep the reduced base-level dimensions after walking the individual mips.
	mWidth = width;
	mHeight = (SWORD)height;
	mTexFormat = fileformat;
}

static SINT	sTextureSizes[32];					// 009cc058: the count of textures loaded by power-of-two width

//******************************************************************************************
// A texture from a resource file: its record, then each frame's levels (or .aya files for LoadFrame).
CDXTexture	*CDXTexture::Deserialize(bool fromresources, CChunkReader *c)	// FUNCTION: 0x00559be0
{
#line 3109
	CDXTexture	*t = new(MEMTYPE_TEXTURE) CDXTexture;

	// the record overwrites everything but the object's links
	DeviceObject	*devnext = t->DeviceObject::mNext;
	CDXTexture	*next = t->CTexture::mNext;
	void		*vtable = *(void **)t;
	c->GetNext();
	c->GetNext();
	c->Read(t, sizeof(CDXTexture), 1);

	for (int frame = 0; frame < t->mNumFrames; frame++)
	{
		c->GetNext();
		SINT	levels;
		c->Read(&levels, 4, 1);
		if (levels <= 1)
			levels = 1;
		t->mResourceLevels = levels;

		int	i;
		for (i = 0; i < 32; i++)
			if ((1 << i) >= t->mWidth)
				break;
		sTextureSizes[i]++;

		if (!fromresources)
			t->LoadFrame(frame);
		else if (FAILED(LT.D3D_CreateTextureNoCheck(t->mWidth, t->mHeight, levels, 0, ToD3DFormat(t->mTexFormat), (D3DPOOL)t->mPool, &t->mTextures[frame])))
			t->CreateMipmaps(c, frame, levels);
		else
		{
			MEMORYSTATUS	ms;
			GlobalMemoryStatus(&ms);
			for (int level = 0; level < levels; level++)
			{
				c->GetNext();
				D3DSURFACE_DESC	desc;
				t->mTextures[frame]->GetLevelDesc(level, &desc);
				D3DLOCKED_RECT	lr;
				if (FAILED(t->mTextures[frame]->LockRect(level, &lr, NULL, 0)))
					return t;
				UINT	rows = (SWORD)(t->mHeight >> level);
				UINT	rowsize = c->GetSize() / rows;
				UBYTE	*d = (UBYTE *)lr.pBits;
				int y;
				for (y = rows; y != 0; y--)
				{
					c->Read(d, rowsize, 1);
					d += lr.Pitch;
				}
				// The exhausted row counter selects frame zero for the unlock (00559e18, 00559e44),
				// whichever frame was locked
				t->mTextures[y]->UnlockRect(level);
			}
		}
	}

	t->DeviceObject::mNext = devnext;
	t->CTexture::mNext = next;
	t->mRefCount = 0;
	t->mVBufTexture = NULL;
	*(void **)t = vtable;

	MEM_MANAGER.TagMem(t, LT.GetBPP(ToD3DFormat(t->mTexFormat)) * t->mHeight * t->mNumFrames * t->mWidth / 8,
					   "Textures/%s/Deserialised (%dx%dx%dx%d)", t->mName, t->mWidth, t->mHeight,
					   LT.GetBPP(ToD3DFormat(t->mTexFormat)), t->mNumFrames);

	for (CDXTexture *o = CDXTexture::mFirst; o && t; o = o->CTexture::mNext)
	{
		if (stricmp(t->mName, o->mName) == 0 && strlen(t->mName) && t->mName[0] != '*' && o->mFormat == t->mFormat && o != t)
		{
			char	buffer[256];
			sprintf(buffer, "Warning : Texture %s in resource file was already loaded!\n", t->mName);
			TRACE(buffer);
		}
	}
	if (stricmp(t->mName, "FrontEnd\\v3\\FE_BEA_title_nav_symbol_language01.tga") == 0)
		TRACE("leaker!\n");

	return t;
}

//******************************************************************************************
// TEXFMT_UNKNOWN is the last case: its block and the final return merge into one, and where it sits decides which
// copy C2's tail merge (0x10735042) keeps where LoadFrame inlines this into a store (retail's 00557499, after the
// other cases). Out of line both orders compile the same.
D3DFORMAT	CDXTexture::ToD3DFormat(ETextureFormat format)	// FUNCTION: 0x0055a0f0
{
	switch (format)
	{
	case TEXFMT_A1R5G5B5:	return D3DFMT_A1R5G5B5;
	case TEXFMT_A4R4G4B4:	return D3DFMT_A4R4G4B4;
	case TEXFMT_X8R8G8B8:	return D3DFMT_X8R8G8B8;
	case TEXFMT_A8R8G8B8:	return D3DFMT_A8R8G8B8;
	case TEXFMT_R5G6B5:		return D3DFMT_R5G6B5;
	case TEXFMT_DXT1:		return D3DFMT_DXT1;
	case TEXFMT_DXT2:		return D3DFMT_DXT2;
	case TEXFMT_DXT4:		return D3DFMT_DXT4;
	case TEXFMT_V8U8:		return D3DFMT_V8U8;
	case TEXFMT_Q8W8V8U8:	return D3DFMT_Q8W8V8U8;
	case TEXFMT_UNKNOWN:	return D3DFMT_UNKNOWN;
	}
	return D3DFMT_UNKNOWN;
}

//******************************************************************************************
ETextureFormat	CDXTexture::FromD3DFormat(D3DFORMAT format)	// FUNCTION: 0x0055a170
{
	switch (format)
	{
	case D3DFMT_UNKNOWN:	return TEXFMT_UNKNOWN;
	case D3DFMT_A1R5G5B5:	return TEXFMT_A1R5G5B5;
	case D3DFMT_A4R4G4B4:	return TEXFMT_A4R4G4B4;
	case D3DFMT_X8R8G8B8:	return TEXFMT_X8R8G8B8;
	case D3DFMT_A8R8G8B8:	return TEXFMT_A8R8G8B8;
	case D3DFMT_R5G6B5:		return TEXFMT_R5G6B5;
	case D3DFMT_DXT1:		return TEXFMT_DXT1;
	case D3DFMT_DXT2:		return TEXFMT_DXT2;
	case D3DFMT_DXT4:		return TEXFMT_DXT4;
	case D3DFMT_V8U8:		return TEXFMT_V8U8;
	case D3DFMT_Q8W8V8U8:	return TEXFMT_Q8W8V8U8;
	}
	return TEXFMT_UNKNOWN;
}
