#include	"Common.h"

#include	"Platform.h"
//#include	"VertexShader.h"
#include	"CLIParams.h"
#include	"console.h"
#include	"text.h"

#include	<stdio.h>
#include	<string.h>

// What the retail GetParams reaches outside this file, declared here with just what it calls (STAND-INS for the real
// headers, which change this object: ltshell.h includes <vfw.h>, whose `static DWORD dwICValue` adds 4 bytes to .bss
// where retail's CLIParams.obj ends at 006630d8 with the next object's data; tweak.h moves the init and exit functions'
// static counters; DebugLog.h includes mathsconst.h, whose initializers retail's object lacks). The evidence for each
// is in its comment.
class CDebugLog		// vtable 005db01c; +0x04 is the file name the log opens with "w" (00441798)
{
public:
	void	SetFilename(char *name);	// 00441730
};
extern CDebugLog SETUP_HISTORY;			// 0066eb90: a second log whose file name defaults to "setuphistory.txt"

class CTweak		// vtable 005e4a94: one pure slot
{
public:
	virtual void	Set(float value) = 0;
};
struct CRenderMethod : public CTweak	// vtable 005e4a80; constructed as the tweak "landscape_method" (00545560)
{
public:
	virtual void	Set(float value);	// 00527d00
};
extern CRenderMethod LANDSCAPE_METHOD;	// 008aa920: constructed as the tweak "landscape_method" (00545560)

class CD3DApplication	// the application's base (d3dapp.h): SetResolution stores the pair at +0x330bc (0052c730)
{
public:
	void	SetResolution(int width, int height);
};
class PCLTShell : public CD3DApplication	// the application (LT, 00855bb0)
{
};
extern PCLTShell LT;

extern int	VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD;	// 00629410, 00679980, 00679984 ("Version %d.%02d.%02d")
extern char	DEFAULT_OPTIONS_NAME[];			// 0063db18, "defaultoptions.bea"

// FUNCTION: 0x004239c0 init:CLIPARAMS
// FUNCTION: 0x004239e0 exit:CLIPARAMS
CCLIParams CLIPARAMS;

static char	sSetupHistoryPath[MAX_PATH];	// 00662cb0

#define MAX_PARMS 30
#define PARM_LENGTH 256

// Retail stores the fields in this order (004239f0): Stuart's statements in his order, with the PC port's added
// between them.
CCLIParams::CCLIParams()	// FUNCTION: 0x004239f0
{
	mUnknown17d=false;
	mFindGoodWater=false;
	mFindBadWater=false;
	mBackBuffers=1;
	mMusic=TRUE;
	mDXTNTextures=TRUE;
	mBuildResources = FALSE;
	mBuildPCResources=FALSE;
	mBuildPS2Resources=FALSE;
	mBuildXBOXResources=FALSE;
	mDeveloperMode=FALSE;
	mNoStaticShadows=FALSE;
	mLargeRAM=FALSE;
	mUnknown185=false;
	mBuildModelInfo=FALSE;
	mNoBaseResources=FALSE;
	mBuildGoodies=FALSE;
	mShowDebugTrace=FALSE;
	mAutoConfigTest=FALSE;
	strcpy(mAutoConfigTestPath, "c:\\beaautoconfigtest\\");

	mGeforce3=FALSE;
	mForcedShaders=FALSE;
	mUnknown040=TRUE;
	mDevKit=FALSE;
	mHiDetailMode=FALSE;
	mDecimateMeshes=FALSE;
	mSound=TRUE;
	mUnknown18c=FALSE;
	mEmulateDVD=FALSE;
	mFlipSpeakers=FALSE;
	mHWSound=FALSE;
	mAllowManualTextures=false;
	mUseSampleBank=TRUE;
	mSoundRate=1;
	m3DSoundMethod=-1;
	mSoundBuffers=-1;
	mTestEur=false;
	mSoundDevice=0;
	mVSync=TRUE;
	mCardID=false;

	mTextureRAMLimit = 0x7fffffff; // default to huge limit, but not huge enough to confuse signed arithmetic

	mForceWindowed=FALSE;
	mResBuilderMode=FALSE;
	mStressTest=0;
	mTraceConsole=FALSE;

	mNoMeshPartReduction=FALSE;

	mArtistTest = FALSE;
	mLevelNo = -1;
	mRecordDemo=false;
	mPlaybackDemo=false;
	mConfigurationNo=0;
	mPal=TRUE;
	mGoStraightToDeviceSelectScreen = -1;
	mNiceCompression = true;
	mLanguage=0;	// LANG_ENGLISH (the game's own value; windows.h's is 9)
	mNoCodeOffCD=FALSE;
	mKillHUD = FALSE;
	strcpy(mSampleBankRoot, ".");

	mAttractMode=FALSE;
	if (E3_BUILD)
		mInactiveTimeout=120000;
	else
		mInactiveTimeout=-1;
	mGameplayTimeout=-1;

	// Default to using a pure device
	mPureDevice=TRUE;
	mSkipFMV = FALSE;
}

void	CCLIParams::GetParams(char *text)	// FUNCTION: 0x00423bc0
{
	if (text[0] == 0)
		return;
	
	// Be very careful not to use any game functions here, as
	// nothing is initialised at this point (not even the memory
	// manager)

	char	parms[MAX_PARMS][PARM_LENGTH];

	SINT	num_parms = 0;
	SINT	x = 0;
	parms[num_parms][x] = 0;

	// break into parms	
	while ((parms[num_parms][x] = *text) != NULL)
	{
		if (*text == ' ')
		{
			if (x != 0) // skip multiple whitespace
			{
				if (num_parms < MAX_PARMS)
				{
					parms[num_parms][x] = 0;
					num_parms ++;
					x = 0;
				}
				else 
					break;
			}
		}
		else
		{
			if (x < PARM_LENGTH)
				x ++;
			else
			{
				if (num_parms < MAX_PARMS)
				{
					parms[num_parms][x] = 0;
					num_parms ++;
					x = 0;
				}
				else 
					break;
			}
		}
		text ++;
	}
	num_parms ++;
	
	// scan parms
	SINT current_parm = 0;

	while (current_parm < num_parms)
	{
		if (!(stricmp(parms[current_parm], "-testeur")))
			mTestEur = true;

		if (!(stricmp(parms[current_parm], "-getversion")))
		{
			printf("\nVersion %d.%02d.%02d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_BUILD);
			ExitProcess((VERSION_MAJOR * 100 + VERSION_MINOR) * 100 + VERSION_BUILD);
		}

		if (!(stricmp(parms[current_parm], "-e3")))
			E3_BUILD = true;

		if (!(stricmp(parms[current_parm], "-defaultoptionsname")))
		{
			current_parm ++;
			sscanf(parms[current_parm], "%s", DEFAULT_OPTIONS_NAME);
		}

		if (!(stricmp(parms[current_parm], "-level")))
		{
			current_parm ++;
			sscanf(parms[current_parm], "%d", &mLevelNo);
		}

		if (!(stricmp(parms[current_parm], "-cardid")))
			mCardID = true;

		if (!(stricmp(parms[current_parm], "-backbuffer2")))
			mBackBuffers = 2;

		if (!(stricmp(parms[current_parm], "-findgoodwater")))
			mFindGoodWater = true;

		if (!(stricmp(parms[current_parm], "-timeout")))
		{
			if (current_parm < num_parms - 1)
			{
				current_parm ++;
				sscanf(parms[current_parm], "%d", &mInactiveTimeout);
			}
		}

		if (!(stricmp(parms[current_parm], "-findbadwater")))
			mFindBadWater = true;

		if (!(stricmp(parms[current_parm], "-playabledemo")))
			PLAYABLE_DEMO = TRUE;

		if (!(stricmp(parms[current_parm], "-res")))
		{
			if (current_parm < num_parms - 2)
			{
				current_parm ++;
				sscanf(parms[current_parm], "%d", &mResWidth);
				current_parm ++;
				sscanf(parms[current_parm], "%d", &mResHeight);
				if ((mResWidth < 640) || (mResHeight < 480))
				{
					mResWidth = 640;
					mResHeight = 480;
				}
				LT.SetResolution(mResWidth, mResHeight);
			}
		}

		if (!(stricmp(parms[current_parm], "-32bittextures")))
			mDXTNTextures = FALSE;

		if (!(stricmp(parms[current_parm], "-dxtntextures")))
			mDXTNTextures = TRUE;

		if (!(stricmp(parms[current_parm], "-traceconsole")))
			mTraceConsole = TRUE;

		if (!(stricmp(parms[current_parm], "-landscape0")))
			LANDSCAPE_METHOD.Set(0.0f);

		if (!(stricmp(parms[current_parm], "-landscape1")))
			LANDSCAPE_METHOD.Set(1.0f);

		if (!(stricmp(parms[current_parm], "-landscape2")))
			LANDSCAPE_METHOD.Set(2.0f);

		if (!(stricmp(parms[current_parm], "-autoconfigtest")))
		{
			mAutoConfigTest = TRUE;
			if ((current_parm < num_parms - 1) && (parms[current_parm + 1][0] != '-'))
			{
				current_parm ++;
				strcpy(mAutoConfigTestPath, parms[current_parm]);
				if (mAutoConfigTestPath[strlen(mAutoConfigTestPath) - 1] != '\\')
					strcat(mAutoConfigTestPath, "\\");
			}
			strcpy(sSetupHistoryPath, mAutoConfigTestPath);
			strcat(sSetupHistoryPath, "setuphistory.txt");
			PLATFORM.CreateDirectoryPath(sSetupHistoryPath, TRUE);
			SETUP_HISTORY.SetFilename(sSetupHistoryPath);
		}

		if (!(stricmp(parms[current_parm], "-nomusic")))
			mMusic=FALSE;

		if (!(stricmp(parms[current_parm], "-soundbuffers")))
		{
			if (current_parm < num_parms - 1)
			{
				current_parm ++;
				sscanf(parms[current_parm], "%d", &mSoundBuffers);
			}
		}

		if (!(stricmp(parms[current_parm], "-showdebugtrace")))
			mShowDebugTrace=TRUE;

		if (!(stricmp(parms[current_parm], "-nosound")))
			mSound=FALSE;

		if (!(stricmp(parms[current_parm], "-skipfmv")))
			mSkipFMV=TRUE;

		if (CLIPARAMS.mTestEur)
		{
			if (!(stricmp(parms[current_parm], "-forcewindowed")))
				mForceWindowed=TRUE;
		}

		current_parm++;
	};
}


void	CCLIParams::GetParams(int num_parms, char**parms)
{
	// This is *really*, *really* stupid, but the only easy way to do it :-(

	char cli[4096];

	strcpy(cli,"");

	for (int i=0;i<num_parms;i++)
	{
		strcat(cli," ");
		strcat(cli,parms[i]);
	}

	GetParams(cli);
}
// STAND-IN (link layout): retail's 8 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_CLIParams[8] = { 0 };
