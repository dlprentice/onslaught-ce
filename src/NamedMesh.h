// NamedMesh.h: a named mesh, a scenery actor that plays its "Idle" animation and blocks the occupancy grid
// (NamedMesh.obj, 004bbbd0..004bc0af, and SpawnThing's case 11; Stuart's release has no copy).

#ifndef NAMEDMESH_H
#define NAMEDMESH_H

#include "actor.h"

enum { OID_CNamedMesh = 11, OID_CBuildingNamedMesh = 17 };	// SpawnThing's cases 11 and 17 (004bf2a1, 004bf3ae)

// A named mesh (0xe4 bytes; vtable 005dd5f0; NamedMesh.cpp).
DECLARE_THING_CLASS(CNamedMesh, CActor)
public:
	virtual void	HandleEvent(CEvent *event);							// 004bc070
	virtual	void	Shutdown();											// 004bc050
	virtual void	Init(CInitThing *init);								// 004bbcd0
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_CAN_BE_WALKED_ON); }
	virtual void	Move();												// 004bbda0
	virtual	SINT	GetRenderMesh() { return mMeshNo; }	// FUNCTION: 0x004183f0

	SINT			mMeshNo;											// +0xe0: the init's mesh number (Init 004bbcdc)
};

// A building's named mesh (vtable 005d910c; BuildingNamedMesh.obj, 004182a0..00418466).
DECLARE_THING_CLASS(CBuildingNamedMesh, CNamedMesh)
public:
					~CBuildingNamedMesh();								// 004183d0: out of line here, so this
																		// object emits the vtable's functions
	virtual	void	Shutdown();											// 00418450
	virtual void	Init(CInitThing *init);								// 00418390
	virtual	void	SetThingType(ULONG t);								// 00418420
};


#endif
