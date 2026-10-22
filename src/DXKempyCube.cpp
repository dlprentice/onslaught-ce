// DXKempyCube.cpp: reconstructed from DXKempyCube.obj (00544040..005443ff); Stuart's release has no copy. The sky
// cube: five textured faces (no floor) drawn around the camera without depth, fog or lighting. Render's identity matrix
// is DXLandscape.obj's (008aa8d8): this object has no maths constants or initializers of its own (mathsconst.h).
#include "common.h"
#define DXKEMPYCUBE_CPP
#include "ltshell.h"
#include "renderinfo.h"
#include "engine.h"
#include "Camera.h"
#include "vbuffer.h"
#include "kempycube.h"
#include "mathsconst.h"
#include "state.h"


void	GetCubeTextureName(char *out, UBYTE number, SINT face);		// 0048de30: "cubes\cube%.2d_%s.tga"


#define KC_S	7544.89258f			// the cube's half size
#define KC_TOP	3772.44629f			// the top of the walls
#define KC_U0	0.001953125f		// half a texel in from each edge
#define KC_U1	0.998046875f
#define KC_V1	0.749023438f

// The five faces as triangle strips of XYZ|TEX1 vertices (006508f0).
static float	sVerts[20][5] =
{
	{ -KC_S, -KC_S, -KC_S, KC_U0, KC_U0 }, {  KC_S, -KC_S, -KC_S, KC_U1, KC_U0 },
	{ -KC_S,  KC_S, -KC_S, KC_U0, KC_U1 }, {  KC_S,  KC_S, -KC_S, KC_U1, KC_U1 },
	{  KC_S,  KC_S, -KC_S, KC_U1, KC_U0 }, {  KC_S,  KC_S, KC_TOP, KC_U1, KC_V1 },
	{ -KC_S,  KC_S, -KC_S, KC_U0, KC_U0 }, { -KC_S,  KC_S, KC_TOP, KC_U0, KC_V1 },
	{  KC_S, -KC_S, -KC_S, KC_U1, KC_U0 }, {  KC_S, -KC_S, KC_TOP, KC_U1, KC_V1 },
	{  KC_S,  KC_S, -KC_S, KC_U0, KC_U0 }, {  KC_S,  KC_S, KC_TOP, KC_U0, KC_V1 },
	{ -KC_S, -KC_S, -KC_S, KC_U1, KC_U0 }, { -KC_S, -KC_S, KC_TOP, KC_U1, KC_V1 },
	{  KC_S, -KC_S, -KC_S, KC_U0, KC_U0 }, {  KC_S, -KC_S, KC_TOP, KC_U0, KC_V1 },
	{ -KC_S,  KC_S, -KC_S, KC_U1, KC_U0 }, { -KC_S,  KC_S, KC_TOP, KC_U1, KC_V1 },
	{ -KC_S, -KC_S, -KC_S, KC_U0, KC_U0 }, { -KC_S, -KC_S, KC_TOP, KC_U0, KC_V1 },
};

CVBuffer	*CDXKempyCube::sVB;

//******************************************************************************************
CDXKempyCube::CDXKempyCube()	// FUNCTION: 0x00544040
{
	for (int i = 0; i < 5; i++)
		mTextures[i] = NULL;
}

//******************************************************************************************
CDXKempyCube::~CDXKempyCube()	// FUNCTION: 0x00544060
{
	for (int i = 0; i < 5; i++)
	{
		if (mTextures[i])
		{
			mTextures[i]->Release();
			mTextures[i] = NULL;
		}
	}
	SAFE_DELETE(sVB);
}

//******************************************************************************************
void CDXKempyCube::Load(UBYTE number)	// FUNCTION: 0x005440a0
{
	for (int i = 0; i < 5; i++)
	{
		GetCubeTextureName(mNames[i], number, i);
		mTextures[i] = CDXTexture::FindTexture(mNames[i], 0, 0, 1, 1, 1);
	}
	SAFE_DELETE(sVB);
#line 82
	sVB = new(MEMTYPE_VBUFFER) CVBuffer;
	sVB->Create(20, 20, D3DFVF_XYZ | D3DFVF_TEX1);
	void *data;
	if (SUCCEEDED(sVB->Lock(&data)))
	{
		memcpy(data, sVerts, sizeof(sVerts));
		sVB->Unlock();
	}
}

//******************************************************************************************
void CDXKempyCube::Change(UBYTE number)	// FUNCTION: 0x005441a0
{
	Load(number);
}

//******************************************************************************************
void CDXKempyCube::Render()	// FUNCTION: 0x005441b0
{
	LT.SRS(D3DRS_ZWRITEENABLE, FALSE);
	LT.SRS(D3DRS_ZENABLE, FALSE);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	RENDERINFO.SetFogEnabled(false);
	RENDERINFO.SetLightingEnabled(false);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	RENDERINFO.SetWorld(ENGINE.GetCamera()->GetPos(), ID_FMATRIX);
	LT.GetDevice()->SetStreamSource(0, sVB->mVB, 0, 20);
	RENDERINFO.SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);
	RENDERINFO.Apply();
	LT.ForceRS(D3DRS_CLIPPLANEENABLE, 0);
	for (int i = 0; i < 5; i++)
	{
		LT.D3D_SetTexture(0, mTextures[i]->GetTexture());
		LT.GetDevice()->DrawPrimitive(D3DPT_TRIANGLESTRIP, i * 4, 2);
	}
	STATE.UpdateClipPlane(false);
	LT.SRS(D3DRS_ZWRITEENABLE, TRUE);
	LT.SRS(D3DRS_ZENABLE, TRUE);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
	LT.SSS_NoCache(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
	RENDERINFO.SetLightingEnabled(true);
	RENDERINFO.SetFogEnabled(true);
}
