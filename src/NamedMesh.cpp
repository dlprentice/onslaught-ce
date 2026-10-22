// NamedMesh.cpp: reconstructed from NamedMesh.obj (004bbbd0..004bc0af); Stuart's release has no copy. A named mesh
// is a scenery actor: it sticks to the ground, plays "Idle", keeps its place in the world's occupancy grid and, when
// its animation needs updating, schedules its own moves.

#include "common.h"
#include "NamedMesh.h"
#include "world.h"
#include "mesh.h"
#include "eventmanager.h"
#include "Event.h"
#include "InitThing.h"
#include "maths_integer.h"
#include "CLIParams.h"

// FUNCTION: 0x004bbbd0 init:ZERO_FVECTOR
// FUNCTION: 0x004bbbf0 init:ID_FMATRIX


FVector	LAST_NAMED_MESH_POS;						// 00807528: the last named mesh's position (name open)

//******************************************************************************************
void CNamedMesh::Init(CInitThing *init)	// FUNCTION: 0x004bbcd0
{
	mMeshNo = init->mMeshNo;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY;
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mUseFixedMeshTransforms = TRUE;

	SUPERTYPE::Init(init);
	StickToGround();
	SetAnimMode(mRenderThing->GetRTMesh()->GetAnimModeByName("Idle"), TRUE, TRUE);

	LAST_NAMED_MESH_POS = mPos;

	if (GetRenderAnimation() && GetRenderAnimation() != AM_INVALID)
		EVENT_MANAGER.AddEvent(MOVE, this, NEXT_FRAME);

	WORLD.AddToOccupancy((CUnit *)this);
}

//******************************************************************************************
void CNamedMesh::Move()	// FUNCTION: 0x004bbda0
{
	if (CLIPARAMS.mDeveloperMode == 1)
	{
		FMatrix rotation;
		rotation.MakeRotationCore(4, 0, 0);
		mOrientation = mOrientation * rotation;
	}
	SUPERTYPE::Move();
}

//******************************************************************************************
void CNamedMesh::Shutdown()	// FUNCTION: 0x004bc050
{
	WORLD.RemoveFromOccupancy((CUnit *)this);
	CComplexThing::Shutdown();
}

//******************************************************************************************
void CNamedMesh::HandleEvent(CEvent *event)	// FUNCTION: 0x004bc070
{
	switch (event->GetEventNum())
	{
	case MOVE:
		EVENT_MANAGER.AddEvent(MOVE, this, NEXT_FRAME);
		break;
	default:
		SUPERTYPE::HandleEvent(event);
	}
}

// The empty initializer this object emits (a lone ret), placed by its .CRT$XCU order.
// STAND-IN (link layout): retail's 8 unreferenced .bss bytes at the end of this object; no code in the specimen addresses them (an
// end-stage concession, applied 2026-10-16; sized from the data-event alignment measurement).
char gLinkBss_NamedMesh[8] = { 0 };
// FUNCTION: 0x004bbcc0 initorder:3
