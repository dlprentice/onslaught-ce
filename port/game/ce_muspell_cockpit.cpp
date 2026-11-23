// SPDX-License-Identifier: GPL-3.0-only
// port/game/ce_muspell_cockpit.cpp: scoped render-time texture swap for Muspell first-person Battle Engine cockpits.
//
// Retail ships meshtex\cockpit_M.tga in every Versus level resource file (loaded for m_m_be1.msh / m_m_be2.msh), while
// the first-person cockpit mesh (cockpit2.msh) binds meshtex\cockpit.tga and meshtex\A8_bluegunlight_LIT.tga. During
// CDXCockpit::Render() for a Muspell Battle Engine under the CE layer, this module temporarily swaps:
//   - meshtex\cockpit.tga            -> authentic retail meshtex\cockpit_M.tga (resolved from live CTexture::mFirst)
//   - meshtex\A8_bluegunlight_LIT.tga -> deterministic procedural warm crimson-amber D3DPOOL_MANAGED glow (mLit=TRUE)
// on both CMeshTexture and its CVBufTexture, then unconditionally restores every swapped pointer in LIFO order right
// after Render() returns.
//
// Strict invariants:
//   - Completely inert outside PortCeLayer(), during recordings, demos, probes, stress/attract/autoconfig, or pilot runs.
//   - Never touches Forseti Battle Engines (allegiance != kMuspellAllegiance).
//   - Scans CTexture::mFirst directly instead of calling CDXTexture::FindTexture: zero disk I/O, zero AddRef leaks,
//     zero FatalFileError on maps lacking cockpit_M.tga, and zero cached raw level CDXTexture* pointers across frames.
//   - Procedural gunlight texture is lazily constructed on first Muspell cockpit render, unlinked from CTexture::mFirst
//     (zero FreeLevelResources leak reports), and overrides DeviceObject lifecycle hooks (safe across D3D9 device reset
//     and clean shutdown).
#include "ce_muspell_cockpit.h"

#ifdef CE_VERSION

#include "common.h"
#include "game.h"
#include "CLIParams.h"
#include "BattleEngine.h"
#include "RenderThing.h"
#include "mesh.h"
#include "Texture.h"
#include "VBufTexture.h"
#include "ltshell.h"
#include "renderinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool PortCeLayer();
extern bool PortRecording();

static_assert((int)kMuspellAllegiance == CE_MUSPELL_ALLEGIANCE, "CE_MUSPELL_ALLEGIANCE matches kMuspellAllegiance");

namespace
{

constexpr int kMaxVisitedMeshes = 16;
constexpr int kMaxLodWalk = 8;

int gLastLoggedLevel = -1;
const void *gLastLoggedEngine = nullptr;

CeMuspellCockpitRestoreStack<CDXTexture *, CVBufTexture *, CE_MUSPELL_COCKPIT_MAX_SWAPS> gActiveSwaps;

struct SavedCockpitLighting
{
	bool active;
	DWORD ambient;
	CLight light[2];
	bool lightEnable[2];
	bool hasMaterial;
	D3DMATERIAL9 material;
};

SavedCockpitLighting gSavedLighting = {};

void ApplyMuspellCockpitLighting()
{
	if (gSavedLighting.active)
		return;
	gSavedLighting.ambient = RENDERINFO.mAmbient;
	for (int i = 0; i < 2; ++i)
	{
		gSavedLighting.light[i] = RENDERINFO.mLights[i];
		gSavedLighting.lightEnable[i] = RENDERINFO.mLightEnable[i];
	}
	gSavedLighting.hasMaterial = false;
	if (LT.GetDevice() && SUCCEEDED(LT.GetDevice()->GetMaterial(&gSavedLighting.material)))
		gSavedLighting.hasMaterial = true;
	gSavedLighting.active = true;

	const CeMuspellCockpitLightBoost boost = CeMuspellCockpitComputeLightBoost(
		(unsigned int)gSavedLighting.ambient,
		gSavedLighting.light[0].mDiffuseR,
		gSavedLighting.light[0].mDiffuseG,
		gSavedLighting.light[0].mDiffuseB);

	RENDERINFO.SetAmbient((DWORD)boost.ambient);

	CLight key = gSavedLighting.light[0];
	key.mType = LIGHT_DIRECTIONAL;
	if (key.mDir.X == 0.f && key.mDir.Y == 0.f && key.mDir.Z == 0.f)
		key.mDir.Set(-0.35f, 0.72f, 0.60f);
	key.mDiffuseR = boost.keyR;
	key.mDiffuseG = boost.keyG;
	key.mDiffuseB = boost.keyB;
	RENDERINFO.SetLight(0, &key);
	RENDERINFO.SetLightEnable(0, true);

	CLight rim = gSavedLighting.light[1];
	rim.mType = LIGHT_DIRECTIONAL;
	rim.mDir.Set(0.45f, -0.65f, 0.61f);
	rim.mDiffuseR = boost.rimR;
	rim.mDiffuseG = boost.rimG;
	rim.mDiffuseB = boost.rimB;
	RENDERINFO.SetLight(1, &rim);
	RENDERINFO.SetLightEnable(1, true);

	if (gSavedLighting.hasMaterial)
	{
		D3DMATERIAL9 mat = gSavedLighting.material;
		mat.Diffuse.r = boost.matDiffR;
		mat.Diffuse.g = boost.matDiffG;
		mat.Diffuse.b = boost.matDiffB;
		mat.Ambient.r = 1.0f;
		mat.Ambient.g = 0.42f;
		mat.Ambient.b = 0.32f;
		mat.Emissive.r = boost.matEmisR;
		mat.Emissive.g = boost.matEmisG;
		mat.Emissive.b = boost.matEmisB;
		mat.Emissive.a = 0.0f;
		LT.D3D_SetMaterial(&mat);
	}
	RENDERINFO.Apply(false, false);
}

void RestoreMuspellCockpitLighting()
{
	if (!gSavedLighting.active)
		return;
	RENDERINFO.SetAmbient(gSavedLighting.ambient);
	for (int i = 0; i < 2; ++i)
	{
		RENDERINFO.SetLight(i, &gSavedLighting.light[i]);
		RENDERINFO.SetLightEnable(i, gSavedLighting.lightEnable[i]);
	}
	if (gSavedLighting.hasMaterial && LT.GetDevice())
		LT.D3D_SetMaterial(&gSavedLighting.material);
	RENDERINFO.Apply(false, false);
	gSavedLighting.active = false;
}

class CMuspellGunlightTexture : public CDXTexture
{
public:
	bool mCreateFailed;

	CMuspellGunlightTexture() : mCreateFailed(false)
	{
		// Unlink from CTexture::mFirst immediately so CTEXTURE::FreeLevelResources() and ClearOut() never touch
		// this persistent procedural texture or report it as a level texture leak.
		CDXTexture *self = static_cast<CDXTexture *>(this);
		if (CTexture::mFirst == self)
		{
			CTexture::mFirst = this->CTexture::mNext;
			this->CTexture::mNext = NULL;
		}
		else
		{
			for (CDXTexture *p = CTexture::mFirst; p; p = p->CTexture::mNext)
			{
				if (p->CTexture::mNext == self)
				{
					p->CTexture::mNext = this->CTexture::mNext;
					this->CTexture::mNext = NULL;
					break;
				}
			}
		}

		strcpy(mName, "ce_muspell_gunlight_lit");
		mLit = TRUE;
		mNumFrames = 1;
		mWidth = CE_MUSPELL_GUNLIGHT_SIZE;
		mHeight = CE_MUSPELL_GUNLIGHT_SIZE;
		mTexFormat = TEXFMT_A8R8G8B8;
		mFormat = TEXFMT_A8R8G8B8;
		mLevels = 1;
		mMipFilter = 0;
		mDisplayFormat = FALSE;
		mPool = D3DPOOL_MANAGED;
		mRefCount = 1;
		InitDeviceObjects();
	}

	~CMuspellGunlightTexture() override
	{
		DeleteDeviceObjects();
	}

	HRESULT InitDeviceObjects() override
	{
		mCreateFailed = false;
		if (!LT.mD3DActive || !LT.GetDevice())
			return S_OK;
		if (mTextures[0])
			return S_OK;

		IDirect3DTexture9 *tex = NULL;
		if (FAILED(LT.D3D_CreateTextureNoCheck(CE_MUSPELL_GUNLIGHT_SIZE, CE_MUSPELL_GUNLIGHT_SIZE, 1, 0,
		                                       D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex)) || !tex)
		{
			mCreateFailed = true;
			return E_FAIL;
		}
		D3DLOCKED_RECT lr;
		if (FAILED(tex->LockRect(0, &lr, NULL, 0)))
		{
			tex->Release();
			mCreateFailed = true;
			return E_FAIL;
		}
		for (int y = 0; y < CE_MUSPELL_GUNLIGHT_SIZE; ++y)
		{
			DWORD *row = (DWORD *)((unsigned char *)lr.pBits + y * lr.Pitch);
			for (int x = 0; x < CE_MUSPELL_GUNLIGHT_SIZE; ++x)
				row[x] = (DWORD)CeMuspellCockpitGunlightPixel(x, y, CE_MUSPELL_GUNLIGHT_SIZE, CE_MUSPELL_GUNLIGHT_SIZE);
		}
		tex->UnlockRect(0);
		mTextures[0] = tex;
		return S_OK;
	}

	HRESULT RestoreDeviceObjects() override
	{
		if (!mTextures[0] && !mCreateFailed)
			return InitDeviceObjects();
		return S_OK;
	}

	HRESULT InvalidateDeviceObjects() override
	{
		// D3DPOOL_MANAGED texture survives D3D9 device reset cleanly without reload.
		return S_OK;
	}

	HRESULT DeleteDeviceObjects() override
	{
		mCreateFailed = false;
		if (mTextures[0])
		{
			mTextures[0]->Release();
			mTextures[0] = NULL;
		}
		return S_OK;
	}
};

CDXTexture *GetMuspellGunlightTexture()
{
	if (!LT.mD3DActive || !LT.GetDevice())
		return nullptr;
	static CMuspellGunlightTexture sGunlight;
	if (!sGunlight.GetTexture() && !sGunlight.mCreateFailed)
		sGunlight.InitDeviceObjects();
	return sGunlight.GetTexture() ? static_cast<CDXTexture *>(&sGunlight) : nullptr;
}

bool PilotEnvPresent()
{
	static int cached = -1;
	if (cached < 0)
	{
		const char *pilot = getenv("BEA_PORT_PILOT");
		const char *autopilot = getenv("BEA_PORT_AUTOPILOT");
		cached = ((pilot && pilot[0]) || (autopilot && autopilot[0])) ? 1 : 0;
	}
	return cached != 0;
}

bool ShouldSwapForEngine(CBattleEngine *be)
{
	if (!be)
		return false;
	CeMuspellCockpitFenceInput in = {};
	in.ceLayer = PortCeLayer();
	in.recording = PortRecording();
	in.playableDemo = (PLAYABLE_DEMO != 0);
	in.autoConfigTest = (CLIPARAMS.mAutoConfigTest != 0);
	in.recordDemo = (CLIPARAMS.mRecordDemo != 0);
	in.playbackDemo = (CLIPARAMS.mPlaybackDemo != 0);
	in.stressTest = (CLIPARAMS.mStressTest != 0);
	in.attractMode = (CLIPARAMS.mAttractMode != 0);
	in.pilotEnv = PilotEnvPresent();
	in.allegiance = (int)be->GetAllegiance();
	return CeMuspellCockpitShouldSwap(in);
}

// Scans the live in-memory CTexture::mFirst list (zero disk I/O, zero AddRef, zero FatalFileError, zero stale cache).
CDXTexture *FindLiveMuspellCockpitTexture()
{
	for (CDXTexture *t = CTexture::mFirst; t; t = t->CTexture::mNext)
	{
		const char *name = t->GetName();
		if (!name || (name[0] != 'm' && name[0] != 'M'))
			continue;
		if (CeMuspellCockpitIsMuspellTextureName(name) && t->GetTexture() != NULL)
			return t;
	}
	return nullptr;
}

void RestoreAllActiveSwaps()
{
	gActiveSwaps.RestoreAll([](CVBufTexture *vb) -> CDXTexture ** {
		return vb ? &vb->mTexture : nullptr;
	});
}

void SwapMeshRecord(CMesh *m, CDXTexture *muspellTex, CDXTexture *gunlightTex,
                    CMesh **visited, int &visitedCount, int &gunlightSwaps)
{
	if (!m || !muspellTex)
		return;
	for (int v = 0; v < visitedCount; ++v)
	{
		if (visited[v] == m)
			return;
	}
	if (visitedCount >= kMaxVisitedMeshes)
		return;
	visited[visitedCount++] = m;

	if (m->mTextures && m->mNumTextures > 0)
	{
		for (SINT i = 0; i < m->mNumTextures; ++i)
		{
			CMeshTexture *mt = &m->mTextures[i];
			if (!mt->mTexture)
				continue;
			const char *tname = mt->mTexture->GetName();
			if (CeMuspellCockpitIsBaseTextureName(tname))
			{
				CVBufTexture *vb = mt->mVBuffer;
				CDXTexture **vbSlot = vb ? &vb->mTexture : nullptr;
				gActiveSwaps.RecordAndSwap(&mt->mTexture, vb, vbSlot, muspellTex);
			}
			else if (gunlightTex && CeMuspellCockpitIsGunlightTextureName(tname))
			{
				CVBufTexture *vb = mt->mVBuffer;
				CDXTexture **vbSlot = vb ? &vb->mTexture : nullptr;
				if (gActiveSwaps.RecordAndSwap(&mt->mTexture, vb, vbSlot, gunlightTex))
					gunlightSwaps++;
			}
		}
	}

	if (m->mParts && m->mNumParts > 0)
	{
		for (SINT p = 0; p < m->mNumParts; ++p)
		{
			CMeshPart *part = m->mParts[p];
			if (part && part->mBaseMesh && part->mBaseMesh != m)
				SwapMeshRecord(part->mBaseMesh, muspellTex, gunlightTex, visited, visitedCount, gunlightSwaps);
		}
	}
}

} // namespace

extern "C" void PortCeMuspellCockpitBegin(CBattleEngine *be, CRenderThing *rt)
{
	if (gSavedLighting.active)
		RestoreMuspellCockpitLighting();
	if (gActiveSwaps.count != 0)
		RestoreAllActiveSwaps();

	if (!be || !rt)
		return;
	if (!ShouldSwapForEngine(be))
		return;

	CMesh *rootMesh = rt->GetRTMesh();
	if (!rootMesh)
		return;

	CDXTexture *muspellTex = FindLiveMuspellCockpitTexture();
	if (!muspellTex)
		return;

	CDXTexture *gunlightTex = GetMuspellGunlightTexture();

	CMesh *visited[kMaxVisitedMeshes] = {};
	int visitedCount = 0;
	int gunlightSwaps = 0;
	int lodSteps = 0;
	for (CMesh *m = rootMesh; m && lodSteps < kMaxLodWalk; m = m->mNextInChain, ++lodSteps)
		SwapMeshRecord(m, muspellTex, gunlightTex, visited, visitedCount, gunlightSwaps);

	if (gActiveSwaps.count > 0)
	{
		ApplyMuspellCockpitLighting();
		const int level = GAME.GetCurrentlyRunningLevelNum();
		if (level != gLastLoggedLevel || (const void *)be != gLastLoggedEngine)
		{
			gLastLoggedLevel = level;
			gLastLoggedEngine = (const void *)be;
			fprintf(stderr, "ce-muspell-cockpit: active level=%d swapped=%d gunlight=%d\n",
			        level, gActiveSwaps.count, gunlightSwaps);
		}
	}
}

extern "C" void PortCeMuspellCockpitEnd(void)
{
	RestoreMuspellCockpitLighting();
	RestoreAllActiveSwaps();
}

#else

extern "C" void PortCeMuspellCockpitBegin(class CBattleEngine *, class CRenderThing *) {}
extern "C" void PortCeMuspellCockpitEnd(void) {}

#endif
