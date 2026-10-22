// BuildingNamedMesh.cpp: reconstructed from the object after Building.obj (004182a0..00418466); Stuart's release has
// no copy. A building's named mesh: a named mesh that is also a building for the thing-type tests.
//
// CNamedMesh::GetRenderMesh (004183f0, emitted here) receives IRenderableThing's secondary this, at complete
// object +8. Its read at [ecx+0xd8] therefore returns mMeshNo at complete-object +0xe0, not mLastMoveTime.
// The +8 RTTI/vtable ownership and the complete getter section agree with the ordinary inline member definition.

#include "common.h"
#include "NamedMesh.h"
#include "InitThing.h"
#include "world.h"

// FUNCTION: 0x004182a0 init:ZERO_FVECTOR
// FUNCTION: 0x004182c0 init:ID_FMATRIX

void CBuildingNamedMesh::Init(CInitThing *init)	// FUNCTION: 0x00418390
{
	init->mInitCST.mMinCollision = ECL_MESH;
	init->mInitCST.mNotSeekCollisionWithBF |= THING_TYPE_STATIONARY;
	SUPERTYPE::Init(init);
	StickToGround();
	WORLD.AddToOccupancy((CUnit *)this);
}

void CBuildingNamedMesh::SetThingType(ULONG t)	// FUNCTION: 0x00418420
{
	SUPERTYPE::SetThingType(t | THING_TYPE_BUILDING | THING_TYPE_STATIONARY);
}

void CBuildingNamedMesh::Shutdown()	// FUNCTION: 0x00418450
{
	WORLD.RemoveFromOccupancy((CUnit *)this);
	SUPERTYPE::Shutdown();
}

CBuildingNamedMesh::~CBuildingNamedMesh()	// FUNCTION: 0x004183d0
{
}
