// DXSnow.cpp: reconstructed from DXSnow.obj (00554e60..00555aef); Stuart's release has no copy. The snow: an
// atmospherics effect with its own vertex shader ("SnowShader"), texture and vertex buffer. DISABLE_SNOW turns it off
// when the effect is made.

#include "common.h"
#define ATMOSPHERICS_FULL
#include "mathsconst.h"
#include "DX.h"
#include "atmospherics.h"
#include "DXSnow.h"
#include "tweak.h"
#include "Texture.h"
#include "VBufTexture.h"
#include "VertexShader.h"
#include "console.h"
#include "engine.h"
#include "Camera.h"
#include "game.h"
#include "renderinfo.h"
#include "ltshell.h"

// FUNCTION: 0x00554e60 init:ZERO_FVECTOR
// FUNCTION: 0x00554e80 init:ID_FMATRIX

CTweakSLONG	DISABLE_SNOW("DISABLE_SNOW", 0);		// 009c7f78
// FUNCTION: 0x00554f50 init:DISABLE_SNOW
// FUNCTION: 0x00554f70 exit:DISABLE_SNOW

// a flake: four of the same point, each with its own start time
struct SSnowVertex
{
	float	mX, mY, mZ;
	float	mTime;
	DWORD	mColour;
};

static const char	sSnowShaderSource[] =		// 005e5148
	"; Snow shader\n"
	"\n"
	"; Inputs :\n"
	";\n"
	"; V0\tPosition\n"
	"; V5\tDiffuse\n"
	"; V2\tStart time\n"
	";\n"
	"; C0-C3\tWorld/View Transformation matrix (no position component)\n"
	"; C8\tCamera position\n"
	"; C10.x\tCurrent time\n"
	"; C11\tSnow delta\n"
	"; C12.x\tPeriod\n"
	"; C12.y 1/Period\n"
	"; C15.x\tFade distance\n"
	"; C15.y Cell size\n"
	"; C15.z 1/Cell size\n"
	"; C15.w Cell size / 2\n"
	"; C16-C19 Projection matrix\n"
	"; C93.x\tNumber of random positions\n"
	"; C93.y 1/Number of random positions\n"
	"; C93.z Start of random number block\n"
	"; C93.w Scale factor for point size\n"
	"; C94\t0,0,0,0\n"
	"; C95\t1,1,1,1\n"
	"\n"
	"vs.1.1\n"
	"dcl_position v0\n"
	"dcl_color v5\n"
	"dcl_blendweight0 v2\n"
	"\n"
	"; Break up the current time into segment/offset chunks\n"
	"\n"
	"add r0.x,-v2.x,c10.x\n"
	"mul r2.w,r0.x,c12.y\n"
	"; R2 now contains time/period\n"
	"expp r3,r2\n"
	"; R3.y=(time/period)-floor(time/period) ie temporal offset\n"
	"add r1.x,-r3.y,r0.x\n"
	"mul r8.y,r1.x,c12.y\n"
	"mov r8.x,r3.y\n"
	"; R8.x=Temporal offset\n"
	"; R8.y=Temporal segment\n"
	"\n"
	"; Get an offset into the random number array using the temporal segment\n"
	"\n"
	"mul r0.w,r8.y,c93.y\n"
	"expp r1,r0\n"
	"mul r0.x,r1.y,c93.x\n"
	"; R0.x=(segment % numrand)\n"
	"add r1.x,r0.x,c93.z\n"
	"mov a0.x,r1.x\n"
	"\n"
	"; Now grab a position from the array, and add the offset\n"
	"\n"
	"mov r0,c[a0.x+0]\n"
	"add r4,r0,v0\n"
	"\n"
	"; Now get our current position\n"
	"\n"
	";mul r1,c11,r8.xxxx\n"
	";add r5,r4,r1\n"
	"mad r5,c11,r8.xxxx,r4\n"
	"\n"
	"; Adjust by camera position\n"
	"\n"
	"add r5.xyz,r5.xyz,-c8.xyz\n"
	"\n"
	"; Mod position to bring it into acceptable range\n"
	"\n"
	"mul r1.xyz,r5.xyz,c15.zzzz\n"
	"expp r2,r1.xxxx\n"
	"mov r3.x,r2.y\n"
	"expp r2,r1.yyyy\n"
	"mov r3.y,r2.y\n"
	"expp r2,r1.zzzz\n"
	"mov r3.z,r2.y\n"
	"mul r5.xyz,r3.xyz,c15.yyyy\n"
	"\n"
	"; Subtract range/2 to get it centred around 0\n"
	"\n"
	"add r5.xyz,r5.xyz,-c15.wwww\n"
	"\n"
	"; Transform by current world matrix\n"
	"\n"
	"dp4 r0.x, r5, c0\n"
	"dp4 r0.y, r5, c1\n"
	"dp4 r0.z, r5, c2\n"
	"dp4 r0.w, r5, c3\n"
	"\n"
	"; Project position\n"
	"\n"
	"mov r1,r0\n"
	"dp4 r0.x, r1, c16\n"
	"dp4 r0.y, r1, c17\n"
	"dp4 r0.z, r1, c18\n"
	"dp4 r0.w, r1, c19\n"
	"\n"
	"; Get the camera distance\n"
	"\n"
	"rcp r8.x,r0.wwww\n"
	"mul r8.x,r8.x,c93.w\n"
	"\n"
	"; Write position\n"
	"\n"
	"mov oPos,r0\n"
	"\n"
	"; Generate particle size\n"
	"\n"
	"mov oPts,r8.x\n"
	"\n"
	"; Clamp camera distance\n"
	"\n"
	"max r8.x,r8.xxxx,c94\n"
	"min r8.x,r8.xxxx,c95\n"
	"\n"
	"; Modulate alpha based on camera distance\n"
	"\n"
	"mov r1,v5\n"
	"mul r1.w,v5.w,r8.x \n"
	"mov oD0,r1\n"
	"\n"
	"// No fog\n"
	"\n"
	"mov oFog, c94.x\n"
	"\n";

static UBYTE	sSnowShaderCode[670];			// 009c7cd8: the assembled shader

//******************************************************************************************
CSnow::CSnow()	// FUNCTION: 0x00554f80
{
	D3DXMatrixIdentity(&mMatrix);
	mLastCameraPosition.X = 0;
	mLastCameraPosition.Y = 0;
	mLastCameraPosition.Z = 0;
	mPeriod = 8.0f;
	mFadeDistance = 2.0f;
	mScale = 10.0f;
	mEnabled = (DISABLE_SNOW.mValue == 0);
}

// FUNCTION: 0x00555010 ?GetName@CSnow@@UAEPADXZ

//******************************************************************************************
// the texture, the shader, a thousand flakes and fifty places to put them
void CSnow::Process()	// FUNCTION: 0x0040c640
{
}

void CSnow::Init()	// FUNCTION: 0x00555020
{
	mTexture = NULL;
	mVBuf = NULL;
	mShader = NULL;

	if (CVertexShader::VertexShadersEnabled)
	{
		mTexture = CDXTexture::GetTextureByName("Atmospherics\\Snow\\Snow.tga");

		LPD3DXBUFFER	shader = NULL;
		LPD3DXBUFFER	errors = NULL;
		if (FAILED(D3DXAssembleShader(sSnowShaderSource, strlen(sSnowShaderSource), NULL, NULL, 0, &shader, &errors)))
		{
			TRACE("%s\n", errors->GetBufferPointer());
			CONSOLE.FatalError(false, 0xd2, 500);
		}
		DWORD	size = shader->GetBufferSize();
		CONSOLE.FatalError(size < sizeof(sSnowShaderCode), 0xd2, 1003);
		memcpy(sSnowShaderCode, shader->GetBufferPointer(), size);
		mShader = CVertexShader::Create("SnowShader", (DWORD *)sSnowShaderCode, 3, NULL, 0, 0);
		SAFE_RELEASE(shader);
		SAFE_RELEASE(errors);

#line 243
		mVBuf = new(MEMTYPE_VBUFTEXTURE) CVBufTexture(NULL);
		DWORD	usage = D3DUSAGE_WRITEONLY;
		if (CVertexShader::mSoftware)
			usage = D3DUSAGE_WRITEONLY | D3DUSAGE_SOFTWAREPROCESSING;
		mVBuf->SetVBFormat(0, usage | D3DUSAGE_POINTS, sizeof(SSnowVertex), D3DPT_POINTLIST, D3DPOOL_MANAGED);
		mVBuf->SetIBFormat(D3DFMT_INDEX16, usage, 2, D3DPOOL_MANAGED);
		mVBuf->SetPersist();

		CONSOLE.RegisterVariable("cg_snow_period", "Snow period (ie time a flake falls before looping)", CVar_float, &mPeriod);
		CONSOLE.RegisterVariable("cg_snow_scale", "Snow scale (ie flake size)", CVar_float, &mScale);
		CONSOLE.RegisterVariable("cg_snow_fadedistance", "Snow flake fade distance", CVar_float, &mFadeDistance);
		CONSOLE.RegisterVariable("cg_snow", "Snow enable/disable", CVar_bool, &mEnabled);

		for (SINT i = 0; i < 1000; i++)
		{
			float	x = (float)(rand() * 5.f) / RAND_MAX;
			float	y = (float)(rand() * 5.f) / RAND_MAX;
			float	z = (float)(rand() * 5.f) / RAND_MAX;
			float	t = (float)(rand() * 200.f) / -RAND_MAX;

			SSnowVertex	v[4];
			v[0].mX = x;
			v[0].mY = y;
			v[0].mZ = z;
			v[0].mTime = t;
			v[0].mColour = 0x80808080;
			v[1].mX = x;
			v[1].mY = y;
			v[1].mZ = z;
			v[1].mTime = t;
			v[1].mColour = 0x80808080;
			v[2].mX = x;
			v[2].mY = y;
			v[2].mZ = z;
			v[2].mTime = t;
			v[2].mColour = 0x80808080;
			v[3].mX = x;
			v[3].mY = y;
			v[3].mZ = z;
			v[3].mTime = t;
			v[3].mColour = 0x80808080;
			WORD	base = mVBuf->AddVertices(v, 4);
			WORD	indices[6];
			indices[0] = base;
			indices[1] = base + 1;
			indices[2] = base + 2;
			indices[3] = base + 2;
			indices[4] = base + 3;
			indices[5] = base;
			mVBuf->AddIndices(indices, 6);
		}

		for (SINT n = 0; n < 50; n++)
		{
			mOffsets[n].x = (float)(rand() * 15.f) / RAND_MAX - 7.5f;
			mOffsets[n].y = (float)(rand() * 15.f) / RAND_MAX - 7.5f;
			mOffsets[n].z = -7.5f;
			mOffsets[n].w = 0;
		}

		mVBuf->UnlockVB();
		mVBuf->UnlockIB();
	}
}

//******************************************************************************************
void CSnow::Shutdown()	// FUNCTION: 0x00555410
{
	if (mTexture)
	{
		mTexture->CTexture::Release();
		mTexture = NULL;
	}
	SAFE_DELETE(mVBuf);
	if (mShader)
	{
		mShader->Release();
		mShader = NULL;
	}
}

//******************************************************************************************
// the transforms and the camera for the shader, then as many flakes as the density asks for
void CSnow::RenderFlakes()	// FUNCTION: 0x00555460
{
	D3DXMATRIX	proj, world, view, world_view;
	RENDERINFO.GetProjection(&proj);
	RENDERINFO.GetWorld(&world);
	RENDERINFO.GetView(&view);
	view._41 = 0;
	view._42 = 0;
	view._43 = 0;
	D3DXMatrixMultiply(&world_view, &world, &view);
	D3DXMatrixTranspose(&world_view, &world_view);
	LT.GetDevice()->SetVertexShaderConstantF(0, (float *)&world_view, 4);
	D3DXMatrixTranspose(&proj, &proj);
	LT.GetDevice()->SetVertexShaderConstantF(16, (float *)&proj, 4);

	FVector		pos = ENGINE.GetCamera()->GetPos();
	D3DXVECTOR4	c;
	c.x = pos.X;
	c.y = pos.Y;
	c.z = pos.Z;
	c.w = 0;
	LT.GetDevice()->SetVertexShaderConstantF(8, (float *)&c, 1);
	c.x = pos.X - mLastCameraPosition.X;
	c.y = pos.Y - mLastCameraPosition.Y;
	c.z = pos.Z - mLastCameraPosition.Z;
	c.w = 0;
	LT.GetDevice()->SetVertexShaderConstantF(9, (float *)&c, 1);

	SINT	count = SINT(CAtmospherics::mSnowDensity * 1000.f);
	if (count > 1000)
		count = 1000;
	else if (count < 0)
		count = 0;
	mVBuf->DrawNonIndexed(false, count);
}

//******************************************************************************************
// STAND-IN (spelling, no code): the bracketed time is one more IL tuple: it moves a scheduling region edge (README), so the
// fall vector's X is stored before its Z and Y are loaded (0055589d..005558a6), as in retail. Without the bracket the
// function is 9 bytes off.
void CSnow::Render()	// FUNCTION: 0x00555600
{
	if ((gMirrorPass) || (!mEnabled) || (!CVertexShader::VertexShadersEnabled) || (CAtmospherics::mSnowDensity == 0) || (!mShader))
		return;

	LT.SRS(D3DRS_CULLMODE, D3DCULL_NONE);
	RENDERINFO.EnableLighting(false);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	LT.SRS(D3DRS_POINTSPRITEENABLE, TRUE);
	LT.SRS(D3DRS_POINTSCALEENABLE, TRUE);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_ONE);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_ONE);
	LT.ForceTS(1, D3DTSS_TEXCOORDINDEX, 1);
	LT.ForceTS(2, D3DTSS_TEXCOORDINDEX, 2);
	LT.ForceTS(3, D3DTSS_TEXCOORDINDEX, 3);
	LT.STS(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.STS(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);

	FVector	wind = CAtmospherics::mWindVector;
	mFallVector = wind;
	mFallVector.Z = 15.f;

	FVector	pos = ENGINE.GetCamera()->GetPos();
	FMatrix	ori = ENGINE.GetCamera()->GetOrientation();

	D3DXVECTOR4	zero(0, 0, 0, 0);
	D3DXVECTOR4	one(1, 1, 1, 1);
	LT.GetDevice()->SetVertexShaderConstantF(94, (float *)&zero, 1);
	LT.GetDevice()->SetVertexShaderConstantF(95, (float *)&one, 1);

	D3DXVECTOR4	c;
	c.x = (GAME.GetFrameRenderFraction() / 20.0f + ENGINE.GetRenderTime());	// STAND-IN (spelling, no code), see above
	LT.GetDevice()->SetVertexShaderConstantF(10, (float *)&c, 1);
	c.x = mFallVector.X;
	c.y = mFallVector.Y;
	c.z = mFallVector.Z;
	c.w = 0;
	LT.GetDevice()->SetVertexShaderConstantF(11, (float *)&c, 1);
	c.x = mPeriod;
	c.y = 1.f / mPeriod;
	LT.GetDevice()->SetVertexShaderConstantF(12, (float *)&c, 1);
	c.x = mFadeDistance;
	c.y = 15.f;
	c.z = 1.f / 15.f;
	c.w = 15.f / 2;
	LT.GetDevice()->SetVertexShaderConstantF(15, (float *)&c, 1);
	for (SINT i = 0; i < 50; i++)
		LT.GetDevice()->SetVertexShaderConstantF(20 + i, (float *)&mOffsets[i], 1);
	c.x = 50.f;
	c.y = 1.f / 50.f;
	c.z = 20.f;
	c.w = mScale;
	LT.GetDevice()->SetVertexShaderConstantF(93, (float *)&c, 1);

	LT.D3D_SetTexture(0, mTexture->GetTexture());
	RENDERINFO.SetShader(mShader);
	RENDERINFO.SetWorld(FVector(0, 0, 0), ID_FMATRIX);
	RenderFlakes();
	RENDERINFO.SetShader(NULL);
	LT.SetSoftwareVertexProcessing(false);

	RENDERINFO.GetView(&mMatrix);
	mLastCameraPosition = pos;

	LT.SetModulate(0);
	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_POINTSCALEENABLE, FALSE);
	LT.SRS(D3DRS_POINTSPRITEENABLE, FALSE);
	RENDERINFO.Apply();
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	RENDERINFO.EnableLighting(true);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x0040c640 ?Process@CSnow@@UAEXXZ
