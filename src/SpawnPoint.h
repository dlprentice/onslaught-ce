// SpawnPoint.h: a respawn point (Stuart's header is not in his release; only what matched files need). Like CStart it
// is a CComplexThing with a player number (CGame::RespawnPlayer 00470237).

#ifndef SPAWNPOINT_H
#define SPAWNPOINT_H

#include "thing.h"
#include "InitThing.h"

class CBattleEngine;

// 0x44c bytes, vtable 005dc9c8 (SpawnThing 004bf818: the initializer +0x80).
DECLARE_THING_CLASS(CSpawnPoint, CComplexThing)
public:
	virtual	void	Shutdown();									// 004e47c0
	virtual void	Init(CInitThing *init);						// 004e46c0
	virtual void	Activate() { mActive = TRUE; }
	virtual void	Deactivate() { mActive = FALSE; }
	UINT			GetPlayerNumber() { return mPlayerNumber; }
	BOOL			Available();								// 004e49f0
	CBattleEngine	*SpawnBattleEngine(BOOL a);					// 004e47e0

	UINT			mPlayerNumber;								// +0x7c
	CBattleEngineInitThing	mInit;								// +0x80
	BOOL			mActive;									// +0x448 (Activate 004bfca0)
};

#endif
