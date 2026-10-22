#ifndef	END_LEVEL_DATA_H
#define	END_LEVEL_DATA_H

#include "Array.h"
#include "game.h"
#include "MissionObjective.h"

// this class is used to pass information from the game to the front end.


// MUST NOT HAVE ANY VIRTUAL STUFF OR POINTERS IN IT
class CEndLevelData
{
public:

	CEndLevelData();
	
	CSArray<BOOL, BASE_THINGS_EXISTS_SIZE>	mBaseThingsLeft;  // base things which are alive
	CSArray<CMissionObjective, MAX_PRIMARY_OBJECTIVES>	mPrimaryObjectives ;
	CSArray<CMissionObjective, MAX_SECONDARY_OBJECTIVES> mSecondaryObjectives ;

	int				mWorldFinished;
	EGameState		mFinalState;
	float			mRanking;		// from 0..1
	SINT			mScore;
	float			mTimeTaken;
	int				mLevelLostReason;	// 0 if a string has not been defined

	BOOL			IsAllSecondaryObjectivesComplete() ;

	CSArray<int, TK_TOTAL>	mThingsKilled;
	CSArray<int, MAX_CAREER_SLOTS> mSlots;
};

// this is the singleton of this class to be used by front end
extern CEndLevelData END_LEVEL_DATA ; 

#endif
