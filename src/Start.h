// Start.h: a player's start position (Stuart's Start.h is not in his release). RTTI: CStart (vtable 005df2ec) is a
// CComplexThing; its object id is 15 (CGame::PostLoadProcess spawns one, 0046d119). Start.obj, 004eabd0..004eb1cb:
// CSpawnPoint's twin with the spawned battle engine kept in an active reader.

#ifndef START_H
#define START_H

#include "thing.h"
#include "InitThing.h"
#include "activereader.h"

class CBattleEngine;

// 0x44c bytes (SpawnThing allocates it, 004bf2ff, and calls the constructor out of line, 004bf324).
DECLARE_THING_CLASS(CStart, CComplexThing)
public:
	CStart();													// 004eacc0
	~CStart();													// 004ead90
	virtual void	Init(CInitThing *init);						// 004eae10
	UINT			GetPlayerNumber() { return mPlayerNumber; }
	CBattleEngine	*GetPlayerObject() { return mPlayerObject.ToRead(); }
	BOOL			Available();								// 004eb130
	CBattleEngine	*SpawnBattleEngine(BOOL effects);			// 004eaf20

	CActiveReader<CBattleEngine>	mPlayerObject;				// +0x7c
	UINT			mPlayerNumber;								// +0x80
	CBattleEngineInitThing	mInit;								// +0x84
};

#endif
