// MissionObjective.h: mission objectives (only what matched files need). CEndLevelData::IsAllSecondaryObjectivesComplete
// (004496e0) walks ten 8-byte objectives from +0x4d0, right after ten primary ones (+0x480), testing a state of 1 or
// 2 in the first dword; the state's meaning and the second dword are open.

#ifndef MISSIONOBJECTIVE_H
#define MISSIONOBJECTIVE_H

#define MAX_PRIMARY_OBJECTIVES		10
#define MAX_SECONDARY_OBJECTIVES	10

// Stuart's names (game.h's SetPrimaryObjectiveComplete and friends); the retail tests treat 1 and 2 as settled.
enum EMissionObjectiveState
{
	MOS_NOT_DEFINED = 0,
	MOS_COMPLETE,
	MOS_FAILED
};

class CMissionObjective
{
public:
	CMissionObjective() { mState = MOS_NOT_DEFINED; mStringId = -1; }	// CGame's constructor (0046c22a)
	void	Set(int state, int string_id) { mState = state; mStringId = string_id; }
	int		GetStatus() { return mState; }
	int		GetStringID() { return mStringId; }
	BOOL	IsComplete() { return mState == MOS_COMPLETE; }	// CGame::DrawGameStuff (00471f86: sete)

	int		mState;				// +0x00
	int		mStringId;			// +0x04
};

#endif
