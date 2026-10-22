// rtmesh.cpp: reconstructed from rtmesh.obj (004dc280..004dd5bf); Stuart's release has no copy. The allocations'
// __LINE__ values are the retail ones; the #line directives place them, because the retail file's layout is unknown.
#include "common.h"
class CRTMesh;class CRenderThing;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#include "rtmesh.h"
#include "meshrenderer.h"
#include "mesh.h"
#include "meshpose.h"
#include "thing.h"
#include "engine.h"
#include "console.h"
#include "DebugLog.h"
#include "ParticleManager.h"
#include "frontend.h"
#include "renderinfo.h"
// CRenderThing's inline members, emitted where the table is: retail folds them with the inline CThing members.
// FUNCTION: 0x0050e890
inline float CRenderThing::GetRTRadius()
{
	return 0.5f;
}

// FUNCTION: 0x00459990
inline void CRenderThing::CreatePolyBuckets()
{
}

// FUNCTION: 0x00459990
inline void CRenderThing::Unknown12()
{
}

// FUNCTION: 0x00459990
inline void CRenderThing::Unknown13()
{
}

// What the mesh render thing reaches outside its own files; declared here until their headers are reconstructed.

class CDXImposter
{
public:
	void		Render(IRenderableThing *thing, SINT alpha, SINT frame);	// 00543300

	UBYTE		mUnknown00[0x44];
	SINT		mNumFrames;					// +0x44
	BOOL		mAnimated;					// +0x48

	static CDXImposter	*FindOrCreate(CInitRenderThing *init, CMESH *mesh, SINT a, SINT b, SINT frames, SINT mode, SINT c);	// 004888f0 (imposter.cpp takes the mode as a SINT)
};

extern UINT		MESH_SURFACE_DETAIL;			// 00630130 (MeshRenderer.cpp; 100 unless a mesh is being drawn)
extern SINT		GAME_RENDER_MODE;				// 00704e48: 4 forces imposters

// CMeshPart::EvaluatePose owns the seven-argument pose operation at 004b4de0.

// The console variables Init registers once.
bool			FORCE_OBJECT_IMPOSTERS = false;	// 0083cd58: initialised, so laid out in definition order (4-aligned: loaded as a dword and masked)
static bool		sVariablesRegistered = false;	// 0083cd59
float			IMPOSTER_FADE_START = 39.0f;	// 00631e94
float			IMPOSTER_FADE_END = 40.0f;		// 00631e98
float			MESH_LOD_BIAS = 1.0f;			// 00631e88
float			MESH_LOD_MED_THRESHOLD = 10.0f;	// 00631e8c
float			MESH_LOD_LOW_THRESHOLD = 20.0f;	// 00631e90
bool			MESH_LOD_IGNORE_ZOOM = false;		// 0083cd5a
bool			SNOW_LAYER_ENABLE = false;			// 0083cd5b
float			MESH_TEXTURE_LOD_BIAS = 100.0f;	// 00631e9c
float			MESH_SURFACE_LOD_BIAS = 400.0f;	// 00631ea0

CRTMesh		*CRTMesh::sRenderedLastFrame = NULL;
CRTMesh		*CRTMesh::sRenderedThisFrame = NULL;

// FUNCTION: 0x004dc280 init:ZERO_FVECTOR
// FUNCTION: 0x004dc2a0 init:ID_FMATRIX

//******************************************************************************************
#line 53
void CRTMesh::Init(CInitRenderThing *init)	// FUNCTION: 0x004dc370
{
	CRenderThing::Init(init);

	if (!sVariablesRegistered)
	{
		CONSOLE.RegisterVariable("cg_forceobjectimposters", "Force use of object imposters", CVar_bool, &FORCE_OBJECT_IMPOSTERS);
		CONSOLE.RegisterVariable("cg_imposterfadestart", "Distance at which imposters start to fade in", CVar_float, &IMPOSTER_FADE_START);
		CONSOLE.RegisterVariable("cg_imposterfadeend", "Distance at which imposters stop fading in", CVar_float, &IMPOSTER_FADE_END);
		CONSOLE.RegisterVariable("cg_meshlodbias", "Mesh LOD bias (use <1 to increase detail)", CVar_float, &MESH_LOD_BIAS);
		CONSOLE.RegisterVariable("cg_meshlodmedthreshold", "Mesh LOD high<->medium detail threshold", CVar_float, &MESH_LOD_MED_THRESHOLD);
		CONSOLE.RegisterVariable("cg_meshlodlowthreshold", "Mesh LOD medium<->low detail threshold", CVar_float, &MESH_LOD_LOW_THRESHOLD);
		CONSOLE.RegisterVariable("cg_meshlodignorezoom", "Ignore the zoom factor for LOD", CVar_bool, &MESH_LOD_IGNORE_ZOOM);
		CONSOLE.RegisterVariable("cg_snowlayerenable", "Enable or disable the snow layer on objects", CVar_bool, &SNOW_LAYER_ENABLE);
		CONSOLE.RegisterVariable("cg_meshtexturelodbias", "Mesh texture LOD bias", CVar_float, &MESH_TEXTURE_LOD_BIAS);
		CONSOLE.RegisterVariable("cg_meshsurfacelodbias", "Mesh surface LOD bias", CVar_float, &MESH_SURFACE_LOD_BIAS);
		sVariablesRegistered = true;
	}

	mUnknown24 = init->mUnknown404;

	if (init->mName[0])
		mMesh = CMESH::GetMesh(init->mName, init->mMeshFlags);
	else
	{
		SINT	n;
		if ((mThing) && ((n = mThing->GetRenderMesh()) != -1) && (n != -2))
			mMesh = ENGINE.GetGlobalMesh(n);
		else
			mMesh = ENGINE.GetDefaultMesh();
	}
	if (!mMesh)
		mMesh = ENGINE.GetDefaultMesh();

	mRadius = mMesh->mRadius;
	mCueFactor = mMesh->mCueFactor;

	mPose = new(MEMTYPE_POSE) CMeshPose;
	if (!mPose)
	{
		TRACE("Warning : not enough thing heap for pose data!\n");
		mPose = NULL;
	}
	else
		mPose->Init(mMesh->mNumParts);

	// the mesh's effect emitters are the records whose names start with '_'
	mNumEffects = 0;
	SINT	i;
	for (i = 0; i < mMesh->GetNumEmitters(); i++)
	{
		char	name[256];
		SINT	id;
		mMesh->GetEmitterNameByID(i, name, &id);
		if (strlen(name) && (name[0] == '_'))
			mNumEffects++;
	}

	if (mNumEffects > 0)
	{
#line 117
		mEffectActive = new(MEMTYPE_RENDERTHING) bool[mNumEffects];
		mEffects = new(MEMTYPE_RENDERTHING) CParticleEffectLink*[mNumEffects];
		mEffectParts = new(MEMTYPE_RENDERTHING) SINT[mNumEffects];
		mEffectPartPointers = new(MEMTYPE_RENDERTHING) CMeshPart *[mNumEffects];
		mEffectIds = new(MEMTYPE_RENDERTHING) SINT[mNumEffects];
		mEffectTypes = new(MEMTYPE_RENDERTHING) CParticleDescriptor*[mNumEffects];

		if ((mEffectActive) && (mEffects) && (mEffectParts) && (mEffectPartPointers) && (mEffectIds))
		{
			SINT	n = 0;
			for (i = 0; i < mMesh->GetNumEmitters(); i++)
			{
				char	name[256];
				SINT	id;
				mMesh->GetEmitterNameByID(i, name, &id);
				if (strlen(name) && (name[0] == '_'))
				{
					mEffectParts[n] = i;
					mEffectActive[n] = false;
					mEffects[n] = NULL;
					mEffectPartPointers[n] = mMesh->GetEmitterSubpartByID(i);
					mEffectIds[n] = id;
					mEffectTypes[n] = PARTICLE_SET.FindByName(name + 1);
					n++;
				}
			}
		}
		else
		{
			TRACE("Warning : not enough thing heap for RTMesh effects\n");
			mNumEffects = 0;
		}
	}
	else
	{
		mEffectActive = NULL;
		mEffects = NULL;
		mEffectParts = NULL;
		mEffectPartPointers = NULL;
		mEffectIds = NULL;
		mEffectTypes = NULL;
	}

	mNextRendered = NULL;
	mPrevRendered = NULL;
	mImposter = NULL;
	if (mThing)
	{
		if (mThing->GetCanBeImpostered())
			mImposter = CDXImposter::FindOrCreate(init, mMesh, 6, 1, mThing->GetImposterFrames(), mThing->GetImposterAnimMode(), 0);
	}
	else
		mImposter = CDXImposter::FindOrCreate(init, mMesh, 6, 1, 1, AM_NONE, 0);

	if (mImposter)
#line 178
		mImposterState = new(MEMTYPE_IMPOSTER) UBYTE;
	else
		mImposterState = NULL;
}

//******************************************************************************************
CRTMesh::~CRTMesh()	// FUNCTION: 0x004dc950
{
	if (mNextRendered)
		mNextRendered->mPrevRendered = mPrevRendered;
	if (mPrevRendered)
		mPrevRendered->mNextRendered = mNextRendered;
	if (sRenderedLastFrame == this)
		sRenderedLastFrame = mNextRendered;
	if (sRenderedThisFrame == this)
		sRenderedThisFrame = mNextRendered;

	for (SINT i = 0; i < mNumEffects; i++)
	{
		if (mEffects[i])
		{
			mEffects[i]->Stop(FALSE);
			delete mEffects[i];
			mEffects[i] = NULL;
		}
	}

	SAFE_DELETE(mPose);
	SAFE_DELETE(mImposterState);
	if (mMesh)
	{
		mMesh->Release();
		mMesh = NULL;
	}
	SAFE_DELETE(mEffects);
	SAFE_DELETE(mEffectParts);
	SAFE_DELETE(mEffectIds);
	SAFE_DELETE(mEffectActive);
	SAFE_DELETE(mEffectPartPointers);
	SAFE_DELETE(mEffectTypes);
}

// FUNCTION: 0x004dcaf0 ?GetRTRadius@CRTMesh@@UAEMXZ
// FUNCTION: 0x004dcb00 ??1CMeshPose@@QAE@XZ
// FUNCTION: 0x004dcb70 ??_GCRTMesh@@UAEPAXI@Z

//******************************************************************************************
void CRTMesh::CreatePolyBuckets()	// FUNCTION: 0x004dcb90
{
	if (mMesh)
		mMesh->BuildAllPolyBuckets();
}

//******************************************************************************************
// The level of detail is RenderImposter's expression, the bias called inside it: retail keeps the square root's
// store (004dcce9). The LOD test reads mMesh, so the lower LOD is read again after the comparisons (004dcdb3).
void CRTMesh::Render(DWORD flags)	// FUNCTION: 0x004dcba0
{
	// move to the head of this frame's list
	if (mNextRendered)
		mNextRendered->mPrevRendered = mPrevRendered;
	if (mPrevRendered)
		mPrevRendered->mNextRendered = mNextRendered;
	if (sRenderedLastFrame == this)
		sRenderedLastFrame = mNextRendered;
	if (sRenderedThisFrame == this)
		sRenderedThisFrame = mNextRendered;
	mNextRendered = sRenderedThisFrame;
	mPrevRendered = NULL;
	if (sRenderedThisFrame)
		sRenderedThisFrame->mPrevRendered = this;
	sRenderedThisFrame = this;

	FVector	pos = ENGINE.GetCamera()->GetPos();
	float	zoom = ENGINE.GetCamera()->GetZoom();
	if (MESH_LOD_IGNORE_ZOOM)
		zoom = 1.0f;
	FVector	thing_pos;	// declared then assigned, here and in RenderImposter: Stuart's occasional habit (thing.cpp:462, :482); direct initialisation loses both (004dcba0, 004dcea0)
	thing_pos = mThing->GetRenderPos();
	pos = pos - thing_pos;
	float	dist = pos.MagnitudeSq();
	float	radius = GetBoundingBox()->mRadius;
	float	lod = (dist = (float)sqrt(dist)) / radius * mThing->GetRenderImposterBias() * MESH_LOD_BIAS * zoom;

	BOOL	near_enough = ((!FORCE_OBJECT_IMPOSTERS) && (lod < IMPOSTER_FADE_END));
	if ((!mImposter) | near_enough)	// STAND-IN (layout; review F 2026-10-15): bitwise or; `||` loses 004dcba0
	{
		FVector	offset = ZERO_FVECTOR;
		FMatrix	ori = ID_FMATRIX;

		SINT	alpha = 255 - (SINT)(((lod - IMPOSTER_FADE_START) * 255.0f) / (IMPOSTER_FADE_END - IMPOSTER_FADE_START));
		if ((alpha > 255) || (!mImposter))
			alpha = 255;

		CMESH	*mesh = mMesh;
		if (mMesh->mNextInChain)
		{
			if (lod > MESH_LOD_MED_THRESHOLD)
				mesh = mesh->mNextInChain;
			if ((lod > MESH_LOD_LOW_THRESHOLD) && (mesh->mNextInChain))
				mesh = mesh->mNextInChain;
		}

		// After push edi at 004dcd57, retail [esp+0xc] at 004dcdcc is the
		// camera zoom stored at 004dcc2b, not the distance temporary.
		SINT	detail = (SINT)(100.0f - MESH_SURFACE_LOD_BIAS / (zoom * 10.0f) * lod);
		if (detail < 0)
			detail = 0;
		else if (detail > 100)
			detail = 100;
		MESH_SURFACE_DETAIL = detail;
		CMeshRenderer::Render(&offset, &ori, mesh, mThing, this, mUnknown24, false, flags);
		MESH_SURFACE_DETAIL = 100;

		// Retail dispatches IRenderableThing slot 24 here (004dce40/004dce4c).
		// The snow-density identity is established by the console binding and pass use.
		if ((SNOW_LAYER_ENABLE) && (mThing->GetSnowDensity()))
		{
			RENDERINFO.mSnowDensity = mThing->GetSnowDensity();
			CMeshRenderer::Render(&offset, &ori, mesh, mThing, this, mUnknown24, false, 0x10);
		}

		if (alpha != 255)
			MESH_ALPHA = 255;
	}
}

//******************************************************************************************
void CRTMesh::RenderImposter()	// FUNCTION: 0x004dcea0
{
	FVector	pos = ENGINE.GetCamera()->GetPos();
	float	zoom = ENGINE.GetCamera()->GetZoom();
	if (MESH_LOD_IGNORE_ZOOM)
		zoom = 1.0f;
	FVector	thing_pos;
	thing_pos = mThing->GetRenderPos();
	pos = pos - thing_pos;
	float	dist = pos.MagnitudeSq();
	float	radius = GetBoundingBox()->mRadius;
	float	lod = (dist = (float)sqrt(dist)) / radius * mThing->GetRenderImposterBias() * MESH_LOD_BIAS * zoom;
	if (mImposter)
	{
		BOOL	far_enough = (lod > IMPOSTER_FADE_START);
		BOOL	forced = (GAME_RENDER_MODE == 4) | FORCE_OBJECT_IMPOSTERS;	// retail's bitwise or, here and in the test below (004dcfe3 or eax,edx; 004dcfe7 or edx,ecx)
		if (forced | far_enough)
		{
			SINT	alpha = (SINT)((lod - IMPOSTER_FADE_START) * 255.0f / (IMPOSTER_FADE_END - IMPOSTER_FADE_START));
			if ((forced) || (alpha > 255))
				alpha = 255;
			if (MESH_ALPHA != 255)
				alpha = alpha * MESH_ALPHA / 255;
			if (alpha > 255)
				alpha = 255;
			else if (alpha <= 0)
				return;

			SINT	frame = 0;
			if ((mImposter->mAnimated) && (mThing->GetRenderAnimation()) && (mThing->GetRenderAnimation() != 3))
			{
				SINT	frames = mImposter->mNumFrames;
				frame = ((SINT)(mThing->GetRenderFrame() * frames)) % frames;
			}
			mImposter->Render(mThing, alpha, frame);
		}
	}
}

//******************************************************************************************
// The meshes rendered last frame and not this one lose their effects.
void CRTMesh::UpdateRenderLists()	// FUNCTION: 0x004dd0c0
{
	while (sRenderedLastFrame)
	{
		CRTMesh	*rt = sRenderedLastFrame;
		for (SINT i = 0; i < rt->mNumEffects; i++)
		{
			if (rt->mEffects[i])
			{
				rt->mEffects[i]->Stop(FALSE);
				delete rt->mEffects[i];
				rt->mEffects[i] = NULL;
			}
		}
		rt->mPrevRendered = NULL;
		sRenderedLastFrame = rt->mNextRendered;
		rt->mNextRendered = NULL;
	}
	sRenderedLastFrame = sRenderedThisFrame;
	sRenderedThisFrame = NULL;
}

//******************************************************************************************
void CRTMesh::GetRTEmitter(char *name, SINT index, FVector &pos, FMatrix &ori, BOOL a, BOOL b)	// FUNCTION: 0x004dd160
{
	pos = ZERO_FVECTOR;
	ori = ID_FMATRIX;
	if (mMesh)
	{
		CMeshPart	*part = mMesh->GetEmitterSubpart(name, index);
		if (part)
			CMeshPart::EvaluatePose(this, mThing, part, &pos, &ori, a, b);
	}
}

//******************************************************************************************
void CRTMesh::GetMovieCameraPosition(FMatrix *ori, FVector *pos, float *fov, IRenderableThing *thing)	// FUNCTION: 0x004dd1e0
{
	if (mMesh)
	{
		SINT	frame;
		if ((thing) && (mMesh->mNumAnimModes))
		{
			SINT	anim = thing->GetRealAnimIndex();
			if (anim >= 0)
			{
				CAnimMode	*a = &mMesh->mAnimModes[anim];
				frame = (SINT)floor(thing->GetRenderFrame() * a->mFrameScaler) + a->mStart;
			}
			else
				frame = 0;
		}
		else
			frame = 0;

		CMeshPart	*part = mMesh->GetMovieCameraSubpart(frame);
		if (part)
		{
			SINT	f = frame % part->mNumVFrames;
			part->GetWorldTransforms(f, 0, 0, thing, ori, pos, 0, TRUE, 0);
			*fov = part->mHFrameFOVs[part->mVHFrameMap[f]];
			return;
		}
	}

	ori->Row[0] = FVector(1.0f, 0.0f, 0.0f);
	ori->Row[1] = FVector(0.0f, 1.0f, 0.0f);
	ori->Row[2] = FVector(0.0f, 0.0f, 1.0f);
	pos->X = 0;
	pos->Y = 0;
	pos->Z = 0;
	*fov = 90.0f;
}

//******************************************************************************************
SINT CRTMesh::GetMovieCameraNumber(IRenderableThing *thing)	// FUNCTION: 0x004dd370
{
	if (mMesh)
	{
		SINT	frame;
		if ((thing) && (mMesh->mNumAnimModes))
		{
			SINT	anim = thing->GetRealAnimIndex();
			if (anim >= 0)
			{
				CAnimMode	*a = &mMesh->mAnimModes[anim];
				frame = (SINT)floor(thing->GetRenderFrame() * a->mFrameScaler) + a->mStart;
			}
			else
				frame = 0;
		}
		else
			frame = 0;

		CMeshPart	*part = mMesh->GetMovieCameraSubpart(frame);
		if (part)
			return part->mPartNumber;
	}
	return 0;
}

//******************************************************************************************
// STAND-IN (layout; review F 2026-10-15): two zero returns (the early one on part and the last) are what give retail its zero register (xor edi,edi);
// one shared return loses 004dd400. The
// blocks now match, and the float sums of ori * vertex and the pop of esi still schedule a few instructions apart.
FVector CRTMesh::GetRandomSurfacePoint(SINT frame)	// FUNCTION: 0x004dd400
{
	if (mMesh)
	{
		CMeshPart	*part = NULL;
		FVector		vertex = mMesh->GetRandomVertexPos(&part);
		if (!part)
			return FVector(0, 0, 0);
		FMatrix	ori;
		FVector	offset;
		part->GetWorldTransforms(frame, frame, 0, mThing, &ori, &offset, 0, TRUE, 0);
		FVector	result = ori * vertex + offset;
		return result;
	}
	return FVector(0, 0, 0);
}

//******************************************************************************************
SINT CRTMesh::FindEffect(CMeshPart *part)	// FUNCTION: 0x004dd510
{
	for (SINT i = 0; i < mNumEffects; i++)
		if (mEffectPartPointers[i] == part)
			return i;
	return -1;
}

bool CRTMesh::GetEffectActive(SINT n)	// FUNCTION: 0x004dd540
{
	return mEffectActive[n];
}

SINT CRTMesh::GetEffectPart(SINT n)	// FUNCTION: 0x004dd550
{
	return mEffectParts[n];
}

SINT CRTMesh::GetEffectId(SINT n)	// FUNCTION: 0x004dd560
{
	return mEffectIds[n];
}

void *CRTMesh::GetEffect(SINT n)	// FUNCTION: 0x004dd570
{
	return mEffects[n];
}

void CRTMesh::SetEffectActive(SINT n, bool on)	// FUNCTION: 0x004dd580
{
	mEffectActive[n] = on;
}

void CRTMesh::SetEffect(SINT n, void *effect)	// FUNCTION: 0x004dd5a0
{
	mEffects[n] = (CParticleEffectLink *)effect;
}

//******************************************************************************************
// Nothing to update (CThing::UpdatePosition jumps here); the empty body is folded into the shared one (/OPT:ICF).

// FUNCTION: 0x0040c640
void CRTMesh::UpdatePosition()
{
}

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004db8a0 ?GetPartBoundingBox@CRTMesh@@UAEPAVCBoundingBox@@H@Z
// FUNCTION: 0x004db9f0 ?GetFrameIncrement@CRTMesh@@UAEMW4EAnimMode@@PAH@Z
