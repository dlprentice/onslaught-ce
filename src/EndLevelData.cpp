#include "common.h"
#include "EndLevelData.h"
#include "debuglog.h"



CEndLevelData END_LEVEL_DATA ;


//******************************************************************************************
CEndLevelData::CEndLevelData()
{
	mRanking=-1;
	mWorldFinished = 100;
	mFinalState = GAME_STATE_LEVEL_LOST;
	mThingsKilled.SetAll(0);

}

//******************************************************************************************
BOOL	CEndLevelData::IsAllSecondaryObjectivesComplete()	// FUNCTION: 0x004496e0
{
	BOOL res = TRUE ;
	SINT c0;
	BOOL is_set = FALSE ;

	for (c0 = 0; c0 < mSecondaryObjectives.Size(); c0 ++)
	{
		if (mSecondaryObjectives[c0].GetStatus() == MOS_COMPLETE ||
			mSecondaryObjectives[c0].GetStatus() == MOS_FAILED )
		{
			is_set = TRUE ;
		}

		if (mSecondaryObjectives[c0].GetStatus() == MOS_FAILED)
		{
			res = FALSE ;
		}
	}


	if (is_set == FALSE)
	{
		LOG.AddMessage("ERROR: No secondary objectives in call to 'IsAllSecondaryObjectivesComplete'") ;
		return FALSE ;
	}

	return res ;
}

// FUNCTION: 0x00449580 init:ZERO_FVECTOR
// FUNCTION: 0x004495a0 init:ID_FMATRIX
// FUNCTION: 0x00449670 init:END_LEVEL_DATA
