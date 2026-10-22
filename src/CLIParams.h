// CLIParams.h: the command-line options (CLIPARAMS, 00662db8; 0x320 bytes).
//
// The retail PC build is a later revision than Stuart's file: it drops his developer options and adds the PC port's
// own (the resolution, texture format, sound device and landscape options, the automatic configuration test).
// Every offset comes from the retail constructor (004239f0), GetParams (00423bc0) and the code that reads the
// fields. Names come from, in order of strength: the option that sets the field; Stuart's field in the same
// position (his fields keep their order and his constructor's order; see the constructor); the RTTI class of the
// options screen that reads it (CVSync, CFlipSpeakers, CHWSound, CSoundDeviceSelect, C3DSoundMethod). Fields with
// no such evidence are named by offset.

#ifndef CLIPARAMS_H
#define CLIPARAMS_H

class	CCLIParams
{
public:
	CCLIParams();
	~CCLIParams() {};

	void	GetParams(char *text);
	void	GetParams(int num_params, char**parms);

	SINT	mBackBuffers;					// +0x000 -backbuffer2; read by CD3DApplication::Initialize3DEnvironment
	BOOL	mArtistTest;					// +0x004
	BOOL	mNoStaticShadows;				// +0x008 read by CStaticShadows
	SINT	mConfigurationNo;				// +0x00c
	SINT	mLevelNo;						// +0x010 -level
	BOOL	mMusic;							// +0x014 -nomusic
	BOOL	mDeveloperMode;					// +0x018 read by CActor::Move's velocity warnings; 1 turns debug logging on (CDebugLog::Reset)
	BOOL	mBuildResources;				// +0x01c read wherever resources are built (textures, polygon buckets, samples)
	BOOL	mBuildPCResources;				// +0x020
	BOOL	mBuildPS2Resources;				// +0x024
	BOOL	mBuildXBOXResources;			// +0x028
	BOOL	mNoBaseResources;				// +0x02c
	BOOL	mBuildModelInfo;				// +0x030
	BOOL	mGeforce3;						// +0x034 read by CPCPlatform::Init
	BOOL	mForceWindowed;					// +0x038 -forcewindowed
	BOOL	mAutoConfigTest;				// +0x03c -autoconfigtest
	BOOL	mUnknown040;					// +0x040
	char	mAutoConfigTestPath[256];		// +0x044 -autoconfigtest's folder; also the screen dumps' prefix (ProcessScreenDump)
	SINT	mUnknown144;					// +0x144 (the constructor leaves it)
	BOOL	mForcedShaders;					// +0x148 read by CPCPlatform::Init
	BOOL	mPureDevice;					// +0x14c
	BOOL	mEmulateDVD;					// +0x150
	BOOL	mDevKit;						// +0x154
	BOOL	mHiDetailMode;					// +0x158 read by CDXShadows::Init, CRTTree::Init
	BOOL	mDecimateMeshes;				// +0x15c
	DWORD	mTextureRAMLimit;				// +0x160
	SINT	mResWidth;						// +0x164 -res (the constructor leaves it)
	SINT	mResHeight;						// +0x168
	BOOL	mLargeRAM;						// +0x16c
	BOOL	mPal;							// +0x170
	BOOL	mNoCodeOffCD;					// +0x174
	BOOL	mResBuilderMode;				// +0x178 the console also mirrors its lines in the registry while set
	bool	mRecordDemo;					// +0x17c
	bool	mUnknown17d;					// +0x17d read by CVertexShader
	bool	mPlaybackDemo;					// +0x17e
	bool	mNiceCompression;				// +0x17f
	bool	mBuildGoodies;					// +0x180
	bool	mCardID;						// +0x181 -cardid
	bool	mFindGoodWater;					// +0x182 -findgoodwater: CDXWater::Render waits while the water pass validates
	bool	mFindBadWater;					// +0x183 -findbadwater: CDXWater::Render resets the method and waits until it validates
	bool	mAllowManualTextures;			// +0x184 (name ours): CSystem raises it around the splash textures; CDXTexture::FindTexture
											// loads unlisted textures only while it (or mBuildResources) is set
	bool	mUnknown185;					// +0x185
	bool	mTestEur;						// +0x186 -testeur; also lets -forcewindowed take effect (GetParams 00423bc0)
	BOOL	mSound;							// +0x188 -nosound
	BOOL	mUnknown18c;					// +0x18c
	BOOL	mNoMeshPartReduction;			// +0x190 read by CMesh::Load
	char	mDemoFilename[256];				// +0x194
	int		mGoStraightToDeviceSelectScreen;	// +0x294
	BOOL	mSkipFMV;						// +0x298 -skipfmv
	BOOL	mKillHUD;						// +0x29c
	int		mStressTest;					// +0x2a0
	int		mLanguage;						// +0x2a4 saved with the options
	BOOL	mShowDebugTrace;				// +0x2a8 -showdebugtrace
	BOOL	mDXTNTextures;					// +0x2ac -dxtntextures, cleared by -32bittextures
	BOOL	mTraceConsole;					// +0x2b0 -traceconsole
	BOOL	mVSync;							// +0x2b4 CVSync
	BOOL	mFlipSpeakers;					// +0x2b8 CFlipSpeakers
	BOOL	mHWSound;						// +0x2bc CHWSound
	int		mSoundDevice;					// +0x2c0 CSoundDeviceSelect
	BOOL	mUseSampleBank;					// +0x2c4 (name ours): play the language's compressed sample bank (CSoundManager,
											// CPCSoundManager)
	int		mSoundRate;						// +0x2c8 CSoundRate
	int		m3DSoundMethod;					// +0x2cc C3DSoundMethod
	int		mSoundBuffers;					// +0x2d0 -soundbuffers
	char	mSampleBankRoot[64];			// +0x2d4 (name ours): the sample bank's folder, "." by default (CSoundManager::LanguageChanged)

	// These are for the playable demo only, but I've left them in for simplicity
	BOOL	mAttractMode;					// +0x314
	int		mInactiveTimeout; // in milliseconds	+0x318 -timeout
	int		mGameplayTimeout; // in milliseconds	+0x31c
};

extern	CCLIParams CLIPARAMS;

// 0066e94e, set by -e3 (CLIParams.cpp); defined in Controller.cpp. The E3 build quits on inactivity and
// keeps only the first multiplayer levels open.
extern	bool	E3_BUILD;


#endif
