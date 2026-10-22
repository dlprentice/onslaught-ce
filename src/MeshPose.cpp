// MeshPose.cpp: reconstructed from MeshPose.obj (004b4ab0..004b512f), with EvaluatePose restored from
// Stuart's October 14 source drop. It builds a render thing's cached pose (meshpose.h): each part's transform.
#include "common.h"
#include "mesh.h"
#include "meshpose.h"
#include "renderablething.h"
#include "Motioncontroller.h"
#include "game.h"
#include "RenderThing.h"
#include "mathsconst.h"

// FUNCTION: 0x004b4ab0 init:ZERO_FVECTOR
// FUNCTION: 0x004b4ad0 init:ID_FMATRIX

//******************************************************************************************
// A part and its children, each given the transform of its parent.
void CMeshPose::Build(FVector pos, FMatrix ori, CMeshPart *part, IRenderableThing *thing, CRenderThing *rt, SINT frame)	// FUNCTION: 0x004b4ba0
{
	CMeshPart	*parent;
	SINT		f = frame;
	CMeshPart::EvaluateAnimatedTransform(thing, &pos, &ori, part, &parent, &f);
	mOrientations[part->mPartNumber] = ori;
	mPositions[part->mPartNumber] = pos;
	mParents[part->mPartNumber] = parent->mPartNumber;
	mFrames[part->mPartNumber] = f;
	for (SINT i = 0; i < part->mNumChildren; i++)
		Build(pos, ori, part->mChildren[i], thing, rt, f);
}

//******************************************************************************************
// Rebuilds the pose once per render frame at most, and otherwise only every 30 frames unless forced.
BOOL CMeshPose::Update(CMesh *mesh, IRenderableThing *thing, CRenderThing *rt, BOOL force)	// FUNCTION: 0x004b4cd0
{
	SINT	frame = GAME.GetRenderFrameNumber();
	if (frame == mLastFrame)
		return FALSE;
	if ((frame < mLastFrame + 30) && (!force))
		return FALSE;

	CMotionController	*mc = thing->GetMotionController();
	float	time = 0;
	SINT	anim = thing->GetRealAnimIndex();
	mLastFrame = -9999;
	if ((mesh->mNumAnimModes) && (anim >= 0))
	{
		CAnimMode	*ma = &mesh->mAnimModes[anim];
		if (ma)
		{
			float	t = thing->GetRenderFrame();
			time = ma->mFrameScaler * t + ma->mStart;
		}
	}
	if (mc)
		mc->Unknown8();

	Build(ZERO_FVECTOR, ID_FMATRIX, mesh->mParts[0], thing, rt, 0);
	mLastFrame = frame;
	mLastTime = time;
	return TRUE;
}

//******************************************************************************************
// Adapted from Stuart's MeshPose.cpp:89-137, CMeshPose::GetPoseTransforms, at c3d8ac46.
// Keep the existing ABI/names: his output references are our pointer parameters, GetPose() is mPose,
// and his default GetWorldTransforms argument is written explicitly. The source's separate guards,
// direct frame read, product-first position sum and else-scoped fallback reproduce all retail bytes
// and references without the former pose self-copy branch or named-position-reference stand-ins.
void CMeshPart::EvaluatePose(CRenderThing *rt, IRenderableThing *thing, CMeshPart *part, FVector *pos, FMatrix *ori, BOOL local, BOOL force) // FUNCTION: 0x004b4de0
{
    *pos = ZERO_FVECTOR;
    *ori = ID_FMATRIX;
    CMeshPose *pose = rt->mPose;
    CMesh *mesh = rt->GetRTMesh();
    if (!mesh)
        return;
    if (!part)
        return;
    if ((pose) && (GAME.mRenderFrameNumber > 1))
    {
        pose->Update(mesh, thing, rt, force);
        *pos = pose->mPositions[part->mPartNumber];
        *ori = pose->mOrientations[part->mPartNumber];
        if (!local)
        {
            FVector tpos = thing->GetRenderPos();
            FMatrix tori = thing->GetRenderOrientation();
            *pos = (tori * *pos) + tpos;
            *ori = tori * *ori;
        }
    }
    else
    {
        if (part)
        {
            SINT frame;
            float fraction;
            if (thing->GetRealAnimIndex() >= 0)
                frame = part->ResolveFrameNumber(thing->GetRenderFrame(), thing->GetRealAnimIndex(), &fraction, thing->GetMotionController());
            else
                frame = 0;
            part->GetWorldTransforms(frame, frame, 0.0f, thing, ori, pos, local ? true : false, true, 0);
        }
    }
}
