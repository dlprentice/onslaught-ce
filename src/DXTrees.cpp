// DXTrees.cpp: reconstructed from DXTrees.obj (0055a230..0055af8f); Stuart's release has no copy. The landscape's
// trees, drawn from two vertex buffers that the shell's device-object list rebuilds.

#include "common.h"
#define TREES_FULL
#include "mathsconst.h"
#include "DX.h"
#include "Trees.h"
#include "ltshell.h"
#include "VBufTexture.h"
#include "vbuffer.h"
#define IMPOSTER_FULL
#include "imposter.h"
#include "renderinfo.h"
#include "engine.h"
#include "map.h"
#include "Camera.h"
#include "tweak.h"
#include "mapwho.h"
#include "thing.h"
#include "rttree.h"
#include "VertexShader.h"
#include <math.h>

extern CTweakSLONG	USE_MODULATE_2X;			// 008554f0 (ltshell.cpp)
extern FVector		sColumnZ;					// 008aa850 (DXImposter.cpp): the billboards' axes
extern FVector		sColumnX;					// 008aa860

#define TREES_NEAR_GROUND	20.f				// the second buffer's minimum camera height (name ours)
#define TREES_Z_OFFSET		0.0					// the top quad's lift above the tree, tuned to nothing (see CDXTrees::Build; name ours)

// FUNCTION: 0x0055a230 init:ZERO_FVECTOR
// FUNCTION: 0x0055a250 init:ID_FMATRIX

CDXTrees	TREES;								// 009cc148
// FUNCTION: 0x0055a320 init:TREES
// FUNCTION: 0x0055a340 exit:TREES
// FUNCTION: 0x0055a350 ??0CDXTrees@@QAE@XZ
// FUNCTION: 0x0055a360 ??_GCDXTrees@@UAEPAXI@Z
// FUNCTION: 0x0055a380 ??1CDXTrees@@UAE@XZ

//******************************************************************************************
void CDXTrees::Init()	// FUNCTION: 0x0055a390
{
	LT.AddDeviceObject(this);
	mVBuf = NULL;
	mUnknownVBuf = NULL;
}

//******************************************************************************************
HRESULT CDXTrees::InitDeviceObjects()	// FUNCTION: 0x005019c0
{
	return S_OK;
}

//******************************************************************************************
HRESULT CDXTrees::DeleteDeviceObjects()	// FUNCTION: 0x0055a3b0
{
	SAFE_DELETE(mVBuf);
	SAFE_DELETE(mUnknownVBuf);
	return S_OK;
}

//******************************************************************************************
void CDXTrees::Shutdown()	// FUNCTION: 0x0055a400
{
	DeleteDeviceObjects();
	LT.RemoveDeviceObject(this);
}

//******************************************************************************************
// Matches with the y-first declaration fit below; the top quad's TREES_Z_OFFSET is read as natural (below). The calls, loops and register
// roles follow retail. The summed position is added in the GetRenderPos temporary and then three dwords (no pad) are copied into
// independent scalars (0055a632..0055a664: x at +0x10, y at +0x14 and z at +0x20, with the width at +0x18 and the tree at +0x1c
// between them), so they are three floats here. The first quad reads the frame as imp->mFrames[view].field, a chain that stays a
// load per use: through `f` the negated width and height are computed once and stored twice, where retail reloads each corner's
// width and height (0055a6ba..; `f` is still the test and the second quad's base). Each vertex is written position, texture
// coordinates, diffuse, normal, which is the order of retail's stores (0055a686..); the other 23 orders of the four groups score
// worse. The second quad's three groups go position, texture, diffuse the same way. The frame is (u0, u1, v0, v1) in memory
// (CDXImposter::Render passes u0, v0, u1, v1): the corners take (u0, v0), (u1, v0), (u0, v1), (u1, v1) in the first quad and (u0,
// v0), (u1, v0), (u1, v1), (u0, v1) in the top quad (0055a69e.., 0055a880..). The sector's node goes through a local, as in
// HLCollisionDetector: with it the MAP_WHO this-pointer is loaded ahead of the sector address (0055a5e6) as in retail.
// The top quad's Z is `(float)(cz + TREES_Z_OFFSET)` on all four corners, the offset a double zero: a tuning define left at
// nothing, as Stuart keeps them (DEFAULT_CAMERA_OFFSET 0.0f, engine.h:13, unused in his release), written as a double as his
// NEXT_FRAME -1.0 (eventmanager.h:14) and the double 0.0 he assigns in float code (BattleEngine.cpp:1107 `mShakeR = 0.0;`,
// Camera.cpp:496), with an explicit (float) on a double expression as Music.cpp:563's `(float)tan((1.0-vol)*1.38f)`. The name is
// ours and the value is fixed by retail: nothing is added. Evidence: retail loads cz through the FPU once
// (`fld [cz]; fst [Tz]; fstp v0.Z`, 0055a86c..0055a874) and the other three corners copy the temp (three integer reloads of Tz,
// 0055a882, 0055a8ca, 0055a906): one CSE'd 4-byte temp that takes thing's slot after thing's last use and keeps w out of it, which
// also moves the whole frame by 4 (0xdc). A double identity inside an explicit (float) cast is what leaves a float-expression leaf
// for the CSE pass to share; float constants, a missing cast, one or two corners, `-(-cz)` (two fchs stay) and named copies of cz
// (copy-propagated away) do not reproduce it.
// The layers count down from 4 and the cell count comes from the layer: the compiler then steps two induction variables as retail
// does (`inc` of the detail level against 4 and `dec` of the layer in esi, 0055a9cd..0055a9e2). A second counter in the for-header
// (`l++, layer--`) puts the countdown in edx.
// The tree's body is inside `if (thing->IsA(...))`: `centre` is then a local struct whose block starts after `thing`. With a
// `continue` it shares the block, every later load through the pointer local `thing` waits for the stores of `centre += origin`
// (VC6 puts a memory edge there), and the vtable load of the GetRenderImposterNo call issues after the sum, where retail issues it
// in the idle slot after the first fadd (0055a638).
// FIT: `SINT y = 0, x = 0;` declares the two counters ahead of the loops, y first, and the loops start them
// again. An initialised declaration is ordinary C style and the values never matter; the ORDER is what is chosen. VC6 numbers a
// variable's live ranges by the order of their first definition (pass 0 of the allocator: layer 47, x 56, y 61, n 67), and the
// reloads of n, x and y that share the e-loop's exit edge block come out in descending order of that number: retail reloads n, x,
// y (0055a9a7, 0055a9ab, 0055a9af), ours without the fit n, y, x. With y defined first the order is retail's and the whole
// function matches. Nothing else moves it: `SINT x = 0, y = 0;` (x first), a bare `SINT y;` or `SINT y, x;`, `x` declared ahead of
// `n`, other declaration positions and ++x / ++y in the loop headers all leave the two reloads in the wrong order.
// Every tree the map-who grid holds, layer by layer, gets a quad in each buffer and remembers its first vertex.
void CDXTrees::Build()	// FUNCTION: 0x0055a420
{
	SAFE_DELETE(mVBuf);
	SAFE_DELETE(mUnknownVBuf);
	mNumTrees = 0;

#line 94
	mVBuf = new(MEMTYPE_VBUFTEXTURE) CVBufTexture(NULL);
	DWORD	usage = CVertexShader::mSoftware ? D3DUSAGE_SOFTWAREPROCESSING : 0;
	mVBuf->SetVBFormat(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1, usage, sizeof(CFVF_GTEX), D3DPT_TRIANGLELIST, D3DPOOL_MANAGED);
	mVBuf->SetIBFormat(D3DFMT_INDEX16, usage, 2, D3DPOOL_MANAGED);
	mVBuf->SetPersist();
#line 106
	mUnknownVBuf = new(MEMTYPE_VBUFTEXTURE) CVBufTexture(NULL);
	mUnknownVBuf->SetVBFormat(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1, usage, sizeof(CFVF_GTEX), D3DPT_TRIANGLELIST, D3DPOOL_MANAGED);
	mUnknownVBuf->SetIBFormat(D3DFMT_INDEX16, usage, 2, D3DPOOL_MANAGED);
	mUnknownVBuf->SetPersist();

	for (SINT layer = 4; layer >= 0; layer--)
	{
		SINT	n = 64 / (1 << (4 - layer));
		SINT	y = 0, x = 0;	// FIT: y first, see the note above Build
		for (x = 0; x < n; x++)
		{
			for (y = 0; y < n; y++)
			{
				CSector	s(x, y, layer);
				if (!MAP_WHO.IsValidSector(s))
					continue;
				CMapWhoNode	*node = MAP_WHO.GetSector(s);
				for (CMapWhoEntry *e = MAP_WHO.GetFirstEntry(node); e; e = MAP_WHO.GetNextEntry())
				{
					CThing	*thing = e->GetThing();
					if (thing->IsA(THING_TYPE_TREE))
					{
						CRTTree		*rt = (CRTTree *)thing->GetRenderThing();
						CDXImposter	*imp = rt->mImposter;
						FVector		centre = thing->GetRenderPos();
						centre += rt->GetBoundingBox()->mOrigin;
						float		cx = centre.X, cy = centre.Y, cz = centre.Z;
						SINT		view = thing->GetRenderImposterNo();
						SImposterFrame	*f = imp->mFrames ? &imp->mFrames[view] : NULL;
						if (f)
						{
							CFVF_GTEX	v[4];
							v[0].mPos.X = cx;	v[0].mPos.Y = cy;	v[0].mPos.Z = cz;
							v[0].mU = imp->mFrames[view].mU0;	v[0].mV = imp->mFrames[view].mV0;
							v[0].mDiffuse = 0xffffffff;
							v[0].mNX = -imp->mFrames[view].mWidth;	v[0].mNY = -imp->mFrames[view].mHeight;	v[0].mNZ = 0;
							v[1].mPos.X = cx;	v[1].mPos.Y = cy;	v[1].mPos.Z = cz;
							v[1].mU = imp->mFrames[view].mU1;	v[1].mV = imp->mFrames[view].mV0;
							v[1].mDiffuse = 0xffffffff;
							v[1].mNX = imp->mFrames[view].mWidth;	v[1].mNY = -imp->mFrames[view].mHeight;	v[1].mNZ = 0;
							v[2].mPos.X = cx;	v[2].mPos.Y = cy;	v[2].mPos.Z = cz;
							v[2].mU = imp->mFrames[view].mU0;	v[2].mV = imp->mFrames[view].mV1;
							v[2].mDiffuse = 0xffffffff;
							v[2].mNX = -imp->mFrames[view].mWidth;	v[2].mNY = imp->mFrames[view].mHeight;	v[2].mNZ = 0;
							v[3].mPos.X = cx;	v[3].mPos.Y = cy;	v[3].mPos.Z = cz;
							v[3].mU = imp->mFrames[view].mU1;	v[3].mV = imp->mFrames[view].mV1;
							v[3].mDiffuse = 0xffffffff;
							v[3].mNX = imp->mFrames[view].mWidth;	v[3].mNY = imp->mFrames[view].mHeight;	v[3].mNZ = 0;
							SINT	i = mVBuf->AddVertices(v, 4);
							UWORD	idx[6];
							idx[0] = i;	idx[1] = i + 1;	idx[2] = i + 2;
							idx[3] = i + 2;	idx[4] = i + 3;	idx[5] = i + 1;
							mVBuf->AddIndices(idx, 6);
							rt->mSlot = idx[0];

							SImposterFrame	*top = imp->mFrames ? &imp->mFrames[4] : NULL;
							float	w = imp->GetFrameWidth(thing) * 0.7f;
							v[0].mPos.X = cx - w;	v[0].mPos.Y = cy - w;	v[0].mPos.Z = (float)(cz + TREES_Z_OFFSET);
							v[0].mU = top->mU0;	v[0].mV = top->mV0;
							v[0].mDiffuse = 0xffffffff;
							v[1].mPos.X = cx + w;	v[1].mPos.Y = cy - w;	v[1].mPos.Z = (float)(cz + TREES_Z_OFFSET);
							v[1].mU = top->mU1;	v[1].mV = top->mV0;
							v[1].mDiffuse = 0xffffffff;
							v[2].mPos.X = cx + w;	v[2].mPos.Y = cy + w;	v[2].mPos.Z = (float)(cz + TREES_Z_OFFSET);
							v[2].mU = top->mU1;	v[2].mV = top->mV1;
							v[2].mDiffuse = 0xffffffff;
							v[3].mPos.X = cx - w;	v[3].mPos.Y = cy + w;	v[3].mPos.Z = (float)(cz + TREES_Z_OFFSET);
							v[3].mU = top->mU0;	v[3].mV = top->mV1;
							v[3].mDiffuse = 0xffffffff;
							i = mUnknownVBuf->AddVertices(v, 4);
							idx[0] = i;	idx[1] = i + 1;	idx[2] = i + 2;
							idx[3] = i + 2;	idx[4] = i + 3;	idx[5] = i;
							mUnknownVBuf->AddIndices(idx, 6);
						}
						else
							rt->mSlot = -1;
						mNumTrees++;
					}
				}
			}
		}
	}
}

//******************************************************************************************
// Near miss: retail copies the two billboard axes through four registers (esi too) and reads the viewpoint into eax;
// the logic matches.
void CDXTrees::Render()	// FUNCTION: 0x0055aa10
{
	if (mNumTrees < 1)
		return;
	if (!mVBuf)
		Build();

	RENDERINFO.SetWorld(ZERO_FVECTOR, ID_FMATRIX);
	LT.D3D_SetTexture(0, CDXImposter::sTexture->GetTexture());
	LT.SRS(D3DRS_CULLMODE, D3DCULL_NONE);

	// the trees are billboards turned to the camera like the imposters. Retail copies each axis through a local: VC6
	// then carries the 16 bytes in four registers (esi too) where a direct copy of the statics uses three, and the
	// scratch registers after the copies rotate with it.
	RENDERINFO.SetLightingEnabled(FALSE);
	FVector	columnX = sColumnX;
	RENDERINFO.mBillboardX = columnX;
	FVector	columnZ = sColumnZ;
	RENDERINFO.mBillboardZ = columnZ;
	RENDERINFO.mBillboard = TRUE;

	LT.SRS(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	LT.SRS(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	LT.SRS(D3DRS_ALPHABLENDENABLE, FALSE);
	LT.SRS(D3DRS_ALPHATESTENABLE, TRUE);
	LT.SRS(D3DRS_ALPHAREF, 8);
	if (USE_MODULATE_2X.mValue)
	{
		LT.SRS(D3DRS_TEXTUREFACTOR, 0xffffffff);
		LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);
		LT.SetModulate(0);
	}
	else
	{
		LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		LT.STS(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	}
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	LT.STS(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	LT.STS(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	SetMipFilterNone(0);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	LT.SSS_NoCache(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	LT.SSS_NoCache(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	LT.SSS_NoCache(2, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	LT.SSS_NoCache(2, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	LT.ForceTS(0, D3DTSS_TEXCOORDINDEX, 0);
	LT.ForceTS(1, D3DTSS_TEXCOORDINDEX, 1);
	LT.ForceTS(2, D3DTSS_TEXCOORDINDEX, 2);
	LT.ForceTS(3, D3DTSS_TEXCOORDINDEX, 3);
	LT.STS(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.STS(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.STS(2, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	LT.STS(3, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	RENDERINFO.Apply(true, false);
	mVBuf->DrawIndexed(false, 0, 0);
	RENDERINFO.mBillboard = FALSE;

	// the second buffer only away from the ground
	FVector	pos = ENGINE.GetCamera()->GetPos();
	if (fabsf(pos.Z - MAP.Collide(pos)) > TREES_NEAR_GROUND)
	{
		RENDERINFO.Apply(false, false);
		mUnknownVBuf->DWATS(false, 0, 0);
	}

	RENDERINFO.SetLightingEnabled(TRUE);
	LT.SRS(D3DRS_ALPHABLENDENABLE, TRUE);
	LT.SRS(D3DRS_CULLMODE, D3DCULL_CCW);
	SetMipFilterByGlobalToggle(0);
	LT.SSS_NoCache(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	LT.SSS_NoCache(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	LT.STS(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	LT.STS(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	LT.STS(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	LT.STS(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

// The buffer being drawn from (an accessor: its call is evaluated before the lock's arguments, 0055ae6c).
static inline CVBuffer *CurrentVB(CVBufTexture *t) { return t->mVB[t->mCurrentVB]; }

//******************************************************************************************
void CDXTrees::HideTree(CRTTree *rt)	// FUNCTION: 0x0055ae40
{
	if (rt->mSlot < 0 || !mVBuf || !mUnknownVBuf)
		return;

	float	*v;
	if (CurrentVB(mVBuf)->LockRange(rt->mSlot * 36, 0x90, (void **)&v, 0) >= 0)
	{
		v[3] = 0;	v[4] = 0;	v[5] = 0;
		v[12] = 0;	v[13] = 0;	v[14] = 0;
		v[21] = 0;	v[22] = 0;	v[23] = 0;
		v[30] = 0;	v[31] = 0;	v[32] = 0;
		CurrentVB(mVBuf)->Unlock();
	}
	if (CurrentVB(mUnknownVBuf)->LockRange(rt->mSlot * 36, 0x90, (void **)&v, 0) >= 0)
	{
		v[0] = 0;	v[1] = 0;	v[2] = 0;
		v[9] = 0;	v[10] = 0;	v[11] = 0;
		v[18] = 0;	v[19] = 0;	v[20] = 0;
		v[27] = 0;	v[28] = 0;	v[29] = 0;
		CurrentVB(mUnknownVBuf)->Unlock();
	}
}
