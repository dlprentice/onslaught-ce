// oids.cpp: reconstructed from oids.obj (004befa0..004c004f); Stuart's release has no copy. SpawnThing makes a new
// thing of an object id; the object also holds the retail copies of the inline members of the classes it constructs
// (their vtables are emitted here). The classes without a header yet are declared below from their RTTI vtables
// (config/classes.tsv), SpawnThing's allocations (sizes) and the functions of this object; their out-of-line members
// live in their own objects. The allocations' __LINE__s are the retail ones (40..72); the #lines place them.

#include "common.h"
class CLevelScriptThing;class CWingmanStart;class CRadar;class CSpawnerThing;class CTree;class CRocket;class CSpawnPoint;class CSafeSide;class CWaypoint;class CNamedMesh;class CSphereTrigger;class CBattleEngineInitThing;class CEscapePod;class CUnitInitThing;class CDebris;class CCutscene;class CBuildingNamedMesh;class CBuilding;class CBattleEngine;class CAnimal;class CActor;	// STAND-IN (end-stage RTTI order): retail's ??_R4 order implies these type records were created in this order (an unidentified header mention); found by an RTTI-order search
#include "unit.h"
#include "BattleEngine.h"
#include "Start.h"
#include "Cutscene.h"
#include "InitThing.h"
#include "tree.h"
// The object ids of the classes below (SpawnThing's switch, 004bf0b7: each case constructs its class).
enum
{
	OID_CRadar = 10,
	OID_CRocket = 16,				// Rocket.h's _GetClassID
	OID_CWaypoint = 18,
	OID_CShell = 21,
	OID_CAnimal = 29,
	OID_CDebris = 31,
	OID_CWingmanStart = 38,			// WingmanStart.h's _GetClassID
	OID_CEscapePod = 43,			// EscapePod.h's _GetClassID
};

//******************************************************************************************
// A radar dish (0x284 bytes; vtable 005dd788).
DECLARE_THING_CLASS(CRadar, CUnit)
public:
	virtual void	Init(CInitThing *init);								// 004d6360
	virtual	void	SetThingType(ULONG t) { SUPERTYPE::SetThingType(t | THING_TYPE_STATIONARY); }
	virtual float	GetMaxVelocity() { return 0.0f; }
	virtual float	GetRadius() { return 0.6f; }
	virtual float	Gravity() { return 0; }
	virtual void	Move();												// 004d63c0

	UBYTE			mUnknown250[0x284 - 0x250];
};

#include "NamedMesh.h"
#include "waypoint.h"
#include "SpawnerThng.h"
#include "LevelScriptThing.h"
#include "SphereTrigger.h"
#include "SafeSide.h"
#include "WingmanStart.h"
#include "Rocket.h"
#include "SpawnPoint.h"
#include "EscapePod.h"
#include "ltshell.h"
#include "shell.h"

// FUNCTION: 0x004befa0 init:ZERO_FVECTOR
// FUNCTION: 0x004befc0 init:ID_FMATRIX

//******************************************************************************************
// An animal (0xe0 bytes; vtable 005d8698).
DECLARE_THING_CLASS(CAnimal, CComplexThing)
public:
	virtual			~CAnimal();											// the ??_G 004041f0
	virtual void	HandleEvent(CEvent *event);							// 0044c140
	virtual void	Init(CInitThing *init);								// 00403d30
	virtual	FVector	GetVelocity();										// 00404120
	virtual	FVector	GetOldPos();										// 004041a0
	virtual FMatrix	GetOldOrientation();								// 004041d0
	virtual void	Render(DWORD flags);								// 004045d0
	virtual void	Move();												// 66 (00404210)
	virtual void	SetVelocity2(const FVector &v);						// 67 (00404150)
	virtual void	AddVelocity2(const FVector &v);						// 68 (00404170)

	UBYTE			mUnknown7C[0xe0 - 0x7c];
};

// Debris (0x84 bytes; vtable 005daf10; debris.cpp).
DECLARE_THING_CLASS(CDebris, CComplexThing)
public:
	virtual			~CDebris();										// debris.cpp's (00441320); declared so this view emits no implicit copy
	virtual void	Init(CInitThing *init);								// 004411a0
	virtual void	Render(DWORD flags);								// 004413a0
	virtual	void	RenderImposter();									// 00441420

	UBYTE			mUnknown7C[0x84 - 0x7c];
};

//******************************************************************************************
CThing *SpawnThing(SINT inID, BOOL inReportErrors)	// FUNCTION: 0x004bf090
{
	switch (inID)
	{
	// The ids made elsewhere (units by UPhysicsManager::SpawnThing) each had a case of their own: retail's table
	// is a direct one from id 2 (004bf0a9, 004bf92c), which VC6 builds only when every id has its own case; one
	// shared case gives a two-level table.
	case 2:	return NULL;
	case 4:	return NULL;
	case 5:	return NULL;
	case 6:	return NULL;
	case 8:	return NULL;
	case 9:	return NULL;
	case 12:	return NULL;
	case 13:	return NULL;
	case 14:	return NULL;
	case 20:	return NULL;
	case 22:	return NULL;
	case 23:	return NULL;
	case 24:	return NULL;
	case 28:	return NULL;
	case 30:	return NULL;
	case 32:	return NULL;
	case 33:	return NULL;
	case 34:	return NULL;
	case 35:	return NULL;
	case 39:	return NULL;
	case 40:	return NULL;
	case 42:	return NULL;
#line 40
	case OID_CBattleEngine:			return new(MT_THING) CBattleEngine;
#line 43
	case OID_CTree:					return new(MT_TREE_THING) CTree;
#line 45
	case OID_CRadar:				return new(MT_THING) CRadar;
	case OID_CNamedMesh:			return new(MT_THING) CNamedMesh;
#line 50
	case OID_CStart:				return new(MT_THING) CStart;
	case OID_CRocket:				return new(MT_THING) CRocket;
	case OID_CBuildingNamedMesh:	return new(MT_THING) CBuildingNamedMesh;
	case OID_CWaypoint:				return new(MT_THING) CWaypoint;
	case OID_CSpawnerThing:			return new(MT_THING) CSpawnerThing;
#line 56
	case OID_CShell:				return new(MT_THING) CShell;
#line 59
	case OID_CBuilding:				return new(MT_THING) CBuilding;
	case OID_CCutscene:				return new(MT_THING) CCutscene;
	case OID_CLevelScriptThing:		return new(MT_THING) CLevelScriptThing;
#line 63
	case OID_CAnimal:				return new(MT_THING) CAnimal;
	case OID_CDebris:				return new(MT_THING) CDebris;
#line 68
	case OID_CSphereTrigger:		return new(MT_THING) CSphereTrigger;
	case OID_CSafeSide:				return new(MT_THING) CSafeSide;
	case OID_CWingmanStart:			return new(MT_THING) CWingmanStart;
	case OID_CSpawnPoint:			return new(MT_THING) CSpawnPoint;
	case OID_CEscapePod:			return new(MT_THING) CEscapePod;
	}
	return NULL;
}

// FUNCTION: 0x004bfa00 ?GetRenderOrientation@CThing@@UAE?AVFMatrix@@XZ
// FUNCTION: 0x004bfa20 ?GetSoundOrientation@CThing@@UAE?AVFMatrix@@XZ
// FUNCTION: 0x004bfa40 ?GetOldOrientation@CThing@@UAE?AVFMatrix@@XZ
// FUNCTION: 0x004bfa80 ?_GetClassName@CTree@@UAEPADXZ
// FUNCTION: 0x004bfa90 ?SetThingType@CTree@@UAEXK@Z
// FUNCTION: 0x004bfaa0 ?GetSoundMaterial@CTree@@UAEHXZ
// FUNCTION: 0x004bfab0 ?Render@CTree@@UAEXK@Z
// FUNCTION: 0x004bfb00 ?_GetClassName@CRadar@@UAEPADXZ
// FUNCTION: 0x004bfb10 ?SetThingType@CRadar@@UAEXK@Z
// FUNCTION: 0x004bfb20 ?GetRadius@CRadar@@UAEMXZ
// FUNCTION: 0x004bfb30 ?_GetClassName@CNamedMesh@@UAEPADXZ
// FUNCTION: 0x004bfb40 ?SetThingType@CNamedMesh@@UAEXK@Z
// FUNCTION: 0x004bfb50 ?_GetClassName@CWaypoint@@UAEPADXZ
// FUNCTION: 0x004bfb60 ?SetThingType@CWaypoint@@UAEXK@Z
// FUNCTION: 0x004bfb70 ?_GetClassName@CSpawnerThing@@UAEPADXZ
// FUNCTION: 0x004bfb80 ?_GetClassName@CLevelScriptThing@@UAEPADXZ
// FUNCTION: 0x004bfb90 ?_GetClassID@CLevelScriptThing@@UAEHXZ
// FUNCTION: 0x004bfba0 ?_GetClassName@CSphereTrigger@@UAEPADXZ
// FUNCTION: 0x004bfbb0 ?_GetClassID@CSphereTrigger@@UAEHXZ
// FUNCTION: 0x004bfbc0 ?GetRadius@CSphereTrigger@@UAEMXZ
// FUNCTION: 0x004bfbd0 ?GetContainedInside@CSphereTrigger@@UAEPAXXZ
// FUNCTION: 0x004bfbe0 ?SetThingType@CSphereTrigger@@UAEXK@Z
// FUNCTION: 0x004bfbf0 ?_GetClassName@CSafeSide@@UAEPADXZ
// FUNCTION: 0x004bfc00 ?_GetClassID@CSafeSide@@UAEHXZ
// FUNCTION: 0x004bfc10 ?GetAllegiance@CSafeSide@@UAEHXZ
// FUNCTION: 0x004bfc20 ?_GetClassName@CWingmanStart@@UAEPADXZ
// FUNCTION: 0x004bfc30 ?_GetClassID@CWingmanStart@@UAEHXZ
// FUNCTION: 0x004bfc40 ?_GetClassName@CRocket@@UAEPADXZ
// FUNCTION: 0x004bfc50 ?GetMaxVelocity@CEscapePod@@UAEMXZ
// FUNCTION: 0x004bfc70 ?SetVulnerable@CRocket@@UAEXH@Z
// FUNCTION: 0x004bfc80 ?_GetClassName@CSpawnPoint@@UAEPADXZ
// FUNCTION: 0x004bfc90 ?_GetClassID@CSpawnPoint@@UAEHXZ
// FUNCTION: 0x004bfca0 ?Activate@CSpawnPoint@@UAEXXZ
// FUNCTION: 0x004bfcb0 ?Deactivate@CSpawnPoint@@UAEXXZ
// FUNCTION: 0x004bfcc0 ?_GetClassName@CEscapePod@@UAEPADXZ
// FUNCTION: 0x004bfcd0 ?_GetClassID@CEscapePod@@UAEHXZ
// FUNCTION: 0x004bfce0 ??_GCTree@@UAEPAXI@Z
// FUNCTION: 0x004bfd40 ??_GCRocket@@UAEPAXI@Z
// FUNCTION: 0x004bfd60 ??_GCWaypoint@@UAEPAXI@Z
// FUNCTION: 0x004bfd80 ??_GCSpawnerThing@@UAEPAXI@Z
// FUNCTION: 0x004bfda0 ??_GCSphereTrigger@@UAEPAXI@Z
// FUNCTION: 0x004bfdc0 ??_GCWingmanStart@@UAEPAXI@Z
// FUNCTION: 0x004bfde0 ??_GCEscapePod@@UAEPAXI@Z
// FUNCTION: 0x004bf9e0 ??0CUnitInitThing@@QAE@XZ
// FUNCTION: 0x004bfa60 ??0CWorldLineColReport@@QAE@XZ
// FUNCTION: 0x004bfd20 ??0CActor@@QAE@XZ
// FUNCTION: 0x004bfe10 ??1CRocket@@UAE@XZ
// FUNCTION: 0x004bfe70 ??1CWaypoint@@UAE@XZ
// FUNCTION: 0x004bfed0 ??1CSpawnerThing@@UAE@XZ
// FUNCTION: 0x004bff40 ??1CSphereTrigger@@UAE@XZ
// FUNCTION: 0x004bffa0 ??1CWingmanStart@@UAE@XZ
// FUNCTION: 0x004c0000 ??1CEscapePod@@UAE@XZ
// FUNCTION: 0x004bff30 ??1CLevelScriptThing@@UAE@XZ

// Inline functions from headers that this object emits; its vtables and matched code place them.
// FUNCTION: 0x004040f0 ?_GetClassName@CAnimal@@UAEPADXZ
// FUNCTION: 0x00404100 ?_GetClassID@CAnimal@@UAEHXZ
// FUNCTION: 0x004041f0 ??_GCAnimal@@UAEPAXI@Z
// FUNCTION: 0x00418400 ?_GetClassName@CBuildingNamedMesh@@UAEPADXZ
// FUNCTION: 0x00418430 ??_GCBuildingNamedMesh@@UAEPAXI@Z
