// RTTree.cpp: reconstructed from RTTree.obj (004dd5c0..004de09f); Stuart's release has no copy. The tree detail
// the options pick (CRTMesh's two statics) lives here too.
#include "common.h"
#define RTTREE_CPP
#include "mathsconst.h"			// FUNCTION: 0x004dd5c0 init:ZERO_FVECTOR
								// FUNCTION: 0x004dd5e0 init:ID_FMATRIX
#include "rttree.h"
#include "rtmesh.h"
#include "meshrenderer.h"
#include "tree.h"
#define TREES_FULL
#include "Trees.h"
#include "engine.h"
#include "Camera.h"
#include "map.h"
#include "renderinfo.h"
#include "tweak.h"
#include "CLIParams.h"
#include "shadows.h"

class CDXImposter
{
public:
	void		Render(IRenderableThing *thing, SINT alpha, SINT frame);	// 00543300

	static CDXImposter	*FindOrCreate(CInitRenderThing *init, CMESH *mesh, SINT a, SINT b, SINT frames, SINT mode, SINT c);	// 004888f0 (imposter.cpp takes the mode as a SINT)
};

// the tree and object detail (Career.cpp saves them as ints; they hold floats)
float			TREE_MESH_DISTANCE = 30.0f;	// 006321a0: nearer than this a tree is its mesh
extern float		MESH_LOD_BIAS;				// 00631e88 (rtmesh.cpp)
extern float		TRAIL_DETAIL;				// 00630e0c (PDSimpleSprite.cpp)
extern CTweakSLONG	LANDSCAPE_LOWRES_GEOM;		// 008aa990


// the mesh's cue factor, through the FPU as the radius accessor goes (004dd813)
static inline float MeshCueFactor(CMESH *mesh) { return mesh->mCueFactor; }

// a tree that has been hit falls as a mesh
static inline bool TreeFalling(IRenderableThing *thing) { return ((CTree *)thing)->mFalling != NULL; }

#define RGB_R(c)	((float)(((c) >> 16) & 0xff) / 256.0f)
#define RGB_G(c)	((float)(((c) >> 8) & 0xff) / 256.0f)
#define RGB_B(c)	((float)((c) & 0xff) / 256.0f)

//******************************************************************************************
void CRTMesh::SetQualityLevel(int level)	// FUNCTION: 0x004dd6b0
{
	switch (level)
	{
	case 2:
		TREE_MESH_DISTANCE = 70.0f;
		MESH_LOD_BIAS = 0.3f;
		TRAIL_DETAIL = 2.0f;
		SHADOWS.mBlobShadowFadeDist = 40.0f;
		LANDSCAPE_LOWRES_GEOM.Set(0);
		break;
	case 1:
		TREE_MESH_DISTANCE = 30.0f;
		MESH_LOD_BIAS = 1.0f;
		TRAIL_DETAIL = 1.0f;
		SHADOWS.mBlobShadowFadeDist = 30.0f;
		LANDSCAPE_LOWRES_GEOM.Set(0);
		break;
	case 0:
		TREE_MESH_DISTANCE = 10.0f;
		MESH_LOD_BIAS = 3.0f;
		TRAIL_DETAIL = 0.1f;
		SHADOWS.mBlobShadowFadeDist = 15.0f;
		LANDSCAPE_LOWRES_GEOM.Set(1.0f);
		break;
	}
}

//******************************************************************************************
int CRTMesh::GetQualityLevel()	// FUNCTION: 0x004dd770
{
	if (TREE_MESH_DISTANCE < 15.0f)
		return 0;
	return (TREE_MESH_DISTANCE > 40.0f) ? 2 : 1;
}

//******************************************************************************************
void CRTTree::Init(CInitRenderThing *init)	// FUNCTION: 0x004dd7b0
{
	CRenderThing::Init(init);

	mMesh = CMESH::GetMesh(init->mName, init->mMeshFlags);
	mImposter = CDXImposter::FindOrCreate(init, mMesh, 6, 1, 1, AM_NONE, 1);
	if (!mMesh)
		mMesh = ENGINE.GetDefaultMesh();

	mRadius = mMesh->Radius();
	mCueFactor = MeshCueFactor(mMesh);
	mMaxSlope = (float)tan(0.6977778f);

	if (CLIPARAMS.mHiDetailMode)
		TREE_MESH_DISTANCE = 45.0f;

	mUnknown28 = false;
	mUnknown1c = 2;
}

//******************************************************************************************
void CRTTree::RenderImposter()	// FUNCTION: 0x004dd850
{
	FVector	cam = ENGINE.GetCamera()->GetPos();
	FVector	pos;
	pos = mThing->GetRenderPos();
	cam.X -= pos.X;
	cam.Y -= pos.Y;
	float	dist = cam.X * cam.X + cam.Y * cam.Y;
	float	beyond = dist - TREE_MESH_DISTANCE * TREE_MESH_DISTANCE;
	float	dz = pos.Z - cam.Z;
	float	slope = dz * dz / (dist + 0.01f);
	BOOL	falling = (((CTree *)mThing)->mFalling != NULL);
	BOOL	close = (beyond <= 0);
	if ((!close && !falling) || FORCE_OBJECT_IMPOSTERS)
	{
		if ((slope >= mMaxSlope) || (((CTree *)mThing)->mFalling))
			mImposter->Render(mThing, 255, 0);
		mImposterDrawn = TRUE;
	}
}

//******************************************************************************************
// The colour channels are named locals with brackets round the whole float initialiser, both of them Stuart's habit:
// engine.cpp's lighting declares r, g and b once and reuses them for each light, and its PS2 branch names every channel.
// The anti-sun light reuses r, g and b here the same way. The named intermediates matter to the scheduler's 81-node
// region cuts, which fall where retail's do (004ddd50..004ddea0, 004ddfa2: the last block is 85 nodes, so the flag
// stores come before the hook call's setup). Plainer forms measured (shape / instructions / bytes): the sun's r, g and b
// without their brackets 28/32/182, the anti-sun's channels passed straight to Init 22/26/67, the ambient's sums
// written out without ambg and ambb 32/54/128, those four locals without their brackets 4/4/16, everything inline 42
// instructions. The channel macros divide by 256.0f as retail does (Stuart's multiplication by (1.0f / 256) is 44/54/221).
void CRTTree::Render(DWORD flags)	// FUNCTION: 0x004dd960
{
	FVector	cam = ENGINE.GetCamera()->GetPos();
	FVector	pos;
	pos = mThing->GetRenderPos();
	cam.X -= pos.X;
	cam.Y -= pos.Y;
	float	beyond = cam.X * cam.X + cam.Y * cam.Y - TREE_MESH_DISTANCE * TREE_MESH_DISTANCE;
	BOOL	falling = (((CTree *)mThing)->mFalling != NULL);
	BOOL	close = (beyond <= 0);
	if ((!close && !falling) || FORCE_OBJECT_IMPOSTERS)
		return;

	FMatrix	ori;
	ori = mThing->GetRenderOrientation();
	RENDERINFO.SetWorld(pos, ori);

	CLight	light, old0, old1;
	old0 = RENDERINFO.mLights[0];
	old1 = RENDERINFO.mLights[1];

	DWORD	sun = MAP.GetSun();
	float	r = (RGB_R(sun) * 0.1f);
	float	g = (RGB_G(sun) * 0.1f);
	float	b = (RGB_B(sun) * 0.1f);
	light.Init(LIGHT_DIRECTIONAL, FVector(0, 0, 1.0f), r, g, b);
	RENDERINFO.SetLight(0, &light);
	RENDERINFO.SetLightEnable(0, TRUE);

	DWORD	antisun = MAP.GetAntiSun();
	r = RGB_R(antisun);
	g = RGB_G(antisun);
	b = RGB_B(antisun);
	light.Init(LIGHT_DIRECTIONAL, FVector(0, 0, -1.0f), r, g, b);
	RENDERINFO.SetLight(1, &light);
	RENDERINFO.SetLightEnable(1, TRUE);

	DWORD	old_ambient = RENDERINFO.mAmbient;
	DWORD	amb = MAP.GetAmbient();
	sun = MAP.GetSun();
	float	sr = RGB_R(sun) * 0.25f;
	float	sb = RGB_B(sun) * 0.25f;
	float	red = RGB_R(amb) * 0.75f + sr;
	float	ambg = (RGB_G(amb) * 0.75f);
	float	green = (ambg + sb);
	float	ambb = (RGB_B(amb) * 0.75f);
	float	blue = (ambb + sb);
	RENDERINFO.SetAmbient((((SINT)(red * 255.0f)) << 16) | (((SINT)(green * 255.0f)) << 8) | ((SINT)(blue * 255.0f)));

	ENGINE.SetTreeLighting(TRUE);
	CMeshRenderer::Render(&pos, &ori, mMesh, NULL, NULL, 0, true, 0);

	RENDERINFO.SetAmbient(old_ambient);
	RENDERINFO.SetLight(0, &old0);
	RENDERINFO.SetLight(1, &old1);
	RENDERINFO.mUnknown2f6 = false;
	ENGINE.SetTreeLighting(FALSE);
}

// Inline, defined out of the class: the destructor's batch of out-of-line inline copies is GetRTRadius (in the class),
// then these two in definition order, then ??_GCRTTree (004de050..004de080).
inline CBoundingBox *CRTTree::GetBoundingBox() { return mMesh->mBoundingBox; }
inline CMESH *CRTTree::GetRTMesh() { return mMesh; }

//******************************************************************************************
CRTTree::~CRTTree()	// FUNCTION: 0x004ddfd0
{
	TREES.HideTree(this);
	if (mMesh)
	{
		mMesh->Release();
		mMesh = NULL;
	}
}

//******************************************************************************************
// The engine's tree-lighting hook (engine.h), empty on the PC. The one-argument body is folded into the shared one at
// 00452b60 (/OPT:ICF); auto-inlining is off around it because retail keeps both calls above.
#pragma auto_inline(off)
// FUNCTION: 0x00452b60
void CEngine::SetTreeLighting(BOOL on)
{
}
#pragma auto_inline(on)

// FUNCTION: 0x004de050 ?GetRTRadius@CRTTree@@UAEMXZ
// FUNCTION: 0x004de060 ?GetBoundingBox@CRTTree@@UAEPAVCBoundingBox@@XZ
// FUNCTION: 0x004de070 ?GetRTMesh@CRTTree@@UAEPAVCMesh@@XZ
// FUNCTION: 0x004de080 ??_GCRTTree@@UAEPAXI@Z
