// MeshRenderer.cpp: reconstructed from MeshRenderer.obj (004b5130..004b6baf); Stuart's release has no copy.
#include "common.h"
#define MESHRENDERER_CPP
#include "meshrenderer.h"
#include "mesh.h"
#include "meshpose.h"
#include "RenderThing.h"
#include "Motioncontroller.h"
#include "renderablething.h"
#include "mathsconst.h"
#include "rtmesh.h"
#include "ParticleManager.h"
#include "engine.h"
#include "renderinfo.h"
#include "PolyBucket.h"

// FUNCTION: 0x004b5130 init:ZERO_FVECTOR
// FUNCTION: 0x004b5150 init:ID_FMATRIX

// FUNCTION: 0x004b5220 init:gMeshRendererVector
FVector		gMeshRendererVector = ZERO_FVECTOR;		// 00704e38 (read by CMeshRenderer's core, 00549b50)
ERenderMode	CMeshRenderer::mRenderMode = R_Normal;				// 00704e48
SINT		MESH_ALPHA = 255;						// 0063012c: the mesh's alpha (meshrenderer.h; 255 draws opaque)
UINT		MESH_SURFACE_DETAIL = 100;				// 00630130: the layers after the first, in percent; 0 draws only the first
											// (DXMeshVB); 100 unless a mesh is being drawn (CRTMesh, CHud)

//******************************************************************************************
// Meant to wrap a value into 0..1; the steps go the wrong way, so a value outside never returns.
float WrapUnit(float v)	// FUNCTION: 0x004b5250
{
	while (v < 0)
		v -= 1.0f;
	while (v > 1.0f)
		v += 1.0f;
	return v;
}

//******************************************************************************************
float AbsFloat(float v)	// FUNCTION: 0x004b52a0
{
	if (v < 0)
		return -v;
	return v;
}

//******************************************************************************************
// A normal packed into a colour: each axis from -1..1 into 1..255, and the alpha from 0..1.
DWORD PackNormal(const FVector *n, float alpha)	// FUNCTION: 0x004b52c0
{
	SINT	x = (SINT)(n->X * 127.0f + 128.0f);
	SINT	a = (SINT)(alpha * -255.0f);
	SINT	y = (SINT)(n->Y * 127.0f + 128.0f);
	SINT	z = (SINT)(n->Z * 127.0f + 128.0f);
	return ((((x - (a << 8)) << 8) + y) << 8) + z;
}
SINT	gMeshRendererAlpha = 0;	float	gMeshRendererOffset = 0.0f;	class CDXTexture	*gMeshRendererTexture = NULL;	// 00704e4c/50/54 (names ours): render modes 2, 6 and 8's alpha, back-face offset and texture
float	gMeshRendererFrame = 0.0f;		// 00704e58 (name ours): the frame drawn when a render's flags have bit 3
SINT	gMeshRendererAnim = 0;		// 00704e5c (name ours): and its animation
BOOL	CUTSCENE_RENDERING = FALSE;		// 00704e60: set around a cutscene's mesh render (RTCutscene.cpp)

//******************************************************************************************
// A part's transform at the thing's animation frame, added onto pos and ori (the parent's): the key interpolated
// between the frame's and the next, through the motion controller, and swapped for the damaged part the controller
// picks. parent returns the part to draw. The fraction is ResolveFrameNumber's spelling (floorf); p and o are reused for the
// damaged part's transform (one frame slot each, as retail's 0x174 frame has them).
// f0 and t start at 0, as the other locals do, and every if (mc) body is braced: both are Stuart's habits (redundant
// initialisers, eventmanager.cpp:330 and Music.cpp:407; a braced single statement after an if, 242 of the 924 single-statement
// ifs in references/Onslaught). They are also what the function's own size needs: the hierarchy arm's matrix product is
// expanded, as in retail, only at an own size of 974 or a little more (FMatrix * FMatrix then has 595 >= 594, and the
// plain arm's += 0 < 51). Measured own sizes: as written 974, without the braces 964, without the initialisers 966,
// without both 956; the plain form differs in 877 bytes.
void CMeshPart::EvaluateAnimatedTransform(IRenderableThing *thing, FVector *pos, FMatrix *ori, CMeshPart *part, CMeshPart **parent, SINT *frame)	// FUNCTION: 0x004b5330
{
	CMotionController	*mc = NULL;
	if (thing)
		mc = thing->GetMotionController();

	SINT	anim = -1;
	float	time = 0;
	if (thing)
		anim = thing->GetRealAnimIndex();
	if ((*frame & 8) == 8)
		anim = gMeshRendererAnim;

	SINT	f0 = 0;
	float	t = 0;
	if ((part->mNumVFrames > 1) && (part->mBaseMesh->mNumAnimModes) && (anim >= 0))
	{
		CAnimMode	*ma = &part->mBaseMesh->mAnimModes[anim];
		if (ma)
		{
			if (thing)
				time = thing->GetRenderFrame();
			if ((*frame & 8) == 8)
				time = gMeshRendererFrame;
			time = ma->mFrameScaler * time;
			time = ma->mStart + time;
			if (mc)
			{
				mc->AdjustFrame(part, &time);
			}
			f0 = SINT(floor(time));
			t = time - floorf(time);
		}
		else
		{
			TRACE("Warning - got null animmode in mesh %s in RenderMeshHierarchy\n", part->mBaseMesh->mName);
			if (mc)
			{
				mc->AdjustFrame(part, &time);
			}
			f0 = SINT(floor(time));
			t = time - floorf(time);
		}
	}
	else
	{
		if (mc)
		{
			mc->AdjustFrame(part, &time);
		}
		f0 = SINT(floor(time));
		t = time - floorf(time);
	}
	if (f0 >= part->mNumVFrames)
		f0 = part->mNumVFrames - 1;

	if (mc)
	{
		mc->Unknown3(part, pos, ori, true);
	}
	BOOL	loop = TRUE;
	if (mc)
	{
		loop = mc->Unknown9();
	}

	UBYTE	k0 = part->mVHFrameMap[f0];
	SINT	f1 = f0 + 1;
	if (f1 >= part->mNumVFrames)
	{
		if (loop)
			f1 = 0;
		else
			f1 = part->mNumVFrames - 1;
	}
	UBYTE	k1 = part->mVHFrameMap[f1];
	FVector	p;
	p = part->GetKeyPosition(k0) + (part->GetKeyPosition(k1) - part->GetKeyPosition(k0)) * t;
	FMatrix	o;
	o = part->GetHFrameOrientation(k0) + (part->GetHFrameOrientation(k1) - part->GetHFrameOrientation(k0)) * t;

	if ((!thing) || (thing->RenderUseHierarchy(part->mPartNumber)))
	{
		*pos += *ori * p;
		*ori = *ori * o;
	}
	else
	{
		*pos += p;
		*ori = o;
	}
	if (mc)
	{
		mc->GetPartTransform(part, pos, ori, true);
		mc->Unknown6(part, frame);
	}

	*parent = part;
	p = *pos;
	o = *ori;
	if (mc)
	{
		SINT	damage = mc->Unknown7(part);
		if (damage)
		{
			for (; damage > 0; damage--)
				*parent = (*parent)->mNextMeshPartInChain;
			SINT	next = f0 + 1;
			if (next >= (*parent)->mNumVFrames)
				next = (*parent)->mNumVFrames - 1;
			(*parent)->GetWorldTransforms(f0, next, t, thing, &o, &p, true, true, false);
		}
	}
	*pos = p;
	*ori = o;
}

//******************************************************************************************
// A part and its children at the thing's current frame, from its parent's transform (the root's from the thing's).
// FUNCTION: 0x004b5ad0 ?RenderPart@CMeshRenderer@@SAXVFVector@@VFMatrix@@PAVCMeshPart@@PAVIRenderableThing@@PAVCRenderThing@@HH@Z
void CMeshRenderer::RenderPart(FVector pos, FMatrix ori, CMeshPart *part, IRenderableThing *thing, CRenderThing *rt, SINT a, SINT b)
{
	CMeshPart	*draw;
	SINT		f = b;
	if ((!part->mParent) && (thing))
	{
		pos = thing->GetRenderOrientation() * pos + thing->GetRenderPos();
		ori = thing->GetRenderOrientation() * ori;
	}
	CMeshPart::EvaluateAnimatedTransform(thing, &pos, &ori, part, &draw, &f);
	RenderPart(&pos, &ori, draw, thing, rt, a, b);
	for (SINT i = 0; i < part->mNumChildren; i++)
		RenderPart(pos, ori, part->mChildren[i], thing, rt, a, b);
}

//******************************************************************************************
// Render each cached pose in the incoming frame; retail composes frame * pose at 004b61c7.
void CMeshRenderer::RenderPose(FVector pos, FMatrix ori, CMesh *mesh, IRenderableThing *thing, CRenderThing *rt, SINT b)	// FUNCTION: 0x004b5e80
{
	CMeshPose	*pose = rt->mPose;
	if (!pose)
		return;

	pos = thing->GetRenderOrientation() * pos + thing->GetRenderPos();
	ori = thing->GetRenderOrientation() * ori;
	for (SINT i = 0; i < mesh->mNumParts; i++)
	{
		if (pose->mParents[i] != -1)
		{
			FVector	p;
			p = pose->mPositions[i];
			FMatrix	o = pose->mOrientations[i];
			p = ori * p + pos;
			const FMatrix &world = ori; o = world * o;
			RenderPart(&p, &o, mesh->mParts[pose->mParents[i]], thing, rt, 0, b | pose->mFrames[i]);
		}
	}
}

//******************************************************************************************
void CMeshRenderer::Render(FVector *pos, FMatrix *ori, CMesh *mesh, IRenderableThing *thing, CRenderThing *rt, SINT a, bool lods, SINT b)	// FUNCTION: 0x004b6260
{
	while (mesh)
	{
		if (mesh->mNumParts > 0)
		{
			if ((rt) && (rt->mPose))
			{
				rt->mPose->Update(mesh, thing, rt, TRUE);
				RenderPose(*pos, *ori, mesh, thing, rt, b);
			}
			else
				RenderPart(*pos, *ori, mesh->mParts[0], thing, rt, a, b);
		}
		if ((!lods) || (!mesh->mNextInChain))
			break;
		mesh = mesh->mNextInChain;
	}
}

//******************************************************************************************
SINT	gMeshRendererHitEffect = 0;	// 00704e64 (name ours): the part's hit effect, for the renderer's layer passes (0054d530)

// Select a debug colour while retaining the virtual query before the collision override.
static inline DWORD GetPartDebugColour(IRenderableThing *thing, bool collision)
{
	DWORD colour = 0xff7f7f7f;
	if (thing)
		colour = thing->GetRenderColour();
	if (collision)
		colour = 0xff008080;
	return colour;
}

// One part at a pose. An effect emitter (type 5) moves its particle effect; the invisible types (2, 4 and 5) are drawn
// only as debug cuboids; a collision stand-in (type 6) draws its collision part.
// FUNCTION: 0x004b6350 ?RenderPart@CMeshRenderer@@SAXPAVFVector@@PAVFMatrix@@PAVCMeshPart@@PAVIRenderableThing@@PAVCRenderThing@@HH@Z
void CMeshRenderer::RenderPart(FVector *pos, FMatrix *ori, CMeshPart *part, IRenderableThing *thing, CRenderThing *rt, SINT a, SINT b)
{
	if (((part->mPartType == 2) || (part->mPartType == 4)) && (!(b & 4)))
		return;
	if ((b & 1) == 1)
		return;
	if (((b & 0x10) == 0x10) && (thing) && (thing->GetRealAnimIndex() != -1))
	{
		if ((part->mNumHFrames > 1) || (part->mNumAFrames > 1))
			return;
	}

	if (part->mPartType == 5)
	{
		if (rt)
		{
			// the effect lists are CRTMesh's; an emitter's part id is its part (CMesh::Deserialize turns the index
			// into the part)
			CRTMesh	*rtm = (CRTMesh *)rt;
			SINT	n = rt->FindEffect(part);
			if ((n != -1) && (rtm->mEffectTypes[n]))
			{
				if (!rtm->mEffects[n])
				{
#line 519
					rtm->mEffects[n] = new(MEMTYPE_PARTICLE) CParticleEffectLink;
					rtm->mEffects[n]->mEffect = NULL;			// again after the constructor (004b6496)
					PARTICLE_MANAGER.CreateEffect(rtm->mEffectTypes[n], rtm->mEffects[n], FVector(0, 0, 0), TRUE, FALSE);
					rtm->mEffects[n]->SetPos(FVector(0, 0, 100.f));
					rtm->mEffects[n]->SetPos(FVector(0, 0, 100.f));
					rtm->mEffectActive[n] = false;
				}
				if (!rt->GetEffectActive(n))
				{
					SINT	id = rt->GetEffectId(n);
					rt->SetEffectActive(n, true);
					CParticleEffectLink	*link = (CParticleEffectLink *)rt->GetEffect(n);
					if (link)
					{
						// Retail tests id with or-and-compare pairs (004b674a..004b6759, 004b68b7). VC6 does not fold
						// id == 0 || id == 1 into that form, so the source wrote it this way: ids 0 and 1 follow the
						// part's orientation, and 2 takes it once, when the effect starts.
						link->SetPos(*pos);
						link->SetPos(*pos);
						if (((id | 1) == 1) || ((id | 2) == 2))
						{
							link->SetOrientation(*ori);
							link->SetOrientation(*ori);
						}
					}
				}
				else
				{
					CParticleEffectLink	*link = (CParticleEffectLink *)rt->GetEffect(n);
					if (link)
					{
						SINT	id = rt->GetEffectId(n);
						link->SetPos(*pos);
						link->SetPos(*pos);
						if ((id | 1) == 1)
						{
							link->SetOrientation(*ori);
							link->SetOrientation(*ori);
						}
					}
				}
			}
		}
		if (!(b & 4))
			return;
	}

	bool	collision = false;
	if (part->mPartType == 6)
	{
		part = part->mReferenceTo;
		collision = true;
	}
	RENDERINFO.SetWorld(*pos, *ori);
	SINT	hit;
	if ((thing) && ((hit = thing->GetShouldDoHitEffect(part->mPartNumber)) > 0))
	{
		b |= 0x20;
		gMeshRendererHitEffect = hit;
	}
	else
		b &= ~0x20;

	if ((!(ENGINE.GetDrawDebugStuff() & DRAW_OBJECTS_AS_CUBOIDS)) && ((b & 4) != 4))
	{
		if (ENGINE.mDrawPolyBuckets)
		{
			if (part->mPolyBucket)
				part->mPolyBucket->DebugRender();
			return;
		}
		CMotionController	*mc = NULL;
		if (thing)
			mc = thing->GetMotionController();
		RenderMeshCore(*pos, ori, part, thing, rt, a, b, mc, collision);
		return;
	}

	else
	{
		FVector	centre = part->mBoundingBox->mOrigin;
		FMatrix	o = ID_FMATRIX;
		DWORD col = GetPartDebugColour(thing, collision);
		// A zero-radius box gets its three fallback axes directly (004b6b12).
		FVector	axes;
		if (part->mBoundingBox->mRadius == 0)
			axes.Set(0.1f, 0.1f, 0.1f);
		else
			axes = part->mBoundingBox->mAxes;
		ENGINE.DrawDebugCuboid(col, axes, centre, o, ENGINE.GetDefaultTexture());
	}
}
// STAND-IN (link layout): retail's 8 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_MeshRenderer[8] = { 0 };
