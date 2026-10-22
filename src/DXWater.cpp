// DXWater.cpp: reconstructed from DXWater.obj (0055af90..0055d5dd); Stuart's release has no copy. The water's
// textures and, on cards with pixel shader 2.0, its shaders, and the render pass.
#include "common.h"
#define DXWATER_CPP
#include "ltshell.h"
#include "tweak.h"
#include "VBufTexture.h"
#include "VertexShader.h"
#include "renderinfo.h"
#include "engine.h"
#include "Camera.h"
#include "map.h"
#include "landscape.h"
#include "game.h"
#include "Platform.h"
#include "surf.h"
#include "water.h"				// FUNCTION: 0x0055af90 init:ZERO_FVECTOR
#include "mathsconst.h"			// FUNCTION: 0x0055afb0 init:ID_FMATRIX
#include	"CLIParams.h"

CRenderMethod	WATER_METHOD("water_method", 3);		// 009cc1c0
// FUNCTION: 0x0055b080 init:WATER_METHOD
// FUNCTION: 0x0055b0a0 exit:WATER_METHOD
CRenderMethod	SUN_METHOD("sun_method", 2);			// 009cc1a8
// FUNCTION: 0x0055b0b0 init:SUN_METHOD
// FUNCTION: 0x0055b0d0 exit:SUN_METHOD

CVertexShader			*CDXWater::sShader = NULL;			// 009cc218; = NULL, before sPixelShader: retail's .bss order (placement evidence)
IDirect3DPixelShader9	*CDXWater::sPixelShader = NULL;	// 009cc21c

#define WATER_VS_TEXT \
	"vs.1.1\n" \
	"def c90, .5f, .5f, 1.f, 1.f\n" \
	"def c91, .5f, -.5f, 1.f, 0.f\n" \
	"def c89, .1f, .1f, .1f, .1f\n" \
	"\n" \
	"dcl_position v0\n" \
	"dcl_color    v5\n" \
	"dcl_texcoord0 v7\n" \
	"\n" \
	";// project\n" \
	"dp4 r2.x, v0, c[4]\n" \
	"dp4 r2.y, v0, c[5]\n" \
	"dp4 r2.z, v0, c[6]\n" \
	"dp4 r2.w, v0, c[7]\n" \
	"\n" \
	"dp4 r3.x, r2, c[8]\n" \
	"dp4 r3.y, r2, c[9]\n" \
	"dp4 r3.z, r2, c[10]\n" \
	"dp4 r3.w, r2, c[11]\n" \
	"\n" \
	"mov oPos, r3\n" \
	"\n" \
	"mov oD0, v5\n" \
	"\n" \
	"; first texture coord\n" \
	"add r1, v0, c70\n" \
	"mul r1, r1, c89\n" \
	"mov oT0, r1\n" \
	"\n" \
	"; second\n" \
	"add r1, v0, c71\n" \
	"mul r1, r1, c89\n" \
	"mov oT3, r1\n" \
	"\n" \
	"mov oT2, r3\n" \
	"\n" \
	"; surf coord\n" \
	"mul r3, v7, c90\n" \
	"add r3, r3, c72\n" \
	"mov oT1, r3\n" \
	"mul r3, v0, c74\n" \
	"add r3, r3, c75\n" \
	"mov oT4, r3\n" \
	"mov oFog, r2.z\n"

#define WATER_PS_TEXT \
	"ps_2_0\n" \
	"dcl_2d s0\n" \
	"dcl_2d s1\n" \
	"dcl_2d s2\n" \
	"dcl_2d s3\n" \
	"dcl t0\n" \
	"dcl t1\n" \
	"dcl t2\n" \
	"dcl t3\n" \
	"dcl t4\n" \
	"dcl v0\n" \
	"mov r1, t2\n" \
	"rcp r1.z, r1.w\n" \
	"mul r1, r1, c0\n" \
	"mad r1, r1, r1.z, c1\n" \
	"texld r2, t0, s0\n" \
	"texld r3, t3, s0\n" \
	"add r5, r2, -r3\n" \
	"texld r7, t1, s3\n" \
	"add r8, r7, c4\n" \
	"lrp r8, v0, r8, r5\n" \
	"mad r4, r8, c2, r1\n" \
	"texld r0, r4, s1\n" \
	"add r9, r4, c5\n" \
	"texld r9, r9, s1\n" \
	"add r0, r9, r0\n" \
	"add r10, r4, -c5\n" \
	"texld r10, r10, s1\n" \
	"add r0, r10, r0\n" \
	"add r11, r4, c6\n" \
	"texld r11, r11, s1\n" \
	"add r0, r11, r0\n" \
	"add r6, r4, -c6\n" \
	"texld r6, r6, s1\n" \
	"add r0, r6, r0\n" \
	"mul_sat r6, r2, r3\n" \
	"mul r6, r6, c9\n" \
	"texld r2, t4, s2\n" \
	"add r6, r6, r2\n" \
	"mul r0, r0, c3\n" \
	"mul r0.xyz, r0, r6\n" \
	"mad r0.xyz, r7, v0, r0\n" \
	"mul r0.a, c0.w, v0.a\n" \
	"mov oC0, r0\n"


//******************************************************************************************
CDXWater::CDXWater()	// FUNCTION: 0x0055b0e0
{
	mReflection = NULL;
	mCaustic = NULL;
	mSunBlob = NULL;
	mSunReflect = NULL;
	mVB = NULL;
	mGridColour = 0;
	LT.AddDeviceObject(this);
}
// FUNCTION: 0x0055b140 ??_GCDXWater@@UAEPAXI@Z

//******************************************************************************************
CDXWater::~CDXWater()	// FUNCTION: 0x0055b160
{
	if (sPixelShader)
		sPixelShader->Release();
	sPixelShader = NULL;
	LT.RemoveDeviceObject(this);
	if (mReflection)
	{
		mReflection->Release();
		mReflection = NULL;
	}
	if (mCaustic)
	{
		mCaustic->Release();
		mCaustic = NULL;
	}
	if (mSunBlob)
	{
		mSunBlob->Release();
		mSunBlob = NULL;
	}
	if (mSunReflect)
	{
		mSunReflect->Release();
		mSunReflect = NULL;
	}
	if (mVB)
	{
		mVB->Release();
		mVB = NULL;
	}
}

//******************************************************************************************
void CDXWater::Load(UBYTE number)	// FUNCTION: 0x0055b230
{
	char	name[24];
	sprintf(name, "mixers\\reflection%.2d.tga", number);
	mReflection = CDXTexture::FindTexture(name, 0, 0, -1, 1, 1);
	sprintf(name, "mixers\\caustic%.2d.tga", number);
	mCaustic = CDXTexture::FindTexture(name, 0, 0, -1, 1, 1);
	mVB = CVBufTexture::GetOrCreate(mReflection, 0);
	int mode = LT.m_bTextureCompressionDisabled;
	LT.m_bTextureCompressionDisabled = 1;
	mSunBlob = CDXTexture::FindTexture("sunblob.tga", 4, 0, -1, 1, 1);
	mSunReflect = CDXTexture::FindTexture("sunreflect.tga", 4, 0, 1, 1, 1);
	LT.m_bTextureCompressionDisabled = mode;
	mVB->SetVBFormat(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DUSAGE_WRITEONLY, 0x18, D3DPT_TRIANGLELIST, D3DPOOL_MANAGED);
	mVB->SetIBFormat(D3DFMT_INDEX16, D3DUSAGE_WRITEONLY, 2, D3DPOOL_MANAGED);
	mUnknown3ab4 = 0;
	InitDeviceObjects();
}

//******************************************************************************************
void CDXWater::Change(UBYTE number)	// FUNCTION: 0x0055b330
{
	if (mReflection)
	{
		mReflection->Release();
		mReflection = NULL;
	}
	if (mCaustic)
	{
		mCaustic->Release();
		mCaustic = NULL;
	}
	if (mSunBlob)
	{
		mSunBlob->Release();
		mSunBlob = NULL;
	}
	if (mSunReflect)
	{
		mSunReflect->Release();
		mSunReflect = NULL;
	}
	mGridColour = 0;
	Load(number);
}

//******************************************************************************************
HRESULT CDXWater::InitDeviceObjects()	// FUNCTION: 0x0055b3a0
{
	mGridColour++;
	if (LT.m_d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2, 0))
	{
		if (SUCCEEDED(LT.D3D_CreatePixelShader(WATER_PS_TEXT, &sPixelShader)))
			sShader = CVertexShader::Create("watershader", (DWORD *)WATER_VS_TEXT, 8, NULL, 0, -1);
	}
	return S_OK;
}

//******************************************************************************************
HRESULT CDXWater::DeleteDeviceObjects()	// FUNCTION: 0x0055b400
{
	if (sPixelShader)
		sPixelShader->Release();
	sPixelShader = NULL;
	SAFE_DELETE(sShader);
	return S_OK;
}

// The shared index builder for a grid of quads (0066ea68; 00440ad0 in its own object; names ours).
class CGridIndices { public: void Build(UWORD *out, SINT w, SINT h, SINT base); };
extern CGridIndices	GRID_INDICES;

bool		WATER_USE_SHADER;				// 009cc1a0 (DXSurf's object reads it too)

#define WATER_GRID_START	(-768.0f)			// 005e5a8c: literals, so retail keeps them with the code's constants after
#define WATER_GRID_FADE		(1.0f / 736.0f)		// 005e5a88  the vtable (the product below is grouped, so the fade's
#define WATER_GRID_SIZE		(1600.0f)			// 005e5a84  constant and the 500.0f stay two multiplies, as retail's)

//******************************************************************************************
void CDXWater::BuildGrid(float level, DWORD colour)	// FUNCTION: 0x0055b440
{
	static int	count = 0;							// 009cc220: a rebuild every 20 frames regardless (= 0 here and in Render: retail's .bss order, placement evidence)
	UWORD		indices[0xd80];
	if (colour != mGridColour || count++ == 20)
	{
		count = 0;
		TRACE("Creating water VBuf\n");
		mVB->Reset();
		struct SVert
		{
			void	SetPos(float x, float y, float z) { X = x; Y = y; Z = z; }
			float	X, Y, Z;
			DWORD	mDiffuse;
			float	mU, mV;
		} *v;
		mVB->GetVertexPtr((void **)&v, 625);
		GRID_INDICES.Build(indices, 24, 24, 0);
		mVB->AddIndices(indices, 0xd80);
		float x = WATER_GRID_START;
		float y = WATER_GRID_START;
		for (int j = 25; j; j--)
		{
			for (int i = 25; i; i--)
			{
				v->SetPos(x, y, 0);
				v->mU = x;
				v->mV = y;
				int alpha = 500 - (int)((sqrtf(x * x + y * y) * WATER_GRID_FADE) * 500.0f);
				if (alpha < 0)
					alpha = 0;
				else if (alpha > 255)
					alpha = 255;
				if (ENGINE.GetLandscape() && ENGINE.GetLandscape()->mMirrorTexture)
					v->mDiffuse = alpha << 24;
				else if (colour == 0xffffffff)
					v->mDiffuse = (((alpha << 8) + alpha << 8) + alpha << 8) + alpha;
				else
					v->mDiffuse = (alpha << 24) + colour;
				v++;
				x += 64.0f;
			}
			y += 64.0f;
			x -= WATER_GRID_SIZE;
		}
		mGridColour = colour;
		mVB->SetPersist();
	}
	FVector cam = ENGINE.GetCamera()->GetPos();
	RENDERINFO.SetWorld(FVector(cam.X, cam.Y, level), ID_FMATRIX);
	if (WATER_USE_SHADER && sShader)
		sShader->SetConstants();
}

// The landscape's shadow-pass shaders at +0x18 and +0x1c (0055b682, 0055b692). They are not declared in landscape.h:
// two more members there flip CGame::DrawDebugStuff (00470650) through the declaration-count tie-break.
struct SLandscapeShadowShaders
{
	UBYTE					mUnknown00[0x18];
	CVertexShader			*mShader;			// +0x18
	IDirect3DPixelShader9	*mPixelShader;		// +0x1c
};

//******************************************************************************************
void CDXWater::RenderShadow()	// FUNCTION: 0x0055b660
{
	BuildGrid(MAP.GetWaterLevel(), mGridColour);
	((SLandscapeShadowShaders *)ENGINE.GetLandscape())->mShader->SetConstants();
	if (SUCCEEDED(LT.GetDevice()->SetPixelShader(((SLandscapeShadowShaders *)ENGINE.GetLandscape())->mPixelShader)))
		mVB->DrawIndexed(false, 0, 0);
}

// The first viewpoint advances each pass's scroll (wrapping at 1) and angle (wrapping at 2 pi) by the render frame.
#define WATER_ADVANCE(scroll, angle)										\
	if (ENGINE.mCurrentViewpoint == 0)										\
	{																		\
		scroll += GAME.GetRenderFrameLength() * 0.003f;						\
		if (scroll > 1.0f)													\
			scroll -= 1.0f;													\
		angle += GAME.GetRenderFrameLength() * 0.05f;						\
		if (angle > PI * 2)													\
			angle -= PI * 2;												\
	}

static inline DWORD	FtoDW(FLOAT f) { return *((DWORD *)&f); }
// STAND-IN (1), see CDXWater::Render: h passed through 133 chained float copies. Each copy is consumed by the next
// and the last is returned, so it emits no code; each takes one inline-time symbol id.
static __forceinline float GlintHeight(float f)
{
	float q0 = f, q1 = q0, q2 = q1, q3 = q2, q4 = q3, q5 = q4, q6 = q5;
	float q7 = q6, q8 = q7, q9 = q8, q10 = q9, q11 = q10, q12 = q11, q13 = q12;
	float q14 = q13, q15 = q14, q16 = q15, q17 = q16, q18 = q17, q19 = q18, q20 = q19;
	float q21 = q20, q22 = q21, q23 = q22, q24 = q23, q25 = q24, q26 = q25, q27 = q26;
	float q28 = q27, q29 = q28, q30 = q29, q31 = q30, q32 = q31, q33 = q32, q34 = q33;
	float q35 = q34, q36 = q35, q37 = q36, q38 = q37, q39 = q38, q40 = q39, q41 = q40;
	float q42 = q41, q43 = q42, q44 = q43, q45 = q44, q46 = q45, q47 = q46, q48 = q47;
	float q49 = q48, q50 = q49, q51 = q50, q52 = q51, q53 = q52, q54 = q53, q55 = q54;
	float q56 = q55, q57 = q56, q58 = q57, q59 = q58, q60 = q59, q61 = q60, q62 = q61;
	float q63 = q62, q64 = q63, q65 = q64, q66 = q65, q67 = q66, q68 = q67, q69 = q68;
	float q70 = q69, q71 = q70, q72 = q71, q73 = q72, q74 = q73, q75 = q74, q76 = q75;
	float q77 = q76, q78 = q77, q79 = q78, q80 = q79, q81 = q80, q82 = q81, q83 = q82;
	float q84 = q83, q85 = q84, q86 = q85, q87 = q86, q88 = q87, q89 = q88, q90 = q89;
	float q91 = q90, q92 = q91, q93 = q92, q94 = q93, q95 = q94, q96 = q95, q97 = q96;
	float q98 = q97, q99 = q98, q100 = q99, q101 = q100, q102 = q101, q103 = q102, q104 = q103;
	float q105 = q104, q106 = q105, q107 = q106, q108 = q107, q109 = q108, q110 = q109, q111 = q110;
	float q112 = q111, q113 = q112, q114 = q113, q115 = q114, q116 = q115, q117 = q116, q118 = q117;
	float q119 = q118, q120 = q119, q121 = q120, q122 = q121, q123 = q122, q124 = q123, q125 = q124;
	float q126 = q125, q127 = q126, q128 = q127, q129 = q128, q130 = q129, q131 = q130, q132 = q131;
	return q132;
}

extern CRenderMethod	SURF_METHOD;			// 009cc030 (DXSurf.cpp)
extern CTweakSLONG		GEFORCE_FX_POWER;		// 0082b498 (PauseMenu.cpp)
extern D3DMATRIX		gTextureMatrix;			// 00628258
// The pixel shader's constants c0..c7 (005e59ec)
static const float	WATER_PS_CONSTANTS[8][4] =
{
	{ 0.5f, -0.5f, 1.0f, 1.0f },
	{ 0.5f, 0.5f, 0.0f, 0.0f },
	{ 0.1f, 0.1f, 0.0f, 0.0f },
	{ 0.4f, 0.4f, 0.4f, 1.0f },
	{ -0.5f, -0.5f, 0.0f, 0.0f },
	{ 1.0f / 128, 1.0f / 128, 0.0f, 0.0f },
	{ -1.0f / 128, 1.0f / 128, 0.0f, 0.0f },
	{ 0.2f, 0.2f, 0.2f, 0.0f },
};

// The sun's reflection: a fan of four vertices, position and two texture coordinates (D3DFVF_XYZ | D3DFVF_TEX2).
struct SSunVertex
{
	D3DXVECTOR2	pos;
	float		Z;
	D3DXVECTOR2	uv0;
	D3DXVECTOR2	uv1;
};

//******************************************************************************************
// Matched with four labelled stand-ins (below). Three methods, the best first: 2 (the pixel shader over the mirror
// texture, else three fixed-function stages), 1 (two caustic layers) and 0 (one); the surf follows when the water
// method is below 2 or the surf's below 1.
// The sun glint works in two-float vectors. Retail copies each corner into the vertex position and the first
// texture pair through one register (0055c595..0055c59d), builds the second pair through a two-slot temporary
// (0055c739..0055c761) and recomputes dir * 8 * h and side * 2 * h for every corner from copies of the scaled
// components (0055c5a1..0055c5e3): the corners are written out as four full expressions over D3DXVECTOR2, with
// only dir * 8 and side * 2 shared. The perpendicular is filled member by member, Y first (-dir.x stays on the FPU
// while side * 2 is built, 0055c51b..0055c549); the heights go in as one chained assignment, which stores them v[1],
// v[3], v[2], v[0] as retail does (0055c7ca).
// Two spellings chosen by measurement give the same instructions as their plain forms:
// - the sun direction's length and factor go through the earlier scratch float a (`a = 1.0f / sqrtf(a)`, then both
//   components times a): the factor's operand order (0055c486) then holds however the optimizer's common-
//   subexpression temporaries are numbered (`float l` with `1.0f / sqrtf(l)` per component: 9/9 below);
// - dir's length squares `centre.y - cam.Y` written again, not dir.y: that difference becomes a shared temporary
//   that dies at the square, which retail computes from the FPU copy (`fld st(0); fmulp`, 0055c4ea) while dir.y
//   keeps only its stored copy (with dir.y, its web fails the stack-order test against dir.x and the square reads
//   the stored copy: 3/3). The temporary squares before dir.x only while it is numbered 0xb0f..0xbff
//   (3 * (t << 6) mod 65536 above dir.x's 0x4b20); stand-in (1) puts it there.
// Numbers are masked/unmasked differing instructions with everything else as here; main had 45/73.
// STAND-IN (1), GlintHeight: h passes through a forced-inline helper of 133 chained float copies (each consumed by the
//   next, the last returned; no code). Retail's corner operand orders (h first at 0055c533 and 0055c699, centre.x
//   first at 0055c575 and 0055c5e7) rank the corner temporaries' X fields (numbered as the D3DXVECTOR2 operators are
//   inlined) against h and centre.x: they need those fields numbered from 0x800 up (wrapped keys below h's), where
//   the plain text starts them at 0x79a. Each copy takes one inline-time symbol id and the copies also lift the
//   common-subexpression temporaries; 108..159 copies match (the shared difference above is then numbered
//   0xb0f..0xbff), 133 is the middle. Without the helper: 31/35.
// STAND-IN (2), zero: `float zero = 0;` at the top and `up.Y += zero;`. C2 folds the sum only after its global
//   optimizer, so it stays in the scheduler's graph as a no-code copy between up.Y's load and its store, and the
//   store issues after the next call's `mov edx, [ecx]; push eax` as retail's does (0055b87a). Without it: 2/2.
// STAND-IN (3), region brackets: `(up.X * 0.02f)` in the first shader constants and every product
//   bracketed in the fixed-function texture lines after SURF.Render add no-code scheduler nodes that put two region
//   cuts where retail's fall (0055be85, 0055c110). Without the first: 2/22; without the second: 8/12.
// STAND-IN (4), the glint's TEXTURE0 sine and cosine as named sinf/cosf locals: six more no-code nodes in that
//   scheduling region, which then ends after `push gTextureMatrix` as retail's does, so SetTransform's other two
//   pushes follow the stores (0055c889) and TEXTURE1 is scheduled in one region. With (float)sin/cos in the stores:
//   10/10.
void CDXWater::Render(float wlevel)	// FUNCTION: 0x0055b6c0
{
	static float	scroll2 = 0, angle2 = 0;			// 009cc224, 009cc228
	static float	sunangle = 0;					// 009cc22c
	static float	scroll1 = 0, angle1 = 0;			// 009cc230, 009cc234
	static float	scroll0 = 0, angle0 = 0;			// 009cc238, 009cc23c
	static float	scrolls = 0, angles = 0;			// 009cc240, 009cc244

	float zero = 0;								// STAND-IN (2)
	RENDERINFO.SetLightingEnabled(false);
	FVector campos = ENGINE.GetCamera()->GetPos();
	float cx = campos.X, cy = campos.Y;

	WATER_USE_SHADER = !GAME.IsMultiplayer() && sPixelShader && sShader && ENGINE.GetLandscape() &&
		ENGINE.GetLandscape()->mMirrorTexture && !gMirrorPass && !PLATFORM.KeyOn(DIK_F1);

	if (WATER_METHOD.Try(2))
	{
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		WATER_ADVANCE(scroll2, angle2)

		gTextureMatrix._21 = 0;
		gTextureMatrix._32 = scroll2;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = 0.5f;
		gTextureMatrix._22 = 0.5f;
		gTextureMatrix._31 = 0;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE3, &gTextureMatrix);

		FVector up = ENGINE.GetCamera()->GetOrientation().GetColumnY();
		up.Y += zero;								// STAND-IN (2)
		FVector pos = ENGINE.GetCamera()->GetPos();
		if (WATER_USE_SHADER)
		{
			float start = 0.0f, end = 10.0f;
			LT.ForceRS(D3DRS_FOGDENSITY, FtoDW(MAP.GetFogDensity()));
			LT.ForceRS(D3DRS_FOGENABLE, TRUE);
			LT.ForceRS(D3DRS_FOGSTART, *(DWORD *)&start);
			LT.ForceRS(D3DRS_FOGEND, *(DWORD *)&end);
			LT.ForceRS(D3DRS_FOGCOLOR, MAP.GetFog());
			LT.SetFogMode(true);
			LT.SSS_NoCache(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
			LT.SSS_NoCache(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			LT.SSS_NoCache(3, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
			LT.SSS_NoCache(3, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		}
		else
		{
			gTextureMatrix._21 = 0;
			gTextureMatrix._12 = 0;
			gTextureMatrix._11 = 1.0f / 256;
			gTextureMatrix._22 = 1.0f / 256;
			gTextureMatrix._31 = up.X * 0.02f - pos.X * 0.0015f;
			gTextureMatrix._32 = up.Y * 0.02f - pos.Y * 0.0015f;
			LT.GetDevice()->SetTransform(D3DTS_TEXTURE2, &gTextureMatrix);
		}

		LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
		LT.STS(1, D3DTSS_TEXCOORDINDEX, 0);
		LT.STS(2, D3DTSS_TEXCOORDINDEX, 0);
		LT.STS(3, D3DTSS_TEXCOORDINDEX, 1);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(2, D3DTSS_COLOROP, D3DTOP_ADD);
		LT.STS(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		if (!(LT.m_d3dCaps.TextureOpCaps & D3DTEXOPCAPS_MULTIPLYADD))
			SURF_METHOD.SetMethod(0);
		else
		{
			LT.STS(3, D3DTSS_COLOROP, D3DTOP_MULTIPLYADD);
			LT.STS(3, D3DTSS_COLORARG0, D3DTA_CURRENT);
			LT.STS(3, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			LT.STS(3, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		}
		LT.STS(3, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		LT.STS(3, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

		gTextureMatrix._11 = 0.1f;
		gTextureMatrix._12 = 0.03f;
		gTextureMatrix._21 = 0.03f;
		gTextureMatrix._22 = -0.1f;
		gTextureMatrix._31 = (float)sin(angle2) * 0.1f;
		gTextureMatrix._32 = (float)cos(angle2) * 0.1f;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		float a = angle2 + PI;
		gTextureMatrix._11 = 0.03f;
		gTextureMatrix._12 = 0.1f;
		gTextureMatrix._21 = 0.1f;
		gTextureMatrix._22 = -0.03f;
		gTextureMatrix._31 = (float)sin(a) * 0.1f;
		gTextureMatrix._32 = (float)cos(a) * 0.1f;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE1, &gTextureMatrix);

		LT.SRS(D3DRS_TEXTUREFACTOR, MAP.GetWaterColour());
		LT.D3D_SetTexture(0, mCaustic->GetTexture());
		LT.D3D_SetTexture(2, mReflection->GetTexture());
		if (WATER_USE_SHADER)
			LT.D3D_SetTexture(1, ENGINE.GetLandscape()->mMirrorTexture);
		else
			LT.D3D_SetTexture(1, mCaustic->GetTexture());
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);

		if (WATER_USE_SHADER)
		{
			LT.GetDevice()->SetPixelShader(sPixelShader);
			float vc[6][4] =
			{
				{ (float)sin(angle2), (float)cos(angle2), 0, 0 },
				{ (float)cos(angle2) * -1.0f, (float)sin(angle2), 0, 0 },
				{ 0, scroll2, 0, 0 },
				{ -0.008f, 1.0f, 0, 0 },
				{ 1.0f / 256, 1.0f / 256, 1.0f, 1.0f },
				{ (up.X * 0.02f) - pos.X * 0.0015f, up.Y * 0.02f - pos.Y * 0.0015f, 1.0f, 1.0f },	// STAND-IN (3)
			};
			LT.GetDevice()->SetVertexShaderConstantF(70, &vc[0][0], 6);
			DWORD col = MAP.GetWaterColour();
			float pc[4];
			pc[3] = 1.0f;
			pc[0] = ((col >> 16) & 0xff) * (1.0f / 255);
			pc[1] = ((col >> 8) & 0xff) * (1.0f / 255);
			pc[2] = (col & 0xff) * (1.0f / 255);
			LT.GetDevice()->SetPixelShaderConstantF(0, &WATER_PS_CONSTANTS[0][0], 8);
			LT.GetDevice()->SetPixelShaderConstantF(9, pc, 1);
			LT.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2);
			LT.SetVertexShader(sShader);
			RENDERINFO.SetDepthBias(4);
			sShader->SetConstants();
		}
		if (!WATER_USE_SHADER && GEFORCE_FX_POWER.mValue)
		{
			LT.STS(0, D3DTSS_TEXCOORDINDEX, 1);
			LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
			LT.STS(2, D3DTSS_TEXCOORDINDEX, 1);
			LT.STS(3, D3DTSS_TEXCOORDINDEX, 0);
		}
		SURF.Render(true);
		if (!WATER_USE_SHADER && GEFORCE_FX_POWER.mValue)
		{
			LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
			LT.STS(1, D3DTSS_TEXCOORDINDEX, 0);
			LT.STS(2, D3DTSS_TEXCOORDINDEX, 0);
			LT.STS(3, D3DTSS_TEXCOORDINDEX, 1);
		}
		LT.SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);

		gTextureMatrix._11 = 0.1f;
		gTextureMatrix._12 = 0.03f;
		gTextureMatrix._21 = 0.03f;
		gTextureMatrix._22 = -0.1f;
		gTextureMatrix._31 = (((float)sin(angle2) + cx) * 0.1f) + (cy * 0.03f);	// STAND-IN (3), five lines
		gTextureMatrix._32 = ((float)cos(angle2) * 0.1f) - (cy * 0.1f) + (cx * 0.03f);
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		a = angle2 + PI;
		gTextureMatrix._11 = 0.03f;
		gTextureMatrix._12 = 0.1f;
		gTextureMatrix._21 = 0.1f;
		gTextureMatrix._22 = -0.03f;
		gTextureMatrix._31 = (((float)sin(a) + cy) * 0.1f) + (cx * 0.03f);
		gTextureMatrix._32 = (((float)cos(a) + cx) * 0.1f) - (cy * 0.03f);
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE1, &gTextureMatrix);
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = 1.0f / 256;
		gTextureMatrix._22 = 1.0f / 256;
		gTextureMatrix._31 = (up.X * 0.02f) - (pos.X * 0.0015f) + (cx * (1.0f / 256));
		gTextureMatrix._32 = up.Y * 0.02f - pos.Y * 0.0015f + cy * (1.0f / 256);
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE2, &gTextureMatrix);

		LT.STS(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.SRS(D3DRS_CULLMODE, D3DCULL_NONE);
		FVector cam = ENGINE.GetCamera()->GetPos();
		BuildGrid(wlevel, 0xffffff);
		if (CLIPARAMS.mFindBadWater)
			WATER_METHOD.mAccepted = FALSE;
		WATER_METHOD.BeginVBufCheck();
		if (WATER_USE_SHADER)
		{
			float vc[6][4] =
			{
				{ (float)sin(angle2) + cx, (float)cos(angle2) + cy, 0, 0 },
				{ cx - (float)cos(angle2), (float)sin(angle2) + cy, 0, 0 },
				{ 0, 0.1f, 0, 0 },
				{ -0.008f, 1.0f, 0, 0 },
				{ 1.0f / 256, 1.0f / 256, 1.0f, 1.0f },
				{ up.X * 0.02f - pos.X * 0.0015f + cx * (1.0f / 256), up.Y * 0.02f - pos.Y * 0.0015f + cy * (1.0f / 256), 1.0f, 1.0f },
			};
			LT.GetDevice()->SetVertexShaderConstantF(70, &vc[0][0], 6);
			mVB->DrawIndexed(false, 0, 0);
			LT.GetDevice()->SetPixelShader(NULL);
		}
		else
			mVB->DWATS(false, 0, 0);

		if (CLIPARAMS.mFindGoodWater)
		{
			if (SUCCEEDED(LT.D3D_ValidateDevice()))
				do Sleep(100); while (SUCCEEDED(LT.D3D_ValidateDevice()));
		}
		else if (CLIPARAMS.mFindBadWater)
		{
			if (FAILED(LT.D3D_ValidateDevice()))
				do Sleep(100); while (FAILED(LT.D3D_ValidateDevice()));
		}

		if (WATER_METHOD.CheckVBufValid())
		{
			RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);
			DWORD sun = MAP.GetSunReflectColour();
			if ((sun & 0xffffff) && SUN_METHOD.Try(1))
			{
				LT.STS(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
				LT.STS(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
				LT.STS(2, D3DTSS_TEXCOORDINDEX, 1);

				D3DXVECTOR2 sdir;
				sdir.x = MAP.mHFP.SunPosX;
				sunangle += 0.01f;
				sdir.y = MAP.mHFP.SunPosY;
				a = sdir.x * sdir.x + sdir.y * sdir.y;
				if (a > 0)
				{
					a = 1.0f / sqrtf(a);
					sdir.x *= a;
					sdir.y *= a;
				}
				float h = GlintHeight(cam.Z - wlevel);			// STAND-IN (1)
				D3DXVECTOR2 centre(cam.X - sdir.x * h * 6.0f, cam.Y - sdir.y * h * 6.0f);
				D3DXVECTOR2 dir(centre.x - cam.X, centre.y - cam.Y);
				float l = sqrtf(dir.x * dir.x + (centre.y - cam.Y) * (centre.y - cam.Y));
				if (l)
				{
					l = 1.0f / l;
					dir.x *= l;
					dir.y *= l;
				}
				D3DXVECTOR2 side;
				side.y = -dir.x;
				side.x = dir.y;

				SSunVertex	v[4];
				D3DXVECTOR2 p = centre + dir * 8.0f * h - side * 2.0f * h;
				v[0].pos = p;	v[0].uv0 = p;
				p = centre + dir * 8.0f * h + side * 2.0f * h;
				v[1].pos = p;	v[1].uv0 = p;
				p = centre - dir * 8.0f * h + side * 2.0f * h;
				v[2].pos = p;	v[2].uv0 = p;
				p = centre - dir * 8.0f * h - side * 2.0f * h;
				v[3].pos = p;	v[3].uv0 = p;
				v[0].uv1 = D3DXVECTOR2(0, 0);
				v[1].uv1 = D3DXVECTOR2(1, 0);
				v[2].uv1 = D3DXVECTOR2(1, 1);
				v[3].uv1 = D3DXVECTOR2(0, 1);
				v[0].Z = v[1].Z = v[2].Z = v[3].Z = wlevel;

				LT.D3D_SetTexture(0, mSunReflect->GetTexture());
				LT.D3D_SetTexture(1, mSunReflect->GetTexture());
				LT.D3D_SetTexture(2, mSunBlob->GetTexture());
				RENDERINFO.SetDepthBias(6);
				gTextureMatrix._11 = 0.1f;
				gTextureMatrix._12 = 0.03f;
				gTextureMatrix._21 = 0.03f;
				gTextureMatrix._22 = -0.1f;
				float s = sinf(angle2), c = cosf(angle2);		// STAND-IN (4)
				gTextureMatrix._31 = s * 0.1f;
				gTextureMatrix._32 = c * 0.1f;
				LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
				a = angle2 + PI;
				gTextureMatrix._11 = 0.03f;
				gTextureMatrix._12 = 0.1f;
				gTextureMatrix._21 = 0.1f;
				gTextureMatrix._22 = -0.03f;
				gTextureMatrix._31 = (float)sin(a) * 0.1f;
				gTextureMatrix._32 = (float)cos(a) * 0.1f;
				LT.GetDevice()->SetTransform(D3DTS_TEXTURE1, &gTextureMatrix);

				RENDERINFO.SetFVF(D3DFVF_XYZ | D3DFVF_TEX2);
				LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
				LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
				LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
				LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_ADD);
				LT.STS(2, D3DTSS_ALPHAOP, D3DTOP_ADD);
				LT.SetAlphaRef(0xc0);
				LT.STS(2, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
				LT.STS(2, D3DTSS_COLORARG1, D3DTA_TFACTOR);
				LT.SRS(D3DRS_TEXTUREFACTOR, sun);
				LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
				LT.SSS_NoCache(0, D3DSAMP_MAXANISOTROPY, 4);
				LT.SSS_NoCache(1, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
				LT.SSS_NoCache(1, D3DSAMP_MAXANISOTROPY, 4);
				LT.SSS_NoCache(2, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
				LT.SSS_NoCache(2, D3DSAMP_MAXANISOTROPY, 4);
				SetMipFilterByGlobalToggle(0);
				SetMipFilterByGlobalToggle(1);
				SetMipFilterByGlobalToggle(2);
				RENDERINFO.Apply();
				if (SUN_METHOD.Validate())
				{
					LT.GetDevice()->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(SSunVertex));
					SUN_METHOD.Accept();
				}
			}
			LT.SSS_NoCache(0, D3DSAMP_MAXANISOTROPY, 1);
			LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			LT.SSS_NoCache(1, D3DSAMP_MAXANISOTROPY, 1);
			LT.SSS_NoCache(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			LT.SetAlphaRef(8);
			LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
			LT.SRS(D3DRS_ALPHATESTENABLE, FALSE);
			RENDERINFO.SetDepthBias(0);
			LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
			LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
			LT.STS(2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			LT.STS(2, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			LT.STS(3, D3DTSS_COLORARG0, D3DTA_TFACTOR);
			LT.STS(3, D3DTSS_COLORARG2, D3DTA_CURRENT);
			LT.STS(3, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
			LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
			LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
			LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
			WATER_METHOD.Accept();
		}
		if (WATER_USE_SHADER)
		{
			LT.SSS_NoCache(1, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
			LT.SSS_NoCache(1, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
		}
		LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
		LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
		LT.STS(2, D3DTSS_TEXCOORDINDEX, 2);
		LT.STS(3, D3DTSS_TEXCOORDINDEX, 3);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(3, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	}

	if (WATER_METHOD.Try(1))
	{
		FVector cam = ENGINE.GetCamera()->GetPos();
		WATER_ADVANCE(scroll1, angle1)
		LT.D3D_SetTexture(0, mCaustic->GetTexture());
		LT.D3D_SetTexture(1, mCaustic->GetTexture());
		gTextureMatrix._11 = 0.1f;
		gTextureMatrix._12 = 0.03f;
		gTextureMatrix._21 = 0.03f;
		gTextureMatrix._22 = -0.1f;
		gTextureMatrix._31 = ((float)sin(angle1) + cx) * 0.1f + cy * 0.03f;
		gTextureMatrix._32 = (float)cos(angle1) * 0.1f - cy * 0.1f + cx * 0.03f;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		float a = angle1 + PI;
		gTextureMatrix._11 = 0.03f;
		gTextureMatrix._12 = 0.1f;
		gTextureMatrix._21 = 0.1f;
		gTextureMatrix._22 = -0.03f;
		gTextureMatrix._31 = ((float)sin(a) + cy) * 0.1f + cx * 0.03f;
		gTextureMatrix._32 = ((float)cos(a) + cx) * 0.1f - cy * 0.03f;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE1, &gTextureMatrix);
		LT.STS(1, D3DTSS_TEXCOORDINDEX, 0);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
		LT.SRS(D3DRS_TEXTUREFACTOR, MAP.GetWaterColour());
		BuildGrid(wlevel, 0xffffffff);
		WATER_METHOD.BeginVBufCheck();
		mVB->DWATS(false, 0, 0);
		if (WATER_METHOD.CheckVBufValid())
			WATER_METHOD.Accept();
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		gTextureMatrix._31 = 0;
		gTextureMatrix._32 = 0;
		gTextureMatrix._11 = 1.0f;
		gTextureMatrix._22 = 1.0f;
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE1, &gTextureMatrix);
		LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	}

	if (WATER_METHOD.Try(0))
	{
		FVector cam = ENGINE.GetCamera()->GetPos();
		BuildGrid(wlevel, MAP.GetWaterColour() & 0xffffff);
		WATER_ADVANCE(scroll0, angle0)
		LT.D3D_SetTexture(0, mCaustic->GetTexture());
		gTextureMatrix._11 = 0.1f;
		gTextureMatrix._12 = 0.03f;
		gTextureMatrix._21 = 0.03f;
		gTextureMatrix._22 = -0.1f;
		gTextureMatrix._31 = ((float)sin(angle0) + cx) * 0.1f + cy * 0.03f;
		gTextureMatrix._32 = (float)cos(angle0) * 0.1f - cy * 0.1f + cx * 0.03f;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
		LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		WATER_METHOD.BeginVBufCheck();
		mVB->DWATS(false, 0, 0);
		if (WATER_METHOD.CheckVBufValid())
			WATER_METHOD.Accept();
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		gTextureMatrix._31 = 0;
		gTextureMatrix._32 = 0;
		gTextureMatrix._11 = 1.0f;
		gTextureMatrix._22 = 1.0f;
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	}

	if (WATER_METHOD.GetMethod() < 2 || SURF_METHOD.GetMethod() < 1)
	{
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
		RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);
		WATER_ADVANCE(scrolls, angles)
		gTextureMatrix._21 = 0;
		gTextureMatrix._12 = 0;
		gTextureMatrix._11 = 0.5f;
		gTextureMatrix._22 = 0.5f;
		gTextureMatrix._31 = 0;
		gTextureMatrix._32 = scrolls;
		LT.GetDevice()->SetTransform(D3DTS_TEXTURE0, &gTextureMatrix);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2);
		LT.SRS(D3DRS_FOGENABLE, FALSE);
		LT.STS(0, D3DTSS_TEXCOORDINDEX, 1);
		LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
		SURF.Render(false);
		LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
		LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
		LT.SRS(D3DRS_FOGENABLE, TRUE);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
		LT.SRS(D3DRS_ZENABLE, TRUE);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	}

	RENDERINFO.SetLightingEnabled(true);
	if (WATER_USE_SHADER)
	{
		WATER_USE_SHADER = false;
		RENDERINFO.SetLightingEnabled(true);
	}
}
// STAND-IN (link layout): retail's 16408 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_DXWater[16408] = { 0 };
