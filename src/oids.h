// oids.h: the object class ids (Stuart's header name; his file is not in the release). Each value names its retail
// evidence.

#ifndef OIDS_H
#define OIDS_H

enum EOID
{
	OID_CThing = 0,
	OID_CStart = 15,			// CGame::PostLoadProcess (0046d11b)
	OID_CActor = 1,
	OID_CBattleEngine = 3,		// CBattleEngine's slot 8 is the shared return-3 (00405ee0)
	OID_CUnit = 8,				// SpawnInitThing makes a CUnitInitThing for it (0050dd71, 0048c650)
	OID_CSquad = 28,			// CSquad::_GetClassID (004e5de0)
	OID_CRound = 4,				// CRound::_GetClassID is the shared return-4 004db8c0
	OID_CExplosion = 22,		// CExplosion::_GetClassID returns it (0050ffc0)
	OID_CFeature = 35,			// 00510100
	OID_CHazard = 39,			// 00510210
	OID_CComplexThing = 40,
#ifdef RADAR_CPP	// radar.cpp only, as OID_CDebris
	OID_CRadar = 10,			// SpawnThing's case 10 constructs one (004bf0b7)
#endif
#ifdef ANIMAL_CPP	// animal.cpp only, as OID_CDebris
	OID_CAnimal = 29,			// CAnimal::_GetClassID (00404100)
#endif
#ifdef DEBRIS_CPP	// debris.cpp only, so the other objects keep their declaration counts
	OID_CDebris = 31,			// CDebris::_GetClassID (00441370)
#endif
	OID_CMissile = 42,			// 0050f8a0
	// SpawnInitThing's switch (0048c650; its table 0048cb78)
	OID_CTree = 7,
	OID_CSpawnerThing = 19,
	OID_CBuilding = 25,
	OID_CCutscene = 26,
	OID_CWall = 33,
	OID_CSphereTrigger = 36,
	OID_CLevelScriptThing = 27,	// LevelScriptThing.h's _GetClassID (004bfb90)
	OID_CSafeSide = 37,			// SafeSide.h's _GetClassID (004bfc00)
	OID_CSpawnPoint = 41,
};

#endif
