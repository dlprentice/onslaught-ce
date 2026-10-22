// renderinfo.cpp: reconstructed from renderinfo.obj (005507f0..00551560); Stuart's release has no copy. CRenderInfo
// caches the lights, transforms, fog and vertex format and hands them to the device in Apply; RENDERINFO is at
// 009c65c0.
#include "common.h"
#include "ltshell.h"
#include "tweak.h"
#include "engine.h"
#include "VertexShader.h"
#include "renderinfo.h"
#include "viewport.h"
#include <d3dx9.h>

extern CTweakSLONG	RENDERSTATE_DISALLOW_MIPMAPPING;	// 0082b468

static FVector	RENDERINFO_ZERO(0.0f, 0.0f, 0.0f);				// 009c65b0 (only initialized)
// FUNCTION: 0x005507f0 init:RENDERINFO_ZERO

CTweakSLONG	VARIABLE_VALIDATEDEVICE("VARIABLE_VALIDATEDEVICE", 0);	// 009c7430
// FUNCTION: 0x00550810 init:VARIABLE_VALIDATEDEVICE
// FUNCTION: 0x00550830 exit:VARIABLE_VALIDATEDEVICE
CTweakFLOAT	ZBIAS_SCALER("ZBIAS_SCALER", 0.00009f);			// 009c7420: a projection step per depth bias
// FUNCTION: 0x00550840 init:ZBIAS_SCALER
// FUNCTION: 0x00550860 exit:ZBIAS_SCALER

CRenderInfo	RENDERINFO;
// FUNCTION: 0x00550870 init:RENDERINFO
// FUNCTION: 0x005508a0 ??0CLight@@QAE@XZ

//******************************************************************************************
void CRenderInfo::Reset()	// FUNCTION: 0x005508e0
{
	D3DXMATRIX	id;
	D3DXMatrixIdentity(&id);
	*(D3DXMATRIX *)&mWorld = id;
	*(D3DXMATRIX *)&mView = id;
	*(D3DXMATRIX *)&mProjection = id;
	mWorldChanged = true;
	mViewChanged = true;
	mProjectionChanged = true;
	mDepthBias = 0;
	for (int i = 0; i < 8; i++)
	{
		mLightEnable[i] = false;
		mLightEnableChanged[i] = true;
		mLightChanged[i] = false;
	}
	mAmbientChanged = true;
	mFogEnabled = true;
	mUnknown2ed = true;
	mFogStartChanged = true;
	mFogEndChanged = true;
	mFogDensityChanged = true;
	mUnknowne2e = true;
	mFogChanged = true;
	mUnknown2edChanged = true;
	mUnknown34f = true;
	mAmbient = 0xfff00fff;
	mFogStart = 0;
	*(float *)&mFogEnd = 10.0f;
	mFogDensity = 1.0f;
	mUnknown34e = false;
	mUnknown2f4 = false;
	mUnknown2f5 = false;
	mUnknown320 = 0;
	mUnknown324 = 0xffff0000;
	mSnowDensity = 0;
	mUnknown308 = false;
	mUnknown30c = 0;
	SRenderMatrix *m = mMatrices;
	*(D3DXMATRIX *)m = id;
	size_t copySize = sizeof(mMatrices) - sizeof(*m);
	memcpy(m + 1, m, copySize);
	mFVF = 0;
	mFVFChanged = false;
	mShader = NULL;
	mUnknowne18 = 0;
	mUnknown2f6 = false;
	mUnknown328 = false;
	mUnknown32c = 0;
	mUnknown330 = 1.0f;
	mUnknown334 = 1.0f;
	mBillboardX.Set(0, 0, 0);
	mBillboardZ.Set(0, 0, 0);
	mBillboard = FALSE;
	mRenderShader = NULL;
	mVirtualScreenEnabled = false;
}

//******************************************************************************************
void CRenderInfo::SetProjection(float nearz, float farz, float width, float height)	// FUNCTION: 0x00550b10
{
	mProjectionChanged = true;
	D3DXMATRIX m;
	m._11 = nearz / width;
	m._12 = m._13 = m._14 = m._21 = 0;
	m._22 = nearz / height;
	m._23 = m._24 = m._31 = m._32 = 0;
	m._33 = farz / (farz - nearz);
	m._34 = 1.0f;
	m._41 = m._42 = 0;
	m._43 = (-(nearz * farz)) / (farz - nearz);
	m._44 = 0;
	*(D3DXMATRIX *)&mProjection = m;
}

//******************************************************************************************
void CRenderInfo::SetView(FMatrix *ori, FVector *pos)	// FUNCTION: 0x00550be0
{
	D3DXMATRIX m;
	m._11 = ori->Row[0].X;	m._12 = ori->Row[0].Y;	m._13 = ori->Row[0].Z;
	m._21 = ori->Row[1].X;	m._22 = ori->Row[1].Y;	m._23 = ori->Row[1].Z;
	m._31 = ori->Row[2].X;	m._32 = ori->Row[2].Y;	m._33 = ori->Row[2].Z;
	m._34 = m._24 = m._14 = 0;
	m._41 = pos->X;			m._42 = pos->Y;			m._43 = pos->Z;
	m._44 = 1.0f;
	D3DXMATRIX view;
	D3DXMatrixInverse(&view, NULL, &m);
	mViewChanged = true;
	*(D3DXMATRIX *)&mView = view;
}

//******************************************************************************************
void CRenderInfo::SetWorld(FVector pos, FMatrix ori)	// FUNCTION: 0x00550ca0
{
	D3DXMATRIX m;
	m._11 = ori.Row[0].X;	m._12 = ori.Row[1].X;	m._13 = ori.Row[2].X;
	m._21 = ori.Row[0].Y;	m._22 = ori.Row[1].Y;	m._23 = ori.Row[2].Y;
	m._31 = ori.Row[0].Z;	m._32 = ori.Row[1].Z;	m._33 = ori.Row[2].Z;
	m._14 = m._24 = m._34 = 0;
	m._41 = pos.X;			m._42 = pos.Y;			m._43 = pos.Z;
	m._44 = 1.0f;
	mWorldChanged = true;
	*(D3DXMATRIX *)&mWorld = m;
}

//******************************************************************************************
void CRenderInfo::Apply(bool a, bool b)	// FUNCTION: 0x00550d50
{
	if (mFogChanged)
	{
		if (gMirrorPass)
			mFogEnabled = false;
		LT.SRS(D3DRS_FOGENABLE, mFogEnabled);
		mFogChanged = false;
	}
	if (mUnknown2edChanged)
	{
		LT.SRS(D3DRS_LIGHTING, mUnknown2ed);
		mUnknown2edChanged = false;
	}
	if (mRenderShader)
		LT.SetFogMode(true);
	if (mRenderShader)
	{
		mRenderShader->Release();
		mRenderShader = NULL;
	}
	if (mShader)
	{
		if (CVertexShader::mSoftware)
			LT.SetSoftwareVertexProcessing(true);
		LT.STS(0, D3DTSS_TEXCOORDINDEX, 0);
		LT.STS(1, D3DTSS_TEXCOORDINDEX, 1);
		LT.STS(2, D3DTSS_TEXCOORDINDEX, 2);
		LT.STS(3, D3DTSS_TEXCOORDINDEX, 3);
		LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
		LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
		LT.STS(2, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
		LT.STS(3, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
		LT.SetVertexShader(mShader);
		return;
	}
	if (a && CVertexShader::VertexShadersEnabled)
	{
		mRenderShader = NULL;
		bool need = false;
		a = false;
		if (CVertexShader::mForce)
			need = true;
		if (mUnknown328)
			need = true;
		if (mUnknown320)
			need = true;
		if (mUnknown2f4)
			need = true;
		if (mBillboard)
			a = need = true;
		if (mUnknown2f5)
			a = need = true;
		if (mUnknown2f6)
			need = true;
		if (mUnknown308)
			a = need = true;
		for (int i = 0; i < 8; i++)
			if (mLightEnable[i] && mLights[i].mType == 2)
				need = true;
		if (need)
		{
			mRenderShader = CVertexShader::BuildRenderInfoShader();
			if (a && !mRenderShader)
				ShaderFailed();
		}
		if (mRenderShader)
		{
			if (CVertexShader::mSoftware)
				LT.SetSoftwareVertexProcessing(true);
			LT.SetFogMode(false);
			LT.ForceTS(0, D3DTSS_TEXCOORDINDEX, 0);
			LT.ForceTS(1, D3DTSS_TEXCOORDINDEX, 1);
			LT.ForceTS(2, D3DTSS_TEXCOORDINDEX, 2);
			LT.ForceTS(3, D3DTSS_TEXCOORDINDEX, 3);
			LT.ForceTS(0, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
			LT.ForceTS(1, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
			LT.ForceTS(2, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
			LT.ForceTS(3, D3DTSS_TEXTURETRANSFORMFLAGS, 0);
			mRenderShader->SetConstants();
			LT.SetVertexShader(mRenderShader);
			return;
		}
	}
	if (CVertexShader::mSoftware)
		LT.SetSoftwareVertexProcessing(false);
	if (mWorldChanged)
	{
		LT.GetDevice()->SetTransform(D3DTS_WORLD, (D3DMATRIX *)&mWorld);
		mWorldChanged = false;
	}
	if (mViewChanged)
	{
		LT.GetDevice()->SetTransform(D3DTS_VIEW, (D3DMATRIX *)&mView);
		mViewChanged = false;
	}
	if (mProjectionChanged)
	{
		D3DXMATRIX m = *(D3DXMATRIX *)&mProjection;
		m._43 -= mDepthBias * ZBIAS_SCALER.mValue;
		LT.GetDevice()->SetTransform(D3DTS_PROJECTION, &m);
		mProjectionChanged = false;
	}
	for (int i = 0; i < 8; i++)
	{
		if (mLightChanged[i])
		{
			ApplyLight(i, FALSE);
			mLightChanged[i] = false;
		}
		if (mLightEnableChanged[i])
		{
			LT.GetDevice()->LightEnable(i, mLightEnable[i]);
			mLightEnableChanged[i] = false;
		}
	}
	if (mAmbientChanged)
	{
		LT.SRS(D3DRS_AMBIENT, mAmbient);
		mAmbientChanged = false;
	}
	if (mFogStartChanged)
	{
		LT.SRS(D3DRS_FOGSTART, mFogStart);
		mFogStartChanged = false;
	}
	if (mFogEndChanged)
	{
		LT.SRS(D3DRS_FOGEND, mFogEnd);
		mFogEndChanged = false;
	}
	if (mFogDensityChanged)
	{
		LT.SRS(D3DRS_FOGDENSITY, *(DWORD *)&mFogDensity);
		mFogDensityChanged = false;
	}
	if (mFVFChanged)
	{
		if (mFVF)
			LT.ForceFVF(mFVF);
		mFVFChanged = false;
		// STAND-IN (order; review F 2026-10-15). Read back through a reference: retail keeps this test of the flag it just cleared (005511b8 mov al,bl;
		// 005511c0 cmp al,bl), where a direct read or a local folds the lighting block away.
		const bool &changed = mFVFChanged;
		if (changed && !(PCLTShell::mCurrentFVF & D3DFVF_NORMAL))
		{
			mUnknown2ed = false;
			LT.SRS(D3DRS_LIGHTING, FALSE);
		}
	}
	mUnknown34f = false;
}

//******************************************************************************************
void CRenderInfo::ApplyLight(SINT n, BOOL ambient)	// FUNCTION: 0x00551200
{
	D3DLIGHT9	light;
	ZeroMemory(&light, sizeof(light));
	CLight		*l = &mLights[n];
	switch (l->mType)
	{
	case 0:
		light.Type = D3DLIGHT_DIRECTIONAL;
		break;
	case 1:
		light.Type = D3DLIGHT_POINT;
		break;
	case 2:
		light.Type = D3DLIGHT_DIRECTIONAL;
		break;
	default:
		light.Type = D3DLIGHT_POINT;
		break;
	}
	light.Position.x = l->mPos.X;
	light.Position.y = l->mPos.Y;
	light.Position.z = l->mPos.Z;
	light.Direction.x = l->mDir.X;
	light.Direction.y = l->mDir.Y;
	light.Direction.z = l->mDir.Z;
	light.Diffuse.r = l->mDiffuseR;
	light.Diffuse.g = l->mDiffuseG;
	light.Diffuse.b = l->mDiffuseB;
	light.Range = l->mUnknown30;
	light.Specular.r = l->mDiffuseR;
	light.Specular.g = l->mDiffuseG;
	light.Specular.b = l->mDiffuseB;
	light.Attenuation2 = 1.0f / (light.Range * light.Range * 0.05f);
	if (ambient == TRUE)
	{
		light.Ambient.r = l->mDiffuseR;
		light.Ambient.g = l->mDiffuseG;
		light.Ambient.b = l->mDiffuseB;
	}
	LT.GetDevice()->SetLight(n, &light);
}

//******************************************************************************************
void CRenderInfo::SetUnknowne18(DWORD d)	// FUNCTION: 0x005512f0
{
	mUnknowne18 = d;
}

//******************************************************************************************
void CRenderInfo::Project(D3DXVECTOR3 *out, D3DXVECTOR3 *in, CViewport *vp)	// FUNCTION: 0x00551300
{
	D3DVIEWPORT9	v;
	v.Height = vp->Height;
	v.MaxZ = vp->MaxZ;
	v.MinZ = vp->MinZ;
	v.Width = vp->Width;
	v.X = vp->X;
	v.Y = vp->Y;
	D3DXMATRIX		world = *(D3DXMATRIX *)&mWorld;
	D3DXMATRIX		view = *(D3DXMATRIX *)&mView;
	D3DXMATRIX		proj = *(D3DXMATRIX *)&mProjection;
	D3DXVec3Project(out, in, &v, &proj, &view, &world);
	float centre = vp->Height * 0.5f;
	out->y = (out->y - centre) * LT.mScreenShapeScale + centre;
}

//******************************************************************************************
void CRenderInfo::SetFogDensity(float density)	// FUNCTION: 0x005513d0
{
	mFogDensityChanged = true;
	mFogDensity = density;
}

//******************************************************************************************
void CRenderInfo::SetShader(CVertexShader *shader)	// FUNCTION: 0x005513f0
{
	mShader = shader;
	if (!shader)
	{
		LT.ForceFVF(mFVF);
		LT.SetFogMode(true);
	}
}

//******************************************************************************************
void SetMipFilterByGlobalToggle(DWORD stage)	// FUNCTION: 0x00551420
{
	if (RENDERSTATE_DISALLOW_MIPMAPPING.mValue)
		LT.SSS(stage, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
	else
		LT.SSS(stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
}

//******************************************************************************************
void SetMipFilterLinear(DWORD stage)	// FUNCTION: 0x00551460
{
	LT.SSS(stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
}

//******************************************************************************************
void SetMipFilterNone(DWORD stage)	// FUNCTION: 0x00551480
{
	LT.SSS(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
}

//******************************************************************************************
void CRenderInfo::SetDepthBias(DWORD bias)	// FUNCTION: 0x005514a0
{
	if (bias != mDepthBias)
	{
		mDepthBias = bias;
		D3DXMATRIX m = *(D3DXMATRIX *)&mProjection;
		m._43 -= ZBIAS_SCALER.Get() * bias;
		LT.GetDevice()->SetTransform(D3DTS_PROJECTION, &m);
		mProjectionChanged = false;
	}
}

//******************************************************************************************
void CRenderInfo::GetBiasedProjection(D3DMATRIX *out)	// FUNCTION: 0x00551510
{
	D3DXMATRIX m = *(D3DXMATRIX *)&mProjection;
	*(D3DXMATRIX *)out = m;
	out->_43 -= mDepthBias * ZBIAS_SCALER.mValue;
}

//******************************************************************************************
// A failed shader needs nothing on the PC (Apply 00550f46); the empty body is folded into the shared one (/OPT:ICF).

#pragma auto_inline(off)	// retail keeps Apply's call
// FUNCTION: 0x0040c640
void CRenderInfo::ShaderFailed()
{
}
#pragma auto_inline(on)
