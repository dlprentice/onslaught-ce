// ThingType.h: the thing-type bits (CThing::SetThingType ORs 1 at 004f3470, CComplexThing 0x80000001 at 00404110,
// CActor 0x80000003 at 00401540). CThing::GetCentrePos (004f3ac0) tests BUILDING|COMPONENT as 0x80100, then
// BATTLE_ENGINE (8) and GROUND_UNIT (0x200); CComponent::SetThingType (0050fcb0) ORs 0x80000 and CGroundUnit's
// (0050ed40) 0x200, which leaves 0x100 for BUILDING. CThing::Init marks a thing with a renderer 0x800000.

#ifndef THINGTYPE_H
#define THINGTYPE_H

#define THING_TYPE_NONE				0x00000000
#define THING_TYPE_THING			0x00000001
#define THING_TYPE_ACTOR			0x00000002
#define THING_TYPE_AMMUNITION		0x00000004	// Stuart's (CBattleEngine::Move passes it, 004094f8)
#define THING_TYPE_BATTLE_ENGINE	0x00000008
#define THING_TYPE_UNIT				0x00000010
#define THING_TYPE_UNKNOWN_20		0x00000020	// CHazard::SetThingType ORs it (00510224), CFeature too (00510114); the name is open
#define THING_TYPE_BUILDING			0x00000100
#define THING_TYPE_GROUND_UNIT		0x00000200
#define THING_TYPE_STATIONARY		0x00000020	// the mission scripts' onsldef.msl names the bits from here to 0x200000
#define THING_TYPE_ANIMAL			0x00000040
#define THING_TYPE_AIR_UNIT			0x00000400	// CAirUnit::SetThingType (0050e870)
#define THING_TYPE_MECH				0x00000800	// onsldef.msl has 2049; CMech::SetThingType ORs 0x800 (0050e9b0)
#define THING_TYPE_NAMED_MESH		0x00002000
#define THING_TYPE_INFANTRY			0x00004000
#define THING_TYPE_NAVAL			0x00008000
#define THING_TYPE_CUTSCENE			0x00010000	// onsldef.msl; CCutscene::SetThingType ORs it (0043ea64), IScript::PlayCutscene tests it (005358ad)
#define THING_TYPE_VEHICLE			0x00020000
#define THING_TYPE_EMPLACEMENT		0x00040000
#define THING_TYPE_COMPONENT		0x00080000
#define THING_TYPE_CAN_BE_WALKED_ON	0x00100000
#define THING_TYPE_CAN_DESTROY_TREES	0x00200000
#define THING_TYPE_UNKNOWN_04000000	0x04000000	// CCarrier::SetThingType (0050ea80); the name is open
#define THING_TYPE_UNKNOWN_08000000	0x08000000	// the HUD draws its radar blob large (CHud::GetRadarBlob 0048583a); the name is open
#define THING_TYPE_UNKNOWN_10000000	0x10000000	// CSubmarine::SetThingType (0050eb70); the name is open
#define THING_TYPE_UNKNOWN_20000000	0x20000000	// CSquad::SetThingType (004e5df0: with COMPLEX_THING and THING, 0xa0000001); open
#define THING_TYPE_UNKNOWN_40000000	0x40000000	// every unit but the boat (CGroundUnit 0050ed40, CAirUnit 0050e870); open
#define THING_TYPE_FEATURE			0x00400000	// CFeature::SetThingType ORs 0x500000 with CAN_BE_WALKED_ON (00510114)
#define THING_TYPE_RENDERABLE		0x00800000
#define THING_TYPE_EXPLOSION		0x01000000	// CExplosion::SetThingType ORs it (0044c18a); the name is ours
#define THING_TYPE_TREE				0x02000000	// Stuart's (CBattleEngine::Hit tests TREE|CAN_BE_WALKED_ON, 00407382)
#define THING_TYPE_COMPLEX_THING	0x80000000
#define THING_TYPE_EVERYTHING		0xffffffff

typedef ULONG EThingType;					// a set of the THING_TYPE_ bits (Stuart's name; CGame::FillOutEndLevelData)

#endif
