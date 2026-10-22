// texture.cpp: the platform-independent texture record, the list of every texture and finding one by name
// (reconstructed; the file name is the object's __FILE__, 00632ef0). The PC texture itself is DXTexture.cpp's.

#include	"common.h"

#define TEXTURE_CPP
#include	"Texture.h"
#include	"mathsconst.h"
#include	"DebugLog.h"
#include	"console.h"
#include	"CLIParams.h"

CDXTexture	*CTexture::mFirst = NULL;				// 0083d9b0
static SINT		sCount;								// 0083d99c: numbers the unnamed ones
UBYTE		CDXTexture::mForceDeleting;				// 0083d998
static CDXTexture	*sDefault = NULL;					// 0083d9b4: "meshtex\default.tga", for what cannot load
static BOOL		sReportMissing = FALSE;				// 0083d9b8: cleared at the end of a level, never set

//*********************************************************************************
CTexture::CTexture()	// FUNCTION: 0x004f2710
{
	mNext = mFirst;
	mFirst = (CDXTexture *)this;
	mRefCount = 0;
	mLit = 0;
	mLossShift = 0;
	mUnknown94 = -1;
	memset(mName, 0, sizeof(mName));
	sprintf(mName, "JCLTEX #%d", sCount);
	sCount++;
}

//*********************************************************************************
CTexture::~CTexture()	// FUNCTION: 0x004f2790
{
	CDXTexture	*prev = NULL;
	for (CDXTexture *t = mFirst; t; t = t->CTexture::mNext)
	{
		if (t == (CDXTexture *)this)
		{
			if (prev)
				prev->CTexture::mNext = t->CTexture::mNext;
			else
				mFirst = t->CTexture::mNext;
		}
		prev = t;
	}
}

//*********************************************************************************
void	CTexture::Release()	// FUNCTION: 0x004f27e0
{
	mRefCount--;
}

//*********************************************************************************
// the texture of that name (and format and mip levels) if it is loaded, else a new one; the default one or NULL when
// it cannot load
CDXTexture	*CDXTexture::FindTexture(char *name, int a, int b, int c, int d, int e)	// FUNCTION: 0x004f27f0
{
	for (CDXTexture *t = mFirst; t; t = t->CTexture::mNext)
	{
		if (!stricmp(name, t->GetName()))
		{
			if ((a == t->mFormat) || (!a))
			{
				if (((c == -1) && (t->mLevels == -1)) || ((c != -1) && (c == t->mLevels)))
				{
					t->AddRef();
					return t;
				}

				char	message[256];
				sprintf(message, "Found possible match for texture %s, but mipmaps=%d, wanted %d\n", name, t->mLevels, c);
				TRACE(message);
			}
		}
	}

	if ((!CLIPARAMS.mAllowManualTextures) && (!CLIPARAMS.mBuildResources))
		CONSOLE.FatalFileError(name, -1);

	char	message[256];
	sprintf(message, "Warning : loading texture %s manually!\n", name);
	TRACE(message);

#line 152
	CDXTexture	*texture = new(MEMTYPE_TEXTURE) CDXTexture;
	if (texture->Init(name, (ETextureFormat)a, (ETextureType)b, c, e))
	{
		if (sReportMissing)
			LOG.AddMessage("Texture '%s' not found in level resource file", name);
		return texture;
	}

	texture->Release();
	texture->ForceDelete();
	if (d)
	{
		if (sDefault)
			sDefault->AddRef();
		return sDefault;
	}
	return NULL;
}

//*********************************************************************************
void	CDXTexture::LoadLevelResources(SINT level)	// FUNCTION: 0x004f29c0
{
	if (!sDefault)
		sDefault = FindTexture("meshtex\\default.tga", 0, 0, -1, 1, 1);

	char	message[256];
	sprintf(message, "Loading texture resources");
	CONSOLE.Status(message);
	CONSOLE.StatusDone(message, TRUE);
}

//*********************************************************************************
// frees every texture nothing uses, reports the rest, then frees those too
void	CDXTexture::ClearOut()	// FUNCTION: 0x004f2a30
{
	if (sDefault)
	{
		sDefault->Release();
		sDefault = NULL;
	}

	CDXTexture	*t = mFirst;
	while (t)
	{
		CDXTexture	*next = t->CTexture::mNext;
		if (!t->mRefCount)
			t->ForceDelete();
		t = next;
	}

	t = mFirst;
	if (t)
	{
		TRACE("\n----------------------\n");
		TRACE("Texture resource leaks\n");
		TRACE("----------------------\n\n");
		for (; t; t = t->CTexture::mNext)
		{
			char	message[256];
			sprintf(message, "Texture '%s' leaked : refcount=%d\n", t->GetName(), t->mRefCount);
			TRACE(message);
		}
		TRACE("\n----------------------\n\n");
	}
	else
		TRACE("No texture resource leaks\n");

	for (t = mFirst; t; t = t->CTexture::mNext)
		t->mRefCount = 0;

	t = mFirst;
	while (t)
	{
		CDXTexture	*next = t->CTexture::mNext;
		if (!t->mRefCount)
			t->ForceDelete();
		t = next;
	}
}

//*********************************************************************************
void	CDXTexture::FreeLevelResources()	// FUNCTION: 0x004f2b40
{
	sReportMissing = FALSE;
	TRACE("CTexture::FreeLevelResources() called\n");

	if (sDefault)
	{
		sDefault->Release();
		sDefault = NULL;
	}

	CDXTexture	*t = mFirst;
	while (t)
	{
		CDXTexture	*next = t->CTexture::mNext;
		if (!t->mRefCount)
			t->ForceDelete();
		t = next;
	}

	t = mFirst;
	if (t)
	{
		TRACE("\n----------------------------------\n");
		TRACE("Texture end-of-level resource leaks\n");
		TRACE("----------------------------------\n\n");
		for (; t; t = t->CTexture::mNext)
		{
			char	message[256];
			sprintf(message, "Texture '%s' leaked : refcount=%d\n", t->GetName(), t->mRefCount);
			TRACE(message);
		}
		TRACE("\n----------------------------------\n\n");
	}
	else
		TRACE("No end-of-level texture resource leaks\n");
}

// FUNCTION: 0x004f2c40 init:ZERO_FVECTOR
