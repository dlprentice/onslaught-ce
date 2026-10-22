// UMTexture.cpp: a texture the program fills itself (CUMTexture), and the string converters that share its object
// (004f79d0..004f7d92). Not in Stuart's release; reconstructed from the retail code. The file name follows the class.

#include	"Common.h"
#include	"ltshell.h"
#include	"UMTexture.h"
#include	"Texture.h"
#include	"tweak.h"
#include	"Platform.h"

extern CTweakSLONG	LANDSCAPE_TEXTURES_MANAGED;	// 006fabc0 (LandscapeTexture.cpp)

//*********************************************************************************
CUMTexture::CUMTexture()	// FUNCTION: 0x004f79d0
{
	mTexture = NULL;
	LT.AddDeviceObject(this);
}

// FUNCTION: 0x004f7a20 ??_GCUMTexture@@UAEPAXI@Z

CUMTexture::~CUMTexture()	// FUNCTION: 0x004f7a40
{
	LT.RemoveDeviceObject(this);
	SAFE_RELEASE(mTexture);
}

//*********************************************************************************
HRESULT	CUMTexture::Init(int size, int mode, int levels)	// FUNCTION: 0x004f7ab0
{
	mSize = size;
	mLevels = levels;
	if (mode == 0)
	{
		mFormat = D3DFMT_R5G6B5;
		mUsage = D3DUSAGE_RENDERTARGET;
		mPool = D3DPOOL_DEFAULT;
	}
	else if (mode == 3)
	{
		mFormat = D3DFMT_UNKNOWN;
		mUsage = D3DUSAGE_RENDERTARGET;
		mPool = D3DPOOL_DEFAULT;
	}
	else if (mode == 1)
	{
		mFormat = D3DFMT_R5G6B5;
		mUsage = 0;
		if (LANDSCAPE_TEXTURES_MANAGED.mValue)
			mPool = D3DPOOL_MANAGED;
		else
			mPool = D3DPOOL_DEFAULT;
	}
	else if (mode == 4)
	{
		mFormat = D3DFMT_A8R8G8B8;
		mUsage = 0;
		mPool = D3DPOOL_MANAGED;
	}
	else if (mode == 5)
	{
		mFormat = D3DFMT_R5G6B5;
		mUsage = 0;
		mPool = D3DPOOL_MANAGED;
	}
	return RestoreDeviceObjects();
}

//*********************************************************************************
HRESULT	CUMTexture::RestoreDeviceObjects()	// FUNCTION: 0x004f7b60
{
	mCreatedFormat = mFormat;
	if (!mFormat)
		mCreatedFormat = CDXTexture::ToD3DFormat(LT.GetDisplayFormat());

	SAFE_RELEASE(mTexture);
	HRESULT	hr = LT.D3D_CreateTextureNoCheck(mSize, mSize, mLevels, mUsage, (D3DFORMAT)mCreatedFormat, (D3DPOOL)mPool, &mTexture);
	if (FAILED(hr))
		return hr;
	return S_OK;
}

HRESULT	CUMTexture::InvalidateDeviceObjects()	// FUNCTION: 0x004f7bd0
{
	SAFE_RELEASE(mTexture);
	return S_OK;
}

//*********************************************************************************
// Each converter hands out the next of four buffers, so a few results can be used at once.

static char		sFromWCHAR[4][0x1000];			// 00840d40
static TCHAR	sFromTCHAR[4][0x1000] = {0};			// 00844d40
static TCHAR	sToTCHAR[4][0x1000] = {0};			// 00848d40
static WCHAR	sToWCHAR[4][0x1000] = {0};			// 0084cd40
static int		sToWCHARNext = 0;					// 00854d40
static int		sToTCHARNext = 0;					// 00854d44
static int		sFromTCHARNext = 0;					// 00854d48
static int		sFromWCHARNext = 0;					// 00854d4c

WCHAR	*ToWCHAR(char *text)	// FUNCTION: 0x004f7bf0
{
	int		len = strlen(text);
	if (++sToWCHARNext >= 4)
		sToWCHARNext = 0;
	for (int i = 0; i < len; i ++)
	{
		if (text[i] & 0x80)
			sToWCHAR[sToWCHARNext][i] = text[i] + 0x100;
		else
			sToWCHAR[sToWCHARNext][i] = text[i];
	}
	sToWCHAR[sToWCHARNext][len] = 0;
	return sToWCHAR[sToWCHARNext];
}

TCHAR	*ToTCHAR(char *text)	// FUNCTION: 0x004f7c70
{
	int		len = strlen(text);
	if (++sToTCHARNext >= 4)
		sToTCHARNext = 0;
	for (int i = 0; i < len; i ++)
		sToTCHAR[sToTCHARNext][i] = text[i];
	sToTCHAR[sToTCHARNext][len] = 0;
	return sToTCHAR[sToTCHARNext];
}

char	*FromTCHAR(TCHAR *text)	// FUNCTION: 0x004f7cd0
{
	int		len = strlen(text);
	if (++sFromTCHARNext >= 4)
		sFromTCHARNext = 0;
	for (int i = 0; i < len; i ++)
		sFromTCHAR[sFromTCHARNext][i] = text[i];
	sFromTCHAR[sFromTCHARNext][len] = 0;
	return sFromTCHAR[sFromTCHARNext];
}

char	*FromWCHAR(WCHAR *text)	// FUNCTION: 0x004f7d30
{
	int		len = wcslen(text);
	if (++sFromWCHARNext >= 4)
		sFromWCHARNext = 0;
	for (int i = 0; i < len; i ++)
		sFromWCHAR[sFromWCHARNext][i] = (char)text[i];
	sFromWCHAR[sFromWCHARNext][len] = 0;
	return sFromWCHAR[sFromWCHARNext];
}
