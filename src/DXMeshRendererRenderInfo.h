// STAND-IN: private copy of current renderinfo.h, used only by DXMeshRenderer.cpp.
// The sole implementation addition is SetMatrix; current member names, layout and ABI are preserved.
// A free-helper/current-math port failed (38/408/7086); ownership and the math changes need separate reproduction.
// This differing class definition is a pinned-build concession, not a claim of standard ODR portability.
// renderinfo.h: the per-draw render state (only what matched files need). RENDERINFO is at 009c65c0; SetWorld is
// 00550ca0 and takes the position and orientation by value (it pops 0x40 bytes; CThing::DrawDebugCuboid 004f37c0).

#ifndef RENDERINFO_H
#define RENDERINFO_H

#include "maths.h"

// The texture-address states Stuart's code sets through RENDERINFO.STS are Direct3D 9 sampler states on the PC,
// set uncached (LT.SSS_NoCache, CFrontEnd::DrawPanel 00467010); the wrappers need ltshell.h included first.
#define TSS_ADDRESSU	D3DSAMP_ADDRESSU
#define TSS_ADDRESSV	D3DSAMP_ADDRESSV
#define TADDRESS_WRAP	D3DTADDRESS_WRAP
#define TADDRESS_CLAMP	D3DTADDRESS_CLAMP

// Stuart's render-state names for RENDERINFO.SRS (CEngine::EnableAlpha 0044a650: 0x1b, 0x13, 0x14, 0x0e)
#define RS_ALPHABLENDENABLE	D3DRS_ALPHABLENDENABLE
#define RS_SRCBLEND			D3DRS_SRCBLEND
#define RS_DESTBLEND		D3DRS_DESTBLEND
#define RS_ZENABLE			D3DRS_ZENABLE
#define RS_ZWRITEENABLE		D3DRS_ZWRITEENABLE
#define RS_LIGHTING			D3DRS_LIGHTING
#define BLEND_ONE			D3DBLEND_ONE
#define BLEND_SRCALPHA		D3DBLEND_SRCALPHA
#define BLEND_INVSRCALPHA	D3DBLEND_INVSRCALPHA
#define BLEND_ZERO			D3DBLEND_ZERO
#define RS_ZFUNC			D3DRS_ZFUNC
#define ZFUNC_LEQUAL		D3DCMP_LESSEQUAL
// texture-stage states go through the cached LT.STS, sampler states through LT.SSS_NoCache
// (CDXEngine::PostRender 0053eda2..0053edd1)
#define TSS_COLOROP		D3DTSS_COLOROP
#define TSS_COLORARG2	D3DTSS_COLORARG2
#define TOP_MODULATE	D3DTOP_MODULATE
#define TA_DIFFUSE		D3DTA_DIFFUSE
#define TSS_MINFILTER	D3DSAMP_MINFILTER
#define TSS_MAGFILTER	D3DSAMP_MAGFILTER
#define TEXF_LINEAR		D3DTEXF_LINEAR

// the PC's mip-filter setter (a cdecl function beside the texture code; name open)
void	SetMipFilterNone(DWORD stage);			// 00551480 (CDXEngine::PostRender 0053ee6b)
void	SetMipFilterByGlobalToggle(DWORD stage);	// 00551420 (CDXEngine::Render 0053e728)
void	SetMipFilterLinear(DWORD stage);			// 00551460 (CDXEngine::Render 0053e791)

// Stuart's light (0x5c bytes; his names from FEPGoodies.cpp). The constructor clears it (the inline copy in
// CFEPBEConfig::Render 0045086b..00450924: the vectors through FVector::Set); Init is 004901e0.
enum ELightType
{
	LIGHT_DIRECTIONAL = 0			// the pages' lights (CFEPBEConfig::Render 004509a6)
};

class CLight
{
public:
	CLight()
	{
		mType = LIGHT_DIRECTIONAL;
		mDir.Set(0, 0, 0);
		mDiffuseR = 0;
		mDiffuseG = 0;
		mDiffuseB = 0;
		mUnknown30 = 0;
		mPos.Set(0, 0, 0);
		mUnknown34 = 0;
		mUnknown38 = 0;
		mUnknown3c = 0;
		mUnknown40 = 0;
		mUnknown44 = 0;
		mUnknown48 = 0;
		mUnknown4c.Set(0, 0, 0);
	}

	void		Init(ELightType type, FVector dir, float r, float g, float b);	// 004901e0

	ELightType	mType;					// +0x00
	FVector		mPos;					// +0x04
	FVector		mDir;					// +0x14
	float		mDiffuseR;				// +0x24
	float		mDiffuseG;				// +0x28
	float		mDiffuseB;				// +0x2c
	float		mUnknown30;				// +0x30
	float		mUnknown34, mUnknown38, mUnknown3c, mUnknown40, mUnknown44, mUnknown48;	// +0x34
	FVector		mUnknown4c;				// +0x4c
};

// a Direct3D matrix, row by row
struct SRenderMatrix
{
	SRenderMatrix() {}		// D3DXMATRIX's empty constructor: RENDERINFO's initializer spends inline budget on the 43
							// (00550870 then calls CLight's constructor out of line, 005508a0)
	float			m[4][4];
};

struct D3DXVECTOR3;

class CRenderInfo
{
public:
	CRenderInfo() { Reset(); }

#ifdef LT_SHELL_INCLUDE
	void			STS(DWORD stage, D3DSAMPLERSTATETYPE type, DWORD val) { LT.SSS_NoCache(stage, type, val); }
	void			SRS(D3DRENDERSTATETYPE state, DWORD val) { LT.SRS(state, val); }
	void			STS(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD val) { LT.STS(stage, type, val); }
	void			SetFogColour(DWORD col) { LT.SRS(D3DRS_FOGCOLOR, col); }	// inline on the PC (CDXEngine::Render 0053e5d2)
#endif
	void			SetWorld(FVector pos, FMatrix ori);
	void			SetProjection(float nearz, float farz, float width, float height);	// 00550b10
	void			SetView(FMatrix *view, FVector *pos);								// 00550be0
	void			Apply(bool a = false, bool b = false);								// 00550d50: first flag tested as a byte (00550e8d); Draw loads second as a byte (00501019)
	void			SetVirtualScreenXSize(int x) { mVirtualScreenXSize = x; }
	void			SetVirtualScreenYSize(int y) { mVirtualScreenYSize = y; }
	void			SetVirtualScreenEnabled(bool e) { mVirtualScreenEnabled = e; }
	// CFEPSaveGame::Process clears the flag at +0x2ec (009c68ac) and sets the byte at +0x34d (009c690d, 00464a5d)
	void			SetFogEnabled(bool enabled) { mFogEnabled = enabled; mFogChanged = true; }
	void			Reset();																// 005508e0 (CDXFrontEnd::RenderStart 00540f82)
	void			SetFogDensity(float density);											// 005513d0 (CDXEngine::Render 0053e5e6)
	void			GetBiasedProjection(D3DMATRIX *m);										// 00551510: the projection, +0x38 less the depth bias
	void			ApplyLight(SINT n, BOOL ambient);										// 00551200
	void			SetUnknowne18(DWORD d);													// 005512f0
	void			Project(D3DXVECTOR3 *out, D3DXVECTOR3 *in, class CViewport *vp);		// 00551300: screen space, stretched by the screen shape
	void			SetShader(class CVertexShader *shader);									// 005513f0
	void			SetDepthBias(DWORD bias);												// 005514a0
	void			ShaderFailed();															// empty: folded into 0040c640 (Apply 00550f46)
	float			GetFogDensity() { return mFogDensity; }
#ifdef VERTEXSHADER_CPP	// the render-state shader's reads (BuildRenderInfoShader 00503ac0; its inline budget)
	BOOL			GetLightEnable(SINT n) { return mLightEnable[n]; }
	CLight			&GetLight(SINT n) { return mLights[n]; }
	BOOL			GetLightingEnabled() { return mUnknown2ed; }
	BOOL			GetBillboard() { return mBillboard; }
	DWORD			GetCustomDeclaration() { return mUnknowne18; }
	FVector			GetScreenSquash() { return mScreenSquash; }
	FVector			GetScreenShift() { return mScreenShift; }
	int				GetDiffuseBlend() { return mUnknown320; }
	DWORD			GetDiffuseBlendColour() { return mUnknown324; }
	DWORD			GetAmbient() { return mAmbient; }
	int				GetNumBones() { return mUnknown30c; }
	void			GetMatrix(SINT n, D3DMATRIX *m) { *(SRenderMatrix *)m = mMatrices[n]; }
	float			GetFogStart() { return mFogStart; }
	float			GetFogEnd() { return mFogEnd; }
	FVector			GetBillboardX() { return mBillboardX; }
	FVector			GetBillboardZ() { return mBillboardZ; }
#endif
	// STAND-IN: retain the actual owner/reference shape of the measured full matrix publication.
	void SetMatrix(SINT n, const D3DMATRIX &m) { mMatrices[n] = *(const SRenderMatrix *)&m; }
	void			GetWorld(D3DMATRIX *m) { *(SRenderMatrix *)m = mWorld; }				// Stuart's (DXEngine.cpp)
	void			GetView(D3DMATRIX *m) { *(SRenderMatrix *)m = mView; }
	void			GetProjection(D3DMATRIX *m) { *(SRenderMatrix *)m = mProjection; }
	// two more cached states CFEPDevelopment::Render sets (004584e6..0045850e; names open)
	void			SetUnknown2ed(bool b) { mUnknown2ed = b; mUnknown2edChanged = true; }
	void			SetUnknown2e8(DWORD d) { mAmbient = d; mAmbientChanged = true; }
	// Stuart's lighting calls (FEPGoodies.cpp), as the configuration page inlines them (CFEPBEConfig::Render
	// 004509b1..00450b3d): SetAmbient is the state at +0x2e8, EnableLighting the one at +0x2ed.
	void			SetAmbient(DWORD col) { mAmbient = col; mAmbientChanged = true; }
	void			EnableLighting(bool on) { mUnknown2ed = on; mUnknown2edChanged = true; }
	void			SetLightingEnabled(bool on) { mUnknown2ed = on; mUnknown2edChanged = true; }	// Stuart's name (engine.cpp)
	void			SetLight(SINT n, CLight *light) { mLights[n] = *light; mLightChanged[n] = true; }
	void			SetLightEnable(SINT n, bool on) { mLightEnable[n] = on; mLightEnableChanged[n] = true; }
	void			SetFVF(DWORD fvf) { mFVF = fvf; mFVFChanged = true; }					// Stuart's name (DXEngine.cpp)

	CLight			mLights[8];				// +0x000 (the pages set 0..2, 009c65c0/009c661c/009c6678)
	bool			mLightEnable[8];		// +0x2e0
	DWORD			mAmbient;			// +0x2e8: the ambient colour
	bool			mFogEnabled;			// +0x2ec
	bool			mUnknown2ed;			// +0x2ed
	UBYTE			mUnknown2ee[0x2f0 - 0x2ee];
	float			mFogDensity;			// +0x2f0: SetFogDensity (005513db); the shader constants read it (00502006)
	bool			mUnknown2f4, mUnknown2f5, mUnknown2f6;	// +0x2f4: each asks for a render-state shader (Apply 00550ece..)
#ifdef VERTEXSHADER_CPP	// the declaration shaders' constants (SetCustomConstants 00502bdc)
	UBYTE			mUnknown2f7[0x2f8 - 0x2f7];
	FVector			mScreenSquash;			// +0x2f8: c_screenspace_squashfactor
#else
	UBYTE			mUnknown2f7[0x308 - 0x2f7];
#endif
	bool			mUnknown308;			// +0x308
	int				mUnknown30c;			// +0x30c
	int				mSnowDensity;			// +0x310 (009c68d0): the snow density of the thing being drawn (CRTMesh, 004dc9d6..)
	int				mVirtualScreenXSize;	// +0x314 (009c68d4, CFrontEnd::Render)
	int				mVirtualScreenYSize;	// +0x318
	bool			mVirtualScreenEnabled;	// +0x31c
	UBYTE			mUnknown31d[0x320 - 0x31d];
	int				mUnknown320;			// +0x320
	DWORD			mUnknown324;			// +0x324: 0xffff0000 at first
	bool			mUnknown328;			// +0x328
#ifdef VERTEXSHADER_CPP
	FVector			mScreenShift;			// +0x32c: c_shift_factor (SetCustomConstants 00502e32)
#else
	int				mUnknown32c;			// +0x32c
	float			mUnknown330, mUnknown334;	// +0x330: 1 at first
	UBYTE			mUnknown338[0x33c - 0x338];
#endif
	bool			mLightChanged[8];		// +0x33c
	bool			mLightEnableChanged[8];	// +0x344
	bool			mAmbientChanged;		// +0x34c
	bool			mFogChanged;			// +0x34d
	bool			mUnknown34e, mUnknown34f;	// +0x34e
	bool			mUnknown2edChanged;		// +0x350
	UBYTE			mUnknown351[0x354 - 0x351];
	SRenderMatrix	mWorld;					// +0x354 (CDXEngine::Render copies it for the HUD, 0053ebf3)
	SRenderMatrix	mView;					// +0x394
	SRenderMatrix	mProjection;			// +0x3d4: SetProjection's result (00550b24)
	SRenderMatrix	mMatrices[40];			// +0x414: identities at first (Reset 00550a7a)
	DWORD			mFVF;					// +0xe14 (CVBuffer::SetStreamSource 00500324)
	DWORD			mUnknowne18;			// +0xe18 (00551300)
#ifdef VERTEXSHADER_CPP	// floats, handed to the device as their bits
	float			mFogStart, mFogEnd;		// +0xe1c: c_fogconst (SetCustomConstants 0050382f)
#else
	DWORD			mFogStart, mFogEnd;		// +0xe1c: D3DRS_FOGSTART and FOGEND (Apply 00551140)
#endif
	DWORD			mDepthBias;				// +0xe24: steps of ZBIAS_SCALER taken from the projection's +0x38
	bool			mWorldChanged, mViewChanged;	// +0xe28
	bool			mProjectionChanged;		// +0xe2a: set by SetProjection (00550b1d)
	bool			mFogStartChanged, mFogEndChanged, mFogDensityChanged, mUnknowne2e;	// +0xe2b
	BOOL			mBillboard;				// +0xe30: the shader turns quads to mBillboardX/Z (CDXTrees::Render)
	FVector			mBillboardX;			// +0xe34
	FVector			mBillboardZ;			// +0xe44
	class CVertexShader	*mRenderShader;		// +0xe54: built from these states when they need one (Apply 00550f36)
	class CVertexShader	*mShader;			// +0xe58: the caller's (SetShader 005513f0)
	bool			mFVFChanged;			// +0xe5c (CVBuffer::SetStreamSource 0050032e)
};

extern CRenderInfo RENDERINFO;

#endif
