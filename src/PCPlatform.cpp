#define PCPLATFORM_CPP
#include	"Common.h"
#if TARGET == PC

#include	"Platform.h"
#include	"DX.h"
#include	"cframetimer.h"
#include	"float.h"
#include	"viewport.h"
#include	"VertexShader.h"
#include	"cliparams.h"
#include	"maths.h"
#include	"DebugLog.h"

void	ProcessScreenDump();					// 00441b80: writes a screen grab the console asked for

// retail: inline (neither has a body of its own in the executable)
inline void	CPCPlatform::SetVertexShadersEnabled( BOOL aShaders )
{
	CVertexShader::VertexShadersEnabled = aShaders;
}

inline void CPCPlatform::MakeD3DViewport(D3DVIEWPORT9 *out,CViewport *in)
{
	out->Width=in->Width;
	out->Height=in->Height;
	out->X=in->X;
	out->Y=in->Y;
	out->MinZ=in->MinZ;
	out->MaxZ=in->MaxZ;
}

//*********************************************************************************
// FUNCTION: 0x005154b0 init:PLATFORM
CPCPlatform	PLATFORM;

//*********************************************************************************
BOOL	CPCPlatform::Init()	// FUNCTION: 0x005154e0
{
	SETUP_HISTORY.AddMessage("Platform init");

#line 27
    if(!(mFrameTimer = new( MEMTYPE_UNKNOWN ) CFrameTimer()))	return FALSE;
    mFrameTimer->Start(1.0f);

	// get how many processor ticks there are in a second
	QueryPerformanceFrequency((LARGE_INTEGER *)&mFrequency);

	// Can't do this here because the console isn't initialised yet!
	//CONSOLE.RegisterVariable("g_clockdivisor","Global clock divisor",CVar_float,&mClockDivisor);

	mClockDivisor=1.0f;

	// retail: no graphics card check; vertex shaders are on unless the command line says otherwise
	BOOL vshaders=TRUE;

	if (CLIPARAMS.mForcedShaders)
		vshaders=CLIPARAMS.mGeforce3;
	
	if (vshaders)
	{
		TRACE("Vertex shader suppport ENABLED\n");
		PLATFORM.SetVertexShadersEnabled(TRUE);
	}
		
	SETUP_HISTORY.AddMessage("Initting shaders");
	CVertexShader::InitShaders();
	SETUP_HISTORY.AddMessage("Done");

	return TRUE;
}

//*********************************************************************************

void	CPCPlatform::InitFonts()	// FUNCTION: 0x005155e0
{
	// Fonts
	
	if (!mFont)
	{
		TRACE("Warning : loading font manually\n");
#line 79
		mFont=new( MEMTYPE_FONT ) CBITMAPFONT();
		//	mFont->InitialiseAsBitmapFont("font.tga",32);
		mFont->InitialiseAsBitmapFont("font22.512.tga",32);
		mFont->EnableCharSwapHack(); // JCL - see header
	}
	
	if (!mDebugFont)
	{
		TRACE("Warning : loading debug font manually\n");
		mDebugFont=new( MEMTYPE_FONT ) CDXBitmapDebugFont();
		mDebugFont->InitialiseAsSysFont(ToTCHAR("Terminal"), 7, 0 );
	}
	
	if (!mSmallFont)
	{
		TRACE("Warning : loading small font manually\n");
#line 95
		mSmallFont =new( MEMTYPE_FONT ) CBITMAPFONT();
		//	mSmallFont->InitialiseAsBitmapFont("smallfont.tga",16);	
		mSmallFont->InitialiseAsBitmapFont("Font13PS.tga",16);	
	}
	
	if (!mTitleFont)
	{
		TRACE("Warning : loading title font manually\n");
#line 103
		mTitleFont =new( MEMTYPE_FONT ) CBITMAPFONT();
		mTitleFont->InitialiseAsBitmapFont("TitleFont.tga",32);	
	}
	
	// retail: no Xbox fonts on the PC
	mXboxFont = NULL;
	mSmallXboxFont = NULL;
}

//*********************************************************************************
void	CPCPlatform::Shutdown()	// FUNCTION: 0x005157b0
{
	SAFE_DELETE(mFont);
	SAFE_DELETE(mDebugFont);
	SAFE_DELETE(mSmallFont);
	SAFE_DELETE(mTitleFont);
	SAFE_DELETE(mXboxFont);
	SAFE_DELETE(mSmallXboxFont);
	SAFE_DELETE(mFrameTimer);
}

//*********************************************************************************
EQuitType	CPCPlatform::Process()	// FUNCTION: 0x00515880
{
	EQuitType	quit = QT_NONE;
	BOOL		more = TRUE;
	bool		first = true;
	do
	{
		more = LT.HandlingSystemStuff(first);
		if (LT.WantsToQuit())
			quit = QT_QUIT_TO_SYSTEM;
		first = false;
	} while (more);

	return quit;
}	

//*********************************************************************************
BOOL	CPCPlatform::BeginScene()	// FUNCTION: 0x005158c0
{
	return SUCCEEDED(LT.D3D_BeginScene());
}

//*********************************************************************************
void	CPCPlatform::EndScene()	// FUNCTION: 0x005158e0
{
	LT.D3D_EndScene();
}

//*********************************************************************************
void	CPCPlatform::DeviceFlip(BOOL in_game)	// FUNCTION: 0x005158f0
{
	if (mFrameTimer)
		mFrameTimer->Frame();

	ProcessScreenDump();
	LT.DisplayScreen();
}

//*********************************************************************************
void	CPCPlatform::ClearScreen(DWORD col)	// FUNCTION: 0x00515910
{
	LT.D3D_Clear( 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, col, 1.0f, 0L );
}

//*********************************************************************************
SINT		CPCPlatform::GetScreenWidth()
{
	return LT.GetWindowWidth();
}
	
int	CPCPlatform::GetWindowWidth()	// FUNCTION: 0x00515940
{
	return(LT.GetWindowWidth());
};

//*********************************************************************************
float	CPCPlatform::GetFPS()	// FUNCTION: 0x00515950
{
	if (mFrameTimer)
		return mFrameTimer->GetFramesPerSec();

	return 1.f;
}

//*********************************************************************************
BOOL	CPCPlatform::KeyOn(SINT c)	// FUNCTION: 0x00515970
{
	return LT.xKeyOn(c);
}

//*********************************************************************************
BOOL	CPCPlatform::KeyOnce(SINT c)	// FUNCTION: 0x00515980
{
	return LT.xKeyOnce(c);
}

//*********************************************************************************
void	CPCPlatform::FlushInputBuffers()	// FUNCTION: 0x005159b0
{
	LT.FlushInputBuffers();
}

//*********************************************************************************
void	CPCPlatform::SetKeytrap(pKeyTrapper trap)	// FUNCTION: 0x005159c0
{
	LT.SetKeytrap( trap );
}

//*********************************************************************************

float	CPCPlatform::GetSysTimeFloat()	// FUNCTION: 0x005159e0
{
	LARGE_INTEGER ts;
	
	static	BOOL	fs_done = FALSE;
	static LARGE_INTEGER first_seen;

	if(mFrequency.QuadPart)
	{
		QueryPerformanceCounter(&ts);
		if (!fs_done)
		{
			first_seen.QuadPart = ts.QuadPart;
			fs_done = TRUE;
		}
		// JCL - think about floating point inaccuracies here!!!!!
		return float(ts.QuadPart- first_seen.QuadPart) / (float(mFrequency.QuadPart)*mClockDivisor);
	}

	return float(timeGetTime()) / 1000.0f;	
}

//*********************************************************************************

CBITMAPFONT	*CPCPlatform::Font( EFontType aFontType )	// FUNCTION: 0x00515a70
{
	switch( aFontType )
	{
	case FONT_NORMAL:
		return(Font());
	case FONT_DEBUG:
		return(DebugFont());
	case FONT_SMALL:
		return(SmallFont());
	case FONT_TITLE:
		return(TitleFont());
	}
	return 0;
}

//*********************************************************************************

void CPCPlatform::SetViewport(CViewport *vp)	// FUNCTION: 0x00515ab0
{
	D3DVIEWPORT9 d3dvp;

	MakeD3DViewport(&d3dvp,vp);

	LT.GetDevice()->SetViewport(&d3dvp);
}

//*********************************************************************************

//*********************************************************************************

int	CPCPlatform::GetWindowHeight()	// FUNCTION: 0x00515b00
{
	return(LT.GetWindowHeight());
};

//*********************************************************************************

void CPCPlatform::Deserialize(CChunkReader *c)	// FUNCTION: 0x00515b10
{
	if (mFont)
	{
		TRACE("Warning : deserializing font twice!\n");
		SAFE_DELETE(mFont);
	}

	if (mXboxFont)
	{
		TRACE("Warning : deserializing font twice!\n");
		SAFE_DELETE(mXboxFont);
	}

	if (mSmallXboxFont)
	{
		TRACE("Warning : deserializing font twice!\n");
		SAFE_DELETE(mSmallXboxFont);
	}

	if (mFont)
	{
		TRACE("Warning : deserializing font twice!\n");
		SAFE_DELETE(mFont);
	}

	if (mDebugFont)
	{
		TRACE("Warning : deserializing font twice!\n");
		SAFE_DELETE(mDebugFont);
	}

	if (mTitleFont)
	{
		TRACE("Warning : deserializing font twice!\n");
		SAFE_DELETE(mTitleFont);
	}

	if (mSmallFont)
	{
		TRACE("Warning : deserializing font twice!\n");
		SAFE_DELETE(mSmallFont);
	}
	
	mFont=new CDXBitmapFont();
	mXboxFont=NULL;
	mSmallXboxFont=NULL;
	mDebugFont=new CDXBitmapDebugFont();
	mTitleFont=new CDXBitmapFont();
	mSmallFont=new CDXBitmapFont();
	
	mFont->Deserialize(c);
	mDebugFont->Deserialize(c);
	mTitleFont->Deserialize(c);
	mSmallFont->Deserialize(c);

	mFont->EnableCharSwapHack(); // JCL - see header
}

//*********************************************************************************

void CPCPlatform::SetRegKey(char *keyname,char *value)	// FUNCTION: 0x00515db0
{
	HKEY key;
	DWORD disposition;

	if (RegCreateKeyEx(HKEY_CURRENT_USER,"Software\\Lost Toys\\Battle Engine Aquila",0,"REG_SZ",REG_OPTION_VOLATILE,KEY_ALL_ACCESS,NULL,&key,&disposition)!=ERROR_SUCCESS)
	{
		ASSERT(0);
	}
	
	if (RegSetValueEx(key,keyname,0,REG_SZ,(unsigned char *) value,strlen(value)+1)!=ERROR_SUCCESS)
	{
		ASSERT(0);
	}
	
	RegCloseKey(key);
}

//******************************************************************************************
// The PC's pads have no rumble here (CBattleEngine calls it); the empty one-argument body is folded into the shared
// one (/OPT:ICF).

// FUNCTION: 0x00452b60
void CPCPlatform::TriggerRumble(int pad)
{
}

#endif

#include "mathsconst.h"	// the maths constant's include, where retail's initializer falls (placement evidence)	// FUNCTION: 0x00515e20 init:ZERO_FVECTOR
