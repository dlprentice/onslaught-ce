// DXFrontEndVideo.cpp: the front end's Bink videos (reconstructed from the retail object, 00540fd0..00541ddf; not in
// Stuart's release). A worker thread opens the file; the video decodes into two textures in turn and is drawn as a
// quad. The allocations' line numbers are retail's (__FILE__ 00650744).

#include	"Common.h"
#define DXFRONTENDVIDEO_CPP
#include	"Frontend.h"
#include	"Engine.h"
#include	"RenderInfo.h"
#include	"Texture.h"
#include	"UMTexture.h"
#include	"WaitingThread.h"
#include	"binkapi.h"
#include	"tweak.h"
#include	"CLIParams.h"
#include	"Career.h"
#include	"console.h"
#include	"SoundManager.h"
#include	"DebugLog.h"
#include	"WarnOnce.h"
#include	<dxerr9.h>


// the maths constants' initializers come first (mathsconst.h)
// FUNCTION: 0x00540fd0 init:ZERO_FVECTOR
// FUNCTION: 0x00540ff0 init:ID_FMATRIX

// FUNCTION: 0x005410c0 init:USE_32_BIT_FMV
// FUNCTION: 0x005410e0 exit:USE_32_BIT_FMV
CTweakSLONG	USE_32_BIT_FMV("USE_32_BIT_FMV", 1);	// 0089ca48: 16-bit textures when 0 (InitVideo falls back)

//******************************************************************************************
// opens a Bink file on the worker thread: the file's name, the sound track, and the video once opened
class CBinkOpenThread : public CWaitingThread	// vtable 005e5078
{
public:
	virtual void	Run();					// 00541140

	bool			mOpened;				// +0x1c: InitVideo takes the video
	HBINK			mBink;					// +0x20
	char			mFileName[260];			// +0x24: cleared once opened
	char			mName[260];				// +0x128: for the error message
	BOOL			mPreload;				// +0x22c
	int				mSoundTrack;			// +0x230
};

// FUNCTION: 0x005410f0 init:sBinkOpenThread
// FUNCTION: 0x00541120 ??0CBinkOpenThread@@QAE@XZ
// FUNCTION: 0x00541110 exit:sBinkOpenThread
static CBinkOpenThread	sBinkOpenThread;	// 008a9810

// an Open made while the thread is busy, tried again by Render
static int		sPendingSoundTrack;			// 008a9808
static BOOL		sPendingPreload;			// 008a9804

//******************************************************************************************
void	CBinkOpenThread::Run()	// FUNCTION: 0x00541140
{
	if (SOUND.GetDirectSound())
		BinkSetSoundSystem(BinkOpenDirectSound, (U32)SOUND.GetDirectSound());

	U32	track = mSoundTrack;
	BinkSetSoundTrack(1, &track);

	if (!CLIPARAMS.mSkipFMV)
		mBink = BinkOpen(mFileName, (mPreload ? BINKPRELOADALL : 0) | BINKSNDTRACK);
	else
		mBink = NULL;

	float	volume = CAREER.GetSoundVolume();
	if (volume < 0.2f)
		volume = 0.2f;
	BinkSetVolume(mBink, track, (S32)(volume * volume * 20000.f));

	mFileName[0] = 0;
	mOpened = true;
}

//******************************************************************************************
CDXFrontEndVideo::CDXFrontEndVideo()	// FUNCTION: 0x00541200
{
	memset(mTextures, 0, sizeof(mTextures));
	mCurrentTexture = 0;
}
// FUNCTION: 0x00541220 ??_GCDXFrontEndVideo@@UAEPAXI@Z

//******************************************************************************************
HRESULT	CDXFrontEndVideo::RestoreDeviceObjects()	// FUNCTION: 0x005019c0
{
	return S_OK;
}

//******************************************************************************************
BOOL	CDXFrontEndVideo::Init()	// FUNCTION: 0x00541240
{
	mWidth = mHeight = 512;
	return TRUE;
}

//******************************************************************************************
void	CDXFrontEndVideo::Shutdown()	// FUNCTION: 0x00541260
{
	CloseVideo();
	mBink = NULL;
}

//******************************************************************************************
// Bink's memory
void	*__stdcall BinkAlloc(U32 bytes)	// FUNCTION: 0x00541280
{
	void	*mem = MEM_MANAGER.Alloc(bytes, MEMTYPE_BINK_VIDEO);
	if (!mem)
		return (void *)-1;
	return mem;
}

void	__stdcall BinkFree(void *mem)	// FUNCTION: 0x005412b0
{
	MEM_MANAGER.Free(mem);
}

//******************************************************************************************
// the pending Open's name, empty until one is pending. STAND-IN (type): retail initializes it dynamically here, its
// initializer (005412d0, .CRT$XCU slot 626) clearing the first character; the real string type is unknown.
static struct SPendingName
{
	char	mText[52];
	SPendingName() { mText[0] = 0; }
	operator char *() { return mText; }
} sPendingName;		// 008a97d0	// FUNCTION: 0x005412d0 init:sPendingName

//******************************************************************************************
void	CDXFrontEndVideo::Open(char *name, int width, int height, BOOL preload, BOOL async, int sound_track)	// FUNCTION: 0x005412e0
{
	RADSetMemory(BinkAlloc, BinkFree);
	mWidth = width;
	mHeight = height;

	if (mBink || sBinkOpenThread.IsBusy() || sBinkOpenThread.mOpened)
	{
		if (async && sBinkOpenThread.IsBusy())
		{
			strcpy(sPendingName, name);
			sPendingPreload = preload;
			sPendingSoundTrack = sound_track;
			return;
		}
		CloseVideo();
	}

	mSoundTrack = sound_track;
	sBinkOpenThread.mSoundTrack = sound_track;
	sBinkOpenThread.Acquire();
	strcpy(sBinkOpenThread.mFileName, name);
	strcpy(sBinkOpenThread.mName, name);
	sBinkOpenThread.mPreload = preload;
	sBinkOpenThread.mOpened = false;
	if (async)
		sBinkOpenThread.StartAsync();
	else
		sBinkOpenThread.RunSync();

	mFrameCopied = FALSE;
	mFrameDecoded = FALSE;
	if (!async)
	{
		InitVideo();
		mFadeAlpha = 255;
	}
	else
		mFadeAlpha = 0;
}

//******************************************************************************************
// the opened video's first frame, and textures big enough for it (as far as the card allows)
void	CDXFrontEndVideo::InitVideo()	// FUNCTION: 0x00541430
{
	sBinkOpenThread.Lock();
	mBink = sBinkOpenThread.mBink;
	sBinkOpenThread.mOpened = false;

	if (!mBink)
	{
		TRACE(" failed to find file");
		if (!CLIPARAMS.mSkipFMV)
			CONSOLE.FatalFileError(sBinkOpenThread.mName, -1);
		mFrameCopied = FALSE;
		mFrameDecoded = FALSE;
		mFadeAlpha = 0;
		sBinkOpenThread.Unlock();
		return;
	}

	BinkDoFrame(mBink);
	BinkNextFrame(mBink);
	mFrameDecoded = TRUE;
	mFrameCopied = FALSE;

	int i; for (i = 0; i < 2; i++)
	{
		if (mTextures[i])
		{
			delete mTextures[i];
			mTextures[i] = NULL;
		}
#line 256
		mTextures[i] = new (MEMTYPE_FMV) CUMTexture;
		if (!mTextures[i])
		{
			TRACE("couldn't create UMTex %d", i);
			sBinkOpenThread.Unlock();
			return;
		}
	}

	mWidth = mHeight = 128;
	while (mWidth < mBink->Width || mHeight < mBink->Height)
	{
		mWidth *= 2;
		mHeight *= 2;
	}
	while (LT.m_d3dCaps.MaxTextureWidth < mWidth)
		mWidth >>= 1;
	while (LT.m_d3dCaps.MaxTextureHeight < mHeight)
		mHeight >>= 1;
	CONSOLE.FatalError(mWidth >= 128, 197, -1);

	// STAND-IN (layout): `goto retry` restarts the loop at its top test, as retail does (5415e4..54162b). A do/while with a retry
	// flag is 40 bytes off, `for (;;)` with a test of i 77, `i = -1` in the loop 68 and a while loop 73.
retry:
	for (i = 0; i < 2; i++)
	{
		mTextures[i]->Init(mWidth, USE_32_BIT_FMV.mValue ? 4 : 5, TRUE);
		if (!mTextures[i]->mTexture && USE_32_BIT_FMV.mValue)
		{
			// no 32-bit texture: all of them 16-bit from now on
			USE_32_BIT_FMV.Set(0);
			goto retry;
		}
	}

	sBinkOpenThread.Unlock();
	mFadeAlpha = 0;
}

//******************************************************************************************
void	CDXFrontEndVideo::CloseVideo()	// FUNCTION: 0x00541650
{
	TRACE("Closing FMV\n");

	if (sBinkOpenThread.IsBusy())
	{
		TRACE("Waiting for open thread.\n");
		do
			Sleep(0);
		while (sBinkOpenThread.IsBusy());
		InitVideo();
	}
	else if (sBinkOpenThread.mOpened)
		InitVideo();

	sPendingName[0] = 0;
	sBinkOpenThread.Lock();

	for (int i = 0; i < 2; i++)
	{
		if (mTextures[i])
		{
			delete mTextures[i];
			mTextures[i] = NULL;
		}
	}

	if (mBink)
	{
		BINKSUMMARY	summary;
		BinkGetSummary(mBink, &summary);
		if (summary.TotalPlayedFrames)
			TRACE("Bink video closing. %i frames played, %i frames skipped (%i%%)\n", summary.TotalPlayedFrames,
				  summary.SkippedFrames, (int)(summary.SkippedFrames * 100.f / summary.TotalPlayedFrames));
		BinkClose(mBink);
	}
	mBink = NULL;

	sBinkOpenThread.Unlock();
	TRACE("done.\n");
}

//******************************************************************************************
int		CDXFrontEndVideo::GetWidth()	// FUNCTION: 0x00541770
{
	if (!mBink)
		return mWidth;
	return mBink->Width;
}

//******************************************************************************************
int		CDXFrontEndVideo::GetHeight()	// FUNCTION: 0x00541780
{
	if (!mBink)
		return mHeight;
	return mBink->Height;
}

// a transformed, lit vertex
struct SVideoVertex
{
	float	mX, mY, mZ, mRHW;
	DWORD	mColour;
	float	mU, mV;
};

//******************************************************************************************
// the current frame on a quad at x, y (its centre when centred), scaled; the default texture while no video plays
bool	CDXFrontEndVideo::Render(float x, float y, float z, float xscale, float yscale, DWORD colour, BOOL centred)	// FUNCTION: 0x00541790
{
	if (z == 0.f)
		z = 0.0001f;

	if (sPendingName[0])
	{
		if (!sBinkOpenThread.IsBusy())
		{
			char	name[52];
			strcpy(name, sPendingName);
			sPendingName[0] = 0;
			Open(name, mWidth, mHeight, sPendingPreload, TRUE, sPendingSoundTrack);
		}
	}
	else if (!mFrameCopied)
	{
		if (sBinkOpenThread.mOpened)
			InitVideo();

		if (mBink)
		{
			if (!mFrameDecoded)
			{
				BinkDoFrame(mBink);
				BinkNextFrame(mBink);
				mFrameDecoded = TRUE;
			}

			D3DLOCKED_RECT	locked;
			IDirect3DTexture9	*texture = mTextures[mCurrentTexture]->mTexture;
			if (mWidth >= mBink->Width && mHeight >= mBink->Height && texture)
			{
				mCurrentTexture = (mCurrentTexture + 1) % 2;

				HRESULT	hr = texture->LockRect(0, &locked, NULL, D3DLOCK_NOSYSLOCK);
				if (FAILED(hr))
				{
					TRACE("Failed lock\n");
					// FUNCTION: 0x00541d20 localexit:CDXFrontEndVideo::Render
					static CWarnOnce	warned;			// its destructor is empty: the atexit thunk is a lone ret
					if (!warned.Warn())
						SETUP_HISTORY.AddMessage("DXFEV: LockRect failed: %s", DXGetErrorString9(hr));
				}
				else
				{
					sBinkOpenThread.Lock();
					U32	flags = (USE_32_BIT_FMV.mValue ? BINKSURFACE32A | BINKCOPYNOSCALING : BINKSURFACE565 | BINKCOPYNOSCALING) | BINKCOPYALL;
					S32	skipped = BinkCopyToBuffer(mBink, locked.pBits, locked.Pitch, mHeight, 0, 0, flags);
					texture->UnlockRect(0);
					if (skipped)
						mCurrentTexture = (mCurrentTexture + 1) % 2;

					mFrameDecoded = FALSE;
					if (mBink)
					{
						BinkDoFrame(mBink);
						BinkNextFrame(mBink);
						mFrameDecoded = TRUE;
					}
					sBinkOpenThread.Unlock();
					mFrameCopied = TRUE;
				}
			}
		}
	}

	float	width, height;
	if (mBink && !sPendingName[0])
	{
		if (mFadeAlpha != 255)
			mFadeAlpha += 17;

		// STAND-IN (order; review F 2026-10-15): the fade read between the two size reads; reading both sizes first loses
		// 00541790.
		U32		bink_width = mBink->Width;
		DWORD	fade = mFadeAlpha;
		width = (float)bink_width;
		U32		bink_height = mBink->Height;
		height = (float)bink_height;
		DWORD	faded = (((colour >> 16) & 0xff) * fade) >> 8;
		faded = (faded << 8) + ((((colour >> 8) & 0xff) * fade) >> 8);
		faded = (faded << 8) + (((colour & 0xff) * fade) >> 8);
		colour = faded + (colour & 0xff000000);

		if (bink_width <= (U32)mWidth && bink_height <= (U32)mHeight)
			LT.D3D_SetTexture(0, mTextures[mCurrentTexture]->mTexture);
		else
		{
			LT.D3D_SetTexture(0, ENGINE.GetDefaultTexture()->GetTexture());
			colour &= 0xff000000;
		}
	}
	else
	{
		width = (float)mWidth;
		height = (float)mHeight;
		LT.D3D_SetTexture(0, ENGINE.GetDefaultTexture()->GetTexture());
		colour &= 0xff000000;
	}

	float	w = width * xscale;
	float	h = height * yscale;
	switch (centred)	// STAND-IN (layout; review F 2026-10-15): `if (centred)` loses 00541790
	{
	case TRUE:
		x -= w * 0.5f;
		y -= h * 0.5f;
		break;
	}

	float	u, v;
	SVideoVertex	verts[4];
	verts[0].mX = x;	verts[0].mY = y;	verts[0].mZ = z;	verts[0].mRHW = 1.f;	verts[0].mColour = colour;	verts[0].mU = 0.f;	verts[0].mV = 0.f;
	verts[1].mX = x + w;	verts[1].mY = y;	verts[1].mZ = z;	verts[1].mRHW = 1.f;	verts[1].mColour = colour;	verts[1].mU = u = width / mWidth;	verts[1].mV = 0.f;
	verts[2].mX = x + w;	verts[2].mY = y + h;	verts[2].mZ = z;	verts[2].mRHW = 1.f;	verts[2].mColour = colour;	verts[2].mU = u;	verts[2].mV = v = height / mHeight;
	verts[3].mX = x;	verts[3].mY = y + h;	verts[3].mZ = z;	verts[3].mRHW = 1.f;	verts[3].mColour = colour;	verts[3].mU = 0.f;	verts[3].mV = v;

	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	RENDERINFO.SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	RENDERINFO.EnableLighting(false);
	RENDERINFO.Apply();
	LT.GetDevice()->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, verts, sizeof(SVideoVertex));
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	return true;
}

//******************************************************************************************
int		CDXFrontEndVideo::NextFrame(bool wait)	// FUNCTION: 0x00541d30
{
	if (mBink)
	{
		sBinkOpenThread.Lock();
		if (mFrameCopied)
		{
			if (wait)
			{
				if (BinkWait(mBink))
				{
					sBinkOpenThread.Unlock();
					return -1;
				}
				mFrameCopied = FALSE;
			}
			else if (!BinkWait(mBink))
				mFrameCopied = FALSE;

			if (!mFrameCopied && !mFrameDecoded)
			{
				BinkDoFrame(mBink);
				BinkNextFrame(mBink);
				mFrameDecoded = TRUE;
			}
		}
		sBinkOpenThread.Unlock();

		if (mBink->FrameNum == mBink->Frames)
			return 1;
	}
	return 0;
}
